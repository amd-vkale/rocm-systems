// Range replay validation tool for XSBench / RSBench.
//
//   RR_PASSES       total passes including the application's own run (default 4)
//   RR_HOST_REPLAY  1 = host replay (sigsetjmp), 0 = packet replay (default 0)
//   RR_COUNTERS     comma list; pass p collects counter [p % n] for every dispatch in the range
//   RR_OUT          append one JSON line per range here (default stderr only)

#include <rocprofiler-sdk/counters.h>
#include <rocprofiler-sdk/dispatch_counting_service.h>
#include <rocprofiler-sdk/experimental/range_replay.h>
#include <rocprofiler-sdk/fwd.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#define RC(call)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        auto _s = (call);                                                                          \
        if(_s != ROCPROFILER_STATUS_SUCCESS)                                                       \
        {                                                                                          \
            fprintf(stderr, "[rr-tool] %s: %s\n", #call, rocprofiler_get_status_string(_s));       \
            std::abort();                                                                          \
        }                                                                                          \
    } while(0)

namespace
{
using clock_type = std::chrono::steady_clock;

const char*
status_name(rocprofiler_range_replay_status_t s)
{
    switch(s)
    {
        case ROCPROFILER_RANGE_REPLAY_STATUS_REPLAYED: return "REPLAYED";
        case ROCPROFILER_RANGE_REPLAY_STATUS_NO_DISPATCH: return "NO_DISPATCH";
        case ROCPROFILER_RANGE_REPLAY_STATUS_NO_PASS_COUNT: return "NO_PASS_COUNT";
        case ROCPROFILER_RANGE_REPLAY_STATUS_MULTI_QUEUE: return "MULTI_QUEUE";
        case ROCPROFILER_RANGE_REPLAY_STATUS_MULTI_AGENT: return "MULTI_AGENT";
        case ROCPROFILER_RANGE_REPLAY_STATUS_GRAPH_LAUNCH: return "GRAPH_LAUNCH";
        case ROCPROFILER_RANGE_REPLAY_STATUS_UNKNOWN_KERNARG_SIZE: return "UNKNOWN_KERNARG_SIZE";
        case ROCPROFILER_RANGE_REPLAY_STATUS_MEMORY_COPY_IN_RANGE: return "MEMORY_COPY_IN_RANGE";
        case ROCPROFILER_RANGE_REPLAY_STATUS_CONCURRENT_DISPATCH: return "CONCURRENT_DISPATCH";
        case ROCPROFILER_RANGE_REPLAY_STATUS_PROGRAM_TOO_LARGE: return "PROGRAM_TOO_LARGE";
        case ROCPROFILER_RANGE_REPLAY_STATUS_SNAPSHOT_FAILED: return "SNAPSHOT_FAILED";
        case ROCPROFILER_RANGE_REPLAY_STATUS_STAGING_FAILED: return "STAGING_FAILED";
        case ROCPROFILER_RANGE_REPLAY_STATUS_ALLOCATION_CHANGED_IN_RANGE:
            return "ALLOCATION_CHANGED_IN_RANGE";
        case ROCPROFILER_RANGE_REPLAY_STATUS_UNSUPPORTED_QUEUE_PATH:
            return "UNSUPPORTED_QUEUE_PATH";
        case ROCPROFILER_RANGE_REPLAY_STATUS_CODE_OBJECT_CHANGED_IN_RANGE:
            return "CODE_OBJECT_CHANGED_IN_RANGE";
        case ROCPROFILER_RANGE_REPLAY_STATUS_HOST_JMP_NOT_SET: return "HOST_JMP_NOT_SET";
        case ROCPROFILER_RANGE_REPLAY_STATUS_LAST: break;
    }
    return "<unknown>";
}

uint64_t                 g_passes = 4;
bool                     g_host   = false;
std::vector<std::string> g_counter_names{};

rocprofiler_context_id_t g_replay_ctx{0};
rocprofiler_context_id_t g_counter_ctx{0};

std::atomic<bool>     g_in_range{false};
std::atomic<uint64_t> g_cur_pass{0};

std::mutex                   g_mtx{};
clock_type::time_point       g_t_config{};
std::vector<double>          g_pass_ms{};  // pass p start -> pass p+1 start (or close)
clock_type::time_point       g_t_last{};
std::map<uint64_t, double>   g_counter_sum{};      // pass -> summed counter value
std::map<uint64_t, uint64_t> g_counter_records{};  // pass -> dispatch records
std::map<uint64_t, uint64_t> g_dispatches_seen{};  // pass -> dispatches seen by counting svc
std::map<uint64_t, uint64_t> g_late_records{};     // pass -> records delivered after CLOSE

uint64_t pass_count(uint64_t, rocprofiler_user_data_t) { return g_passes; }

std::string
counter_for_pass(uint64_t pass)
{
    if(g_counter_names.empty()) return {};
    return g_counter_names[pass % g_counter_names.size()];
}

void
range_replay_cb(rocprofiler_callback_tracing_record_t record, rocprofiler_user_data_t*, void*)
{
    if(record.kind != ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY) return;
    if(record.phase != ROCPROFILER_CALLBACK_PHASE_ENTER) return;
    auto* d   = static_cast<rocprofiler_callback_tracing_range_replay_data_t*>(record.payload);
    auto  now = clock_type::now();

    if(record.operation == ROCPROFILER_RANGE_REPLAY_CONFIG)
    {
        d->pass_count_cb = pass_count;
        d->host_replay   = g_host ? 1 : 0;
        std::lock_guard<std::mutex> lk{g_mtx};
        g_t_config = now;
        g_t_last   = now;
        g_pass_ms.clear();
        g_counter_sum.clear();
        g_counter_records.clear();
        g_dispatches_seen.clear();
        g_cur_pass.store(0);
        g_in_range.store(true);
        fprintf(stderr,
                "[rr-tool] CONFIG range=%lu passes=%lu mode=%s\n",
                (unsigned long) d->range_id,
                (unsigned long) g_passes,
                g_host ? "host" : "packet");
    }
    else if(record.operation == ROCPROFILER_RANGE_REPLAY_PASS)
    {
        {
            std::lock_guard<std::mutex> lk{g_mtx};
            g_pass_ms.push_back(std::chrono::duration<double, std::milli>(now - g_t_last).count());
            g_t_last = now;
        }
        g_cur_pass.store(d->current_pass);
        fprintf(stderr,
                "[rr-tool] PASS %lu/%lu dispatches=%lu\n",
                (unsigned long) d->current_pass,
                (unsigned long) d->total_passes,
                (unsigned long) d->dispatch_count);
    }
    else if(record.operation == ROCPROFILER_RANGE_REPLAY_CLOSE)
    {
        g_in_range.store(false);
        std::lock_guard<std::mutex> lk{g_mtx};
        g_pass_ms.push_back(std::chrono::duration<double, std::milli>(now - g_t_last).count());
        double total_ms = std::chrono::duration<double, std::milli>(now - g_t_config).count();

        std::ostringstream js{};
        js << "{\"range_id\":" << d->range_id << ",\"mode\":\"" << (g_host ? "host" : "packet")
           << "\",\"passes_requested\":" << g_passes << ",\"status\":\"" << status_name(d->status)
           << "\",\"dispatch_count\":" << d->dispatch_count
           << ",\"divergence_count\":" << d->divergence_count << ",\"range_ms\":" << total_ms
           << ",\"pass_ms\":[";
        for(size_t i = 0; i < g_pass_ms.size(); ++i)
            js << (i ? "," : "") << g_pass_ms[i];
        js << "],\"counters\":[";
        bool first = true;
        for(auto& [pass, seen] : g_dispatches_seen)
        {
            js << (first ? "" : ",") << "{\"pass\":" << pass << ",\"counter\":\""
               << counter_for_pass(pass) << "\",\"records\":" << g_counter_records[pass]
               << ",\"dispatches\":" << seen << ",\"sum\":" << g_counter_sum[pass] << "}";
            first = false;
        }
        js << "]}";
        fprintf(stderr, "[rr-tool] CLOSE %s\n", js.str().c_str());
        if(const char* out = std::getenv("RR_OUT"); out && *out)
        {
            if(FILE* f = fopen(out, "a"))
            {
                fprintf(f, "%s\n", js.str().c_str());
                fclose(f);
            }
        }
    }
}

rocprofiler_counter_config_id_t
config_for(rocprofiler_agent_id_t agent, const std::string& name)
{
    static std::mutex                                                                  m{};
    static std::map<std::pair<uint64_t, std::string>, rocprofiler_counter_config_id_t> cache{};
    std::lock_guard<std::mutex>                                                        lk{m};
    auto key = std::make_pair(agent.handle, name);
    if(auto it = cache.find(key); it != cache.end()) return it->second;

    std::vector<rocprofiler_counter_id_t> all{};
    RC(rocprofiler_iterate_agent_supported_counters(
        agent,
        [](rocprofiler_agent_id_t, rocprofiler_counter_id_t* cs, size_t n, void* ud) {
            auto* v = static_cast<std::vector<rocprofiler_counter_id_t>*>(ud);
            for(size_t i = 0; i < n; ++i)
                v->push_back(cs[i]);
            return ROCPROFILER_STATUS_SUCCESS;
        },
        &all));
    std::vector<rocprofiler_counter_id_t> want{};
    for(auto c : all)
    {
        rocprofiler_counter_info_v0_t info{};
        RC(rocprofiler_query_counter_info(c, ROCPROFILER_COUNTER_INFO_VERSION_0, &info));
        if(info.name && name == info.name) want.push_back(c);
    }
    if(want.empty())
    {
        fprintf(stderr, "[rr-tool] counter %s not found\n", name.c_str());
        std::abort();
    }
    rocprofiler_counter_config_id_t cfg{.handle = 0};
    RC(rocprofiler_create_counter_config(agent, want.data(), want.size(), &cfg));
    cache.emplace(key, cfg);
    return cfg;
}

// Dispatches outside any range, keyed by kernel id: (records, summed value). Filled only with
// RR_COUNT_ALWAYS=1, using the first RR_COUNTERS entry.
bool                                            g_count_always = false;
constexpr uint64_t                              kOutsideRange  = 1ULL << 40;
std::map<uint64_t, std::pair<uint64_t, double>> g_outside{};

void
counter_dispatch_cb(rocprofiler_dispatch_counting_service_data_t d,
                    rocprofiler_counter_config_id_t*             config,
                    rocprofiler_user_data_t*                     user_data,
                    void*)
{
    if(!g_in_range.load())
    {
        if(!g_count_always) return;
        user_data->value = kOutsideRange + d.dispatch_info.kernel_id;
        *config          = config_for(d.dispatch_info.agent_id, g_counter_names.front());
        return;
    }
    auto pass        = g_cur_pass.load();
    user_data->value = pass + 1;
    {
        std::lock_guard<std::mutex> lk{g_mtx};
        g_dispatches_seen[pass]++;
    }
    *config = config_for(d.dispatch_info.agent_id, counter_for_pass(pass));
}

void
counter_record_cb(rocprofiler_dispatch_counting_service_data_t,
                  rocprofiler_counter_record_t* records,
                  size_t                        n,
                  rocprofiler_user_data_t       user_data,
                  void*)
{
    if(user_data.value == 0) return;
    std::lock_guard<std::mutex> lk{g_mtx};
    if(user_data.value >= kOutsideRange)
    {
        auto& slot = g_outside[user_data.value - kOutsideRange];
        slot.first++;
        for(size_t i = 0; i < n; ++i)
            slot.second += records[i].counter_value;
        return;
    }
    auto pass = user_data.value - 1;
    if(!g_in_range.load()) g_late_records[pass]++;
    g_counter_records[pass]++;
    for(size_t i = 0; i < n; ++i)
        g_counter_sum[pass] += records[i].counter_value;
}

int
tool_init(rocprofiler_client_finalize_t, void*)
{
    if(const char* p = std::getenv("RR_PASSES")) g_passes = std::strtoull(p, nullptr, 10);
    if(const char* h = std::getenv("RR_HOST_REPLAY")) g_host = (h[0] != '\0' && h[0] != '0');
    if(const char* a = std::getenv("RR_COUNT_ALWAYS")) g_count_always = (a[0] == '1');
    if(const char* c = std::getenv("RR_COUNTERS"); c && *c)
    {
        std::stringstream ss{c};
        std::string       tok{};
        while(std::getline(ss, tok, ','))
            if(!tok.empty()) g_counter_names.push_back(tok);
    }

    RC(rocprofiler_create_context(&g_replay_ctx));
    RC(rocprofiler_configure_callback_tracing_service(g_replay_ctx,
                                                      ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY,
                                                      nullptr,
                                                      0,
                                                      range_replay_cb,
                                                      nullptr));
    if(!g_counter_names.empty())
    {
        RC(rocprofiler_create_context(&g_counter_ctx));
        RC(rocprofiler_configure_callback_dispatch_counting_service(
            g_counter_ctx, counter_dispatch_cb, nullptr, counter_record_cb, nullptr));
        RC(rocprofiler_start_context(g_counter_ctx));
    }
    RC(rocprofiler_start_context(g_replay_ctx));
    return 0;
}

void
tool_fini(void*)
{
    std::lock_guard<std::mutex> lk{g_mtx};
    std::ostringstream          js{};
    js << "{\"late_records\":[";
    bool first = true;
    for(auto& [pass, n] : g_late_records)
    {
        js << (first ? "" : ",") << "{\"pass\":" << pass << ",\"records\":" << n
           << ",\"sum\":" << g_counter_sum[pass] << "}";
        first = false;
    }
    js << "],\"outside_range\":[";
    first = true;
    for(auto& [kid, v] : g_outside)
    {
        js << (first ? "" : ",") << "{\"kernel_id\":" << kid << ",\"records\":" << v.first
           << ",\"sum\":" << v.second << "}";
        first = false;
    }
    js << "]}";
    fprintf(stderr, "[rr-tool] FINI %s\n", js.str().c_str());
    if(const char* out = std::getenv("RR_OUT"); out && *out)
    {
        if(FILE* f = fopen(out, "a"))
        {
            fprintf(f, "%s\n", js.str().c_str());
            fclose(f);
        }
    }
}
}  // namespace

extern "C" rocprofiler_tool_configure_result_t*
rocprofiler_configure(uint32_t, const char*, uint32_t priority, rocprofiler_client_id_t* id)
{
    if(priority > 0) return nullptr;
    id->name        = "rr-validate-tool";
    static auto cfg = rocprofiler_tool_configure_result_t{
        sizeof(rocprofiler_tool_configure_result_t), &tool_init, &tool_fini, nullptr};
    return &cfg;
}

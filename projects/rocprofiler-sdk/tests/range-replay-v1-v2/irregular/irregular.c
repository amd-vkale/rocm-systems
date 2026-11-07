// Irregular-access OpenMP offload kernels in a replayable range.
//   irregular <spmv|gups|hist|gather|chase> [scale]
// Every benchmark keeps its inputs mapped on the device, runs ITERS kernels inside range 1, and
// checks the result against a serial host reference. See rr_rt.h for RR_APP_MODE / RR_RANGE.
#include "rr_rt.h"

#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

static inline uint64_t
mix64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static uint64_t
hash_bytes(const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*) p;
    uint64_t             h = 1469598103934665603ULL;
    for(size_t i = 0; i < n; ++i)
        h = (h ^ b[i]) * 1099511628211ULL;
    return h;
}

static int
report(const char* name, double range_ms, double kern_ms, uint64_t hash, double err, int ok)
{
    printf("[bench] name=%s lang=c range=%s mode=%s range_ms=%.3f kernels_ms=%.3f hash=%016llx "
           "err=%.3g valid=%d\n",
           name, rr_range_is_full() ? "full" : "kernel", getenv("RR_APP_MODE") ? getenv("RR_APP_MODE") : "none",
           range_ms, kern_ms, (unsigned long long) hash, err, ok);
    return ok ? 0 : 1;
}

enum
{
    ITERS = 8
};

// ---------------------------------------------------------------------------------------------
// CSR SpMV, random columns: y = A x, then x = y / ||row|| bound, ITERS times.
static int
run_spmv(long scale)
{
    const long rows = 1L << 20, k = 16 * scale, nnz = rows * k;
    int*       col  = malloc(nnz * sizeof(int));
    double*    val  = malloc(nnz * sizeof(double));
    double*    x    = malloc(rows * sizeof(double));
    double*    y    = malloc(rows * sizeof(double));
    double*    rx   = malloc(rows * sizeof(double));
    double*    ry   = malloc(rows * sizeof(double));
    for(long i = 0; i < nnz; ++i)
    {
        col[i] = (int) (mix64(i) % rows);
        val[i] = 1.0 / (double) k * (0.5 + (double) (mix64(i + nnz) % 1000) / 1000.0);
    }
    for(long i = 0; i < rows; ++i)
        x[i] = 1.0 + (double) (i % 7);
    memcpy(rx, x, rows * sizeof(double));

#pragma omp target enter data map(to : col[0 : nnz], val[0 : nnz], x[0 : rows]) map(alloc : y[0 : rows])
    const int full = rr_range_is_full();
    if(full) rr_track(x, rows * sizeof(*x));
    double    t0   = now_ms(), tk0 = 0, tk1 = 0;
    RR_BEGIN(1);
    if(full)
    {
#pragma omp target update to(x[0 : rows])
    }
    tk0 = now_ms();
    for(int it = 0; it < ITERS; ++it)
    {
#pragma omp target teams distribute parallel for
        for(long r = 0; r < rows; ++r)
        {
            double s = 0;
            for(long j = r * k; j < (r + 1) * k; ++j)
                s += val[j] * x[col[j]];
            y[r] = s;
        }
#pragma omp target teams distribute parallel for
        for(long r = 0; r < rows; ++r)
            x[r] = 0.5 * y[r] + 0.25;
    }
    tk1 = now_ms();
    if(full)
    {
#pragma omp target update from(x[0 : rows])
    }
    RR_END();
    double t1 = now_ms();
    if(!full)
    {
#pragma omp target update from(x[0 : rows])
    }

    for(int it = 0; it < ITERS; ++it)
    {
        for(long r = 0; r < rows; ++r)
        {
            double s = 0;
            for(long j = r * k; j < (r + 1) * k; ++j)
                s += val[j] * rx[col[j]];
            ry[r] = s;
        }
        for(long r = 0; r < rows; ++r)
            rx[r] = 0.5 * ry[r] + 0.25;
    }
    double err = 0;
    for(long r = 0; r < rows; ++r)
        err = fmax(err, fabs(x[r] - rx[r]) / fmax(1e-30, fabs(rx[r])));
#pragma omp target exit data map(delete : col[0 : nnz], val[0 : nnz], x[0 : rows], y[0 : rows])
    return report("spmv", t1 - t0, tk1 - tk0, hash_bytes(x, rows * sizeof(double)), err, err < 1e-12);
}

// ---------------------------------------------------------------------------------------------
// GUPS: random XOR updates to a 2^24-entry table, atomic so the result is order independent.
static int
run_gups(long scale)
{
    const long n = (1L << 24) * scale, updates = 4 * n;
    uint64_t*  t = malloc(n * sizeof(uint64_t));
    uint64_t*  r = malloc(n * sizeof(uint64_t));
    for(long i = 0; i < n; ++i)
        t[i] = (uint64_t) i;
    memcpy(r, t, n * sizeof(uint64_t));

#pragma omp target enter data map(to : t[0 : n])
    const int full = rr_range_is_full();
    if(full) rr_track(t, n * sizeof(*t));
    double    t0   = now_ms(), tk0 = 0, tk1 = 0;
    RR_BEGIN(1);
    if(full)
    {
#pragma omp target update to(t[0 : n])
    }
    tk0 = now_ms();
    for(int it = 0; it < ITERS; ++it)
    {
#pragma omp target teams distribute parallel for
        for(long u = 0; u < updates; ++u)
        {
            const uint64_t v = mix64((uint64_t) u * ITERS + it);
#pragma omp atomic update
            t[v % n] ^= v;
        }
    }
    tk1 = now_ms();
    if(full)
    {
#pragma omp target update from(t[0 : n])
    }
    RR_END();
    double t1 = now_ms();
    if(!full)
    {
#pragma omp target update from(t[0 : n])
    }

    for(int it = 0; it < ITERS; ++it)
        for(long u = 0; u < updates; ++u)
        {
            const uint64_t v = mix64((uint64_t) u * ITERS + it);
            r[v % n] ^= v;
        }
    long bad = 0;
    for(long i = 0; i < n; ++i)
        bad += t[i] != r[i];
#pragma omp target exit data map(delete : t[0 : n])
    return report("gups", t1 - t0, tk1 - tk0, hash_bytes(t, n * sizeof(uint64_t)), (double) bad,
                  bad == 0);
}

// ---------------------------------------------------------------------------------------------
// Histogram: 2^26 skewed keys into 4096 bins with atomic increments.
static int
run_hist(long scale)
{
    const long n = (1L << 26) * scale, bins = 4096;
    uint32_t*  key = malloc(n * sizeof(uint32_t));
    uint64_t*  h   = calloc(bins, sizeof(uint64_t));
    uint64_t*  rh  = calloc(bins, sizeof(uint64_t));
    for(long i = 0; i < n; ++i)
    {
        const uint64_t m = mix64(i);
        key[i]           = (uint32_t) ((m & 1) ? (m >> 8) % 64 : (m >> 8) % bins);
    }

#pragma omp target enter data map(to : key[0 : n], h[0 : bins])
    const int full = rr_range_is_full();
    if(full) rr_track(h, bins * sizeof(*h));
    double    t0   = now_ms(), tk0 = 0, tk1 = 0;
    RR_BEGIN(1);
    if(full)
    {
#pragma omp target update to(h[0 : bins])
    }
    tk0 = now_ms();
    for(int it = 0; it < ITERS; ++it)
    {
#pragma omp target teams distribute parallel for
        for(long i = 0; i < n; ++i)
        {
#pragma omp atomic update
            h[(key[i] + it) % bins] += 1;
        }
    }
    tk1 = now_ms();
    if(full)
    {
#pragma omp target update from(h[0 : bins])
    }
    RR_END();
    double t1 = now_ms();
    if(!full)
    {
#pragma omp target update from(h[0 : bins])
    }

    for(int it = 0; it < ITERS; ++it)
        for(long i = 0; i < n; ++i)
            rh[(key[i] + it) % bins] += 1;
    long bad = 0;
    for(long b = 0; b < bins; ++b)
        bad += h[b] != rh[b];
#pragma omp target exit data map(delete : key[0 : n], h[0 : bins])
    return report("hist", t1 - t0, tk1 - tk0, hash_bytes(h, bins * sizeof(uint64_t)), (double) bad,
                  bad == 0);
}

// ---------------------------------------------------------------------------------------------
// Gather/scatter: y[perm[i]] = 0.5 * y[perm[i]] + x[idx[i]], with perm a permutation.
static int
run_gather(long scale)
{
    const long n    = (1L << 24) * scale;
    long*      idx  = malloc(n * sizeof(long));
    long*      perm = malloc(n * sizeof(long));
    double*    x    = malloc(n * sizeof(double));
    double*    y    = malloc(n * sizeof(double));
    double*    ry   = malloc(n * sizeof(double));
    for(long i = 0; i < n; ++i)
    {
        idx[i]  = (long) (mix64(i) % n);
        perm[i] = i;
        x[i]    = (double) (i % 101) * 0.01;
        y[i]    = 1.0;
    }
    for(long i = n - 1; i > 0; --i)
    {
        const long j = (long) (mix64(i + 7 * n) % (uint64_t) (i + 1));
        const long t = perm[i];
        perm[i]      = perm[j];
        perm[j]      = t;
    }
    memcpy(ry, y, n * sizeof(double));

#pragma omp target enter data map(to : idx[0 : n], perm[0 : n], x[0 : n], y[0 : n])
    const int full = rr_range_is_full();
    if(full) rr_track(y, n * sizeof(*y));
    double    t0   = now_ms(), tk0 = 0, tk1 = 0;
    RR_BEGIN(1);
    if(full)
    {
#pragma omp target update to(y[0 : n])
    }
    tk0 = now_ms();
    for(int it = 0; it < ITERS; ++it)
    {
#pragma omp target teams distribute parallel for
        for(long i = 0; i < n; ++i)
            y[perm[i]] = 0.5 * y[perm[i]] + x[idx[i]];
    }
    tk1 = now_ms();
    if(full)
    {
#pragma omp target update from(y[0 : n])
    }
    RR_END();
    double t1 = now_ms();
    if(!full)
    {
#pragma omp target update from(y[0 : n])
    }

    for(int it = 0; it < ITERS; ++it)
        for(long i = 0; i < n; ++i)
            ry[perm[i]] = 0.5 * ry[perm[i]] + x[idx[i]];
    double err = 0;
    for(long i = 0; i < n; ++i)
        err = fmax(err, fabs(y[i] - ry[i]));
#pragma omp target exit data map(delete : idx[0 : n], perm[0 : n], x[0 : n], y[0 : n])
    return report("gather", t1 - t0, tk1 - tk0, hash_bytes(y, n * sizeof(double)), err, err < 1e-12);
}

// ---------------------------------------------------------------------------------------------
// Pointer chase: 2^16 walkers follow a single random cycle of 2^24 nodes, 256 steps per kernel;
// each walker's position carries over to the next kernel.
static int
run_chase(long scale)
{
    const long n = (1L << 24) * scale, walkers = 1L << 16, steps = 256;
    int*       next = malloc(n * sizeof(int));
    int*       pos  = malloc(walkers * sizeof(int));
    int*       rpos = malloc(walkers * sizeof(int));
    for(long i = 0; i < n; ++i)
        next[i] = (int) i;
    for(long i = n - 1; i > 0; --i)
    {
        const long j = (long) (mix64(i) % (uint64_t) i);
        const int  t = next[i];
        next[i]      = next[j];
        next[j]      = t;
    }
    for(long w = 0; w < walkers; ++w)
        pos[w] = (int) (w * (n / walkers));
    memcpy(rpos, pos, walkers * sizeof(int));

#pragma omp target enter data map(to : next[0 : n], pos[0 : walkers])
    const int full = rr_range_is_full();
    if(full) rr_track(pos, walkers * sizeof(*pos));
    double    t0   = now_ms(), tk0 = 0, tk1 = 0;
    RR_BEGIN(1);
    if(full)
    {
#pragma omp target update to(pos[0 : walkers])
    }
    tk0 = now_ms();
    for(int it = 0; it < ITERS; ++it)
    {
#pragma omp target teams distribute parallel for
        for(long w = 0; w < walkers; ++w)
        {
            int p = pos[w];
            for(long s = 0; s < steps; ++s)
                p = next[p];
            pos[w] = p;
        }
    }
    tk1 = now_ms();
    if(full)
    {
#pragma omp target update from(pos[0 : walkers])
    }
    RR_END();
    double t1 = now_ms();
    if(!full)
    {
#pragma omp target update from(pos[0 : walkers])
    }

    for(int it = 0; it < ITERS; ++it)
        for(long w = 0; w < walkers; ++w)
        {
            int p = rpos[w];
            for(long s = 0; s < steps; ++s)
                p = next[p];
            rpos[w] = p;
        }
    long bad = 0;
    for(long w = 0; w < walkers; ++w)
        bad += pos[w] != rpos[w];
#pragma omp target exit data map(delete : next[0 : n], pos[0 : walkers])
    return report("chase", t1 - t0, tk1 - tk0, hash_bytes(pos, walkers * sizeof(int)), (double) bad,
                  bad == 0);
}

int
main(int argc, char** argv)
{
    if(argc < 2)
    {
        fprintf(stderr, "usage: %s spmv|gups|hist|gather|chase [scale]\n", argv[0]);
        return 2;
    }
    const long scale = argc > 2 ? atol(argv[2]) : 1;
    // Initialize the device and load the image before any range.
#pragma omp target
    {
    }
    if(!strcmp(argv[1], "spmv")) return run_spmv(scale);
    if(!strcmp(argv[1], "gups")) return run_gups(scale);
    if(!strcmp(argv[1], "hist")) return run_hist(scale);
    if(!strcmp(argv[1], "gather")) return run_gather(scale);
    if(!strcmp(argv[1], "chase")) return run_chase(scale);
    fprintf(stderr, "unknown benchmark %s\n", argv[1]);
    return 2;
}

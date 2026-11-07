// Harnesses for range_state.cpp's qualified_kernel_name():
//   * LLVMFuzzerTestOneInput (built with -fsanitize=fuzzer,address,undefined) checks that no input
//     reads out of bounds or overflows into UB, and that the result is never longer than the input
//     plus the "::" separators it inserts;
//   * main() in differential mode (built without -fsanitize=fuzzer) reads mangled names on stdin
//     and prints "<mangled>\t<qualified>" for comparison against llvm-cxxfilt.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "qkn_extracted.inc"

#ifdef QKN_FUZZ
extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const auto input = std::string_view{reinterpret_cast<const char*>(data), size};
    const auto out   = qualified_kernel_name(input);
    // Each source name contributes at most its own bytes plus a two-byte separator, and is
    // preceded by at least one length digit, so the output can never exceed 2x the input.
    if(out.size() > 2 * size + 2) std::abort();
    return 0;
}
#else
int
main()
{
    std::string line;
    while(std::getline(std::cin, line))
        std::cout << line << '\t' << qualified_kernel_name(line) << '\n';
    return 0;
}
#endif

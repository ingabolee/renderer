// benchmarks/benchmark_main.cpp
// Benchmark entry point: delegates to CLI's run_benchmark.
#include "../src/cli.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    // Reuse CLI infrastructure for clean output.
    // Default: run benchmark with 1000 nodes, 50 iterations.
    std::vector<const char*> args;
    args.push_back("recalc");
    args.push_back("benchmark");
    args.push_back("--color=auto");
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    return incremental::cli::parse_and_run(
        static_cast<int>(args.size()),
        const_cast<char**>(args.data()),
        std::cout, std::cerr);
}

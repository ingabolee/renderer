// src/cli.hpp
// CLI command dispatcher declarations.
#pragma once

#include <string>
#include <vector>
#include <ostream>

namespace incremental::cli {

// ── Parsed CLI options ────────────────────────────────────────────────────────
struct Options {
    std::string command;            // "demo", "benchmark", ""

    // demo options
    std::string scenario{"spreadsheet"};

    // benchmark options
    std::size_t nodes{1000};
    std::size_t iterations{50};
    std::string csv_output;
    unsigned seed{42};

    // global
    bool help{false};
    bool version_flag{false};
    bool quiet{false};
    bool verbose{false};
};

// ── Command handlers ──────────────────────────────────────────────────────────
int run_demo(const Options& opts, std::ostream& out);
int run_benchmark(const Options& opts, std::ostream& out);
void print_help(std::ostream& out);
void print_version(std::ostream& out);

// ── Argument parser ───────────────────────────────────────────────────────────
/// Parse argc/argv and return Options. Returns nullptr on parse error.
/// Writes error to err stream. Returns exit code via result.
int parse_and_run(int argc, char* argv[], std::ostream& out, std::ostream& err);

} // namespace incremental::cli

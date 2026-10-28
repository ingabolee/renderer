// src/main.cpp
// Entry point for the recalc CLI executable.
#include "cli.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    return incremental::cli::parse_and_run(argc, argv, std::cout, std::cerr);
}

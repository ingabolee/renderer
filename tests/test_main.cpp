// tests/test_main.cpp
// Minimal test harness: runs tests registered via TEST() macro.
// Usage: incremental_tests --test-case "suite.name"
//        incremental_tests            (runs all tests)
#include "test_main.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--test-case" && i + 1 < argc) {
            filter = argv[++i];
        } else if (arg.starts_with("--test-case=")) {
            filter = std::string(arg.substr(12));
        }
    }

    auto& registry = TestRegistry::instance();
    int passed = 0, failed = 0, skipped = 0;

    for (auto& [name, fn] : registry.tests()) {
        if (!filter.empty() && name != filter) {
            ++skipped;
            continue;
        }
        std::cout << "  [ RUN  ] " << name << '\n';
        try {
            fn();
            std::cout << "  [ PASS ] " << name << '\n';
            ++passed;
        } catch (const TestFailure& tf) {
            std::cout << "  [ FAIL ] " << name << ": " << tf.what() << '\n';
            ++failed;
        } catch (const std::exception& ex) {
            std::cout << "  [ EXCP ] " << name << ": " << ex.what() << '\n';
            ++failed;
        } catch (...) {
            std::cout << "  [ EXCP ] " << name << ": unknown exception\n";
            ++failed;
        }
    }

    if (filter.empty()) {
        std::cout << "\n  Results: " << passed << " passed, "
                  << failed << " failed, "
                  << skipped << " skipped\n";
    }
    return (failed > 0) ? 1 : 0;
}

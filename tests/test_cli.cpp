// tests/test_cli.cpp
// CLI integration tests: invoke parse_and_run and check results.
#include "test_main.hpp"
#include "../src/cli.hpp"
#include <sstream>

using namespace incremental::cli;

static int run_cli(std::vector<const char*> args,
                   std::string* out_str = nullptr,
                   std::string* err_str = nullptr) {
    std::ostringstream out, err;
    std::vector<char*> argv;
    // First arg is program name
    argv.push_back(const_cast<char*>("recalc"));
    for (auto a : args) argv.push_back(const_cast<char*>(a));
    int rc = parse_and_run(static_cast<int>(argv.size()), argv.data(), out, err);
    if (out_str) *out_str = out.str();
    if (err_str) *err_str = err.str();
    return rc;
}

TEST(cli, help) {
    std::string out;
    int rc = run_cli({"--help"}, &out);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(out.find("USAGE") != std::string::npos ||
                out.find("recalc") != std::string::npos);
}

TEST(cli, version) {
    std::string out;
    int rc = run_cli({"--version"}, &out);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(out.find("1.0.0") != std::string::npos);
}

TEST(cli, demo) {
    std::string out;
    int rc = run_cli({"demo", "--color=never"}, &out);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(out.find("Revenue") != std::string::npos);
}

TEST(cli, invalid_option) {
    std::string err;
    int rc = run_cli({"--nonexistent-option"}, nullptr, &err);
    ASSERT_NE(rc, 0);
    ASSERT_TRUE(err.find("unknown") != std::string::npos ||
                err.find("recalc") != std::string::npos);
}

TEST(cli, ascii_mode) {
    std::string out;
    int rc = run_cli({"demo", "--ascii", "--color=never"}, &out);
    ASSERT_EQ(rc, 0);
    // ASCII mode: no Unicode box chars in output
    ASSERT_TRUE(out.find("\u256D") == std::string::npos); // ╭ should not appear
}

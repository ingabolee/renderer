// tests/test_main.hpp
// Minimal test harness header: TEST() macro + registry + assertions.
#pragma once

#include <functional>
#include <string>
#include <vector>
#include <stdexcept>
#include <format>
#include <iostream>

// ── TestFailure exception ─────────────────────────────────────────────────────
struct TestFailure : std::runtime_error {
    explicit TestFailure(std::string msg) : std::runtime_error(std::move(msg)) {}
};

// ── Assertion macros ──────────────────────────────────────────────────────────
#define ASSERT_TRUE(expr) \
    do { if (!(expr)) throw TestFailure(std::format("{}:{} ASSERT_TRUE({}) failed", \
        __FILE__, __LINE__, #expr)); } while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) throw TestFailure(std::format("{}:{} ASSERT_FALSE({}) failed", \
        __FILE__, __LINE__, #expr)); } while(0)

#define ASSERT_EQ(a, b) \
    do { if (!((a) == (b))) throw TestFailure(std::format("{}:{} ASSERT_EQ: {} != {}", \
        __FILE__, __LINE__, (a), (b))); } while(0)

#define ASSERT_NE(a, b) \
    do { if ((a) == (b)) throw TestFailure(std::format("{}:{} ASSERT_NE: both equal", \
        __FILE__, __LINE__)); } while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { auto _d = (a)-(b); if (_d<0) _d=-_d; \
         if (_d > (eps)) throw TestFailure(std::format("{}:{} ASSERT_NEAR: |{} - {}| = {} > {}", \
             __FILE__, __LINE__, (double)(a), (double)(b), (double)_d, (double)(eps))); } while(0)

#define ASSERT_THROWS(expr, ExcType) \
    do { bool _threw = false; \
         try { (expr); } catch (const ExcType&) { _threw = true; } catch (...) {} \
         if (!_threw) throw TestFailure(std::format("{}:{} Expected {} to be thrown by: {}", \
             __FILE__, __LINE__, #ExcType, #expr)); } while(0)

#define ASSERT_THROWS_ANY(expr) \
    do { bool _threw = false; \
         try { (expr); } catch (...) { _threw = true; } \
         if (!_threw) throw TestFailure(std::format("{}:{} Expected an exception from: {}", \
             __FILE__, __LINE__, #expr)); } while(0)

#define ASSERT_NO_THROW(expr) \
    do { try { (expr); } \
         catch (const std::exception& _e) { \
             throw TestFailure(std::format("{}:{} Unexpected exception: {}", \
                 __FILE__, __LINE__, _e.what())); } \
         catch (...) { \
             throw TestFailure(std::format("{}:{} Unexpected unknown exception", \
                 __FILE__, __LINE__)); } } while(0)

// ── TestRegistry ──────────────────────────────────────────────────────────────
class TestRegistry {
public:
    using TestFn = std::function<void()>;

    static TestRegistry& instance() {
        static TestRegistry r;
        return r;
    }

    void add(std::string name, TestFn fn) {
        tests_.emplace_back(std::move(name), std::move(fn));
    }

    const std::vector<std::pair<std::string, TestFn>>& tests() const { return tests_; }

private:
    std::vector<std::pair<std::string, TestFn>> tests_;
};

// ── TEST macro ────────────────────────────────────────────────────────────────
struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        TestRegistry::instance().add(name, std::move(fn));
    }
};

#define TEST(suite, name) \
    static void test_##suite##_##name(); \
    static TestRegistrar reg_##suite##_##name{ #suite "." #name, test_##suite##_##name }; \
    static void test_##suite##_##name()

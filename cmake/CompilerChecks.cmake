# cmake/CompilerChecks.cmake
# Detect compiler capabilities and set project-wide defines.

include(CheckCXXSourceCompiles)

# ── Determine best available C++ standard ─────────────────────────────────────
# Request C++23; fall back gracefully when feature_test macros are missing.
# GCC 13 supports -std=c++23 (essentially C++23 + some C++26 paper acceptance).
# We label this "C++23" throughout the documentation.

message(STATUS "[incremental] Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "[incremental] C++ standard requested: ${CMAKE_CXX_STANDARD}")

# ── Feature probes ─────────────────────────────────────────────────────────────
check_cxx_source_compiles("
#include <expected>
int main(){ std::expected<int,int> e{1}; return e.value()-1; }
" INCREMENTAL_HAS_EXPECTED)

check_cxx_source_compiles("
#include <format>
#include <string>
int main(){ auto s = std::format(\"{}\", 42); return s.empty() ? 1 : 0; }
" INCREMENTAL_HAS_FORMAT)

check_cxx_source_compiles("
#include <ranges>
#include <vector>
int main(){ std::vector<int> v{1,2,3}; auto r = v | std::views::reverse; return 0; }
" INCREMENTAL_HAS_RANGES)

check_cxx_source_compiles("
#include <print>
int main(){ std::print(\"ok\"); return 0; }
" INCREMENTAL_HAS_PRINT)

check_cxx_source_compiles("
#include <stacktrace>
int main(){ auto s = std::stacktrace::current(); return 0; }
" INCREMENTAL_HAS_STACKTRACE)

if(INCREMENTAL_HAS_EXPECTED)
    message(STATUS "[incremental] std::expected            : YES")
else()
    message(WARNING "[incremental] std::expected            : NO  (required)")
endif()

if(INCREMENTAL_HAS_FORMAT)
    message(STATUS "[incremental] std::format              : YES")
else()
    message(WARNING "[incremental] std::format              : NO  (required)")
endif()

if(INCREMENTAL_HAS_RANGES)
    message(STATUS "[incremental] std::ranges              : YES")
else()
    message(WARNING "[incremental] std::ranges              : NO  (required)")
endif()

if(INCREMENTAL_HAS_PRINT)
    message(STATUS "[incremental] std::print               : YES")
else()
    message(STATUS "[incremental] std::print               : NO  (using std::cout fallback)")
endif()

if(INCREMENTAL_HAS_STACKTRACE)
    message(STATUS "[incremental] std::stacktrace          : YES")
else()
    message(STATUS "[incremental] std::stacktrace          : NO  (optional)")
endif()

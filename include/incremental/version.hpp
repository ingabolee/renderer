// include/incremental/version.hpp
// Project version constants.
// Part of the incremental library (MIT License).
#pragma once

#define INCREMENTAL_VERSION_MAJOR 1
#define INCREMENTAL_VERSION_MINOR 0
#define INCREMENTAL_VERSION_PATCH 0
#define INCREMENTAL_VERSION_STRING "1.0.0"

// Detected C++ standard mode.
// GCC 13 with -std=c++23 is the validated build.
// -std=c++2b (draft C++26 mode) compiles but uses the same libstdc++13 headers.
#if __cplusplus >= 202302L
#  define INCREMENTAL_CXX_STANDARD 23
#elif __cplusplus >= 202002L
#  define INCREMENTAL_CXX_STANDARD 20
#else
#  error "incremental requires C++20 or later"
#endif

// Compiler identification string used in --version output.
#if defined(__GNUC__) && !defined(__clang__)
#  define INCREMENTAL_COMPILER_STRING \
     "GCC " __VERSION__
#elif defined(__clang__)
#  define INCREMENTAL_COMPILER_STRING \
     "Clang " __clang_version__
#elif defined(_MSC_VER)
#  define INCREMENTAL_COMPILER_STRING \
     "MSVC " INCREMENTAL_STRINGIFY(_MSC_FULL_VER)
#else
#  define INCREMENTAL_COMPILER_STRING "Unknown"
#endif

namespace incremental {

struct version_info {
    int major_version{INCREMENTAL_VERSION_MAJOR};
    int minor_version{INCREMENTAL_VERSION_MINOR};
    int patch_version{INCREMENTAL_VERSION_PATCH};
    const char* version_string{INCREMENTAL_VERSION_STRING};
    const char* compiler_string{INCREMENTAL_COMPILER_STRING};
    int cxx_standard{INCREMENTAL_CXX_STANDARD};
};

[[nodiscard]] inline version_info version() noexcept {
    return {};
}

} // namespace incremental

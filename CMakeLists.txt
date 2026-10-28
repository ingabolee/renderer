cmake_minimum_required(VERSION 3.22)

project(incremental
    VERSION 1.0.0
    DESCRIPTION "Dependency-tracked incremental recomputation C++ library"
    LANGUAGES CXX
)

# ── C++ standard ──────────────────────────────────────────────────────────────
# Request C++23. GCC 13 uses -std=c++23. If a newer compiler supports C++26,
# CMake will pick it up via CXX_STANDARD=26 + STANDARD_REQUIRED.
# We document C++23 as the validated standard; C++26 is forward-declared.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# ── Options ───────────────────────────────────────────────────────────────────
option(INCREMENTAL_BUILD_TESTS      "Build unit tests"         ON)
option(INCREMENTAL_BUILD_BENCHMARKS "Build benchmarks"         ON)
option(INCREMENTAL_BUILD_EXAMPLES   "Build example programs"   ON)
option(INCREMENTAL_ENABLE_SANITIZERS "Enable ASan/UBSan (debug)" OFF)

# ── CMake modules ─────────────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerChecks)
include(Warnings)

# ── Header-only interface library ─────────────────────────────────────────────
add_library(incremental INTERFACE)
add_library(incremental::incremental ALIAS incremental)

target_include_directories(incremental INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(incremental INTERFACE cxx_std_23)

# ── CLI executable: recalc ────────────────────────────────────────────────────
add_executable(recalc
    src/main.cpp
    src/cli.cpp
    src/terminal.cpp
)

target_link_libraries(recalc PRIVATE incremental::incremental)
incremental_set_warnings(recalc)

if(WIN32)
    # Enable Unicode console on Windows
    target_compile_definitions(recalc PRIVATE UNICODE _UNICODE)
    # Enable UTF-8 execution charset on MSVC
    if(MSVC)
        target_compile_options(recalc PRIVATE /utf-8)
    endif()
endif()

# ── Sanitizers ────────────────────────────────────────────────────────────────
if(INCREMENTAL_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
    target_compile_options(recalc PRIVATE ${SANITIZER_FLAGS})
    target_link_options(recalc PRIVATE ${SANITIZER_FLAGS})
endif()

# ── Examples ─────────────────────────────────────────────────────────────────
if(INCREMENTAL_BUILD_EXAMPLES)
    add_executable(spreadsheet_demo examples/spreadsheet_demo.cpp)
    target_link_libraries(spreadsheet_demo PRIVATE incremental::incremental)
    incremental_set_warnings(spreadsheet_demo)

    add_executable(graph_examples examples/graph_examples.cpp)
    target_link_libraries(graph_examples PRIVATE incremental::incremental)
    incremental_set_warnings(graph_examples)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
if(INCREMENTAL_BUILD_TESTS)
    enable_testing()

    set(TEST_SOURCES
        tests/test_main.cpp
        tests/test_graph.cpp
        tests/test_incremental_recalculation.cpp
        tests/test_transactions.cpp
        tests/test_exception_safety.cpp
        tests/test_reentrancy.cpp
        tests/test_cycle_detection.cpp
        tests/test_undo_redo.cpp
        tests/test_customization_points.cpp
        tests/test_cli.cpp
        tests/test_randomized.cpp
        tests/test_header_self_containment.cpp
        # CLI sources needed by test_cli.cpp
        src/cli.cpp
        src/terminal.cpp
    )

    add_executable(incremental_tests ${TEST_SOURCES})
    target_include_directories(incremental_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(incremental_tests PRIVATE incremental::incremental)
    incremental_set_warnings(incremental_tests)
    # Suppress inconsequential warnings from test/cli code
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(incremental_tests PRIVATE
            -Wno-missing-field-initializers
            -Wno-unused-but-set-variable
        )
    endif()

    if(INCREMENTAL_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(incremental_tests PRIVATE ${SANITIZER_FLAGS})
        target_link_options(incremental_tests PRIVATE ${SANITIZER_FLAGS})
    endif()

    # Register each test as a CTest case via --test-case argument
    set(TEST_CASES
        "graph.create_input_nodes"
        "graph.create_derived_nodes"
        "graph.read_computed_values"
        "graph.reject_invalid_handles"
        "graph.cross_graph_handle_rejection"
        "incremental.single_input_change"
        "incremental.multi_input_change"
        "incremental.shared_dependency_once"
        "incremental.unrelated_untouched"
        "incremental.long_chain"
        "incremental.empty_transaction"
        "incremental.results_match_full_recompute"
        "transactions.commit_valid"
        "transactions.abandon_uncommitted"
        "transactions.formula_throws_no_partial_state"
        "transactions.no_notify_on_failed_commit"
        "transactions.notification_order"
        "exception_safety.strong_guarantee"
        "exception_safety.rollback_on_formula_throw"
        "reentrancy.callback_reads_consistent"
        "reentrancy.callback_queues_mutation"
        "reentrancy.queued_processed_separately"
        "reentrancy.callback_throws"
        "reentrancy.feedback_loop_limit"
        "cycles.self_cycle"
        "cycles.two_node_cycle"
        "cycles.multi_node_cycle"
        "cycles.valid_diamond"
        "cycles.rejected_leaves_graph_intact"
        "undo_redo.undo_one"
        "undo_redo.redo_one"
        "undo_redo.undo_multi"
        "undo_redo.branch_after_undo"
        "undo_redo.derived_recomputed"
        "customization.member_protocol"
        "customization.adl_protocol"
        "customization.range_protocol"
        "customization.index_protocol"
        "customization.priority_member_over_adl"
        "customization.priority_adl_over_range"
        "customization.priority_range_over_index"
        "cli.help"
        "cli.version"
        "cli.demo"
        "cli.invalid_option"
        "cli.ascii_mode"
        "randomized.incremental_equals_full"
    )

    foreach(TC ${TEST_CASES})
        add_test(NAME ${TC}
            COMMAND incremental_tests --test-case "${TC}"
        )
        set_tests_properties(${TC} PROPERTIES TIMEOUT 30)
    endforeach()
endif()

# ── Benchmarks ───────────────────────────────────────────────────────────────
if(INCREMENTAL_BUILD_BENCHMARKS)
    add_executable(incremental_bench
        benchmarks/benchmark_main.cpp
        # CLI/benchmark logic lives in cli.cpp
        src/cli.cpp
        src/terminal.cpp
    )
    target_include_directories(incremental_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(incremental_bench PRIVATE incremental::incremental)
    incremental_set_warnings(incremental_bench)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(incremental_bench PRIVATE
            -Wno-missing-field-initializers
            -Wno-unused-but-set-variable
        )
    endif()
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS incremental
    EXPORT incrementalTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/incremental
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(TARGETS recalc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "┌─ incremental configuration ──────────────────────────────────────┐")
message(STATUS "│  Build type         : ${CMAKE_BUILD_TYPE}")
message(STATUS "│  C++ standard       : C++${CMAKE_CXX_STANDARD}")
message(STATUS "│  Compiler           : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "│  Tests              : ${INCREMENTAL_BUILD_TESTS}")
message(STATUS "│  Benchmarks         : ${INCREMENTAL_BUILD_BENCHMARKS}")
message(STATUS "│  Examples           : ${INCREMENTAL_BUILD_EXAMPLES}")
message(STATUS "│  Sanitizers         : ${INCREMENTAL_ENABLE_SANITIZERS}")
message(STATUS "└──────────────────────────────────────────────────────────────────┘")
message(STATUS "")

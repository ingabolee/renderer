# cmake/Warnings.cmake
# Apply strict warning flags per compiler.

function(incremental_set_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wno-unused-parameter
            -Wcast-align
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /wd4100  # unreferenced formal parameter
        )
    endif()
endfunction()

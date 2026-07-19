cmake_minimum_required(VERSION 4.2)
project(Chompo)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHOMPO_ENABLE_PERFORMANCE_TESTS "Enable heavy Release performance/TLE checks" OFF)
option(CHOMPO_NATIVE_OPTIMIZATIONS "Optimize Release binaries for the current CPU" OFF)
option(CHOMPO_PGO_GENERATE "Build an instrumented binary for profile generation" OFF)
option(CHOMPO_PGO_USE "Build using previously generated profile data" OFF)

if(CHOMPO_PGO_GENERATE AND CHOMPO_PGO_USE)
    message(FATAL_ERROR "CHOMPO_PGO_GENERATE and CHOMPO_PGO_USE are mutually exclusive")
endif()

add_executable(Chompo
        src/main.cpp
        src/lexer/symbol.h src/lexer/symbol.cpp
        src/lexer/token.h src/lexer/token.cpp src/lexer/lexer.h src/lexer/lexer.cpp
        src/parser/ast.h src/parser/parser.h src/parser/parser.cpp
        src/parser/resolver.h src/parser/resolver.cpp
        src/parser/ast_printer.h src/parser/ast_printer.cpp
        src/interpreter/value.h src/interpreter/value.cpp
        src/interpreter/runtime_error.h src/interpreter/runtime_error.cpp
        src/interpreter/environment.h src/interpreter/environment.cpp
        src/interpreter/interpreter.h src/interpreter/interpreter.cpp
        src/interpreter/callable.h src/interpreter/callable.cpp
        src/interpreter/collection_builtins.cpp src/interpreter/system_builtins.cpp
        src/interpreter/io_manager.h src/interpreter/io_manager.cpp src/interpreter/io_builtins.cpp
        src/interpreter/network_manager.h src/interpreter/network_manager.cpp src/interpreter/network_builtins.cpp
        src/interpreter/return_signal.h src/interpreter/loop_signal.h src/config.h
)

target_include_directories(Chompo PRIVATE src)

if(MSVC)
    target_compile_options(Chompo PRIVATE
            /W4
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/Ob3>
            $<$<CONFIG:Release>:/GL>)
    target_link_options(Chompo PRIVATE
            $<$<CONFIG:Release>:/LTCG>
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>)

    if(CHOMPO_NATIVE_OPTIMIZATIONS)
        target_compile_options(Chompo PRIVATE $<$<CONFIG:Release>:/arch:AVX2>)
    endif()

    if(CHOMPO_PGO_GENERATE OR CHOMPO_PGO_USE)
        message(FATAL_ERROR "The current PGO options are implemented for GCC/Clang toolchains")
    endif()
else()
    target_compile_options(Chompo PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Release>:-fno-semantic-interposition>
            $<$<CONFIG:Release>:-ffunction-sections>
            $<$<CONFIG:Release>:-fdata-sections>)
    target_link_options(Chompo PRIVATE
            $<$<CONFIG:Release>:-Wl,--gc-sections>)

    if(CHOMPO_NATIVE_OPTIMIZATIONS)
        target_compile_options(Chompo PRIVATE
                $<$<CONFIG:Release>:-march=native>
                $<$<CONFIG:Release>:-mtune=native>)
    endif()

    if(CHOMPO_PGO_GENERATE)
        target_compile_options(Chompo PRIVATE -fprofile-generate)
        target_link_options(Chompo PRIVATE -fprofile-generate)
    elseif(CHOMPO_PGO_USE)
        target_compile_options(Chompo PRIVATE -fprofile-use -fprofile-correction)
        target_link_options(Chompo PRIVATE -fprofile-use -fprofile-correction)
    endif()
endif()

if(WIN32)
    target_link_libraries(Chompo PRIVATE ws2_32)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT CHOMPO_IPO_SUPPORTED OUTPUT CHOMPO_IPO_ERROR)
if(CHOMPO_IPO_SUPPORTED)
    set_property(TARGET Chompo PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "Release IPO/LTO is unavailable: ${CHOMPO_IPO_ERROR}")
endif()

include(CTest)

function(add_chompo_test name expected_exit use_stderr)
    add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
            -DCHOMPO_EXECUTABLE=$<TARGET_FILE:Chompo>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/${name}.chmp
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${name}.txt
            -DEXPECTED_EXIT_CODE=${expected_exit}
            -DUSE_STDERR=${use_stderr}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_case.cmake)
    set_tests_properties(${name} PROPERTIES TIMEOUT 5)
endfunction()

add_chompo_test(basic 0 FALSE)
add_chompo_test(arrays 0 FALSE)
add_chompo_test(functions 0 FALSE)
add_chompo_test(updates 0 FALSE)
add_chompo_test(stack_overflow 1 TRUE)
add_chompo_test(direct_array_cycle 1 TRUE)
add_chompo_test(indirect_array_cycle 1 TRUE)
add_chompo_test(loops 0 FALSE)
add_chompo_test(break_outside_loop 1 TRUE)
add_chompo_test(break_inside_nested_function 1 TRUE)
add_chompo_test(membership 0 FALSE)
add_chompo_test(in_invalid_container 1 TRUE)
add_chompo_test(in_invalid_string_needle 1 TRUE)
add_chompo_test(continue_outside_loop 1 TRUE)
add_chompo_test(for_in 0 FALSE)
add_chompo_test(regression 0 FALSE)
add_chompo_test(collections 0 FALSE)
add_chompo_test(push_cycle 1 TRUE)
add_chompo_test(push_invalid_target 1 TRUE)
add_chompo_test(pop_invalid_target 1 TRUE)
add_chompo_test(remove_at_invalid_target 1 TRUE)
add_chompo_test(remove_at_invalid_index 1 TRUE)
add_chompo_test(resolver_slots 0 FALSE)
add_chompo_test(control_flow_fast 0 FALSE)

add_test(NAME error_regressions
        COMMAND ${CMAKE_COMMAND}
        -DCHOMPO_EXECUTABLE=$<TARGET_FILE:Chompo>
        -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/chompo-error-tests
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_error_suite.cmake)
set_tests_properties(error_regressions PROPERTIES TIMEOUT 10)

add_test(NAME io_streams
        COMMAND ${CMAKE_COMMAND}
        -DCHOMPO_EXECUTABLE=$<TARGET_FILE:Chompo>
        -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/chompo-io-tests
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_io_suite.cmake)
set_tests_properties(io_streams PROPERTIES TIMEOUT 10)

add_test(NAME system_args
        COMMAND ${CMAKE_COMMAND}
        -DCHOMPO_EXECUTABLE=$<TARGET_FILE:Chompo>
        -DTEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/chompo-args-tests
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_args_suite.cmake)
set_tests_properties(system_args PROPERTIES TIMEOUT 5)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME langjam_chat
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/chat_smoke.py
                    --executable $<TARGET_FILE:Chompo>
                    --server ${CMAKE_CURRENT_SOURCE_DIR}/langjam/Chompo/chat_server.chmp
                    --client ${CMAKE_CURRENT_SOURCE_DIR}/langjam/Chompo/chat_client.chmp)
    set_tests_properties(langjam_chat PROPERTIES TIMEOUT 25)
else()
    message(STATUS "Python was not found; the LangJam end-to-end chat test is disabled")
endif()

add_executable(network_manager_test
        tests/network_manager_test.cpp
        src/interpreter/network_manager.cpp
        src/interpreter/network_manager.h)
target_include_directories(network_manager_test PRIVATE src)
if(MSVC)
    target_compile_options(network_manager_test PRIVATE /W4)
else()
    target_compile_options(network_manager_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(network_manager_test PRIVATE ws2_32)
endif()
add_test(NAME network_manager COMMAND network_manager_test)
set_tests_properties(network_manager PROPERTIES TIMEOUT 10)

if(CHOMPO_ENABLE_PERFORMANCE_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME performance_tle
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/run_performance_suite.py
                    --executable $<TARGET_FILE:Chompo>
                    --cases ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/cases)
    set_tests_properties(performance_tle PROPERTIES LABELS performance TIMEOUT 70)
endif()

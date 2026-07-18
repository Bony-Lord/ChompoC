cmake_minimum_required(VERSION 4.2)
project(Chompo)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Chompo
        src/main.cpp
        src/lexer/token.h src/lexer/token.cpp src/lexer/lexer.h src/lexer/lexer.cpp
        src/parser/ast.h src/parser/parser.h src/parser/parser.cpp src/parser/ast_printer.h src/parser/ast_printer.cpp
        src/interpreter/value.h src/interpreter/value.cpp
        src/interpreter/runtime_error.h src/interpreter/runtime_error.cpp
        src/interpreter/environment.h src/interpreter/environment.cpp
        src/interpreter/interpreter.h src/interpreter/interpreter.cpp
        src/interpreter/callable.h src/interpreter/callable.cpp
        src/interpreter/io_manager.h src/interpreter/io_manager.cpp src/interpreter/io_builtins.cpp
        src/interpreter/network_manager.h src/interpreter/network_manager.cpp src/interpreter/network_builtins.cpp
        src/interpreter/return_signal.h src/interpreter/loop_signal.h src/config.h
)

target_include_directories(Chompo PRIVATE src)
if(WIN32)
    target_link_libraries(Chompo PRIVATE ws2_32)
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

add_executable(network_manager_test
        tests/network_manager_test.cpp
        src/interpreter/network_manager.cpp
        src/interpreter/network_manager.h)
target_include_directories(network_manager_test PRIVATE src)
if(WIN32)
    target_link_libraries(network_manager_test PRIVATE ws2_32)
endif()
add_test(NAME network_manager COMMAND network_manager_test)
set_tests_properties(network_manager PROPERTIES TIMEOUT 10)

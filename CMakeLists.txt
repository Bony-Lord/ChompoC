cmake_minimum_required(VERSION 4.2)
project(Chompo)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Chompo
        src/main.cpp

        src/lexer/token.h
        src/lexer/token.cpp
        src/lexer/lexer.h
        src/lexer/lexer.cpp

        src/parser/ast.h
        src/parser/parser.h
        src/parser/parser.cpp
        src/parser/ast_printer.h
        src/parser/ast_printer.cpp

        src/interpreter/value.h
        src/interpreter/value.cpp
        src/interpreter/runtime_error.h
        src/interpreter/runtime_error.cpp
        src/interpreter/environment.h
        src/interpreter/environment.cpp
        src/interpreter/interpreter.h
        src/interpreter/interpreter.cpp
        src/interpreter/callable.h
        src/interpreter/callable.cpp
        src/interpreter/return_signal.h
        src/config.h
)

target_include_directories(Chompo PRIVATE src)

include(CTest)

function(add_chompo_test
        name
        expected_exit
        use_stderr)
    add_test(
            NAME ${name}
            COMMAND ${CMAKE_COMMAND}
            -DCHOMPO_EXECUTABLE=$<TARGET_FILE:Chompo>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/${name}.chmp
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${name}.txt
            -DEXPECTED_EXIT_CODE=${expected_exit}
            -DUSE_STDERR=${use_stderr}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_case.cmake
    )
endfunction()

add_chompo_test(basic 0 FALSE)
add_chompo_test(arrays 0 FALSE)
add_chompo_test(functions 0 FALSE)
add_chompo_test(updates 0 FALSE)
add_chompo_test(stack_overflow 1 TRUE)
add_chompo_test(direct_array_cycle 1 TRUE)
add_chompo_test(indirect_array_cycle 1 TRUE)
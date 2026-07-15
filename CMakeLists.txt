cmake_minimum_required(VERSION 4.2)
project(Chompo)

set(CMAKE_CXX_STANDARD 23)

add_executable(Chompo main.cpp
        token.h
        token.cpp
        lexer.h
        lexer.cpp
        ast.h
        parser.h
        parser.cpp
        ast_printer.h
        ast_printer.cpp
        value.h
        value.cpp
        runtime_error.h
        runtime_error.cpp
        environment.h
        environment.cpp
        interpreter.h
        interpreter.cpp)

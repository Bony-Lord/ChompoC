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
        ast_printer.cpp)

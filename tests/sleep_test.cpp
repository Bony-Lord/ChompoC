//! This is an absurd test:
//! 1. It's not consistent, random cpu lag spike and this test will fail.
//! 2. It has too many dependencies
//!
//! By default, this test is *NOT* included
// If you still want to add this test, then add it to CMakeLists.txt.

#include "interpreter/interpreter.h"
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/ast.h"
#include "parser/parser.h"

#include <exception>
#include <format>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <chrono>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void inline_interpret(std::string source) {
    Interpreter interpreter(std::cout);
    Lexer lexer(std::move(source));
    std::vector<Token> tokens = lexer.scan_tokens();

    Parser parser(std::move(tokens));
    Program program = parser.parse();

    interpreter.interpret(program);
}
}

const std::int64_t EPSILON = 100;

int main() {
    try {
        std::mt19937 rand(985879);
        std::uniform_int_distribution<std::int32_t> dist(1000, 3000);
        std::int64_t time = dist(rand);

        auto start = std::chrono::steady_clock::now();
        inline_interpret(
            std::format("sleep({})", time)
        );
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::int64_t elapsed_ms = static_cast<std::int64_t>(elapsed.count());
        require(std::abs(elapsed_ms - time) <= EPSILON, "sleeped longer than expected");

        std::cout << "sleep test passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

#include "lexer.h"
#include "token.h"

#include <iostream>
#include <string>

int main() {
    std::string source =
    "let result42 = 12.5;\n"
    "print(result42, \"another_value\");";

    Lexer lexer(std::move(source));
    const auto tokens = lexer.scan_tokens();

    for (const Token& token : tokens) {
        std::cout
            << token.position.line
            << ':'
            << token.position.column
            << "  "
            << token_type_name(token.type)
            << "  "
            << token.lexeme
            << '\n';
    }
}
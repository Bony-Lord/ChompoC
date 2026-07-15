#include "lexer.h"
#include "token.h"
#include "parser.h"
#include "ast_printer.h"

#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <filesystem>

std::string read_file(const std::string& path) {
    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error(
            "Failed to open source file: " +
            std::filesystem::absolute(path).string()
        );
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

int main() {
    try {
        const std::string source = read_file("./../test_code.txt");

        Lexer lexer(source);
        const auto tokens = lexer.scan_tokens();

        for (const Token& token : tokens) {
            std::cout
                << token.position.line
                << ':'
                << token.position.column
                << "  "
                << std::left
                << std::setw(14)
                << token_type_name(token.type)
                << std::quoted(token.lexeme)
                << '\n';
        }

        Parser parser(std::move(tokens));
        ExprPtr tree = parser.parse();

        AstPrinter printer;
        std::cout << printer.print(*tree) << '\n';

    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
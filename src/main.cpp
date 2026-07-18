#include "config.h"
#include "interpreter/interpreter.h"
#include "interpreter/io_manager.h"
#include "interpreter/network_manager.h"
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/ast_printer.h"
#include "parser/parser.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

std::filesystem::path find_file(const std::filesystem::path &relative_path) {
    std::filesystem::path directory = std::filesystem::current_path();

    while (true) {
        const std::filesystem::path candidate = directory / relative_path;
        if (std::filesystem::is_regular_file(candidate))
            return candidate;

        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory)
            break;

        directory = parent;
    }

    throw std::runtime_error("Failed to find source file: " + relative_path.string() +
                             "\nWorking directory: " + std::filesystem::current_path().string());
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Failed to open source file: " + std::filesystem::absolute(path).string());

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char *argv[]) {
    try {
        const std::filesystem::path source_path =
            argc > 1 ? std::filesystem::path(argv[1]) : find_file("tests/test_code.chmp");
        const std::string source = read_file(source_path);

        if constexpr (ChompoConfig::EnableDebugOutput)
            std::cout << "Source file: " << std::filesystem::absolute(source_path).string() << "\n\n";

        Lexer lexer(source);
        auto tokens = lexer.scan_tokens();

        if constexpr (ChompoConfig::EnableDebugOutput) {
            std::cout << "====== Lexer ======\n";
            for (const Token &token : tokens)
                std::cout << token.position.line << ':' << token.position.column << "  " << std::left
                          << std::setw(14) << token_type_name(token.type) << std::quoted(token.lexeme) << '\n';
        }

        Parser parser(std::move(tokens));
        Program program = parser.parse();

        if constexpr (ChompoConfig::EnableDebugOutput) {
            std::cout << "====== Parser ======\n";
            std::cout << "Parsed " << program.size() << " top-level statements\n";
            AstPrinter printer;
            std::cout << printer.print(program);
            std::cout << "====== Output ======\n";
        }

        IOManager io_manager(std::cin, std::cout);
        NetworkManager network_manager;
        Interpreter interpreter(io_manager.output_stream());
        interpreter.install_io_builtins(io_manager);
        interpreter.install_network_builtins(network_manager);
        interpreter.interpret(program);
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}

#include "config.h"
#include "interpreter/interpreter.h"
#include "interpreter/io_manager.h"
#include "interpreter/network_manager.h"
#include "interpreter/terminal_manager.h"
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/ast_printer.h"
#include "parser/parser.h"
#include "parser/resolver.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Failed to open source file: " + std::filesystem::absolute(path).string());

    const std::streampos end = file.tellg();
    if (end < 0)
        throw std::runtime_error("Failed to determine source file size: " + path.string());

    std::string source(static_cast<std::size_t>(end), '\0');
    file.seekg(0, std::ios::beg);

    if (!source.empty() && !file.read(source.data(), static_cast<std::streamsize>(source.size())))
        throw std::runtime_error("Failed to read source file: " + path.string());

    return source;
}

int main(int argc, char *argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

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

        Resolver resolver;
        resolver.resolve(program);

        if constexpr (ChompoConfig::EnableDebugOutput) {
            std::cout << "====== Parser ======\n";
            std::cout << "Parsed and resolved " << program.size() << " top-level statements\n";
            AstPrinter printer;
            std::cout << printer.print(program);
            std::cout << "====== Output ======\n";
        }

        std::vector<std::string> script_arguments;
        script_arguments.reserve(argc > 2 ? static_cast<std::size_t>(argc - 2) : 0);
        for (int index = 2; index < argc; ++index)
            script_arguments.emplace_back(argv[index]);

        IOManager io_manager(std::cin, std::cout);
        NetworkManager network_manager;
        TerminalManager terminal_manager(io_manager, io_manager.output_stream());
        Interpreter interpreter(io_manager.output_stream());
        interpreter.install_collection_builtins();
        interpreter.install_io_builtins(io_manager);
        interpreter.install_network_builtins(network_manager);
        interpreter.install_secure_network_builtins(network_manager);
        interpreter.install_terminal_builtins(terminal_manager);
        interpreter.install_system_builtins(std::move(script_arguments));
        interpreter.interpret(program);
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}

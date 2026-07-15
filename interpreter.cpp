#include "interpreter.h"
#include "runtime_error.h"

#include <charconv>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace {
    std::int64_t parse_integer(const Token& token) {
        std::int64_t value = 0;

        const char* begin = token.lexeme.data();
        const char* end = begin + token.lexeme.size();

        const auto [position, error] = std::from_chars(begin, end, value);

        if (error != std::errc{} || position != end) {
            throw RuntimeError(token, "invalid integer literal '" + token.lexeme + "'");
        }

        return value;
    }

    std::string parse_string(const Token& token) {
        if (token.lexeme.size() < 2 || token.lexeme.front() != '"' || token.lexeme.back() != '"') {
            throw RuntimeError(token, "invalid string literal");
        }

        std::string result;

        for (std::size_t index = 1; index + 1 < token.lexeme.size(); ++index) {
            const char character = token.lexeme[index];

            if (character != '\\') {
                result += character;
                continue;
            }
            ++index;

            if (index + 1 >= token.lexeme.size()) {
                throw RuntimeError(token, "unfinished escape sequence");
            }

            switch (token.lexeme[index]) {
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                default:
                    throw RuntimeError(token, "unknown escape sequence");
            }
        }

        return result;
    }
    bool values_equal(const Value& left, const Value& right) {
        if (left.data.index() != right.data.index()) return false;
        if (left.is_null()) return true;

        if (left.is_bool()) return std::get<bool>(left.data) == std::get<bool>(right.data);
        if (left.is_integer()) return std::get<std::int64_t>(left.data) == std::get<std::int64_t>(right.data);
        if (left.is_string()) return std::get<std::string>(left.data) == std::get<std::string>(right.data);
        if (left.is_array())  return std::get<ArrayPtr>(left.data) == std::get<ArrayPtr>(right.data); // ? {1} != {1}
        return false;
    }

    [[noreturn]] void binary_type_error(const Token& operation, const Value& left, const Value& right) {
        throw RuntimeError(operation, "operator '" + operation.lexeme + "' cannot be applied to " +
            left.type_name() + " and " + right.type_name());
    }
}

Interpreter::Interpreter(std::ostream& output) :
    globals_(std::make_shared<Environment>()), environment_(globals_), output_(output) {}

void Interpreter::interpret(const Program& program) {
    for (const StmtPtr& statement : program) {
        execute(*statement);
    }
}

Value Interpreter::evaluate(const Expr& expression) {
    return std::visit([this](const auto& node) { return evaluate_node(node); }, expression.node);
}

void Interpreter::execute(const Stmt& statement) {
    std::visit([this](const auto& node) { execute_node(node); }, statement.node);
}


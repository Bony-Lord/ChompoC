#include "interpreter.h"
#include "callable.h"
#include "config.h"
#include "runtime_error.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace {
    std::int64_t parse_integer(const Token &token) {
        std::int64_t value = 0;
        const char *begin = token.lexeme.data();
        const char *end = begin + token.lexeme.size();
        const auto [position, error] = std::from_chars(begin, end, value);
        if (error != std::errc{} || position != end)
            throw RuntimeError(token, "invalid integer literal '" + token.lexeme + "'");
        return value;
    }

    double parse_double(const Token &token) {
        double value = 0.0;
        const char *begin = token.lexeme.data();
        const char *end = begin + token.lexeme.size();
        const auto [position, error] = std::from_chars(begin, end, value, std::chars_format::general);
        if (error != std::errc{} || position != end)
            throw RuntimeError(token, "invalid double literal '" + token.lexeme + "'");
        return value;
    }

    std::string parse_string(const Token &token) {
        if (token.lexeme.size() < 2 || token.lexeme.front() != '"' || token.lexeme.back() != '"')
            throw RuntimeError(token, "invalid string literal");

        std::string result;
        result.reserve(token.lexeme.size() - 2);

        for (std::size_t index = 1; index + 1 < token.lexeme.size(); ++index) {
            const char character = token.lexeme[index];
            if (character != '\\') {
                result += character;
                continue;
            }

            ++index;
            if (index + 1 >= token.lexeme.size())
                throw RuntimeError(token, "unfinished escape sequence");

            switch (token.lexeme[index]) {
            case 'n': result += '\n'; break;
            case 't': result += '\t'; break;
            case 'r': result += '\r'; break;
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            default: throw RuntimeError(token, "unknown escape sequence");
            }
        }
        return result;
    }

    char parse_char(const Token &token) {
        if (token.lexeme.size() < 3 || token.lexeme.front() != '\'' || token.lexeme.back() != '\'')
            throw RuntimeError(token, "invalid character literal");

        if (token.lexeme[1] != '\\') {
            if (token.lexeme.size() != 3)
                throw RuntimeError(token, "character literal must contain exactly one character");
            return token.lexeme[1];
        }

        if (token.lexeme.size() != 4)
            throw RuntimeError(token, "invalid character escape sequence");

        switch (token.lexeme[2]) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return '\0';
        case '\\': return '\\';
        case '\'': return '\'';
        default: throw RuntimeError(token, "unknown character escape sequence");
        }
    }

    bool values_equal(const Value &left, const Value &right) {
        if (const auto *left_integer = std::get_if<std::int64_t>(&left.data)) {
            if (const auto *right_integer = std::get_if<std::int64_t>(&right.data))
                return *left_integer == *right_integer;
        }

        if (left.is_number() && right.is_number()) {
            if (left.is_double() || right.is_double())
                return left.number_as_double() == right.number_as_double();
            return left.number_as_integer() == right.number_as_integer();
        }

        if (left.data.index() != right.data.index())
            return false;
        if (left.is_null())
            return true;
        if (const auto *value = std::get_if<bool>(&left.data))
            return *value == std::get<bool>(right.data);
        if (const auto *value = std::get_if<std::string>(&left.data))
            return *value == std::get<std::string>(right.data);
        if (const auto *value = std::get_if<double>(&left.data))
            return *value == std::get<double>(right.data);
        if (const auto *value = std::get_if<char>(&left.data))
            return *value == std::get<char>(right.data);

        if (left.is_array()) {
            const ArrayPtr &left_array = std::get<ArrayPtr>(left.data);
            const ArrayPtr &right_array = std::get<ArrayPtr>(right.data);
            if (left_array == right_array)
                return true;
            if (!left_array || !right_array || left_array->size() != right_array->size())
                return false;
            for (std::size_t index = 0; index < left_array->size(); ++index) {
                if (!values_equal((*left_array)[index], (*right_array)[index]))
                    return false;
            }
            return true;
        }

        if (left.is_callable())
            return std::get<CallablePtr>(left.data) == std::get<CallablePtr>(right.data);
        if (left.is_map())
            return std::get<MapPtr>(left.data) == std::get<MapPtr>(right.data);
        return false;
    }

    Value evaluate_membership(const Token &operation, const Value &needle, const Value &container) {
        if (container.is_array()) {
            const ArrayPtr &array = std::get<ArrayPtr>(container.data);
            if (!array)
                return Value(false);
            for (const Value &element : *array) {
                if (values_equal(needle, element))
                    return Value(true);
            }
            return Value(false);
        }

        if (container.is_string()) {
            const std::string &string = std::get<std::string>(container.data);
            if (needle.is_char())
                return Value(string.find(std::get<char>(needle.data)) != std::string::npos);
            if (needle.is_string())
                return Value(string.find(std::get<std::string>(needle.data)) != std::string::npos);
            throw RuntimeError(operation, "left operand of 'in' must be char or string when right operand is string, got " +
                                              needle.type_name());
        }

        if (container.is_map()) {
            const MapPtr &map = std::get<MapPtr>(container.data);
            if (!map)
                return Value(false);
            return Value(map->table.find(needle) != map->table.end());
        }

        throw RuntimeError(operation,
                           "right operand of 'in' must be array, string, or map, got " + container.type_name());
    }

    Value map_lookup(Interpreter &interpreter, const Token &token, const MapPtr &map, const Value &key) {
        if (!map)
            throw RuntimeError(token, "operator '[]' cannot be applied to null map");

        auto existing = map->table.find(key);
        if (existing != map->table.end())
            return existing->second;

        if (!map->default_factory.is_callable())
            throw RuntimeError(token, "map key not found");

        const CallablePtr &factory = std::get<CallablePtr>(map->default_factory.data);
        if (!factory)
            throw RuntimeError(token, "map default factory is null");
        if (!factory->accepts_arity(0))
            throw RuntimeError(token, "map default factory expects " + factory->arity_description() +
                                          " argument(s), got 0");

        Value created = factory->call(interpreter, token, {});
        auto inserted = map->table.emplace(key, std::move(created));
        return inserted.first->second;
    }

    Value concatenate_arrays(const ArrayPtr &left, const ArrayPtr &right) {
        auto result = std::make_shared<ArrayValue>();
        const std::size_t left_size = left ? left->size() : 0;
        const std::size_t right_size = right ? right->size() : 0;
        if (right_size > result->max_size() - left_size)
            throw std::length_error("array concatenation is too large");
        result->reserve(left_size + right_size);
        if (left)
            result->insert(result->end(), left->begin(), left->end());
        if (right)
            result->insert(result->end(), right->begin(), right->end());
        return Value(std::move(result));
    }

    Value repeat_string(const Token &operation, const std::string &string, std::int64_t count) {
        if (count < 0)
            throw RuntimeError(operation, "string multiplication count cannot be negative");

        const std::size_t repeats = static_cast<std::size_t>(count);
        std::string result;
        if (!string.empty() && repeats > result.max_size() / string.size())
            throw RuntimeError(operation, "repeated string is too large");
        result.reserve(string.size() * repeats);
        for (std::size_t index = 0; index < repeats; ++index)
            result += string;
        return Value(std::move(result));
    }

    Value repeat_array(const Token &operation, const ArrayPtr &array, std::int64_t count) {
        if (count < 0)
            throw RuntimeError(operation, "array multiplication count cannot be negative");

        auto result = std::make_shared<ArrayValue>();
        if (!array || array->empty() || count == 0)
            return Value(std::move(result));

        const std::size_t repeats = static_cast<std::size_t>(count);
        if (repeats > result->max_size() / array->size())
            throw RuntimeError(operation, "repeated array is too large");
        result->reserve(repeats * array->size());
        for (std::size_t repeat = 0; repeat < repeats; ++repeat)
            result->insert(result->end(), array->begin(), array->end());
        return Value(std::move(result));
    }

    bool can_convert_to_string_implicitly(const Value &value) {
        return value.is_null() || value.is_number() || value.is_string() || value.is_char() || value.is_array();
    }

    TokenType binary_operator_type(TokenType type) {
        switch (type) {
        case TokenType::PlusEq:
        case TokenType::PlusOne: return TokenType::Plus;
        case TokenType::MinusEq:
        case TokenType::MinusOne: return TokenType::Minus;
        case TokenType::MulEq: return TokenType::Star;
        case TokenType::DivideEq: return TokenType::Slash;
        default: return type;
        }
    }

    [[noreturn]] void binary_type_error(const Token &operation, const Value &left, const Value &right) {
        throw RuntimeError(operation, "operator '" + operation.lexeme + "' cannot be applied to " + left.type_name() +
                                          " and " + right.type_name());
    }

    Value apply_integer_binary(const Token &operation, TokenType type, std::int64_t left, std::int64_t right) {
        switch (type) {
        case TokenType::Plus: return Value(left + right);
        case TokenType::Minus: return Value(left - right);
        case TokenType::Star: return Value(left * right);
        case TokenType::Slash:
            if (right == 0)
                throw RuntimeError(operation, "division by zero");
            if (left == std::numeric_limits<std::int64_t>::min() && right == -1)
                throw RuntimeError(operation, "integer division overflow");
            return Value(left / right);
        case TokenType::Percent:
            if (right == 0)
                throw RuntimeError(operation, "division by zero");
            if (left == std::numeric_limits<std::int64_t>::min() && right == -1)
                return Value(std::int64_t{0});
            return Value(left % right);
        case TokenType::Less: return Value(left < right);
        case TokenType::LessEqual: return Value(left <= right);
        case TokenType::Greater: return Value(left > right);
        case TokenType::GreaterEqual: return Value(left >= right);
        case TokenType::EqualEqual: return Value(left == right);
        case TokenType::NotEqual: return Value(left != right);
        default: throw RuntimeError(operation, "unknown integer operator '" + operation.lexeme + "'");
        }
    }

    Value apply_binary(const Token &operation, const Value &left, const Value &right) {
        const TokenType type = binary_operator_type(operation.type);

        if (operation.type == TokenType::In)
            return evaluate_membership(operation, left, right);

        if (const auto *left_integer = std::get_if<std::int64_t>(&left.data)) {
            if (const auto *right_integer = std::get_if<std::int64_t>(&right.data))
                return apply_integer_binary(operation, type, *left_integer, *right_integer);
        }

        if (type == TokenType::EqualEqual)
            return Value(values_equal(left, right));
        if (type == TokenType::NotEqual)
            return Value(!values_equal(left, right));

        if (type == TokenType::Plus && (left.is_string() || right.is_string())) {
            if (!can_convert_to_string_implicitly(left) || !can_convert_to_string_implicitly(right))
                binary_type_error(operation, left, right);
            return Value(left.to_string() + right.to_string());
        }

        if (type == TokenType::Plus && left.is_array() && right.is_array())
            return concatenate_arrays(std::get<ArrayPtr>(left.data), std::get<ArrayPtr>(right.data));

        if (type == TokenType::Star) {
            if (left.is_string() && right.is_integer_number())
                return repeat_string(operation, std::get<std::string>(left.data), right.number_as_integer());
            if (right.is_string() && left.is_integer_number())
                return repeat_string(operation, std::get<std::string>(right.data), left.number_as_integer());
            if (left.is_array() && right.is_integer_number())
                return repeat_array(operation, std::get<ArrayPtr>(left.data), right.number_as_integer());
            if (right.is_array() && left.is_integer_number())
                return repeat_array(operation, std::get<ArrayPtr>(right.data), left.number_as_integer());
        }

        if (!left.is_number() || !right.is_number())
            binary_type_error(operation, left, right);

        const bool use_double = left.is_double() || right.is_double();
        switch (type) {
        case TokenType::Plus:
            return use_double ? Value(left.number_as_double() + right.number_as_double())
                              : Value(left.number_as_integer() + right.number_as_integer());
        case TokenType::Minus:
            return use_double ? Value(left.number_as_double() - right.number_as_double())
                              : Value(left.number_as_integer() - right.number_as_integer());
        case TokenType::Star:
            return use_double ? Value(left.number_as_double() * right.number_as_double())
                              : Value(left.number_as_integer() * right.number_as_integer());
        case TokenType::Slash:
            if (use_double) {
                const double denominator = right.number_as_double();
                if (denominator == 0.0)
                    throw RuntimeError(operation, "division by zero");
                return Value(left.number_as_double() / denominator);
            } else {
                const std::int64_t denominator = right.number_as_integer();
                if (denominator == 0)
                    throw RuntimeError(operation, "division by zero");
                return Value(left.number_as_integer() / denominator);
            }
        case TokenType::Percent: {
            if (!left.is_integer_number() || !right.is_integer_number())
                binary_type_error(operation, left, right);
            const std::int64_t denominator = right.number_as_integer();
            if (denominator == 0)
                throw RuntimeError(operation, "division by zero");
            return Value(left.number_as_integer() % denominator);
        }
        case TokenType::Less:
            return use_double ? Value(left.number_as_double() < right.number_as_double())
                              : Value(left.number_as_integer() < right.number_as_integer());
        case TokenType::LessEqual:
            return use_double ? Value(left.number_as_double() <= right.number_as_double())
                              : Value(left.number_as_integer() <= right.number_as_integer());
        case TokenType::Greater:
            return use_double ? Value(left.number_as_double() > right.number_as_double())
                              : Value(left.number_as_integer() > right.number_as_integer());
        case TokenType::GreaterEqual:
            return use_double ? Value(left.number_as_double() >= right.number_as_double())
                              : Value(left.number_as_integer() >= right.number_as_integer());
        default:
            throw RuntimeError(operation, "unknown binary operator '" + operation.lexeme + "'");
        }
    }

    Value convert_to_int(const Token &token, const Value &value) {
        if (value.is_integer_number())
            return Value(value.number_as_integer());
        if (value.is_double()) {
            const double number = value.number_as_double();
            if (!std::isfinite(number))
                throw RuntimeError(token, "cannot convert non-finite double to integer");
            const long double extended = number;
            if (extended < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                extended > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                throw RuntimeError(token, "double is outside the integer range");
            return Value(static_cast<std::int64_t>(number));
        }
        if (value.is_string()) {
            const std::string &string = std::get<std::string>(value.data);
            std::int64_t result = 0;
            const auto [position, error] = std::from_chars(string.data(), string.data() + string.size(), result);
            if (error != std::errc{} || position != string.data() + string.size())
                throw RuntimeError(token, "cannot convert '" + string + "' to integer");
            return Value(result);
        }
        if (value.is_null())
            return Value(std::int64_t{0});
        throw RuntimeError(token, "cannot convert " + value.type_name() + " to integer");
    }

    Value convert_to_double(const Token &token, const Value &value) {
        if (value.is_number())
            return Value(value.number_as_double());
        if (value.is_string()) {
            const std::string &string = std::get<std::string>(value.data);
            double result = 0.0;
            const auto [position, error] =
                std::from_chars(string.data(), string.data() + string.size(), result, std::chars_format::general);
            if (error != std::errc{} || position != string.data() + string.size())
                throw RuntimeError(token, "cannot convert '" + string + "' to double");
            return Value(result);
        }
        if (value.is_null())
            return Value(0.0);
        throw RuntimeError(token, "cannot convert " + value.type_name() + " to double");
    }

    Value convert_to_bool(const Value &value) { return Value(value.is_truthy()); }

    Value convert_to_string(const Token &token, const Value &value) {
        if (value.is_callable())
            throw RuntimeError(token, "cannot convert function to string");
        return Value(value.to_string());
    }

    Value convert_to_char(const Token &token, const Value &value) {
        if (value.is_char())
            return value;
        if (value.is_string()) {
            const std::string &string = std::get<std::string>(value.data);
            if (string.size() != 1)
                throw RuntimeError(token, "string must contain exactly one byte");
            return Value(string[0]);
        }
        if (value.is_integer()) {
            const std::int64_t number = value.number_as_integer();
            if (number < 0 || number > 255)
                throw RuntimeError(token, "integer is outside the char range");
            return Value(static_cast<char>(static_cast<unsigned char>(number)));
        }
        throw RuntimeError(token, "cannot convert " + value.type_name() + " to char");
    }

    Value convert_to_array(const Token &, const Value &value) {
        if (value.is_array())
            return value;
        if (value.is_string()) {
            const std::string &string = std::get<std::string>(value.data);
            auto result = std::make_shared<ArrayValue>();
            result->reserve(string.size());
            for (const char character : string)
                result->emplace_back(character);
            return Value(std::move(result));
        }
        auto result = std::make_shared<ArrayValue>();
        result->push_back(value);
        return Value(std::move(result));
    }

    Value convert_char_array_to_string(const Token &token, const Value &value) {
        if (!value.is_array())
            throw RuntimeError(token, "CATS requires an array, got " + value.type_name());
        const ArrayPtr &array = std::get<ArrayPtr>(value.data);
        if (!array)
            return Value("");
        std::string result;
        result.reserve(array->size());
        for (std::size_t index = 0; index < array->size(); ++index) {
            const Value &element = (*array)[index];
            if (!element.is_char())
                throw RuntimeError(token, "CATS requires an array of char, but element " + std::to_string(index) +
                                              " is " + element.type_name());
            result.push_back(std::get<char>(element.data));
        }
        return Value(std::move(result));
    }

    CallablePtr make_native(std::string name, std::size_t min_arity, std::size_t max_arity,
                            NativeFunction::Function function) {
        return std::make_shared<NativeFunction>(std::move(name), min_arity, max_arity, std::move(function));
    }

    CallablePtr make_native(std::string name, std::size_t arity, NativeFunction::Function function) {
        return make_native(std::move(name), arity, arity, std::move(function));
    }

    Value sequence_length(const Token &token, const Value &value) {
        std::size_t size = 0;
        if (value.is_string()) {
            size = std::get<std::string>(value.data).size();
        } else if (value.is_array()) {
            const ArrayPtr &array = std::get<ArrayPtr>(value.data);
            size = array ? array->size() : 0;
        } else if (value.is_map()) {
            const MapPtr &map = std::get<MapPtr>(value.data);
            size = map ? map->table.size() : 0;
        } else {
            throw RuntimeError(token, "len requires string, array, or map, got " + value.type_name());
        }
        if (size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
            throw RuntimeError(token, "sequence is too large to represent its length");
        return Value(static_cast<std::int64_t>(size));
    }

    std::size_t checked_index(const Token &token, const Value &value, std::size_t size) {
        if (!value.is_integer_number())
            throw RuntimeError(token, "sequence index must be an integer, got " + value.type_name());
        const std::int64_t index = value.number_as_integer();
        if (index < 0 || static_cast<std::uint64_t>(index) >= static_cast<std::uint64_t>(size))
            throw RuntimeError(token, "index " + std::to_string(index) + " is out of range for sequence of size " +
                                          std::to_string(size));
        return static_cast<std::size_t>(index);
    }

    void normalize_char_assignment(const Token &operation, const Value &previous, Value &result) {
        if (!previous.is_char())
            return;
        if (!result.is_integer_number())
            throw RuntimeError(operation, "compound assignment on char must produce an integer");
        const std::int64_t code = result.number_as_integer();
        if (code < 0 || code > 255)
            throw RuntimeError(operation, "char value is outside range 0..255");
        result = Value(static_cast<char>(static_cast<unsigned char>(code)));
    }
}

Interpreter::Interpreter(std::ostream &output, std::ostream &diagnostics)
    : globals_(std::make_shared<Environment>()), environment_(globals_), output_(output), diagnostics_(diagnostics) {
    environment_pool_.reserve(64);

    globals_->define("Int", Value(make_native("Int", 1, [](Interpreter &, const Token &token,
                                                            const std::vector<Value> &arguments) {
        return convert_to_int(token, arguments[0]);
    })));
    globals_->define("Double", Value(make_native("Double", 1, [](Interpreter &, const Token &token,
                                                                  const std::vector<Value> &arguments) {
        return convert_to_double(token, arguments[0]);
    })));
    globals_->define("Bool", Value(make_native("Bool", 1, [](Interpreter &, const Token &,
                                                              const std::vector<Value> &arguments) {
        return convert_to_bool(arguments[0]);
    })));
    globals_->define("String", Value(make_native("String", 1, [](Interpreter &, const Token &token,
                                                                  const std::vector<Value> &arguments) {
        return convert_to_string(token, arguments[0]);
    })));
    globals_->define("Type", Value(make_native("Type", 1, [](Interpreter &, const Token &,
                                                              const std::vector<Value> &arguments) {
        return Value(arguments[0].type_name());
    })));
    globals_->define("Char", Value(make_native("Char", 1, [](Interpreter &, const Token &token,
                                                              const std::vector<Value> &arguments) {
        return convert_to_char(token, arguments[0]);
    })));
    globals_->define("Array", Value(make_native("Array", 1, [](Interpreter &, const Token &token,
                                                                const std::vector<Value> &arguments) {
        return convert_to_array(token, arguments[0]);
    })));
    // Map() empty map; Map(factory) defaultdict-style map with zero-arg factory.
    globals_->define(
        "Map", Value(make_native("Map", 0, 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
            auto map = std::make_shared<MapData>();
            if (!arguments.empty()) {
                if (!arguments[0].is_callable())
                    throw RuntimeError(token, "Map factory must be callable, got " + arguments[0].type_name());
                map->default_factory = arguments[0];
            }
            return Value(std::move(map));
        })));
    globals_->define("CATS", Value(make_native("CATS", 1, [](Interpreter &, const Token &token,
                                                              const std::vector<Value> &arguments) {
        return convert_char_array_to_string(token, arguments[0]);
    })));
    globals_->define("len", Value(make_native("len", 1, [](Interpreter &, const Token &token,
                                                            const std::vector<Value> &arguments) {
        return sequence_length(token, arguments[0]);
    })));
    globals_->define("sleep", Value(make_native("sleep", 1, [](Interpreter &, const Token &token,
                                                            const std::vector<Value> &arguments) {
        const Value& value = arguments[0];
        if (!value.is_integer_number()) {
            throw RuntimeError(token, "sleep expected integer number, got " + value.type_name());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(value.number_as_integer()));
        return Value(nullptr);
    })));
}

void Interpreter::interpret(const Program &program) {
    execute_statements(program);
    if (control_flow_ != ControlFlow::None)
        throw std::logic_error("unexpected control flow escaped top-level program");
}

Value Interpreter::evaluate(const Expr &expression) {
    return std::visit([this](const auto &node) { return evaluate_node(node); }, expression.node);
}

void Interpreter::execute(const Stmt &statement) {
    if (control_flow_ == ControlFlow::None)
        std::visit([this](const auto &node) { execute_node(node); }, statement.node);
}

void Interpreter::execute_statements(const std::vector<StmtPtr> &statements) {
    for (const StmtPtr &statement : statements) {
        if (control_flow_ != ControlFlow::None)
            break;
        execute(*statement);
    }
}

std::shared_ptr<Environment> Interpreter::acquire_environment(std::shared_ptr<Environment> parent, std::size_t slots) {
    if (environment_pool_.empty())
        return std::make_shared<Environment>(std::move(parent), slots);

    std::shared_ptr<Environment> environment = std::move(environment_pool_.back());
    environment_pool_.pop_back();
    environment->reset(std::move(parent), slots);
    return environment;
}

void Interpreter::release_environment(std::shared_ptr<Environment> environment) {
    if (!environment || environment.use_count() != 1 || environment_pool_.size() >= 64)
        return;
    environment->reset(nullptr, 0);
    environment_pool_.push_back(std::move(environment));
}

void Interpreter::execute_block(const std::vector<StmtPtr> &statements, std::shared_ptr<Environment> environment) {
    const std::shared_ptr<Environment> previous = environment_;
    environment_ = std::move(environment);
    try {
        execute_statements(statements);
    } catch (...) {
        environment_ = previous;
        throw;
    }
    environment_ = previous;
}

void Interpreter::execute_in_environment(const Stmt &statement, std::shared_ptr<Environment> environment) {
    const std::shared_ptr<Environment> previous = environment_;
    environment_ = std::move(environment);
    try {
        execute(statement);
    } catch (...) {
        environment_ = previous;
        throw;
    }
    environment_ = previous;
}

Value Interpreter::execute_function_body(const std::vector<StmtPtr> &statements,
                                         std::shared_ptr<Environment> environment) {
    const ControlFlow saved_flow = control_flow_;
    Value saved_return = std::move(return_value_);
    control_flow_ = ControlFlow::None;
    return_value_ = Value(nullptr);

    try {
        execute_block(statements, std::move(environment));
    } catch (...) {
        control_flow_ = saved_flow;
        return_value_ = std::move(saved_return);
        throw;
    }

    const ControlFlow completed_flow = control_flow_;
    Value result = completed_flow == ControlFlow::Return ? std::move(return_value_) : Value(nullptr);
    control_flow_ = saved_flow;
    return_value_ = std::move(saved_return);

    if (completed_flow == ControlFlow::Break || completed_flow == ControlFlow::Continue)
        throw std::logic_error("loop control escaped a function body");
    return result;
}

Value Interpreter::evaluate_node(const LiteralExpr &expression) {
    if (!expression.decoded) {
        const Token &token = expression.value;
        switch (token.type) {
        case TokenType::Number:
            expression.cached = token.lexeme.find('.') != std::string::npos
                                    ? CachedLiteral(parse_double(token))
                                    : CachedLiteral(parse_integer(token));
            break;
        case TokenType::String: expression.cached = parse_string(token); break;
        case TokenType::True: expression.cached = true; break;
        case TokenType::False: expression.cached = false; break;
        case TokenType::Null: expression.cached = std::monostate{}; break;
        case TokenType::Char: expression.cached = parse_char(token); break;
        default: throw RuntimeError(token, "invalid literal");
        }
        expression.decoded = true;
    }

    return std::visit([](const auto &cached) -> Value {
        using T = std::decay_t<decltype(cached)>;
        if constexpr (std::is_same_v<T, std::monostate>)
            return Value(nullptr);
        else
            return Value(cached);
    }, expression.cached);
}

Value Interpreter::evaluate_node(const VariableExpr &expression) { return environment_->get(expression.name); }
Value Interpreter::evaluate_node(const GroupingExpr &expression) { return evaluate(*expression.expression); }

Value Interpreter::evaluate_node(const AssignmentExpr &expression) {
    if (const auto *variable = std::get_if<VariableExpr>(&expression.target->node)) {
        Value &destination = environment_->get_ref(variable->name);
        const Value previous = destination;
        const Value right = evaluate(*expression.value);

        if (expression.op.type == TokenType::Equal) {
            destination = right;
            return right;
        }

        Value result = apply_binary(expression.op, previous, right);
        normalize_char_assignment(expression.op, previous, result);
        destination = result;
        return result;
    }

    ResolvedTarget target = resolve_target(*expression.target);
    const Value right = evaluate(*expression.value);
    if (expression.op.type == TokenType::Equal) {
        target.write(right);
        return right;
    }

    Value result = apply_binary(expression.op, target.value, right);
    normalize_char_assignment(expression.op, target.value, result);
    target.write(result);
    return result;
}

Value Interpreter::evaluate_node(const ArrayExpr &expression) {
    auto array = std::make_shared<ArrayValue>();
    array->reserve(expression.elements.size());
    for (const ExprPtr &element : expression.elements)
        array->push_back(evaluate(*element));
    return Value(std::move(array));
}

Value Interpreter::evaluate_node(const MapExpr &expression) {
    auto map = std::make_shared<MapData>();
    for (const auto &entry : expression.elements) {
        Value key = evaluate(*entry.first);
        Value value = evaluate(*entry.second);
        map->table.insert_or_assign(std::move(key), std::move(value));
    }
    return Value(std::move(map));
}

Value Interpreter::evaluate_node(const UnaryExpr &expression) {
    const Value right = evaluate(*expression.right);
    switch (expression.operation.type) {
    case TokenType::Minus:
        if (const auto *integer = std::get_if<std::int64_t>(&right.data))
            return Value(-*integer);
        if (!right.is_number())
            throw RuntimeError(expression.operation,
                               "operator '-' requires a numeric operand, got " + right.type_name());
        return right.is_double() ? Value(-right.number_as_double()) : Value(-right.number_as_integer());
    case TokenType::Not: return Value(!right.is_truthy());
    default:
        throw RuntimeError(expression.operation,
                           "Interpreter: unknown unary operator '" + expression.operation.lexeme + "'");
    }
}

Value Interpreter::evaluate_node(const BinaryExpr &expression) {
    const Value left = evaluate(*expression.left);
    if (expression.operation.type == TokenType::AndAnd)
        return !left.is_truthy() ? Value(false) : Value(evaluate(*expression.right).is_truthy());
    if (expression.operation.type == TokenType::OrOr)
        return left.is_truthy() ? Value(true) : Value(evaluate(*expression.right).is_truthy());
    const Value right = evaluate(*expression.right);
    return apply_binary(expression.operation, left, right);
}

Value Interpreter::evaluate_node(const CallExpr &expression) {
    const Value callee = evaluate(*expression.callee);
    if (!callee.is_callable())
        throw RuntimeError(expression.closing_parenthesis, "value of type " + callee.type_name() + " is not callable");

    const CallablePtr &callable = std::get<CallablePtr>(callee.data);
    if (!callable)
        throw RuntimeError(expression.closing_parenthesis, "cannot call NULL function");
    if (call_depth_ >= ChompoConfig::MaxCallDepth)
        throw RuntimeError(expression.closing_parenthesis, "Runtime StackOverflow: maximum call depth of " +
                                                               std::to_string(ChompoConfig::MaxCallDepth) + " exceeded");

    CallDepthGuard depth_guard(call_depth_);
    const std::size_t buffer_index = call_depth_ - 1;
    while (argument_buffers_.size() <= buffer_index)
        argument_buffers_.emplace_back();

    std::vector<Value> &arguments = argument_buffers_[buffer_index];
    arguments.clear();
    if (arguments.capacity() < expression.arguments.size())
        arguments.reserve(expression.arguments.size());
    for (const ExprPtr &argument : expression.arguments)
        arguments.push_back(evaluate(*argument));

    if (!callable->accepts_arity(arguments.size()))
        throw RuntimeError(expression.closing_parenthesis, "function '" + callable->name() + "' expects " +
                                                               callable->arity_description() + " argument(s), got " +
                                                               std::to_string(arguments.size()));

    Value result = callable->call(*this, expression.closing_parenthesis, arguments);
    arguments.clear();
    return result;
}

Value Interpreter::evaluate_node(const IndexExpr &expression) {
    const Value object = evaluate(*expression.object);
    const Value index = evaluate(*expression.index);

    if (object.is_array()) {
        const ArrayPtr &array = std::get<ArrayPtr>(object.data);
        const std::size_t size = array ? array->size() : 0;
        return (*array)[checked_index(expression.bracket, index, size)];
    }
    if (object.is_string()) {
        const std::string &string = std::get<std::string>(object.data);
        return Value(string[checked_index(expression.bracket, index, string.size())]);
    }
    if (object.is_map())
        return map_lookup(*this, expression.bracket, std::get<MapPtr>(object.data), index);
    throw RuntimeError(expression.bracket, "operator '[]' cannot be applied to " + object.type_name());
}

Value Interpreter::evaluate_node(const UpdateExpr &expression) {
    auto update_value = [&](Value &current) -> Value {
        const Value previous = current;
        const std::int64_t delta = expression.operation.type == TokenType::PlusOne ? 1 : -1;

        if (auto *integer = std::get_if<std::int64_t>(&current.data)) {
            *integer += delta;
        } else if (auto *number = std::get_if<double>(&current.data)) {
            *number += static_cast<double>(delta);
        } else if (auto *character = std::get_if<char>(&current.data)) {
            const std::int64_t code = static_cast<unsigned char>(*character) + delta;
            if (code < 0 || code > 255)
                throw RuntimeError(expression.operation, "char increment or decrement is outside range 0..255");
            *character = static_cast<char>(static_cast<unsigned char>(code));
        } else {
            if (!current.is_number())
                throw RuntimeError(expression.operation, "operator '" + expression.operation.lexeme +
                                                             "' requires a numeric target, got " + current.type_name());
            current = apply_binary(expression.operation, previous, Value(std::int64_t{1}));
        }
        return expression.prefix ? current : previous;
    };

    if (const auto *variable = std::get_if<VariableExpr>(&expression.target->node)) {
        Value &current = environment_->get_ref(variable->name);
        return update_value(current);
    }

    ResolvedTarget target = resolve_target(*expression.target);
    Value current = target.value;
    Value result = update_value(current);
    target.write(current);
    return result;
}

void Interpreter::execute_node(const EmptyStmt &) {}
void Interpreter::execute_node(const ExpressionStmt &statement) { evaluate(*statement.expression); }

void Interpreter::execute_node(const VarStmt &statement) {
    Value value(nullptr);
    if (statement.initializer)
        value = evaluate(*statement.initializer);
    environment_->define(statement.name, std::move(value));
}

void Interpreter::execute_node(const PrintStmt &statement) {
    for (const ExprPtr &argument : statement.arguments)
        output_ << evaluate(*argument).to_string();
}

void Interpreter::execute_node(const BlockStmt &statement) {
    if (statement.scope_slots == 0) {
        execute_statements(statement.statements);
        return;
    }

    std::shared_ptr<Environment> block = acquire_environment(environment_, statement.scope_slots);
    try {
        execute_block(statement.statements, block);
    } catch (...) {
        release_environment(std::move(block));
        throw;
    }
    release_environment(std::move(block));
}

void Interpreter::execute_node(const IfStmt &statement) {
    if (evaluate(*statement.condition).is_truthy())
        execute(*statement.then_branch);
    else if (statement.else_branch)
        execute(*statement.else_branch);
}

void Interpreter::execute_node(const WhileStmt &statement) {
    while (evaluate(*statement.condition).is_truthy()) {
        execute(*statement.body);
        if (control_flow_ == ControlFlow::Return)
            return;
        if (control_flow_ == ControlFlow::Break) {
            control_flow_ = ControlFlow::None;
            return;
        }
        if (control_flow_ == ControlFlow::Continue)
            control_flow_ = ControlFlow::None;
    }
}

void Interpreter::execute_node(const BreakStmt &) { control_flow_ = ControlFlow::Break; }
void Interpreter::execute_node(const ContinueStmt &) { control_flow_ = ControlFlow::Continue; }

void Interpreter::execute_node(const FunctionStmt &statement) {
    CallablePtr function = std::make_shared<UserFunction>(statement, environment_);
    environment_->define(statement.name, Value(std::move(function)));
}

void Interpreter::execute_node(const ReturnStmt &statement) {
    return_value_ = statement.value ? evaluate(*statement.value) : Value(nullptr);
    control_flow_ = ControlFlow::Return;
}

void Interpreter::execute_node(const ForInStmt &statement) {
    const Value iterable = evaluate(*statement.iterable);
    std::vector<Value> snapshot;

    if (iterable.is_array()) {
        const ArrayPtr &array = std::get<ArrayPtr>(iterable.data);
        if (array)
            snapshot = *array;
    } else if (iterable.is_string()) {
        const std::string &string = std::get<std::string>(iterable.data);
        snapshot.reserve(string.size());
        for (const char character : string)
            snapshot.emplace_back(character);
    } else {
        throw RuntimeError(statement.keyword, "for-in requires array or string, got " + iterable.type_name());
    }

    for (Value &element : snapshot) {
        std::shared_ptr<Environment> iteration =
            acquire_environment(environment_, statement.variable.scope_slots);
        iteration->define(statement.variable, std::move(element));

        try {
            execute_in_environment(*statement.body, iteration);
        } catch (...) {
            release_environment(std::move(iteration));
            throw;
        }
        release_environment(std::move(iteration));

        if (control_flow_ == ControlFlow::Return)
            return;
        if (control_flow_ == ControlFlow::Break) {
            control_flow_ = ControlFlow::None;
            return;
        }
        if (control_flow_ == ControlFlow::Continue)
            control_flow_ = ControlFlow::None;
    }
}

Interpreter::ResolvedTarget Interpreter::resolve_target(const Expr &expression) {
    if (const auto *variable = std::get_if<VariableExpr>(&expression.node)) {
        const Token name = variable->name;
        return ResolvedTarget{environment_->get(name),
                              [this, name](Value value) { environment_->assign(name, std::move(value)); }};
    }

    const auto *index = std::get_if<IndexExpr>(&expression.node);
    if (!index)
        throw std::logic_error("invalid assignment target");

    ResolvedTarget object = resolve_target(*index->object);
    const Value index_value = evaluate(*index->index);

    if (object.value.is_array()) {
        const ArrayPtr array = std::get<ArrayPtr>(object.value.data);
        const std::size_t position = checked_index(index->bracket, index_value, array ? array->size() : 0);
        const Token bracket = index->bracket;
        return ResolvedTarget{(*array)[position], [array, position, bracket](Value value) {
                                  if (value.contains_array(array.get()))
                                      throw RuntimeError(bracket, "cyclic array references are not allowed");
                                  (*array)[position] = std::move(value);
                              }};
    }

    if (object.value.is_string()) {
        std::string string = std::get<std::string>(object.value.data);
        const std::size_t position = checked_index(index->bracket, index_value, string.size());
        Value current(string[position]);
        auto write_parent = std::move(object.write);
        const Token bracket = index->bracket;
        return ResolvedTarget{std::move(current),
                              [string = std::move(string), position, write_parent = std::move(write_parent),
                               bracket](Value value) mutable {
                                  if (!value.is_char())
                                      throw RuntimeError(bracket,
                                                         "string element must be char, got " + value.type_name());
                                  string[position] = std::get<char>(value.data);
                                  write_parent(Value(string));
                              }};
    }

    if (object.value.is_map()) {
        const MapPtr map = std::get<MapPtr>(object.value.data);
        if (!map)
            throw RuntimeError(index->bracket, "operator '[]' cannot be applied to null map");
        Value current(nullptr);
        auto existing = map->table.find(index_value);
        if (existing != map->table.end())
            current = existing->second;
        return ResolvedTarget{std::move(current), [map, index_value](Value value) {
                                  map->table.insert_or_assign(index_value, std::move(value));
                              }};
    }

    throw RuntimeError(index->bracket, "operator '[]' cannot be applied to " + object.value.type_name());
}

void Interpreter::warning(const Token &token, const std::string &message) {
    if constexpr (ChompoConfig::EnableRuntimeWarnings)
        diagnostics_ << "Runtime warning at " << token.position.line << ':' << token.position.column << ": " << message
                     << '\n';
}

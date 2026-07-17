#include "interpreter.h"
#include "callable.h"
#include "config.h"
#include "return_signal.h"
#include "runtime_error.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <variant>

namespace {
    std::int64_t parse_integer(const Token &token) {
        std::int64_t value = 0;

        const char *begin = token.lexeme.data();
        const char *end = begin + token.lexeme.size();

        const auto [position, error] = std::from_chars(begin, end, value);

        if (error != std::errc{} || position != end) {
            throw RuntimeError(token, "invalid integer literal '" + token.lexeme + "'");
        }

        return value;
    }
    std::string parse_string(const Token &token) {
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
    double parse_double(const Token &token) {
        double value = 0.0;

        const char *begin = token.lexeme.data();
        const char *end = begin + token.lexeme.size();

        const auto [position, error] = std::from_chars(begin, end, value, std::chars_format::general);

        if (error != std::errc{} || position != end) {
            throw RuntimeError(token, "invalid double literal '" + token.lexeme + "'");
        }

        return value;
    }
    Value parse_number(const Token &token) {
        if (token.lexeme.find('.') != std::string::npos)
            return Value(parse_double(token));

        return Value(parse_integer(token));
    }
    char parse_char(const Token &token) {
        if (token.lexeme.size() < 3 || token.lexeme.front() != '\'' || token.lexeme.back() != '\'') {
            throw RuntimeError(token, "invalid character literal");
        }

        if (token.lexeme[1] != '\\') {
            if (token.lexeme.size() != 3)
                throw RuntimeError(token, "character literal must contain exactly one character");

            return token.lexeme[1];
        }

        if (token.lexeme.size() != 4)
            throw RuntimeError(token, "invalid character escape sequence");

        switch (token.lexeme[2]) {
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case 'r':
            return '\r';
        case '0':
            return '\0';
        case '\\':
            return '\\';
        case '\'':
            return '\'';
        default:
            throw RuntimeError(token, "unknown character escape sequence");
        }
    }

    bool values_equal(const Value &left, const Value &right) {
        if (left.is_number() && right.is_number()) {
            if (left.is_double() || right.is_double()) {
                return left.number_as_double() == right.number_as_double();
            }

            return left.number_as_integer() == right.number_as_integer();
        }

        if (left.data.index() != right.data.index())
            return false;

        if (left.is_null())
            return true;

        if (left.is_bool())
            return std::get<bool>(left.data) == std::get<bool>(right.data);
        if (left.is_integer())
            return std::get<std::int64_t>(left.data) == std::get<std::int64_t>(right.data);
        if (left.is_string())
            return std::get<std::string>(left.data) == std::get<std::string>(right.data);
        if (left.is_array()) {
            const ArrayPtr &left_array = std::get<ArrayPtr>(left.data);
            const ArrayPtr &right_array = std::get<ArrayPtr>(right.data);

            if (left_array == right_array)
                return true;
            if (!left_array || !right_array)
                return false;
            if (left_array->size() != right_array->size())
                return false;

            for (auto &&[left_element, right_element] : std::views::zip(*left_array, *right_array)) {
                if (!values_equal(left_element, right_element))
                    return false;
            }

            return true;
        }
        return false;
    }

    Value concatenate_arrays(const ArrayPtr &left, const ArrayPtr &right) {
        auto result = std::make_shared<ArrayValue>();

        const std::size_t left_size = left ? left->size() : 0;

        const std::size_t right_size = right ? right->size() : 0;

        if (right_size > result->max_size() - left_size) {
            throw std::length_error("array concatenation is too large");
        }

        result->reserve(left_size + right_size);

        if (left) {
            result->insert(result->end(), left->begin(), left->end());
        }

        if (right) {
            result->insert(result->end(), right->begin(), right->end());
        }

        return Value(std::move(result));
    }
    Value repeat_string(const Token &operation, const std::string &string, std::int64_t count) {
        if (count < 0) {
            throw RuntimeError(operation, "string multiplication count cannot be negative");
        }

        std::string result;

        for (std::int64_t index = 0; index < count; ++index)
            result += string;

        return Value(std::move(result));
    }
    Value repeat_array(const Token &operation, const ArrayPtr &array, std::int64_t count) {
        if (count < 0) {
            throw RuntimeError(operation, "array multiplication count "
                                          "cannot be negative");
        }

        auto result = std::make_shared<ArrayValue>();

        if (!array || array->empty() || count == 0)
            return Value(std::move(result));

        const std::size_t repeats = static_cast<std::size_t>(count);

        if (repeats > result->max_size() / array->size()) {
            throw RuntimeError(operation, "repeated array is too large");
        }

        result->reserve(repeats * array->size());

        for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
            result->insert(result->end(), array->begin(), array->end());
        }

        return Value(std::move(result));
    }

    bool can_convert_to_string_implicitly(const Value &value) {
        return value.is_null() || value.is_number() || value.is_string() || value.is_char() || value.is_array();
    }

    TokenType binary_operator_type(TokenType type) {
        switch (type) {
        case TokenType::PlusEq:
            return TokenType::Plus;

        case TokenType::MinusEq:
            return TokenType::Minus;

        case TokenType::MulEq:
            return TokenType::Star;

        case TokenType::DivideEq:
            return TokenType::Slash;

        case TokenType::PlusOne:
            return TokenType::Plus;

        case TokenType::MinusOne:
            return TokenType::Minus;

        default:
            return type;
        }
    }
    [[noreturn]] void binary_type_error(const Token &operation, const Value &left, const Value &right) {
        throw RuntimeError(operation, "operator '" + operation.lexeme + "' cannot be applied to " + left.type_name() +
                                          " and " + right.type_name());
    }
    Value apply_binary(const Token &operation, const Value &left, const Value &right) {
        const TokenType type = binary_operator_type(operation.type);

        if (type == TokenType::EqualEqual)
            return Value(values_equal(left, right));

        if (type == TokenType::NotEqual)
            return Value(!values_equal(left, right));

        if (type == TokenType::Plus && (left.is_string() || right.is_string())) {
            if (!can_convert_to_string_implicitly(left) || !can_convert_to_string_implicitly(right)) {
                binary_type_error(operation, left, right);
            }

            return Value(left.to_string() + right.to_string());
        }
        if (type == TokenType::Plus && left.is_array() && right.is_array()) {
            return concatenate_arrays(std::get<ArrayPtr>(left.data), std::get<ArrayPtr>(right.data));
        }

        if (type == TokenType::Star) {
            if (left.is_string() && right.is_integer_number()) {
                return repeat_string(operation, std::get<std::string>(left.data), right.number_as_integer());
            }

            if (right.is_string() && left.is_integer_number()) {
                return repeat_string(operation, std::get<std::string>(right.data), left.number_as_integer());
            }

            if (left.is_array() && right.is_integer_number()) {
                return repeat_array(operation, std::get<ArrayPtr>(left.data), right.number_as_integer());
            }

            if (right.is_array() && left.is_integer_number()) {
                return repeat_array(operation, std::get<ArrayPtr>(right.data), left.number_as_integer());
            }
        }

        if (!left.is_number() || !right.is_number())
            binary_type_error(operation, left, right);

        const bool use_double = left.is_double() || right.is_double();

        switch (type) {
        case TokenType::Plus:
            if (use_double) {
                return Value(left.number_as_double() + right.number_as_double());
            }

            return Value(left.number_as_integer() + right.number_as_integer());

        case TokenType::Minus:
            if (use_double) {
                return Value(left.number_as_double() - right.number_as_double());
            }

            return Value(left.number_as_integer() - right.number_as_integer());

        case TokenType::Star:
            if (use_double) {
                return Value(left.number_as_double() * right.number_as_double());
            }

            return Value(left.number_as_integer() * right.number_as_integer());

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
            if (!left.is_integer_number() || !right.is_integer_number()) {
                binary_type_error(operation, left, right);
            }

            const std::int64_t denominator = right.number_as_integer();

            if (denominator == 0)
                throw RuntimeError(operation, "division by zero");

            return Value(left.number_as_integer() % denominator);
        }

        case TokenType::Less:
            if (use_double) {
                return Value(left.number_as_double() < right.number_as_double());
            }

            return Value(left.number_as_integer() < right.number_as_integer());

        case TokenType::LessEqual:
            if (use_double) {
                return Value(left.number_as_double() <= right.number_as_double());
            }

            return Value(left.number_as_integer() <= right.number_as_integer());

        case TokenType::Greater:
            if (use_double) {
                return Value(left.number_as_double() > right.number_as_double());
            }

            return Value(left.number_as_integer() > right.number_as_integer());

        case TokenType::GreaterEqual:
            if (use_double) {
                return Value(left.number_as_double() >= right.number_as_double());
            }

            return Value(left.number_as_integer() >= right.number_as_integer());

        default:
            throw RuntimeError(operation, "unknown binary operator '" + operation.lexeme + "'");
        }
    }

    Value convert_to_int(const Token &token, const Value &value) {
        if (value.is_integer_number())
            return Value(value.number_as_integer());

        if (value.is_double()) {
            const double number = value.number_as_double();

            if (!std::isfinite(number)) {
                throw RuntimeError(token, "cannot convert non-finite double to integer");
            }

            const long double extended = number;

            if (extended < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                extended > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
                throw RuntimeError(token, "double is outside the integer range");
            }

            return Value(static_cast<std::int64_t>(number));
        }

        if (value.is_string()) {
            const std::string &string = std::get<std::string>(value.data);

            std::int64_t result = 0;

            const char *begin = string.data();
            const char *end = begin + string.size();

            const auto [position, error] = std::from_chars(begin, end, result);

            if (error != std::errc{} || position != end) {
                throw RuntimeError(token, "cannot convert '" + string + "' to integer");
            }

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

            const char *begin = string.data();
            const char *end = begin + string.size();

            const auto [position, error] = std::from_chars(begin, end, result, std::chars_format::general);

            if (error != std::errc{} || position != end) {
                throw RuntimeError(token, "cannot convert '" + string + "' to double");
            }

            return Value(result);
        }

        if (value.is_null())
            return Value(0.0);

        throw RuntimeError(token, "cannot convert " + value.type_name() + " to double");
    }
    Value convert_to_bool(const Value &value) { return Value(value.is_truthy()); }
    Value convert_to_string(const Token &token, const Value &value) {
        if (value.is_callable()) {
            throw RuntimeError(token, "cannot convert function to string");
        }

        return Value(value.to_string());
    }
    Value convert_to_char(const Token &token, const Value &value) {
        if (value.is_char())
            return value;

        if (value.is_string()) {
            const std::string &string = std::get<std::string>(value.data);

            if (string.size() != 1) {
                throw RuntimeError(token, "string must contain exactly one byte");
            }

            return Value(string[0]);
        }

        if (value.is_integer()) {
            const std::int64_t number = value.number_as_integer();

            if (number < 0 || number > 255) {
                throw RuntimeError(token, "integer is outside the char range");
            }

            return Value(static_cast<char>(static_cast<unsigned char>(number)));
        }

        throw RuntimeError(token, "cannot convert " + value.type_name() + " to char");
    }
    Value convert_to_array(const Token &token, const Value &value) {
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

        return Value(std::make_shared<ArrayValue>(std::vector<Value>{std::move(value)}));
    }
    Value convert_char_array_to_string(const Token &token, const Value &value) {
        if (!value.is_array()) {
            throw RuntimeError(token, "CATS requires an array, got " + value.type_name());
        }

        const ArrayPtr &array = std::get<ArrayPtr>(value.data);

        if (!array)
            return Value("");

        std::string result;
        result.reserve(array->size());

        for (std::size_t index = 0; index < array->size(); ++index) {
            const Value &element = (*array)[index];

            if (!element.is_char()) {
                throw RuntimeError(token, "CATS requires an array of char, but element " + std::to_string(index) +
                                              " is " + element.type_name());
            }

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
        } else {
            throw RuntimeError(token, "len requires string or array, got " + value.type_name());
        }

        if (size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            throw RuntimeError(token, "sequence is too large to represent its length");
        }

        return Value(static_cast<std::int64_t>(size));
    }

    std::size_t checked_index(const Token &token, const Value &value, std::size_t size) {
        if (!value.is_integer_number()) {
            throw RuntimeError(token, "sequence index must be an integer, got " + value.type_name());
        }

        const std::int64_t index = value.number_as_integer();

        if (index < 0 || static_cast<std::uint64_t>(index) >= static_cast<std::uint64_t>(size)) {
            throw RuntimeError(token, "index " + std::to_string(index) + " is out of range for sequence of size " +
                                          std::to_string(size));
        }

        return static_cast<std::size_t>(index);
    }

    bool contains_array_impl(const Value &value, const ArrayValue *target,
                             std::unordered_set<const ArrayValue *> &visited) {
        if (!value.is_array())
            return false;

        const ArrayPtr &array = std::get<ArrayPtr>(value.data);

        if (!array)
            return false;

        if (array.get() == target)
            return true;

        if (!visited.insert(array.get()).second)
            return false;

        for (const Value &element : *array) {
            if (contains_array_impl(element, target, visited)) {
                return true;
            }
        }

        return false;
    }

    bool contains_array(const Value &value, const ArrayValue *target) {
        std::unordered_set<const ArrayValue *> visited;

        return contains_array_impl(value, target, visited);
    }
} // namespace

Interpreter::Interpreter(std::ostream &output, std::ostream &diagnostics)
    : globals_(std::make_shared<Environment>()), environment_(globals_), output_(output), diagnostics_(diagnostics) {
    globals_->define(
        "Int", Value(make_native("Int", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
            return convert_to_int(token, arguments[0]);
        })));

    globals_->define(
        "Double",
        Value(make_native("Double", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
            return convert_to_double(token, arguments[0]);
        })));

    globals_->define(
        "Bool", Value(make_native("Bool", 1, [](Interpreter &, const Token &, const std::vector<Value> &arguments) {
            return convert_to_bool(arguments[0]);
        })));

    globals_->define(
        "String",
        Value(make_native("String", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
            return convert_to_string(token, arguments[0]);
        })));
    globals_->define("Type", Value(make_native(
                                 "Type", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                                     return Value(arguments[0].type_name());
                                 })));
    globals_->define("Char", Value(make_native(
                                 "Char", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                                     return convert_to_char(token, arguments[0]);
                                 })));

    globals_->define(
        "Array",
        Value(make_native("Array", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
            return convert_to_array(token, arguments[0]);
        })));

    globals_->define("CATS", Value(make_native(
                                 "CATS", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                                     return convert_char_array_to_string(token, arguments[0]);
                                 })));
    globals_->define(
        "len", Value(make_native("len", 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
            return sequence_length(token, arguments[0]);
        })));
}

void Interpreter::interpret(const Program &program) {
    for (const StmtPtr &statement : program) {
        execute(*statement);
    }
}

Value Interpreter::evaluate(const Expr &expression) {
    return std::visit([this](const auto &node) { return evaluate_node(node); }, expression.node);
}

void Interpreter::execute(const Stmt &statement) {
    std::visit([this](const auto &node) { execute_node(node); }, statement.node);
}

Value Interpreter::evaluate_node(const LiteralExpr &expression) {
    const Token &token = expression.value;
    switch (token.type) {
    case TokenType::Number:
        return parse_number(token);
    case TokenType::String:
        return Value(parse_string(token));
    case TokenType::True:
        return Value(true);
    case TokenType::False:
        return Value(false);
    case TokenType::Null:
        return Value(nullptr);
    case TokenType::Char:
        return Value(parse_char(token));
    default:
        throw RuntimeError(token, "invalid literal");
    }
}
Value Interpreter::evaluate_node(const VariableExpr &expression) { return environment_->get(expression.name); }
Value Interpreter::evaluate_node(const GroupingExpr &expression) { return evaluate(*expression.expression); }
Value Interpreter::evaluate_node(const AssignmentExpr &expression) {
    ResolvedTarget target = resolve_target(*expression.target);

    const Value right = evaluate(*expression.value);

    if (expression.op.type == TokenType::Equal) {
        target.write(right);
        return right;
    }

    Value result = apply_binary(expression.op, target.value, right);

    if (target.value.is_char()) {
        if (!result.is_integer_number()) {
            throw RuntimeError(expression.op, "compound assignment on char must "
                                              "produce an integer");
        }

        const std::int64_t code = result.number_as_integer();

        if (code < 0 || code > 255) {
            throw RuntimeError(expression.op, "char value is outside range 0..255");
        }

        result = Value(static_cast<char>(static_cast<unsigned char>(code)));
    }

    target.write(result);
    return result;
}
Value Interpreter::evaluate_node(const ArrayExpr &expression) {
    auto array = std::make_shared<ArrayValue>();
    array->reserve(expression.elements.size());
    for (const ExprPtr &element : expression.elements) {
        array->push_back(evaluate(*element));
    }
    return Value(std::move(array));
}
Value Interpreter::evaluate_node(const UnaryExpr &expression) {
    const Value right = evaluate(*expression.right);

    switch (expression.operation.type) {
    case TokenType::Minus:
        if (!right.is_number()) {
            throw RuntimeError(expression.operation,
                               "operator '-' requires a numeric operand, got " + right.type_name());
        }

        if (right.is_double())
            return Value(-right.number_as_double());

        return Value(-right.number_as_integer());

    case TokenType::Not:
        return Value(!right.is_truthy());

    default:
        throw RuntimeError(expression.operation,
                           "Interpreter: unknown unary operator '" + expression.operation.lexeme + "'");
    }
}
Value Interpreter::evaluate_node(const BinaryExpr &expression) {
    const Value left = evaluate(*expression.left);

    if (expression.operation.type == TokenType::AndAnd) {
        if (!left.is_truthy())
            return Value(false);

        return Value(evaluate(*expression.right).is_truthy());
    }

    if (expression.operation.type == TokenType::OrOr) {
        if (left.is_truthy())
            return Value(true);

        return Value(evaluate(*expression.right).is_truthy());
    }

    const Value right = evaluate(*expression.right);

    return apply_binary(expression.operation, left, right);
}
Value Interpreter::evaluate_node(const CallExpr &expression) {
    const Value callee = evaluate(*expression.callee);

    if (!callee.is_callable()) {
        throw RuntimeError(expression.closing_parenthesis, "value of type " + callee.type_name() + " is not callable");
    }

    const CallablePtr &callable = std::get<CallablePtr>(callee.data);

    if (!callable) {
        throw RuntimeError(expression.closing_parenthesis, "cannot call NULL function");
    }

    std::vector<Value> arguments;
    arguments.reserve(expression.arguments.size());

    for (const ExprPtr &argument : expression.arguments)
        arguments.push_back(evaluate(*argument));

    if (!callable->accepts_arity(arguments.size())) {
        throw RuntimeError(expression.closing_parenthesis, "function '" + callable->name() + "' expects " +
                                                               callable->arity_description() + " argument(s), got " +
                                                               std::to_string(arguments.size()));
    }
    if (call_depth_ >= ChompoConfig::MaxCallDepth) {
        throw RuntimeError(expression.closing_parenthesis, "Runtime StackOverflow: maximum call depth of " +
                                                               std::to_string(ChompoConfig::MaxCallDepth) +
                                                               " exceeded");
    }

    CallDepthGuard depth_guard(call_depth_);

    return callable->call(*this, expression.closing_parenthesis, arguments);
}
Value Interpreter::evaluate_node(const IndexExpr &expression) {
    const Value object = evaluate(*expression.object);

    const Value index = evaluate(*expression.index);

    if (object.is_array()) {
        const ArrayPtr &array = std::get<ArrayPtr>(object.data);

        const std::size_t size = array ? array->size() : 0;

        const std::size_t position = checked_index(expression.bracket, index, size);

        return (*array)[position];
    }

    if (object.is_string()) {
        const std::string &string = std::get<std::string>(object.data);

        const std::size_t position = checked_index(expression.bracket, index, string.size());

        return Value(string[position]);
    }

    throw RuntimeError(expression.bracket, "operator '[]' cannot be applied to " + object.type_name());
}
Value Interpreter::evaluate_node(const UpdateExpr &expression) {
    ResolvedTarget target = resolve_target(*expression.target);

    if (!target.value.is_number()) {
        throw RuntimeError(expression.operation, "operator '" + expression.operation.lexeme +
                                                     "' requires a numeric target, got " + target.value.type_name());
    }

    const Value previous = target.value;

    Value updated = apply_binary(expression.operation, previous, Value(std::int64_t{1}));

    if (previous.is_char()) {
        const std::int64_t code = updated.number_as_integer();

        if (code < 0 || code > 255) {
            throw RuntimeError(expression.operation, "char increment or decrement "
                                                     "is outside range 0..255");
        }

        updated = Value(static_cast<char>(static_cast<unsigned char>(code)));
    }

    target.write(updated);

    if (expression.prefix)
        return updated;

    return previous;
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
    for (const auto &arg : statement.arguments) {
        output_ << evaluate(*arg).to_string();
    }
}
void Interpreter::execute_node(const BlockStmt &statement) {
    execute_block(statement.statements, std::make_shared<Environment>(environment_));
}
void Interpreter::execute_node(const IfStmt &statement) {
    if (evaluate(*statement.condition).is_truthy()) {
        execute(*statement.then_branch);
        return;
    }
    if (statement.else_branch)
        execute(*statement.else_branch);
}
void Interpreter::execute_node(const FunctionStmt &statement) {
    CallablePtr function = std::make_shared<UserFunction>(statement, environment_);

    environment_->define(statement.name, Value(std::move(function)));
}
void Interpreter::execute_node(const ReturnStmt &statement) {
    Value value(nullptr);

    if (statement.value)
        value = evaluate(*statement.value);

    throw ReturnSignal(std::move(value));
}

void Interpreter::execute_block(const std::vector<StmtPtr> &statements, std::shared_ptr<Environment> environment) {
    const std::shared_ptr<Environment> previous = environment_;
    environment_ = std::move(environment);

    try {
        for (const StmtPtr &statement : statements)
            execute(*statement);
    } catch (...) {
        environment_ = previous;
        throw;
    }
    environment_ = previous;
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

        const std::size_t size = array ? array->size() : 0;

        const std::size_t position = checked_index(index->bracket, index_value, size);

        const Token bracket = index->bracket;

        return ResolvedTarget{(*array)[position], [array, position, bracket](Value value) {
                                  if (contains_array(value, array.get())) {
                                      throw RuntimeError(bracket, "cyclic array references are not allowed");
                                  }

                                  (*array)[position] = std::move(value);
                              }};
    }

    if (object.value.is_string()) {
        std::string string = std::get<std::string>(object.value.data);

        const std::size_t position = checked_index(index->bracket, index_value, string.size());

        Value current(string[position]);

        auto write_parent = std::move(object.write);

        const Token bracket = index->bracket;

        return ResolvedTarget{
            std::move(current), [string = std::move(string), position, write_parent = std::move(write_parent),
                                 bracket](Value value) mutable {
                if (!value.is_char()) {
                    throw RuntimeError(bracket, "string element must be char, got " + value.type_name());
                }

                string[position] = std::get<char>(value.data);

                write_parent(Value(string));
            }};
    }

    throw RuntimeError(index->bracket, "operator '[]' cannot be applied to " + object.value.type_name());
}

void Interpreter::warning(const Token &token, const std::string &message) {
    if constexpr (ChompoConfig::EnableRuntimeWarnings) {
        diagnostics_ << "Runtime warning at " << token.position.line << ":" << token.position.column << ": " << message
                     << '\n';
    }
}
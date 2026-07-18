#include "value.h"
#include "callable.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

Value::Value() : data(std::monostate{}) {}
Value::Value(std::nullptr_t) : data(std::monostate{}) {}
Value::Value(ArrayPtr value) : data(std::move(value)) {}
Value::Value(bool value) : data(value) {}
Value::Value(std::int64_t value) : data(value) {}
Value::Value(std::string value) : data(std::move(value)) {}
Value::Value(const char *value) : data(std::string(value)) {}
Value::Value(double value) : data(value) {}
Value::Value(CallablePtr callable) : data(std::move(callable)) {}
Value::Value(char value) : data(value) {}

bool Value::is_null() const { return std::holds_alternative<std::monostate>(data); }
bool Value::is_bool() const { return std::holds_alternative<bool>(data); }
bool Value::is_integer() const { return std::holds_alternative<std::int64_t>(data); }
bool Value::is_string() const { return std::holds_alternative<std::string>(data); }
bool Value::is_array() const { return std::holds_alternative<ArrayPtr>(data); }
bool Value::is_double() const { return std::holds_alternative<double>(data); }
bool Value::is_callable() const { return std::holds_alternative<CallablePtr>(data); }
bool Value::is_char() const { return std::holds_alternative<char>(data); }

bool Value::is_truthy() const {
    if (is_null())
        return false;
    if (const auto *boolean = std::get_if<bool>(&data))
        return *boolean;
    if (const auto *integer = std::get_if<std::int64_t>(&data))
        return *integer != 0;
    if (const auto *doubler = std::get_if<double>(&data))
        return *doubler != 0.0;
    if (const auto *string = std::get_if<std::string>(&data))
        return !string->empty();
    if (const auto *array = std::get_if<ArrayPtr>(&data))
        return *array && !(*array)->empty();
    if (const auto *character = std::get_if<char>(&data))
        return *character != 0;

    return true;
}

bool Value::is_number() const { return is_bool() || is_integer() || is_double() || is_char(); }

bool Value::is_integer_number() const { return is_bool() || is_integer() || is_char(); }

std::int64_t Value::number_as_integer() const {
    if (const auto *boolean = std::get_if<bool>(&data))
        return *boolean ? 1 : 0;
    if (const auto *integer = std::get_if<std::int64_t>(&data))
        return *integer;
    if (const auto *character = std::get_if<char>(&data))
        return *character;

    throw std::logic_error("value is not an integer number");
}

double Value::number_as_double() const {
    if (const auto *boolean = std::get_if<bool>(&data))
        return *boolean ? 1.0 : 0.0;
    if (const auto *integer = std::get_if<std::int64_t>(&data))
        return static_cast<double>(*integer);
    if (const auto *number = std::get_if<double>(&data))
        return *number;
    if (const auto *character = std::get_if<char>(&data))
        return *character;

    throw std::logic_error("value is not numeric");
}

std::string Value::type_name() const {
    if (is_null())
        return "NULL";
    if (is_bool())
        return "bool";
    if (is_integer())
        return "integer";
    if (is_string())
        return "string";
    if (is_array())
        return "array";
    if (is_double())
        return "double";
    if (is_callable())
        return "callable";
    if (is_char())
        return "char";
    return "unknown";
}

std::string Value::to_string() const {
    if (is_null())
        return "NULL";
    if (const auto *boolean = std::get_if<bool>(&data))
        return *boolean ? "true" : "false";
    if (const auto *integer = std::get_if<std::int64_t>(&data))
        return std::to_string(*integer);
    if (const auto *string = std::get_if<std::string>(&data))
        return *string;

    if (const auto *array = std::get_if<ArrayPtr>(&data)) {
        if (!*array)
            return "{}";

        const std::size_t element_count = (*array)->size();

        std::string result;
        if (element_count <= (result.max_size() - 2) / 3)
            result.reserve(2 + element_count * 3);

        result += '{';

        for (std::size_t index = 0; index < element_count; ++index) {
            if (index > 0)
                result += ", ";

            result += (**array)[index].to_string();
        }

        result += '}';
        return result;
    }

    if (const auto *doubler = std::get_if<double>(&data)) {
        std::ostringstream output;
        output << std::setprecision(15) << *doubler;
        return output.str();
    }

    if (const auto *callable = std::get_if<CallablePtr>(&data)) {
        if (!*callable)
            return "<function>";

        std::string result = "<function ";
        result += (*callable)->name();
        result += '>';
        return result;
    }

    if (const auto *character = std::get_if<char>(&data))
        return std::string(1, *character);

    return "<unknown>";
}

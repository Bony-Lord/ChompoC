#include "value.h"

#include <sstream>
#include <stdexcept>
#include <utility>
#include <iomanip>


Value::Value() : data(std::monostate{}) {}
Value::Value(std::nullptr_t) : data(std::monostate{}) {}
Value::Value(ArrayPtr value) : data(std::move(value)) {}
Value::Value(bool value) : data(value) {}
Value::Value(std::int64_t value) : data(value) {}
Value::Value(std::string value) : data(std::move(value)) {}
Value::Value(const char *value) : data(std::move(std::string(value))) {}
Value::Value(double value) : data(value) {}

bool Value::is_null() const {
    return std::holds_alternative<std::monostate>(data);
}
bool Value::is_bool() const { return std::holds_alternative<bool>(data); }
bool Value::is_integer() const {
    return std::holds_alternative<std::int64_t>(data);
}
bool Value::is_string() const {
    return std::holds_alternative<std::string>(data);
}
bool Value::is_array() const { return std::holds_alternative<ArrayPtr>(data); }
bool Value::is_double() const { return std::holds_alternative<double>(data); }

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
        return !(*array)->empty();

    return true;
}

bool Value::is_number() const {
    return is_bool() || is_integer() || is_double();
}

bool Value::is_integer_number() const { return is_bool() || is_integer(); }

std::int64_t Value::number_as_integer() const {
    if (const auto *boolean = std::get_if<bool>(&data))
        return *boolean ? 1 : 0;

    if (const auto *integer = std::get_if<std::int64_t>(&data))
        return *integer;

    throw std::logic_error("value is not an integer number");
}

double Value::number_as_double() const {
    if (const auto *boolean = std::get_if<bool>(&data))
        return *boolean ? 1.0 : 0.0;

    if (const auto *integer = std::get_if<std::int64_t>(&data))
        return static_cast<double>(*integer);

    if (const auto *number = std::get_if<double>(&data))
        return *number;

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
        if (!*array) {
            return "{}";
        }

        std::string result = "{";
        for (std::size_t index = 0; index < (*array)->size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += (**array)[index].to_string();
        }
        result += "}";
        return result;
    }
    if (const auto *doubler = std::get_if<double>(&data)) {
        std::ostringstream output;
        output << std::setprecision(15) << std::fixed << *doubler;
        return output.str();
    }

    return "<unknown>";
}
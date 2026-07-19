#include "value.h"
#include "callable.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

Value::Value() : data(std::monostate{}) {}
Value::Value(std::nullptr_t) : data(std::monostate{}) {}
Value::Value(ArrayPtr value) : data(std::move(value)) {}
Value::Value(MapPtr value) : data(std::move(value)) {}
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
bool Value::is_map() const { return std::holds_alternative<MapPtr>(data); }
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
    if (const auto *map = std::get_if<MapPtr>(&data))
        return *map && !(*map)->table.empty();
    if (const auto *character = std::get_if<char>(&data))
        return *character != 0;

    return true;
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
        if (contains_array_impl(element, target, visited))
            return true;
    }
    return false;
}

bool Value::contains_array(const ArrayValue *target) const {
    std::unordered_set<const ArrayValue *> visited;
    return contains_array_impl(*this, target, visited);
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
    if (is_map())
        return "map";
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

    if (const auto *map = std::get_if<MapPtr>(&data)) {
        if (!*map)
            return "Map{}";
        std::string result = "Map{";
        bool first = true;
        for (const auto &entry : (*map)->table) {
            if (!first)
                result += ", ";
            first = false;
            result += entry.first.to_string();
            result += ": ";
            result += entry.second.to_string();
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

std::size_t ValueHash::operator()(const Value &val) const {
    return std::visit(
        [](const auto &arg) -> std::size_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return 0;
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::hash<bool>{}(arg) ^ 0x11111111;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::hash<std::int64_t>{}(arg) ^ 0x22222222;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::hash<std::string>{}(arg) ^ 0x33333333;
            } else if constexpr (std::is_same_v<T, double>) {
                return std::hash<double>{}(arg) ^ 0x44444444;
            } else if constexpr (std::is_same_v<T, char>) {
                return std::hash<char>{}(arg) ^ 0x55555555;
            } else {
                if constexpr (std::is_same_v<T, ArrayPtr> || std::is_same_v<T, CallablePtr> ||
                              std::is_same_v<T, MapPtr>) {
                    return std::hash<void *>{}(static_cast<void *>(arg.get())) ^ 0x66666666;
                }
                return 0;
            }
        },
        val.data);
}

bool ValueEqual::operator()(const Value &lhs, const Value &rhs) const {
    if (lhs.data.index() != rhs.data.index())
        return false;

    return std::visit(
        [&rhs](const auto &left_val) -> bool {
            using T = std::decay_t<decltype(left_val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return true;
            } else if constexpr (std::is_same_v<T, ArrayPtr> || std::is_same_v<T, CallablePtr> ||
                                 std::is_same_v<T, MapPtr>) {
                return left_val == std::get<T>(rhs.data);
            } else {
                return left_val == std::get<T>(rhs.data);
            }
        },
        lhs.data);
}

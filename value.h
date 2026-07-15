#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct Value;

using ArrayValue = std::vector<Value>;
using ArrayPtr = std::shared_ptr<ArrayValue>;

struct Value {
    using Storage = std::variant<
        std::monostate, // NULL
        bool,
        std::int64_t,
        std::string,
        ArrayPtr
    >;

    Storage data;

    Value();
    Value(std::nullptr_t);
    Value(bool value);
    Value(std::int64_t value);
    Value(std::string value);
    Value(const char* value);
    Value(ArrayPtr value);

    bool is_null() const;
    bool is_bool() const;
    bool is_integer() const;
    bool is_string() const;
    bool is_array() const;

    bool is_truthy() const;

    std::string type_name() const;
    std::string to_string() const;
};
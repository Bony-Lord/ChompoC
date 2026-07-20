#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct Value;

using ArrayValue = std::vector<Value>;
using ArrayPtr = std::shared_ptr<ArrayValue>;

struct MapData;
using MapPtr = std::shared_ptr<MapData>;

class Callable;
using CallablePtr = std::shared_ptr<Callable>;

struct Value {
    using Storage = std::variant<std::monostate /*NULL*/, bool, std::int64_t, std::string, ArrayPtr, MapPtr, double,
                                 CallablePtr, char>;

    Storage data;

    Value();
    Value(std::nullptr_t);
    Value(bool value);
    Value(std::int64_t value);
    Value(std::string value);
    Value(const char *value);
    Value(ArrayPtr value);
    Value(MapPtr value);
    Value(double value);
    Value(CallablePtr value);
    Value(char value);

    bool is_null() const;
    bool is_bool() const;
    bool is_integer() const;
    bool is_double() const;
    bool is_number() const;
    bool is_integer_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_map() const;
    bool is_callable() const;
    bool is_char() const;
    bool contains_array(const ArrayValue *target) const;

    std::int64_t number_as_integer() const;
    double number_as_double() const;

    bool is_truthy() const;

    std::string type_name() const;
    std::string to_string() const;
};

struct ValueHash {
    std::size_t operator()(const Value &val) const;
};

struct ValueEqual {
    bool operator()(const Value &lhs, const Value &rhs) const;
};

struct MapData {
    std::unordered_map<Value, Value, ValueHash, ValueEqual> table;
    Value default_factory;
};

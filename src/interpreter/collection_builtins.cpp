#include "interpreter.h"
#include "callable.h"
#include "runtime_error.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
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

    bool contains_array(const Value &value, const ArrayValue *target) {
        std::unordered_set<const ArrayValue *> visited;
        return contains_array_impl(value, target, visited);
    }

    ArrayPtr require_array(const Token &token, const Value &value, const std::string &function_name) {
        if (!value.is_array()) {
            throw RuntimeError(token, function_name + " requires array as the first argument, got " +
                                          value.type_name());
        }

        const ArrayPtr &array = std::get<ArrayPtr>(value.data);
        if (!array)
            throw RuntimeError(token, function_name + " received invalid array storage");

        return array;
    }

    std::size_t require_index(const Token &token, const Value &value, std::size_t size,
                              const std::string &function_name) {
        if (!value.is_integer_number())
            throw RuntimeError(token, function_name + " index must be integer, got " + value.type_name());

        const std::int64_t index = value.number_as_integer();
        if (index < 0 || static_cast<std::uint64_t>(index) >= static_cast<std::uint64_t>(size)) {
            throw RuntimeError(token, function_name + " index " + std::to_string(index) +
                                          " is out of range for array of size " + std::to_string(size));
        }

        return static_cast<std::size_t>(index);
    }

    Value array_size_value(const Token &token, std::size_t size) {
        if (size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
            throw RuntimeError(token, "array size is too large to represent as integer");

        return Value(static_cast<std::int64_t>(size));
    }
}

void Interpreter::install_collection_builtins() {
    globals_->define(
        "push",
        Value(std::make_shared<NativeFunction>(
            "push", 2, std::numeric_limits<std::size_t>::max(),
            [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                ArrayPtr array = require_array(token, arguments[0], "push");
                const std::size_t added_count = arguments.size() - 1;

                if (added_count > array->max_size() - array->size())
                    throw RuntimeError(token, "push would make the array too large");

                for (std::size_t index = 1; index < arguments.size(); ++index) {
                    if (arguments[index].is_array() && contains_array(arguments[index], array.get()))
                        throw RuntimeError(token, "cyclic array references are not allowed");
                }

                array->insert(array->end(), arguments.begin() + 1, arguments.end());
                return array_size_value(token, array->size());
            })));

    globals_->define(
        "pop",
        Value(std::make_shared<NativeFunction>(
            "pop", 1, 1,
            [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                ArrayPtr array = require_array(token, arguments[0], "pop");
                if (array->empty())
                    return Value(nullptr);

                Value value = std::move(array->back());
                array->pop_back();
                return value;
            })));

    globals_->define(
        "removeAt",
        Value(std::make_shared<NativeFunction>(
            "removeAt", 2, 2,
            [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                ArrayPtr array = require_array(token, arguments[0], "removeAt");
                const std::size_t index = require_index(token, arguments[1], array->size(), "removeAt");

                Value removed = std::move((*array)[index]);
                array->erase(array->begin() + static_cast<std::ptrdiff_t>(index));
                return removed;
            })));
}

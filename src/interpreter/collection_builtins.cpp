#include "callable.h"
#include "interpreter.h"
#include "runtime_error.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

    ArrayPtr require_array(const Token &token, const Value &value, const std::string &function_name) {
        if (!value.is_array()) {
            throw RuntimeError(token,
                               function_name + " requires array as the first argument, got " + value.type_name());
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
} // namespace

void Interpreter::install_collection_builtins() {
    globals_->define("push", Value(std::make_shared<NativeFunction>(
                                 "push", 2, std::numeric_limits<std::size_t>::max(),
                                 [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                                     ArrayPtr array = require_array(token, arguments[0], "push");
                                     const std::size_t added_count = arguments.size() - 1;

                                     if (added_count > array->max_size() - array->size())
                                         throw RuntimeError(token, "push would make the array too large");

                                     for (std::size_t index = 1; index < arguments.size(); ++index) {
                                         if (arguments[index].contains_array(array.get()))
                                             throw RuntimeError(token, "cyclic array references are not allowed");
                                     }

                                     array->insert(array->end(), arguments.begin() + 1, arguments.end());
                                     return array_size_value(token, array->size());
                                 })));

    globals_->define("pop",
                     Value(std::make_shared<NativeFunction>(
                         "pop", 1, 1, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                             ArrayPtr array = require_array(token, arguments[0], "pop");
                             if (array->empty())
                                 return Value(nullptr);

                             Value value = std::move(array->back());
                             array->pop_back();
                             return value;
                         })));

    globals_->define("removeAt",
                     Value(std::make_shared<NativeFunction>(
                         "removeAt", 2, 2, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                             ArrayPtr array = require_array(token, arguments[0], "removeAt");
                             const std::size_t index = require_index(token, arguments[1], array->size(), "removeAt");

                             Value removed = std::move((*array)[index]);
                             array->erase(array->begin() + static_cast<std::ptrdiff_t>(index));
                             return removed;
                         })));

    globals_->define(
        "removeKey",
        Value(std::make_shared<NativeFunction>(
            "removeKey", 2, 2, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                if (!arguments[0].is_map())
                    throw RuntimeError(token, "removeKey requires map as the first argument, got " +
                                                  arguments[0].type_name());
                const MapPtr &map = std::get<MapPtr>(arguments[0].data);
                if (!map)
                    return Value(nullptr);
                auto it = map->table.find(arguments[1]);
                if (it == map->table.end())
                    return Value(nullptr);
                Value removed = std::move(it->second);
                map->table.erase(it);
                return removed;
            })));

    globals_->define(
        "removeKey",
        Value(std::make_shared<NativeFunction>(
            "removeKey", 2, 2, [](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                if (!arguments[0].is_map())
                    throw RuntimeError(token, "removeKey requires map as the first argument, got " +
                                                  arguments[0].type_name());
                const MapPtr &map = std::get<MapPtr>(arguments[0].data);
                if (!map)
                    return Value(nullptr);
                auto it = map->table.find(arguments[1]);
                if (it == map->table.end())
                    return Value(nullptr);
                Value removed = std::move(it->second);
                map->table.erase(it);
                return removed;
            })));
}

#include "interpreter.h"
#include "callable.h"
#include "io_manager.h"
#include "runtime_error.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
const std::string &require_string_argument(const Token &token, const Value &value, std::string_view description) {
    if (!value.is_string())
        throw RuntimeError(token, std::string(description) + " must be string, got " + value.type_name());
    return std::get<std::string>(value.data);
}

int require_timeout(const Token &token, const Value &value) {
    if (!value.is_integer_number())
        throw RuntimeError(token, "inputPoll timeout must be integer, got " + value.type_name());

    const std::int64_t timeout = value.number_as_integer();
    if (timeout < -1 || timeout > std::numeric_limits<int>::max())
        throw RuntimeError(token, "inputPoll timeout must be -1 or a non-negative 32-bit integer");
    return static_cast<int>(timeout);
}

Value input_result_to_value(IOManager::InputResult result) {
    auto array = std::make_shared<ArrayValue>();

    switch (result.status) {
    case IOManager::InputStatus::Data:
        array->reserve(2);
        array->emplace_back("data");
        array->emplace_back(std::move(result.data));
        break;
    case IOManager::InputStatus::Wait:
        array->emplace_back("wait");
        break;
    case IOManager::InputStatus::Closed:
        array->emplace_back("closed");
        break;
    }

    return Value(std::move(array));
}

template <class Operation> Value perform_io(const Token &token, Operation operation) {
    try {
        return operation();
    } catch (const RuntimeError &) {
        throw;
    } catch (const std::exception &exception) {
        throw RuntimeError(token, exception.what());
    }
}
}

void Interpreter::install_io_builtins(IOManager &io_manager) {
    IOManager *const manager = &io_manager;

    auto define_native = [this](std::string name, std::size_t min_arity, std::size_t max_arity,
                                NativeFunction::Function function) {
        CallablePtr callable = std::make_shared<NativeFunction>(name, min_arity, max_arity, std::move(function));
        globals_->define(std::move(name), Value(std::move(callable)));
    };

    define_native("input", 0, 0, [manager](Interpreter &, const Token &token, const std::vector<Value> &) {
        return perform_io(token, [manager]() {
            std::optional<std::string> line = manager->read_line();
            return line ? Value(std::move(*line)) : Value(nullptr);
        });
    });

    define_native("inputPoll", 0, 1,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      int timeout_ms = 0;
                      if (!arguments.empty())
                          timeout_ms = require_timeout(token, arguments[0]);

                      return perform_io(token, [manager, timeout_ms]() {
                          return input_result_to_value(manager->poll_line(timeout_ms));
                      });
                  });

    define_native("flush", 0, 0, [manager](Interpreter &, const Token &token, const std::vector<Value> &) {
        return perform_io(token, [manager]() {
            manager->flush();
            return Value(nullptr);
        });
    });

    define_native("istream", 0, 1, [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
        std::string_view path = IOManager::StandardPath;
        if (!arguments.empty())
            path = require_string_argument(token, arguments[0], "istream path");
        return perform_io(token, [manager, path]() {
            manager->set_input(path);
            return Value(nullptr);
        });
    });

    define_native("ostream", 0, 2, [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
        std::string_view path = IOManager::StandardPath;
        std::string_view mode = IOManager::RewriteMode;
        if (!arguments.empty())
            path = require_string_argument(token, arguments[0], "ostream path");
        if (arguments.size() >= 2)
            mode = require_string_argument(token, arguments[1], "ostream mode");
        return perform_io(token, [manager, path, mode]() {
            manager->set_output(path, mode);
            return Value(nullptr);
        });
    });

    define_native("iostream", 0, 3, [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
        std::string_view input_path = IOManager::StandardPath;
        std::string_view output_path = IOManager::StandardPath;
        std::string_view output_mode = IOManager::RewriteMode;
        if (!arguments.empty())
            input_path = require_string_argument(token, arguments[0], "iostream input path");
        if (arguments.size() >= 2)
            output_path = require_string_argument(token, arguments[1], "iostream output path");
        if (arguments.size() >= 3)
            output_mode = require_string_argument(token, arguments[2], "iostream output mode");
        return perform_io(token, [manager, input_path, output_path, output_mode]() {
            manager->set_streams(input_path, output_path, output_mode);
            return Value(nullptr);
        });
    });
}

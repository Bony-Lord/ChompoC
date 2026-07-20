#include "interpreter.h"
#include "callable.h"
#include "runtime_error.h"
#include "terminal_manager.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
const std::string &require_terminal_string(const Token &token, const Value &value, std::string_view description) {
    if (!value.is_string())
        throw RuntimeError(token, std::string(description) + " must be string, got " + value.type_name());
    return std::get<std::string>(value.data);
}

bool require_terminal_bool(const Token &token, const Value &value, std::string_view description) {
    if (!value.is_bool())
        throw RuntimeError(token, std::string(description) + " must be bool, got " + value.type_name());
    return std::get<bool>(value.data);
}

int require_terminal_timeout(const Token &token, const Value &value) {
    if (!value.is_integer_number())
        throw RuntimeError(token, "termPoll timeout must be integer, got " + value.type_name());
    const std::int64_t timeout = value.number_as_integer();
    if (timeout < -1 || timeout > std::numeric_limits<int>::max())
        throw RuntimeError(token, "termPoll timeout must be -1 or a non-negative 32-bit integer");
    return static_cast<int>(timeout);
}

Value terminal_result_to_value(TerminalManager::InputResult result) {
    auto array = std::make_shared<ArrayValue>();
    switch (result.status) {
    case TerminalManager::InputStatus::Data:
        array->reserve(2);
        array->emplace_back("data");
        array->emplace_back(std::move(result.data));
        break;
    case TerminalManager::InputStatus::Wait:
        array->emplace_back("wait");
        break;
    case TerminalManager::InputStatus::Closed:
        array->emplace_back("closed");
        break;
    }
    return Value(std::move(array));
}
}

void Interpreter::install_terminal_builtins(TerminalManager &terminal_manager) {
    TerminalManager *const terminal = &terminal_manager;

    auto define_native = [this](std::string name, std::size_t min_arity, std::size_t max_arity,
                                NativeFunction::Function function) {
        CallablePtr callable = std::make_shared<NativeFunction>(name, min_arity, max_arity, std::move(function));
        globals_->define(std::move(name), Value(std::move(callable)));
    };

    define_native("termOpen", 0, 0,
                  [terminal](Interpreter &, const Token &, const std::vector<Value> &) {
                      return Value(terminal->open());
                  });

    define_native("termClose", 0, 0,
                  [terminal](Interpreter &, const Token &, const std::vector<Value> &) {
                      terminal->close();
                      return Value(nullptr);
                  });

    define_native("termInteractive", 0, 0,
                  [terminal](Interpreter &, const Token &, const std::vector<Value> &) {
                      return Value(terminal->interactive());
                  });

    define_native("termSetPrompt", 1, 2,
                  [terminal](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      std::string text = require_terminal_string(token, arguments[0], "termSetPrompt text");
                      std::string style = "accent";
                      if (arguments.size() >= 2)
                          style = require_terminal_string(token, arguments[1], "termSetPrompt style");
                      terminal->set_prompt(std::move(text), std::move(style));
                      return Value(nullptr);
                  });

    define_native("termSetTitle", 1, 1,
                  [terminal](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      terminal->set_title(require_terminal_string(token, arguments[0], "termSetTitle title"));
                      return Value(nullptr);
                  });

    define_native("termClear", 0, 0,
                  [terminal](Interpreter &, const Token &, const std::vector<Value> &) {
                      terminal->clear();
                      return Value(nullptr);
                  });

    define_native("termPrint", 1, 2,
                  [terminal](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const std::string &text = require_terminal_string(token, arguments[0], "termPrint text");
                      std::string_view style = "default";
                      if (arguments.size() >= 2)
                          style = require_terminal_string(token, arguments[1], "termPrint style");
                      terminal->print_line(text, style);
                      return Value(nullptr);
                  });

    define_native("termPoll", 0, 1,
                  [terminal](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      int timeout = 0;
                      if (!arguments.empty())
                          timeout = require_terminal_timeout(token, arguments[0]);
                      return terminal_result_to_value(terminal->poll_line(timeout));
                  });

    define_native("termReadLine", 0, 3,
                  [terminal](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      std::string prompt;
                      bool masked = false;
                      std::string style = "accent";
                      if (!arguments.empty())
                          prompt = require_terminal_string(token, arguments[0], "termReadLine prompt");
                      if (arguments.size() >= 2)
                          masked = require_terminal_bool(token, arguments[1], "termReadLine masked");
                      if (arguments.size() >= 3)
                          style = require_terminal_string(token, arguments[2], "termReadLine style");

                      std::optional<std::string> line = terminal->read_line(std::move(prompt), masked, std::move(style));
                      return line ? Value(std::move(*line)) : Value(nullptr);
                  });
}

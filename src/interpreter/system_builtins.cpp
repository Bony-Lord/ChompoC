#include "interpreter.h"
#include "callable.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

void Interpreter::install_system_builtins(std::vector<std::string> arguments) {
    auto stored_arguments = std::make_shared<const std::vector<std::string>>(std::move(arguments));

    globals_->define(
        "args",
        Value(std::make_shared<NativeFunction>(
            "args", 0, 0,
            [stored_arguments](Interpreter &, const Token &, const std::vector<Value> &) {
                auto result = std::make_shared<ArrayValue>();
                result->reserve(stored_arguments->size());
                for (const std::string &argument : *stored_arguments)
                    result->emplace_back(argument);
                return Value(std::move(result));
            })));
}

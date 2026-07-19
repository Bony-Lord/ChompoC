#include "interpreter.h"
#include "callable.h"
#include "value.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

Value local_clock_time() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t epoch = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &epoch);
#else
    localtime_r(&epoch, &local);
#endif
    auto result = std::make_shared<ArrayValue>();
    result->emplace_back(static_cast<std::int64_t>(local.tm_hour));
    result->emplace_back(static_cast<std::int64_t>(local.tm_min));
    result->emplace_back(static_cast<std::int64_t>(local.tm_sec));
    return Value(std::move(result));
}

} // namespace

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

    // Local wall-clock time as Array{hour, minute, second} (0..23 / 0..59 / 0..59).
    globals_->define(
        "clockTime",
        Value(std::make_shared<NativeFunction>(
            "clockTime", 0, 0,
            [](Interpreter &, const Token &, const std::vector<Value> &) { return local_clock_time(); })));
}

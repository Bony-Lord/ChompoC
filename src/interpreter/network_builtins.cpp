#include "interpreter.h"
#include "callable.h"
#include "network_manager.h"
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
    constexpr std::size_t MaxReceiveSize = 1024 * 1024;

    const std::string &require_string(const Token &token, const Value &value, std::string_view description) {
        if (!value.is_string())
            throw RuntimeError(token, std::string(description) + " must be
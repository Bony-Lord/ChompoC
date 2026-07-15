#pragma once

#include "token.h"

#include <stdexcept>
#include <string_view>

class RuntimeError : public std::runtime_error {
public:
    RuntimeError(const Token& token, std::string_view message);
};
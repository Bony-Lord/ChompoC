#include "runtime_error.h"

#include <string>

RuntimeError::RuntimeError(const Token& token, std::string_view message) :
    std::runtime_error("RuntimeError: in " +
    std::to_string(token.position.line) + ":" + std::to_string(token.position.column) +
    " " + token.lexeme + ": \n" + std::string(message)) {}
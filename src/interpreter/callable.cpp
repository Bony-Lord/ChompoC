#include "callable.h"

#include <utility>

NativeFunction::NativeFunction(std::string name, std::size_t arity, Function function)
    : name_(std::move(name)), arity_(arity), function_(function) {}

std::size_t NativeFunction::arity() const {
    return arity_;
}

Value NativeFunction::call(const Token &token, const std::vector<Value> &args) const {
    return function_(token, args);
}

std::string NativeFunction::name() const {
    return name_;
}
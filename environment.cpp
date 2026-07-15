#include "environment.h"

#include <utility>

Environment::Environment(std::shared_ptr<Environment> parent) : parent_(std::move(parent)) {}

void Environment::define(const Token &name, Value value) {
    if (values_.contains(name.lexeme)) {
        throw RuntimeError(name, "variable'" + name.lexeme + "' is already declared in this scope");
    }
    values_.emplace(name.lexeme, std::move(value));
}

Value Environment::get(const Token &name) const {
    if (const auto iterator = values_.find(name.lexeme); iterator != values_.end()) return iterator->second;
    if (parent_) return parent_->get(name);
    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

void Environment::assign(const Token& name, Value value) {
    if (const auto iterator = values_.find(name.lexeme); iterator != values_.end()) { iterator->second = std::move(value); return; }
    if (parent_) { parent_->assign(name, std::move(value)); return; }
    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

std::shared_ptr<Environment> Environment::parent() const { return parent_; }

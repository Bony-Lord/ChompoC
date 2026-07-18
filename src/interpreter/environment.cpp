#include "environment.h"
#include "runtime_error.h"

#include <stdexcept>
#include <utility>

Environment::Environment(std::shared_ptr<Environment> parent) : parent_(std::move(parent)) {}

void Environment::define(const Token &name, Value value) {
    const bool inserted = values_.try_emplace(name.lexeme, std::move(value)).second;

    if (!inserted) {
        throw RuntimeError(name, "variable '" + name.lexeme + "' is already declared in this scope");
    }
}

void Environment::define(std::string name, Value value) {
    const bool inserted = values_.try_emplace(name, std::move(value)).second;

    if (!inserted) {
        throw std::logic_error("global value '" + name + "' is already defined");
    }
}

Value Environment::get(const Token &name) const {
    for (const Environment *environment = this; environment != nullptr; environment = environment->parent_.get()) {
        if (const auto iterator = environment->values_.find(name.lexeme); iterator != environment->values_.end()) {
            return iterator->second;
        }
    }

    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

void Environment::assign(const Token &name, Value value) {
    for (Environment *environment = this; environment != nullptr; environment = environment->parent_.get()) {
        if (const auto iterator = environment->values_.find(name.lexeme); iterator != environment->values_.end()) {
            iterator->second = std::move(value);
            return;
        }
    }

    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

std::shared_ptr<Environment> Environment::parent() const { return parent_; }

#include "environment.h"
#include "runtime_error.h"

#include <stdexcept>
#include <utility>

Environment::Environment(std::shared_ptr<Environment> parent, std::size_t expected_values)
    : parent_(std::move(parent)) {
    slots_.reserve(expected_values);
    slot_defined_.reserve(expected_values);
    dynamic_values_.reserve(parent_ ? 2 : 32);
}

void Environment::reset(std::shared_ptr<Environment> parent, std::size_t expected_values) {
    parent_ = std::move(parent);

    slots_.clear();
    slot_defined_.clear();
    dynamic_values_.clear();

    if (slots_.capacity() < expected_values)
        slots_.reserve(expected_values);
    if (slot_defined_.capacity() < expected_values)
        slot_defined_.reserve(expected_values);
}

SymbolId Environment::token_symbol(const Token &name) const {
    return name.symbol != InvalidSymbol ? name.symbol : intern_symbol(name.lexeme);
}

void Environment::ensure_slot(std::size_t slot) {
    const std::size_t required = slot + 1;
    if (slots_.size() < required) {
        slots_.resize(required);
        slot_defined_.resize(required, 0);
    }
}

const Environment *Environment::ancestor(std::size_t depth) const {
    const Environment *environment = this;
    while (depth-- > 0) {
        if (!environment->parent_)
            return nullptr;
        environment = environment->parent_.get();
    }
    return environment;
}

Environment *Environment::ancestor(std::size_t depth) {
    Environment *environment = this;
    while (depth-- > 0) {
        if (!environment->parent_)
            return nullptr;
        environment = environment->parent_.get();
    }
    return environment;
}

const Environment *Environment::global_environment() const {
    const Environment *environment = this;
    while (environment->parent_)
        environment = environment->parent_.get();
    return environment;
}

Environment *Environment::global_environment() {
    Environment *environment = this;
    while (environment->parent_)
        environment = environment->parent_.get();
    return environment;
}

void Environment::define(const Token &name, Value value) {
    const SymbolId symbol = token_symbol(name);

    if (name.binding == BindingKind::Local) {
        ensure_slot(name.slot);
        if (slot_defined_[name.slot])
            throw RuntimeError(name, "variable '" + name.lexeme + "' is already declared in this scope");

        slots_[name.slot] = std::move(value);
        slot_defined_[name.slot] = 1;
        return;
    }

    Environment *target = name.binding == BindingKind::Global ? global_environment() : this;
    const bool inserted = target->dynamic_values_.try_emplace(symbol, std::move(value)).second;
    if (!inserted)
        throw RuntimeError(name, "variable '" + name.lexeme + "' is already declared in this scope");
}

void Environment::define(std::string name, Value value) {
    Environment *target = global_environment();
    const SymbolId symbol = intern_symbol(name);
    const bool inserted = target->dynamic_values_.try_emplace(symbol, std::move(value)).second;

    if (!inserted)
        throw std::logic_error("global value '" + name + "' is already defined");
}

Value Environment::get_dynamic(const Token &name, SymbolId symbol) const {
    for (const Environment *environment = this; environment != nullptr; environment = environment->parent_.get()) {
        const auto iterator = environment->dynamic_values_.find(symbol);
        if (iterator != environment->dynamic_values_.end())
            return iterator->second;
    }

    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

Value Environment::get(const Token &name) const {
    const SymbolId symbol = token_symbol(name);

    if (name.binding == BindingKind::Local) {
        const Environment *target = ancestor(name.depth);
        if (target && name.slot < target->slots_.size() && target->slot_defined_[name.slot])
            return target->slots_[name.slot];

        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    if (name.binding == BindingKind::Global) {
        const Environment *target = global_environment();
        const auto iterator = target->dynamic_values_.find(symbol);
        if (iterator != target->dynamic_values_.end())
            return iterator->second;

        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    return get_dynamic(name, symbol);
}

void Environment::assign_dynamic(const Token &name, SymbolId symbol, Value value) {
    for (Environment *environment = this; environment != nullptr; environment = environment->parent_.get()) {
        const auto iterator = environment->dynamic_values_.find(symbol);
        if (iterator != environment->dynamic_values_.end()) {
            iterator->second = std::move(value);
            return;
        }
    }

    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

void Environment::assign(const Token &name, Value value) {
    const SymbolId symbol = token_symbol(name);

    if (name.binding == BindingKind::Local) {
        Environment *target = ancestor(name.depth);
        if (target && name.slot < target->slots_.size() && target->slot_defined_[name.slot]) {
            target->slots_[name.slot] = std::move(value);
            return;
        }

        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    if (name.binding == BindingKind::Global) {
        Environment *target = global_environment();
        const auto iterator = target->dynamic_values_.find(symbol);
        if (iterator != target->dynamic_values_.end()) {
            iterator->second = std::move(value);
            return;
        }

        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    assign_dynamic(name, symbol, std::move(value));
}

std::shared_ptr<Environment> Environment::parent() const { return parent_; }

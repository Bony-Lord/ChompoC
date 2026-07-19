#include "environment.h"
#include "runtime_error.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

Environment::Environment(std::shared_ptr<Environment> parent, std::size_t expected_values)
    : slots_(expected_values), slot_defined_(expected_values, 0), parent_(std::move(parent)) {
    root_ = parent_ ? parent_->root_ : this;
    dynamic_values_.reserve(parent_ ? 2 : 32);
}

void Environment::reset(std::shared_ptr<Environment> parent, std::size_t expected_values) {
    parent_ = std::move(parent);
    root_ = parent_ ? parent_->root_ : this;

    slots_.clear();
    slots_.resize(expected_values);
    slot_defined_.assign(expected_values, 0);
    dynamic_values_.clear();
}

SymbolId Environment::token_symbol(const Token &name) const {
    return name.symbol != InvalidSymbol ? name.symbol : intern_symbol(name.lexeme);
}

void Environment::ensure_slot(std::size_t slot) {
    const std::size_t required = slot + 1;
    if (slots_.size() >= required)
        return;

    slots_.resize(required);
    slot_defined_.resize(required, 0);
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

    Environment *target = name.binding == BindingKind::Global ? root_ : this;
    const bool inserted = target->dynamic_values_.try_emplace(symbol, std::move(value)).second;
    if (!inserted)
        throw RuntimeError(name, "variable '" + name.lexeme + "' is already declared in this scope");
}

void Environment::define(std::string name, Value value) {
    const SymbolId symbol = intern_symbol(name);
    const bool inserted = root_->dynamic_values_.try_emplace(symbol, std::move(value)).second;
    if (!inserted)
        throw std::logic_error("global value '" + name + "' is already defined");
}

const Value &Environment::get_dynamic_ref(const Token &name, SymbolId symbol) const {
    for (const Environment *environment = this; environment != nullptr; environment = environment->parent_.get()) {
        const auto iterator = environment->dynamic_values_.find(symbol);
        if (iterator != environment->dynamic_values_.end())
            return iterator->second;
    }
    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

Value &Environment::get_dynamic_ref(const Token &name, SymbolId symbol) {
    for (Environment *environment = this; environment != nullptr; environment = environment->parent_.get()) {
        const auto iterator = environment->dynamic_values_.find(symbol);
        if (iterator != environment->dynamic_values_.end())
            return iterator->second;
    }
    throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
}

const Value &Environment::get_ref(const Token &name) const {
    const SymbolId symbol = token_symbol(name);

    if (name.binding == BindingKind::Local) {
        const Environment *target = name.depth == 0 ? this : ancestor(name.depth);
        if (target && name.slot < target->slots_.size() && target->slot_defined_[name.slot])
            return target->slots_[name.slot];
        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    if (name.binding == BindingKind::Global) {
        const auto iterator = root_->dynamic_values_.find(symbol);
        if (iterator != root_->dynamic_values_.end())
            return iterator->second;
        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    return get_dynamic_ref(name, symbol);
}

Value &Environment::get_ref(const Token &name) {
    const SymbolId symbol = token_symbol(name);

    if (name.binding == BindingKind::Local) {
        Environment *target = name.depth == 0 ? this : ancestor(name.depth);
        if (target && name.slot < target->slots_.size() && target->slot_defined_[name.slot])
            return target->slots_[name.slot];
        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    if (name.binding == BindingKind::Global) {
        const auto iterator = root_->dynamic_values_.find(symbol);
        if (iterator != root_->dynamic_values_.end())
            return iterator->second;
        throw RuntimeError(name, "undefined variable '" + name.lexeme + "'");
    }

    return get_dynamic_ref(name, symbol);
}

Value Environment::get(const Token &name) const { return get_ref(name); }

void Environment::assign(const Token &name, Value value) { get_ref(name) = std::move(value); }

std::shared_ptr<Environment> Environment::parent() const { return parent_; }

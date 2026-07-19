#pragma once

#include "lexer/token.h"
#include "value.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Environment {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr, std::size_t expected_values = 0);

    void reset(std::shared_ptr<Environment> parent, std::size_t expected_values = 0);

    // String registration is intentionally preserved for native modules.
    void define(std::string name, Value value);
    void define(const Token &name, Value value);

    Value get(const Token &name) const;
    const Value &get_ref(const Token &name) const;
    Value &get_ref(const Token &name);
    void assign(const Token &name, Value value);

    std::shared_ptr<Environment> parent() const;

private:
    using DynamicValues = std::unordered_map<SymbolId, Value>;

    SymbolId token_symbol(const Token &name) const;

    const Environment *ancestor(std::size_t depth) const;
    Environment *ancestor(std::size_t depth);

    void ensure_slot(std::size_t slot);
    const Value &get_dynamic_ref(const Token &name, SymbolId symbol) const;
    Value &get_dynamic_ref(const Token &name, SymbolId symbol);

    std::vector<Value> slots_;
    std::vector<std::uint8_t> slot_defined_;
    DynamicValues dynamic_values_;
    std::shared_ptr<Environment> parent_;
    Environment *root_ = this;
};

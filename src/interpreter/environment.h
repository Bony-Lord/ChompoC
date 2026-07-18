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
    explicit Environment(std::shared_ptr<Environment> parent = nullptr, std::size_t expected_values = 8);

    void reset(std::shared_ptr<Environment> parent, std::size_t expected_values = 8);

    // String registration is intentionally preserved for native modules.
    // Built-ins remain extensible and are installed without going through
    // the source resolver.
    void define(std::string name, Value value);
    void define(const Token &name, Value value);

    Value get(const Token &name) const;
    void assign(const Token &name, Value value);

    std::shared_ptr<Environment> parent() const;

private:
    using DynamicValues = std::unordered_map<SymbolId, Value>;

    SymbolId token_symbol(const Token &name) const;

    const Environment *ancestor(std::size_t depth) const;
    Environment *ancestor(std::size_t depth);

    const Environment *global_environment() const;
    Environment *global_environment();

    void ensure_slot(std::size_t slot);
    Value get_dynamic(const Token &name, SymbolId symbol) const;
    void assign_dynamic(const Token &name, SymbolId symbol, Value value);

    // Resolved locals use dense direct slots. DynamicValues remains as a
    // compatibility/extensibility path for globals and unresolved host code.
    std::vector<Value> slots_;
    std::vector<std::uint8_t> slot_defined_;
    DynamicValues dynamic_values_;
    std::shared_ptr<Environment> parent_;
};

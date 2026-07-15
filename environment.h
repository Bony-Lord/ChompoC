#pragma once

#include "runtime_error.h"
#include "token.h"
#include "value.h"

#include <memory>
#include <string>
#include <unordered_map>

class Environment {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr);

    void define(const Token& name, Value value);
    Value get(const Token& name) const;
    void assign(const Token& name, Value value);

    std::shared_ptr<Environment> parent() const;

private:
    std::unordered_map<std::string, Value> values_;
    std::shared_ptr<Environment> parent_;
};
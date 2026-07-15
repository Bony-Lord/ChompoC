#pragma once

#include "token.h"

#include <memory>
#include <variant>

struct Expr;

using ExprPtr = std::unique_ptr<Expr>;

struct LiteralExpr {
    Token value;
};

struct VariableExpr {
    Token name;
};

struct UnaryExpr {
    Token operation;
    ExprPtr right;
};

struct BinaryExpr {
    ExprPtr left;
    Token operation;
    ExprPtr right;
};

struct GroupingExpr {
    ExprPtr expression;
};

struct Expr {
    using Node = std::variant<LiteralExpr, UnaryExpr, BinaryExpr, GroupingExpr, VariableExpr>;

    Node node;

    template <class T>
    explicit Expr(T value) : node(std::move(value)) {}
};
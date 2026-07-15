#pragma once

#include "token.h"

#include <memory>
#include <variant>
#include <vector>

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

struct Stmt;

using StmtPtr = std::unique_ptr<Stmt>;
using Program = std::vector<StmtPtr>;

struct ExpressionStmt {
    ExprPtr expression;
};

struct VarStmt {
    Token name;
    bool is_array;
    ExprPtr initializer;
};

struct PrintStmt {
    std::vector<ExprPtr> arguments;
};

struct BlockStmt {
    std::vector<StmtPtr> statements;
};

struct Stmt {
    using Node = std::variant<PrintStmt, BlockStmt, VarStmt, ExpressionStmt>;

    Node node;
    
    template<class T>
    explicit Stmt(T value)
        : node(std::move(value)) {
    }
};
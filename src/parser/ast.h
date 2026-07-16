#pragma once

#include "lexer/token.h"

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


struct AssignmentExpr {
    Token name;
    Token op;
    ExprPtr value;
};

struct CallExpr {
    ExprPtr callee;
    Token closing_parenthesis;
    std::vector<ExprPtr> arguments;
};

struct ArrayExpr {
    std::vector<ExprPtr> elements;
};

struct Expr {
    using Node = std::variant<LiteralExpr, UnaryExpr, BinaryExpr, GroupingExpr, VariableExpr, AssignmentExpr, CallExpr, ArrayExpr>;

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

struct IfStmt {
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch;
};

struct Stmt {
    using Node = std::variant<PrintStmt, BlockStmt, VarStmt, ExpressionStmt, IfStmt>;

    Node node;

    template<class T>
    explicit Stmt(T value)
        : node(std::move(value)) {
    }
};
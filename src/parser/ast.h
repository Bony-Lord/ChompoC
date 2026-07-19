#pragma once

#include "lexer/token.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct Expr;

using ExprPtr = std::unique_ptr<Expr>;
using CachedLiteral = std::variant<std::monostate, bool, std::int64_t, double, std::string, char>;

struct LiteralExpr {
    Token value;
    mutable bool decoded = false;
    mutable CachedLiteral cached;
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
    ExprPtr target;
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

struct IndexExpr {
    ExprPtr object;
    Token bracket;
    ExprPtr index;
};

struct UpdateExpr {
    ExprPtr target;
    Token operation;
    bool prefix;
};

struct Expr {
    using Node = std::variant<LiteralExpr, UnaryExpr, BinaryExpr, GroupingExpr, VariableExpr, AssignmentExpr, CallExpr,
                              ArrayExpr, IndexExpr, UpdateExpr>;

    Node node;

    template <class T> explicit Expr(T value) : node(std::move(value)) {}
};

struct Stmt;

using StmtPtr = std::unique_ptr<Stmt>;
using Program = std::vector<StmtPtr>;

struct EmptyStmt {};

struct ExpressionStmt {
    ExprPtr expression;
};

struct VarStmt {
    Token name;
    ExprPtr initializer;
};

struct PrintStmt {
    std::vector<ExprPtr> arguments;
};

struct BlockStmt {
    std::vector<StmtPtr> statements;
    std::uint32_t scope_slots = 0;
};

struct IfStmt {
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch;
};

struct WhileStmt {
    Token keyword;
    ExprPtr condition;
    StmtPtr body;
};

struct BreakStmt {
    Token keyword;
};

struct ContinueStmt {
    Token keyword;
};

struct FunctionStmt {
    Token name;
    std::vector<Token> parameters;
    std::vector<StmtPtr> body;
};

struct ReturnStmt {
    Token keyword;
    ExprPtr value;
};

struct ForInStmt {
    Token keyword;
    Token variable;
    ExprPtr iterable;
    StmtPtr body;
};

struct Stmt {
    using Node = std::variant<EmptyStmt, PrintStmt, BlockStmt, VarStmt, ExpressionStmt, IfStmt, WhileStmt, BreakStmt,
                              ContinueStmt, FunctionStmt, ReturnStmt, ForInStmt>;

    Node node;

    template <class T> explicit Stmt(T value) : node(std::move(value)) {}
};

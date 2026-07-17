#pragma once

#include "environment.h"
#include "parser/ast.h"
#include "value.h"

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

class UserFunction;

class Interpreter {
    friend class UserFunction;

public:
    Interpreter(std::ostream &output, std::ostream &diagnostics = std::cerr);

    void interpret(const Program &program);

private:
    std::shared_ptr<Environment> globals_;
    std::shared_ptr<Environment> environment_;
    std::size_t call_depth_ = 0;

    std::ostream &output_;
    std::ostream &diagnostics_;

    Value evaluate(const Expr &expression);
    void execute(const Stmt &statement);

    void execute_block(const std::vector<StmtPtr> &statements, std::shared_ptr<Environment> environment);
    struct ResolvedTarget {
        Value value;
        std::function<void(Value)> write;
    };
    ResolvedTarget resolve_target(const Expr &expression);

    // Выражения
    Value evaluate_node(const LiteralExpr &expression);
    Value evaluate_node(const VariableExpr &expression);
    Value evaluate_node(const UnaryExpr &expression);
    Value evaluate_node(const BinaryExpr &expression);
    Value evaluate_node(const GroupingExpr &expression);
    Value evaluate_node(const AssignmentExpr &expression);
    Value evaluate_node(const CallExpr &expression);
    Value evaluate_node(const ArrayExpr &expression);
    Value evaluate_node(const IndexExpr &expression);
    Value evaluate_node(const UpdateExpr &expression);

    // Инструкции
    void execute_node(const EmptyStmt &);
    void execute_node(const ExpressionStmt &statement);
    void execute_node(const VarStmt &statement);
    void execute_node(const PrintStmt &statement);
    void execute_node(const BlockStmt &statement);
    void execute_node(const IfStmt &statement);
    void execute_node(const FunctionStmt &statement);
    void execute_node(const ReturnStmt &statement);

    class CallDepthGuard {
    public:
        explicit CallDepthGuard(std::size_t &depth) : depth_(depth) { ++depth_; }

        ~CallDepthGuard() { --depth_; }

        CallDepthGuard(const CallDepthGuard &) = delete;
        CallDepthGuard &operator=(const CallDepthGuard &) = delete;

    private:
        std::size_t &depth_;
    };
    void warning(const Token &token, const std::string &message);
};
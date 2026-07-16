#pragma once

#include "environment.h"
#include "parser/ast.h"
#include "value.h"

#include <iostream>
#include <memory>
#include <vector>

class Interpreter {
public:
    explicit Interpreter(std::ostream &output);

    void interpret(const Program &program);

private:
    std::shared_ptr<Environment> globals_;
    std::shared_ptr<Environment> environment_;

    std::ostream &output_;

    Value evaluate(const Expr &expression);
    void execute(const Stmt &statement);

    void execute_block(const std::vector<StmtPtr> &statements, std::shared_ptr<Environment> environment);

    // Выражения.
    Value evaluate_node(const LiteralExpr &expression);
    Value evaluate_node(const VariableExpr &expression);
    Value evaluate_node(const UnaryExpr &expression);
    Value evaluate_node(const BinaryExpr &expression);
    Value evaluate_node(const GroupingExpr &expression);
    Value evaluate_node(const AssignmentExpr &expression);
    Value evaluate_node(const CallExpr &expression);
    Value evaluate_node(const ArrayExpr &expression);

    // Инструкции.
    void execute_node(const ExpressionStmt &statement);
    void execute_node(const VarStmt &statement);
    void execute_node(const PrintStmt &statement);
    void execute_node(const BlockStmt &statement);
    void execute_node(const IfStmt &statement);
};
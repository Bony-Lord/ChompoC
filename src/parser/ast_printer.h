#pragma once

#include "ast.h"

#include <initializer_list>
#include <string>
#include <string_view>

class AstPrinter {
public:
    std::string print(const Expr &expression) const;
    std::string print(const Stmt &statement) const;
    std::string print(const Program &program) const;

private:
    // Expr
    std::string print_node(const LiteralExpr &expression) const;
    std::string print_node(const VariableExpr &expression) const;
    std::string print_node(const UnaryExpr &expression) const;
    std::string print_node(const BinaryExpr &expression) const;
    std::string print_node(const GroupingExpr &expression) const;
    std::string print_node(const AssignmentExpr &expression) const;
    std::string print_node(const CallExpr &expression) const;
    std::string print_node(const ArrayExpr &expression) const;
    std::string print_node(const IndexExpr &expression) const;
    std::string print_node(const UpdateExpr &expression) const;

    // Stmt
    std::string print_node(const EmptyStmt &) const;
    std::string print_node(const ExpressionStmt &statement) const;
    std::string print_node(const VarStmt &statement) const;
    std::string print_node(const PrintStmt &statement) const;
    std::string print_node(const BlockStmt &statement) const;
    std::string print_node(const IfStmt &statement) const;
    std::string print_node(const FunctionStmt &statement) const;
    std::string print_node(const WhileStmt &statement) const;
    std::string print_node(const BreakStmt &) const;
    std::string print_node(const ContinueStmt &) const;
    std::string print_node(const ReturnStmt &statement) const;
    std::string print_node(const ForInStmt &statement) const;

    std::string parenthesize(std::string_view name, std::initializer_list<const Expr *> expressions) const;
};
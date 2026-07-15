#pragma once

#include "ast.h"

#include <initializer_list>
#include <string>
#include <string_view>

class AstPrinter {
public:
    std::string print(const Expr& expression) const;

private:
    std::string print_node(const LiteralExpr& expression) const;
    std::string print_node(const VariableExpr& expression) const;
    std::string print_node(const UnaryExpr& expression) const;
    std::string print_node(const BinaryExpr& expression) const;
    std::string print_node(const GroupingExpr& expression) const;

    std::string parenthesize(
        std::string_view name,
        std::initializer_list<const Expr*> expressions
    ) const;
};
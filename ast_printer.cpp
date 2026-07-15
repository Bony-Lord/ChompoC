#include "ast_printer.h"

#include <utility>
#include <variant>

std::string AstPrinter::print(const Expr& expression) const {
    return std::visit(
        [this](const auto& node) {
            return print_node(node);
        },
        expression.node
    );
}

std::string AstPrinter::print_node(const LiteralExpr& expression) const {
    return expression.value.lexeme;
}
std::string AstPrinter::print_node(const VariableExpr& expression) const {
    return expression.name.lexeme;
}
std::string AstPrinter::print_node(const UnaryExpr& expression) const {
    return parenthesize(expression.operation.lexeme, {expression.right.get()});
}
std::string AstPrinter::print_node(const BinaryExpr& expression) const {
    return parenthesize(expression.operation.lexeme, {expression.left.get(), expression.right.get()});
}
std::string AstPrinter::print_node(const GroupingExpr& expression) const {
    return parenthesize("group", {expression.expression.get()});
}
std::string AstPrinter::parenthesize(std::string_view name,std::initializer_list<const Expr*> expressions) const {
    std::string result = "(";
    result += name;

    for (const Expr* expression : expressions) {
        result += " ";
        result += print(*expression);
    }

    result += ")";
    return result;
}
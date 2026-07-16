#include "ast_printer.h"

#include <utility>
#include <variant>

std::string AstPrinter::print(const Expr &expression) const {
    return std::visit([this](const auto &node) { return print_node(node); }, expression.node);
}

std::string AstPrinter::print(const Stmt &statement) const {
    return std::visit([this](const auto &node) { return print_node(node); }, statement.node);
}

std::string AstPrinter::print(const Program &program) const {
    std::string result;

    for (const StmtPtr &statement : program) {
        result += print(*statement);
        result += '\n';
    }

    return result;
}

std::string AstPrinter::print_node(const LiteralExpr &expression) const { return expression.value.lexeme; }
std::string AstPrinter::print_node(const VariableExpr &expression) const { return expression.name.lexeme; }
std::string AstPrinter::print_node(const UnaryExpr &expression) const {
    return parenthesize(expression.operation.lexeme, {expression.right.get()});
}
std::string AstPrinter::print_node(const BinaryExpr &expression) const {
    return parenthesize(expression.operation.lexeme, {expression.left.get(), expression.right.get()});
}
std::string AstPrinter::print_node(const GroupingExpr &expression) const {
    return parenthesize("group", {expression.expression.get()});
}
std::string AstPrinter::print_node(const AssignmentExpr &expression) const {
    std::string result = "(= ";
    result += expression.name.lexeme;
    result += " ";
    result += print(*expression.value);
    result += ")";

    return result;
}
std::string AstPrinter::print_node(const CallExpr &expression) const {
    std::string result = "(call ";
    result += print(*expression.callee);

    for (const ExprPtr &argument : expression.arguments) {
        result += " ";
        result += print(*argument);
    }

    result += ")";
    return result;
}
std::string AstPrinter::print_node(const ArrayExpr &expression) const {
    std::string result = "(array";

    for (const ExprPtr &element : expression.elements) {
        result += " ";
        result += print(*element);
    }

    result += ")";
    return result;
}
std::string AstPrinter::print_node(const ExpressionStmt &statement) const {
    return parenthesize("expr", {statement.expression.get()});
}
std::string AstPrinter::print_node(const VarStmt &statement) const {
    std::string result;

    if (statement.is_array) {
        result = "(var-array ";
    } else {
        result = "(var ";
    }

    result += statement.name.lexeme;

    if (statement.initializer) {
        result += " ";
        result += print(*statement.initializer);
    } else if (statement.is_array) {
        result += " (array)";
    } else {
        result += " NULL";
    }

    result += ")";
    return result;
}
std::string AstPrinter::print_node(const PrintStmt &statement) const {
    std::string result = "(print";

    for (const ExprPtr &argument : statement.arguments) {
        result += " ";
        result += print(*argument);
    }

    result += ")";
    return result;
}
std::string AstPrinter::print_node(const BlockStmt &statement) const {
    std::string result = "(block";

    for (const StmtPtr &child : statement.statements) {
        result += " ";
        result += print(*child);
    }

    result += ")";
    return result;
}
std::string AstPrinter::print_node(const IfStmt &statement) const {
    std::string result = "(if ";

    result += print(*statement.condition);
    result += " ";
    result += print(*statement.then_branch);
    if (statement.else_branch) {
        result += " ";
        result += print(*statement.else_branch);
    }

    result += ")";
    return result;
}

std::string AstPrinter::parenthesize(std::string_view name, std::initializer_list<const Expr *> expressions) const {
    std::string result = "(";
    result += name;

    for (const Expr *expression : expressions) {
        result += " ";
        result += print(*expression);
    }

    result += ")";
    return result;
}
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
    return parenthesize(expression.op.lexeme, {expression.target.get(), expression.value.get()});
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
std::string AstPrinter::print_node(const MapExpr &expression) const {
    std::string result = "(map";
    for (const auto &entry : expression.elements) {
        result += " ";
        result += print(*entry.first);
        result += ":";
        result += print(*entry.second);
    }
    result += ")";
    return result;
}
std::string AstPrinter::print_node(const IndexExpr &expression) const {
    return parenthesize("index", {expression.object.get(), expression.index.get()});
}
std::string AstPrinter::print_node(const UpdateExpr &expression) const {
    std::string name = expression.prefix ? "prefix" : "postfix";

    name += expression.operation.lexeme;

    return parenthesize(name, {expression.target.get()});
}

std::string AstPrinter::print_node(const EmptyStmt &) const { return "(empty)"; }
std::string AstPrinter::print_node(const ExpressionStmt &statement) const {
    return parenthesize("expr", {statement.expression.get()});
}
std::string AstPrinter::print_node(const VarStmt &statement) const {
    std::string result = "(var ";
    result += statement.name.lexeme;

    if (statement.initializer) {
        result += " ";
        result += print(*statement.initializer);
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
std::string AstPrinter::print_node(const WhileStmt &statement) const {
    std::string result = "(while ";
    result += print(*statement.condition);
    result += " ";
    result += print(*statement.body);
    result += ")";

    return result;
}
std::string AstPrinter::print_node(const BreakStmt &) const { return "(break)"; }
std::string AstPrinter::print_node(const ContinueStmt &) const { return "(continue)"; }
std::string AstPrinter::print_node(const FunctionStmt &statement) const {
    std::string result = "(fun ";
    result += statement.name.lexeme;
    result += " (";

    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
        if (index > 0)
            result += " ";

        result += statement.parameters[index].lexeme;
    }

    result += ")";

    for (const StmtPtr &child : statement.body) {
        result += " ";
        result += print(*child);
    }

    result += ")";
    return result;
}
std::string AstPrinter::print_node(const ReturnStmt &statement) const {
    if (!statement.value)
        return "(return)";

    return parenthesize("return", {statement.value.get()});
}
std::string AstPrinter::print_node(const ForInStmt &statement) const {
    std::string result = "(for-in ";
    result += statement.variable.lexeme;
    result += " ";
    result += print(*statement.iterable);
    result += " ";
    result += print(*statement.body);
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
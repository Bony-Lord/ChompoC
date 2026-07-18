#include "resolver.h"

#include "lexer/symbol.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {
    [[noreturn]] void resolution_error(const Token &token, const std::string &message) {
        throw std::runtime_error("Resolver error: in " + std::to_string(token.position.line) + ":" +
                                 std::to_string(token.position.column) + " near '" + token.lexeme + "':\n" + message);
    }
}

void Resolver::resolve(Program &program) {
    for (StmtPtr &statement : program)
        resolve(*statement);
}

void Resolver::resolve(Stmt &statement) {
    std::visit([this](auto &node) { resolve_node(node); }, statement.node);
}

void Resolver::resolve(Expr &expression) {
    std::visit([this](auto &node) { resolve_node(node); }, expression.node);
}

void Resolver::begin_scope() { scopes_.emplace_back(); }

std::uint32_t Resolver::end_scope() {
    const std::uint32_t slots = scopes_.back().next_slot;
    scopes_.pop_back();
    return slots;
}

std::uint32_t Resolver::declare(Token &name) {
    if (name.symbol == InvalidSymbol)
        name.symbol = intern_symbol(name.lexeme);

    if (scopes_.empty()) {
        name.binding = BindingKind::Global;
        name.depth = 0;
        name.slot = 0;
        return 0;
    }

    Scope &scope = scopes_.back();
    const auto existing = scope.slots.find(name.symbol);

    std::uint32_t slot = 0;
    if (existing != scope.slots.end()) {
        // Keep runtime duplicate-declaration behaviour: both declarations
        // address the same slot and Environment::define reports the error.
        slot = existing->second;
    } else {
        if (scope.next_slot == std::numeric_limits<std::uint32_t>::max())
            resolution_error(name, "too many local variables in one lexical scope");

        slot = scope.next_slot++;
        scope.slots.emplace(name.symbol, slot);
    }

    name.binding = BindingKind::Local;
    name.depth = 0;
    name.slot = slot;
    return slot;
}

void Resolver::resolve_reference(Token &name) {
    if (name.symbol == InvalidSymbol)
        name.symbol = intern_symbol(name.lexeme);

    for (std::uint32_t depth = 0; depth < scopes_.size(); ++depth) {
        const Scope &scope = scopes_[scopes_.size() - 1 - depth];
        const auto found = scope.slots.find(name.symbol);

        if (found != scope.slots.end()) {
            name.binding = BindingKind::Local;
            name.depth = depth;
            name.slot = found->second;
            return;
        }
    }

    name.binding = BindingKind::Global;
    name.depth = 0;
    name.slot = 0;
}

void Resolver::resolve_node(EmptyStmt &) {}

void Resolver::resolve_node(ExpressionStmt &statement) { resolve(*statement.expression); }

void Resolver::resolve_node(VarStmt &statement) {
    // Preserve the old shadowing rule: the initializer is evaluated before
    // the new name exists, so `var x = x;` may read an outer x.
    if (statement.initializer)
        resolve(*statement.initializer);

    declare(statement.name);
}

void Resolver::resolve_node(PrintStmt &statement) {
    for (ExprPtr &argument : statement.arguments)
        resolve(*argument);
}

void Resolver::resolve_node(BlockStmt &statement) {
    begin_scope();
    for (StmtPtr &child : statement.statements)
        resolve(*child);
    end_scope();
}

void Resolver::resolve_node(IfStmt &statement) {
    resolve(*statement.condition);
    resolve(*statement.then_branch);
    if (statement.else_branch)
        resolve(*statement.else_branch);
}

void Resolver::resolve_node(WhileStmt &statement) {
    resolve(*statement.condition);
    resolve(*statement.body);
}

void Resolver::resolve_node(BreakStmt &) {}
void Resolver::resolve_node(ContinueStmt &) {}

void Resolver::resolve_node(FunctionStmt &statement) {
    // Declare first so local recursion resolves to the enclosing slot.
    declare(statement.name);

    begin_scope();
    for (Token &parameter : statement.parameters)
        declare(parameter);
    for (StmtPtr &child : statement.body)
        resolve(*child);
    statement.name.scope_slots = end_scope();
}

void Resolver::resolve_node(ReturnStmt &statement) {
    if (statement.value)
        resolve(*statement.value);
}

void Resolver::resolve_node(ForInStmt &statement) {
    resolve(*statement.iterable);

    begin_scope();
    declare(statement.variable);
    resolve(*statement.body);
    statement.variable.scope_slots = end_scope();
}

void Resolver::resolve_node(LiteralExpr &) {}

void Resolver::resolve_node(VariableExpr &expression) { resolve_reference(expression.name); }

void Resolver::resolve_node(UnaryExpr &expression) { resolve(*expression.right); }

void Resolver::resolve_node(BinaryExpr &expression) {
    resolve(*expression.left);
    resolve(*expression.right);
}

void Resolver::resolve_node(GroupingExpr &expression) { resolve(*expression.expression); }

void Resolver::resolve_node(AssignmentExpr &expression) {
    resolve(*expression.target);
    resolve(*expression.value);
}

void Resolver::resolve_node(CallExpr &expression) {
    resolve(*expression.callee);
    for (ExprPtr &argument : expression.arguments)
        resolve(*argument);
}

void Resolver::resolve_node(ArrayExpr &expression) {
    for (ExprPtr &element : expression.elements)
        resolve(*element);
}

void Resolver::resolve_node(IndexExpr &expression) {
    resolve(*expression.object);
    resolve(*expression.index);
}

void Resolver::resolve_node(UpdateExpr &expression) { resolve(*expression.target); }

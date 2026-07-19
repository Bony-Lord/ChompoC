#pragma once

#include "ast.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

class Resolver {
public:
    void resolve(Program &program);

private:
    struct Scope {
        std::unordered_map<SymbolId, std::uint32_t> slots;
        std::uint32_t next_slot = 0;
    };

    std::vector<Scope> scopes_;

    void resolve(Stmt &statement);
    void resolve(Expr &expression);

    void resolve_node(EmptyStmt &);
    void resolve_node(ExpressionStmt &statement);
    void resolve_node(VarStmt &statement);
    void resolve_node(PrintStmt &statement);
    void resolve_node(BlockStmt &statement);
    void resolve_node(IfStmt &statement);
    void resolve_node(WhileStmt &statement);
    void resolve_node(BreakStmt &);
    void resolve_node(ContinueStmt &);
    void resolve_node(FunctionStmt &statement);
    void resolve_node(ReturnStmt &statement);
    void resolve_node(ForInStmt &statement);

    void resolve_node(LiteralExpr &);
    void resolve_node(VariableExpr &expression);
    void resolve_node(UnaryExpr &expression);
    void resolve_node(BinaryExpr &expression);
    void resolve_node(GroupingExpr &expression);
    void resolve_node(AssignmentExpr &expression);
    void resolve_node(CallExpr &expression);
    void resolve_node(ArrayExpr &expression);
    void resolve_node(MapExpr &expression);
    void resolve_node(IndexExpr &expression);
    void resolve_node(UpdateExpr &expression);

    void begin_scope();
    std::uint32_t end_scope();
    std::uint32_t declare(Token &name);
    void resolve_reference(Token &name);
};

#pragma once

#include "environment.h"
#include "parser/ast.h"
#include "value.h"

#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class IOManager;
class NetworkManager;
class TerminalManager;
class UserFunction;

class Interpreter {
    friend class UserFunction;

public:
    Interpreter(std::ostream &output, std::ostream &diagnostics = std::cerr);

    void install_collection_builtins();
    void install_io_builtins(IOManager &io_manager);
    void install_network_builtins(NetworkManager &network_manager);
    void install_secure_network_builtins(NetworkManager &network_manager);
    void install_terminal_builtins(TerminalManager &terminal_manager);
    void install_system_builtins(std::vector<std::string> arguments);
    void interpret(const Program &program);

private:
    enum class ControlFlow : std::uint8_t {
        None,
        Break,
        Continue,
        Return,
    };

    std::shared_ptr<Environment> globals_;
    std::shared_ptr<Environment> environment_;
    std::size_t call_depth_ = 0;

    ControlFlow control_flow_ = ControlFlow::None;
    Value return_value_;

    std::deque<std::vector<Value>> argument_buffers_;
    std::vector<std::shared_ptr<Environment>> environment_pool_;

    std::ostream &output_;
    std::ostream &diagnostics_;

    Value evaluate(const Expr &expression);
    void execute(const Stmt &statement);
    void execute_statements(const std::vector<StmtPtr> &statements);

    void execute_block(const std::vector<StmtPtr> &statements, std::shared_ptr<Environment> environment);
    void execute_in_environment(const Stmt &statement, std::shared_ptr<Environment> environment);
    Value execute_function_body(const std::vector<StmtPtr> &statements, std::shared_ptr<Environment> environment);

    std::shared_ptr<Environment> acquire_environment(std::shared_ptr<Environment> parent, std::size_t slots);
    void release_environment(std::shared_ptr<Environment> environment);

    struct ResolvedTarget {
        Value value;
        std::function<void(Value)> write;
    };
    ResolvedTarget resolve_target(const Expr &expression);

    Value evaluate_node(const LiteralExpr &expression);
    Value evaluate_node(const VariableExpr &expression);
    Value evaluate_node(const UnaryExpr &expression);
    Value evaluate_node(const BinaryExpr &expression);
    Value evaluate_node(const GroupingExpr &expression);
    Value evaluate_node(const AssignmentExpr &expression);
    Value evaluate_node(const CallExpr &expression);
    Value evaluate_node(const ArrayExpr &expression);
    Value evaluate_node(const MapExpr &expression);
    Value evaluate_node(const IndexExpr &expression);
    Value evaluate_node(const UpdateExpr &expression);

    void execute_node(const EmptyStmt &);
    void execute_node(const ExpressionStmt &statement);
    void execute_node(const VarStmt &statement);
    void execute_node(const PrintStmt &statement);
    void execute_node(const BlockStmt &statement);
    void execute_node(const IfStmt &statement);
    void execute_node(const FunctionStmt &statement);
    void execute_node(const ReturnStmt &statement);
    void execute_node(const WhileStmt &statement);
    void execute_node(const BreakStmt &statement);
    void execute_node(const ContinueStmt &statement);
    void execute_node(const ForInStmt &statement);

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

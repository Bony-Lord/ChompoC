#pragma once

#include "lexer/token.h"
#include "value.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <memory>

class Interpreter;
class Environment;
struct FunctionStmt;

class Callable {
public:
    virtual ~Callable() = default;

    virtual std::size_t arity() const = 0;
    virtual Value call(Interpreter &interpreter, const Token &token, const std::vector<Value> &arguments) const = 0;
    virtual std::string name() const = 0;
};

class NativeFunction final : public Callable {
public:
    using Function = std::function<Value(const Token &, const std::vector<Value> &)>;

    NativeFunction(std::string name, std::size_t arity, Function function);

    std::size_t arity() const override;
    Value call(Interpreter &interpreter, const Token &token, const std::vector<Value> &arguments) const override;
    std::string name() const override;

private:
    std::string name_;
    std::size_t arity_;
    Function function_;
};

class UserFunction final : public Callable {
public:
    UserFunction(const FunctionStmt &declaration, std::shared_ptr<Environment> closure);

    std::size_t arity() const override;
    Value call(Interpreter &interpreter, const Token &token, const std::vector<Value> &arguments) const override;
    std::string name() const override;

private:
    const FunctionStmt *declaration_;
    std::shared_ptr<Environment> closure_;
};
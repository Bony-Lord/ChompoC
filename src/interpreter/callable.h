#pragma once

#include "lexer/token.h"
#include "value.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Interpreter;
class Environment;
struct FunctionStmt;

class Callable {
public:
    virtual ~Callable() = default;

    virtual Value call(Interpreter &interpreter, const Token &token, const std::vector<Value> &arguments) const = 0;
    virtual std::string name() const = 0;
    virtual bool accepts_arity(std::size_t count) const = 0;
    virtual std::string arity_description() const = 0;
};

class NativeFunction final : public Callable {
public:
    using Function = std::function<Value(Interpreter &, const Token &, const std::vector<Value> &)>;

    NativeFunction(std::string name, std::size_t min_arity, std::size_t max_arity, Function function);

    Value call(Interpreter &interpreter, const Token &token, const std::vector<Value> &arguments) const override;
    std::string name() const override;

    bool accepts_arity(std::size_t count) const override;
    std::string arity_description() const override;

private:
    std::string name_;
    std::size_t min_arity_;
    std::size_t max_arity_;
    Function function_;
};

class UserFunction final : public Callable {
public:
    UserFunction(const FunctionStmt &declaration, std::shared_ptr<Environment> closure);

    Value call(Interpreter &interpreter, const Token &token, const std::vector<Value> &arguments) const override;
    std::string name() const override;
    std::string arity_description() const override;
    bool accepts_arity(std::size_t count) const override;

private:
    static constexpr std::size_t MaxCachedFrames = 16;

    const FunctionStmt *declaration_;
    std::shared_ptr<Environment> closure_;
    mutable std::vector<std::shared_ptr<Environment>> frame_pool_;
};

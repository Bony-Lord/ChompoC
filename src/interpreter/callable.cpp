#include "callable.h"
#include "environment.h"
#include "interpreter.h"
#include "parser/ast.h"
#include "return_signal.h"

#include <limits>
#include <utility>

NativeFunction::NativeFunction(std::string name, std::size_t min_arity, std::size_t max_arity, Function function)
    : name_(std::move(name)), min_arity_(min_arity), max_arity_(max_arity), function_(std::move(function)) {}

bool NativeFunction::accepts_arity(std::size_t count) const { return count >= min_arity_ && count <= max_arity_; }

std::string NativeFunction::arity_description() const {
    if (min_arity_ == max_arity_)
        return std::to_string(min_arity_);

    if (max_arity_ == std::numeric_limits<std::size_t>::max()) {
        return "at least " + std::to_string(min_arity_);
    }

    return std::to_string(min_arity_) + ".." + std::to_string(max_arity_);
}

Value NativeFunction::call(Interpreter &interpreter, const Token &token, const std::vector<Value> &arguments) const {
    return function_(interpreter, token, arguments);
}

std::string NativeFunction::name() const { return name_; }

UserFunction::UserFunction(const FunctionStmt &declaration, std::shared_ptr<Environment> closure)
    : declaration_(&declaration), closure_(std::move(closure)) {}

Value UserFunction::call(Interpreter &interpreter, const Token &, const std::vector<Value> &arguments) const {
    auto environment = std::make_shared<Environment>(closure_);

    for (std::size_t index = 0; index < declaration_->parameters.size(); ++index) {
        environment->define(declaration_->parameters[index], arguments[index]);
    }

    try {
        interpreter.execute_block(declaration_->body, std::move(environment));
    } catch (const ReturnSignal &signal) {
        return signal.value;
    }

    return Value(nullptr);
}

std::string UserFunction::name() const { return declaration_->name.lexeme; }

bool UserFunction::accepts_arity(std::size_t count) const { return count == declaration_->parameters.size(); }

std::string UserFunction::arity_description() const { return std::to_string(declaration_->parameters.size()); }
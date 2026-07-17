#include "parser.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {
    constexpr std::size_t token_index(TokenType type) { return static_cast<std::size_t>(type); }
} // namespace

Parser::Precedence Parser::next_precedence(Precedence precedence) {
    return static_cast<Precedence>(static_cast<int>(precedence) + 1);
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token &Parser::peek() const {
    if (current_ >= tokens_.size())
        return tokens_.back();
    return tokens_[current_];
}

const Token &Parser::previous() const { return tokens_[current_ - 1]; }

const Token &Parser::advance() {
    if (current_ < tokens_.size())
        ++current_;
    return previous();
}

bool Parser::check(TokenType type) const { return peek().type == type; }

bool Parser::match(std::initializer_list<TokenType> types) {
    for (const TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

bool Parser::is_at_end() const { return current_ >= tokens_.size() || peek().type == TokenType::EndOfFile; }

const Token &Parser::consume(TokenType type, std::string_view message) {
    if (check(type))
        return advance();
    error(peek(), message);
}

[[noreturn]] void Parser::error(const Token &token, std::string_view message) const {
    std::string location;

    if (token.type == TokenType::EndOfFile) {
        location = "at end of file";
    } else {
        location = "near '" + token.lexeme + "'";
    }

    throw std::runtime_error("Parser error: in " + std::to_string(token.position.line) + ":" +
                             std::to_string(token.position.column) + " " + location + ": \n" + std::string(message));
}

const Parser::ParseRule &Parser::get_rule(TokenType type) {
    constexpr std::size_t rule_count = static_cast<std::size_t>(TokenType::Count);

    static constexpr std::array<ParseRule, rule_count> rules = [] -> std::array<ParseRule, rule_count> {
        std::array<ParseRule, rule_count> result{};
        // Литералы
        result[token_index(TokenType::Number)] = {&Parser::literal, nullptr, Precedence::None};
        result[token_index(TokenType::String)] = {&Parser::literal, nullptr, Precedence::None};
        result[token_index(TokenType::True)] = {&Parser::literal, nullptr, Precedence::None};
        result[token_index(TokenType::False)] = {&Parser::literal, nullptr, Precedence::None};
        result[token_index(TokenType::Null)] = {&Parser::literal, nullptr, Precedence::None};
        // Имя переменной
        result[token_index(TokenType::Identifier)] = {&Parser::variable, nullptr, Precedence::None};
        // (expression)
        result[token_index(TokenType::LeftParen)] = {&Parser::grouping, &Parser::call, Precedence::Call};
        // -value и left - right
        result[token_index(TokenType::Minus)] = {&Parser::unary, &Parser::binary, Precedence::Term};
        // !value
        result[token_index(TokenType::Not)] = {&Parser::unary, nullptr, Precedence::None};
        // += -= *= /=
        result[token_index(TokenType::PlusEq)] = {nullptr, &Parser::assignment, Precedence::Assignment};

        result[token_index(TokenType::MinusEq)] = {nullptr, &Parser::assignment, Precedence::Assignment};

        result[token_index(TokenType::MulEq)] = {nullptr, &Parser::assignment, Precedence::Assignment};

        result[token_index(TokenType::DivideEq)] = {nullptr, &Parser::assignment, Precedence::Assignment};

        // Сложение
        result[token_index(TokenType::Plus)] = {nullptr, &Parser::binary, Precedence::Term};
        // Умножение, деление, остаток
        result[token_index(TokenType::Star)] = {nullptr, &Parser::binary, Precedence::Factor};
        result[token_index(TokenType::Slash)] = {nullptr, &Parser::binary, Precedence::Factor};
        result[token_index(TokenType::Percent)] = {nullptr, &Parser::binary, Precedence::Factor};
        // Сравнения
        result[token_index(TokenType::Less)] = {nullptr, &Parser::binary, Precedence::Comparison};
        result[token_index(TokenType::LessEqual)] = {nullptr, &Parser::binary, Precedence::Comparison};
        result[token_index(TokenType::Greater)] = {nullptr, &Parser::binary, Precedence::Comparison};
        result[token_index(TokenType::GreaterEqual)] = {nullptr, &Parser::binary, Precedence::Comparison};
        // Равенство
        result[token_index(TokenType::EqualEqual)] = {nullptr, &Parser::binary, Precedence::Equality};
        result[token_index(TokenType::NotEqual)] = {nullptr, &Parser::binary, Precedence::Equality};
        // Логические операции
        result[token_index(TokenType::AndAnd)] = {nullptr, &Parser::binary, Precedence::And};
        result[token_index(TokenType::OrOr)] = {nullptr, &Parser::binary, Precedence::Or};
        result[token_index(TokenType::Equal)] = {nullptr, &Parser::assignment, Precedence::Assignment};
        result[token_index(TokenType::LeftBrace)] = {&Parser::array_literal, nullptr, Precedence::None};

        return result;
    }();

    return rules[token_index(type)];
}

ExprPtr Parser::expression() { return parse_precedence(Precedence::Assignment); }

ExprPtr Parser::parse_precedence(Precedence precedence) {
    const Token &first_token = advance();
    const PrefixFunction prefix = get_rule(first_token.type).prefix;

    if (prefix == nullptr) {
        error(first_token, "expected expression");
    }

    ExprPtr left = (this->*prefix)();

    while (static_cast<int>(precedence) <= static_cast<int>(get_rule(peek().type).precedence)) {
        const Token &operator_token = advance();
        const InfixFunction infix = get_rule(operator_token.type).infix;
        if (infix == nullptr) {
            error(operator_token, "expected infix operator");
        }
        left = (this->*infix)(std::move(left));
    }
    return left;
}

ExprPtr Parser::literal() { return std::make_unique<Expr>(LiteralExpr(previous())); }
ExprPtr Parser::variable() { return std::make_unique<Expr>(VariableExpr{previous()}); }

ExprPtr Parser::grouping() {
    ExprPtr inner = expression();

    consume(TokenType::RightParen, "expected ')' after expression");
    return std::make_unique<Expr>(GroupingExpr{std::move(inner)});
}

ExprPtr Parser::unary() {
    const Token operation = previous();
    ExprPtr right = parse_precedence(Precedence::Unary);

    return std::make_unique<Expr>(UnaryExpr(operation, std::move(right)));
}

ExprPtr Parser::array_literal() {
    std::vector<ExprPtr> elements;
    if (!check(TokenType::RightBrace)) {
        while (true) {
            elements.push_back(expression());
            if (!match({TokenType::Comma}))
                break;
            if (check(TokenType::RightBrace))
                break;
        }
    }
    consume(TokenType::RightBrace, "expected '}' after array elements");
    return std::make_unique<Expr>(ArrayExpr{std::move(elements)});
}

ExprPtr Parser::binary(ExprPtr left) {
    const Token operation = previous();

    const Precedence precedence = get_rule(operation.type).precedence;

    ExprPtr right = parse_precedence(next_precedence(precedence));
    return std::make_unique<Expr>(BinaryExpr(std::move(left), operation, std::move(right)));
}

ExprPtr Parser::assignment(ExprPtr left) {
    const Token operation = previous();
    const auto *variable = std::get_if<VariableExpr>(&left->node);

    if (variable == nullptr)
        error(operation, "invalid assignment target");

    const Token name = variable->name;

    ExprPtr value = parse_precedence(Precedence::Assignment);

    return std::make_unique<Expr>(AssignmentExpr{name, operation, std::move(value)});
}

ExprPtr Parser::call(ExprPtr callee) {
    std::vector<ExprPtr> arguments;

    if (!check(TokenType::RightParen)) {
        while (true) {
            arguments.push_back(expression());
            if (!match({TokenType::Comma}))
                break;
            if (check(TokenType::RightParen))
                break;
        }
    }
    const Token closing_parenthesis = consume(TokenType::RightParen, "expected ')' after function arguments");

    return std::make_unique<Expr>(CallExpr{std::move(callee), closing_parenthesis, std::move(arguments)});
}

Program Parser::parse() {
    Program program;

    while (!is_at_end()) {
        program.push_back(declaration());
    }

    return program;
}

StmtPtr Parser::declaration() {
    if (match({TokenType::Fun}))
        return function_declaration();
    if (match({TokenType::Var}))
        return var_declaration();

    return statement();
}

StmtPtr Parser::var_declaration() {
    const Token name = consume(TokenType::Identifier, "expected variable name after 'var'");

    bool is_array = false;
    if (match({TokenType::LeftBracket})) {
        consume(TokenType::RightBracket, "expected ']' after '[' in array declaration");
        is_array = true;
    }

    ExprPtr initializer;

    if (match({TokenType::Equal})) {
        initializer = expression();
    }

    consume(TokenType::Semicolon, "expected ';' after variable declaration");

    return std::make_unique<Stmt>(VarStmt{name, is_array, std::move(initializer)});
}
StmtPtr Parser::function_declaration() {
    const Token name =
        consume(TokenType::Identifier, "expected function name after 'fun'");

    consume(TokenType::LeftParen,
            "expected '(' after function name");

    std::vector<Token> parameters;

    if (!check(TokenType::RightParen)) {
        do {
            parameters.push_back(
                consume(TokenType::Identifier,
                        "expected parameter name"));
        } while (match({TokenType::Comma}));
    }

    consume(TokenType::RightParen,
            "expected ')' after function parameters");

    consume(TokenType::LeftBrace,
            "expected '{' before function body");

    ++function_depth_;
    std::vector<StmtPtr> body = block();
    --function_depth_;

    return std::make_unique<Stmt>(
        FunctionStmt{
            name,
            std::move(parameters),
            std::move(body)
        });
}

StmtPtr Parser::statement() {
    if (match({TokenType::Print}))
        return print_statement();
    if (match({TokenType::LeftBrace}))
        return block_statement();
    if (match({TokenType::If}))
        return if_statement();
    if (match({TokenType::Return}))
        return return_statement();
    return expression_statement();
}

StmtPtr Parser::print_statement() {
    consume(TokenType::LeftParen, "expected '(' after 'print'");

    std::vector<ExprPtr> arguments;

    if (!check(TokenType::RightParen)) {
        do {
            arguments.push_back(expression());
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RightParen, "expected ')' after print arguments");

    consume(TokenType::Semicolon, "expected ';' after print statement");

    return std::make_unique<Stmt>(PrintStmt{std::move(arguments)});
}
StmtPtr Parser::return_statement() {
    const Token keyword = previous();

    if (function_depth_ == 0)
        error(keyword, "cannot return from top-level code");

    ExprPtr value;

    if (!check(TokenType::Semicolon))
        value = expression();

    consume(TokenType::Semicolon,
            "expected ';' after return value");

    return std::make_unique<Stmt>(
        ReturnStmt{keyword, std::move(value)});
}
StmtPtr Parser::expression_statement() {
    ExprPtr value = expression();

    consume(TokenType::Semicolon, "expected ';' after expression");

    return std::make_unique<Stmt>(ExpressionStmt{std::move(value)});
}
StmtPtr Parser::block_statement() { return std::make_unique<Stmt>(BlockStmt{block()}); }
std::vector<StmtPtr> Parser::block() {
    std::vector<StmtPtr> statements;

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        statements.push_back(declaration());
    }

    consume(TokenType::RightBrace, "expected '}' after block");
    return statements;
}
StmtPtr Parser::if_statement() {
    consume(TokenType::LeftParen, "expected '(' after 'if'");
    ExprPtr condition = expression();
    consume(TokenType::RightParen, "expected ')' after condition");
    StmtPtr then_branch = statement();
    StmtPtr else_branch;
    if (match({TokenType::Else}))
        else_branch = statement();
    return std::make_unique<Stmt>(IfStmt(std::move(condition), std::move(then_branch), std::move(else_branch)));
}

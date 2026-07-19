#include "lexer.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {
    const std::unordered_map<std::string_view, TokenType> keywords{
        {"var", TokenType::Var},       {"print", TokenType::Print}, {"if", TokenType::If},
        {"else", TokenType::Else},     {"while", TokenType::While}, {"for", TokenType::For},
        {"return", TokenType::Return}, {"break", TokenType::Break}, {"continue", TokenType::Continue},
        {"fun", TokenType::Fun},       {"true", TokenType::True},   {"false", TokenType::False},
        {"NULL", TokenType::Null},     {"in", TokenType::In},       {"Array", TokenType::Array},
        {"Map", TokenType::Map},
    };
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

bool Lexer::is_at_end() const { return current_ >= source_.size(); }

char Lexer::peek() const {
    if (is_at_end())
        return '\0';
    return source_[current_];
}
char Lexer::peek_next() const {
    if (current_ + 1 >= source_.size())
        return '\0';
    return source_[current_ + 1];
}

char Lexer::advance() {
    const char cur_char = peek();
    ++current_;

    if (cur_char == '\n') {
        ++current_position_.line;
        current_position_.column = 1;
    } else {
        ++current_position_.column;
    }
    return cur_char;
}

void Lexer::add_token(TokenType type) {
    Token token{type, source_.substr(start_, current_ - start_), start_position_};

    if (type == TokenType::Identifier || type == TokenType::Array || type == TokenType::Map)
        token.symbol = intern_symbol(token.lexeme);

    tokens_.push_back(std::move(token));
}

std::vector<Token> Lexer::scan_tokens() {
    if (tokens_.capacity() == 0)
        tokens_.reserve(source_.size() / 3 + 1);

    while (!is_at_end()) {
        start_ = current_;
        start_position_ = current_position_;

        scan_token();
    }
    tokens_.push_back(Token{TokenType::EndOfFile, "", current_position_});
    return tokens_;
}
bool Lexer::is_support_name(std::string_view s) {
    if (s.empty() or !is_alpha(s[0]))
        return false;

    for (char c : s) {
        if (!is_alpha_numeric(c))
            return false;
    }

    return !keywords.contains(s);
}
bool Lexer::is_digit(char c) { return c >= '0' && c <= '9'; }
bool Lexer::is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_'); }
bool Lexer::is_alpha_numeric(char c) { return is_alpha(c) || is_digit(c); }
bool Lexer::match(char c) {
    if (peek() != c)
        return false;
    advance();
    return true;
}

void Lexer::number() {
    while (is_digit(peek()))
        advance();

    if (peek() == '.' && is_digit(peek_next())) {
        advance();

        while (is_digit(peek()))
            advance();
    }

    add_token(TokenType::Number);
}

void Lexer::identifier() {
    while (is_alpha_numeric(peek()))
        advance();

    const std::string_view text(source_.data() + start_, current_ - start_);
    const auto keyword = keywords.find(text);
    if (keyword != keywords.end()) {
        add_token(keyword->second);
    } else {
        add_token(TokenType::Identifier);
    }
}

void Lexer::string_literal() {
    while (!is_at_end()) {
        if (peek() == '"') {
            advance();
            add_token(TokenType::String);
            return;
        }

        if (peek() == '\\') {
            advance();

            if (is_at_end())
                error("unfinished escape sequence");

            if (peek() == '\n')
                error("line break after '\\' is not allowed");

            advance();
            continue;
        }

        advance();
    }

    error("unterminated string literal");
}

void Lexer::char_literal() {
    if (is_at_end() || peek() == '\n')
        error("unterminated character literal");

    if (peek() == '\'')
        error("character literal cannot be empty");

    if (peek() == '\\') {
        advance();

        if (is_at_end() || peek() == '\n')
            error("unfinished character escape sequence");

        advance();
    } else {
        advance();
    }

    if (peek() != '\'')
        error("character literal must contain exactly one character");

    advance();
    add_token(TokenType::Char);
}

void Lexer::scan_token() {
    const char c = advance();
    switch (c) {
    case '(':
        add_token(TokenType::LeftParen);
        break;

    case ')':
        add_token(TokenType::RightParen);
        break;

    case '{':
        add_token(TokenType::LeftBrace);
        break;

    case '}':
        add_token(TokenType::RightBrace);
        break;

    case '[':
        add_token(TokenType::LeftBracket);
        break;

    case ']':
        add_token(TokenType::RightBracket);
        break;

    case ';':
        add_token(TokenType::Semicolon);
        break;

    case ':':
        add_token(TokenType::Colon);
        break;

    case ',':
        add_token(TokenType::Comma);
        break;

    case '.':
        add_token(TokenType::Dot);
        break;

    case '+':
        if (match('+'))
            add_token(TokenType::PlusOne);
        else if (match('='))
            add_token(TokenType::PlusEq);
        else
            add_token(TokenType::Plus);
        break;

    case '-':
        if (match('-'))
            add_token(TokenType::MinusOne);
        else if (match('='))
            add_token(TokenType::MinusEq);
        else
            add_token(TokenType::Minus);
        break;

    case '*':
        if (match('='))
            add_token(TokenType::MulEq);
        else
            add_token(TokenType::Star);
        break;

    case '/':
        if (match('/')) {
            while (peek() != '\n' && !is_at_end())
                advance();
        } else if (match('='))
            add_token(TokenType::DivideEq);
        else
            add_token(TokenType::Slash);
        break;

    case '%':
        add_token(TokenType::Percent);
        break;

    case '=':
        if (match('='))
            add_token(TokenType::EqualEqual);
        else
            add_token(TokenType::Equal);
        break;

    case '!':
        if (match('='))
            add_token(TokenType::NotEqual);
        else
            add_token(TokenType::Not);
        break;

    case '<':
        if (match('='))
            add_token(TokenType::LessEqual);
        else
            add_token(TokenType::Less);
        break;

    case '>':
        if (match('='))
            add_token(TokenType::GreaterEqual);
        else
            add_token(TokenType::Greater);
        break;

    case '"':
        string_literal();
        break;

    case '&':
        if (!match('&'))
            error("expected '&' after '&'");
        else
            add_token(TokenType::AndAnd);
        break;

    case '|':
        if (!match('|'))
            error("expected '|' after '|'");
        else
            add_token(TokenType::OrOr);
        break;

    case '\'':
        char_literal();
        break;

    case ' ':
    case '\t':
    case '\r':
    case '\n':
        break;

    default:
        if (is_digit(c)) {
            number();
        } else if (is_alpha(c)) {
            identifier();
        } else {
            error(std::string("invalid character '") + c + "'");
        }
        break;
    }
}

[[noreturn]] void Lexer::error(std::string_view message) const {
    throw std::runtime_error("Lexer error: in " + std::to_string(start_position_.line) + ":" +
                             std::to_string(start_position_.column) + ": \n" + std::string(message));
}

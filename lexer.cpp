#include "lexer.h"

//#include <utility>
#include <unordered_map>
#include <stdexcept>

namespace {
    const std::unordered_map<std::string, TokenType> keywords {
        {"let", TokenType::Let},
        {"print", TokenType::Print}
    };
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

bool Lexer::is_at_end() const { return current_ >= source_.size(); }

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source_[current_];
}
char Lexer::peek_next() const {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

char Lexer::advance() { // if !is_at_end()
    const char cur_char = peek();
    ++current_;

    if (cur_char == '\n') {
        ++current_position_.line;
        current_position_.column = 1;
    }
    else {
        ++current_position_.column;
    }
    return cur_char;
}

void Lexer::add_token(TokenType type) {
    tokens_.push_back(Token{type, source_.substr(start_, current_ - start_), start_position_});
}

std::vector<Token> Lexer::scan_tokens() {
    while (!is_at_end()) {
        start_ = current_;
        start_position_ = current_position_;

        scan_token();
    }
    tokens_.push_back(Token{TokenType::EndOfFile, "", current_position_});
    return tokens_;
}

bool Lexer::is_digit(char c) { return c >= '0' && c <= '9'; }
bool Lexer::is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_'); }
bool Lexer::is_alpha_numeric(char c) { return is_alpha(c) || is_digit(c); }

bool Lexer::match(char c) {
    if (peek() != c) return false;
    advance();
    return true;
}

void Lexer::number() {
    while (is_digit(peek())) {
        advance();
    }
    add_token(TokenType::Number);
}

void Lexer::identifier() {
    while (is_alpha_numeric(peek())) {
        advance();
    }

    const std::string text = source_.substr(start_, current_ - start_);
    const auto keyword = keywords.find(text);
    if (keyword != keywords.end()) {
        add_token(keyword->second);
    }
    else {
        add_token(TokenType::Identifier);
    }
}

void Lexer::string_literal() {
    while (peek() != '"' && !is_at_end()) {
        advance();
    }
    if (is_at_end()) {
        error("unterminated string literal");
    }
    advance();
    add_token(TokenType::String);

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

        case ';':
            add_token(TokenType::Semicolon);
            break;

        case ',':
            add_token(TokenType::Comma);
            break;

        case '.':
            add_token(TokenType::Dot);
            break;

        case '+':
            add_token(TokenType::Plus);
            break;

        case '-':
            add_token(TokenType::Minus);
            break;

        case '*':
            add_token(TokenType::Star);
            break;

        case '/':
            if (match('/')) {
                while (peek() != '\n' && !is_at_end()) {
                    advance();
                }
            } else {
                add_token(TokenType::Slash);
            }
            break;

        case '=':
            if (match('=')) add_token(TokenType::EqualEqual);
            else add_token(TokenType::Equal);
            break;

        case '!':
            if (match('=')) add_token(TokenType::NotEqual);
            else add_token(TokenType::Not);
            break;

        case '<':
            if (match('=')) add_token(TokenType::LessEqual);
            else add_token(TokenType::Less);
            break;

        case '>':
            if (match('=')) add_token(TokenType::GreaterEqual);
            else add_token(TokenType::Greater);
            break;

        case '"':
            string_literal();
            break;

        case ' ':
        case '\t':
        case '\r':
        case '\n':
            break;

        default:
            if (is_digit(c)) {
                number();
            }
            else if (is_alpha(c)) {
                identifier();
            }
            else {
                error(std::string("invalid character '") + c + "'");
            }
            break;
    }
}

[[noreturn]] void Lexer::error(std::string_view message) const {
    throw std::runtime_error("Lexer error: in " + std::to_string(start_position_.line) + ":" +
        std::to_string(start_position_.column) + ": \n" + std::string(message));
}

#pragma once

#include <array>
#include <cassert>
#include <het_unordered_map.hpp>
#include <string>
#include <string_view>

#include <Exception.hpp>
#include <Logger.hpp>
#include <Source.hpp>
#include <Token.hpp>

class Tokenizer {
  public:
    Tokenizer(const std::string& source) : _source(source) { skip_whitespace(); }

    Token consume() {
        auto t = search_next();
        // Skip discardable characters immediatly (avoid ending empty token)
        skip_whitespace();
        return t;
    }

    bool has_more() const noexcept { return _current_pos < _source.length(); }

  private:
    inline bool is_discardable(char c) const noexcept { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
    inline bool is_allowed_in_identifiers(char c) const noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
    inline bool is_digit(char c) const noexcept { return c >= '0' && c <= '9'; }
    bool        is_newline(char c) const noexcept { return c == '\n'; }

    bool        eof() const noexcept { return _current_pos >= _source.length(); }
    inline char peek() const noexcept { return _source[_current_pos]; }

    void advance() noexcept;
    void newline() noexcept;
    void skip_whitespace() noexcept;

    Token search_next();

    // Display a hint to the origin of an error.
    std::string point_error(size_t at, size_t line, size_t from = (std::numeric_limits<size_t>::max)(), size_t to = (std::numeric_limits<size_t>::max)()) const noexcept {
        return ::point_error(_source, at, line, from, to);
    }

    static constexpr std::string_view control_chars = ";{}";
    static constexpr std::string_view operators_chars = ".=*/+-^!<>&|%()[]";

    static inline bool    is_allowed_in_operators(char c) { return operators_chars.find(c) != operators_chars.npos; }
    static constexpr char escaped_char[] = {'?', '\'', '\"', '\?', '\a', '\b', '\f', '\n', '\r', '\t', '\v', '\0'};

    const het_unordered_map<Token::Type> operators{
        {"=", Token::Type::Assignment},
        {"*", Token::Type::Multiplication},
        {"+", Token::Type::Addition},
        {"-", Token::Type::Substraction},
        {"/", Token::Type::Division},
        {"^", Token::Type::Xor},
        {"==", Token::Type::Equal},
        {"!=", Token::Type::Different},
        {"!", Token::Type::Not},
        {">", Token::Type::Greater},
        {"<", Token::Type::Lesser},
        {">=", Token::Type::GreaterOrEqual},
        {"<=", Token::Type::LesserOrEqual},
        {"&&", Token::Type::And},
        {"||", Token::Type::Or},
        {"%", Token::Type::Modulus},
        {"++", Token::Type::Increment},
        {"--", Token::Type::Decrement},
        {"(", Token::Type::OpenParenthesis},
        {")", Token::Type::CloseParenthesis},
        {"[", Token::Type::OpenSubscript},
        {"]", Token::Type::CloseSubscript},
        {".", Token::Type::MemberAccess},
        {":", Token::Type::Colon},
    };

    const het_unordered_map<Token::Type> keywords{
        {"function", Token::Type::Function}, {"let", Token::Type::Let},          {"return", Token::Type::Return},   {"if", Token::Type::If},
        {"else", Token::Type::Else},         {"while", Token::Type::While},      {"for", Token::Type::For},         {"bool", Token::Type::Identifier},
        {"int", Token::Type::Identifier},    {"float", Token::Type::Identifier}, {"char", Token::Type::Identifier}, {"true", Token::Type::Boolean},
        {"false", Token::Type::Boolean},     {"const", Token::Type::Const},      {"import", Token::Type::Import},   {"export", Token::Type::Export},
        {"extern", Token::Type::Extern},     {"type", Token::Type::Type},        {"and", Token::Type::And},         {"or", Token::Type::Or},
        {"sizeof", Token::Type::Sizeof},
    };

    const std::string& _source;
    size_t             _current_pos = 0;
    size_t             _current_line = 0;
    size_t             _current_column = 0;
};

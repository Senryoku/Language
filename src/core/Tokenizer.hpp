#pragma once

#include <array>
#include <cassert>
#include <het_unordered_map.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Logger.hpp>

// FIXME: Move, specialize
class Exception : public std::logic_error {
  public:
    Exception(const std::string& what, const std::string& hint) : std::logic_error(what.c_str()), _hint(hint) {}

    const std::string& hint() const { return _hint; }

    void display() const {
        error(what());
        print("\n");
        info(hint());
    }

  private:
    std::string _hint;
};

class Tokenizer {
  public:
    struct Token {
        enum class Type {
            EndStatement,
            Comma,
            OpenScope,
            CloseScope,
            Colon,

            // Constants
            Digits,
            Float,
            CharLiteral,
            StringLiteral,
            Boolean,
            // Operators
            Assignment,
            Xor,
            Or,
            And,
            Equal,
            Different,
            Lesser,
            LesserOrEqual,
            Greater,
            GreaterOrEqual,
            Addition,
            Substraction,
            Multiplication,
            Division,
            Modulus,
            Increment,
            Decrement,
            OpenParenthesis,
            CloseParenthesis,
            OpenSubscript,
            CloseSubscript,
            MemberAccess,

            Identifier,
            // Keywords
            Import,
            If,
            Else,
            While,
            For,
            Type,
            Function,
            Return,
            Const,

            Comment,

            Unknown
        };

        Token() = default;

        Token(Type type, const std::string_view val, size_t line, size_t column) : type(type), value(val), line(line), column(column) {}

        Type             type = Type::Unknown;
        std::string_view value;

        // Debug Info
        size_t line = 0;
        size_t column = 0; // FIXME: TODO
    };

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
    std::string point_error(size_t at, size_t line, size_t from = (std::numeric_limits<size_t>::max)(), size_t to = (std::numeric_limits<size_t>::max)()) const noexcept;

    static constexpr std::string_view control_chars = ";{}";
    static constexpr std::string_view operators_chars = ".=*/+-^!<>&|%()[]";

    static inline bool    is_allowed_in_operators(char c) { return operators_chars.find(c) != operators_chars.npos; }
    static constexpr char escaped_char[] = {'?', '\'', '\"', '\?', '\a', '\b', '\f', '\n', '\r', '\t', '\v'};

    const het_unordered_map<Token::Type> operators{
        {"=", Token::Type::Assignment},
        {"*", Token::Type::Multiplication},
        {"+", Token::Type::Addition},
        {"-", Token::Type::Substraction},
        {"/", Token::Type::Division},
        {"^", Token::Type::Xor},
        {"==", Token::Type::Equal},
        {"!=", Token::Type::Different},
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
        {"function", Token::Type::Function}, {"return", Token::Type::Return},     {"if", Token::Type::If},
        {"else", Token::Type::Else},         {"while", Token::Type::While},       {"for", Token::Type::For},
        {"bool", Token::Type::Identifier},   {"int", Token::Type::Identifier},    {"float", Token::Type::Identifier},
        {"char", Token::Type::Identifier},   {"string", Token::Type::Identifier}, {"true", Token::Type::Boolean},
        {"false", Token::Type::Boolean},     {"const", Token::Type::Const},       {"import", Token::Type::Import},
        {"type", Token::Type::Type},         {"and", Token::Type::And},           {"or", Token::Type::Or},
    };

    const std::string& _source;
    size_t             _current_pos = 0;
    size_t             _current_line = 0;
    size_t             _current_column = 0;
};

// fmt Formaters for Token and Token::Type

#include <fmt/core.h>
#include <fmt/format.h>

template<>
struct fmt::formatter<Tokenizer::Token> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for Tokenizer::Token");
        return it;
    }

    template<typename FormatContext>
    auto format(const Tokenizer::Token& t, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), fg(fmt::color::gray), "T({} {:12} {:3})", t.type, t.value, t.line);
    }
};

template<>
struct fmt::formatter<Tokenizer::Token::Type> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for Tokenizer::Token::Type");
        return it;
    }
    template<typename FormatContext>
    auto format(const Tokenizer::Token::Type& t, FormatContext& ctx) const -> decltype(ctx.out()) {
        switch(t) {
#define OP(name) \
    case Tokenizer::Token::Type::name: return fmt::format_to(ctx.out(), "{:12}", #name);
            OP(Comma);
            OP(EndStatement);
            OP(OpenScope);
            OP(CloseScope);
            OP(Digits);
            OP(Boolean);
            OP(Assignment);
            OP(Xor);
            OP(Or);
            OP(And);
            OP(Equal);
            OP(Different);
            OP(Lesser);
            OP(LesserOrEqual);
            OP(Greater);
            OP(GreaterOrEqual);
            OP(Addition);
            OP(Substraction);
            OP(Multiplication);
            OP(Division);
            OP(Modulus);
            OP(Increment);
            OP(Decrement);
            OP(OpenParenthesis);
            OP(CloseParenthesis);
            OP(OpenSubscript);
            OP(CloseSubscript);
            OP(MemberAccess);
            case Tokenizer::Token::Type::Function: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "Function");
            case Tokenizer::Token::Type::While: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "While");
            case Tokenizer::Token::Type::If: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "If");
            case Tokenizer::Token::Type::Else: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "Else");
            case Tokenizer::Token::Type::Identifier: return fmt::format_to(ctx.out(), fg(fmt::color::light_blue), "{:12}", "Identifier");
#undef OP
            default:
            case Tokenizer::Token::Type::Unknown: return fmt::format_to(ctx.out(), "{:12}", "Unknown");
        }
    }
};

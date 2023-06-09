#pragma once

#include <cassert>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>

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
        Export,
        Extern, // Allow defining functions that will be linked later (useful for providing C++ functions for example)
        If,
        Else,
        While,
        For,
        Type,
        Let,
        Function,
        Return,
        Const,
        Sizeof,

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

// fmt Formaters for Token and Token::Type

template<>
struct fmt::formatter<Token> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for Token");
        return it;
    }

    template<typename FormatContext>
    auto format(const Token& t, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), fg(fmt::color::gray), "T({} {:12} {:3})", t.type, t.value, t.line);
    }
};

template<>
struct fmt::formatter<Token::Type> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for Token::Type");
        return it;
    }
    template<typename FormatContext>
    auto format(const Token::Type& t, FormatContext& ctx) const -> decltype(ctx.out()) {
        switch(t) {
#define OP(name) \
    case Token::Type::name: return fmt::format_to(ctx.out(), "{:12}", #name);
            OP(Comma);
            OP(EndStatement);
            OP(OpenScope);
            OP(CloseScope);
            OP(Digits);
            OP(Float);
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
            OP(Sizeof);
            case Token::Type::Function: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "Function");
            case Token::Type::For: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "While");
            case Token::Type::While: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "While");
            case Token::Type::If: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "If");
            case Token::Type::Else: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "Else");
            case Token::Type::Identifier: return fmt::format_to(ctx.out(), fg(fmt::color::light_blue), "{:12}", "Identifier");
            case Token::Type::CharLiteral: return fmt::format_to(ctx.out(), fg(fmt::color::burly_wood), "{:12}", "CharLiteral");
            case Token::Type::StringLiteral: return fmt::format_to(ctx.out(), fg(fmt::color::burly_wood), "{:12}", "StrLiteral");
            case Token::Type::Return: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "Return");
#undef OP
            case Token::Type::Unknown: return fmt::format_to(ctx.out(), "{:12}", "Unknown");
            default: assert(false); return fmt::format_to(ctx.out(), "{:12}", "Invalid");
        }
    }
};

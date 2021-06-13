#pragma once

#include <array>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <fmt/format.h>

class Tokenizer {
  public:
    struct Token {
        enum class Type
        {
            Control,
            Keyword,
            Digits,
            BuiltInType,
            Operator,
            Identifier,
            If,
            Else,
            While,
            Unknown
        };

        Token() = default;

        Token(Type type, const std::string_view val, size_t line) : type(type), value(val), line(line) {
        }

        Type             type = Type::Unknown;
        std::string_view value;

        // Debug Info
        size_t line = 0;
    };

    Tokenizer(const std::string& source) : _source(source) {
        advance_ptr(_current_pos);
    }

    Token next() const {
        auto curr = _current_pos;
        return search_next(curr);
    }

    Token consume() {
        auto t = search_next(_current_pos);
        // Skip discardable characters immediatly (avoid ending empty token)
        advance_ptr(_current_pos);
        return t;
    }

    bool has_more() const {
        return _current_pos < _source.length() - 1;
    }

  private:
    bool is_discardable(char c) const {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t';
    }

    bool is_allowed_in_identifiers(char c) const {
        return c >= 'A' && c <= 'z' || c == '_';
    }

    bool is_digit(char c) const {
        return c >= '0' && c <= '9';
    }

    void advance_ptr(size_t& pointer) {
        // Skip discardable characters
        while(is_discardable(_source[pointer]) && pointer < _source.length()) {
            if(_source[pointer] == '\n')
                ++_current_line;
            ++pointer;
        }
    }

    constexpr static std::array<char, 6> binary_operators{'=', '*', '+', '-', '/', '^'};

    constexpr static std::array<char, 7> control_characters{
        ';', '(', ')', '{', '}', '[', ']',
    };

    template <int N>
    static bool is(char c, std::array<char, N> arr) {
        for(const auto& s : arr)
            if(s == c)
                return true;
        return false;
    }

    std::unordered_map<std::string, Token::Type> keywords{
        {"function", Token::Type::Keyword}, {"if", Token::Type::If},           {"else", Token::Type::Else},         {"while", Token::Type::While},
        {"bool", Token::Type::BuiltInType}, {"int", Token::Type::BuiltInType}, {"float", Token::Type::BuiltInType}, {"string", Token::Type::BuiltInType},
    };

    Token search_next(size_t& pointer) const {
        auto type       = Token::Type::Unknown;
        auto begin      = pointer;
        auto first_char = _source[pointer];

        if(is(first_char, binary_operators)) {
            pointer += 1;
            type = Token::Type::Operator;
        } else if(is(first_char, control_characters)) {
            pointer += 1;
            type = Token::Type::Control;
        } else if(is_digit(first_char)) {
            while(is_digit(_source[pointer]) && pointer < _source.length())
                ++pointer;
            type = Token::Type::Digits;
        } else {
            while(is_allowed_in_identifiers(_source[pointer]) && pointer < _source.length()) {
                ++pointer;
            }
            const std::string str{_source.begin() + begin, _source.begin() + pointer}; // Having to construct a string here isn't great.
            if(keywords.contains(str))
                type = keywords.at(str);
            else
                type = Token::Type::Identifier;
        }
        return Token{type, std::string_view{_source.begin() + begin, _source.begin() + pointer}, _current_line};
    }

    const std::string& _source;
    size_t             _current_pos  = 0;
    size_t             _current_line = 0;
};

template <>
struct fmt::formatter<Tokenizer::Token> {
    constexpr auto parse(format_parse_context& ctx) {
        // auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) // c++11
        // [ctx.begin(), ctx.end()) is a character range that contains a part of
        // the format string starting from the format specifications to be parsed,
        // e.g. in
        //
        //   fmt::format("{:f} - point of interest", point{1, 2});
        //
        // the range will contain "f} - point of interest". The formatter should
        // parse specifiers until '}' or the end of the range. In this example
        // the formatter should parse the 'f' specifier and return an iterator
        // pointing to '}'.

        // Parse the presentation format and store it in the formatter:
        auto it = ctx.begin(), end = ctx.end();
        // Check if reached the end of the range:
        // Allow only empty format.
        if(it != end && *it != '}')
            throw format_error("Invalid format for Tokenizer::Token");

        // Return an iterator past the end of the parsed range:
        return it;
    }

    // Formats the point p using the parsed format specification (presentation)
    // stored in this formatter.
    template <typename FormatContext>
    auto format(const Tokenizer::Token& t, FormatContext& ctx) {
        // auto format(const point &p, FormatContext &ctx) -> decltype(ctx.out()) // c++11
        // ctx.out() is an output iterator to write to.
        return format_to(ctx.out(), "Token({}, '{}', Ln: {})", t.type, t.value, t.line);
    }
};

template <>
struct fmt::formatter<Tokenizer::Token::Type> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for Tokenizer::Token::Type");
        return it;
    }
    template <typename FormatContext>
    auto format(const Tokenizer::Token::Type& t, FormatContext& ctx) {
        switch(t) {
            case Tokenizer::Token::Type::Control:
                return format_to(ctx.out(), "{}", "Control");
            case Tokenizer::Token::Type::Keyword:
                return format_to(ctx.out(), "{}", "Keyword");
            case Tokenizer::Token::Type::Digits:
                return format_to(ctx.out(), "{}", "Digits");
            case Tokenizer::Token::Type::BuiltInType:
                return format_to(ctx.out(), "{}", "BuiltInType");
            case Tokenizer::Token::Type::Operator:
                return format_to(ctx.out(), "{}", "Operator");
            case Tokenizer::Token::Type::Identifier:
                return format_to(ctx.out(), "{}", "Identifier");
            default:
            case Tokenizer::Token::Type::Unknown:
                return format_to(ctx.out(), "{}", "Unknown");
        }
    }
};
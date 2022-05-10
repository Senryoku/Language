#pragma once

#include <array>
#include <het_unordered_map.hpp>
#include <string>
#include <string_view>

#include <Logger.hpp>

class Tokenizer {
  public:
    struct Token {
        enum class Type {
            Control,
            Function,
            Return,
            // Constants
            Digits,
            Float,
            CharLiteral,
            StringLiteral,
            Boolean,

            BuiltInType,
            Operator,
            Identifier,

            If,
            Else,
            While,

            Unknown
        };

        Token() = default;

        Token(Type type, const std::string_view val, size_t line) : type(type), value(val), line(line) {}

        Type             type = Type::Unknown;
        std::string_view value;

        // Debug Info
        size_t line = 0;
    };

    Tokenizer(const std::string& source) : _source(source) { advance_ptr(_current_pos); }

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

    bool has_more() const { return _current_pos < _source.length(); }

  private:
    inline bool is_discardable(char c) const { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
    inline bool is_allowed_in_identifiers(char c) const { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
    inline bool is_digit(char c) const { return c >= '0' && c <= '9'; }

    void advance_ptr(size_t& pointer) {
        // Skip discardable characters
        while(is_discardable(_source[pointer]) && pointer < _source.length()) {
            if(_source[pointer] == '\n')
                ++_current_line;
            ++pointer;
        }
    }

    constexpr static std::array<char, 7> control_characters{
        ';', '(', ')', '{', '}', '[', ']',
    };

    template<size_t N>
    static bool is(char c, std::array<char, N> arr) {
        for(const auto& s : arr)
            if(s == c)
                return true;
        return false;
    }

    static constexpr char escaped_char[] = {'?', '\'', '\"', '\?', '\a', '\b', '\f', '\n', '\r', '\t', '\v'};

    const het_unordered_map<Token::Type> binary_operators{
        {"=", Token::Type::Operator},  {"*", Token::Type::Operator},  {"+", Token::Type::Operator},  {"-", Token::Type::Operator},  {"/", Token::Type::Operator},
        {"^", Token::Type::Operator},  {"==", Token::Type::Operator}, {"!=", Token::Type::Operator}, {">", Token::Type::Operator},  {"<", Token::Type::Operator},
        {">=", Token::Type::Operator}, {"<=", Token::Type::Operator}, {"&&", Token::Type::Operator}, {"||", Token::Type::Operator}, {"%", Token::Type::Operator}};

    // FIXME: This is a workaround, not a proper way to recognize operators :)
    static inline bool is_allowed_in_operators(char c) { return (c >= '*' && c <= '/') || (c >= '<' && c <= '>') || (c == '&' || c == '|' || c == '%'); }

    const het_unordered_map<Token::Type> keywords{
        {"function", Token::Type::Function}, {"return", Token::Type::Return},      {"if", Token::Type::If},           {"else", Token::Type::Else},
        {"while", Token::Type::While},       {"bool", Token::Type::BuiltInType},   {"int", Token::Type::BuiltInType}, {"float", Token::Type::BuiltInType},
        {"char", Token::Type::BuiltInType},  {"string", Token::Type::BuiltInType}, {"true", Token::Type::Boolean},    {"false", Token::Type::Boolean},
    };

    Token search_next(size_t& pointer) const {
        auto type = Token::Type::Unknown;
        auto begin = pointer;
        auto first_char = _source[pointer];

        // FIXME: Should be more specific
        if(!is_allowed_in_identifiers(first_char)) {
            if(first_char == ',') {
                pointer += 1;
                type = Token::Type::Control;
            } else if(first_char == '\'') {
                ++pointer;
                type = Token::Type::CharLiteral;
                if(_source[pointer] == '\\') { // Escaped character
                    ++pointer;
                    size_t c = 0;
                    switch(_source[pointer]) {
                        case '\'': c = 1; break;
                        case '"': c = 2; break;
                        case '?': c = 3; break;
                        case '\\': c = 4; break;
                        case 'a': c = 4; break;
                        case 'b': c = 5; break;
                        case 'f': c = 6; break;
                        case 'n': c = 7; break;
                        case 'r': c = 8; break;
                        case 't': c = 9; break;
                        case 'v': c = 10; break;
                        default: error("[Tokenizer:{}] Unknown escape sequence: \\{}.\n", __LINE__, _source[pointer]);
                    }
                    pointer += 2;
                    return Token{type, std::string_view{escaped_char + c, escaped_char + c + 1}, _current_line};
                } else {
                    pointer += 2;
                    return Token{type, std::string_view{_source.begin() + begin + 1, _source.begin() + (pointer - 1)}, _current_line};
                }
            } else if(first_char == '"') {
                ++pointer; // Skip '"'
                while(pointer < _source.length() && _source[pointer] != '"')
                    ++pointer;
                assert(pointer < _source.length());
                ++pointer; // Skip '"'
                type = Token::Type::StringLiteral;
                return Token{type, std::string_view{_source.begin() + begin + 1, _source.begin() + (pointer - 1)}, _current_line};
            } else if(is(first_char, control_characters)) {
                pointer += 1;
                type = Token::Type::Control;
            } else if(is_digit(first_char)) {
                bool found_decimal_separator = false;
                while(pointer < _source.length() && (is_digit(_source[pointer]) || _source[pointer] == '.')) {
                    if(_source[pointer] == '.') {
                        if(found_decimal_separator)
                            error("[Tokenizer] Unexcepted supernumerary '.' in float constant on line {}.\n", _current_line);
                        found_decimal_separator = true;
                    }
                    ++pointer;
                }
                if(found_decimal_separator)
                    type = Token::Type::Float;
                else
                    type = Token::Type::Digits;
            } else {
                // Operators
                // FIXME (Better solution than is_allowed_in_operators?)
                while(pointer < _source.length() && !is_discardable(_source[pointer]) && is_allowed_in_operators(_source[pointer]))
                    ++pointer;
                if(binary_operators.contains(std::string_view{_source.begin() + begin, _source.begin() + pointer})) {
                    type = Token::Type::Operator;
                }
            }
        } else {
            while(is_allowed_in_identifiers(_source[pointer]) && pointer < _source.length())
                ++pointer;

            const std::string_view str{_source.begin() + begin, _source.begin() + pointer};
            if(keywords.contains(str))
                type = keywords.find(str)->second;
            else
                type = Token::Type::Identifier;
        }
        return Token{type, std::string_view{_source.begin() + begin, _source.begin() + pointer}, _current_line};
    }

    const std::string& _source;
    size_t             _current_pos = 0;
    size_t             _current_line = 0;
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
            case Tokenizer::Token::Type::Control: return fmt::format_to(ctx.out(), "{:12}", "Control");
            case Tokenizer::Token::Type::Function: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "Function");
            case Tokenizer::Token::Type::While: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "While");
            case Tokenizer::Token::Type::If: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "If");
            case Tokenizer::Token::Type::Else: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{:12}", "Else");
            case Tokenizer::Token::Type::Digits: return fmt::format_to(ctx.out(), "{:12}", "Digits");
            case Tokenizer::Token::Type::Boolean: return fmt::format_to(ctx.out(), "{:12}", "Boolean");
            case Tokenizer::Token::Type::BuiltInType: return fmt::format_to(ctx.out(), "{:12}", "BuiltInType");
            case Tokenizer::Token::Type::Operator: return fmt::format_to(ctx.out(), "{:12}", "Operator");
            case Tokenizer::Token::Type::Identifier: return fmt::format_to(ctx.out(), fg(fmt::color::light_blue), "{:12}", "Identifier");
            default:
            case Tokenizer::Token::Type::Unknown: return fmt::format_to(ctx.out(), "{:12}", "Unknown");
        }
    }
};

#include "Tokenizer.hpp"

Tokenizer::Token Tokenizer::search_next(size_t& pointer) const {
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
            if(operators.contains(std::string_view{_source.begin() + begin, _source.begin() + pointer})) {
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

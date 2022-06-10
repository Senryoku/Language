#include "Tokenizer.hpp"

void Tokenizer::advance() noexcept {
    if(is_newline(peek()))
        newline();
    else
        ++_current_column;
    ++_current_pos;
}

void Tokenizer::newline() noexcept {
    ++_current_line;
    _current_column = 0;
}

void Tokenizer::skip_whitespace() noexcept {
    while(!eof() && is_discardable(peek()))
        advance();
}

Tokenizer::Token Tokenizer::search_next() {
    auto type = Token::Type::Unknown;
    auto begin = _current_pos;
    auto first_char = peek();

    // FIXME: Should be more specific
    if(!is_allowed_in_identifiers(first_char)) {
        switch(first_char) {
            case '\'': {
                advance();
                type = Token::Type::CharLiteral;
                if(peek() == '\\') { // Escaped character
                    advance();
                    size_t c = 0;
                    switch(peek()) {
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
                        default: throw Exception(fmt::format("[Tokenizer] Unknown escape sequence \\'{}'.", peek()), point_error(_current_pos, _current_line, begin));
                    }
                    advance();
                    advance();
                    return Token{type, std::string_view{escaped_char + c, escaped_char + c + 1}, _current_line, _current_column};
                } else {
                    advance();
                    advance();
                    if(eof()) {
                        throw Exception(fmt::format("[Tokenizer] Reached end of file without matching ' on line {}.", _current_line),
                                        point_error(begin, _current_line, begin, _source.length() - 1));
                    }
                    return Token{type, std::string_view{_source.begin() + begin + 1, _source.begin() + (_current_pos - 1)}, _current_line, _current_column};
                }
                break;
            }
            case '"': {
                advance();                     // Skip '"'
                while(!eof() && peek() != '"') // TODO: Handle escaped "
                    advance();
                if(eof())
                    throw Exception(fmt::format("[Tokenizer] Reached end of file without matching \" on line {}.", _current_line),
                                    point_error(begin, _current_line, begin, _source.length() - 1));
                advance(); // Skip '"'
                type = Token::Type::StringLiteral;
                return Token{type, std::string_view{_source.begin() + begin + 1, _source.begin() + (_current_pos - 1)}, _current_line, _current_column};
            }
            case ',': [[fallthrough]];
            case '{': [[fallthrough]];
            case '}': {
                advance();
                type = Token::Type::Control;
                break;
            }
            case '/':
                // Comments, check for a second /, fallthrough to the general case if not found.
                if(_current_pos + 1 < _source.length() && _source[_current_pos + 1] == '/') {
                    type = Token::Type::Comment;
                    while(!eof() && peek() != '\n')
                        advance();
                    break;
                }
                [[fallthrough]];
            default: {
                if(is_digit(first_char)) {
                    bool found_decimal_separator = false;
                    while(!eof() && (is_digit(peek()) || peek() == '.')) {
                        if(peek() == '.') {
                            if(found_decimal_separator)
                                throw Exception(fmt::format("Usupernumerary '.' in float constant on line {}.", _current_line), point_error(_current_pos, _current_line, begin));
                            found_decimal_separator = true;
                        }
                        advance();
                    }
                    type = found_decimal_separator ? Token::Type::Float : Token::Type::Digits;
                } else {
                    type = Token::Type::Operator;
                    auto temp_cursor = _current_pos;
                    // Operators
                    // FIXME (Better solution than is_allowed_in_operators?)
                    while(!eof() && !is_discardable(peek()) && is_allowed_in_operators(peek()))
                        ++temp_cursor;
                    const auto end = temp_cursor;
                    while(temp_cursor != begin && !operators.contains(std::string_view{_source.begin() + begin, _source.begin() + temp_cursor}))
                        --temp_cursor;
                    if(temp_cursor == begin)
                        throw Exception(fmt::format("Error: No matching operator for '{}'.", std::string_view{_source.begin() + begin, _source.begin() + end}),
                                        point_error(temp_cursor, _current_line, begin, end));
                    // Sync our cursor with the temp one.
                    while(_current_pos != temp_cursor)
                        advance();
                }
            }
        }
    } else {
        while((is_allowed_in_identifiers(peek()) || is_digit(peek())) && !eof())
            advance();

        const std::string_view str{_source.begin() + begin, _source.begin() + _current_pos};
        if(auto it = keywords.find(str); it != keywords.end())
            type = it->second;
        else
            type = Token::Type::Identifier;
    }
    return Token{type, std::string_view{_source.begin() + begin, _source.begin() + _current_pos}, _current_line, _current_column};
}

std::string Tokenizer::point_error(size_t at, size_t line, int from, int to) const noexcept {
    std::string return_value = "";
    // Search previous line break
    auto line_start = at;
    while(line_start > 0 && _source[line_start] != '\n')
        --line_start;
    ++line_start;
    auto line_end = at;
    while(line_end < _source.size() && _source[line_end] != '\n')
        ++line_end;
    auto line_info = fmt::format("Line {}: ", line); // FIXME:
    // Display the source line containing the error
    return_value += fmt::format("{}{}\n", line_info, std::string_view{_source.begin() + line_start, _source.begin() + line_end});
    // Point at it
    if(from < 0 && to < 0) {
        return_value += fmt::format("{:>{}s}\n", "^", line_info.size() + at - line_start + 1);
    } else {
        if(from < 0)
            from = static_cast<int>(at);
        if(to < 0)
            to = static_cast<int>(at);
        std::string str = "";
        for(size_t i = 0; i < std::max(at, static_cast<size_t>(std::max(to, from))) - line_start + line_info.size() + 1; ++i)
            str += " ";
        for(size_t i = from - line_start + line_info.size(); i < to - line_start + line_info.size(); ++i)
            str[i] = '~';
        str[at - line_start + line_info.size()] = '^';
        return_value += fmt::format("{}\n", str);
    }
    return return_value;
}

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

Token Tokenizer::search_next() {
    auto type = Token::Type::Unknown;
    auto begin = _current_pos;
    auto first_char = peek();
    advance();

    // FIXME: Should be more specific
    if(!is_allowed_in_identifiers(first_char)) {
        switch(first_char) {
            case '\'': {
                type = Token::Type::CharLiteral;
                if(eof())
                    throw Exception(fmt::format("[Tokenizer] Error: Reached end of file without matching ' on line {}.", _current_line),
                                    point_error(begin, _current_line, begin, _source.length() - 1));
                if(peek() == '\\') { // Escaped character
                    advance();
                    if(eof())
                        throw Exception(fmt::format("[Tokenizer] Error: Expected escape sequence, got EOF on line {}.", _current_line),
                                        point_error(begin, _current_line, begin, _source.length() - 1));
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
                        default: throw Exception(fmt::format("[Tokenizer] Error: Unknown escape sequence \\'{}'.", peek()), point_error(_current_pos, _current_line, begin));
                    }
                    advance();
                    if(eof() || peek() != '\'')
                        throw Exception(fmt::format("[Tokenizer] Error: Reached end of file without matching ' on line {}.", _current_line),
                                        point_error(begin, _current_line, begin, _source.length() - 1));
                    advance(); // Skip '
                    return Token{type, std::string_view{escaped_char + c, escaped_char + c + 1}, _current_line, _current_column};
                } else {
                    advance();
                    if(eof() || peek() != '\'')
                        throw Exception(fmt::format("[Tokenizer] Error: Reached end of file without matching ' on line {}.", _current_line),
                                        point_error(begin, _current_line, begin, _source.length() - 1));
                    advance(); // Skip '
                    return Token{type, std::string_view{_source.begin() + begin + 1, _source.begin() + (_current_pos - 1)}, _current_line, _current_column};
                }
                break;
            }
            case '"': {
                // For strings, the escape sequences will be handled by the Parser
                while(!eof() && peek() != '"')
                {
                    if(peek() == '\\') {
                        advance();
                        if(eof())
                            break;
                        if(peek() == '"')
                            advance();
                    }
                    advance();
                }
                if(eof())
                    throw Exception(fmt::format("[Tokenizer] Error: Reached end of file without matching \" on line {}.", _current_line),
                                    point_error(begin, _current_line, begin, _source.length() - 1));
                advance(); // Skip '"'
                type = Token::Type::StringLiteral;
                return Token{type, std::string_view{_source.begin() + begin + 1, _source.begin() + (_current_pos - 1)}, _current_line, _current_column};
            }
            case ',': type = Token::Type::Comma; break;
            case ';': type = Token::Type::EndStatement; break;
            case '{': type = Token::Type::OpenScope; break;
            case '}': type = Token::Type::CloseScope; break;
            case '/':
                // Comments, checks for a second /, fallthrough to the general case if not found.
                if(!eof() && peek() == '/') {
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
                                throw Exception(fmt::format("[Tokenizer] Error: Unexpected supernumerary '.' in float constant on line {}.", _current_line),
                                                point_error(_current_pos, _current_line, begin));
                            found_decimal_separator = true;
                        }
                        advance();
                    }
                    type = found_decimal_separator ? Token::Type::Float : Token::Type::Digits;
                } else {
                    auto temp_cursor = _current_pos;
                    // Operators
                    while(!eof() && !is_discardable(_source[temp_cursor]) && is_allowed_in_operators(_source[temp_cursor]))
                        ++temp_cursor;
                    const auto end = temp_cursor;
                    while(temp_cursor != begin && !operators.contains(std::string_view{_source.begin() + begin, _source.begin() + temp_cursor}))
                        --temp_cursor;
                    if(temp_cursor == begin)
                        throw Exception(fmt::format("[Tokenizer] Error: No matching operator for '{}'.", std::string_view{_source.begin() + begin, _source.begin() + end}),
                                        point_error(temp_cursor, _current_line, begin, end));
                    type = operators.find(std::string_view{_source.begin() + begin, _source.begin() + temp_cursor})->second;
                    // Sync our cursor with the temp one.
                    while(_current_pos != temp_cursor)
                        advance();
                }
            }
        }
    } else {
        while(!eof() && (is_allowed_in_identifiers(peek()) || is_digit(peek())))
            advance();

        const std::string_view str{_source.begin() + begin, _source.begin() + _current_pos};
        if(auto it = keywords.find(str); it != keywords.end())
            type = it->second;
        else
            type = Token::Type::Identifier;
    }
    return Token{type, std::string_view{_source.begin() + begin, _source.begin() + _current_pos}, _current_line, _current_column};
}

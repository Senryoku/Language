#pragma once

#include <array>
#include <deque>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef WIN32
#include <stdio.h>
#include <strsafe.h>
#include <tchar.h>
#define NOMINMAX
#include <windows.h>
#endif

#include <Logger.hpp>
#include <Tokenizer.hpp>

class Prompt {
  public:
    const bool complex_prompt = true; // If false, will fallback to a simple stdin.getline() repl

    Prompt();
    ~Prompt();

    std::string get_line();
    // Returns str source with color information for terminal display.
    // Currently work on a token basis, could be improved to use the full AST, but it's probably not worth it for the REPL (and slower, more complicated).
    std::string syntax_highlight(const std::string& str);

  private:
    void                     add_history(const std::string& str);
    std::vector<std::string> autocomplete(const std::string& str) const;

    std::deque<std::string> _history;

    // FIXME: Come up with a better theme. Make it configurable? Also, clearly, rework the token types.
    inline static std::unordered_map<Token::Type, fmt::color> s_token_colors = {
        {Token::Type::Unknown, fmt::color::red},
        {Token::Type::Boolean, fmt::color::royal_blue},
        {Token::Type::CharLiteral, fmt::color::burly_wood},
        {Token::Type::Comment, fmt::color::dark_green},
        {Token::Type::Const, fmt::color::royal_blue},
        {Token::Type::EndStatement, fmt::color::light_gray},
        {Token::Type::Digits, fmt::color::golden_rod},
        {Token::Type::If, fmt::color::royal_blue},
        {Token::Type::Else, fmt::color::royal_blue},
        {Token::Type::While, fmt::color::royal_blue},
        {Token::Type::Float, fmt::color::golden_rod},
        {Token::Type::Function, fmt::color::royal_blue},
        {Token::Type::Import, fmt::color::royal_blue},
        {Token::Type::StringLiteral, fmt::color::burly_wood},
        {Token::Type::Identifier, fmt::color::light_blue},
    };

#ifdef WIN32
    HANDLE _stdin_handle = INVALID_HANDLE_VALUE;
    HANDLE _stdout_handle = INVALID_HANDLE_VALUE;
    DWORD  _saved_console_mode = 0;
#endif
};

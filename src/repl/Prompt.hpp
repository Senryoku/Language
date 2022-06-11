#pragma once

#include <array>
#include <deque>
#include <iostream>
#include <string>
#include <string_view>

#ifdef WIN32
#include <stdio.h>
#include <strsafe.h>
#include <tchar.h>
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
    void add_history(const std::string& str);

    std::deque<std::string> _history;

    // FIXME: Come up with a better theme. Make it configurable? Also, clearly, rework the token types.
    inline static std::unordered_map<Tokenizer::Token::Type, fmt::color> s_token_colors = {
        {Tokenizer::Token::Type::Unknown, fmt::color::red},
        {Tokenizer::Token::Type::Boolean, fmt::color::royal_blue},
        {Tokenizer::Token::Type::BuiltInType, fmt::color::royal_blue},
        {Tokenizer::Token::Type::CharLiteral, fmt::color::burly_wood},
        {Tokenizer::Token::Type::Comment, fmt::color::dark_green},
        {Tokenizer::Token::Type::Const, fmt::color::royal_blue},
        {Tokenizer::Token::Type::Control, fmt::color::light_gray},
        {Tokenizer::Token::Type::Digits, fmt::color::golden_rod},
        {Tokenizer::Token::Type::If, fmt::color::royal_blue},
        {Tokenizer::Token::Type::Else, fmt::color::royal_blue},
        {Tokenizer::Token::Type::While, fmt::color::royal_blue},
        {Tokenizer::Token::Type::Float, fmt::color::golden_rod},
        {Tokenizer::Token::Type::Function, fmt::color::royal_blue},
        {Tokenizer::Token::Type::Import, fmt::color::royal_blue},
        {Tokenizer::Token::Type::StringLiteral, fmt::color::burly_wood},
        {Tokenizer::Token::Type::Identifier, fmt::color::light_blue},
    };

#ifdef WIN32
    HANDLE _stdin_handle = INVALID_HANDLE_VALUE;
    HANDLE _stdout_handle = INVALID_HANDLE_VALUE;
    DWORD  _saved_console_mode = 0;
#endif
};

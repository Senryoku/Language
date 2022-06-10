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

#ifdef WIN32
void win_error_exit(const char* lpszFunction) {
    error("Error: {}\n", lpszFunction);

    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD  dw = GetLastError();

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&lpMsgBuf, 0, NULL);

    // Display the error message and exit the process

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf, LocalSize(lpDisplayBuf) / sizeof(TCHAR), TEXT("%s failed with error %d: %s"), lpszFunction, dw, lpMsgBuf);
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(dw);
}
#endif;

class Prompt {
  public:
    const bool complex_prompt = true; // If false, will fallback to a simple stdin.getline() repl

    Prompt() {
#ifdef WIN32
        if(complex_prompt) {
            // Get the standard input handle.
            _stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
            if(_stdin_handle == INVALID_HANDLE_VALUE)
                win_error_exit("GetStdHandle");
            _stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
            if(_stdout_handle == INVALID_HANDLE_VALUE)
                win_error_exit("GetStdHandle");

            // Save the current input mode, to be restored on exit.
            if(!GetConsoleMode(_stdin_handle, &_saved_console_mode))
                win_error_exit("GetConsoleMode");

            // Enable the window input events.
            auto fdwMode = ENABLE_WINDOW_INPUT | ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS;
            if(!SetConsoleMode(_stdin_handle, fdwMode))
                win_error_exit("SetConsoleMode");
        }
#endif
    }

    ~Prompt() {
#ifdef WIN32
        if(complex_prompt)
            SetConsoleMode(_stdin_handle, _saved_console_mode);
#endif
    }

    std::string get_line() {
#ifdef WIN32
        if(complex_prompt) {
            DWORD                         num_read = 0;
            std::array<INPUT_RECORD, 128> input_buffer;
            std::string                   current_line;
            size_t                        cursor = 0;

            size_t      in_history = 0;
            std::string current_line_backup;

            while(true) {
                if(!ReadConsoleInput(_stdin_handle, input_buffer.data(), input_buffer.size(), &num_read))
                    win_error_exit("ReadConsoleInput");

                for(int i = 0; i < num_read; i++) {
                    switch(input_buffer[i].EventType) {
                        case KEY_EVENT: {
                            if(input_buffer[i].Event.KeyEvent.bKeyDown) {

                                switch(input_buffer[i].Event.KeyEvent.wVirtualKeyCode) {
                                        // TODO: For a complete REPL experience (arrow navigation, proper history, autocompletion...)
                                        //       we'll have to handle key press event directly and complete this switch statement.
                                    case 0x25: // Left
                                        if(cursor > 0)
                                            --cursor;
                                        break;
                                    case 0x26: // Up
                                        if(in_history < _history.size()) {
                                            if(in_history == 0) {
                                                current_line_backup = current_line;
                                            }
                                            current_line = *(_history.rbegin() + in_history);
                                            cursor = current_line.size();
                                            ++in_history;
                                        }
                                        break;
                                    case 0x27: // Right
                                        if(cursor < current_line.size())
                                            ++cursor;
                                        break;
                                    case 0x28: // Down
                                        if(in_history > 0) {
                                            --in_history;
                                            if(in_history == 0) {
                                                current_line = current_line_backup;
                                            } else {
                                                current_line = *(_history.rbegin() + (in_history - 1));
                                            }
                                            cursor = current_line.size();
                                        }
                                        break;
                                    case 0x0d: // Return
                                        print("\r\033[0J > {}\n", syntax_highlight(current_line));
                                        add_history(current_line);
                                        return current_line;
                                        break;
                                    case 0x08: // Backspace
                                        if(cursor > 0) {
                                            current_line.erase(cursor - 1, 1);
                                            --cursor;
                                        }
                                        break;
                                    case 0x09: // Tab
                                        break;
                                    default:
                                        // FIXME: This is not a proper way to filter printable caracters (Modifier keys send a keypressed event for example)
                                        if(input_buffer[i].Event.KeyEvent.uChar.AsciiChar >= 0x20 && input_buffer[i].Event.KeyEvent.uChar.AsciiChar < 0x80) {
                                            current_line.insert(cursor, 1, input_buffer[i].Event.KeyEvent.uChar.AsciiChar);
                                            ++cursor;
                                        } else {
                                            std::cout << std::endl
                                                      << "Unhandled key event with virtual key code 0x" << std::hex << input_buffer[i].Event.KeyEvent.wVirtualKeyCode << std::endl;
                                        }
                                }
                            }
                            break;
                        }
                        case MOUSE_EVENT: // Ignore these events
                        case WINDOW_BUFFER_SIZE_EVENT:
                        case FOCUS_EVENT:
                        case MENU_EVENT: break;
                        default: error("Unknown event type"); break;
                    }

                    // \r           Back to the start of the line
                    // \033[0J      Clear to the end of the screen
                    print("\r\033[0J > {}", syntax_highlight(current_line));
                    CONSOLE_SCREEN_BUFFER_INFO console_info;
                    if(!GetConsoleScreenBufferInfo(_stdout_handle, &console_info))
                        win_error_exit("GetConsoleScreenBufferInfo");
                    COORD coords(cursor + 3, console_info.dwCursorPosition.Y);
                    if(SetConsoleCursorPosition(_stdout_handle, coords) == 0)
                        win_error_exit("SetConsoleCursorPosition");
                }
            }
        }
#endif
        // Simple version, using only standard C++
        std::string line;
        print(" > ");
        std::getline(std::cin, line);
        return line;
    }

    // FIXME: Doesn't handle tokenizing errors very well :)
    std::string syntax_highlight(const std::string& str) {
        try {
            Tokenizer                     tokenizer(str);
            std::vector<Tokenizer::Token> tokens;
            while(tokenizer.has_more())
                tokens.push_back(tokenizer.consume());
            std::string r = "";
            auto        cursor_on_input = str.data();
            for(const auto& token : tokens) {
                auto start = token.value.data();
                while(cursor_on_input != start) {
                    r += *cursor_on_input;
                    ++cursor_on_input;
                }
                // r += std::string(cursor_on_input, start);
                switch(token.type) {
                    case Tokenizer::Token::Type::Digits: r += "\033[0;32m"; break;
                    case Tokenizer::Token::Type::Function: r += "\033[0;34m"; break;
                    default: break;
                }
                r += token.value;
                r += "\033[0m";
                cursor_on_input = token.value.data() + token.value.size();
            }
            return r;
        } catch(...) { return str; }
    }

    void add_history(const std::string& str) {
        _history.push_back(str);
        if(_history.size() > 100)
            _history.pop_front();
    }

  private:
    std::deque<std::string> _history;

#ifdef WIN32
    HANDLE _stdin_handle = INVALID_HANDLE_VALUE;
    HANDLE _stdout_handle = INVALID_HANDLE_VALUE;
    DWORD  _saved_console_mode = 0;
#endif
};

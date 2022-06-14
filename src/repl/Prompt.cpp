#include "Prompt.hpp"

#include <filesystem>
#include <win_error.hpp>

#ifdef WIN32
std::string get_clipboard_text() {
    if(!OpenClipboard(nullptr))
        win_error_exit("OpenClipboard");
    HANDLE hData = GetClipboardData(CF_TEXT);
    if(hData == nullptr)
        win_error_exit("GetClipboardData");

    // Lock the handle to get the actual text pointer
    char* pszText = static_cast<char*>(GlobalLock(hData));
    if(pszText == nullptr)
        win_error_exit("GlobalLock");

    std::string text(pszText);

    // Release the lock
    GlobalUnlock(hData);

    CloseClipboard();

    return text;
}
#endif

Prompt::Prompt() {
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

Prompt ::~Prompt() {
#ifdef WIN32
    if(complex_prompt)
        SetConsoleMode(_stdin_handle, _saved_console_mode);
#endif
}

std::string Prompt::get_line() {
    static const std::string_view prompt_str = " > ";

#ifdef WIN32
    if(complex_prompt) {
        // Handle keyboard events rather than simply reading from stdin for a more interactive experience :)
        DWORD                         num_read = 0;
        std::array<INPUT_RECORD, 128> input_buffer;
        std::string                   current_line;
        size_t                        cursor = 0;

        size_t      in_history = 0;
        std::string current_line_backup;

        print(prompt_str);
        while(true) {
            if(!ReadConsoleInput(_stdin_handle, input_buffer.data(), input_buffer.size(), &num_read))
                win_error_exit("ReadConsoleInput");

            for(int i = 0; i < num_read; i++) {
                switch(input_buffer[i].EventType) {
                    case KEY_EVENT: {
                        if(input_buffer[i].Event.KeyEvent.bKeyDown) {
                            switch(input_buffer[i].Event.KeyEvent.wVirtualKeyCode) {
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
                                    print("\r\033[0J{}{}\n", prompt_str, syntax_highlight(current_line));
                                    add_history(current_line);
                                    return current_line;
                                    break;
                                case 0x08: // Backspace
                                    if(cursor > 0) {
                                        current_line.erase(cursor - 1, 1);
                                        --cursor;
                                    }
                                    break;
                                case 0x09: { // Tab
                                    // TODO: Autocomplete on the cursor and not exclusively at the end of the string.
                                    auto candidates = autocomplete(current_line);
                                    if(candidates.size() == 1) {
                                        auto last_blank = current_line.find_last_of(" \""); // FIXME
                                        if(last_blank != current_line.npos)
                                            current_line = current_line.substr(0, last_blank + 1) + candidates[0];
                                        else
                                            current_line = candidates[0];
                                        cursor = current_line.size();
                                    } else {
                                        CONSOLE_SCREEN_BUFFER_INFO console_info;
                                        if(!GetConsoleScreenBufferInfo(_stdout_handle, &console_info))
                                            win_error_exit("GetConsoleScreenBufferInfo");
                                        if(candidates.empty()) {
                                            fmt::print(fg(fmt::color::dark_gray), "\n\033[0JNo match found.");
                                            // TODO: Try WriteConsoleOutput to avoid cursor movement here.
                                        } else {
                                            print("\n\033[0J");
                                            for(auto&& candidate : candidates) {
                                                fmt::print(fg(fmt::color::gray), "{}\t", candidate);
                                            }
                                        }
                                        if(SetConsoleCursorPosition(_stdout_handle, console_info.dwCursorPosition) == 0)
                                            win_error_exit("SetConsoleCursorPosition");
                                    }
                                    break;
                                }
                                case 0x2e: // Suppr
                                    if(cursor < current_line.size()) {
                                        current_line.erase(cursor, 1);
                                    }
                                    break;
                                case 0x10: // Shift
                                case 0x11: // Ctrl
                                case 0x12: // Alt
                                    break;
                                case 0x56: { // Ctrl+V
                                    auto clipboard_text = get_clipboard_text();
                                    if(auto it = clipboard_text.find_first_of('\n'); it != clipboard_text.npos)
                                        clipboard_text = clipboard_text.substr(0, it - 1);
                                    current_line += clipboard_text;
                                    cursor = current_line.size();
                                    break;
                                }
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
                    case MENU_EVENT: // Right click
                        break;
                    default: error("Unknown event type"); break;
                }

                // \r           Back to the start of the line
                // \033[0J      Clear to the end of the screen
                // \033[K      Clear to the end of the screen
                // print("\r\033[0J{}{}", prompt_str, syntax_highlight(current_line));
                print("\r\033[K{}{}", prompt_str, syntax_highlight(current_line));
                CONSOLE_SCREEN_BUFFER_INFO console_info;
                if(!GetConsoleScreenBufferInfo(_stdout_handle, &console_info))
                    win_error_exit("GetConsoleScreenBufferInfo");
                COORD coords(cursor + 3, console_info.dwCursorPosition.Y);
                if(SetConsoleCursorPosition(_stdout_handle, coords) == 0)
                    win_error_exit("SetConsoleCursorPosition");
            }
        }
        // Should not be reached
        return "";
    }
#endif
    // Simple version, using only standard C++
    std::string line;
    print(prompt_str);
    std::getline(std::cin, line);
    return line;
}

// FIXME: Doesn't handle tokenizing errors very well :)
std::string Prompt::syntax_highlight(const std::string& str) {
    try {
        Tokenizer                     tokenizer(str);
        std::vector<Tokenizer::Token> tokens;
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());
        std::string r = "";
        auto        cursor_on_input = str.data();
        for(const auto& token : tokens) {
            // Copy everything between the tokens.
            r += std::string_view{cursor_on_input, token.value.data()};
            // Output the token with the color information.
            auto color = s_token_colors.find(token.type);
            r += fmt::format(fg(color != s_token_colors.end() ? color->second : fmt::color::white), "{}", token.value);
            // Advance cursor_on_input to the end of the token in the input string.
            cursor_on_input = token.value.data() + token.value.size();
        }
        // Concat the end of the input string that might be ignored by the tokenizer (ending quote, whitespaces...)
        r += std::string_view{cursor_on_input, str.data() + str.size()};
        return r;
    } catch(...) { return str; }
}

void Prompt::add_history(const std::string& str) {
    _history.push_back(str);
    if(_history.size() > 100)
        _history.pop_front();
}

std::vector<std::string> Prompt::autocomplete(const std::string& str) const {
    std::vector<std::string> ret;
    try {
        std::filesystem::path path = "./";

        auto last_blank = str.find_last_of(" \"");
        if(last_blank != str.npos)
            path.append(str.substr(last_blank + 1, str.size()));
        else
            path.append(str);
        /*
        // Using the Tokenizer directly could be better (autocomplete on keyword/identifier instead of files depending on the context), but it's not stable enough yet.
        Tokenizer tokenizer(str);
        if(tokenizer.has_more()) {
            auto last_token = tokenizer.consume();
            while(tokenizer.has_more())
                last_token = tokenizer.consume();
            path = last_token.value;
        }
        */
        auto folder = path.parent_path();
        auto name = path.filename().string();

        for(const auto& entry : std::filesystem::directory_iterator(folder))
            if(entry.path().filename().string().starts_with(name))
                ret.push_back(entry.path().lexically_normal().string());

    } catch(...) {}
    return ret;
}

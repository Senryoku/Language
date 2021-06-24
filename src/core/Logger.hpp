#pragma once

#include <fmt/color.h>
#include <fmt/core.h>

template <typename... Args>
void error(Args&&... args) {
    fmt::print(fg(fmt::color::red), std::forward<Args>(args)...);
}

template <typename... Args>
void warn(Args&&... args) {
    fmt::print(fg(fmt::color::yellow), std::forward<Args>(args)...);
}

template <typename... Args>
void success(Args&&... args) {
    fmt::print(fg(fmt::color::green), std::forward<Args>(args)...);
}

template <typename... Args>
void print(Args&&... args) {
    fmt::print(std::forward<Args>(args)...);
}

struct Indenter {
    const size_t tab_size = 4;
    size_t       indent = 0;

    void group() { indent += tab_size; }
    void end() {
        if(indent >= tab_size)
            indent -= tab_size;
        else
            indent = 0;
    }

    template <typename... Args>
    void print(fmt::string_view format_str, Args&&... args) {
        fmt::print("{:{}}", "", indent);
        fmt::print(format_str, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void print_same_line(fmt::string_view format_str, Args&&... args) {
        fmt::print(format_str, std::forward<Args>(args)...);
    }
};
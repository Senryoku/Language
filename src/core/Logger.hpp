#pragma once

#include <fmt/color.h>
#include <fmt/core.h>

template <typename... Args>
void error(Args... args) {
    fmt::print(fg(fmt::color::red), args...);
}

template <typename... Args>
void warn(Args... args) {
    fmt::print(fg(fmt::color::yellow), args...);
}

template <typename... Args>
void success(Args... args) {
    fmt::print(fg(fmt::color::green), args...);
}

template <typename... Args>
void print(Args... args) {
    fmt::print(args...);
}

struct Indenter {
    size_t indent = 0;

    void group() { indent += 4; }
    void end() {
        if(indent >= 4)
            indent -= 4;
        else
            indent = 0;
    }

    template <typename... Args>
    void print(fmt::string_view format_str, const Args&... args) {
        fmt::print("{:{}}", "", indent);
        fmt::print(format_str, args...);
    }
};
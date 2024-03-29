#pragma once

#include <string_view>

#include <fmt/color.h>
#include <fmt/core.h>

inline std::string link(const std::string_view& url, const std::string_view& text) {
    return fmt::format("\x1B]8;;{}\x1B\\{}\x1B]8;;\x1B\\", url, text);
}

inline std::string link(const std::string_view& url) {
    return link(url, url);
}

inline void print_link(const std::string_view& url, const std::string_view& text) {
    fmt::print(fmt::runtime(link(url, text)));
}

template<typename... Args>
inline void error(Args&&... args) {
    fmt::print(fg(fmt::color::red), std::forward<Args>(args)...);
}

template<typename... Args>
inline void info(Args&&... args) {
    fmt::print(fg(fmt::color::light_blue), std::forward<Args>(args)...);
}

template<typename... Args>
inline void warn(Args&&... args) {
    fmt::print(fg(fmt::color::yellow), std::forward<Args>(args)...);
}

template<typename... Args>
inline void success(Args&&... args) {
    fmt::print(fg(fmt::color::green), std::forward<Args>(args)...);
}

template<typename... Args>
inline void print_subtle(Args&&... args) {
    fmt::print(fg(fmt::color::gray), std::forward<Args>(args)...);
}

template<typename... Args>
inline void print(Args&&... args) {
    fmt::print(std::forward<Args>(args)...);
}

template<typename Format, typename... Args>
inline void print(Format&& format, Args&&... args) {
    fmt::print(fmt::runtime(format), std::forward<Args>(args)...); // TODO: Find a way to get rid of this fmt::runtime.
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

    template<typename... Args>
    void print(fmt::string_view format_str, Args&&... args) {
        fmt::print("{:{}}", "", indent);
        fmt::print(fmt::runtime(format_str), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void print_same_line(fmt::string_view format_str, Args&&... args) {
        fmt::print(fmt::runtime(format_str), std::forward<Args>(args)...);
    }
};

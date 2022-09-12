#include <Source.hpp>

#include <cassert>

#include <fmt/format.h>

std::string point_error(const std::string& source, size_t at, size_t line, size_t from, size_t to) noexcept {
    assert((from == std::numeric_limits<size_t>::max() || to == std::numeric_limits<size_t>::max()) || from <= to);
    std::string return_value = "";
    // Search previous line break
    auto line_start = at;
    while(line_start > 0 && source[line_start] != '\n')
        --line_start;
    if(source[line_start] == '\n')
        ++line_start;
    auto line_end = at;
    while(line_end < source.size() && source[line_end] != '\n')
        ++line_end;
    auto line_info = fmt::format("{: >5} | ", line);
    auto padding = fmt::format("{: >{}} | ", "", line_info.size() - 3);
    // Display the source line containing the error
    return_value += fmt::format("{}{}\n", line_info, std::string_view{source.begin() + line_start, source.begin() + line_end});
    // Point at it
    if(from != std::numeric_limits<size_t>::max() && to != std::numeric_limits<size_t>::max()) {
        return_value += fmt::format("{}{:>{}s}\n", padding, "^", at - line_start + 1);
    } else {
        if(from == std::numeric_limits<size_t>::max())
            from = at;
        if(to == std::numeric_limits<size_t>::max())
            to = at;
        std::string str = "";
        for(size_t i = 0; i < std::max(at, static_cast<size_t>(std::max(to, from))) - line_start + 1; ++i)
            str += " ";
        for(size_t i = from - line_start; i < std::min(to - line_start, str.size()); ++i)
            str[i] = '~';
        str[at - line_start] = '^';
        return_value += fmt::format("{}{}\n", padding, str);
    }
    return return_value;
}
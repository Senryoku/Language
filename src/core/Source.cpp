#include <Source.hpp>

#include <cassert>

#include <fmt/format.h>

std::string_view get_nth_line(const std::string& source, size_t n) {
    size_t start = 0;
    while(start < source.size() && n > 0) {
        if(source[start] == '\n')
            --n;
        ++start;
    }
    auto end = source.find('\n', start + 1);
    if(end == source.npos)
        end = source.size();
    else
        ++end; // Include \n
    return std::string_view(source.data() + start, source.data() + end);
}

std::string point_error_find_line(const std::string& source, const Token& token) noexcept {
    return point_error(get_nth_line(source, token.line), token);
}

std::string point_error(const std::string_view& source, const Token& token) noexcept {
    return point_error(source, token.column, token.line, token.column, token.column + token.value.size());
}

std::string point_error_find_line(const std::string& source, size_t at, size_t line, size_t from, size_t to) noexcept {
    return point_error(get_nth_line(source, line), at, line, from, to);
}

std::string point_error(const std::string_view& source, size_t at, size_t line, size_t from, size_t to) noexcept {
    assert((from == std::numeric_limits<size_t>::max() || to == std::numeric_limits<size_t>::max()) || from <= to);
    std::string return_value = "";
    at = std::min(at, source.size() - 1);
    // Search previous line break
    auto line_start = at;
    if(source[line_start] == '\n' && line_start > 0)
        --line_start;
    while(line_start > 0 && source[line_start] != '\n')
        --line_start;
    if(source[line_start] == '\n')
        ++line_start;
    auto line_end = line_start;
    while(line_end < source.size() && source[line_end] != '\n')
        ++line_end;
    auto line_info = fmt::format("{: >5} | ", line + 1);
    auto padding = fmt::format("{: >{}} | ", "", line_info.size() - 3);
    // Display the source line containing the error
    return_value += fmt::format("{}{}\n", line_info, std::string_view{source.begin() + line_start, source.begin() + line_end});
    // Point at it
    // FIXME: Why? I don't remember...
    if(from != std::numeric_limits<size_t>::max() && to != std::numeric_limits<size_t>::max()) {
        std::string point;
        point.resize(std::max(at, static_cast<size_t>(std::max(to, from)) - line_start) + 1, ' ');
        for(size_t i = from - line_start + 1; i < std::min(to - line_start, point.size()) + 1; ++i)
            point[i] = '~';
        point[at - line_start + 1] = '^';
        // Handle tabulations in input line by copying them.
        // for(auto i = padding.size(); i < point.size() && i - padding.size() < source.size(); ++i)
        //  if(source[i - padding.size()] == '\t' && point[i] == ' ')
        //      point[i] = '\t';
        return_value += padding + point + '\n';
    } else {
        if(from == std::numeric_limits<size_t>::max())
            from = at;
        if(to == std::numeric_limits<size_t>::max())
            to = at;
        std::string str = "";
        for(size_t i = 0; i < std::max(at, static_cast<size_t>(std::max(to, from))) - line_start + 1; ++i)
            if(source[i + line_start] == '\t')
                str += '\t';
            else
                str += ' ';
        for(size_t i = from - line_start; i < std::min(to - line_start, str.size()); ++i)
            str[i] = '~';
        str[at - line_start] = '^';
        return_value += fmt::format("{}{}\n", padding, str);
    }
    return return_value;
}
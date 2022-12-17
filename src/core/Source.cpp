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
    return std::string_view(source.data() + start, source.data() + end);
}

// [from, to[
std::string point_error_impl(const std::string_view& line, size_t at, size_t line_number, size_t from, size_t to) noexcept {
    assert((from == std::numeric_limits<size_t>::max() || to == std::numeric_limits<size_t>::max()) || from <= to);
    std::string return_value = "";
    at = std::min(at, line.size() - 1);
    auto line_info = fmt::format("{: >5} | ", line_number + 1);
    auto padding = fmt::format("{: >{}} | ", "", line_info.size() - 3);
    // Display the source line containing the error
    return_value += fmt::format("{}{}\n", line_info, line);

    // Point at it
    if(from == std::numeric_limits<size_t>::max() || from > at)
        from = at;
    if(to == std::numeric_limits<size_t>::max() || to < at)
        to = at;
    std::string point;
    point.resize(line.size() + 1, ' ');
    for(size_t i = from; i < std::min(to, point.size()); ++i)
        point[i] = '~';
    point[at] = '^';
    // Handle tabulations in input line by copying them.
    for(auto i = 0; i < std::min(point.size(), line.size()); ++i)
        if(line[i] == '\t' && point[i] == ' ')
            point[i] = '\t';
    return_value += fmt::format("{}{}\n", padding, point);

    return return_value;
}

std::string point_error_impl(const std::string_view& line, const Token& token) noexcept {
    return point_error_impl(line, token.column, token.line, token.column - 1, token.column - 1 + token.value.size());
}

std::string point_error(const std::string& source, const Token& token) noexcept {
    return point_error_impl(get_nth_line(source, token.line), token);
}
std::string point_error(const std::string& source, size_t at, size_t line_number, size_t from, size_t to) noexcept {
    return point_error_impl(get_nth_line(source, line_number), at, line_number, from, to);
}

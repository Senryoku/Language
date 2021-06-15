#pragma once

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>

struct StringView {
    const char* begin;
    uint32_t    size;

    std::string_view to_std_string_view() const { return std::string_view(begin, begin + size); }
};

union ValueUnion {
    int32_t    as_int32_t;
    StringView as_string; // Not sure if this is the right choice?
};

struct GenericValue {
    enum class Type
    {
        Integer,
        String,
        Composite,
        Undefined
    };

    Type       type;
    ValueUnion value;
};

inline static GenericValue::Type parse_type(const std::string_view& str) {
    using enum GenericValue::Type;
    if(str == "int")
        return Integer;
    return Undefined;
}

template <>
struct fmt::formatter<StringView> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for StringView");
        return it;
    }
    template <typename FormatContext>
    auto format(const StringView& v, FormatContext& ctx) {
        std::string_view str{v.begin, v.begin + v.size};
        return format_to(ctx.out(), "{}", str);
    }
};

template <>
struct fmt::formatter<GenericValue> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for GenericValue");
        return it;
    }
    template <typename FormatContext>
    auto format(const GenericValue& v, FormatContext& ctx) {
        switch(v.type) {
            using enum GenericValue::Type;
            case Integer: return format_to(ctx.out(), "{}:Integer", v.value.as_int32_t);
            case String: return format_to(ctx.out(), "{}:String", v.value.as_string);
            default: return format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
        }
    }
};
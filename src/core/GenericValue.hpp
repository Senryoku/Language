#pragma once

#include <string_view>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>

struct StringView {
    const char* begin;
    uint32_t    size;

    StringView& operator=(const std::string_view& sv) {
        begin = sv.data();
        size = sv.length();
        return *this;
    }
    std::string_view to_std_string_view() const { return std::string_view(begin, size); }
};

union ValueUnion {
    int32_t    as_int32_t;
    StringView as_string; // Not sure if this is the right choice?
    bool       as_bool;
};

struct GenericValue {
    enum class Type
    {
        Integer,
        String,
        Bool,
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
    else if(str == "bool")
        return Bool;
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
            case Integer: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_int32_t);
            case String: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_string);
            case Bool: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_bool ? "True" : "False");
            default: return format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
        }
    }
};

template <>
struct fmt::formatter<GenericValue::Type> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for GenericValue");
        return it;
    }
    template <typename FormatContext>
    auto format(const GenericValue::Type& t, FormatContext& ctx) {
        switch(t) {
            using enum GenericValue::Type;
            case Integer: return format_to(ctx.out(), "{}", "Integer");
            case String: return format_to(ctx.out(), "{}", "String");
            case Bool: return format_to(ctx.out(), "{}", "Boolean");
            default: return format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
        }
    }
};
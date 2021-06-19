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
        size = static_cast<uint32_t>(sv.length());
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
        Boolean,
        Composite,
        Undefined
    };

    GenericValue operator==(const GenericValue& rhs) {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) // TODO: Hanlde implicit conversion?
            return r;
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t == rhs.value.as_int32_t; break;
            case Type::String: assert(false); break; // TODO
            case Type::Boolean: r.value.as_bool = value.as_bool == rhs.value.as_bool; break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator!=(const GenericValue& rhs) {
        // TODO: Handle types
        auto r = (*this == rhs);
        r.value.as_bool = !r.value.as_bool;
        return r;
    }

    GenericValue operator+(const GenericValue& rhs) {
        GenericValue r{.type = Type::Integer};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t + rhs.value.as_int32_t; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator-(const GenericValue& rhs) {
        GenericValue r{.type = Type::Integer};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t - rhs.value.as_int32_t; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator*(const GenericValue& rhs) {
        GenericValue r{.type = Type::Integer};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t * rhs.value.as_int32_t; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator/(const GenericValue& rhs) {
        GenericValue r{.type = Type::Integer};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t / rhs.value.as_int32_t; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator^(const GenericValue& rhs) {
        GenericValue r{.type = Type::Integer};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: {
                auto exp = rhs.value.as_int32_t;
                r.value.as_int32_t = 1;
                while(--exp > 0)
                    r.value.as_int32_t *= value.as_int32_t;
                break;
            }
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    Type       type;
    ValueUnion value;
};

inline static GenericValue::Type parse_type(const std::string_view& str) {
    using enum GenericValue::Type;
    if(str == "int")
        return Integer;
    else if(str == "bool")
        return Boolean;
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
            case Boolean: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_bool ? "True" : "False");
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
            case Boolean: return format_to(ctx.out(), "{}", "Boolean");
            default: return format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
        }
    }
};
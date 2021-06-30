#pragma once

#include <string_view>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>

struct GenericValue {
    enum class Type
    {
        Boolean,
        Integer,
        Float,
        String,
        Array,
        Composite,
        Undefined
    };

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

    union ValueUnion;

    // Fixed-Size array.
    struct Array {
        GenericValue::Type type; // FIXME: Enforce this.
        uint32_t           capacity;
        GenericValue*      items; // FIXME. Also not space eficient, but way easier this way (the type information is useless here, but convenient). The runtime will have the
                                  // responsability to manage this memory.
    };

    union ValueUnion {
        bool       as_bool;
        int32_t    as_int32_t;
        float      as_float;
        StringView as_string; // Not sure if this is the right choice?
        Array      as_array;
    };

    static GenericValue::Type resolve_operator_type(const std::string_view& op, GenericValue::Type lhs, GenericValue::Type rhs) {
        // TODO: Cleanup?
        if(op == "==" || op == "!=" || op == "<" || op == ">" || op == "=>" || op == "<=" || op == "&&" || op == "||")
            return GenericValue::Type::Boolean;
        else if(lhs == GenericValue::Type::Integer && rhs == GenericValue::Type::Integer)
            return GenericValue::Type::Integer;
        else if(lhs == GenericValue::Type::Float && rhs == GenericValue::Type::Float)
            return GenericValue::Type::Float;
        else if((lhs == GenericValue::Type::Integer && rhs == GenericValue::Type::Float) ||
                (lhs == GenericValue::Type::Float && rhs == GenericValue::Type::Integer)) // Promote integer to Float
            return GenericValue::Type::Float;
        else if(lhs == GenericValue::Type::String && rhs == GenericValue::Type::String)
            return GenericValue::Type::String;
        return GenericValue::Type::Undefined;
    }

    // Boolean operators

    GenericValue operator==(const GenericValue& rhs) {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) // TODO: Hanlde implicit conversion?
            return r;
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t == rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_bool = value.as_float == rhs.value.as_float; break;
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

    GenericValue operator<(const GenericValue& rhs) {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) // TODO: Hanlde implicit conversion?
            return r;
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t < rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_bool = value.as_float < rhs.value.as_float; break;
            case Type::String: assert(false); break; // TODO
            case Type::Boolean: r.value.as_bool = value.as_bool == rhs.value.as_bool; break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator>(const GenericValue& rhs) {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) // TODO: Hanlde implicit conversion?
            return r;
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t > rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_bool = value.as_float > rhs.value.as_float; break;
            case Type::String: assert(false); break; // TODO
            case Type::Boolean: r.value.as_bool = value.as_bool == rhs.value.as_bool; break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator&&(const GenericValue& rhs) {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) // TODO: Hanlde implicit conversion?
            return r;
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t != 0 && rhs.value.as_int32_t != 0; break;
            case Type::Float: r.value.as_bool = value.as_float != 0 && rhs.value.as_float != 0; break;
            case Type::String: assert(false); break; // TODO
            case Type::Boolean: r.value.as_bool = value.as_bool && rhs.value.as_bool; break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator||(const GenericValue& rhs) {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) // TODO: Hanlde implicit conversion?
            return r;
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t != 0 || rhs.value.as_int32_t != 0; break;
            case Type::Float: r.value.as_bool = value.as_float != 0 || rhs.value.as_float != 0; break;
            case Type::String: assert(false); break; // TODO
            case Type::Boolean: r.value.as_bool = value.as_bool || rhs.value.as_bool; break;
            default: assert(false); break;
        }
        return r;
    }

    // Arithmetic operators

    GenericValue operator+(const GenericValue& rhs) {
        GenericValue r{.type = resolve_operator_type("+", type, rhs.type)};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t + rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float + rhs.value.as_float; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator-(const GenericValue& rhs) {
        GenericValue r{.type = resolve_operator_type("-", type, rhs.type)};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t - rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float - rhs.value.as_float; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator*(const GenericValue& rhs) {
        GenericValue r{.type = resolve_operator_type("*", type, rhs.type)};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t * rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float * rhs.value.as_float; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator/(const GenericValue& rhs) {
        GenericValue r{.type = resolve_operator_type("/", type, rhs.type)};
        if(type != rhs.type)
            return r;
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t / rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float / rhs.value.as_float; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator^(const GenericValue& rhs) {
        GenericValue r{.type = resolve_operator_type("^", type, rhs.type)};
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
            case Type::Float: {
                auto exp = rhs.value.as_float;
                r.value.as_float = 1;
                while(--exp > 0)
                    r.value.as_float *= value.as_float;
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
    else if(str == "float")
        return Float;
    else if(str == "bool")
        return Boolean;
    else if(str == "string")
        return String;
    return Undefined;
}

template<>
struct fmt::formatter<GenericValue::StringView> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for StringView");
        return it;
    }
    template<typename FormatContext>
    auto format(const GenericValue::StringView& v, FormatContext& ctx) {
        std::string_view str{v.begin, v.begin + v.size};
        return format_to(ctx.out(), "{}", str);
    }
};

template<>
struct fmt::formatter<GenericValue> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for GenericValue");
        return it;
    }
    template<typename FormatContext>
    auto format(const GenericValue& v, FormatContext& ctx) {
        switch(v.type) {
            using enum GenericValue::Type;
            case Integer: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_int32_t);
            case Float: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_float);
            case String: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_string.to_std_string_view());
            case Boolean: return format_to(ctx.out(), "{}:{}", v.type, v.value.as_bool ? "True" : "False");
            case Array: {
                auto r = format_to(ctx.out(), "{}:{}[{}]", v.type, v.value.as_array.type, v.value.as_array.capacity);
                if(v.value.as_array.items) {
                    r = format_to(ctx.out(), " [");
                    for(size_t i = 0; i < v.value.as_array.capacity; ++i)
                        r = format_to(ctx.out(), "{}, ", v.value.as_array.items[i]);
                    r = format_to(ctx.out(), "]");
                }
                return r;
            }
            default: return format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
        }
    }
};

template<>
struct fmt::formatter<GenericValue::Type> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for GenericValue");
        return it;
    }
    template<typename FormatContext>
    auto format(const GenericValue::Type& t, FormatContext& ctx) {
        switch(t) {
            using enum GenericValue::Type;
            case Integer: return format_to(ctx.out(), fg(fmt::color::golden_rod), "{}", "Integer");
            case Float: return format_to(ctx.out(), fg(fmt::color::golden_rod), "{}", "Float");
            case String: return format_to(ctx.out(), fg(fmt::color::burly_wood), "{}", "String");
            case Boolean: return format_to(ctx.out(), fg(fmt::color::royal_blue), "{}", "Boolean");
            case Array: return format_to(ctx.out(), "{}", "Array");
            default: return format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
        }
    }
};
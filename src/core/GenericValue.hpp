#pragma once

#include <cassert>
#include <string_view>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>

#include <Logger.hpp>

using TypeID = uint64_t;

struct GenericValue {
    enum class Type {
        Boolean,
        Integer,
        Float,
        Char,
        String,
        Array,
        Composite,
        Reference,
        Undefined
    };

    enum Flags {
        None = 0,
        Const = 0x1,
        CompileConst = 0x2, // constexpr
    };

    static bool is_numeric(Type t) { return t == Type::Integer || t == Type::Float; }
    static Type common_type(Type t0, Type t1) {
        if(t0 == Type::Float || t1 == Type::Float)
            return Type::Float;
        return Type::Integer;
    }

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

    struct Composite {
        TypeID        type_id;
        GenericValue* members; // FIXME? Managed by the runtime.
        size_t        member_count;
    };

    // Should only be used by the Interpreter
    struct Reference {
        GenericValue* value; // FIXME? Weak pointer to memory managed by the runtime.
                             // FIXME: This forces the Variable pointer to be stable in the interpreter implementation, which is a shame.
    };

    union ValueUnion {
        bool       as_bool;
        int32_t    as_int32_t;
        float      as_float;
        char       as_char;
        StringView as_string; // Not sure if this is the right choice?
        Array      as_array;
        Composite  as_composite;
        Reference  as_reference;
    };

    static GenericValue::Type resolve_operator_type(const std::string_view& op, GenericValue::Type lhs, GenericValue::Type rhs) {
        // TODO: Cleanup?
        if(op == ".")
            return rhs;
        if(op == "=")
            return lhs;
        if(op == "==" || op == "!=" || op == "<" || op == ">" || op == "=>" || op == "<=" || op == "&&" || op == "||")
            return GenericValue::Type::Boolean;
        else if(op == "/") // Special case for division: Always promote to Float.
            return GenericValue::Type::Float;
        else if(lhs == GenericValue::Type::Integer && rhs == GenericValue::Type::Integer)
            return GenericValue::Type::Integer;
        else if(lhs == GenericValue::Type::Float && rhs == GenericValue::Type::Float)
            return GenericValue::Type::Float;
        else if((lhs == GenericValue::Type::Integer && rhs == GenericValue::Type::Float) ||
                (lhs == GenericValue::Type::Float && rhs == GenericValue::Type::Integer)) // Promote integer to Float
            return GenericValue::Type::Float;
        else if(lhs == GenericValue::Type::String && rhs == GenericValue::Type::String)
            return GenericValue::Type::String;
        else if(op == "[" && lhs == GenericValue::Type::String && rhs == GenericValue::Type::Integer)
            return GenericValue::Type::Char;
        else if(op == "[" && lhs == GenericValue::Type::String && rhs == GenericValue::Type::Float) // Auto cast float to integer for string indexing
            return GenericValue::Type::Char;
        return GenericValue::Type::Undefined;
    }

    GenericValue& assign(const GenericValue& rhs) {
        switch(type) {
            case GenericValue::Type::Integer: value.as_int32_t = rhs.to_int32_t(); return *this;
            case GenericValue::Type::Float: value.as_float = rhs.to_float(); return *this;
            case GenericValue::Type::String: {
                if(rhs.type == GenericValue::Type::String) {
                    value.as_string = rhs.value.as_string;
                    return *this;
                }
                break;
            }
            case GenericValue::Type::Composite: {
                assert(value.as_composite.type_id == rhs.value.as_composite.type_id);
                assert(value.as_composite.member_count == rhs.value.as_composite.member_count);
                for(size_t i = 0; i < rhs.value.as_composite.member_count; ++i)
                    value.as_composite.members[i] = rhs.value.as_composite.members[i];
                return *this;
            }
        }
        error("[GenericValue:{}] Error assigning {} to {}.\n", __LINE__, rhs, *this);
        return *this;
    }

    // Boolean operators

    GenericValue operator==(const GenericValue& rhs) const {
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

    GenericValue operator!=(const GenericValue& rhs) const {
        // TODO: Handle types
        auto r = (*this == rhs);
        r.value.as_bool = !r.value.as_bool;
        return r;
    }

    GenericValue operator<(const GenericValue& rhs) const {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                Type t = common_type(type, rhs.type);
                switch(t) {
                    case Type::Integer: r.value.as_bool = to_int32_t() < rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_bool = to_float() < rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t < rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_bool = value.as_float < rhs.value.as_float; break;
            case Type::String: assert(false); break; // TODO
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator<=(const GenericValue& rhs) const {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                Type t = common_type(type, rhs.type);
                switch(t) {
                    case Type::Integer: r.value.as_bool = to_int32_t() <= rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_bool = to_float() <= rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t <= rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_bool = value.as_float <= rhs.value.as_float; break;
            case Type::String: assert(false); break; // TODO
            case Type::Boolean: r.value.as_bool = value.as_bool == rhs.value.as_bool; break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator>(const GenericValue& rhs) const {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                Type t = common_type(type, rhs.type);
                switch(t) {
                    case Type::Integer: r.value.as_bool = to_int32_t() > rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_bool = to_float() > rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t > rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_bool = value.as_float > rhs.value.as_float; break;
            case Type::String: assert(false); break; // TODO
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator>=(const GenericValue& rhs) const {
        GenericValue r{.type = Type::Boolean};
        r.value.as_bool = false;
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                Type t = common_type(type, rhs.type);
                switch(t) {
                    case Type::Integer: r.value.as_bool = to_int32_t() >= rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_bool = to_float() >= rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_bool = value.as_int32_t >= rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_bool = value.as_float >= rhs.value.as_float; break;
            case Type::String: assert(false); break; // TODO
            case Type::Boolean: r.value.as_bool = value.as_bool == rhs.value.as_bool; break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator&&(const GenericValue& rhs) const {
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

    GenericValue operator||(const GenericValue& rhs) const {
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

    GenericValue operator+() const {
        assert(is_numeric(type));
        return *this;
    }
    GenericValue operator-() const {
        assert(is_numeric(type));
        GenericValue r = *this;
        switch(r.type) {
            case Type::Integer: r.value.as_int32_t = -r.value.as_int32_t; break;
            case Type::Float: r.value.as_float = -r.value.as_float; break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator+(const GenericValue& rhs) const {
        GenericValue r{.type = resolve_operator_type("+", type, rhs.type)};
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                switch(r.type) {
                    case Type::Integer: r.value.as_int32_t = to_int32_t() + rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_float = to_float() + rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t + rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float + rhs.value.as_float; break;
            case Type::String: assert(false); break;  // TODO
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator-(const GenericValue& rhs) const {
        GenericValue r{.type = resolve_operator_type("-", type, rhs.type)};
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                switch(r.type) {
                    case Type::Integer: r.value.as_int32_t = to_int32_t() - rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_float = to_float() - rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t - rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float - rhs.value.as_float; break;
            case Type::String: assert(false); break;  // Invalid
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator*(const GenericValue& rhs) const {
        GenericValue r{.type = resolve_operator_type("*", type, rhs.type)};
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                switch(r.type) {
                    case Type::Integer: r.value.as_int32_t = to_int32_t() * rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_float = to_float() * rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t * rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float * rhs.value.as_float; break;
            case Type::String: assert(false); break;  // Invalid
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator/(const GenericValue& rhs) const {
        GenericValue r{.type = resolve_operator_type("/", type, rhs.type)};
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                switch(r.type) {
                    case Type::Integer: r.value.as_int32_t = to_int32_t() / rhs.to_int32_t(); break;
                    case Type::Float: r.value.as_float = to_float() / rhs.to_float(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t / rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = value.as_float / rhs.value.as_float; break;
            case Type::String: assert(false); break;  // Invalid
            case Type::Boolean: assert(false); break; // Invalid
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator%(const GenericValue& rhs) const {
        GenericValue r{.type = resolve_operator_type("%", type, rhs.type)};
        if(type != rhs.type) { // TODO: Hanlde implicit conversion?
            if(is_numeric(type) && is_numeric(rhs.type)) {
                switch(r.type) {
                    case Type::Integer: r.value.as_int32_t = to_int32_t() % rhs.to_int32_t(); break;
                    default: assert(false); break;
                }
            }
            return r; // We don't know what to do! (yet?)
        }
        switch(type) {
            case Type::Integer: r.value.as_int32_t = value.as_int32_t % rhs.value.as_int32_t; break;
            case Type::Float: r.value.as_float = std::fmod(value.as_float, rhs.value.as_float); break;
            default: assert(false); break;
        }
        return r;
    }

    GenericValue operator^(const GenericValue& rhs) const {
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

    // Conversion utilities
    float to_float() const {
        if(type == Type::Integer)
            return static_cast<float>(value.as_int32_t);
        if(type == Type::Float)
            return value.as_float;
        assert(false);
        return value.as_float;
    }

    int32_t to_int32_t() const {
        if(type == Type::Integer)
            return value.as_int32_t;
        if(type == Type::Float)
            return static_cast<int32_t>(value.as_float);
        assert(false);
        return value.as_int32_t;
    }

    bool is_const() const { return flags & Flags::Const; }
    bool is_constexpr() const { return flags & Flags::CompileConst; }

    inline static Type parse_type(const std::string_view& str) {
        using enum Type;
        if(str == "int")
            return Integer;
        else if(str == "float")
            return Float;
        else if(str == "bool")
            return Boolean;
        else if(str == "char")
            return Char;
        else if(str == "string")
            return String;
        return Undefined;
    }

    Type       type;
    Flags      flags;
    ValueUnion value;
};

inline GenericValue::Flags operator|(GenericValue::Flags a, GenericValue::Flags b) {
    return static_cast<GenericValue::Flags>(static_cast<int>(a) | static_cast<int>(b));
}

template<>
struct fmt::formatter<GenericValue::StringView> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for StringView");
        return it;
    }
    template<typename FormatContext>
    auto format(const GenericValue::StringView& v, FormatContext& ctx) -> decltype(ctx.out()) {
        std::string_view str{v.begin, v.begin + v.size};
        return format_to(ctx.out(), "{}", str);
    }
};

template<>
struct fmt::formatter<GenericValue::Type> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for GenericValue");
        return it;
    }
    template<typename FormatContext>
    auto format(const GenericValue::Type& t, FormatContext& ctx) -> decltype(ctx.out()) {
        switch(t) {
            using enum GenericValue::Type;
            case Integer: return fmt::format_to(ctx.out(), fg(fmt::color::golden_rod), "{}", "Integer");
            case Float: return fmt::format_to(ctx.out(), fg(fmt::color::golden_rod), "{}", "Float");
            case Char: return fmt::format_to(ctx.out(), fg(fmt::color::burly_wood), "{}", "Char");
            case String: return fmt::format_to(ctx.out(), fg(fmt::color::burly_wood), "{}", "String");
            case Boolean: return fmt::format_to(ctx.out(), fg(fmt::color::royal_blue), "{}", "Boolean");
            case Array: return fmt::format_to(ctx.out(), "{}", "Array");
            case Composite: return fmt::format_to(ctx.out(), fg(fmt::color::light_green), "{}", "Composite");
            case Reference: return fmt::format_to(ctx.out(), fg(fmt::color::blue), "{}", "Reference");
            case Undefined: return fmt::format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
            default: return fmt::format_to(ctx.out(), fg(fmt::color::red), "{}: {}", "Unknown Generic Value Type [by the formatter]", static_cast<int>(t));
        }
    }
};

template<>
struct fmt::formatter<GenericValue> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for GenericValue");
        return it;
    }
    template<typename FormatContext>
    auto format(const GenericValue& v, FormatContext& ctx) const -> decltype(ctx.out()) {
        switch(v.type) {
            using enum GenericValue::Type;
            case Integer: return fmt::format_to(ctx.out(), "{}:{}", v.type, v.value.as_int32_t);
            case Float: return fmt::format_to(ctx.out(), "{}:{}", v.type, v.value.as_float);
            case Char: return fmt::format_to(ctx.out(), "{}:{}", v.type, v.value.as_char);
            case String: return fmt::format_to(ctx.out(), "{}:{}", v.type, v.value.as_string.to_std_string_view());
            case Boolean: return fmt::format_to(ctx.out(), "{}:{}", v.type, v.value.as_bool ? "True" : "False");
            case Array: {
                auto r = fmt::format_to(ctx.out(), "{}:{}[{}]", v.type, v.value.as_array.type, v.value.as_array.capacity);
                if(v.value.as_array.items) {
                    r = fmt::format_to(ctx.out(), " [");
                    for(size_t i = 0; i < v.value.as_array.capacity; ++i)
                        r = fmt::format_to(ctx.out(), "{}, ", v.value.as_array.items[i]);
                    r = fmt::format_to(ctx.out(), "]");
                }
                return r;
            }
            case Composite: return fmt::format_to(ctx.out(), "{}", v.type);                                         // TODO
            case Reference: return fmt::format_to(ctx.out(), "{} to {}", v.type, v.value.as_reference.value->type); // TODO
            case Undefined: return fmt::format_to(ctx.out(), fg(fmt::color::gray), "{}", "Undefined");
            default: return fmt::format_to(ctx.out(), fg(fmt::color::red), "Generic Value of type {}", v.type);
        }
    }
};

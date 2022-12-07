#pragma once

#include <cassert>

#include <PrimitiveType.hpp>

struct ValueType {
    bool is_primitive() const { return !is_array && primitive != PrimitiveType::Composite; }
    bool is_composite() const { return primitive == PrimitiveType::Composite; }
    bool is_undefined() const { return primitive == PrimitiveType::Undefined; }

    PrimitiveType primitive = PrimitiveType::Undefined;
    bool          is_array = false;
    size_t        capacity = 0;
    bool          is_reference = false; // FIXME: Do we need both?
    bool          is_pointer = false;

    TypeID type_id = InvalidTypeID;

    bool operator==(const ValueType&) const = default;

    ValueType get_element_type() const {
        auto copy = *this;
        copy.is_array = false; // FIXME: This is obviously wrong.
        return copy;
    }

    ValueType get_pointed_type() const { // FIXME: Also wrong, what about int**?
        assert(is_reference || is_pointer);
        auto copy = *this;
        copy.is_reference = false;
        copy.is_pointer = false;
        return copy;
    }

    std::string serialize() const;
    static ValueType   parse(const std::string& str);

    static ValueType integer() { return ValueType{.primitive = PrimitiveType::Integer}; }
    static ValueType floating_point() { return ValueType{.primitive = PrimitiveType::Float}; }
    static ValueType character() { return ValueType{.primitive = PrimitiveType::Char}; }
    static ValueType boolean() { return ValueType{.primitive = PrimitiveType::Boolean}; }
    static ValueType string() { return ValueType{.primitive = PrimitiveType::String}; }
    static ValueType void_t() { return ValueType{.primitive = PrimitiveType::Void}; }
    static ValueType undefined() { return ValueType{.primitive = PrimitiveType::Undefined}; }
};

struct TypeMember {
    std::string_view name;
    ValueType        type;
};

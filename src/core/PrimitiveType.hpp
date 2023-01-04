#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

using TypeID = uint64_t;
constexpr TypeID InvalidTypeID = static_cast<TypeID>(-1);

enum PrimitiveType : TypeID {
    Void,
    Char,
    Boolean,
    U8,
    U16,
    U32,
    U64,
    I8,
    I16,
    I32,
    I64,
    Integer, // Same as I32
    Pointer, // Same as U64, not sure if it's useful.
    Float,
    Double,
    CString,

    Count, // Max. TypeID for Primitive Types
};

constexpr uint64_t MaxPlaceholderTypes = 1024;

constexpr TypeID PlaceholderTypeID_Min = PrimitiveType::Count + 1;
constexpr TypeID PlaceholderTypeID_Max = PrimitiveType::Count + 1 + MaxPlaceholderTypes;

inline bool is_primitive(TypeID type_id) {
    return type_id < PrimitiveType::Count;
}

inline bool is_placeholder(TypeID type_id) {
    return type_id >= PlaceholderTypeID_Min && type_id < PlaceholderTypeID_Max;
}

inline uint64_t get_placeholder_index(TypeID type_id) {
    assert(is_placeholder(type_id));
    return type_id - PlaceholderTypeID_Min;
}

inline bool is_integer(TypeID type_id) {
    return type_id >= U8 && type_id <= Integer;
}

inline bool is_unsigned(TypeID type_id) {
    return type_id >= U8 && type_id <= U64;
}

inline bool is_floating_point(TypeID type_id) {
    return type_id >= Float && type_id <= Double;
}

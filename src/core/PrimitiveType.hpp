#pragma once

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
    String, // Remove?

    Count, // Max. TypeID for Primitive Types
};

inline bool is_primitive(TypeID type_id) {
    return type_id < PrimitiveType::Count;
}

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

using TypeID = uint64_t;
constexpr TypeID InvalidTypeID = static_cast<TypeID>(-1);

enum class PrimitiveType {
    Integer,
    Float,
    Char,
    Boolean,
    String,
    Void,
    Composite,
    Undefined
};

std::string   serialize(PrimitiveType type);
PrimitiveType parse_primitive_type(const std::string_view& str);

#include <PrimitiveType.hpp>

#include <cassert>

std::string serialize(PrimitiveType type) {
    switch(type) {
        using enum PrimitiveType;
        case Integer: return "int";
        case Float: return "float";
        case Char: return "char";
        case Boolean: return "bool";
        case String: return "string";
        case Void: return "void";
        default: assert(false);
    }
    return "";
}

PrimitiveType parse_primitive_type(const std::string_view& str) {
    using enum PrimitiveType;
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
    else if(str == "void")
        return Void;
    return Undefined;
}
#pragma once

#include <string>
#include <string_view>

#include <PrimitiveType.hpp>

inline constexpr std::string_view INVALID_TYPE_ID_STR = "InvalidTypeID";

std::string serialize_type_id(TypeID type_id);
std::string type_id_to_string(TypeID type_id);

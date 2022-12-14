#include "TypeIDFormat.hpp"

#include <fmt/core.h>
#include <fmt/color.h>

#include <GlobalTypeRegistry.hpp>

static const std::string _INVALID_TYPE_ID_STR_(fmt::format(fg(fmt::color::gray), "InvalidTypeID"));

std::string type_id_to_string(TypeID type_id) {
    if(type_id == InvalidTypeID)
        return _INVALID_TYPE_ID_STR_;

    auto type_name = GlobalTypeRegistry::instance().get_type(type_id)->designation;
    if(type_id < PrimitiveType::Count) {
        auto out_color = fmt::color::red;
        switch(type_id) {
            using enum PrimitiveType;
            case Void: out_color = fmt::color::gray; break;
            case Char: out_color = fmt::color::burly_wood; break;
            case Boolean: out_color = fmt::color::royal_blue; break;
            case U8: [[fallthrough]];
            case U16: [[fallthrough]];
            case U32: [[fallthrough]];
            case U64: [[fallthrough]];
            case I8: [[fallthrough]];
            case I16: [[fallthrough]];
            case I32: [[fallthrough]];
            case I64: [[fallthrough]];
            case Integer: out_color = fmt::color::golden_rod; break;
            case Float: out_color = fmt::color::golden_rod; break;
            case Double: out_color = fmt::color::golden_rod; break;
            case CString: out_color = fmt::color::burly_wood; break;
        }
        return fmt::format(fg(out_color), "{}", type_name);
    }
    return type_name;
}
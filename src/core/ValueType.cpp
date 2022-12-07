#include <ValueType.hpp>

#include <GlobalTypeRegistry.hpp>

std::string ValueType::serialize() const {
    std::string r;
    if(is_primitive())
        r = ::serialize(primitive);
    else r = std::string(GlobalTypeRegistry::instance().get_type(type_id)->token.value);

    // FIXME: Really serialize this thing (But we need a type system and a format first.)
    if(is_pointer)
        r += "*";

    return r;
}

ValueType ValueType::parse(const std::string& str) {
    ValueType r;

    // FIXME: Really parse this thing (But we need a type system and a format first.)
    auto is_pointer = str.ends_with('*');

    auto type_str = is_pointer ? std::string_view(str.begin(), str.begin() + str.size() - 1) : str;

    r.primitive = parse_primitive_type(type_str);
    if(r.primitive == PrimitiveType::Undefined)
        r = GlobalTypeRegistry::instance().get_type(std::string(type_str))->value_type;

    r.is_pointer = is_pointer;

    return r;
}

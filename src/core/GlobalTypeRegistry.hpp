#pragma once

#include <AST.hpp>

class GlobalTypeRegistry {
  public:
    AST::TypeDeclaration* get_type(const std::string& name);

    AST::TypeDeclaration* get_type(TypeID id) {
        assert(id != InvalidTypeID);
        return _types[id];
    }

    TypeID next_id() const { return _types.size(); }

    TypeID register_type(AST::TypeDeclaration& type_node);

    inline static GlobalTypeRegistry& instance() {
        static GlobalTypeRegistry gtr;
        return gtr;
    }

  private:
    std::vector<AST::TypeDeclaration*> _types;
};

template<>
struct fmt::formatter<ValueType> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for ValueType");
        return it;
    }

    template<typename FormatContext>
    auto format(const ValueType& t, FormatContext& ctx) -> decltype(ctx.out()) {
        if(t.is_primitive())
            return fmt::format_to(ctx.out(), "{}{}", t.primitive, t.is_pointer ? "*" : "");
        if(t.is_array)
            return fmt::format_to(ctx.out(), "{}[{}]", t.primitive, t.capacity);
        // TODO: Handle Ref/Pointers
        if(t.type_id == InvalidTypeID)
            return fmt::format_to(ctx.out(), fg(fmt::color::red), "{}", "Non-Primitive ValueType With Invalid TypeID");
        return fmt::format_to(ctx.out(), fg(fmt::color::dark_sea_green), "{}{}", GlobalTypeRegistry::instance().get_type(t.type_id)->token.value, t.is_pointer ? "*" : "");
    }
};

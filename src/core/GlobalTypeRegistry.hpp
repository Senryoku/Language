#pragma once

#include <tuple>
#include <unordered_map>

#include <AST.hpp>
#include <ValueType.hpp>

// Really simple Hash for std::vector<TypeID>, to use in cache
static TypeID hash(const std::vector<TypeID>& v) {
    static std::array<size_t, 10> primes = {3, 5, 7, 11, 13, 17, 19, 23, 29, 31};
    uint64_t                      hash = v[0];
    for(size_t n = 1; n < std::min(primes.size(), v.size()); ++n)
        hash += (primes[n] * v[n]);
    return hash;
}
typedef std::tuple<TypeID, size_t>              array_cache_key_t;
typedef std::tuple<TypeID, std::vector<TypeID>> template_cache_key_t;

struct array_key_hash {
    std::size_t operator()(const array_cache_key_t& k) const { return std::get<0>(k) ^ std::get<1>(k); }
};
struct template_key_hash {
    std::size_t operator()(const template_cache_key_t& k) const { return std::get<0>(k) ^ hash(std::get<1>(k)); }
};

class GlobalTypeRegistry {
  public:
    const Type* get_type(TypeID id) const;
    const Type* get_type(const std::string& name) const;
    TypeID      get_type_id(const std::string& name) const;

    const Type* get_or_register_type(const std::string& name);

    TypeID get_pointer_to(TypeID id);
    TypeID get_array_of(TypeID id, uint32_t capacity);
    TypeID get_specialized_type(TypeID id, const std::vector<TypeID>& parameters);
    bool   specialized_type_exists(TypeID id, const std::vector<TypeID>& parameters) { return _specialized_types.contains({id, parameters}); }

    TypeID register_type(AST::TypeDeclaration& type_node);

    inline static GlobalTypeRegistry& instance() {
        static GlobalTypeRegistry gtr;
        return gtr;
    }

  private:
    std::vector<std::unique_ptr<Type>> _types;
    // Cache Lookup
    std::unordered_map<std::string, TypeID>                                   _types_by_designation;
    std::unordered_map<TypeID, TypeID>                                        _pointers_to;
    std::unordered_map<const array_cache_key_t, TypeID, array_key_hash>       _arrays_of;
    std::unordered_map<const template_cache_key_t, TypeID, template_key_hash> _specialized_types;

    void update_caches(Type* t) {
        _types_by_designation[t->designation] = t->type_id;
        if(t->is_pointer())
            _pointers_to[dynamic_cast<PointerType*>(t)->pointee_type] = t->type_id;
        if(t->is_array()) {
            auto array_type = dynamic_cast<ArrayType*>(t);
            _arrays_of[std::make_tuple(array_type->element_type, array_type->capacity)] = t->type_id;
        }
        if(t->is_templated()) {
            auto templated_type = dynamic_cast<TemplatedType*>(t);
            _specialized_types[std::make_tuple(templated_type->template_type_id, templated_type->parameters)] = t->type_id;
        }
    }

    void add_type(Type* t) {
        if(t->type_id < next_id())
            _types.emplace(_types.begin() + t->type_id, t);
        else {
            while(t->type_id > next_id())
                _types.emplace_back(nullptr); // Padding
            _types.emplace_back(t);
        }
        update_caches(t);
    }

    TypeID next_id() const { return _types.size(); }

    GlobalTypeRegistry() {
        _types.reserve(2 * PrimitiveType::Count);

        add_type(new ScalarType("void", PrimitiveType::Void));
        add_type(new ScalarType("char", PrimitiveType::Char));
        add_type(new ScalarType("bool", PrimitiveType::Boolean));
        add_type(new ScalarType("u8", PrimitiveType::U8));
        add_type(new ScalarType("u16", PrimitiveType::U16));
        add_type(new ScalarType("u32", PrimitiveType::U32));
        add_type(new ScalarType("u64", PrimitiveType::U64));
        add_type(new ScalarType("i8", PrimitiveType::I8));
        add_type(new ScalarType("i16", PrimitiveType::I16));
        add_type(new ScalarType("i32", PrimitiveType::I32));
        add_type(new ScalarType("i64", PrimitiveType::I64));
        add_type(new ScalarType("int", PrimitiveType::Integer));
        add_type(new ScalarType("pointer", PrimitiveType::Pointer));
        add_type(new ScalarType("float", PrimitiveType::Float));
        add_type(new ScalarType("double", PrimitiveType::Double));
        add_type(new PointerType("cstr", PrimitiveType::CString, PrimitiveType::Char));

        for(auto i = 0; i < MaxPlaceholderTypes; ++i)
            add_type(new PlaceholderType(fmt::format("__placeholder_{}", i), PlaceholderTypeID_Min + i));
    }
};

template<>
struct fmt::formatter<Type> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for ValueType");
        return it;
    }

    template<typename FormatContext>
    auto format(const Type& t, FormatContext& ctx) -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", t.designation);
    }
};

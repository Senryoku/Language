#pragma once

#include <tuple>
#include <unordered_map>

#include <AST.hpp>

typedef std::tuple<TypeID, size_t> key_t;

struct key_hash {
    std::size_t operator()(const key_t& k) const { return std::get<0>(k) ^ std::get<1>(k); }
};

class TypeRecord {
  public:
    TypeRecord() = default;
    TypeRecord(Type* t, AST::TypeDeclaration* t_node = nullptr) : type(t), type_node(t_node) {}
    TypeRecord(TypeRecord&&) = default;
    TypeRecord& operator = (TypeRecord &&) = default;

    std::unique_ptr<Type> type;
    AST::TypeDeclaration* type_node = nullptr;
};

class GlobalTypeRegistry {
  public:
    const TypeRecord& get_type(TypeID id) const;
    const TypeRecord& get_type(const std::string& name) const;

    const TypeRecord& get_or_register_type(const std::string& name);

    TypeID get_pointer_to(TypeID id);
    TypeID get_array_of(TypeID id, uint32_t capacity);

    TypeID register_type(AST::TypeDeclaration& type_node);

    inline static GlobalTypeRegistry& instance() {
        static GlobalTypeRegistry gtr;
        return gtr;
    }

  private:
    std::vector<TypeRecord> _types;
    // Cache Lookup
    std::unordered_map<std::string, TypeID> _types_by_designation;
    std::unordered_map<TypeID, TypeID>      _pointers_to;
    std::unordered_map<const key_t, TypeID, key_hash> _arrays_of;

    void update_caches(Type* t) {
        _types_by_designation[t->designation] = t->type_id;
        if(t->is_pointer())
            _pointers_to[dynamic_cast<PointerType*>(t)->pointee_type] = t->type_id;
        if(t->is_array()) {
            auto array_type = dynamic_cast<ArrayType*>(t);
            _arrays_of[std::make_tuple(array_type->element_type, array_type->capacity)] = t->type_id;
        }
    }

    void add_type(Type* t) {
        if(t->type_id < next_id())
            _types.emplace(_types.begin() + t->type_id, t);
        else {
            assert(t->type_id == next_id());
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
        add_type(new ScalarType("u8",   PrimitiveType::U8));
        add_type(new ScalarType("u16",  PrimitiveType::U16));
        add_type(new ScalarType("u32",  PrimitiveType::U32));
        add_type(new ScalarType("u64",  PrimitiveType::U64));
        add_type(new ScalarType("i8",   PrimitiveType::I8));
        add_type(new ScalarType("i16",  PrimitiveType::I16));
        add_type(new ScalarType("i32",  PrimitiveType::I32));
        add_type(new ScalarType("i64",  PrimitiveType::I64));
        add_type(new ScalarType("int",  PrimitiveType::Integer));
        add_type(new ScalarType("pointer", PrimitiveType::Pointer));
        add_type(new ScalarType("float",   PrimitiveType::Float));
        add_type(new ScalarType("double",  PrimitiveType::Double));
        add_type(new ScalarType("string",  PrimitiveType::String));
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

#include <formatters/ASTFormat.hpp>

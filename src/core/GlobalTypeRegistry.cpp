#include <GlobalTypeRegistry.hpp>

#include <fmt/core.h>

#include <Exception.hpp>
#include <AST.hpp>

const Type* GlobalTypeRegistry::get_type(TypeID id) const {
    assert(id != InvalidTypeID);
    return _types[id].get();
}


const Type* GlobalTypeRegistry::get_type(const std::string& name) const {
    return get_type(_types_by_designation.at(name));
}

const Type* GlobalTypeRegistry::get_or_register_type(const std::string& name) {
    if(_types_by_designation.contains(name))
        return get_type(name);
    // Try to register unknown pointer to existing type.
    if (name.ends_with("*")) {
        const auto& base_type = get_or_register_type(name.substr(0, name.size() - 1));
        return get_type(get_pointer_to(base_type->type_id));
    }
    throw fmt::format("[GlobalTypeRegistry::get_or_register_type] Unknown type {}.", name);
}

TypeID GlobalTypeRegistry::get_pointer_to(TypeID id) {
    if(_pointers_to.contains(id))
        return _pointers_to.at(id);
    auto nid = next_id();
    add_type(new PointerType(get_type(id)->designation + "*", nid, id));
    return nid;
}

TypeID GlobalTypeRegistry::get_array_of(TypeID id, uint32_t capacity) {
    if(_arrays_of.contains({id, capacity}))
        return _arrays_of.at({id, capacity});
    auto nid = next_id();
    add_type(new ArrayType(get_type(id)->designation + "[" + std::to_string(capacity) + "]", nid, id, capacity));
    return nid;
}

TypeID GlobalTypeRegistry::register_type(AST::TypeDeclaration& type_node) {
    if(type_node.type_id != InvalidTypeID) {
        warn("[GlobalTypeRegistry] Note: Type '{}' is already registered (type_id already set).\n", type_node.token.value);
        return type_node.type_id;
    }

    // FIXME: We shouldn't have to do this.
    //        Since this is shared by all translation units, compiling a dependency with a type export
    //        will result in registering the type twice: Once when compiling the dependency and once
    //        when parsing the type from the dependency's interface, resulting in two different type_id
    //        for the same type.
    //        For now, will skip the registration if the name matches an already registered type, 
    //        but this slow and error-prone, if not completly wrong.
    for(const auto& record : _types) {
        if(type_node.token.value == record->designation) {
            warn("[GlobalTypeRegistry] Note: A type with the name '{}' is already registered. FIXME: This should be an error, but is currently necessary because of our poor type import implementation.\n", type_node.token.value);
            type_node.type_id = record->type_id;
            return record->type_id;
        }
    }

    std::string type_designation(type_node.token.value);
    StructType* tr = dynamic_cast<StructType*>(_types.emplace_back(new StructType{type_designation, next_id()}).get());
    uint32_t    index = 0;
    for (const auto child : type_node.children) {
        StructType::Member member = {.name = std::string(child->token.value), .index = index, .type_id = child->type_id};
        tr->members[std::string(child->token.value)] = member;
        ++index;
    }
    update_caches(tr);

    return tr->type_id;
}
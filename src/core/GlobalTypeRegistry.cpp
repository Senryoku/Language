#include <GlobalTypeRegistry.hpp>

#include <fmt/core.h>

#include <Exception.hpp>
#include <AST.hpp>

AST::TypeDeclaration* GlobalTypeRegistry::get_type(const std::string& name) {
    for(auto node : _types) {
        if(node->token.value == name) {
            return node;
        }
    }
    throw Exception(fmt::format("[GlobalTypeRegistry] Unknown type: {}.\n", name));
}


TypeID GlobalTypeRegistry::register_type(AST::TypeDeclaration& type_node) {
    if(type_node.type_id() != InvalidTypeID) {
        warn("[GlobalTypeRegistry] Note: Type '{}' is already registered (type_id already set).\n", type_node.token.value);
        return type_node.type_id();
    }

    // FIXME: We shouldn't have to do this.
    //        Since this is shared by all translation units, compiling a dependency with a type export
    //        will result in registering the type twice: Once when compiling the dependency and once
    //        when parsing the type from the dependency's interface, resulting in two different type_id
    //        for the same type.
    //        For now, will skip the registration if the name matches an already registered type, 
    //        but this slow and error-prone, if not completly wrong.
    for(auto node : _types) {
        if(type_node.token.value == node->token.value) {
            warn("[GlobalTypeRegistry] Note: A type with the name '{}' is already registered. FIXME: This should be an error, but is currently necessary because of our poor type import implementation.\n", type_node.token.value);
            type_node.value_type.primitive = PrimitiveType::Composite;
            type_node.value_type.type_id = node->type_id();
            return node->type_id();
        }
    }


    auto id = next_id();
    _types.push_back(&type_node);
    type_node.value_type.primitive = PrimitiveType::Composite;
    type_node.value_type.type_id = id;
    return id;
}
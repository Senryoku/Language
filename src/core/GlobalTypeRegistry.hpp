#pragma once

#include <AST.hpp>

class GlobalTypeRegistry {
  public:
    AST::TypeDeclaration* get_type(TypeID id) {
        assert(id != InvalidTypeID);
        return _types[id];
    }

    TypeID next_id() const { return _types.size(); }

    TypeID register_type(AST::TypeDeclaration& type_node) {
        auto id = next_id();
        _types.push_back(&type_node);
        type_node.value_type.primitive = PrimitiveType::Composite;
        type_node.value_type.type_id = id;
        return id;
    }

    inline static GlobalTypeRegistry& instance() {
        static GlobalTypeRegistry gtr;
        return gtr;
    }

  private:
    std::vector<AST::TypeDeclaration*> _types;
};

#include <AST.hpp>

AST::Node* AST::Node::add_child(Node* n) {
    assert(n->parent == nullptr);
    children.push_back(n);
    n->parent = this;
    return n;
}

AST::Node* AST::Node::pop_child() {
    AST::Node* c = children.back();
    children.pop_back();
    c->parent = nullptr;
    return c;
}

// Insert a node between this and its nth child
AST::Node* AST::Node::insert_between(size_t n, Node* node) {
    assert(node->children.size() == 0);
    children[n]->parent = nullptr;
    node->add_child(children[n]);
    children[n] = node;
    return node;
}

std::string serialize(PrimitiveType type) {
    switch(type) {
        using enum PrimitiveType;
        case Integer: return "int";
        case Float: return "float";
        case Char: return "char";
        case Boolean: return "bool";
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
    return Undefined;
}

std::string ValueType::serialize() const {
    if(is_primitive())
        return ::serialize(primitive);

    assert(false && "TODO: Handle Non-Primitive types.");
    // GlobalTypeRegistry::instance().get_type(type.type_id);
    return "";
}

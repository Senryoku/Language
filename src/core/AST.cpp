#include <AST.hpp>

#include <GlobalTypeRegistry.hpp>

AST::Node* AST::Node::add_child(Node* n) {
    assert(n->parent == nullptr);
    children.push_back(n);
    n->parent = this;
    return n;
}

AST::Node* AST::Node::add_child_front(Node* n) {
    assert(n->parent == nullptr);
    children.insert(children.begin(), n);
    n->parent = this;
    return n;
}

AST::Node* AST::Node::add_child_after(Node* n, const Node* prev) {
    assert(n->parent == nullptr);
    children.insert(++std::find(children.begin(), children.end(), prev), n);
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

auto mangle_name(const std::string_view& name, auto arguments, AST::FunctionDeclaration::Flag flags) {
    std::string r{name};
    // FIXME: Correctly handle manged names for variadic functions.
    if((flags & AST::FunctionDeclaration::Flag::Variadic) || (flags & AST::FunctionDeclaration::Flag::Extern) || (flags & AST::FunctionDeclaration::Flag::BuiltIn))
        return r;
    for(auto arg : arguments)
        r += std::string("_") + GlobalTypeRegistry::instance().get_type(arg->type_id)->designation;
    return r;
}

std::string AST::FunctionDeclaration::mangled_name() const {
    return mangle_name(token.value, arguments(), flags);
}

bool AST::FunctionDeclaration::is_templated() const {
    if(GlobalTypeRegistry::instance().get_type(type_id)->is_placeholder())
        return true;
    for(const auto& arg : arguments())
        if(GlobalTypeRegistry::instance().get_type(arg->type_id)->is_placeholder())
            return true;
    return false;
}

std::string AST::FunctionCall::mangled_name() const {
    return mangle_name(token.value, arguments(), flags);
}

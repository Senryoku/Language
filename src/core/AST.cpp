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

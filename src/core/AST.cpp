#include <AST.hpp>

#include <Interpreter.hpp>

void AST::optimize() {
    _root = std::move(*optimize(&_root));
}

bool compatible(GenericValue::Type lhs, GenericValue::Type rhs) {
    return lhs == rhs; // TODO
}

// Todo: Apply repeatedly until there's no changes?
AST::Node* AST::optimize(AST::Node* currNode) {
    for(size_t i = 0; i < currNode->children.size(); ++i)
        currNode->children[i] = optimize(currNode->children[i]);

    // Remove trivial expression nodes (FIXME: Figure out if we really want to allow an expression with multiple children...)
    if(currNode->type == AST::Node::Type::Expression && currNode->children.size() == 1) {
        auto child = currNode->children[0];
        currNode->children.clear();
        delete currNode;
        currNode = child;
    }

    // Compute const expression when possible
    if(currNode->type == AST::Node::Type::BinaryOperator && currNode->children.size() == 2) {
        auto lhs = currNode->children[0];
        auto rhs = currNode->children[1];
        if(lhs->type == AST::Node::Type::ConstantValue && rhs->type == AST::Node::Type::ConstantValue && compatible(lhs->value.type, rhs->value.type)) {
            assert(lhs->children.size() == 0);
            assert(rhs->children.size() == 0);

            Interpreter interp;
            interp.execute(*currNode);
            currNode->children.clear();
            delete currNode;
            currNode = new AST::Node(AST::Node::Type::ConstantValue);
            currNode->value = interp.get_return_value();
            delete lhs;
            delete rhs;
        }
    }

    return currNode;
}
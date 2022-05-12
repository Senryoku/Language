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

    // Remove trivial expression or statement nodes (FIXME: Figure out if we really want to allow an expression with multiple children...)
    if((currNode->type == AST::Node::Type::Expression || currNode->type == AST::Node::Type::Statement) && currNode->children.size() == 1) {
        auto child = currNode->children[0];
        currNode->children.clear();
        delete currNode;
        currNode = child;
    }

    auto execute_node = [&]() {
        Interpreter interp;
        interp.execute(*currNode);
        delete currNode;
        currNode = new AST::Node(AST::Node::Type::ConstantValue);
        currNode->value = interp.get_return_value();
    };

    // Compute const expression when possible
    if(currNode->type == AST::Node::Type::UnaryOperator && currNode->children.size() == 1) {
        auto child = currNode->children[0];
        if(child->type == AST::Node::Type::ConstantValue) {
            assert(child->children.size() == 0);
            execute_node();
        }
    }

    if(currNode->type == AST::Node::Type::BinaryOperator && currNode->children.size() == 2) {
        auto lhs = currNode->children[0];
        auto rhs = currNode->children[1];
        if(lhs->type == AST::Node::Type::ConstantValue && rhs->type == AST::Node::Type::ConstantValue && compatible(lhs->value.type, rhs->value.type)) {
            assert(lhs->children.size() == 0);
            assert(rhs->children.size() == 0);
            execute_node();
        }
    }

    return currNode;
}

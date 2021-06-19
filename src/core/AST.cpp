#include <AST.hpp>

void AST::optimize() {
    _root = std::move(*optimize(&_root));
}

bool compatible(GenericValue::Type lhs, GenericValue::Type rhs) {
    return lhs == rhs; // TODO
}

// Todo: Apply repeatedly until there's no changes.
AST::Node* AST::optimize(AST::Node* currNode) {
    for(size_t i = 0; i < currNode->children.size(); ++i)
        currNode->children[i] = optimize(currNode->children[i]);

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

            if(lhs->value.type == GenericValue::Type::Integer && rhs->value.type == GenericValue::Type::Integer) {
                auto apply = [&](GenericValue res) {
                    currNode->children.clear();
                    delete currNode;
                    currNode = new AST::Node(AST::Node::Type::ConstantValue);
                    currNode->value = res;
                    delete lhs;
                    delete rhs;
                };

                switch(currNode->token.value[0]) {
                    case '+': apply(lhs->value + rhs->value); break;
                    case '-': apply(lhs->value - rhs->value); break;
                    case '*': apply(lhs->value * rhs->value); break;
                    case '/': apply(lhs->value / rhs->value); break;
                    default: break; // Assignment
                }
            }
        }
    }

    return currNode;
}
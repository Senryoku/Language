#include <AST.hpp>

void AST::optimize() {
    _root = *optimize(&_root);
}

bool compatible(AST::Node::ValueType lhs, AST::Node::ValueType rhs) {
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
        if(lhs->type == AST::Node::Type::ConstantValue && rhs->type == AST::Node::Type::ConstantValue && compatible(lhs->value_type, rhs->value_type)) {
            assert(lhs->children.size() == 0);
            assert(rhs->children.size() == 0);

            if(lhs->value_type == AST::Node::ValueType::Integer && rhs->value_type == AST::Node::ValueType::Integer) {
                constexpr auto apply = [&](int32_t res) {
                    currNode->children.clear();
                    delete currNode;
                    currNode = new AST::Node(AST::Node::Type::ConstantValue);
                    currNode->value_type = AST::Node::ValueType::Integer;
                    currNode->value.as_int32_t = res;
                    delete lhs;
                    delete rhs;
                };

                switch(currNode->token.value[0]) {
                    case '+': apply(lhs->value.as_int32_t + rhs->value.as_int32_t); break;
                    case '-': apply(lhs->value.as_int32_t - rhs->value.as_int32_t); break;
                    case '*': apply(lhs->value.as_int32_t * rhs->value.as_int32_t); break;
                    case '/': apply(lhs->value.as_int32_t / rhs->value.as_int32_t); break;
                    default: break; // Assignment
                }
            }
        }
    }

    return currNode;
}
#pragma once

#include <AST.hpp>
#include <Logger.hpp>
#include <Scope.hpp>
#include <VariableStore.hpp>

constexpr bool is_assignable(GenericValue::Type var, GenericValue::Type val) {
    // TODO
    return var == val;
}

class Interpreter : public Scoped {
  public:
    Interpreter() {
        push_scope(); // TODO: Push an empty for now.
    }

    void execute(const AST& ast) { execute(ast.getRoot()); }

    GenericValue execute(const AST::Node& node) {
        switch(node.type) {
            using enum AST::Node::Type;
            case Root: {
                for(const auto& child : node.children)
                    execute(*child);
                break;
            }
            case VariableDeclaration: {
                assert(!get_scope().is_declared(node.token.value));
                get_scope().declare_variable(node.value.type, node.value.value.as_string.to_std_string_view());
                return node.value; // FIXME
                break;
            }
            case Variable: {
                return get_scope()[node.token.value];
                break;
            }
            case BinaryOperator: {
                assert(node.children.size() == 2);
                auto lhs = execute(*node.children[0]);
                auto rhs = execute(*node.children[1]);

                // Assignment (Should probably be its own Node::Type...)
                if(node.token.value[0] == '=') {
                    if(node.children[0]->type != Variable) {
                        error("Trying to assign to something ({}) that's not a variable?", node.children[0]->token);
                        break;
                    }
                    if(is_assignable(node.children[0]->value.type, node.children[1]->value.type)) {
                        auto& v = get_scope()[node.children[0]->token.value];
                        // TODO: Handle others types and implicit conversions.
                        v.value.as_int32_t = rhs.value.as_int32_t;
                        _return_value.type = v.type;
                        _return_value.value = v.value;
                    }
                    break;
                }

                // TODO: Abstract this. (GenericValue operators? Free function(s) ?)
                if(lhs.type == GenericValue::Type::Integer && rhs.type == GenericValue::Type::Integer) {
                    _return_value.type = GenericValue::Type::Integer;
                    switch(node.token.value[0]) {
                        case '+': _return_value.value.as_int32_t = lhs.value.as_int32_t + rhs.value.as_int32_t; break;
                        case '-': _return_value.value.as_int32_t = lhs.value.as_int32_t - rhs.value.as_int32_t; break;
                        case '*': _return_value.value.as_int32_t = lhs.value.as_int32_t * rhs.value.as_int32_t; break;
                        case '/': _return_value.value.as_int32_t = lhs.value.as_int32_t / rhs.value.as_int32_t; break;
                        default: error("BinaryOperator: Unsupported operation ('{}') on Integer.\n", node.token.value);
                    }
                    return _return_value;
                }

                error("BinaryOperator: Unsupported operation ('{}') on {} and {}.\n", node.token.value, lhs, rhs);
                break;
            }
            case ConstantValue: {
                _return_value = node.value;
                break;
            }
        }
        return _return_value;
    }

    const const GenericValue& get_return_value() const { return _return_value; }

  private:
    GenericValue _return_value; // FIXME: Probably not the right move!
};
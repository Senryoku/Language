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
            case Root:
            case Scope: {
                for(const auto& child : node.children)
                    execute(*child);
                break;
            }
            case Expression: {
                auto value = execute(*node.children[0]);
                return value; // FIXME
                break;
            }
            case WhileStatement: {
                auto condition = execute(*node.children[0]);
                while(condition.value.as_bool) {
                    execute(*node.children[1]);
                    condition = execute(*node.children[0]);
                }
                return node.value; // FIXME
                break;
            }
            case VariableDeclaration: {
                assert(!get_scope().is_declared(node.token.value));
                get_scope().declare_variable(node.value.type, node.value.value.as_string.to_std_string_view());
                return node.value; // FIXME
                break;
            }
            case Variable: {
                auto pVar = get(node.token.value);
                if(!pVar) {
                    error("Syntax error: Undeclared variable '{}' on line {}.\n", node.token.value, node.token.line);
                    break;
                }
                return *pVar; // FIXME: Make sure GenericValue & Variable are transparent? Remove the 'Variable' type?
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
                        GenericValue* v = get(node.children[0]->token.value);
                        assert(v);
                        // TODO: Handle others types and implicit conversions.
                        v->value.as_int32_t = rhs.value.as_int32_t;
                        _return_value.type = v->type;
                        _return_value.value = v->value;
                    }
                    break;
                }

                // TODO: Abstract this. (GenericValue operators? Free function(s) ?)
                if(lhs.type == GenericValue::Type::Integer && rhs.type == GenericValue::Type::Integer) {
                    if(node.token.value.length() == 1) {
                        _return_value.type = GenericValue::Type::Integer;
                        switch(node.token.value[0]) {
                            case '+': _return_value.value.as_int32_t = lhs.value.as_int32_t + rhs.value.as_int32_t; break;
                            case '-': _return_value.value.as_int32_t = lhs.value.as_int32_t - rhs.value.as_int32_t; break;
                            case '*': _return_value.value.as_int32_t = lhs.value.as_int32_t * rhs.value.as_int32_t; break;
                            case '/': _return_value.value.as_int32_t = lhs.value.as_int32_t / rhs.value.as_int32_t; break;
                            case '<':
                                _return_value.type = GenericValue::Type::Bool;
                                _return_value.value.as_bool = lhs.value.as_int32_t < rhs.value.as_int32_t;
                                break;
                            case '>':
                                _return_value.type = GenericValue::Type::Bool;
                                _return_value.value.as_bool = lhs.value.as_int32_t > rhs.value.as_int32_t;
                                break;
                            default: error("BinaryOperator: Unsupported operation ('{}') on Integer.\n", node.token.value);
                        }
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
            case ReturnStatement: {
                assert(node.children.size() == 1);
                const auto result = execute(*node.children[0]);
                _return_value = result;
                break;
            }
            default: {
                warn("[Interpreter] Unimplemented Node type : {}.\n", node.type);
                break;
            }
        }
        return _return_value;
    }

    const GenericValue& get_return_value() const { return _return_value; }

  private:
    GenericValue _return_value; // FIXME: Probably not the right move!
};
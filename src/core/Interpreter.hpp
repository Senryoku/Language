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
                for(const auto& child : node.children) {
                    execute(*child);
                    if(_returning_value)
                        break;
                }
                break;
            }
            case Expression: {
                return execute(*node.children[0]);
                break;
            }
            case WhileStatement: {
                auto condition = execute(*node.children[0]);
                while(condition.value.as_bool) {
                    execute(*node.children[1]);
                    if(_returning_value)
                        break;
                    condition = execute(*node.children[0]);
                }
                break;
            }
            case IfStatement: {
                auto condition = execute(*node.children[0]);
                if(condition.value.as_bool) {
                    execute(*node.children[1]);
                }
                break;
            }
            case FunctionDeclaration: {
                get_scope().declare_function(node);
                break;
            }
            case FunctionCall: {
                auto functionNode = get_function(node.token.value);
                if(!functionNode) {
                    error("Runtime error: function {} as not been declared in this scope (line {}).\n", node.token.value, node.token.line);
                    break;
                }
                // TODO: Check arguments count and type
                // Execute argement in the caller scope.
                std::vector<GenericValue> arguments_values;
                for(size_t i = 0; i < functionNode->children.size() - 1; ++i)
                    arguments_values.push_back(execute(*node.children[i]));
                push_scope();
                // Declare and bind arguments (Last child in functionNode is the function body)
                for(size_t i = 0; i < functionNode->children.size() - 1; ++i) {
                    // Declare arguments
                    // TODO: Default values (?)
                    execute(*functionNode->children[i]);
                    // Bind value
                    get_scope().get(functionNode->children[i]->value.value.as_string.to_std_string_view()) = arguments_values[i];
                }
                execute(*functionNode->children.back());
                pop_scope();
                if(_returning_value) {
                    _returning_value = false;
                    return _return_value;
                }
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
                if(node.token.value.length() == 1 && node.token.value[0] == '=') {
                    if(node.children[0]->type != Variable) {
                        error("[Interpreter] Trying to assign to something ({}) that's not a variable?\n", node.children[0]->token);
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
                                _return_value.type = GenericValue::Type::Boolean;
                                _return_value.value.as_bool = lhs.value.as_int32_t < rhs.value.as_int32_t;
                                break;
                            case '>':
                                _return_value.type = GenericValue::Type::Boolean;
                                _return_value.value.as_bool = lhs.value.as_int32_t > rhs.value.as_int32_t;
                                break;
                            default: error("BinaryOperator: Unsupported operation ('{}') on Integer.\n", node.token.value);
                        }
                    } else {
                        if(node.token.value == "==") {
                            _return_value = lhs == rhs;
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
                _returning_value = true;
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
    bool         _returning_value = false;
    GenericValue _return_value{.type = GenericValue::Type::Integer, .value = 0}; // FIXME: Probably not the right move!
};
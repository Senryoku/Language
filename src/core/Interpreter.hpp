#pragma once

#include <AST.hpp>
#include <Logger.hpp>
#include <Scope.hpp>
#include <VariableStore.hpp>

constexpr bool is_assignable(const GenericValue& var, const GenericValue& val) {
    // TODO
    return var.type == val.type || (var.type == GenericValue::Type::Array && (var.value.as_array.type == val.type));
}

class Interpreter : public Scoped {
  public:
    Interpreter() {
        push_scope(); // TODO: Push an empty scope for now.
        _builtin_print.reset(new AST::Node(AST::Node::Type::BuiltInFunctionDeclaration));
        _builtin_print->token.value = "print"; // We have to provide a name via the token.
        get_scope().declare_function(*_builtin_print);
    }

    ~Interpreter() {
        for(auto p : _allocated_arrays)
            delete[] p;
        _allocated_arrays.clear();
    }

    void execute(const AST& ast) { execute(ast.getRoot()); }

    void allocate_array(Variable& var) {
        assert(var.type == GenericValue::Type::Array);
        auto arr = new GenericValue[var.value.as_array.capacity];
        _allocated_arrays.push_back(arr);
        var.value.as_array.items = arr;
    }

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
                if(functionNode->type == BuiltInFunctionDeclaration) {
                    fmt::print("Call to builtin function.\n");
                    for(size_t i = 0; i < node.children.size(); ++i) {
                        fmt::print("  {}\n", execute(*node.children[i]));
                    }
                    break;
                }
                // TODO: Check arguments count and type
                assert(node.children.size() == functionNode->children.size() - 1); // FIXME: Turn this into a runtime error and handle optional arguments
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
                    get_scope().get(functionNode->children[i]->token.value) = arguments_values[i];
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
                get_scope().declare_variable(node);
                if(node.value.type == GenericValue::Type::Array)
                    allocate_array(get_scope().get(node.token.value));
                return node.value; // FIXME
                break;
            }
            case Variable: {
                auto pVar = get(node.token.value);
                if(!pVar) {
                    error("Syntax error: Undeclared variable '{}' on line {}.\n", node.token.value, node.token.line);
                    break;
                }
                if(node.value.type == GenericValue::Type::Array) {
                    if(node.children.size() == 1) { // Accessed using an index
                        auto index = execute(*node.children[0]);
                        assert(index.type == GenericValue::Type::Integer);
                        assert((size_t)index.value.as_int32_t < node.value.value.as_array.capacity); // FIXME: Should be a runtime error?
                        return pVar->value.as_array.items[index.value.as_int32_t];
                    }
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
                    if(is_assignable(node.children[0]->value, node.children[1]->value)) {
                        GenericValue* v = get(node.children[0]->token.value);
                        assert(v);
                        if(v->type == GenericValue::Type::Array) {
                            assert(node.children[0]->children.size() == 1);
                            const auto index = execute(*node.children[0]->children[0]);
                            assert(index.type == GenericValue::Type::Integer);
                            assert((size_t)index.value.as_int32_t < v->value.as_array.capacity); // FIXME: Runtime error?
                            v = v->value.as_array.items + index.value.as_int32_t;
                            v->type = rhs.type;
                        }
                        // TODO: Handle others types and implicit conversions.
                        v->value.as_int32_t = rhs.value.as_int32_t;
                        _return_value.type = v->type;
                        _return_value.value = v->value;
                    }
                    break;
                }

                // Call the appropriate operator, see GenericValue operator overloads
#define OP(O)                  \
    if(node.token.value == #O) \
        _return_value = lhs O rhs;

                OP(+)
                else OP(-) else OP(*) else OP(/) else OP(<) else OP(>) else OP(==) else OP(!=) else error("BinaryOperator: Unsupported operation ('{}') on {} and {}.\n",
                                                                                                          node.token.value, lhs, rhs);
#undef OP

                return _return_value;

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

    std::vector<GenericValue*> _allocated_arrays;

    // FIXME
    std::shared_ptr<AST::Node> _builtin_print{nullptr};
};
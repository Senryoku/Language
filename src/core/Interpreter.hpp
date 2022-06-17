#pragma once

#include <AST.hpp>
#include <Logger.hpp>
#include <Scope.hpp>
#include <VariableStore.hpp>

constexpr bool is_assignable(const GenericValue& var, const GenericValue& val) {
    // TODO
    return var.type == val.type || (GenericValue::is_numeric(var.type) && GenericValue::is_numeric(val.type));
}

class Interpreter : public Scoped {
  public:
    Interpreter() {
        push_scope(); // TODO: Push an empty scope for now.
        _builtin_print.reset(new AST::Node(AST::Node::Type::BuiltInFunctionDeclaration));
        _builtin_print->token.value = "print"; // We have to provide a name via the token.
        get_scope().declare_function(*_builtin_print);
        _builtin_put.reset(new AST::Node(AST::Node::Type::BuiltInFunctionDeclaration));
        _builtin_put->token.value = "put"; // We have to provide a name via the token.
        get_scope().declare_function(*_builtin_put);
    }

    virtual ~Interpreter() {
        for(auto p : _allocated_arrays)
            delete[] p;
        _allocated_arrays.clear();
    }

    void execute(const AST& ast) { execute(ast.getRoot()); }

    void allocate_array(Variable& var, uint32_t size) {
        assert(var.type == GenericValue::Type::Array);
        var.value.as_array.capacity = size;
        auto arr = new GenericValue[var.value.as_array.capacity];
        _allocated_arrays.push_back(arr);
        var.value.as_array.items = arr;
        for(auto i = 0u; i < var.value.as_array.capacity; ++i) {
            var.value.as_array.items[i].type = var.value.as_array.type;
            // TODO: Also call constructors if necessary/default value?
            // TODO: Correctly handle composite types?
        }
    }

    void allocate_composite(Variable& var) {
        assert(var.type == GenericValue::Type::Composite);
        auto type = get_type(var.value.as_composite.type_id);
        auto member_count = type->children.size();
        auto arr = new GenericValue[member_count];
        _allocated_arrays.push_back(arr);
        var.value.as_composite.members = arr;
        var.value.as_composite.member_count = member_count;
        for(size_t idx = 0; idx < type->children.size(); ++idx) {
            arr[idx].type = type->children[idx]->value.type;
            // Also allocate nested variables
            if(arr[idx].type == GenericValue::Type::Composite) {
                arr[idx].value.as_composite.type_id = type->children[idx]->value.value.as_composite.type_id;
                allocate_composite(static_cast<Variable&>(arr[idx]));
            }
            // If this member as a default value, compute it.
            if(type->children[idx]->children.size() > 0)
                arr[idx].value = execute(*type->children[idx]->children[0]).value; // FIXME: We stored the default value as a child.
        }
    }

    Variable execute(const AST::Node& node) {
        switch(node.type) {
            using enum AST::Node::Type;
            case Root:
            case Statement:
                for(const auto& child : node.children) {
                    _return_value = execute(*child);
                    dereference_return_value();
                    if(_returning_value)
                        break;
                }
                break;
            case Scope: {
                push_scope();
                for(const auto& child : node.children) {
                    _return_value = execute(*child);
                    dereference_return_value();
                    if(_returning_value)
                        break;
                }
                pop_scope();
                break;
            }
            case Expression: {
                _return_value = execute(*node.children[0]);
                dereference_return_value();
                return _return_value;
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
            case ForStatement: {
                assert(node.children.size() == 4);
                auto initialisation = node.children[0];
                auto condition = node.children[1];
                auto increment = node.children[2];
                auto body = node.children[3];
                push_scope();
                execute(*initialisation);
                while(execute(*condition).value.as_bool) {
                    execute(*body);
                    execute(*increment);
                    if(_returning_value)
                        break;
                }
                pop_scope();
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
            case TypeDeclaration: {
                get_scope().declare_type(node);
                break;
            }
            case FunctionCall: {
                // FIXME: Get Function name from the first children.
                // execute(*node->children.front());
                auto functionNode = get_function(node.token.value);
                if(!functionNode) {
                    error("Runtime error: function {} as not been declared in this scope (line {}).\n", node.token.value, node.token.line);
                    break;
                }
                if(functionNode->type == BuiltInFunctionDeclaration) {
                    if(functionNode->token.value == "put") {
                        assert(node.children.size() == 2);
                        auto c = execute(*node.children[1]);
                        assert(c.type == GenericValue::Type::Char);
                        std::putchar(c.value.as_char);
                    } else {
                        fmt::print("Call to builtin function.\n");
                        for(size_t i = 0; i < node.children.size(); ++i) {
                            fmt::print("  {}\n", execute(*node.children[i]));
                        }
                    }
                    break;
                }
                // TODO: Check arguments count and type
                //     Name + Arguments            Parameters Declaration + Function Body
                assert(node.children.size() - 1 == functionNode->children.size() - 1); // FIXME: Turn this into a runtime error and handle optional arguments
                // Execute argement in the caller scope.
                std::vector<GenericValue> arguments_values;
                for(size_t i = 1; i < node.children.size(); ++i)
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
                if(get_scope().is_declared(node.token.value))
                    error("Interpreter::execute: Variable '{}' already defined in scope.", node.token.value);
                get_scope().declare_variable(node);
                if(node.value.type == GenericValue::Type::Array) {
                    auto capacity = execute(*node.children[0]);           // Compute requested capacity
                    assert(capacity.type == GenericValue::Type::Integer); // FIXME: Turn this into a runtime error
                    allocate_array(get_scope().get(node.token.value), capacity.value.as_int32_t);
                } else if(node.value.type == GenericValue::Type::Composite) {
                    allocate_composite(get_scope().get(node.token.value));
                }
                return node.value; // FIXME
                break;
            }
            case Variable: {
                auto pVar = get(node.token.value);
                if(!pVar) {
                    error("Syntax error: Undeclared variable '{}' on line {}.\n", node.token.value, node.token.line);
                    break;
                }
                _return_value.type = GenericValue::Type::Reference;
                _return_value.value.as_reference.value = pVar;
                return _return_value;
                break;
            }
            case UnaryOperator: {
                assert(node.children.size() == 1);
                if(node.token.type == Tokenizer::Token::Type::Substraction) {
                    auto rhs = execute(*node.children[0]);
                    _return_value = -rhs;
                } else if(node.token.type == Tokenizer::Token::Type::Addition) {
                    auto rhs = execute(*node.children[0]);
                    _return_value = rhs;
                } else if(node.token.type == Tokenizer::Token::Type::Increment) {
                    assert(node.children.size() == 1);
                    assert(node.children[0]->type == Variable);
                    auto* v = get(node.children[0]->token.value);
                    if(node.subtype == AST::Node::SubType::Prefix)
                        _return_value = ++(*v);
                    else
                        _return_value = (*v)++;
                } else if(node.token.type == Tokenizer::Token::Type::Decrement) {
                    assert(node.children.size() == 1);
                    assert(node.children[0]->type == Variable);
                    auto* v = get(node.children[0]->token.value);
                    if(node.subtype == AST::Node::SubType::Prefix)
                        _return_value = --(*v);
                    else
                        _return_value = (*v)--;
                } else
                    error("Unknown unary operator: '{}'\n", node.token.value);
                return _return_value;
                break;
            }
            case BinaryOperator: {
                assert(node.children.size() == 2);
                auto lhs = execute(*node.children[0]);
                auto rhs = execute(*node.children[1]);
                // rhs will never need to be accessed as a reference.
                if(rhs.type == GenericValue::Type::Reference)
                    rhs = *rhs.value.as_reference.value;

                if(node.token.type == Tokenizer::Token::Type::Assignment) {
                    assert(lhs.type == GenericValue::Type::Reference);

                    if(is_assignable(*lhs.value.as_reference.value, rhs)) {
                        lhs.value.as_reference.value->assign(rhs);
                        _return_value = *lhs.value.as_reference.value;
                        return _return_value;
                    } else {
                        error("[Interpreter] {} can't be assigned to {}\n", rhs, *lhs.value.as_reference.value);
                        print("{}", node);
                        return _return_value;
                    }
                }

                if(node.token.type == Tokenizer::Token::Type::OpenParenthesis) {
                    // FIXME
                    error("[Interpreter:{}] Operator '(' not implemented.\n", __LINE__);
                } else if(node.token.type == Tokenizer::Token::Type::OpenSubscript) {
                    auto index = rhs;
                    // Automatically convert float indices to integer, because we don't have in-language easy conversion (yet?)
                    // FIXME: I don't think this should be handled here, or at least not in this way.
                    if(index.type == GenericValue::Type::Float) {
                        index.value.as_int32_t = index.to_int32_t();
                        index.type = GenericValue::Type::Integer;
                    }
                    assert(index.type == GenericValue::Type::Integer);
                    if(lhs.type == GenericValue::Type::Reference) {
                        auto variable = lhs.value.as_reference.value;
                        if(variable->type == GenericValue::Type::Array) {
                            assert((size_t)index.value.as_int32_t < variable->value.as_array.capacity); // FIXME: Should be a runtime error?
                            _return_value.type = GenericValue::Type::Reference;
                            _return_value.value.as_reference.value = &variable->value.as_array.items[index.value.as_int32_t];
                            return _return_value;
                        }
                        // FIXME: Handle Strings (Which is not possible with our current implementation of strings as string_views)
                    } else { // Constants
                        if(lhs.type == GenericValue::Type::Array) {
                            assert((size_t)index.value.as_int32_t < lhs.value.as_array.capacity); // FIXME: Should be a runtime error?
                            _return_value = lhs.value.as_array.items[index.value.as_int32_t];
                            return _return_value;
                        } else if(lhs.type == GenericValue::Type::String) {
                            assert((size_t)index.value.as_int32_t < lhs.value.as_string.size); // FIXME: Should be a runtime error?
                            GenericValue ret{.type = GenericValue::Type::Char};
                            ret.value.as_char = *(lhs.value.as_string.begin + index.value.as_int32_t);
                            _return_value = ret;
                            return _return_value;
                        }
                    }
                } else if(node.token.type == Tokenizer::Token::Type::MemberAccess) {
                    assert(lhs.type == GenericValue::Type::Reference);
                    auto variable = lhs.value.as_reference.value;
                    assert(node.children[1]->type == AST::Node::Type::MemberIdentifier);
                    assert(variable->type == GenericValue::Type::Composite);
                    auto        lhs_type = get_type(variable->value.as_composite.type_id);
                    const auto& member_name = node.children[1]->token.value;
                    for(auto idx = 0; idx < lhs_type->children.size(); ++idx)
                        if(lhs_type->children[idx]->token.value == member_name) {
                            _return_value.type = GenericValue::Type::Reference;
                            _return_value.value.as_reference.value = &variable->value.as_composite.members[idx];
                            return _return_value;
                        }
                    assert(false);
                } else {
                    // Dereference lhs if it is a reference, we now need the actual value
                    if(lhs.type == GenericValue::Type::Reference)
                        lhs = *lhs.value.as_reference.value;

                        // Call the appropriate operator, see GenericValue operator overloads
#define OP(O)                  \
    if(node.token.value == #O) \
        _return_value = lhs O rhs;

                    OP(+)
                    else OP(-) else OP(*) else OP(/) else OP(%) else OP(<) else OP(>) else OP(<=) else OP(>=) else OP(==) else OP(!=) else OP(&&) else OP(||) else error(
                        "BinaryOperator: Unsupported operation ('{}') on {} and {}.\n", node.token.value, lhs, rhs);
#undef OP
                }

                return _return_value;

                break;
            }
            case ConstantValue: {
                _return_value = node.value;
                break;
            }
            case Cast: {
                auto child = execute(*node.children[0]);
                if(child.type == GenericValue::Type::Reference)
                    child = *child.value.as_reference.value;
                switch(child.type) {
                    case GenericValue::Type::Integer: {
                        _return_value.type = GenericValue::Type::Integer;
                        _return_value.value.as_int32_t = child.to_int32_t();
                        break;
                    }
                    case GenericValue::Type::Float: {
                        _return_value.type = GenericValue::Type::Float;
                        _return_value.value.as_float = child.to_float();
                        break;
                    }
                    default: error("[Interpreter] Unimplemented cast {}.\n", child.type);
                }
                break;
            }
            case ReturnStatement: {
                assert(node.children.size() == 1);
                auto result = execute(*node.children[0]);
                _return_value = result;
                // Fixme: Maybe we'll want to be able to return references at some point?
                dereference_return_value();
                _returning_value = true;
                break;
            }
            case MemberIdentifier: // FIXME: This node is probably useless, it's only there to provide the name of the accessed member.
                // FIXME: Actually it could be replaced by an index, or an offset, instead of the name stored in the token.
                break;
            default: {
                warn("[Interpreter] Unimplemented Node type : {}.\n", node.type);
                break;
            }
        }
        return _return_value;
    }

    const GenericValue& get_return_value() const {
        return _return_value;
    }

  private:
    bool     _returning_value = false;
    Variable _return_value{GenericValue::Type::Integer, 0}; // FIXME: Probably not the right move!

    std::vector<GenericValue*> _allocated_arrays;

    // FIXME: Not exactly elegant.
    void dereference_return_value() {
        if(_return_value.type == GenericValue::Type::Reference)
            _return_value = *_return_value.value.as_reference.value;
    }

    // FIXME
    std::shared_ptr<AST::Node> _builtin_print{nullptr};
    std::shared_ptr<AST::Node> _builtin_put{nullptr};
};

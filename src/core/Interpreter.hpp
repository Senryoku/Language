#pragma once

#include <AST.hpp>
#include <Logger.hpp>
#include <Scope.hpp>
#include <VariableStore.hpp>

constexpr bool is_assignable(const GenericValue& var, const GenericValue& val) {
    // TODO
    return var.type == val.type || (GenericValue::is_numeric(var.type) && GenericValue::is_numeric(val.type)) ||
           (var.type == GenericValue::Type::Array && (var.value.as_array.type == val.type));
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

    ~Interpreter() {
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
    }

    void allocate_composite(Variable& var) {
        assert(var.type == GenericValue::Type::Composite);
        auto type = get_type(var.value.as_composite.type_id);
        auto member_count = type->children.size();
        auto arr = new GenericValue[member_count];
        _allocated_arrays.push_back(arr);
        var.value.as_composite.members = arr;
        for(size_t idx = 0; idx < type->children.size(); ++idx) {
            arr[idx].type = type->children[idx]->value.type;
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
                    execute(*child);
                    if(_returning_value)
                        break;
                }
                break;
            case Scope: {
                push_scope();
                for(const auto& child : node.children) {
                    execute(*child);
                    if(_returning_value)
                        break;
                }
                pop_scope();
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
            case ForStatement: {
                assert(node.children.size() == 4);
                auto initialisation = node.children[0];
                auto condition = node.children[1];
                auto increment = node.children[2];
                auto body = node.children[3];
                execute(*initialisation);
                while(execute(*condition).value.as_bool) {
                    execute(*body);
                    execute(*increment);
                    if(_returning_value)
                        break;
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
                _return_value = *pVar;
                return *pVar; // FIXME: Make sure GenericValue & Variable are transparent? Remove the 'Variable' type?
                break;
            }
            case UnaryOperator: {
                assert(node.children.size() == 1);
                if(node.token.value == "-") {
                    auto rhs = execute(*node.children[0]);
                    _return_value = -rhs;
                } else if(node.token.value == "+") {
                    auto rhs = execute(*node.children[0]);
                    _return_value = rhs;
                } else if(node.token.value == "++") {
                    assert(node.children.size() == 1);
                    assert(node.children[0]->type == Variable);
                    auto* v = get(node.children[0]->token.value);
                    if(node.subtype == AST::Node::SubType::Prefix)
                        _return_value = ++(*v);
                    else
                        _return_value = (*v)++;
                } else if(node.token.value == "--") {
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
                auto rhs = execute(*node.children[1]);

                // Assignment (Should probably be its own Node::Type...)
                if(node.token.value.length() == 1 && node.token.value[0] == '=') {
                    // Search for an l-value (FIXME: should execute the whole left-hand side)
                    if(node.children[0]->type == Variable) { // Variable
                        if(is_assignable(node.children[0]->value, node.children[1]->value)) {
                            auto* v = get(node.children[0]->token.value);
                            assert(v);
                            v->assign(rhs);
                            _return_value.type = v->type;
                            _return_value.value = v->value;
                            break;
                        } else
                            error("[Interpreter] {} can't be assigned to {}\n", node.children[0]->token, node.children[1]->token);
                    } else if(node.children[0]->type == BinaryOperator && node.children[0]->token.value == "[") { // Array accessor
                        assert(node.children[0]->children.size() == 2);
                        const auto index = execute(*node.children[0]->children[1]);
                        assert(index.type == GenericValue::Type::Integer);
                        if(is_assignable(node.children[0]->children[1]->value, node.children[1]->value)) {
                            GenericValue* v = get(node.children[0]->children[0]->token.value);
                            assert(v);
                            assert((size_t)index.value.as_int32_t < v->value.as_array.capacity); // FIXME: Runtime error?
                            v = v->value.as_array.items + index.value.as_int32_t;
                            v->type = rhs.type;
                            v->assign(rhs);
                            _return_value.type = v->type;
                            _return_value.value = v->value;
                            break;
                        } else
                            error("[Interpreter] {}[{}] can't be assigned to {}\n", node.children[0]->children[0]->token, index, node.children[1]->token);
                    } else if(node.children[0]->type == BinaryOperator && node.children[0]->token.value == ".") { // Member access assignement (foo.bar = baz)
                        // TODO: We can't chain (foo.bar.baz = ...) nested composite types.
                        auto variable = node.children[0]->children[0];
                        assert(variable->type == AST::Node::Type::Variable);
                        assert(node.children[0]->children[1]->type == AST::Node::Type::MemberIdentifier);
                        const auto&   member_identifier = node.children[0]->children[1]->token.value;
                        auto          lhs_type = get_type(variable->value.value.as_composite.type_id);
                        GenericValue* structure = get(variable->token.value);
                        for(auto idx = 0; idx < lhs_type->children.size(); ++idx)
                            if(lhs_type->children[idx]->token.value == member_identifier) {
                                auto target_value = structure->value.as_composite.members + idx;
                                target_value->assign(rhs);
                                _return_value = rhs;
                                return _return_value;
                            }
                    } else {
                        error("[Interpreter] Trying to assign to something ({}) that's not neither a variable or an array?\n", node.children[0]->token);
                        break;
                    }
                }

                auto lhs = execute(*node.children[0]);

                if(node.token.value == "(") {
                    // FIXME
                } else if(node.token.value == "[") {
                    auto variableNode = node.children[0];
                    auto pVar = get(variableNode->token.value);
                    auto index = execute(*node.children[1]);
                    if(variableNode->value.type == GenericValue::Type::Array) {
                        assert(index.type == GenericValue::Type::Integer);
                        assert((size_t)index.value.as_int32_t < pVar->value.as_array.capacity); // FIXME: Should be a runtime error?
                        _return_value = pVar->value.as_array.items[index.value.as_int32_t];
                        return pVar->value.as_array.items[index.value.as_int32_t];
                    } else if(variableNode->value.type == GenericValue::Type::String) {
                        // FIXME: This would be much cleaner if string was just a char[]...
                        // Automatically convert float indices to integer, because we don't have in-language easy conversion (yet?)
                        // FIXME: I don't think this should be handled here.
                        if(index.type == GenericValue::Type::Float) {
                            index.value.as_int32_t = index.to_int32_t();
                            index.type = GenericValue::Type::Integer;
                        }
                        assert(index.type == GenericValue::Type::Integer);
                        assert((size_t)index.value.as_int32_t < pVar->value.as_string.size); // FIXME: Should be a runtime error?
                        GenericValue ret{.type = GenericValue::Type::Char};
                        ret.value.as_char = *(pVar->value.as_string.begin + index.value.as_int32_t);
                        _return_value = ret;
                        return ret;
                    }
                } else if(node.token.value == ".") {
                    assert(node.children[1]->type == AST::Node::Type::MemberIdentifier);
                    assert(lhs.type == GenericValue::Type::Composite);
                    auto        lhs_type = get_type(lhs.value.as_composite.type_id);
                    const auto& member_name = node.children[1]->token.value;
                    for(auto idx = 0; idx < lhs_type->children.size(); ++idx)
                        if(lhs_type->children[idx]->token.value == member_name) {
                            _return_value = lhs.value.as_composite.members[idx];
                            return _return_value;
                        }
                    assert(false);
                } else {
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
                switch(node.value.type) {
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
                    default: error("[Interpreter] Unimplemented cast {}.\n", node.value.type);
                }
                break;
            }
            case ReturnStatement: {
                assert(node.children.size() == 1);
                const auto result = execute(*node.children[0]);
                _return_value = result;
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
    Variable _return_value{{.type = GenericValue::Type::Integer, .value = 0}}; // FIXME: Probably not the right move!

    std::vector<GenericValue*> _allocated_arrays;

    // FIXME
    std::shared_ptr<AST::Node> _builtin_print{nullptr};
    std::shared_ptr<AST::Node> _builtin_put{nullptr};
};

#include <Parser.hpp>

#include <algorithm>
#include <fstream>

bool Parser::parse(const std::span<Tokenizer::Token>& tokens, AST::Node* curr_node) {
    curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Statement));
    auto it = tokens.begin();
    while(it != tokens.end()) {
        const auto& token = *it;
        switch(token.type) {
            case Tokenizer::Token::Type::OpenScope: {
                curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Scope, *it));
                push_scope();
                ++it;
                break;
            }
            case Tokenizer::Token::Type::CloseScope: {
                assert(false); // FIXME: I don't think this case is needed, it should already be taken care of and always return an error.
                while(curr_node->type != AST::Node::Type::Scope && curr_node->parent != nullptr)
                    curr_node = curr_node->parent;
                if(curr_node->type != AST::Node::Type::Scope) {
                    error("[Parser] Syntax error: Unmatched '}' one line {}.\n", it->line);
                    return false;
                }
                curr_node->value.type = get_scope().get_return_type();
                curr_node = curr_node->parent;
                pop_scope();
                ++it;
                break;
            }
            case Tokenizer::Token::Type::EndStatement: {
                curr_node = curr_node->parent;
                curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Statement, *it));
                ++it;
                break;
            }
            case Tokenizer::Token::Type::If: {
                if(!peek(tokens, it, Tokenizer::Token::Type::OpenParenthesis)) {
                    error("[Parser] Syntax error: expected '(' after 'if'.\n");
                    return false;
                }
                auto ifNode = curr_node->add_child(new AST::Node(AST::Node::Type::IfStatement, *it));
                it += 2;
                if(!parse_next_expression(tokens, it, ifNode, max_precedence, true)) {
                    delete curr_node->pop_child();
                    return false;
                }
                if(!parse_scope_or_single_statement(tokens, it, ifNode)) {
                    error("[Parser] Syntax error: Expected 'new scope' or single statement after 'if'.\n");
                    delete curr_node->pop_child();
                    return false;
                }

                if(it->type == Tokenizer::Token::Type::Else) {
                    ++it;
                    if(!parse_scope_or_single_statement(tokens, it, ifNode)) {
                        error("[Parser] Syntax error: Expected 'new scope' or single statement after 'else'.\n");
                        delete curr_node->pop_child();
                        return false;
                    }
                }
                break;
            }
            case Tokenizer::Token::Type::Const:
                ++it;
                assert(it->type == Tokenizer::Token::Type::Identifier);
                if(!parse_variable_declaration(tokens, it, curr_node, true))
                    return false;
                break;
            case Tokenizer::Token::Type::Return: {
                auto returnNode = curr_node->add_child(new AST::Node(AST::Node::Type::ReturnStatement, *it));
                ++it;
                if(!parse_next_expression(tokens, it, returnNode)) {
                    delete curr_node->pop_child();
                    return false;
                }
                returnNode->value.type = returnNode->children[0]->value.type;
                auto scope_return_type = get_scope().get_return_type();
                if(scope_return_type == GenericValue::Type::Undefined) {
                    get_scope().set_return_type(returnNode->value.type);
                } else if(scope_return_type != returnNode->value.type) {
                    error("[Parser] Syntax error: Incoherent return types, got {} on line {}, expected {}.\n", returnNode->value.type, returnNode->token.line, scope_return_type);
                    return false;
                }
                break;
            }

            // Constants
            case Tokenizer::Token::Type::Boolean: {
                if(!parse_boolean(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Digits: {
                if(!parse_digits(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Float: {
                if(!parse_float(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::CharLiteral: {
                if(!parse_char(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::StringLiteral: {
                if(!parse_string(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Assignment: [[fallthrough]];
            case Tokenizer::Token::Type::Xor: [[fallthrough]];
            case Tokenizer::Token::Type::Or: [[fallthrough]];
            case Tokenizer::Token::Type::And: [[fallthrough]];
            case Tokenizer::Token::Type::Equal: [[fallthrough]];
            case Tokenizer::Token::Type::Different: [[fallthrough]];
            case Tokenizer::Token::Type::Lesser: [[fallthrough]];
            case Tokenizer::Token::Type::LesserOrEqual: [[fallthrough]];
            case Tokenizer::Token::Type::Greater: [[fallthrough]];
            case Tokenizer::Token::Type::GreaterOrEqual: [[fallthrough]];
            case Tokenizer::Token::Type::Addition: [[fallthrough]];
            case Tokenizer::Token::Type::Substraction: [[fallthrough]];
            case Tokenizer::Token::Type::Multiplication: [[fallthrough]];
            case Tokenizer::Token::Type::Division: [[fallthrough]];
            case Tokenizer::Token::Type::Modulus: [[fallthrough]];
            case Tokenizer::Token::Type::Increment: [[fallthrough]];
            case Tokenizer::Token::Type::Decrement: [[fallthrough]];
            case Tokenizer::Token::Type::OpenParenthesis: [[fallthrough]];
            case Tokenizer::Token::Type::CloseParenthesis: [[fallthrough]];
            case Tokenizer::Token::Type::OpenSubscript: [[fallthrough]];
            case Tokenizer::Token::Type::CloseSubscript: [[fallthrough]];
            case Tokenizer::Token::Type::MemberAccess: {
                if(!parse_operator(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Identifier: {
                if(!parse_identifier(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::While: {
                if(!parse_while(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::For: {
                if(!parse_for(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Function: {
                if(!parse_function_declaration(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Type: {
                if(!parse_type_declaration(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Import: {
                if(!parse_import(tokens, it, curr_node))
                    return false;
                break;
            }
            case Tokenizer::Token::Type::Export: {
                ++it;
                // Assume it's a function for now
                // TODO: Handle exported variables too.
                if(!parse_function_declaration(tokens, it, curr_node))
                    return false;
                _exports.push_back(curr_node->children.back());
                break;
            }
            case Tokenizer::Token::Type::Comment: ++it; break;
            default:
                warn("[Parser] Unused token: {}.\n", *it);
                ++it;
                break;
        }
    }
    // Remove empty statements (at end of file)
    if(curr_node->type == AST::Node::Type::Statement && curr_node->children.empty()) {
        auto tmp = curr_node;
        curr_node = curr_node->parent;
        curr_node->children.erase(std::find(curr_node->children.begin(), curr_node->children.end(), tmp));
        delete tmp;
    }
    return true;
}

bool Parser::parse_next_scope(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    if(it == tokens.end() || it->type != Tokenizer::Token::Type::OpenScope) {
        if(it == tokens.end())
            error("[Parser] Syntax error: Expected scope opening, got end-of-document.\n");
        else
            error("[Parser] Syntax error: Expected scope opening on line {}, got {}.\n", it->line, it->value);
        return false;
    }
    auto   begin = it + 1;
    auto   end = it;
    size_t opened_scopes = 0;
    while(end != tokens.end()) {
        if(end->type == Tokenizer::Token::Type::OpenScope)
            ++opened_scopes;
        if(end->type == Tokenizer::Token::Type::CloseScope) {
            --opened_scopes;
            if(opened_scopes == 0)
                break;
        }
        ++end;
    }
    if(opened_scopes > 0) {
        error("[Parser] Syntax error: no matching 'closing bracket', got end-of-document.\n");
        return false;
    }

    auto scope = curr_node->add_child(new AST::Node(AST::Node::Type::Scope, *it));
    push_scope();
    bool r = parse({begin, end}, scope);
    scope->value.type = get_scope().get_return_type();
    pop_scope();
    it = end + 1;
    return r;
}

// TODO: Formely define wtf is an expression :)
bool Parser::parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node, uint32_t precedence,
                                   bool search_for_matching_bracket) {
    if(it == tokens.end()) {
        error("[Parser] Expected expression, got end-of-file.\n");
        return false;
    }

    // Temporary expression node. Will be replace by its child when we're done parsing it.
    auto exprNode = curr_node->add_child(new AST::Node(AST::Node::Type::Expression));

    if(it->type == Tokenizer::Token::Type::OpenParenthesis) {
        ++it;
        if(!parse_next_expression(tokens, it, exprNode, max_precedence, true)) {
            delete curr_node->pop_child();
            return false;
        }
    }

    bool stop = false;

    while(it != tokens.end() && !(it->type == Tokenizer::Token::Type::EndStatement || it->type == Tokenizer::Token::Type::Comma) && !stop) {
        // TODO: Check other types!
        switch(it->type) {
            using enum Tokenizer::Token::Type;
            case Boolean: {
                if(!parse_boolean(tokens, it, exprNode)) {
                    delete curr_node->pop_child();
                    return false;
                }
                break;
            }
            case Digits: {
                if(!parse_digits(tokens, it, exprNode)) {
                    delete curr_node->pop_child();
                    return false;
                }
                break;
            }
            case Float: {
                if(!parse_float(tokens, it, exprNode)) {
                    delete curr_node->pop_child();
                    return false;
                }
                break;
            }
            case CharLiteral: {
                if(!parse_char(tokens, it, exprNode)) {
                    delete curr_node->pop_child();
                    return false;
                }
                break;
            }
            case StringLiteral: {
                if(!parse_string(tokens, it, exprNode)) {
                    delete curr_node->pop_child();
                    return false;
                }
                break;
            }
            case Identifier: {
                if(!parse_identifier(tokens, it, exprNode)) {
                    delete curr_node->pop_child();
                    return false;
                }
                break;
            }
            case CloseParenthesis: [[fallthrough]];
            case CloseSubscript: stop = true; break;
            case Assignment: [[fallthrough]];
            case Xor: [[fallthrough]];
            case Or: [[fallthrough]];
            case And: [[fallthrough]];
            case Equal: [[fallthrough]];
            case Different: [[fallthrough]];
            case Lesser: [[fallthrough]];
            case LesserOrEqual: [[fallthrough]];
            case Greater: [[fallthrough]];
            case GreaterOrEqual: [[fallthrough]];
            case Addition: [[fallthrough]];
            case Substraction: [[fallthrough]];
            case Multiplication: [[fallthrough]];
            case Division: [[fallthrough]];
            case Modulus: [[fallthrough]];
            case Increment: [[fallthrough]];
            case Decrement: [[fallthrough]];
            case OpenParenthesis: [[fallthrough]];
            case OpenSubscript: [[fallthrough]];
            case MemberAccess: {
                auto p = operator_precedence.at(it->type);
                if(p < precedence) {
                    if(!parse_operator(tokens, it, exprNode)) {
                        delete curr_node->pop_child();
                        return false;
                    }
                } else {
                    stop = true;
                }
                break;
            }
            default: {
                warn("[parse_next_expression] Unexpected Token Type '{}' ({}).\n", it->type, *it);
                delete curr_node->pop_child();
                return false;
                break;
            }
        }
    }

    if(search_for_matching_bracket && (it == tokens.end() || it->type != Tokenizer::Token::Type::CloseParenthesis)) {
        if(it == tokens.end())
            error("[Parser] Unmatched '(' after reaching end-of-document.\n");
        else
            error("[Parser] Unmatched '(' on line {}.\n", it->line);
        delete curr_node->pop_child();
        return false;
    }

    assert(exprNode->children.size() == 1);

    curr_node->pop_child();
    auto child = exprNode->pop_child();
    curr_node->add_child(child);
    delete exprNode;

    if(search_for_matching_bracket) // Skip ending bracket
        ++it;

    return true;
}

bool Parser::parse_identifier(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    assert(it->type == Tokenizer::Token::Type::Identifier);

    if(is_type(it->value)) {
        return parse_variable_declaration(tokens, it, curr_node);
    }

    // Function Call
    // FIXME: Should be handled by parse_operator for () to be a generic operator!
    //        Or realise that this is a function, somehow (keep track of declaration).
    if(peek(tokens, it, Tokenizer::Token::Type::OpenParenthesis)) {
        // TODO: Check if the function has been declared (or is a built-in?) & Fetch corresponding FunctionDeclaration Node.
        curr_node->add_child(new AST::Node(AST::Node::Type::Variable, *it)); // FIXME: Use another node type
        ++it;
        return true;
    }

    auto maybe_variable = get(it->value);
    if(!maybe_variable) {
        error("[Parser] Syntax Error: Variable '{}' has not been declared on line {}.\n", it->value, it->line);
        return false;
    }
    const auto& variable = *maybe_variable;

    // FIXME: Replaces the variable lookup by a constant, should be a net win for the interpreter, but not in general.
    if(variable.is_constexpr()) {
        auto constNode = curr_node->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
        constNode->value = variable;
        ++it;
        return true;
    }

    auto variable_node = curr_node->add_child(new AST::Node(AST::Node::Type::Variable, *it));
    variable_node->value.type = variable.type;
    if(variable.type == GenericValue::Type::Composite)
        variable_node->value.value.as_composite.type_id = variable.value.as_composite.type_id;

    if(peek(tokens, it, Tokenizer::Token::Type::OpenSubscript)) { // Array accessor
        if(variable.type != GenericValue::Type::Array && variable.type != GenericValue::Type::String) {
            error("[Parser] Syntax Error: Subscript operator on variable '{}' on line {} which neither an array nor a string.\n", it->value, it->line);
            delete curr_node->pop_child();
            return false;
        }
        variable_node = curr_node->pop_child();
        auto access_operator_node = curr_node->add_child(new AST::Node(AST::Node::Type::BinaryOperator, *(it + 1)));
        access_operator_node->add_child(variable_node);

        if(variable.type == GenericValue::Type::Array) {
            variable_node->value.value.as_array.type = variable.value.as_array.type;
            variable_node->value.value.as_array.capacity = variable.value.as_array.capacity; // FIXME: Not known anymore at this stage
            access_operator_node->value.type = variable_node->value.value.as_array.type;
        }

        it += 2;
        // Get the index and add it as a child.
        // FIXME: Search the matching ']' here?
        if(!parse_next_expression(tokens, it, access_operator_node, max_precedence, false)) {
            delete curr_node->pop_child();
            return false;
        }

        // TODO: Make sure this is an integer? And compile-time constant?
        assert(it->type == Tokenizer::Token::Type::CloseSubscript);
        ++it; // Skip ']'
    } else {
        ++it;
    }
    return true;
}

bool Parser::parse_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    // FIXME: There are probably a lot more tokens that should stop a single statement (first example would be an 'else' after an if without an open block).
    //        Does the list of stopping keywords depend on the context?
    auto end = it;
    while(end != tokens.end() && end->type != Tokenizer::Token::Type::EndStatement)
        ++end;
    // Include ',' in the sub-parsing
    if(end != tokens.end())
        ++end;
    if(!parse({it, end}, curr_node))
        return false;
    it = end;
    return true;
}

bool Parser::parse_scope_or_single_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    if(it->type == Tokenizer::Token::Type::OpenScope) {
        if(!parse_next_scope(tokens, it, curr_node))
            return false;
    } else {
        if(!parse_statement(tokens, it, curr_node))
            return false;
    }
    return true;
}

bool Parser::parse_while(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto whileNode = curr_node->add_child(new AST::Node(AST::Node::Type::WhileStatement, *it));
    ++it;
    if(it->type != Tokenizer::Token::Type::OpenParenthesis) {
        error("Expected '(' after while on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }
    // Parse condition and add it as first child
    ++it; // Point to the beginning of the expression until ')' ('search_for_matching_bracket': true)
    if(!parse_next_expression(tokens, it, whileNode, max_precedence, true)) {
        delete curr_node->pop_child();
        return false;
    }

    if(it == tokens.end()) {
        error("Expected while body on line {}, got end-of-file.\n", it->line);
        delete curr_node->pop_child();
        return false;
    }

    if(!parse_scope_or_single_statement(tokens, it, whileNode)) {
        delete curr_node->pop_child();
        return false;
    }
    return true;
}

bool Parser::parse_for(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto forNode = curr_node->add_child(new AST::Node(AST::Node::Type::ForStatement, *it));
    ++it;
    if(it->type != Tokenizer::Token::Type::OpenParenthesis) {
        error("Expected '(' after for on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }
    ++it; // Skip '('

    push_scope(); // Encapsulate variable declaration from initialisation and single statement body.

    const auto cleanup_on_error = [&]() {
        delete curr_node->pop_child();
        pop_scope();
        return false;
    };

    // Initialisation
    if(!parse_statement(tokens, it, forNode))
        return cleanup_on_error();
    // Condition
    if(!parse_statement(tokens, it, forNode))
        return cleanup_on_error();
    // Increment (until bracket)
    if(!parse_next_expression(tokens, it, forNode, max_precedence, true))
        return cleanup_on_error();

    if(it == tokens.end()) {
        error("Expected while body on line {}, got end-of-file.\n", it->line);
        return cleanup_on_error();
    }

    if(!parse_scope_or_single_statement(tokens, it, forNode))
        return cleanup_on_error();

    pop_scope();
    return true;
}

bool Parser::parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto functionNode = curr_node->add_child(new AST::Node(AST::Node::Type::FunctionDeclaration, *it));
    ++it;
    if(it->type != Tokenizer::Token::Type::Identifier) {
        error("Expected identifier in function declaration on line {}, got {}.\n", it->line, it->value);
        return false;
    }
    functionNode->token = *it;                                // Store the function name using its token.
    functionNode->value.type = GenericValue::Type::Undefined; // TODO: Actually compute the return type (will probably just be part of the declation.)
    ++it;
    if(it->type != Tokenizer::Token::Type::OpenParenthesis) {
        error("Expected '(' in function declaration on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }

    // Declare the function immediatly to allow recursive calls.
    get_scope().declare_function(*functionNode);

    push_scope(); // FIXME: Restrict function parameters to this scope, do better.

    auto cleanup_on_error = [&]() {
        delete curr_node->pop_child();
        pop_scope();
        // Cleanup the declared function.
        get_scope().remove_function(*functionNode);
    };

    ++it;
    // Parse parameters
    while(it != tokens.end() && it->type != Tokenizer::Token::Type::CloseParenthesis) {
        if(!parse_variable_declaration(tokens, it, functionNode)) {
            cleanup_on_error();
            return false;
        }
        if(it->type == Tokenizer::Token::Type::Comma)
            ++it;
        else if(it->type != Tokenizer::Token::Type::CloseParenthesis) {
            error("[Parser] Expected ',' in function declaration argument list on line {}, got {}.\n", it->line, it->value);
            cleanup_on_error();
            return false;
        }
    }
    if(it == tokens.end() || it->type != Tokenizer::Token::Type::CloseParenthesis) {
        error("[Parser] Unmatched '(' in function declaration on line {}.\n", it->line);
        cleanup_on_error();
        return false;
    }

    ++it;
    if(it == tokens.end()) {
        error("[Parser] Expected function body on line {}, got end-of-file.\n", it->line);
        cleanup_on_error();
        return false;
    }

    if(it->type == Tokenizer::Token::Type::Colon) {
        ++it;
        if(it->type != Tokenizer::Token::Type::Identifier || !is_type(it->value)) {
            error("[Parser] Expected type identifier after function '{}' declaration body on line {}, got '{}'.\n", functionNode->token.value, it->line, it->value);
            cleanup_on_error();
            return false;
        }
        functionNode->value.type = GenericValue::parse_type(it->value);
        ++it;
    }

    if(!parse_scope_or_single_statement(tokens, it, functionNode)) {
        cleanup_on_error();
        return false;
    }

    // Return type deduction
    if(functionNode->value.type == GenericValue::Type::Undefined)
        functionNode->value.type = functionNode->children.back()->value.type;
    else if(functionNode->value.type != functionNode->children.back()->value.type) {
        error("[Parser] Syntax error: Incoherent return types for function {}, got {} on line {}, expected {}.\n", functionNode->token.value,
              functionNode->children.back()->value.type, functionNode->children.back()->token.line, functionNode->value.type);
        cleanup_on_error();
        return false;
    }

    pop_scope();

    return true;
}

bool Parser::parse_type_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto type_node = curr_node->add_child(new AST::Node(AST::Node::Type::TypeDeclaration, *it));
    ++it;
    if(it->type != Tokenizer::Token::Type::Identifier) {
        error("Expected identifier in type declaration on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }
    type_node->token = *it; // Store the type name using its token.

    if(!get_scope().declare_type(*type_node)) {
        delete curr_node->pop_child();
        return false;
    }

    ++it;
    if(!(it->type == Tokenizer::Token::Type::OpenScope)) {
        error("Expected '{{' after type declaration on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }

    push_scope();
    ++it;

    while(it != tokens.end() && !(it->type == Tokenizer::Token::Type::CloseScope)) {
        if(!parse_variable_declaration(tokens, it, type_node)) {
            delete curr_node->pop_child();
            pop_scope();
            return false;
        }
        // Skip ';'
        if(it != tokens.end() && it->type == Tokenizer::Token::Type::EndStatement)
            ++it;
        // parse_variable_declaration may add an initialisation node, we'll make this a special case and add the default value as a child.
        if(type_node->children.back()->type == AST::Node::Type::BinaryOperator) {
            auto assignment_node = type_node->pop_child();
            auto rhs = assignment_node->pop_child();
            type_node->children.back()->add_child(rhs);
            delete assignment_node;
            // FIXME: Should we require a constexpr value here? In this case we could store it directly in the declaration node value.
        }

        if(type_node->children.back()->type == AST::Node::Type::VariableDeclaration && type_node->children.back()->value.type == GenericValue::Type::Composite) {
            // FIXME: Add a call to the constructor here? Like the default values?
        }
    }
    pop_scope();

    if(it == tokens.end()) {
        error("Expected '}}' after type declaration on line {}, got end-of-file.\n ", it->line);
        delete curr_node->pop_child();
        return false;
    }

    ++it; // Skip '}'

    return true;
}

bool Parser::parse_boolean(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto boolNode = curr_node->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    boolNode->value.type = GenericValue::Type::Boolean;
    boolNode->value.flags = GenericValue::Flags::CompileConst;
    boolNode->value.value.as_bool = it->value == "true";
    ++it;
    return true;
}

bool Parser::parse_digits(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto integer = curr_node->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    integer->value.type = GenericValue::Type::Integer;
    integer->value.flags = GenericValue::Flags::CompileConst;
    auto [ptr, error_code] = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), integer->value.value.as_int32_t);
    if(error_code == std::errc::invalid_argument) {
        error("[Parser::parse_digits] std::from_chars returned invalid_argument.\n");
        return false;
    } else if(error_code == std::errc::result_out_of_range) {
        error("[Parser::parse_digits] std::from_chars returned result_out_of_range.\n");
        return false;
    }
    ++it;
    return true;
}

bool Parser::parse_float(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto floatNode = curr_node->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    floatNode->value.type = GenericValue::Type::Float;
    floatNode->value.flags = GenericValue::Flags::CompileConst;
    auto [ptr, error_code] = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), floatNode->value.value.as_float);
    if(error_code == std::errc::invalid_argument) {
        error("[Parser::parse_float] std::from_chars returned invalid_argument.\n");
        return false;
    } else if(error_code == std::errc::result_out_of_range) {
        error("[Parser::parse_float] std::from_chars returned result_out_of_range.\n");
        return false;
    }
    ++it;
    return true;
}

bool Parser::parse_char(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto strNode = curr_node->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    strNode->value.type = GenericValue::Type::Char;
    strNode->value.flags = GenericValue::Flags::CompileConst;
    strNode->value.value.as_char = it->value[0];
    ++it;
    return true;
}

bool Parser::parse_string(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto strNode = curr_node->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    strNode->value.type = GenericValue::Type::String;
    strNode->value.flags = GenericValue::Flags::CompileConst;

    if(it->value.find('\\')) {
        std::string str;
        for(auto idx = 0u; idx < it->value.size(); ++idx) {
            auto ch = it->value[idx];
            if(ch == '\\') {
                assert(idx != it->value.size() - 1);
                ch = it->value[++idx];
                switch(ch) {
                    case 'a': str += '\a'; break;
                    case 'b': str += '\b'; break;
                    case 'n': str += '\n'; break;
                    case 'r': str += '\r'; break;
                    case 't': str += '\t'; break;
                    case '"': str += '"'; break;
                    case '\\': str += '\\'; break;
                    default: error("[Parser] Unknown escape sequence \\{} in string on line {}.\n", ch, it->line); str += '\\' + ch;
                }
            } else
                str += ch;
        }
        strNode->value.value.as_string = *internalize_string(str);
    } else
        strNode->value.value.as_string = it->value;
    ++it;
    return true;
}

bool Parser::parse_operator(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto operator_type = it->type;
    // Unary operators
    if(is_unary_operator(operator_type) && curr_node->children.empty()) {
        AST::Node* unary_operator_node = curr_node->add_child(new AST::Node(AST::Node::Type::UnaryOperator, *it, AST::Node::SubType::Prefix));
        auto       precedence = operator_precedence.at(operator_type);
        ++it;
        if(!parse_next_expression(tokens, it, unary_operator_node, precedence)) {
            delete curr_node->pop_child();
            return false;
        }
        resolve_operator_type(unary_operator_node);
        return true;
    }

    if((operator_type == Tokenizer::Token::Type::Increment || operator_type == Tokenizer::Token::Type::Decrement) && !curr_node->children.empty()) {
        auto       prev_node = curr_node->pop_child();
        AST::Node* unary_operator_node = curr_node->add_child(new AST::Node(AST::Node::Type::UnaryOperator, *it, AST::Node::SubType::Postfix));
        // auto   precedence = operator_precedence.at(std::string(it->value));
        // FIXME: How do we use the precedence here?
        unary_operator_node->add_child(prev_node);
        ++it;
        resolve_operator_type(unary_operator_node);
        return true;
    }

    // '(', but not a function declaration or call operator.
    if(operator_type == Tokenizer::Token::Type::OpenParenthesis && curr_node->children.empty())
        return parse_next_expression(tokens, it, curr_node);

    if(operator_type == Tokenizer::Token::Type::CloseParenthesis) {
        // Should have been handled by others parsing functions.
        error("[Parser::parse_operator] Unmatched ')' on line {}.\n", it->line);
        return false;
    }

    // Function call
    if(operator_type == Tokenizer::Token::Type::OpenParenthesis) {
        const auto start = (it + 1);
        auto       function_node = curr_node->pop_child();
        auto       call_node = curr_node->add_child(new AST::Node(AST::Node::Type::FunctionCall, function_node->token));
        call_node->add_child(function_node);

        // TODO: Check type of functionNode (is it actually callable?)
        if(function_node->type != AST::Node::Type::Variable) {
            error("[Parser] '{}' doesn't seem to be callable (may be a missing implementation).\n", function_node->token.value);
            return false;
        }
        // FIXME: FunctionCall uses its token for now to get the function name, but this in incorrect, it should look at the
        // first child and execute it to get a reference to the function. Using the function name token as a temporary workaround.
        auto function_name = function_node->token.value;
        auto function = get_function(function_name);
        if(!function) {
            error("[Parser] Call to undefined function '{}' on line {}.\n", function_name, it->line);
            return false;
        }
        call_node->value.type = function->value.type;

        ++it;
        // Parse arguments
        while(it != tokens.end() && it->type != Tokenizer::Token::Type::CloseParenthesis) {
            if(!parse_next_expression(tokens, it, call_node)) {
                delete curr_node->pop_child();
                return false;
            }
            // Skip ","
            if(it != tokens.end() && it->type == Tokenizer::Token::Type::Comma)
                ++it;
        }
        if(it == tokens.end()) {
            error("[Parser::parse_operator] Syntax error: Unmatched '(' on line {}, got to end-of-file.\n", start->line);
            delete curr_node->pop_child();
            return false;
        }
        ++it;
        return true;
    }

    if(curr_node->children.empty()) {
        error("[Parser::parse_operator] Syntax error: unexpected binary operator: {}.\n", *it);
        return false;
    }
    AST::Node* prev_expr = curr_node->pop_child();
    // TODO: Test type of previous node! (Must be an expression resolving to something operable)
    AST::Node* binary_operator_node = curr_node->add_child(new AST::Node(AST::Node::Type::BinaryOperator, *it));

    binary_operator_node->add_child(prev_expr);

    auto precedence = operator_precedence.at(operator_type);
    ++it;
    if(it == tokens.end()) {
        error("[Parser::parse_operator] Syntax error: Reached end of document without a right-hand side operand for {} on line {}.\n", it->value, it->line);
        delete curr_node->pop_child();
        return false;
    }

    if(operator_type == Tokenizer::Token::Type::OpenSubscript) {
        if(!parse_next_expression(tokens, it, binary_operator_node)) {
            delete curr_node->pop_child();
            return false;
        }
        // Check for ']' and advance
        if(it->type != Tokenizer::Token::Type::CloseSubscript) {
            error("[Parser::parse_operator] Syntax error: Expected ']', got '{}' (line {}).\n", it->value, it->line);
            delete curr_node->pop_child();
            return false;
        }
        ++it;
    } else if(operator_type == Tokenizer::Token::Type::MemberAccess) {
        if(prev_expr->value.type != GenericValue::Type::Composite) {
            error("[Parser] Syntax error: Use of the '.' operator is only valid on composite types.\n");
            delete curr_node->pop_child();
            return false;
        }
        // Only allow a single identifier, use subscript for more complex expressions?
        if(!(it->type == Tokenizer::Token::Type::Identifier)) {
            error("[Parser] Syntax error: Expected identifier on the right side of '.' operator.\n");
            delete curr_node->pop_child();
            return false;
        }
        const auto       identifier_name = it->value;
        const auto       type = get_type(prev_expr->value.value.as_composite.type_id);
        bool             member_exists = false;
        const AST::Node* member_node = nullptr;
        for(const auto& c : type->children)
            if(c->token.value == identifier_name) {
                member_exists = true;
                member_node = c;
                break;
            }
        if(!member_exists) {
            error("[Parser] Syntax error: Member '{}' does not exists on type {}.\n", identifier_name, type->token.value);
            delete curr_node->pop_child();
            return false;
        }
        binary_operator_node->add_child(new AST::Node(AST::Node::Type::MemberIdentifier, *it));
        ++it;
    } else {
        // Lookahead for rhs
        if(!parse_next_expression(tokens, it, binary_operator_node, precedence)) {
            delete curr_node->pop_child();
            return false;
        }
    }

    // TODO: Test if types are compatible (with the operator and between each other)

    // Assignment: if variable is const and value is constexpr, mark the variable as constexpr.
    if(operator_type == Tokenizer::Token::Type::Assignment) {
        // FIXME: Workaround to allow more constant propagation. Should be done in a later stage.
        binary_operator_node->children[1] = AST::optimize(binary_operator_node->children[1]);
        if(binary_operator_node->children[0]->type == AST::Node::Type::Variable && binary_operator_node->children[0]->subtype == AST::Node::SubType::Const &&
           binary_operator_node->children[1]->type == AST::Node::Type::ConstantValue) {
            auto maybe_variable = get(binary_operator_node->children[0]->token.value);
            assert(maybe_variable);
            *maybe_variable = binary_operator_node->children[1]->value;
            maybe_variable->flags = maybe_variable->flags | GenericValue::Flags::CompileConst;
        }
    }

    resolve_operator_type(binary_operator_node);
    // Implicit casts
    auto create_cast_node = [&](int index, GenericValue::Type type) {
        auto castNode = new AST::Node(AST::Node::Type::Cast);
        castNode->value.type = type;
        castNode->parent = binary_operator_node;
        auto child = binary_operator_node->children[index];
        child->parent = nullptr;
        castNode->add_child(child);
        binary_operator_node->children[index] = castNode;
    };
    // FIXME: Cleanup, like everything else :)
    // Promotion from integer to float, either because of the binary_operator_node type, or the type of its operands.
    if(binary_operator_node->value.type == GenericValue::Type::Float ||
       ((binary_operator_node->children[0]->value.type == GenericValue::Type::Integer && binary_operator_node->children[1]->value.type == GenericValue::Type::Float) ||
        (binary_operator_node->children[0]->value.type == GenericValue::Type::Float && binary_operator_node->children[1]->value.type == GenericValue::Type::Integer))) {

        if(binary_operator_node->token.type != Tokenizer::Token::Type::Assignment && binary_operator_node->children[0]->value.type == GenericValue::Type::Integer)
            create_cast_node(0, GenericValue::Type::Float);
        if(binary_operator_node->children[1]->value.type == GenericValue::Type::Integer)
            create_cast_node(1, GenericValue::Type::Float);
    }
    // Truncation to integer (in assignments for example)
    if(binary_operator_node->value.type == GenericValue::Type::Integer) {
        if(binary_operator_node->token.type != Tokenizer::Token::Type::Assignment && binary_operator_node->children[0]->value.type == GenericValue::Type::Float)
            create_cast_node(0, GenericValue::Type::Integer);
        if(binary_operator_node->children[1]->value.type == GenericValue::Type::Float)
            create_cast_node(1, GenericValue::Type::Integer);
    }

    return true;
}

/* it must point to an type identifier
 * TODO: Handle non-built-it types.
 */
bool Parser::parse_variable_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node, bool is_const) {
    assert(it->type == Tokenizer::Token::Type::Identifier);
    assert(is_type(it->value));
    auto varDecNode = curr_node->add_child(new AST::Node(AST::Node::Type::VariableDeclaration, *it));
    auto cleanup_on_error = [&]() { delete curr_node->pop_child(); };
    varDecNode->value.type = GenericValue::parse_type(it->value);
    if(varDecNode->value.type == GenericValue::Type::Undefined) {
        varDecNode->value.type = GenericValue::Type::Composite;
        varDecNode->value.value.as_composite.type_id = get_type(it->value).id;
    }

    if(is_const)
        varDecNode->subtype = AST::Node::SubType::Const;
    ++it;
    // Array declaration
    if(it != tokens.end() && (it->type == Tokenizer::Token::Type::OpenSubscript)) {
        varDecNode->value.value.as_array.type = varDecNode->value.type;
        varDecNode->value.type = GenericValue::Type::Array;
        ++it;
        if(!parse_next_expression(tokens, it, varDecNode)) {
            cleanup_on_error();
            return false;
        }
        assert(varDecNode->children.size() == 1);
        // TODO: Check if size expression could be simplified to a constant here?
        // std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), varDecNode->value.value.as_array.capacity);
        if(it == tokens.end()) {
            error("[Parser] Syntax error: Expected ']', got end-of-document.");
            cleanup_on_error();
            return false;
        } else if(it->type != Tokenizer::Token::Type::CloseSubscript) {
            error("[Parser] Syntax error: Expected ']', got {}.", *it);
            cleanup_on_error();
            return false;
        }
        ++it;
    }
    if(it == tokens.end()) {
        error("[Parser] Syntax error: Expected variable identifier, got end-of-document.");
        cleanup_on_error();
        return false;
    }
    auto next = *it; // Hopefully a name
    if(next.type != Tokenizer::Token::Type::Identifier) {
        error("[Parser] Syntax error: Expected Identifier for variable declaration, got {}.\n", next);
        cleanup_on_error();
        return false;
    }

    varDecNode->token = next;
    if(!get_scope().declare_variable(*varDecNode, next.line)) {
        cleanup_on_error();
        return false;
    }
    ++it;
    // Also push a variable identifier for initialisation
    bool has_initializer = it != tokens.end() && (it->type == Tokenizer::Token::Type::Assignment);
    if(is_const && !has_initializer) {
        error("[Parser] Syntax error: Variable '{}' declared as const but not initialized on line {}.\n", next.value, next.line);
        cleanup_on_error();
        return false;
    }
    if(has_initializer) {
        auto varNode = curr_node->add_child(new AST::Node(AST::Node::Type::Variable, next));
        varNode->value.type = varDecNode->value.type;
        if(is_const)
            varNode->subtype = AST::Node::SubType::Const;
        if(!parse_operator(tokens, it, curr_node)) {
            cleanup_on_error();
            return false;
        }
    }

    return true;
}

bool Parser::parse_import(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    assert(it->type == Tokenizer::Token::Type::Import);
    ++it;

    // TODO: Check Cache
    if(it == tokens.end()) {
        error("[Parser] Syntax error: Module name expected after import statement, got end-of-file.\n");
        return false;
    }

    std::string module_name = std::string(it->value);
    auto        cached_interface_file = _cache_folder;
    cached_interface_file += module_name + ".int";
    std::ifstream module_file(cached_interface_file);
    if(!module_file) {
        error("[Parser] Could not find interface file for module {}.\n", module_name);
        return false;
    }

    print(" * Imported Module {}\n", module_name);

    // FIXME: File format not specified
    std::string name, type;
    while(module_file >> name >> type) {
        Tokenizer::Token token;
        token.type = Tokenizer::Token::Type::Identifier;
        token.value = *internalize_string(name);
        auto funcDecNode = _imports.emplace_back(new AST::Node(AST::Node::Type::FunctionDeclaration, token)).get(); // Keep it out of the AST
        funcDecNode->value.type = GenericValue::parse_type(type);
        print("     Imported {} : {}\n", name, type);
        if(!get_root_scope().declare_function(*funcDecNode)) {
            return false;
        }
    }

    ++it;

    return true;
}

bool Parser::write_export_interface(const std::filesystem::path& path) const {
    auto cached_interface_file = _cache_folder;
    cached_interface_file += path;
    std::ofstream interface_file(cached_interface_file);
    if(!interface_file) {
        error("[Parser] Could not open interface file {} for writing.\n", path.string());
        return false;
    }
    // FIXME: Ultra TEMP, I didn't specify a file format for the module interface.
    for(const auto& n : _exports) {
        interface_file << n->token.value << " " << serialize(n->value.type) << std::endl;
    }
    return true;
}
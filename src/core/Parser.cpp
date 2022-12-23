#include <Parser.hpp>

#include <algorithm>
#include <fstream>

#include <ModuleInterface.hpp>

std::optional<AST> Parser::parse(const std::span<Token>& tokens) {
    std::optional<AST> ast(AST{});
    try {
        bool r = parse(tokens, &(*ast).get_root());
        if(!r) {
            error("Error while parsing!\n");
            ast.reset();
        }
    } catch(const Exception& e) {
        e.display();
        ast.reset();
    }
    return ast;
}

// Append to an existing AST and return the added children
AST::Node* Parser::parse(const std::span<Token>& tokens, AST& ast) {
    // Adds a dummy root node to easily get rid of it on error.
    auto root = ast.get_root().add_child(new AST::Node{AST::Node::Type::Root});
    bool r = parse(tokens, root);
    if(!r) {
        error("Error while parsing!\n");
        delete ast.get_root().pop_child();
        return nullptr;
    }
    return root;
}

std::vector<std::string> Parser::parse_dependencies(const std::span<Token>& tokens) {
    std::vector<std::string> dependencies;

    auto it = tokens.begin();
    while(it != tokens.end()) {
        if(it->type == Token::Type::Import) {
            ++it;
            if(it->type != Token::Type::StringLiteral)
                throw Exception(fmt::format("[Parser] Error listing dependencies: Expected a StringLiteral after import statement, got {}.", *it), point_error(*it));
            dependencies.push_back(std::string(it->value));
        }
        ++it;
    }

    return dependencies;
}

TypeID Parser::resolve_operator_type(Token::Type op, TypeID lhs, TypeID rhs) {
    using enum Token::Type;
    if(op == MemberAccess)
        return rhs;
    if(op == Assignment)
        return lhs;
    if(op == Equal || op == Different || op == Lesser || op == Greater || op == GreaterOrEqual || op == LesserOrEqual || op == And || op == Or)
        return PrimitiveType::Boolean;

    if(op == OpenSubscript) {
        if(lhs == PrimitiveType::CString)
            return PrimitiveType::Char;
        auto lhs_type = GlobalTypeRegistry::instance().get_type(lhs);
        if(lhs_type->is_array())
            return dynamic_cast<const ArrayType*>(lhs_type)->element_type;
        if(lhs != PrimitiveType::Pointer && lhs_type->is_pointer())
            return dynamic_cast<const PointerType*>(lhs_type)->pointee_type;
    }

    if(lhs == rhs && is_primitive(lhs))
        return lhs;

    // Promote integer to Float
    if(is_primitive(lhs) && is_primitive(rhs) &&
       ((lhs == PrimitiveType::Float && rhs == PrimitiveType::Integer) || (lhs == PrimitiveType::Integer && rhs == PrimitiveType::Float))) {
        return PrimitiveType::Float;
    }

    return InvalidTypeID;
}

bool Parser::parse(const std::span<Token>& tokens, AST::Node* curr_node) {
    curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Statement));
    auto it = tokens.begin();
    while(it != tokens.end()) {
        const auto& token = *it;
        switch(token.type) {
            case Token::Type::OpenScope: {
                curr_node = curr_node->add_child(new AST::Scope(*it));
                push_scope();
                ++it;
                break;
            }
            case Token::Type::CloseScope: {
                assert(false); // FIXME: I don't think this case is needed, it should already be taken care of and always return an error.
                while(curr_node->type != AST::Node::Type::Scope && curr_node->parent != nullptr)
                    curr_node = curr_node->parent;
                if(curr_node->type != AST::Node::Type::Scope)
                    throw Exception(fmt::format("[Parser] Syntax error: Unmatched '}}' on line {}.\n", it->line), point_error(*it));
                curr_node->type_id = get_scope().get_return_type();
                curr_node = curr_node->parent;
                pop_scope();
                ++it;
                break;
            }
            case Token::Type::EndStatement: {
                curr_node = curr_node->parent;
                curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Statement, *it));
                ++it;
                break;
            }
            case Token::Type::If: {
                if(!peek(tokens, it, Token::Type::OpenParenthesis)) {
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

                if(it != tokens.end() && it->type == Token::Type::Else) {
                    ++it;
                    if(!parse_scope_or_single_statement(tokens, it, ifNode)) {
                        error("[Parser] Syntax error: Expected 'new scope' or single statement after 'else'.\n");
                        delete curr_node->pop_child();
                        return false;
                    }
                }
                break;
            }
            case Token::Type::Let:
                ++it;
                assert(it->type == Token::Type::Identifier);
                parse_variable_declaration(tokens, it, curr_node, false);
                break;
            case Token::Type::Const:
                ++it;
                assert(it->type == Token::Type::Identifier);
                parse_variable_declaration(tokens, it, curr_node, true);
                break;
            case Token::Type::Return: {
                std::unique_ptr<AST::Node> return_node(new AST::Node(AST::Node::Type::ReturnStatement, *it));
                ++it;
                if(it->type == Token::Type::EndStatement) {
                    return_node->type_id = PrimitiveType::Void;
                    curr_node->add_child(return_node.release());
                    break;
                }
                auto to_rvalue = return_node->add_child(new AST::Node(AST::Node::Type::LValueToRValue));
                if(!parse_next_expression(tokens, it, to_rvalue))
                    return false;

                // FIXME: Detect return of structs to mark them as 'moved' and avoid calling their destructor.
                //        There's probably a more elegant way to do this... And it will probably not be the only way to move a local value.
                AST::VariableDeclaration* return_variable = nullptr;
                if(to_rvalue->children[0]->type == AST::Node::Type::Variable) {
                    auto ret_type = GlobalTypeRegistry::instance().get_type(to_rvalue->children[0]->type_id);
                    if(ret_type->is_struct()) {
                        return_variable = get(to_rvalue->children[0]->token.value);
                        if(!return_variable) {
                            warn("[Parser] Uh?! Returning a non-existant variable '{}' ?\n", to_rvalue->children[0]->token.value);
                        } else {
                            // FIXME: This could be completely fine for type without destructors (or with another set of compile time constraints? Traits?)
                            if(return_variable->flags & AST::VariableDeclaration::Flag::Moved)
                                throw Exception(fmt::format("[Parser] Returning variable '{}' which was already moved!\n", return_variable->token.value),
                                                point_error(return_node->token));

                            return_variable->flags |= AST::VariableDeclaration::Flag::Moved;
                        }
                    }
                }

                insert_defer_node(curr_node);

                // The return value is moved only if this return is actually taken, restore the original value for the rest of this scope.
                if(return_variable)
                    return_variable->flags |= AST::VariableDeclaration::Flag::Moved;

                to_rvalue->type_id = to_rvalue->children[0]->type_id;
                return_node->type_id = return_node->children[0]->type_id;
                auto scope_return_type = get_scope().get_return_type();
                if(scope_return_type == InvalidTypeID) {
                    get_scope().set_return_type(return_node->type_id);
                } else if(scope_return_type != return_node->type_id) {
                    throw Exception(fmt::format("[Parser] Syntax error: Incoherent return types, got {}, expected {}.\n",
                                                GlobalTypeRegistry::instance().get_type(return_node->type_id)->designation, scope_return_type),
                                    point_error(return_node->token));
                }

                curr_node->add_child(return_node.release());
                break;
            }

            // Constants
            case Token::Type::Boolean: {
                if(!parse_boolean(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Digits: {
                if(!parse_digits(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Float: {
                if(!parse_float(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::CharLiteral: {
                if(!parse_char(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::StringLiteral: {
                if(!parse_string(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Assignment: [[fallthrough]];
            case Token::Type::Xor: [[fallthrough]];
            case Token::Type::Or: [[fallthrough]];
            case Token::Type::And: [[fallthrough]];
            case Token::Type::Equal: [[fallthrough]];
            case Token::Type::Different: [[fallthrough]];
            case Token::Type::Lesser: [[fallthrough]];
            case Token::Type::LesserOrEqual: [[fallthrough]];
            case Token::Type::Greater: [[fallthrough]];
            case Token::Type::GreaterOrEqual: [[fallthrough]];
            case Token::Type::Addition: [[fallthrough]];
            case Token::Type::Substraction: [[fallthrough]];
            case Token::Type::Multiplication: [[fallthrough]];
            case Token::Type::Division: [[fallthrough]];
            case Token::Type::Modulus: [[fallthrough]];
            case Token::Type::Increment: [[fallthrough]];
            case Token::Type::Decrement: [[fallthrough]];
            case Token::Type::OpenParenthesis: [[fallthrough]];
            case Token::Type::CloseParenthesis: [[fallthrough]];
            case Token::Type::OpenSubscript: [[fallthrough]];
            case Token::Type::CloseSubscript: [[fallthrough]];
            case Token::Type::MemberAccess: {
                if(!parse_operator(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Identifier: {
                if(!parse_identifier(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::While: {
                if(!parse_while(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::For: {
                if(!parse_for(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Function: {
                if(!parse_function_declaration(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Extern: {
                ++it;
                if(!parse_function_declaration(tokens, it, curr_node, AST::FunctionDeclaration::Flag::Extern))
                    return false;
                break;
            }
            case Token::Type::Type: {
                if(!parse_type_declaration(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Import: {
                if(!parse_import(tokens, it, curr_node))
                    return false;
                break;
            }
            case Token::Type::Export: {
                ++it;
                auto function_flags = AST::FunctionDeclaration::Flag::Exported;
                // TODO: Handle exported variables too.
                switch(it->type) {
                    case Token::Type::Extern:
                        ++it;
                        function_flags |= AST::FunctionDeclaration::Flag::Extern;
                        [[fallthrough]];
                    case Token::Type::Function: {
                        parse_function_declaration(tokens, it, curr_node, function_flags);
                        _module_interface.exports.push_back(dynamic_cast<AST::FunctionDeclaration*>(curr_node->children.back()));
                        break;
                    }
                    case Token::Type::Type: {
                        if(!parse_type_declaration(tokens, it, curr_node))
                            return false;
                        // Also export the default (auto-generated) constructor, if there's one.
                        // FIXME: Hackish, as always.
                        if(curr_node->children.back()->type == AST::Node::Type::FunctionDeclaration) {
                            auto constructor = dynamic_cast<AST::FunctionDeclaration*>(curr_node->children.back());
                            constructor->flags |= AST::FunctionDeclaration::Flag::Exported;
                            _module_interface.exports.push_back(constructor);
                            _module_interface.type_exports.push_back(dynamic_cast<AST::TypeDeclaration*>(curr_node->children[curr_node->children.size() - 2]));
                        } else
                            _module_interface.type_exports.push_back(dynamic_cast<AST::TypeDeclaration*>(curr_node->children.back()));
                        break;
                    }
                    case Token::Type::Let:
                    case Token::Type::Const: {
                        throw Exception("[Parser] Variable export not yet implemented!");
                        break;
                    }
                    default: {
                        throw Exception(fmt::format("[Parser] Unexpected token {} after export keyword.", it->value));
                    }
                }
                break;
            }
            case Token::Type::Comment: ++it; break;
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

bool Parser::parse_next_scope(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    if(it == tokens.end() || it->type != Token::Type::OpenScope) {
        if(it == tokens.end())
            error("[Parser] Syntax error: Expected scope opening, got end-of-document.\n");
        else
            error("[Parser] Syntax error: Expected scope opening on line {}, got {}.\n", it->line, it->value);
        return false;
    }
    auto   begin = it + 1;
    auto   end = it;
    size_t opened_scopes = 0;
    Token  last_closing;
    while(end != tokens.end()) {
        if(end->type == Token::Type::OpenScope)
            ++opened_scopes;
        if(end->type == Token::Type::CloseScope) {
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

    auto scope = new AST::Scope(*it);
    curr_node->add_child(scope);
    push_scope();
    bool r = parse({begin, end}, scope);
    scope->type_id = get_scope().get_return_type();

    pop_scope();
    it = end + 1;
    return r;
}

// TODO: Formely define wtf is an expression :)
bool Parser::parse_next_expression(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node, uint32_t precedence, bool search_for_matching_bracket) {
    if(it == tokens.end()) {
        error("[Parser] Expected expression, got end-of-file.\n");
        return false;
    }

    // Temporary expression node. Will be replaced by its child when we're done parsing it.
    auto exprNode = curr_node->add_child(new AST::Node(AST::Node::Type::Expression));

    if(it->type == Token::Type::OpenParenthesis) {
        ++it;
        if(!parse_next_expression(tokens, it, exprNode, max_precedence, true)) {
            delete curr_node->pop_child();
            return false;
        }
    }

    bool stop = false;

    while(it != tokens.end() && !(it->type == Token::Type::EndStatement || it->type == Token::Type::Comma) && !stop) {
        // TODO: Check other types!
        switch(it->type) {
            using enum Token::Type;
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

    if(search_for_matching_bracket && (it == tokens.end() || it->type != Token::Type::CloseParenthesis)) {
        if(it == tokens.end())
            error("[Parser] Unmatched '(' after reaching end-of-document.\n");
        else
            error("[Parser] Unmatched '(' on line {}.\n", it->line);
        delete curr_node->pop_child();
        return false;
    }

    if(exprNode->children.size() != 1)
        throw Exception("[Parser] Invalid expression ended here:", point_error(*it));

    curr_node->pop_child();
    auto child = exprNode->pop_child();
    curr_node->add_child(child);
    delete exprNode;

    if(search_for_matching_bracket) // Skip ending bracket
        ++it;

    return true;
}

bool Parser::parse_identifier(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    assert(it->type == Token::Type::Identifier);

    // Function Call
    // FIXME: Should be handled by parse_operator for () to be a generic operator!
    //        Or realise that this is a function, somehow (keep track of declaration).
    if(peek(tokens, it, Token::Type::OpenParenthesis)) {
        // TODO: Check if the function has been declared (or is a built-in?) & Fetch corresponding FunctionDeclaration Node.
        curr_node->add_child(new AST::Node(AST::Node::Type::Variable, *it)); // FIXME: Use another node type
        ++it;
        return true;
    }

    auto maybe_variable = get(it->value);
    if(!maybe_variable)
        throw Exception(fmt::format("[Parser] Syntax Error: Variable '{}' has not been declared.\n", it->value), point_error(*it));
    const auto& variable = *maybe_variable;

    auto variable_node = new AST::Variable(*it);
    curr_node->add_child(variable_node);
    variable_node->type_id = variable.type_id;

    if(peek(tokens, it, Token::Type::OpenSubscript)) { // Array accessor
        auto type = GlobalTypeRegistry::instance().get_type(variable.type_id);
        if(!(variable.type_id == PrimitiveType::CString) && !type->is_array() && !type->is_pointer())
            throw Exception(fmt::format("[Parser] Syntax Error: Subscript operator on variable '{}' which is neither a string nor an array, nor a pointer.\n", it->value),
                            point_error(*it));
        auto access_operator_node = new AST::BinaryOperator(*(it + 1));
        access_operator_node->add_child(curr_node->pop_child());
        curr_node->add_child(access_operator_node);

        // FIXME: Get rid of this special case? (And the string type?)
        if(variable.type_id == PrimitiveType::CString)
            access_operator_node->type_id = PrimitiveType::Char;
        else if(type->is_pointer()) // FIXME: Won't work for string. But we'll probably get rid of it anyway.
            access_operator_node->type_id = dynamic_cast<const PointerType*>(type)->pointee_type;
        else // FIXME: Won't work for string. But we'll probably get rid of it anyway.
            access_operator_node->type_id = dynamic_cast<const ArrayType*>(type)->element_type;

        auto ltor = new AST::Node(AST::Node::Type::LValueToRValue);
        access_operator_node->add_child(ltor);

        it += 2;
        // Get the index and add it as a child.
        // FIXME: Search the matching ']' here?
        parse_next_expression(tokens, it, ltor, max_precedence, false);

        ltor->type_id = ltor->children[0]->type_id;

        // TODO: Make sure this is an integer?
        if(access_operator_node->children.back()->type_id != PrimitiveType::Integer &&
           !(access_operator_node->children.back()->type_id >= PrimitiveType::U8 && access_operator_node->children.back()->type_id <= PrimitiveType::I64)) {
            warn("[Parser] Subscript operator called with a non integer argument: {}", point_error(access_operator_node->token));
            auto cast_node = access_operator_node->insert_between(access_operator_node->children.size() - 1, new AST::Node(AST::Node::Type::Cast));
            cast_node->type_id = PrimitiveType::Integer;
        }

        expect(tokens, it, Token::Type::CloseSubscript);
    } else {
        ++it;
    }
    return true;
}

bool Parser::parse_statement(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    // FIXME: There are probably a lot more tokens that should stop a single statement (first example would be an 'else' after an if without an open block).
    //        Does the list of stopping keywords depend on the context?
    auto end = it;
    while(end != tokens.end() && end->type != Token::Type::EndStatement)
        ++end;
    // Include ',' in the sub-parsing
    if(end != tokens.end())
        ++end;
    if(!parse({it, end}, curr_node))
        return false;
    it = end;
    return true;
}

bool Parser::parse_scope_or_single_statement(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    if(it->type == Token::Type::OpenScope) {
        if(!parse_next_scope(tokens, it, curr_node))
            return false;
    } else {
        if(!parse_statement(tokens, it, curr_node))
            return false;
    }
    return true;
}

bool Parser::parse_while(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto whileNode = curr_node->add_child(new AST::Node(AST::Node::Type::WhileStatement, *it));
    ++it;
    if(it->type != Token::Type::OpenParenthesis) {
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

bool Parser::parse_for(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto forNode = curr_node->add_child(new AST::Node(AST::Node::Type::ForStatement, *it));
    ++it;
    if(it->type != Token::Type::OpenParenthesis) {
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

bool Parser::parse_method_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    return parse_function_declaration(tokens, it, curr_node);
}

bool Parser::parse_function_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node, AST::FunctionDeclaration::Flag flags) {
    expect(tokens, it, Token::Type::Function);
    if(it->type != Token::Type::Identifier)
        throw Exception(fmt::format("[Parser] Expected identifier in function declaration, got {}.\n", *it), point_error(*it));
    auto function_node = new AST::FunctionDeclaration(*it);
    curr_node->add_child(function_node);
    function_node->token.value = *internalize_string(std::string(it->value));

    if((flags & AST::FunctionDeclaration::Flag::Exported) || function_node->name() == "main")
        function_node->flags |= AST::FunctionDeclaration::Flag::Exported;

    ++it;

    // Declare the function immediatly to allow recursive calls.
    if(!get_scope().declare_function(*function_node))
        throw Exception(fmt::format("[Parser] Syntax error: Function '{}' already declared in this scope.\n", function_node->name()), point_error(function_node->token));

    bool templated = false;
    if(it->type == Token::Type::Lesser) {
        declare_template_types(tokens, it);
        templated = true;
    }

    if(it->type != Token::Type::OpenParenthesis)
        throw Exception(fmt::format("Expected '(' in function declaration, got {}.\n", *it), point_error(*it));

    push_scope(); // FIXME: Restrict function parameters to this scope, do better.

    ++it;
    // Parse parameters
    while(it != tokens.end() && it->type != Token::Type::CloseParenthesis) {
        parse_variable_declaration(tokens, it, function_node, false, false);
        if(it->type == Token::Type::Comma)
            ++it;
        else if(it->type != Token::Type::CloseParenthesis)
            throw Exception(fmt::format("[Parser] Expected ',' in function declaration argument list, got {}.\n", *it), point_error(*it));
    }
    expect(tokens, it, Token::Type::CloseParenthesis);

    if(it == tokens.end())
        throw Exception("[Parser] Expected function body, got end-of-file.\n");

    if(it->type == Token::Type::Colon) {
        ++it;
        if(it->type != Token::Type::Identifier || !is_type(it->value))
            throw Exception(fmt::format("[Parser] Expected type identifier after function '{}' declaration body, got '{}'.\n", function_node->token.value, it->value),
                            point_error(*it));
        function_node->type_id = parse_type(tokens, it, curr_node);
    }

    // FIXME: Hackish this.
    if(function_node->children.size() > 0)
        get_scope().set_this(get(function_node->children[0]->token.value));

    if(flags & AST::FunctionDeclaration::Flag::Extern) {
        function_node->flags |= AST::FunctionDeclaration::Flag::Extern;
        if(function_node->type_id == InvalidTypeID)
            function_node->type_id = PrimitiveType::Void;
    } else {
        // Function body
        parse_scope_or_single_statement(tokens, it, function_node);

        // Return type deduction
        auto return_type = function_node->body()->type_id;
        if(return_type == InvalidTypeID)
            return_type = PrimitiveType::Void;
        if(function_node->type_id != InvalidTypeID && function_node->type_id != return_type)
            throw Exception(fmt::format("[Parser] Syntax error: Incoherent return types for function {}, got {}, expected {}.\n", function_node->token.value, return_type,
                                        function_node->type_id),
                            point_error(function_node->body()->token));
        function_node->type_id = return_type;
    }

    pop_scope();

    return true;
}

std::vector<TypeID> Parser::parse_template_types(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    assert(it->type == Token::Type::Lesser);
    ++it;
    std::vector<TypeID> typenames;
    while(it != tokens.end() && it->type != Token::Type::Greater) {
        auto scoped_type_id = parse_type(tokens, it, curr_node);
        typenames.push_back(scoped_type_id);
        skip(tokens, it, Token::Type::Comma);
    }
    expect(tokens, it, Token::Type::Greater);

    return typenames;
}

std::vector<std::string> Parser::declare_template_types(const std::span<Token>& tokens, std::span<Token>::iterator& it) {
    assert(it->type == Token::Type::Lesser);
    ++it;
    std::vector<std::string> typenames;
    while(it != tokens.end() && it->type != Token::Type::Greater) {
        if(it->type != Token::Type::Identifier)
            throw Exception(fmt::format("[Parser] Expected type identifier in template declaration, got '{}'.", *it), point_error(*it));
        typenames.push_back(std::string(it->value));
        get_scope().declare_template_placeholder_type(std::string(it->value));
        ++it;
        skip(tokens, it, Token::Type::Comma);
    }
    expect(tokens, it, Token::Type::Greater);

    return typenames;
}

bool Parser::parse_type_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    expect(tokens, it, Token::Type::Type);
    if(it->type != Token::Type::Identifier)
        throw Exception(fmt::format("Expected identifier in type declaration, got {}.\n", it->value), point_error(*it));

    const auto type_token = *it;
    ++it;

    AST::TypeDeclaration*    type_node = new AST::TypeDeclaration(type_token);
    bool                     templated_type = false;
    std::vector<std::string> template_typenames;

    if(it->type == Token::Type::Lesser) {
        template_typenames = declare_template_types(tokens, it);
        templated_type = true;
    }

    curr_node->add_child(type_node);

    push_scope();

    if(it->type != Token::Type::OpenScope)
        throw Exception(fmt::format("Expected '{{' after type declaration, got {}.\n", it->value), point_error(*it));
    ++it;

    std::vector<AST::Node*> default_values;
    std::vector<AST::Node*> constructors;
    bool                    has_at_least_one_default_value = false;

    while(it != tokens.end() && !(it->type == Token::Type::CloseScope)) {
        bool const_var = false;
        switch(it->type) {
            case Token::Type::Function: {
                // Note: Added to curr_node, not type_node. Right now methods are not special.
                parse_method_declaration(tokens, it, curr_node);
                break;
            }
            case Token::Type::Const: const_var = true; [[fallthrough]];
            case Token::Type::Let: {
                ++it;
                parse_variable_declaration(tokens, it, type_node, const_var);
                skip(tokens, it, Token::Type::EndStatement);

                // parse_variable_declaration may add an initialisation node, we'll extract the default value and use it to create a default constructor.
                if(type_node->children.back()->type == AST::Node::Type::BinaryOperator) {
                    auto assignment_node = type_node->pop_child();
                    auto rhs = assignment_node->pop_child();
                    default_values.push_back(rhs);
                    delete assignment_node;
                    has_at_least_one_default_value = true;
                    constructors.push_back(nullptr);
                } else if(type_node->children.back()->type == AST::Node::Type::FunctionCall) {
                    // parse_variable_declaration generated a constructor call
                    auto constructor_node = type_node->pop_child();
                    constructors.push_back(constructor_node);
                    has_at_least_one_default_value = true;
                    default_values.push_back(nullptr);
                } else {
                    default_values.push_back(nullptr); // Still push a null node to keep the indices in sync
                    constructors.push_back(nullptr);
                }
                break;
            }
            case Token::Type::Comment: ++it; break;
            default: throw Exception(fmt::format("[Parser] Unexpected token '{}' in type declaration.\n", it->value), point_error(*it));
        }
    }
    pop_scope();

    // Note: Since we're declaring the type after parsing it (because we need the members to be established before the call to declare_type right now),
    //       types cannot reference themselves.
    /* if(templated_type) {
        if(!get_scope().declare_templated_type(*type_node, template_typenames)) {
            warn("[Parser] Syntax error: Type {} already declared in this scope.\n", type_node->token.value);
            fmt::print("{}", point_error(type_node->token));
        }
    } else */
    if(!get_scope().declare_type(*type_node)) {
        warn("[Parser] Syntax error: Type {} already declared in this scope.\n", type_node->token.value);
        fmt::print("{}", point_error(type_node->token));
    }

    // FIXME: Generate templated constructor function for templated types.
    if(has_at_least_one_default_value) {
        // Declare a default constructor.
        // FIXME: This could probably be way more elegant, rather then contructing the AST by hand...
        auto this_token = Token(Token::Type::Identifier, *internalize_string("this"), 0, 0);
        auto function_node = new AST::FunctionDeclaration(Token(Token::Type::Identifier, *internalize_string("constructor"), 0, 0));
        curr_node->add_child(function_node);
        function_node->type_id = PrimitiveType::Void;
        auto this_declaration_node = function_node->add_child(new AST::VariableDeclaration(this_token));
        this_declaration_node->type_id = GlobalTypeRegistry::instance().get_pointer_to(type_node->type_id);
        auto function_scope = function_node->add_child(new AST::Scope());

        auto type = dynamic_cast<const StructType*>(GlobalTypeRegistry::instance().get_type(type_node->type_id));

        for(auto idx = 0; idx < type->members.size(); ++idx) {
            if(default_values[idx]) {
                auto assignment = new AST::BinaryOperator(Token(Token::Type::Assignment, *internalize_string("="), 0, 0));
                function_scope->add_child(assignment);
                auto member_access = new AST::BinaryOperator(Token(Token::Type::MemberAccess, *internalize_string("."), 0, 0));
                assignment->add_child(member_access);
                auto dereference = new AST::Node(AST::Node::Type::Dereference);
                member_access->add_child(dereference);
                dereference->type_id = type_node->type_id;
                auto variable = dereference->add_child(new AST::Variable(this_token));
                variable->type_id = this_declaration_node->type_id;
                auto member_identifier = new AST::MemberIdentifier(Token(Token::Type::Identifier, *internalize_string(std::string(type_node->members()[idx]->token.value)), 0, 0));
                member_access->add_child(member_identifier);
                member_identifier->index = idx;
                member_identifier->type_id = type_node->members()[idx]->type_id;
                assignment->add_child(default_values[idx]);
                resolve_operator_type(member_access);
                resolve_operator_type(assignment);
            }
            if(constructors[idx])
                function_scope->add_child(constructors[idx]);
        }

        if(!get_scope().declare_function(*function_node))
            throw Exception(fmt::format("[Parser] Syntax error: Function '{}' already declared in this scope.\n", function_node->name()));
    }

    expect(tokens, it, Token::Type::CloseScope);
    return true;
}

AST::BoolLiteral* Parser::parse_boolean(const std::span<Token>&, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto boolNode = new AST::BoolLiteral(*it);
    curr_node->add_child(boolNode);
    boolNode->value = it->value == "true";
    ++it;
    return boolNode;
}

AST::IntegerLiteral* Parser::parse_digits(const std::span<Token>&, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto integer = new AST::IntegerLiteral(*it);
    curr_node->add_child(integer);
    auto [ptr, error_code] = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), integer->value);
    if(error_code == std::errc::invalid_argument)
        throw Exception("[Parser::parse_digits] std::from_chars returned invalid_argument.\n", point_error(*it));
    else if(error_code == std::errc::result_out_of_range)
        throw Exception("[Parser::parse_digits] std::from_chars returned result_out_of_range.\n", point_error(*it));
    ++it;
    return integer;
}

AST::FloatLiteral* Parser::parse_float(const std::span<Token>&, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto floatNode = new AST::FloatLiteral(*it);
    curr_node->add_child(floatNode);
    auto [ptr, error_code] = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), floatNode->value);
    if(error_code == std::errc::invalid_argument)
        throw Exception("[Parser::parse_float] std::from_chars returned invalid_argument.\n", point_error(*it));
    else if(error_code == std::errc::result_out_of_range)
        throw Exception("[Parser::parse_float] std::from_chars returned result_out_of_range.\n", point_error(*it));
    ++it;
    return floatNode;
}

AST::CharLiteral* Parser::parse_char(const std::span<Token>&, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto strNode = new AST::CharLiteral(*it);
    curr_node->add_child(strNode);
    strNode->value = it->value[0];
    ++it;
    return strNode;
}

bool Parser::parse_string(const std::span<Token>&, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto strNode = new AST::StringLiteral(*it);
    curr_node->add_child(strNode);

    if(it->value.find('\\') != it->value.npos) {
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
                    default: throw Exception(fmt::format("[Parser] Unknown escape sequence \\{} in string.\n", ch), point_error(*it));
                }
            } else
                str += ch;
        }
        strNode->value = *internalize_string(str);
    } else
        strNode->value = it->value;
    ++it;
    return true;
}

bool Parser::parse_function_arguments(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    assert(it->type == Token::Type::OpenParenthesis);
    it++;
    assert(curr_node->type == AST::Node::Type::FunctionCall);
    const auto start = (it + 1);
    // Parse arguments
    while(it != tokens.end() && it->type != Token::Type::CloseParenthesis) {
        auto arg_index = curr_node->children.size();
        parse_next_expression(tokens, it, curr_node);
        auto to_rvalue = curr_node->insert_between(arg_index, new AST::Node(AST::Node::Type::LValueToRValue, curr_node->token));
        to_rvalue->type_id = to_rvalue->children[0]->type_id;
        skip(tokens, it, Token::Type::Comma);
    }
    expect(tokens, it, Token::Type::CloseParenthesis);
    return true;
}

void Parser::check_function_call(AST::FunctionCall* call_node, const AST::FunctionDeclaration* function) {
    auto function_flags = function->flags;
    if(!(function_flags & AST::FunctionDeclaration::Flag::Variadic) && call_node->arguments().size() != function->arguments().size()) {
        throw Exception(fmt::format("[Parser] Function '{}' expects {} argument(s), got {}.\n", function->name(), function->arguments().size(), call_node->arguments().size()),
                        point_error(call_node->token));
    }
    // Type Check
    // FIXME: Do better.
    if(!(function_flags & AST::FunctionDeclaration::Flag::Variadic))
        for(auto i = 0; i < call_node->arguments().size(); ++i) {
            if(call_node->arguments()[i]->type_id != function->arguments()[i]->type_id) {
                throw Exception(fmt::format("[Parser] Function '{}' expects an argument of type {} on position #{}, got {}.\n", function->name(),
                                            type_id_to_string(function->arguments()[i]->type_id), i, type_id_to_string(call_node->arguments()[i]->type_id),
                                            point_error(call_node->arguments()[i]->token)),
                                point_error(call_node->token));
            }
        }
}

std::string get_overloads_hint_string(const AST::FunctionCall* call_node, const std::vector<const AST::FunctionDeclaration*>& candidates) {
    std::string used_types = "Called with: " + std::string(call_node->token.value) + "(";
    for(auto i = 0; i < call_node->arguments().size(); ++i)
        used_types += (i > 0 ? ", " : "") + type_id_to_string(call_node->arguments()[i]->type_id);
    used_types += ")\n";
    std::string candidates_display = "Candidates are:\n";
    for(const auto& func : candidates) {
        candidates_display += "\t" + std::string(func->name()) + "("; // TODO: We can probably do better.
        for(auto i = 0; i < func->arguments().size(); ++i)
            candidates_display += (i > 0 ? ", " : "") +
                                  (func->arguments()[i]->token.value.size() > 0 ? std::string(func->arguments()[i]->token.value) : ("#" + std::to_string(i))) + " : " +
                                  type_id_to_string(func->arguments()[i]->type_id);
        candidates_display += ") : " + type_id_to_string(func->type_id) + "\n";
    }
    return used_types + candidates_display;
}

bool Parser::parse_operator(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto operator_type = it->type;
    // Unary operators
    if(is_unary_operator(operator_type) && curr_node->children.empty()) {
        auto unary_operator_node = new AST::UnaryOperator(*it);
        unary_operator_node->flags |= AST::UnaryOperator::Flag::Prefix;
        curr_node->add_child(unary_operator_node);
        auto precedence = operator_precedence.at(operator_type);
        ++it;
        parse_next_expression(tokens, it, unary_operator_node, precedence);
        resolve_operator_type(unary_operator_node);
        return true;
    }

    if((operator_type == Token::Type::Increment || operator_type == Token::Type::Decrement) && !curr_node->children.empty()) {
        auto prev_node = curr_node->pop_child();
        auto unary_operator_node = new AST::UnaryOperator(*it);
        unary_operator_node->flags |= AST::UnaryOperator::Flag::Postfix;
        curr_node->add_child(unary_operator_node);
        // auto   precedence = operator_precedence.at(std::string(it->value));
        // FIXME: How do we use the precedence here?
        unary_operator_node->add_child(prev_node);
        ++it;
        resolve_operator_type(unary_operator_node);
        return true;
    }

    // '(', but not a function declaration or call operator.
    if(operator_type == Token::Type::OpenParenthesis && curr_node->children.empty())
        return parse_next_expression(tokens, it, curr_node);

    // Should have been handled by others parsing functions.
    if(operator_type == Token::Type::CloseParenthesis)
        throw Exception(fmt::format("[Parser::parse_operator] Unmatched ')' on line {}.\n", it->line), point_error(*it));

    // Function call
    if(operator_type == Token::Type::OpenParenthesis) {
        auto function_node = curr_node->pop_child();
        auto call_node = new AST::FunctionCall(function_node->token);
        curr_node->add_child(call_node);
        call_node->add_child(function_node);

        // TODO: Check type of function_node (is it actually callable?)
        if(function_node->type != AST::Node::Type::Variable)
            throw Exception(fmt::format("[Parser] '{}' doesn't seem to be callable (may be a missing implementation).\n", function_node->token.value), point_error(*it));

        // FIXME: FunctionCall uses its token for now to get the function name, but this in incorrect, it should look at the
        // first child and execute it to get a reference to the function. Using the function name token as a temporary workaround.
        auto function_name = function_node->token.value;

        parse_function_arguments(tokens, it, call_node);

        auto resolved_function = get_function(function_name, call_node->arguments());
        if(!resolved_function) {
            auto candidates = get_functions(function_name);
            if(candidates.size() == 0)
                throw Exception(fmt::format("[Parser] Call to undefined function '{}'.\n", function_name), point_error(call_node->token));
            else {
                auto hint = get_overloads_hint_string(call_node, candidates);
                throw Exception(fmt::format("[Parser] Call to undefined function '{}', no candidate matches the arguments types.\n", function_name),
                                point_error(call_node->token) + hint);
            }
        }

        // Automatically cast any pointer to 'opaque' pointer type for interfacing with C++
        // Automatically cast to larger types (always safe)
        for(auto i = 0; i < std::min(resolved_function->arguments().size(), call_node->arguments().size()); ++i) {
            if(resolved_function->arguments()[i]->type_id == PrimitiveType::Pointer && GlobalTypeRegistry::instance().get_type(call_node->arguments()[i]->type_id)->is_pointer()) {
                auto cast_node = new AST::Node(AST::Node::Type::Cast);
                cast_node->type_id = PrimitiveType::Pointer;
                call_node->insert_before_argument(i, cast_node);
            }

            // FIXME: Consolidate these automatic casts
            const auto allow_automatic_cast = [&](PrimitiveType to, const std::vector<PrimitiveType>& from) {
                if(resolved_function->arguments()[i]->type_id == to) {
                    for(auto type_id : from) {
                        if(call_node->arguments()[i]->type_id == type_id) {
                            auto cast_node = new AST::Node(AST::Node::Type::Cast);
                            cast_node->type_id = to;
                            call_node->insert_before_argument(i, cast_node);
                            break;
                        }
                    }
                }
            };
            allow_automatic_cast(PrimitiveType::U64, {PrimitiveType::Integer, PrimitiveType::U32, PrimitiveType::U16, PrimitiveType::U8});
            allow_automatic_cast(PrimitiveType::U32, {PrimitiveType::Integer, PrimitiveType::U16, PrimitiveType::U8});
            allow_automatic_cast(PrimitiveType::U16, {PrimitiveType::U8});
            allow_automatic_cast(PrimitiveType::I64, {PrimitiveType::Integer, PrimitiveType::I32, PrimitiveType::I16, PrimitiveType::I8});
            allow_automatic_cast(PrimitiveType::I32, {PrimitiveType::Integer, PrimitiveType::I16, PrimitiveType::I8});
            allow_automatic_cast(PrimitiveType::I16, {PrimitiveType::I8});
        }

        call_node->type_id = resolved_function->type_id;
        call_node->flags = resolved_function->flags;

        check_function_call(call_node, resolved_function);

        return true;
    }

    // Implicit 'this'
    // FIXME: This probably shouldn't work this way.
    //        We have to check the return type of the last child node to not try to apply the MemberAccess operator to a Statement, for example, which is pretty wack.
    if((curr_node->children.empty() || curr_node->children.back()->type_id == InvalidTypeID || curr_node->children.back()->type_id == Void) &&
       operator_type == Token::Type::MemberAccess) {
        auto t = get_this();
        if(!t)
            throw Exception(fmt::format("[Parser] Syntax error: Implicit 'this' access, but 'this' is not defined here.\n", *it), point_error(*it));
        Token token = *it;
        token.value = *internalize_string("this");
        auto this_node = new AST::Variable(token);
        this_node->type_id = t->type_id;
        curr_node->add_child(this_node);

        auto type = GlobalTypeRegistry::instance().get_type(t->type_id);
        // Allow using the dot operator on pointers and directly on an object
        if(type->is_pointer()) {
            auto ltor = new AST::Node(AST::Node::Type::Dereference);
            ltor->type_id = dynamic_cast<const PointerType*>(type)->pointee_type;
            curr_node->insert_between(curr_node->children.size() - 1, ltor);
        }
    }

    if(curr_node->children.empty())
        throw Exception(fmt::format("[Parser::parse_operator] Syntax error: unexpected binary operator: {}.\n", *it), point_error(*it));

    AST::Node* prev_expr = curr_node->pop_child();
    // TODO: Test type of previous node! (Must be an expression resolving to something operable)
    auto binary_operator_node = new AST::BinaryOperator(*it);
    curr_node->add_child(binary_operator_node);

    binary_operator_node->add_child(prev_expr);

    auto precedence = operator_precedence.at(operator_type);
    ++it;
    if(it == tokens.end())
        throw Exception(fmt::format("[Parser::parse_operator] Syntax error: Reached end of document without a right-hand side operand for {}.\n", it->value));

    if(operator_type == Token::Type::OpenSubscript) {
        parse_next_expression(tokens, it, binary_operator_node);
        expect(tokens, it, Token::Type::CloseSubscript);
    } else if(operator_type == Token::Type::MemberAccess) {
        if(is_primitive(prev_expr->type_id))
            throw Exception("[Parser] Syntax error: Use of the '.' operator is only valid on composite types.\n", point_error(*it));
        // Only allow a single identifier, use subscript for more complex expressions?
        if(!(it->type == Token::Type::Identifier))
            throw Exception("[Parser] Syntax error: Expected identifier on the right side of '.' operator.\n", point_error(*it));
        const auto identifier_name = it->value;
        auto       type_id = prev_expr->type_id;
        auto       type = GlobalTypeRegistry::instance().get_type(type_id);
        // Automatic cast to pointee type (Could be a separate Node)
        auto base_type = GlobalTypeRegistry::instance().get_type(type->is_pointer() ? dynamic_cast<const PointerType*>(type)->pointee_type : type_id);
        assert(base_type->is_struct() || base_type->is_templated());
        auto as_struct_type = dynamic_cast<const StructType*>(
            base_type->is_templated() ? GlobalTypeRegistry::instance().get_type(dynamic_cast<const TemplatedType*>(base_type)->template_type_id) : base_type);
        auto member_it = as_struct_type->members.find(std::string(identifier_name));
        if(member_it != as_struct_type->members.end()) {
            auto member_identifier_node = new AST::MemberIdentifier(*it);
            binary_operator_node->add_child(member_identifier_node);
            member_identifier_node->index = member_it->second.index;
            if(base_type->is_templated())
                member_identifier_node->type_id = specialize(member_it->second.type_id, dynamic_cast<const TemplatedType*>(base_type)->parameters);
            else
                member_identifier_node->type_id = member_it->second.type_id;
            ++it;
        } else if(peek(tokens, it, Token::Type::OpenParenthesis)) {
            auto binary_node = curr_node->pop_child();
            auto first_argument = binary_node->pop_child();
            delete binary_node;
            auto call_node = new AST::FunctionCall(*it);
            curr_node->add_child(call_node);
            auto function_node = new AST::Variable(*it);
            call_node->add_child(function_node);
            ++it;

            call_node->add_child(first_argument);

            parse_function_arguments(tokens, it, call_node);

            // Allow using the dot operator on pointers and directly on an object
            if(!type->is_pointer()) {
                auto first_arg = call_node->arguments()[0];
                // FIXME: if(first_arg->type == AST::Node::Type::Dereference)
                //        We could simply remove the redundant Dereference node, or, even better, not generate it in the first place.
                auto get_pointer_node = new AST::Node(AST::Node::Type::GetPointer, first_arg->token);
                call_node->set_argument(0, nullptr);
                get_pointer_node->add_child(first_arg);
                get_pointer_node->type_id = GlobalTypeRegistry::instance().get_pointer_to(first_arg->type_id);
                call_node->set_argument(0, get_pointer_node);
            }

            // Search for a corresponding method
            const auto method = get_function(identifier_name, call_node->arguments());
            if(method && method->arguments().size() > 0 && !is_primitive(method->arguments()[0]->type_id) &&
               GlobalTypeRegistry::instance().get_pointer_to(base_type->type_id) == method->arguments()[0]->type_id) {
                call_node->type_id = method->type_id;
                call_node->flags = method->flags;

                check_function_call(call_node, method);

                return true;
            } else {
                auto candidates = get_functions(identifier_name);

                // TEMP DEBUG
                print("{}", get_overloads_hint_string(call_node, candidates));

                // TODO: Actually deduce types.
                std::vector<TypeID> deduced_types = {PrimitiveType::U64};

                for(const auto& candidate : candidates) {
                    if(candidate->is_templated()) {
                        print("Template candidate: {}\n", *static_cast<const AST::Node*>(candidate));
                        auto specialized = candidate->clone();
                        specialize(specialized, deduced_types);
                        // Insert it right after the generic version.
                        candidate->parent->add_child_after(specialized, candidate);
                        // FIXME: It should be declared in the scope of the original function declaration.
                        get_scope().declare_function(*specialized);
                        print("Specialized candidate: {}\n", *static_cast<const AST::Node*>(specialized));
                        // FIXME: Factorize this code with previous successful path.
                        if(specialized && specialized->arguments().size() > 0 && !is_primitive(specialized->arguments()[0]->type_id) &&
                           GlobalTypeRegistry::instance().get_pointer_to(base_type->type_id) == specialized->arguments()[0]->type_id) {
                            call_node->type_id = specialized->type_id;
                            call_node->flags = specialized->flags;

                            check_function_call(call_node, specialized);

                            return true;
                        }
                    } else {
                        print("Non-Template candidate: {}\n", *static_cast<const AST::Node*>(candidate));
                    }
                }
                if(candidates.size() == 0) {
                    throw Exception(fmt::format("[Parser] Syntax error: Method '{}' does not exists on type {}.\n", identifier_name, base_type->designation), point_error(*it));
                } else {
                    auto hint = get_overloads_hint_string(call_node, candidates);
                    throw Exception(fmt::format("[Parser] Syntax error: Method '{}' on type {} does not match the supplied arguments.\n", identifier_name, base_type->designation),
                                    point_error(*it) + hint);
                }
            }
        } else {
            throw Exception(fmt::format("[Parser] Syntax error: Member '{}' does not exists on type {}.\n", identifier_name, base_type->designation), point_error(*it));
        }
    } else {
        // Lookahead for rhs
        parse_next_expression(tokens, it, binary_operator_node, precedence);
    }

    // TODO: Test if types are compatible (with the operator and between each other)

    // FIXME: Consolidate these automatic casts
    const auto allow_automatic_cast = [&](PrimitiveType to, const std::vector<PrimitiveType>& from) {
        if(binary_operator_node->children[0]->type_id == to) {
            for(auto type_id : from) {
                if(binary_operator_node->children[1]->type_id == type_id) {
                    auto cast = binary_operator_node->insert_between(1, new AST::Node(AST::Node::Type::Cast));
                    cast->type_id = to;
                    break;
                }
            }
        }
    };
    allow_automatic_cast(PrimitiveType::U64, {PrimitiveType::Integer, PrimitiveType::U32, PrimitiveType::U16, PrimitiveType::U8});
    allow_automatic_cast(PrimitiveType::U32, {PrimitiveType::Integer, PrimitiveType::U16, PrimitiveType::U8});
    allow_automatic_cast(PrimitiveType::U16, {PrimitiveType::U8});
    allow_automatic_cast(PrimitiveType::I64, {PrimitiveType::Integer, PrimitiveType::I32, PrimitiveType::I16, PrimitiveType::I8});
    allow_automatic_cast(PrimitiveType::I32, {PrimitiveType::Integer, PrimitiveType::I16, PrimitiveType::I8});
    allow_automatic_cast(PrimitiveType::I16, {PrimitiveType::I8});

    // Allow assignement of pointer to any pointer type.
    // FIXME: Should this be explicit in user code?
    if(binary_operator_node->children[1]->type_id == PrimitiveType::Pointer && binary_operator_node->children[0]->type_id != binary_operator_node->children[1]->type_id) {
        auto cast = binary_operator_node->insert_between(1, new AST::Node(AST::Node::Type::Cast));
        cast->type_id = binary_operator_node->children[0]->type_id;
    }

    // Convert l-values to r-values when necessary (will generate a Load on the LLVM backend)
    // Left side of Assignement and MemberAcces should always be a l-value.
    if(operator_type != Token::Type::Assignment && operator_type != Token::Type::MemberAccess) {
        if(binary_operator_node->children[0]->type != AST::Node::Type::MemberIdentifier && binary_operator_node->children[0]->type != AST::Node::Type::ConstantValue) {
            auto ltor = binary_operator_node->insert_between(0, new AST::Node(AST::Node::Type::LValueToRValue));
            ltor->type_id = ltor->children.front()->type_id; // Propagate type
        }
    }
    if(binary_operator_node->children[1]->type != AST::Node::Type::MemberIdentifier && binary_operator_node->children[1]->type != AST::Node::Type::ConstantValue) {
        auto ltor = binary_operator_node->insert_between(1, new AST::Node(AST::Node::Type::LValueToRValue));
        ltor->type_id = ltor->children.front()->type_id;
    }

    resolve_operator_type(binary_operator_node);
    // Implicit casts
    auto create_cast_node = [&](int index, TypeID type) {
        auto castNode = new AST::Node(AST::Node::Type::Cast);
        castNode->type_id = type;
        castNode->parent = binary_operator_node;
        auto child = binary_operator_node->children[index];
        child->parent = nullptr;
        castNode->add_child(child);
        binary_operator_node->children[index] = castNode;
    };
    // FIXME: Cleanup, like everything else :)
    // Promotion from integer to float, either because of the binary_operator_node type, or the type of its operands.
    if(binary_operator_node->type_id == PrimitiveType::Float ||
       ((binary_operator_node->children[0]->type_id == PrimitiveType::Integer && binary_operator_node->children[1]->type_id == PrimitiveType::Float) ||
        (binary_operator_node->children[0]->type_id == PrimitiveType::Float && binary_operator_node->children[1]->type_id == PrimitiveType::Integer))) {

        if(binary_operator_node->token.type != Token::Type::Assignment && binary_operator_node->children[0]->type_id == PrimitiveType::Integer)
            create_cast_node(0, PrimitiveType::Float);
        if(binary_operator_node->children[1]->type_id == PrimitiveType::Integer)
            create_cast_node(1, PrimitiveType::Float);
    }
    // Truncation to integer (in assignments for example)
    if(binary_operator_node->type_id == PrimitiveType::Integer) {
        if(binary_operator_node->token.type != Token::Type::Assignment && binary_operator_node->children[0]->type_id == PrimitiveType::Float)
            create_cast_node(0, PrimitiveType::Integer);
        if(binary_operator_node->children[1]->type_id == PrimitiveType::Float)
            create_cast_node(1, PrimitiveType::Integer);
    }

    return true;
}

bool Parser::parse_variable_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node, bool is_const, bool allow_construtor) {
    auto identifier = expect(tokens, it, Token::Type::Identifier);

    auto var_declaration_node = new AST::VariableDeclaration(identifier);
    curr_node->add_child(var_declaration_node);

    if(it->type == Token::Type::Colon) {
        ++it;
        var_declaration_node->type_id = parse_type(tokens, it, curr_node);
    }

    if(!get_scope().declare_variable(*var_declaration_node))
        throw Exception(fmt::format("[Scope] Syntax error: Variable '{}' already declared.\n", var_declaration_node->token.value), point_error(var_declaration_node->token));

    // Also push a variable identifier for initialisation
    bool has_initializer = it != tokens.end() && (it->type == Token::Type::Assignment);
    if(is_const && !has_initializer)
        throw Exception(fmt::format("[Parser] Syntax error: Variable '{}' declared as const but not initialized.\n", identifier.value), point_error(identifier));
    if(has_initializer) {
        auto variable_node = curr_node->add_child(new AST::Node(AST::Node::Type::Variable, identifier));
        variable_node->type_id = var_declaration_node->type_id;
        parse_operator(tokens, it, curr_node);
        // Deduce variable type from initial value
        if(var_declaration_node->type_id == InvalidTypeID)
            var_declaration_node->type_id = variable_node->type_id;
    } else if(!is_primitive(var_declaration_node->type_id) && allow_construtor) {
        // Search for a default constructor and add a call to it if it exists
        std::vector<TypeID> span;
        span.push_back(GlobalTypeRegistry::instance().get_pointer_to(var_declaration_node->type_id));
        auto constructor = get_function("constructor", span);
        if(constructor) {
            auto call_node = new AST::FunctionCall(Token(Token::Type::Identifier, *internalize_string("constructor"), 0, 0));
            call_node->type_id = constructor->type_id;
            call_node->flags = constructor->flags;

            curr_node->add_child(call_node);
            // Constructor method designation
            auto constructor_node =
                new AST::Variable(Token(Token::Type::Identifier, *internalize_string("constructor"), 0, 0)); // FIXME: Still using the token to get the function...
            call_node->add_child(constructor_node);
            // Constructor argument (pointer to the object)
            auto get_pointer_node = call_node->add_child(new AST::Node(AST::Node::Type::GetPointer, var_declaration_node->token));
            get_pointer_node->type_id = GlobalTypeRegistry::instance().get_pointer_to(var_declaration_node->type_id);
            auto var_node = get_pointer_node->add_child(new AST::Variable(var_declaration_node->token));
            var_node->type_id = var_declaration_node->type_id;

            check_function_call(call_node, constructor);
        }
    }

    return true;
}

bool Parser::parse_import(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node*) {
    assert(it->type == Token::Type::Import);
    ++it;

    if(it == tokens.end())
        throw Exception("[Parser] Syntax error: Module name expected after import statement, got end-of-file.\n");

    std::string module_name = std::string(it->value);
    _module_interface.dependencies.push_back(module_name);
    auto cached_interface_file = _cache_folder;
    cached_interface_file += ModuleInterface::get_cache_filename(_module_interface.resolve_dependency(module_name)).replace_extension(".int");

    auto [success, new_type_imports, new_function_imports] = _module_interface.import_module(cached_interface_file);
    if(!success)
        return false;

    if(new_type_imports.empty() && new_function_imports.empty())
        warn("[Parser] Imported module {} doesn't export any symbol.\n", module_name);

    for(const auto& e : new_type_imports) {
        if(!get_root_scope().declare_type(*e)) {
            warn("[Parser::parse_import] Warning: declare_type on {} returned false, imported twice?\n", e->token.value);
        }
    }

    for(const auto& e : new_function_imports) {
        if(!get_root_scope().declare_function(*e)) {
            warn("[Parser::parse_import] Warning: declare_function on {} returned false, imported twice?\n", e->token.value);
        }
    }

    // FIXME: We'll want to add a way to also directly export the imported symbols.
    //        I don't think this should be the default behavior, but opt-in by using another keyword, or an additional marker.
    // FIXME: For now, we'll forward all the type definitions unconditionally.
    _module_interface.type_exports.insert(_module_interface.type_exports.end(), new_type_imports.begin(), new_type_imports.end());

    ++it;

    return true;
}

TypeID Parser::parse_type(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto token = expect(tokens, it, Token::Type::Identifier);

    auto scoped_type_id = get_type(std::string(token.value));

    if(it->type == Token::Type::Lesser) {
        auto type_parameters = parse_template_types(tokens, it, curr_node);
        fmt::print("[DEBUG] Parsed: {}<", token.value);
        for(const auto& name : type_parameters)
            fmt::print("{},", name);
        fmt::print(">\n");
        // Generate this type declaration if we never encountered it before
        if(!GlobalTypeRegistry::instance().specialized_type_exists(scoped_type_id, type_parameters)) {
            // This will create the type on the GlobalRegistry, so we have to get the scoped_type_id after checking for its existence
            scoped_type_id = GlobalTypeRegistry::instance().get_specialized_type(scoped_type_id, type_parameters);
            auto type = GlobalTypeRegistry::instance().get_type(scoped_type_id);
            assert(type->is_templated());
            if(!type->is_placeholder()) {
                auto templated_type = dynamic_cast<const TemplatedType*>(type);
                auto underlying_type = GlobalTypeRegistry::instance().get_type(templated_type->template_type_id);
                assert(underlying_type->is_struct());
                auto struct_type = dynamic_cast<const StructType*>(underlying_type);
                // auto parent = curr_node;
                // while(parent->parent != nullptr)
                //     parent = parent->parent;
                AST::TypeDeclaration* type_declaration_node = new AST::TypeDeclaration(Token(Token::Type::Identifier, templated_type->designation, 0, 0));
                type_declaration_node->type_id = scoped_type_id;
                for(const auto& [name, member] : struct_type->members) {
                    auto mem = type_declaration_node->add_child(new AST::VariableDeclaration(Token(Token::Type::Identifier, member.name, 0, 0)));
                    mem->type_id = member.type_id;
                }
                specialize(type_declaration_node, type_parameters);
                // Declare early
                print("TEST: {}\n", *static_cast<AST::Node*>(type_declaration_node));
                auto parent = curr_node;
                while(parent->parent)
                    parent = parent->parent;
                parent->add_child_front(type_declaration_node);
            }
        } else
            scoped_type_id = GlobalTypeRegistry::instance().get_specialized_type(scoped_type_id, type_parameters);
    }

    while(it->type == Token::Type::Multiplication) {
        scoped_type_id = GlobalTypeRegistry::instance().get_pointer_to(scoped_type_id);
        ++it;
    }

    // TODO: Doesn't work for template types yet.
    if(it->type == Token::Type::OpenSubscript) {
        ++it;
        // FIXME: Parse full expression?
        // parse_next_expression(tokens, it, varDecNode);
        uint32_t capacity;
        auto     digits = expect(tokens, it, Token::Type::Digits);
        std::from_chars(&*(digits.value.begin()), &*(digits.value.begin()) + digits.value.length(), capacity);
        expect(tokens, it, Token::Type::CloseSubscript);

        scoped_type_id = GlobalTypeRegistry::instance().get_array_of(scoped_type_id, capacity);
    }

    return scoped_type_id;
}

bool Parser::write_export_interface(const std::filesystem::path& path) const {
    auto cached_interface_file = _cache_folder;
    cached_interface_file += path;
    _module_interface.save(cached_interface_file);
    return true;
}

void Parser::resolve_operator_type(AST::UnaryOperator* op_node) {
    auto rhs = op_node->children[0]->type_id;
    op_node->type_id = rhs;

    if(op_node->type_id == InvalidTypeID) {
        error("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}. Node:\n", op_node->token.line);
        fmt::print("{}\n", *static_cast<AST::Node*>(op_node));
        throw Exception(fmt::format("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}.\n", op_node->token.line));
    }
}

void Parser::resolve_operator_type(AST::BinaryOperator* op_node) {
    if(op_node->token.type == Token::Type::MemberAccess) {
        op_node->type_id = op_node->children[1]->type_id;
        return;
    }

    auto lhs = op_node->children[0]->type_id;
    auto rhs = op_node->children[1]->type_id;
    op_node->type_id = resolve_operator_type(op_node->token.type, lhs, rhs);

    if(op_node->type_id == InvalidTypeID) {
        // Infer variable type from the initial value if not specified at declaration.
        if(op_node->token.type == Token::Type::Assignment && lhs == InvalidTypeID && rhs != InvalidTypeID) {
            op_node->children[0]->type_id = rhs;
            op_node->type_id = rhs;
        } else {
            error("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}. Node:\n", op_node->token.line);
            fmt::print("{}\n", *static_cast<AST::Node*>(op_node));
            throw Exception(fmt::format("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}.\n", op_node->token.line));
        }
    }
}

void Parser::insert_defer_node(AST::Node* curr_node) {
    auto ordered_variable_declarations = get_scope().get_ordered_variable_declarations();
    while(!ordered_variable_declarations.empty()) {
        auto dec = ordered_variable_declarations.top();
        ordered_variable_declarations.pop();

        if(dec->flags & AST::VariableDeclaration::Flag::Moved)
            continue;

        std::vector<TypeID> span;
        span.push_back(GlobalTypeRegistry::instance().get_pointer_to(dec->type_id));
        auto destructor = get_function("destructor", span);
        if(destructor) {
            Token destructor_token;
            destructor_token.type = Token::Type::Identifier;
            destructor_token.value = *internalize_string("destructor");
            auto call_node = new AST::FunctionCall(destructor_token);
            call_node->type_id = destructor->type_id;
            call_node->flags = destructor->flags;

            curr_node->add_child(call_node);
            // Destructor method designation
            auto destructor_node = new AST::Variable(destructor_token); // FIXME: Still using the token to get the function...
            call_node->add_child(destructor_node);
            // Destructor argument (pointer to the object)
            auto get_pointer_node = call_node->add_child(new AST::Node(AST::Node::Type::GetPointer, dec->token));
            get_pointer_node->type_id = GlobalTypeRegistry::instance().get_pointer_to(dec->type_id);
            auto var_node = get_pointer_node->add_child(new AST::Variable(dec->token));
            var_node->type_id = dec->type_id;

            check_function_call(call_node, destructor);
        }
    }
}

TypeID Parser::specialize(TypeID type_id, const std::vector<TypeID>& parameters) {
    auto r = type_id;
    auto type = GlobalTypeRegistry::instance().get_type(r);
    if(type->is_placeholder()) {
        // FIXME: Feels hackish, as always.
        size_t indirection_count = 0;
        while(type->is_pointer()) {
            auto pointer_type = dynamic_cast<const PointerType*>(type);
            type = GlobalTypeRegistry::instance().get_type(pointer_type->pointee_type);
            ++indirection_count;
        }

        if(type->is_templated()) {
            auto templated_type = dynamic_cast<const TemplatedType*>(type);
            auto specialized_type_id = GlobalTypeRegistry::instance().get_specialized_type(templated_type->template_type_id, parameters);
            r = specialized_type_id;
        } else
            r = parameters[type->type_id - PlaceholderTypeID_Min];

        for(auto i = 0; i < indirection_count; ++i)
            r = GlobalTypeRegistry::instance().get_pointer_to(r);
    }
    return r;
}

void Parser::specialize(AST::Node* node, const std::vector<TypeID>& parameters) {
    for(auto c : node->children)
        specialize(c, parameters);
    if(node->type_id != InvalidTypeID)
        node->type_id = specialize(node->type_id, parameters);
}

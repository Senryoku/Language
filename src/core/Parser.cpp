#include <Parser.hpp>

#include <algorithm>
#include <fstream>

#include <fmt/ranges.h>

#include <GlobalTemplateCache.hpp>
#include <ModuleInterface.hpp>

const static std::unordered_map<PrimitiveType, std::vector<PrimitiveType>> AllowedAutomaticCasts = {
    {PrimitiveType::U64, {PrimitiveType::Integer, PrimitiveType::U32, PrimitiveType::U16, PrimitiveType::U8}},
    {PrimitiveType::U32, {PrimitiveType::Integer, PrimitiveType::U16, PrimitiveType::U8}},
    {PrimitiveType::U16, {PrimitiveType::U8}},
    {PrimitiveType::I64, {PrimitiveType::Integer, PrimitiveType::I32, PrimitiveType::I16, PrimitiveType::I8}},
    {PrimitiveType::I32, {PrimitiveType::Integer, PrimitiveType::I16, PrimitiveType::I8}},
    {PrimitiveType::I16, {PrimitiveType::I8}},
};

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

AST::Node* Parser::parse_type(const std::span<Token>& tokens, AST& ast) {
    auto root = ast.get_root().add_child(new AST::Node{AST::Node::Type::Root});
    auto it = tokens.begin();
    auto type_id = parse_type(tokens, it, root);
    for(const auto& child : get_hoisted_declarations_node(root)->children) {
        if(child->type == AST::Node::Type::TypeDeclaration && child->type_id == type_id) {
            return child;
        }
    }
    assert(false);
    return nullptr;
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

    // Promote to int for intermediary results, unless it doesn't fit.
    if(is_integer(lhs) && is_integer(rhs)) {
        // Keep the signed nature of the expression
        if(lhs == PrimitiveType::I64 || rhs == PrimitiveType::I64)
            return PrimitiveType::I64;
        // Whole expression is unsigned, and at least one side is 64bits
        if((lhs == PrimitiveType::U64 && is_unsigned(rhs)) || (is_unsigned(lhs) && rhs == PrimitiveType::U64))
            return PrimitiveType::U64;
        if(is_unsigned(lhs) && is_unsigned(lhs))
            return PrimitiveType::U32;
        return PrimitiveType::Integer;
    }

    if(lhs == rhs && is_primitive(lhs))
        return lhs;

    // Promote integer to Float
    if((is_floating_point(lhs) && is_integer(rhs)) || (is_integer(lhs) && is_floating_point(rhs))) {
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
                    if(ret_type->is_struct() ||
                       (ret_type->is_templated() && GlobalTypeRegistry::instance().get_type(dynamic_cast<const TemplatedType*>(ret_type)->template_type_id)->is_struct())) {
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
                    return_variable->flags &= ~AST::VariableDeclaration::Flag::Moved;

                to_rvalue->type_id = to_rvalue->children[0]->type_id;
                return_node->type_id = return_node->children[0]->type_id;

                curr_node->add_child(return_node.release());
                update_return_type(curr_node->children.back());
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
    check_eof(tokens, it, "scope opening");
    if(it->type != Token::Type::OpenScope) {
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

    pop_scope();
    it = end + 1;
    return r;
}

AST::FunctionDeclaration* Parser::get_parent_function(AST::Node* node) {
    auto it = node;
    while(it && it->type != AST::Node::Type::FunctionDeclaration) {
        it = it->parent;
    }
    if(!it)
        throw Exception(fmt::format("[Parser] Node doesn't have a parent function: \n{}\n", *node));
    return dynamic_cast<AST::FunctionDeclaration*>(it);
}

void Parser::update_return_type(AST::Node* return_node) {
    auto parent_function = get_parent_function(return_node);
    auto previous_return_type = parent_function->body()->type_id;
    // Return not set yet, we'll use this return statement to infer it automatically.
    if(previous_return_type == InvalidTypeID) {
        parent_function->body()->type_id = return_node->type_id;
    } else if(previous_return_type != return_node->type_id) {
        auto type = GlobalTypeRegistry::instance().get_type(previous_return_type);
        // If the return type is context dependent, we can't raise an error yet.
        if(!type->is_placeholder())
            throw Exception(fmt::format("[Parser] Syntax error: Incoherent return types, got {}, expected {}.\n", type_id_to_string(return_node->type_id),
                                        type_id_to_string(previous_return_type)),
                            point_error(return_node->token));
    }
}

// TODO: Formely define wtf is an expression :)
bool Parser::parse_next_expression(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node, uint32_t precedence, bool search_for_matching_bracket) {
    check_eof(tokens, it, "expression");

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
        check_eof(tokens, it, "closing parenthesis ')'");
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
    skip(tokens, end, Token::Type::EndStatement);
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
    check_eof(tokens, it, "open parenthesis");
    if(it->type != Token::Type::OpenParenthesis)
        throw Exception(fmt::format("Expected '(' after while on line {}, got {}.\n", it->line, it->value), point_error(*it));

    // Parse condition and add it as first child
    ++it; // Point to the beginning of the expression until ')' ('search_for_matching_bracket': true)
    if(!parse_next_expression(tokens, it, whileNode, max_precedence, true))
        return false;

    check_eof(tokens, it, "while body");

    if(!parse_scope_or_single_statement(tokens, it, whileNode))
        return false;

    return true;
}

bool Parser::parse_for(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node) {
    auto forNode = curr_node->add_child(new AST::Node(AST::Node::Type::ForStatement, *it));
    ++it;
    expect(tokens, it, Token::Type::OpenParenthesis);
    check_eof(tokens, it, "for condition");

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

    check_eof(tokens, it, "for body");

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

    check_eof(tokens, it, "function body");

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

        check_function_return_type(function_node);
    }

    pop_scope();

    if(templated)
        GlobalTemplateCache::instance().register_function(*function_node);

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
    if(!get_scope().declare_type(*type_node)) {
        warn("[Parser] Syntax error: Type {} already declared in this scope.\n", type_node->token.value);
        fmt::print("{}", point_error(type_node->token));
        type_node->type_id = get_scope().find_type(type_node->token.value);
    }

    // FIXME: Generate templated constructor function for templated types.
    if(has_at_least_one_default_value) {
        // Declare a default constructor.
        // FIXME: This could probably be way more elegant, rather then contructing the AST by hand...
        auto this_token = Token(Token::Type::Identifier, *internalize_string("this"), type_node->token.line, type_node->token.column);
        auto function_node = new AST::FunctionDeclaration(Token(Token::Type::Identifier, *internalize_string("constructor"), type_node->token.line, type_node->token.column));
        curr_node->add_child(function_node);
        function_node->type_id = PrimitiveType::Void;
        auto this_declaration_node = function_node->add_child(new AST::VariableDeclaration(this_token));
        // For template types, we need to create the templated type with its placeholder arguments.
        auto this_base_type = type_node->type_id;
        if(templated_type) {
            std::vector<TypeID> placeholder_types;
            for(auto i = 0; i < template_typenames.size(); ++i)
                placeholder_types.push_back(PlaceholderTypeID_Min + i);
            this_base_type = GlobalTypeRegistry::instance().get_specialized_type(type_node->type_id, placeholder_types);
        }
        this_declaration_node->type_id = GlobalTypeRegistry::instance().get_pointer_to(this_base_type);
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
                dereference->type_id = this_base_type;
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
        if(function_node->is_templated())
            GlobalTemplateCache::instance().register_function(*function_node);
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

AST::Node* Parser::parse_digits(const std::span<Token>&, std::span<Token>::iterator& it, AST::Node* curr_node, PrimitiveType type) {
    uint64_t value;
    auto [ptr, error_code] = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), value);
    if(error_code == std::errc::invalid_argument)
        throw Exception("[Parser::parse_digits] std::from_chars returned invalid_argument.\n", point_error(*it));
    else if(error_code == std::errc::result_out_of_range)
        throw Exception("[Parser::parse_digits] std::from_chars returned result_out_of_range.\n", point_error(*it));
    AST::Node* integer = nullptr;
    if(type == PrimitiveType::Void) {
        if(value > std::numeric_limits<int64_t>::max())
            type = PrimitiveType::U64;
        else if(value > std::numeric_limits<uint32_t>::max())
            type = PrimitiveType::I64;
        else if(value > std::numeric_limits<int32_t>::max())
            type = PrimitiveType::U32;
        else
            type = PrimitiveType::Integer;
    }
    switch(type) {
        case PrimitiveType::I8: integer = gen_integer_literal_node<int8_t>(*it, value, type); break;
        case PrimitiveType::I16: integer = gen_integer_literal_node<int16_t>(*it, value, type); break;
        case PrimitiveType::I32: integer = gen_integer_literal_node<int32_t>(*it, value, type); break;
        case PrimitiveType::I64: integer = gen_integer_literal_node<int64_t>(*it, value, type); break;
        case PrimitiveType::U8: integer = gen_integer_literal_node<uint8_t>(*it, value, type); break;
        case PrimitiveType::U16: integer = gen_integer_literal_node<uint16_t>(*it, value, type); break;
        case PrimitiveType::U32: integer = gen_integer_literal_node<uint32_t>(*it, value, type); break;
        case PrimitiveType::U64: integer = gen_integer_literal_node<uint64_t>(*it, value, type); break;
        case PrimitiveType::Integer: {
            if(static_cast<int64_t>(value) < std::numeric_limits<int32_t>::min() || static_cast<int64_t>(value) > std::numeric_limits<int32_t>::max())
                throw Exception(fmt::format("Error parsing integer for target type {}: value '{}' out-of-bounds (range: [{}, {}]).", type_id_to_string(PrimitiveType::Integer),
                                            value, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()),
                                point_error(*it));
            auto local = new AST::IntegerLiteral(*it);
            local->value = static_cast<int32_t>(value);
            integer = local;
            break;
        }
        default: throw Exception(fmt::format("[Parser::parse_digits] Unexpected target type '{}'.\n", type));
    }
    curr_node->add_child(integer);
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
        strNode->value = *internalize_string(std::string(it->value));
    ++it;
    return true;
}

bool Parser::parse_function_arguments(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::FunctionCall* curr_node) {
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

std::string Parser::get_overloads_hint_string(const std::string_view& name, const std::span<TypeID>& arguments, const std::vector<const AST::FunctionDeclaration*>& candidates) {
    std::string used_types = "Called with: " + std::string(name) + "(";
    for(auto i = 0; i < arguments.size(); ++i)
        used_types += (i > 0 ? ", " : "") + type_id_to_string(arguments[i]);
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

void Parser::throw_unresolved_function(const Token& name, const std::span<TypeID>& arguments) {
    auto candidates = get_functions(name.value);
    if(candidates.size() == 0)
        throw Exception(fmt::format("[Parser] Call to undefined function '{}'.\n", name.value), point_error(name));
    else {
        auto hint = get_overloads_hint_string(name.value, arguments, candidates);
        throw Exception(fmt::format("[Parser] Call to undefined function '{}', no candidate matches the arguments types.\n", name.value), point_error(name) + hint);
    }
}

const AST::FunctionDeclaration* Parser::resolve_or_instanciate_function(AST::FunctionCall* call_node) {
    std::vector<TypeID> param_types;
    for(auto c : call_node->arguments())
        param_types.push_back(c->type_id);
    return resolve_or_instanciate_function(call_node->token.value, param_types, call_node);
}

const AST::FunctionDeclaration* Parser::resolve_or_instanciate_function(const std::string_view& name, const std::span<TypeID>& arguments, AST::Node* curr_node) {
    // Search for a corresponding method
    const auto function = get_function(name, arguments);
    if(function) {
        return function;
    } else {
        auto candidates = get_functions(name);
        if(candidates.size() == 0)
            return nullptr;

        for(const auto& candidate : candidates) {
            // Try to specialize matching templated functions.
            // TODO: Handle variadics
            if(candidate->is_templated() && candidate->arguments().size() == arguments.size()) {
                std::vector<TypeID> deduced_types = deduce_placeholder_types(arguments, candidate);
                if(deduced_types.empty()) // Argument types cannot match.
                    break;

                auto specialized = candidate->body() ? candidate->clone() : GlobalTemplateCache::instance().get_function(std::string(candidate->token.value))->clone();
                specialize(specialized, deduced_types);
                check_function_return_type(specialized);
                // Insert it right after the generic version.
                if(candidate->parent)
                    candidate->parent->add_child_after(specialized, candidate);
                else {
                    get_hoisted_declarations_node(curr_node)->add_child(specialized);
                }
                // FIXME: Idealy, it should be declared in the scope of the original function declaration.
                get_root_scope().declare_function(*specialized);
                if(specialized && specialized->arguments().size() > 0)
                    return specialized;
            }
        }
    }
    return nullptr;
}

void Parser::check_function_call(AST::FunctionCall* call_node, const AST::FunctionDeclaration* function) {
    call_node->type_id = function->type_id;
    call_node->flags = function->flags;

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

bool Parser::deduce_placeholder_types(const Type* call_node, const Type* function_node, std::vector<TypeID>& deduced_types) {
    if(function_node->is_placeholder()) {
        if(function_node->is_templated()) {
            if(!call_node->is_templated())
                return false;
            auto arg_templated_type = dynamic_cast<const TemplatedType*>(call_node);
            auto param_templated_type = dynamic_cast<const TemplatedType*>(function_node);
            if(arg_templated_type->template_type_id != param_templated_type->template_type_id)
                return false;
            if(arg_templated_type->parameters.size() != param_templated_type->parameters.size())
                return false;
            for(auto idx = 0; idx < arg_templated_type->parameters.size(); ++idx) {
                if(!deduce_placeholder_types(GlobalTypeRegistry::instance().get_type(arg_templated_type->parameters[idx]),
                                             GlobalTypeRegistry::instance().get_type(param_templated_type->parameters[idx]), deduced_types))
                    return false;
            }
            return true;
        }
        if(function_node->is_pointer()) {
            if(!call_node->is_pointer())
                return false;
            return deduce_placeholder_types(GlobalTypeRegistry::instance().get_type(dynamic_cast<const PointerType*>(call_node)->pointee_type),
                                            GlobalTypeRegistry::instance().get_type(dynamic_cast<const PointerType*>(function_node)->pointee_type), deduced_types);
        }
        // TODO: More.
        assert(is_placeholder(function_node->type_id));
        auto index = get_placeholder_index(function_node->type_id);
        if(deduced_types.size() <= index)
            deduced_types.resize(index + 1, InvalidTypeID);
        // Mismatch
        if(deduced_types[index] != InvalidTypeID && deduced_types[index] != call_node->type_id)
            return false;
        deduced_types[index] = call_node->type_id;
        return true;
    }
    return true;
}

std::vector<TypeID> Parser::deduce_placeholder_types(const std::span<TypeID>& arguments, const AST::FunctionDeclaration* function_node) {
    std::vector<TypeID> deduced_types;
    for(auto idx = 0; idx < arguments.size(); ++idx) {
        auto arg_type = GlobalTypeRegistry::instance().get_type(arguments[idx]);
        auto param_type = GlobalTypeRegistry::instance().get_type(function_node->arguments()[idx]->type_id);
        if(!deduce_placeholder_types(arg_type, param_type, deduced_types))
            return {};
    }
    return deduced_types;
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

        std::vector<TypeID> arguments_types;
        for(const auto& c : call_node->arguments())
            arguments_types.push_back(c->type_id);
        auto resolved_function = resolve_or_instanciate_function(function_name, arguments_types, call_node);
        if(!resolved_function)
            throw_unresolved_function(call_node->token, arguments_types);

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
            for(const auto& [from, to] : AllowedAutomaticCasts)
                allow_automatic_cast(from, to);
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
    check_eof(tokens, it, "right-hand side operand");

    if(operator_type == Token::Type::OpenSubscript) {
        parse_next_expression(tokens, it, binary_operator_node);
        expect(tokens, it, Token::Type::CloseSubscript);
    } else if(operator_type == Token::Type::MemberAccess) {
        if(is_primitive(prev_expr->type_id))
            throw Exception("[Parser] Syntax error: Use of the '.' operator is only valid on composite types.\n", point_error(*it));
        // Only allow a single identifier, use subscript for more complex expressions?
        if(!(it->type == Token::Type::Identifier))
            throw Exception("[Parser] Syntax error: Expected identifier on the right side of '.' operator.\n", point_error(*it));
        auto type_id = prev_expr->type_id;
        auto type = GlobalTypeRegistry::instance().get_type(type_id);
        // Automatic cast to pointee type (Could be a separate Node)
        auto base_type = GlobalTypeRegistry::instance().get_type(type->is_pointer() ? dynamic_cast<const PointerType*>(type)->pointee_type : type_id);
        if(peek(tokens, it, Token::Type::OpenParenthesis)) {
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

            auto arg_types = call_node->get_argument_types();

            bool has_placeholder = false;
            for(const auto& arg : arg_types)
                if(GlobalTypeRegistry::instance().get_type(arg)->is_placeholder()) {
                    has_placeholder = true;
                    break;
                }
            // Delay checking until specialization
            if(has_placeholder)
                return true;

            auto method = resolve_or_instanciate_function(call_node->token.value, arg_types, call_node);
            if(!method)
                throw_unresolved_function(call_node->token, arg_types);
            check_function_call(call_node, method);
            return true;
        } else {
            auto member_identifier_node = new AST::MemberIdentifier(*it);
            binary_operator_node->add_child(member_identifier_node);
            // TODO: Handle non-specialized templated types. We have to delay the MemberIdentifier creation afters specialization (member index cannot be known at this time,
            //       unless we have constraint on the placeholder, like contracts/traits, but we have nothing like that right now :) )
            if(is_placeholder(base_type->type_id)) {
                // throw Exception("TODO: Handle member access on non-specialized templated types.\n", point_error(*it));
            } else {
                revolve_member_identifier(base_type, member_identifier_node);
            }
            ++it;
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
                    // Insert load if we're dealing with a variable.
                    if(cast->children[0]->type == AST::Node::Type::Variable)
                        cast->insert_between(0, new AST::Node(AST::Node::Type::LValueToRValue));
                    break;
                }
            }
        }
    };
    for(const auto& [from, to] : AllowedAutomaticCasts)
        allow_automatic_cast(from, to);

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
        auto cast_node = new AST::Node(AST::Node::Type::Cast);
        cast_node->type_id = type;
        cast_node->parent = binary_operator_node;
        auto child = binary_operator_node->children[index];
        child->parent = nullptr;
        cast_node->add_child(child);
        binary_operator_node->children[index] = cast_node;
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

    if(binary_operator_node->token.type == Token::Type::Assignment) {
        if(binary_operator_node->children[1]->type_id == PrimitiveType::Void)
            throw Exception(fmt::format("[Parser] Cannot assign void to a variable.\n"), point_error(binary_operator_node->token));

        // Make sure both sides of the assignment are of the same type.
        // FIXME: Do better (in regards to placeholders at least).
        if(!is_placeholder(binary_operator_node->children[0]->type_id) && !is_placeholder(binary_operator_node->children[1]->type_id) &&
           binary_operator_node->children[0]->type_id != binary_operator_node->children[1]->type_id)
            throw Exception(fmt::format("[Parser] Cannot assign value of type {} to variable '{}' of type {}.\n", type_id_to_string(binary_operator_node->children[1]->type_id),
                                        binary_operator_node->children[0]->token.value, type_id_to_string(binary_operator_node->children[0]->type_id)),
                            point_error(binary_operator_node->token));
    }

    return true;
}

void Parser::revolve_member_identifier(const Type* base_type, AST::MemberIdentifier* member_identifier_node) {
    const auto& identifier_name = member_identifier_node->token.value;
    assert(base_type->is_struct() || base_type->is_templated());
    auto as_struct_type = dynamic_cast<const StructType*>(
        base_type->is_templated() ? GlobalTypeRegistry::instance().get_type(dynamic_cast<const TemplatedType*>(base_type)->template_type_id) : base_type);
    auto member_it = as_struct_type->members.find(std::string(identifier_name));
    if(member_it != as_struct_type->members.end()) {
        member_identifier_node->index = member_it->second.index;
        if(base_type->is_templated())
            member_identifier_node->type_id = specialize(member_it->second.type_id, dynamic_cast<const TemplatedType*>(base_type)->parameters);
        else
            member_identifier_node->type_id = member_it->second.type_id;
    } else
        throw Exception(fmt::format("[Parser] Syntax error: Member '{}' does not exists on type {}.\n", identifier_name, base_type->designation),
                        point_error(member_identifier_node->token));
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
        else {
            auto assignment_node = curr_node->children.back();
            if(var_declaration_node->type_id != assignment_node->children.back()->type_id) {
                // FIXME: Check and warn (or prevent) against unsafe implicit casts
                assignment_node->insert_between(assignment_node->children.size() - 1, new AST::Cast(var_declaration_node->type_id));
            }
        }
    } else if(auto type = GlobalTypeRegistry::instance().get_type(var_declaration_node->type_id); allow_construtor && (type->is_struct() || type->is_templated())) {
        // Search for a default constructor and add a call to it if it exists
        std::vector<TypeID> span;
        span.push_back(GlobalTypeRegistry::instance().get_pointer_to(var_declaration_node->type_id));
        auto constructor = resolve_or_instanciate_function("constructor", span, var_declaration_node);
        auto fake_token = Token(Token::Type::Identifier, *internalize_string("constructor"), var_declaration_node->token.line, var_declaration_node->token.column);
        if(constructor) {
            auto call_node = new AST::FunctionCall(fake_token);

            curr_node->add_child(call_node);
            // Constructor method designation
            auto constructor_node = new AST::Variable(fake_token); // FIXME: Still using the token to get the function...
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
    check_eof(tokens, it, "module name");

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
        // This will generate this type specialization if we never encountered it before (across modules)
        scoped_type_id = GlobalTypeRegistry::instance().get_specialized_type(scoped_type_id, type_parameters);
        // We still have to generate a local type declaration since each module need to know the layout of the type.
        // FIXME: We could rewrite the Module to use Type objects to generate the LLVM struct, removing the need to generate and hoist these nodes (as it would be easy to generate
        //        missing specializations on the fly), but the current shape of TemplatedStruct makes it a little awkward (we still have to
        //        access the underlying StructType to get the member types).
        // FIXME: Search if this type is already declared locally, this could be done much more efficiently.
        bool already_declared = false;
        for(const auto& child : get_hoisted_declarations_node(curr_node)->children) {
            if(child->type == AST::Node::Type::TypeDeclaration && child->type_id == scoped_type_id) {
                already_declared = true;
                break;
            }
        }
        if(!already_declared) {
            auto type = GlobalTypeRegistry::instance().get_type(scoped_type_id);
            assert(type->is_templated());
            if(!type->is_placeholder()) {
                auto templated_type = dynamic_cast<const TemplatedType*>(type);
                auto underlying_type = GlobalTypeRegistry::instance().get_type(templated_type->template_type_id);
                assert(underlying_type->is_struct());
                auto struct_type = dynamic_cast<const StructType*>(underlying_type);

                AST::TypeDeclaration* type_declaration_node = new AST::TypeDeclaration(Token(Token::Type::Identifier, templated_type->designation, 0, 0));
                type_declaration_node->type_id = scoped_type_id;
                // Insert specialized members in the same order as the original declaration
                std::vector<const StructType::Member*> members;
                members.resize(struct_type->members.size());
                for(const auto& [name, member] : struct_type->members)
                    members[member.index] = &member;
                for(const auto& member : members) {
                    auto mem = type_declaration_node->add_child(new AST::VariableDeclaration(Token(Token::Type::Identifier, member->name, 0, 0)));
                    mem->type_id = member->type_id;
                }
                specialize(type_declaration_node, type_parameters);
                // Declare early
                get_hoisted_declarations_node(curr_node)->add_child(type_declaration_node);
                // FIXME: Systematically exports nexly generated template specializations.
                //        Ideally we'd want to export only the specializations that are part of some form of interface (parameters/return types of exported functions, for example)
                _module_interface.type_exports.push_back(type_declaration_node);
            }
        }
    }

    if(it == tokens.end())
        return scoped_type_id;

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
        error("[Parser] Couldn't resolve unary operator return type (Missing impl.) on line {}. Node:\n", op_node->token.line);
        fmt::print("{}\n", *static_cast<AST::Node*>(op_node));
        throw Exception(fmt::format("[Parser] Couldn't resolve unary operator return type (Missing impl.) on line {}.\n", op_node->token.line));
    }
}

void Parser::resolve_operator_type(AST::BinaryOperator* op_node) {
    auto lhs = op_node->children[0]->type_id;
    auto rhs = op_node->children[1]->type_id;

    if(op_node->token.type == Token::Type::MemberAccess) {
        op_node->type_id = rhs;
        return;
    }

    op_node->type_id = resolve_operator_type(op_node->token.type, lhs, rhs);

    if(op_node->type_id == InvalidTypeID) {
        // Infer variable type from the initial value if not specified at declaration.
        if(op_node->token.type == Token::Type::Assignment && lhs == InvalidTypeID && rhs != InvalidTypeID) {
            op_node->children[0]->type_id = rhs;
            op_node->type_id = rhs;
        } else {
            error("[Parser] Couldn't resolve binary operator return type (Missing impl.) on line {}. Node:\n", op_node->token.line);
            fmt::print("{}\n", *static_cast<AST::Node*>(op_node));
            throw Exception(fmt::format("[Parser] Couldn't resolve binary operator return type (Missing impl.) on line {}.\n", op_node->token.line));
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
        auto destructor = resolve_or_instanciate_function("destructor", span, dec);
        if(destructor) {
            Token destructor_token;
            destructor_token.type = Token::Type::Identifier;
            destructor_token.value = *internalize_string("destructor");
            auto call_node = new AST::FunctionCall(destructor_token);

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

void Parser::check_function_return_type(AST::FunctionDeclaration* function_node) {
    // Return type deduction
    auto return_type = function_node->body()->type_id;

    if(function_node->is_templated() && return_type == InvalidTypeID && function_node->type_id == InvalidTypeID)
        return; // We cannot determine the return type yet, delay type checking on template spacialization.

    if(return_type == InvalidTypeID)
        return_type = PrimitiveType::Void;
    if(function_node->type_id != InvalidTypeID && function_node->type_id != return_type)
        throw Exception(fmt::format("[Parser] Syntax error: Incoherent return types for function {}, got {}, expected {}.\n", function_node->token.value,
                                    type_id_to_string(return_type), type_id_to_string(function_node->type_id)),
                        point_error(function_node->body()->token));
    function_node->type_id = return_type;
}

void Parser::specialize(AST::Node* node, const std::vector<TypeID>& parameters) {
    for(auto c : node->children)
        specialize(c, parameters);
    if(node->type_id != InvalidTypeID)
        node->type_id = specialize(node->type_id, parameters);

    // Verify the updated node.
    // FIXME: We also need to update/propagate a lot of types.
    switch(node->type) {
        case AST::Node::Type::FunctionCall: {
            auto function_call_node = dynamic_cast<AST::FunctionCall*>(node);
            auto function = resolve_or_instanciate_function(function_call_node);
            if(!function)
                throw Exception(fmt::format("[Parser] Could not find specialized function for:\n{}\n", *node));
            check_function_call(function_call_node, function);
            break;
        }
        case AST::Node::Type::BinaryOperator: {
            resolve_operator_type(dynamic_cast<AST::BinaryOperator*>(node));
            break;
        }
        case AST::Node::Type::LValueToRValue: {
            node->type_id = node->children[0]->type_id;
            break;
        }
        case AST::Node::Type::ReturnStatement: {
            if(node->children.empty())
                node->type_id = PrimitiveType::Void;
            else
                node->type_id = node->children[0]->type_id;
            update_return_type(node);
            break;
        }
        case AST::Node::Type::MemberIdentifier: {
            auto member_identifier_node = dynamic_cast<AST::MemberIdentifier*>(node);
            assert(node->parent && node->parent->type == AST::Node::Type::BinaryOperator && node->parent->token.type == Token::Type::MemberAccess);
            revolve_member_identifier(GlobalTypeRegistry::instance().get_type(node->parent->children[0]->type_id), member_identifier_node);
        }
            // TODO: Check Types Declarations
    }
}

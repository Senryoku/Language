#include <Parser.hpp>

#include <algorithm>

bool Parser::parse(const std::span<Tokenizer::Token>& tokens, AST::Node* curr_node) {
    curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Statement));
    auto it = tokens.begin();
    while(it != tokens.end()) {
        const auto& token = *it;
        switch(token.type) {
            case Tokenizer::Token::Type::Control: {
                switch(token.value[0]) {
                    case '{': { // Open a new scope
                        curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Scope, *it));
                        push_scope();
                        ++it;
                        break;
                    }
                    case '}': { // Close nearest scope
                        while(curr_node->type != AST::Node::Type::Scope && curr_node->parent != nullptr)
                            curr_node = curr_node->parent;
                        if(curr_node->type != AST::Node::Type::Scope) {
                            error("[Parser] Syntax error: Unmatched '}' one line {}.\n", it->line);
                            return false;
                        }
                        curr_node = curr_node->parent;
                        pop_scope();
                        ++it;
                        break;
                    }
                    case ';':
                        curr_node = curr_node->parent;
                        curr_node = curr_node->add_child(new AST::Node(AST::Node::Type::Statement, *it));
                        ++it;
                        break;
                    default:
                        warn("[Parser] Unused token: {}.\n", *it);
                        ++it;
                        break;
                }
                break;
            }
            case Tokenizer::Token::Type::If: {
                if(!peek(tokens, it, Tokenizer::Token::Type::Operator, "(")) {
                    error("[Parser] Syntax error: expected '(' after 'if'.\n");
                    return false;
                }
                auto ifNode = curr_node->add_child(new AST::Node(AST::Node::Type::IfStatement, *it));
                it += 2;
                if(!parse_next_expression(tokens, it, ifNode, 0, true)) {
                    delete curr_node->pop_child();
                    return false;
                }
                if(!parse_scope_or_single_statement(tokens, it, ifNode)) {
                    error("[Parser] Syntax error: expected 'new scope' after 'if'.\n");
                    delete curr_node->pop_child();
                    return false;
                }
                // TODO: Handle Else here?

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
                if(!parse_next_expression(tokens, it, returnNode, 0)) {
                    delete curr_node->pop_child();
                    return false;
                }
                returnNode->value.type = returnNode->children[0]->value.type;
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

            case Tokenizer::Token::Type::Operator: {
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
    if(it == tokens.end() || it->value != "{") {
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
        if(end->value == "{")
            ++opened_scopes;
        if(end->value == "}") {
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

    if(it->type == Tokenizer::Token::Type::Operator && it->value == "(") {
        ++it;
        if(!parse_next_expression(tokens, it, exprNode, 0, true)) {
            delete curr_node->pop_child();
            return false;
        }
    }

    bool stop = false;

    while(it != tokens.end() && !(it->type == Tokenizer::Token::Type::Control && (it->value == ";" || it->value == ",")) && !stop) {
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
            case Operator: {
                if(it->value == ")") {
                    stop = true;
                    break;
                } else if(it->value == "]") {
                    stop = true;
                    break;
                }
                auto p = operator_precedence.at(std::string(it->value));
                if(p > precedence) {
                    if(!parse_operator(tokens, it, exprNode)) {
                        delete curr_node->pop_child();
                        return false;
                    }
                } else {
                    stop = true;
                }
                break;
            }
            case Control: {
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

    if(search_for_matching_bracket && (it == tokens.end() || it->value != ")")) {
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
    if(peek(tokens, it, Tokenizer::Token::Type::Operator, "(")) {
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

    if(peek(tokens, it, Tokenizer::Token::Type::Operator, "[")) { // Array accessor
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
        if(!parse_next_expression(tokens, it, access_operator_node, 0, false)) {
            delete curr_node->pop_child();
            return false;
        }

        // TODO: Make sure this is an integer? And compile-time constant?
        assert(it->value == "]");
        ++it; // Skip ']'
    } else {
        ++it;
    }
    return true;
}

bool Parser::parse_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    // FIXME: Probably
    auto end = it;
    while(end != tokens.end() && end->value != ";")
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
    if(it->value == "{") {
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
    if(it->value != "(") {
        error("Expected '(' after while on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }
    // Parse condition and add it as first child
    ++it; // Point to the beginning of the expression until ')' ('search_for_matching_bracket': true)
    if(!parse_next_expression(tokens, it, whileNode, 0, true)) {
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
    if(it->value != "(") {
        error("Expected '(' after for on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }
    ++it; // Skip '('
    // Initialisation
    if(!parse_statement(tokens, it, forNode)) {
        delete curr_node->pop_child();
        return false;
    }
    // Condition
    if(!parse_statement(tokens, it, forNode)) {
        delete curr_node->pop_child();
        return false;
    }
    // Increment (until bracket)
    if(!parse_next_expression(tokens, it, forNode, 0, true)) {
        delete curr_node->pop_child();
        return false;
    }

    if(it == tokens.end()) {
        error("Expected while body on line {}, got end-of-file.\n", it->line);
        delete curr_node->pop_child();
        return false;
    }

    if(!parse_scope_or_single_statement(tokens, it, forNode)) {
        delete curr_node->pop_child();
        return false;
    }
    return true;
}

bool Parser::parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto functionNode = curr_node->add_child(new AST::Node(AST::Node::Type::FunctionDeclaration, *it));
    ++it;
    if(it->type != Tokenizer::Token::Type::Identifier) {
        error("Expected identifier in function declaration on line {}, got {}.\n", it->line, it->value);
        return false;
    }
    functionNode->token = *it;                              // Store the function name using its token.
    functionNode->value.type = GenericValue::Type::Integer; // TODO: Actually compute the return type (will probably just be part of the declation.)
    ++it;
    if(it->value != "(") {
        error("Expected '(' in function declaration on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }

    push_scope(); // FIXME: Restrict function parameters to this scope, do better.

    auto cleanup_on_error = [&]() {
        delete curr_node->pop_child();
        pop_scope();
    };

    ++it;
    // Parse parameters
    while(it != tokens.end() && it->value != ")") {
        if(!parse_variable_declaration(tokens, it, functionNode)) {
            cleanup_on_error();
            return false;
        }
        if(it->value == ",")
            ++it;
        else if(it->value != ")") {
            error("Expected ',' in function declaration argument list on line {}, got {}.\n", it->line, it->value);
            cleanup_on_error();
            return false;
        }
    }
    if(it == tokens.end() || it->value != ")") {
        error("Unmatched '(' in function declaration on line {}.\n", it->line);
        cleanup_on_error();
        return false;
    }

    ++it;
    if(it == tokens.end()) {
        error("Expected function body on line {}, got end-of-file.\n", it->line);
        cleanup_on_error();
        return false;
    }

    if(!parse_scope_or_single_statement(tokens, it, functionNode)) {
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
    if(!(it->type == Tokenizer::Token::Type::Control && it->value == "{")) {
        error("Expected '{{' after type declaration on line {}, got {}.\n", it->line, it->value);
        delete curr_node->pop_child();
        return false;
    }

    push_scope();
    ++it;

    while(it != tokens.end() && !(it->type == Tokenizer::Token::Type::Control && it->value == "}")) {
        if(!parse_variable_declaration(tokens, it, type_node)) {
            delete curr_node->pop_child();
            pop_scope();
            return false;
        }
        // Skip ';'
        if(it != tokens.end() && it->type == Tokenizer::Token::Type::Control && it->value == ";")
            ++it;
        // parse_variable_declaration may add an initialisation node, we'll make this a special case and add the default value as a child.
        if(type_node->children.back()->type == AST::Node::Type::BinaryOperator) {
            auto assignment_node = type_node->pop_child();
            auto rhs = assignment_node->pop_child();
            type_node->children.back()->add_child(rhs);
            delete assignment_node;
            // FIXME: Should we require a constexpr value here? In this case we could store it directly in the declaration node value.
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
    auto result = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), integer->value.value.as_int32_t);
    ++it;
    return true;
}

bool Parser::parse_float(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    auto floatNode = curr_node->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    floatNode->value.type = GenericValue::Type::Float;
    floatNode->value.flags = GenericValue::Flags::CompileConst;
    auto result = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), floatNode->value.value.as_float);
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
    strNode->value.value.as_string = it->value;
    ++it;
    return true;
}

bool Parser::parse_operator(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node) {
    assert(it->type == Tokenizer::Token::Type::Operator);
    // Unary operators
    if((it->value == "+" || it->value == "-" || it->value == "++" || it->value == "--") && curr_node->children.empty()) {
        AST::Node* unary_operator_node = curr_node->add_child(new AST::Node(AST::Node::Type::UnaryOperator, *it, AST::Node::SubType::Prefix));
        auto       precedence = operator_precedence.at(std::string(it->value));
        ++it;
        if(!parse_next_expression(tokens, it, unary_operator_node, precedence)) {
            delete curr_node->pop_child();
            return false;
        }
        resolve_operator_type(unary_operator_node);
        return true;
    }

    if((it->value == "++" || it->value == "--") && !curr_node->children.empty()) {
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
    if(it->value == "(" && curr_node->children.empty())
        return parse_next_expression(tokens, it, curr_node, 0);

    if(it->value == ")") {
        // Should have been handled by others parsing functions.
        error("Unmatched ')' on line {}.\n", it->line);
        return false;
    }
    if(it->value == "(") {
        const auto start = (it + 1);
        auto       function_node = curr_node->pop_child();
        // TODO: Check type of functionNode (is it actually callable?)
        // FIXME: FunctionCall uses its token for now to get the function name, but this in incorrect, it should look at the
        // first child and execute it to get a reference to the function. Using the function name token as a temporary workaround.
        auto call_node = curr_node->add_child(new AST::Node(AST::Node::Type::FunctionCall, function_node->token));
        // auto callNode = curr_node->parent->add_child(new AST::Node(AST::Node::Type::FunctionCall, *it));
        call_node->value.type = GenericValue::Type::Integer; // TODO: Actually compute the return type (Could it be depend on the actual parameter type at the call site? Like
                                                             // automatic (albeit maybe only on explicit request at function declaration) templating?).
        call_node->add_child(function_node);
        ++it;
        // Parse arguments
        while(it != tokens.end() && it->value != ")") {
            if(!parse_next_expression(tokens, it, call_node, 0)) {
                delete curr_node->pop_child();
                return false;
            }
            // Skip ","
            if(it != tokens.end() && it->value == ",")
                ++it;
        }
        if(it == tokens.end()) {
            error("[Parser] Syntax error: Unmatched '(' on line {}, got to end-of-file.\n", start->line);
            delete curr_node->pop_child();
            return false;
        }
        ++it;
        return true;
    }

    if(curr_node->children.empty()) {
        error("Syntax error: unexpected binary operator: {}.\n", *it);
        return false;
    }
    AST::Node* prevExpr = curr_node->pop_child();
    // TODO: Test type of previous node! (Must be an expression resolving to something operable)
    AST::Node* binaryOperatorNode = curr_node->add_child(new AST::Node(AST::Node::Type::BinaryOperator, *it));

    binaryOperatorNode->add_child(prevExpr);

    auto precedence = operator_precedence.at(std::string(it->value));
    ++it;
    if(it == tokens.end()) {
        error("[Parser::parse_operator] Syntax error: Reached end of document without a right-hand side operand for {} on line {}.\n", it->value, it->line);
        delete curr_node->pop_child();
        return false;
    }

    if(binaryOperatorNode->token.value == "[") {
        if(!parse_next_expression(tokens, it, binaryOperatorNode, 0)) {
            delete curr_node->pop_child();
            return false;
        }
        // Check for ']' and advance
        if(it->value != "]") {
            error("[Parser::parse_operator] Syntax error: Expected ']', got '{}' (line {}).\n", it->value, it->line);
            delete curr_node->pop_child();
            return false;
        }
        ++it;
    } else if(binaryOperatorNode->token.value == ".") {
        if(!(prevExpr->type == AST::Node::Type::Variable && prevExpr->value.type == GenericValue::Type::Composite)) {
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
        const auto identifier_name = it->value;
        const auto type = get_type(prevExpr->value.value.as_composite.type_id);
        bool       member_exists = false;
        for(const auto& c : type->children)
            if(c->token.value == identifier_name) {
                member_exists = true;
                break;
            }
        if(!member_exists) {
            error("[Parser] Syntax error: Member '{}' does not exists on type {}.\n", identifier_name, type->token.value);
            delete curr_node->pop_child();
            return false;
        }
        auto identifierNode = binaryOperatorNode->add_child(new AST::Node(AST::Node::Type::MemberIdentifier, *it));
        ++it;
    } else {
        // Lookahead for rhs
        if(!parse_next_expression(tokens, it, binaryOperatorNode, precedence)) {
            delete curr_node->pop_child();
            return false;
        }
    }

    // TODO: Test if types are compatible (with the operator and between each other)

    // Assignment: if variable is const and value is constexpr, mark the variable as constexpr.
    if(binaryOperatorNode->token.value == "=") {
        // FIXME: Workaround to allow more constant propagation. Should be done in a later stage.
        binaryOperatorNode->children[1] = AST::optimize(binaryOperatorNode->children[1]);
        if(binaryOperatorNode->children[0]->type == AST::Node::Type::Variable && binaryOperatorNode->children[0]->subtype == AST::Node::SubType::Const &&
           binaryOperatorNode->children[1]->type == AST::Node::Type::ConstantValue) {
            auto maybe_variable = get(binaryOperatorNode->children[0]->token.value);
            assert(maybe_variable);
            *maybe_variable = binaryOperatorNode->children[1]->value;
            maybe_variable->flags = maybe_variable->flags | GenericValue::Flags::CompileConst;
        }
    }

    resolve_operator_type(binaryOperatorNode);
    // Implicit casts
    if(binaryOperatorNode->value.type == GenericValue::Type::Float) {
        if(binaryOperatorNode->children[0]->value.type == GenericValue::Type::Integer) {
            auto castNode = new AST::Node(AST::Node::Type::Cast);
            castNode->value.type = GenericValue::Type::Float;
            castNode->parent = binaryOperatorNode;
            auto lhs = binaryOperatorNode->children[0];
            lhs->parent = nullptr;
            castNode->add_child(lhs);
            binaryOperatorNode->children[0] = castNode;
        }
        if(binaryOperatorNode->children[1]->value.type == GenericValue::Type::Integer) {
            auto castNode = new AST::Node(AST::Node::Type::Cast);
            castNode->value.type = GenericValue::Type::Float;
            castNode->parent = binaryOperatorNode;
            auto rhs = binaryOperatorNode->children[1];
            rhs->parent = nullptr;
            castNode->add_child(rhs);
            binaryOperatorNode->children[1] = castNode;
        }
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
    if(it != tokens.end() && (it->type == Tokenizer::Token::Type::Operator && it->value == "[")) {
        varDecNode->value.value.as_array.type = varDecNode->value.type;
        varDecNode->value.type = GenericValue::Type::Array;
        ++it;
        if(!parse_next_expression(tokens, it, varDecNode, 0)) {
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
        } else if(it->value != "]") {
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
    bool has_initializer = it != tokens.end() && (it->type == Tokenizer::Token::Type::Operator && it->value == "=");
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

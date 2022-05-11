#include <Parser.hpp>

bool Parser::parse_next_scope(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    if(it == tokens.end() || it->value != "{") {
        if(it == tokens.end())
            error("Syntax error: Expected scope opening, got end-of-document.\n");
        else
            error("Syntax error: Expected scope opening on line {}, got {}.\n", it->line, it->value);
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
        error("Syntax error: no matching 'closing bracket', got end-of-document.\n");
        return false;
    }

    auto scope = currNode->add_child(new AST::Node(AST::Node::Type::Scope));
    push_scope();
    bool r = parse({begin, end}, scope);
    pop_scope();
    it = end + 1;
    return r;
}

// TODO: Formely define wtf is an expression :)
bool Parser::parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, uint8_t precedence,
                                   bool search_for_matching_bracket) {
    if(it == tokens.end()) {
        error("Expected expression, got end-of-file.\n");
        return false;
    }

    auto exprNode = currNode->add_child(new AST::Node(AST::Node::Type::Expression));

    if(it->type == Tokenizer::Token::Type::Control && it->value == "(") {
        ++it;
        if(!parse_next_expression(tokens, it, exprNode, 0, true)) {
            delete currNode->pop_child();
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
                    delete currNode->pop_child();
                    return false;
                }
                break;
            }
            case Digits: {
                if(!parse_digits(tokens, it, exprNode)) {
                    delete currNode->pop_child();
                    return false;
                }
                break;
            }
            case Float: {
                if(!parse_float(tokens, it, exprNode)) {
                    delete currNode->pop_child();
                    return false;
                }
                break;
            }
            case CharLiteral: {
                if(!parse_char(tokens, it, exprNode)) {
                    delete currNode->pop_child();
                    return false;
                }
                break;
            }
            case StringLiteral: {
                if(!parse_string(tokens, it, exprNode)) {
                    delete currNode->pop_child();
                    return false;
                }
                break;
            }
            case Identifier: {
                if(!parse_identifier(tokens, it, exprNode)) {
                    delete currNode->pop_child();
                    return false;
                }
                break;
            }
            case Operator: {
                auto p = operator_precedence.at(std::string(it->value));
                if(p > precedence) {
                    if(!parse_operator(tokens, it, exprNode)) {
                        delete currNode->pop_child();
                        return false;
                    }
                } else {
                    stop = true;
                }
                break;
            }
            case Control: {
                if(it->value == "(") {
                    if(!parse_next_expression(tokens, it, exprNode, 0)) {
                        delete currNode->pop_child();
                        return false;
                    }
                } else if(it->value == ")") {
                    stop = true;
                } else if(it->value == "]") {
                    stop = true;
                }
                break;
            }
            default: {
                warn("[parse_next_expression] Unexpected Token Type '{}' ({}).\n", it->type, *it);
                delete currNode->pop_child();
                return false;
                break;
            }
        }
    }

    if(search_for_matching_bracket && (it == tokens.end() || it->value != ")")) {
        if(it == tokens.end())
            error("Unmatched '(' after reaching end-of-document.\n");
        else
            error("Unmatched '(' on line {}.\n", it->line);
        delete currNode->pop_child();
        return false;
    }

    // Evaluate Expression final return type
    // FIXME
    if(exprNode->children.size() == 1) {
        // FIXME: This is a hack to propagate the proper type of array elements accessed by subscript and should not be there. (Create a proper new node type?)
        if(exprNode->children[0]->type == AST::Node::Type::Variable && exprNode->children[0]->value.type == GenericValue::Type::Array &&
           exprNode->children[0]->children.size() == 1) {
            exprNode->value.type = exprNode->children[0]->value.value.as_array.type;
        } else {
            exprNode->value.type = exprNode->children[0]->value.type;
        }
    }

    if(search_for_matching_bracket) // Skip ending bracket
        ++it;

    return true;
}

bool Parser::parse_identifier(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    assert(it->type == Tokenizer::Token::Type::Identifier);
    // Function Call
    if(it + 1 != tokens.end() && (it + 1)->value == "(") {
        const auto start = (it + 1);
        // TODO: Check if the function has been declared (or is a built-in?) & Fetch corresponding FunctionDeclaration Node.
        auto callNode = currNode->add_child(new AST::Node(AST::Node::Type::FunctionCall, *it));
        callNode->value.type = GenericValue::Type::Integer; // TODO: Actually compute the return type (Could it be depend on the actual parameter type at the call site? Like
                                                            // automatic (albeit maybe only on explicit request at function declaration) templating?).
        it += 2;
        // Parse arguments
        while(it != tokens.end() && it->value != ")") {
            if(!parse_next_expression(tokens, it, callNode, 0)) {
                delete currNode->pop_child();
                return false;
            }
            if(it != tokens.end() && it->value == ",")
                ++it;
        }
        if(it == tokens.end()) {
            error("[Parser] Syntax error: Unmatched '(' on line {}, got to end-of-file.\n", start->line);
            delete currNode->pop_child();
            return false;
        }
        ++it;
        return true;
    } else { // Variable
        auto maybe_variable = get(it->value);
        if(!maybe_variable) {
            error("[Parser] Syntax Error: Variable '{}' has not been declared on line.\n", it->value, it->line);
            return false;
        }
        const auto& variable = *maybe_variable;

        auto varNode = currNode->add_child(new AST::Node(AST::Node::Type::Variable, *it));
        varNode->value.type = variable.type;
        // FIXME: The resulting node is not of the correct type, it should be the type of the array elements
        if(it + 1 != tokens.end() && (it + 1)->value == "[") { // Array accessor
            if(variable.type != GenericValue::Type::Array && variable.type != GenericValue::Type::String) {
                error("[Parser] Syntax Error: Subscript operator on variable '{}' on line {} which neither an array nor a string.\n", it->value, it->line);
                delete currNode->pop_child();
                return false;
            }
            if(variable.type == GenericValue::Type::Array) {
                varNode->value.value.as_array.type = variable.value.as_array.type;
                varNode->value.value.as_array.capacity = variable.value.as_array.capacity; // FIXME: Not known anymore at this stage
            }
            it += 2;
            // Get the index and add it as a child.
            if(!parse_next_expression(tokens, it, varNode, 0, false)) {
                delete currNode->pop_child();
                return false;
            }
            // TODO: Make sure this is an integer?
            assert(it->value == "]");
            ++it; // Skip ']'
        } else {
            ++it;
        }
        return true;
    }
}

bool Parser::parse_scope_or_single_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    if(it->value == "{") {
        if(!parse_next_scope(tokens, it, currNode))
            return false;
    } else {
        // FIXME: Probably
        auto end = it;
        while(end != tokens.end() && end->value != ";")
            ++end;
        if(!parse({it, end}, currNode))
            return false;
        it = end;
    }
    return true;
}

bool Parser::parse_while(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    auto whileNode = currNode->add_child(new AST::Node(AST::Node::Type::WhileStatement, *it));
    ++it;
    if(it->value != "(") {
        error("Expected '(' after while on line {}, got {}.\n", it->line, it->value);
        delete currNode->pop_child();
        return false;
    }
    // Parse condition and add it as first child
    ++it; // Point to the beginning of the expression until ')' ('search_for_matching_bracket': true)
    if(!parse_next_expression(tokens, it, whileNode, 0, true)) {
        delete currNode->pop_child();
        return false;
    }

    if(it == tokens.end()) {
        error("Expected while body on line {}, got end-of-file.\n", it->line);
        delete currNode->pop_child();
        return false;
    }

    if(!parse_scope_or_single_statement(tokens, it, whileNode)) {
        delete currNode->pop_child();
        return false;
    }
    return true;
}

bool Parser::parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    auto functionNode = currNode->add_child(new AST::Node(AST::Node::Type::FunctionDeclaration, *it));
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
        delete currNode->pop_child();
        return false;
    }

    push_scope(); // FIXME: Restrict function parameters to this scope, do better.

    auto cleanup_on_error = [&]() {
        delete currNode->pop_child();
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

    if(it->value == "{") {
        if(!parse_next_scope(tokens, it, functionNode)) {
            cleanup_on_error();
            return false;
        }
    } else {
        // FIXME: Probably
        auto end = it;
        while(end != tokens.end() && end->value != ";")
            ++end;
        if(!parse({it, end}, functionNode)) {
            cleanup_on_error();
            return false;
        }
    }

    pop_scope();
    return true;
}

bool Parser::parse_boolean(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    auto boolNode = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    boolNode->value.type = GenericValue::Type::Boolean;
    boolNode->value.value.as_bool = it->value == "true";
    ++it;
    return true;
}

bool Parser::parse_digits(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    auto integer = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    integer->value.type = GenericValue::Type::Integer;
    auto result = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), integer->value.value.as_int32_t);
    ++it;
    return true;
}

bool Parser::parse_float(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    auto floatNode = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    floatNode->value.type = GenericValue::Type::Float;
    auto result = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), floatNode->value.value.as_float);
    ++it;
    return true;
}

bool Parser::parse_char(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    auto strNode = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    strNode->value.type = GenericValue::Type::Char;
    strNode->value.value.as_char = it->value[0];
    ++it;
    return true;
}

bool Parser::parse_string(const std::span<Tokenizer::Token>&, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    auto strNode = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
    strNode->value.type = GenericValue::Type::String;
    strNode->value.value.as_string = it->value;
    ++it;
    return true;
}

bool Parser::parse_operator(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    // Unary operators
    if((it->value == "+" || it->value == "-" || it->value == "++" || it->value == "--") && currNode->children.empty()) {
        AST::Node* unaryOperatorNode = currNode->add_child(new AST::Node(AST::Node::Type::UnaryOperator, *it));
        auto       precedence = operator_precedence.at(std::string(it->value));
        ++it;
        if(!parse_next_expression(tokens, it, unaryOperatorNode, precedence)) {
            delete currNode->pop_child();
            return false;
        }
        resolve_operator_type(unaryOperatorNode);
        return true;
    }

    if((it->value == "++" || it->value == "--") && !currNode->children.empty()) {
        auto       prevNode = currNode->pop_child();
        AST::Node* unaryOperatorNode = currNode->add_child(new AST::Node(AST::Node::Type::UnaryOperator, *it));
        // auto   precedence = operator_precedence.at(std::string(it->value));
        // FIXME: How do we use the precedence here?
        unaryOperatorNode->add_child(prevNode);
        ++it;
        resolve_operator_type(unaryOperatorNode);
        return true;
    }

    if(currNode->children.empty()) {
        error("Syntax error: unexpected binary operator: {}.\n", *it);
        return false;
    }
    AST::Node* prevExpr = currNode->pop_child();
    // TODO: Test type of previous node! (Must be an expression resolving to something operable)
    AST::Node* binaryOperatorNode = currNode->add_child(new AST::Node(AST::Node::Type::BinaryOperator, *it));

    binaryOperatorNode->add_child(prevExpr);

    auto precedence = operator_precedence.at(std::string(it->value));
    if((it + 1) == tokens.end()) {
        error("Syntax error: Reached end of document without a right-hand side operand for {} on line {}.\n", it->value, it->line);
        delete currNode->pop_child();
        return false;
    }
    ++it;
    // Lookahead for rhs
    if(!parse_next_expression(tokens, it, binaryOperatorNode, precedence)) {
        delete currNode->pop_child();
        return false;
    }
    // TODO: Test if types are compatible (with the operator and between each other)

    resolve_operator_type(binaryOperatorNode);
    return true;
}

/* it must point to an type identifier
 * TODO: Handle non-built-it types.
 */
bool Parser::parse_variable_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
    assert(it->type == Tokenizer::Token::Type::BuiltInType);
    auto varDecNode = currNode->add_child(new AST::Node(AST::Node::Type::VariableDeclaration, *it));
    auto cleanup_on_error = [&]() { delete currNode->pop_child(); };
    varDecNode->value.type = parse_type(it->value);
    ++it;
    // Array declaration
    if(it != tokens.end() && it->value == "[") {
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
    if(next.type == Tokenizer::Token::Type::Identifier) {
        varDecNode->token = next; // We'll be getting the function name from the token. This may still be a FIXME?...
        if(!get_scope().declare_variable(*varDecNode, next.line)) {
            cleanup_on_error();
            return false;
        }
        ++it;
        // Also push a variable identifier for initialisation
        if(it != tokens.end() && it->value == "=") {
            auto varNode = currNode->add_child(new AST::Node(AST::Node::Type::Variable, next));
            varNode->value.type = varDecNode->value.type;
        }
    } else {
        error("[Parser] Syntax error: Expected Identifier for variable declaration, got {}.\n", next);
        cleanup_on_error();
        return false;
    }
    return true;
}

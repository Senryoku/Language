#pragma once

#include <cassert>
#include <charconv>
#include <optional>
#include <span>

#include <fmt/color.h>

#include <AST.hpp>
#include <Logger.hpp>
#include <Scope.hpp>
#include <Tokenizer.hpp>
#include <VariableStore.hpp>

class Parser : public Scoped {
  public:
    Parser() {
        // FIXME: Pushing an empty scope for now, we'll probably use that to provide some built-ins
        push_scope();
    }

    // FIXME: For many reasons (string creations...) Just correctly type the node from the start (i.e. token stage) I guess?
    inline static const std::unordered_map<std::string, uint8_t> operator_precedence{
        {"=", (uint8_t)0},  {"==", (uint8_t)1}, {"!=", (uint8_t)1}, {">", (uint8_t)1}, {"<", (uint8_t)1}, {">=", (uint8_t)1},
        {"<=", (uint8_t)1}, {"+", (uint8_t)2},  {"-", (uint8_t)2},  {"*", (uint8_t)3}, {"/", (uint8_t)3}, {"^", (uint8_t)4},
    };

    void resolve_operator_type(AST::Node* binaryOp) {
        assert(binaryOp->type == AST::Node::Type::BinaryOperator);
        // TODO
        if(binaryOp->token.value == "==" || binaryOp->token.value == "!=" || binaryOp->token.value == "<" || binaryOp->token.value == ">" || binaryOp->token.value == "=>" ||
           binaryOp->token.value == "<=")
            binaryOp->value.type = GenericValue::Type::Bool;
        else if(binaryOp->children[0]->value.type == GenericValue::Type::Integer && binaryOp->children[1]->value.type == GenericValue::Type::Integer)
            binaryOp->value.type = GenericValue::Type::Integer;
        else
            warn("Couldn't resolve operator return type (Missing impl.).\n");
    }

    std::optional<AST> parse(const std::span<Tokenizer::Token>& tokens, bool optimize = true) {
        std::optional<AST> ast(AST{});

        bool r = parse(tokens, &(*ast).getRoot());
        if(!r) {
            error("Error while parsing!\n");
            ast.reset();
        } else if(optimize) {
            ast->optimize();
        }

        return ast;
    }

    bool parse_next_scope(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        if(it == tokens.end() || it->value != "{") {
            if(it == tokens.end())
                error("Syntax error: Expected scope opening, got end-of-document.\n");
            else
                error("Syntax error: Expected scope opening on line {}, got {}.\n", it->line, it->value);
            return false;
        }
        auto begin = it + 1;
        auto end = begin + 1;
        while(end != tokens.end() && end->value != "}")
            ++end;
        if(end == tokens.end()) {
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
    bool parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, uint8_t precedence) {
        bool search_for_matching_bracket = false;
        if(it->type == Tokenizer::Token::Type::Control && it->value == "(") {
            precedence = 0;
            ++it;
            search_for_matching_bracket = true;
        }

        auto exprNode = currNode->add_child(new AST::Node(AST::Node::Type::Expression));

        bool stop = false;

        while(it != tokens.end() && it->value != ";" && it->value != "," && !stop) {
            // TODO: Check other types!
            switch(it->type) {
                using enum Tokenizer::Token::Type;
                case Digits: {
                    if(!parse_digits(tokens, it, exprNode))
                        return false;
                    break;
                }
                case Identifier: {
                    if(!parse_identifier(tokens, it, exprNode))
                        return false;
                    break;
                }
                case Operator: {
                    auto p = operator_precedence.at(std::string(it->value));
                    if(p > precedence) {
                        if(!parse_binary_operator(tokens, it, exprNode))
                            return false;
                    } else {
                        stop = true;
                    }
                    break;
                }
                case Control: {
                    if(it->value == "(") {
                        if(!parse_next_expression(tokens, it, exprNode, 0))
                            return false;
                    } else if(it->value == ")") {
                        stop = true;
                    }
                    break;
                }
                default: {
                    warn("[parse_next_expression] Unexpected Token Type '{}' ({}).\n", it->type, *it);
                    return false;
                    break;
                }
            }
        }

        if(search_for_matching_bracket && (it == tokens.end() || it->value != ")")) {
            error("Unmatched '(' on line {}.\n", it->line);
            return false;
        }

        // Evaluate Expression final return type
        // FIXME
        if(exprNode->children.size() == 1)
            exprNode->value.type = exprNode->children[0]->value.type;

        if(search_for_matching_bracket) // Skip ending bracket
            ++it;

        return true;
    }

    bool parse_identifier(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        // Function Call
        if(it + 1 != tokens.end() && (it + 1)->value == "(") {
            // TODO: Check if the function has been declared (or is a built-in?) & Fetch corresponding FunctionDeclaration Node.
            auto callNode = currNode->add_child(new AST::Node(AST::Node::Type::FunctionCall, *it));
            it += 2;
            // Parse arguments
            while(it != tokens.end() && it->value != ")") {
                if(!parse_next_expression(tokens, it, callNode, 0))
                    return false;
                if(it != tokens.end() && it->value == ",")
                    ++it;
            }
            if(it == tokens.end()) {
                error("Syntax error: Unmatched '(' on line {}, got to end-of-file.\n", it->line);
                return false;
            } else
                ++it;
            return true;
        } else { // Variable
            auto maybe_variable = get(it->value);
            if(!maybe_variable) {
                error("Syntax Error: Variable '{}' has not been declared.\n", it->value);
                return false;
            }

            const auto& variable = *maybe_variable;
            auto        varNode = currNode->add_child(new AST::Node(AST::Node::Type::Variable, *it));
            varNode->value.type = variable.type;
            ++it;
            return true;
        }
    }

    bool parse_scope_or_single_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
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
        }
        return true;
    }

    bool parse_while(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        auto whileNode = currNode->add_child(new AST::Node(AST::Node::Type::WhileStatement, *it));
        ++it;
        if(it->value != "(") {
            error("Expected '(' after while on line {}, got {}.\n", it->line, it->value);
            return false;
        }
        // Parse condition and add it as first child
        if(!parse_next_expression(tokens, it, whileNode, 0))
            return false;

        if(it == tokens.end()) {
            error("Expected while body on line {}, got end-of-file.\n", it->line);
            return false;
        }

        if(!parse_scope_or_single_statement(tokens, it, whileNode))
            return false;
        return true;
    }

    bool parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        auto functionNode = currNode->add_child(new AST::Node(AST::Node::Type::FunctionDeclaration, *it));
        ++it;
        if(it->type != Tokenizer::Token::Type::Identifier) {
            error("Expected identifier in function declaration on line {}, got {}.\n", it->line, it->value);
            return false;
        }
        functionNode->value.type = GenericValue::Type::String;
        functionNode->value.value.as_string = it->value;
        ++it;
        if(it->value != "(") {
            error("Expected '(' in function declaration on line {}, got {}.\n", it->line, it->value);
            return false;
        }

        push_scope(); // FIXME: Restrict function parameters to this scope, do better.

        // Parse parameters
        do {
            ++it;
            if(!parse_variable_declaration(tokens, it, functionNode))
                return false;
        } while(it != tokens.end() && it->value == ",");
        if(it == tokens.end() || it->value != ")") {
            error("Unmatched '(' in function declaration on line {}.\n", it->line);
            return false;
        }

        ++it;
        if(it == tokens.end()) {
            error("Expected function body on line {}, got end-of-file.\n", it->line);
            return false;
        }

        if(it->value == "{") {
            if(!parse_next_scope(tokens, it, functionNode))
                return false;
        } else {
            // FIXME: Probably
            auto end = it;
            while(end != tokens.end() && end->value != ";")
                ++end;
            if(!parse({it, end}, functionNode))
                return false;
        }

        pop_scope();

        return true;
    }

    bool parse_digits(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        auto integer = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
        integer->value.type = GenericValue::Type::Integer;
        auto result = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), integer->value.value.as_int32_t);
        ++it;
        return true;
    }

    bool parse_binary_operator(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
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
            return false;
        }
        ++it;
        // Lookahead for rhs
        if(!parse_next_expression(tokens, it, binaryOperatorNode, precedence))
            return false;
        // TODO: Test if types are compatible (with the operator and between each other)

        resolve_operator_type(binaryOperatorNode);
        return true;
    }

    /* it must point to an type identifier
     * TODO: Handle non-built-it types.
     */
    bool parse_variable_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        assert(it->type == Tokenizer::Token::Type::BuiltInType);
        if(it + 1 == tokens.end()) {
            error("Syntax error: expected Identifier for variable declaration, got Nothing.\n");
            return false;
        }
        auto next = *(it + 1); // Hopefully a name
        switch(next.type) {
            case Tokenizer::Token::Type::Identifier: {
                auto varDecNode = currNode->add_child(new AST::Node(AST::Node::Type::VariableDeclaration, *it));
                varDecNode->value.type = parse_type(it->value);
                // FIXME: Storing the variable name in the value... That probably shouldn't be there.
                varDecNode->value.value.as_string = {.begin = &*next.value.begin(), .size = static_cast<uint32_t>(next.value.length())};
                get_scope().declare_variable(varDecNode->value.type, next.value, it->line);
                it += 2;
                // Also push a variable identifier for initialisation
                if(it != tokens.end() && it->value == "=") { // FIXME: Better check for initialisation ("= | {" I guess?)
                    auto varNode = currNode->add_child(new AST::Node(AST::Node::Type::Variable, next));
                    varNode->value.type = varDecNode->value.type;
                }
                break;
            }
            default: error("Syntax error: expected Identifier for variable declaration, got {}.\n", next); return false;
        }
        return true;
    }

    bool parse(const std::span<Tokenizer::Token>& tokens, AST::Node* currNode) {
        auto it = tokens.begin();
        while(it != tokens.end()) {
            const auto& token = *it;
            switch(token.type) {
                case Tokenizer::Token::Type::Control: {
                    switch(token.value[0]) {
                        case '{': { // Open a new scope
                            currNode = currNode->add_child(new AST::Node(AST::Node::Type::Scope, *it));
                            push_scope();
                            ++it;
                            break;
                        }
                        case '}': { // Close nearest scope
                            while(currNode->type != AST::Node::Type::Scope && currNode->parent != nullptr)
                                currNode = currNode->parent;
                            if(currNode->type != AST::Node::Type::Scope) {
                                error("Syntax error: Unmatched '}' one line {}.\n", it->line);
                                return false;
                            }
                            currNode = currNode->parent;
                            pop_scope();
                            ++it;
                            break;
                        }
                        case '(': {
                            if(!parse_next_expression(tokens, it, currNode, 0))
                                return false;
                            break;
                        }
                        case ')': error("Unmatched ')' on line {}.\n", it->line); return false;
                        case ';': // Just do nothing
                            ++it;
                            break;
                        default:
                            warn("Unused token: {}.\n", *it);
                            ++it;
                            break;
                    }
                    break;
                }
                case Tokenizer::Token::Type::If: {
                    if(it + 1 == tokens.end() || (it + 1)->value != "(") {
                        error("Syntax error: expected '(' after 'if'.\n");
                        return false;
                    }

                    // TODO: Abstract this ("parse_next_expression"?)
                    auto begin = it + 1;
                    auto end = begin + 1;
                    while(end != tokens.end() && end->value != ")")
                        ++end;
                    if(end == tokens.end()) {
                        error("Syntax error: no matching ')'.\n");
                        return false;
                    }

                    auto ifNode = currNode->add_child(new AST::Node(AST::Node::Type::IfStatement));
                    auto expr = ifNode->add_child(new AST::Node(AST::Node::Type::Expression)); // TODO: Should be re-using something else
                    if(!parse({begin + 1, end}, expr))
                        return false;

                    it = end + 1;

                    if(!parse_next_scope(tokens, it, ifNode)) {
                        error("Syntax error: expected 'new scope' after 'if'.\n");
                        return false;
                    }
                    // TODO: Handle Else here?

                    break;
                }
                case Tokenizer::Token::Type::BuiltInType: {
                    if(!parse_variable_declaration(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::Return: {
                    auto returnNode = currNode->add_child(new AST::Node(AST::Node::Type::ReturnStatement, *it));
                    ++it;
                    if(!parse_next_expression(tokens, it, returnNode, 0))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::Digits: {
                    if(!parse_digits(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::Operator: {
                    if(!parse_binary_operator(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::Identifier: {
                    if(!parse_identifier(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::While: {
                    if(!parse_while(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::Function: {
                    if(!parse_function_declaration(tokens, it, currNode))
                        return false;
                    break;
                }
                default:
                    warn("Unused token: {}.\n", *it);
                    ++it;
                    break;
            }
        }
        return true;
    }
};
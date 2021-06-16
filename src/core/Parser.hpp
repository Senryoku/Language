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

    std::optional<AST> parse(const std::span<Tokenizer::Token>& tokens) {
        std::optional<AST> ast(AST{});

        bool r = parse(tokens, &(*ast).getRoot());
        if(!r) {
            error("Error while parsing!\n");
            ast.reset();
        } else {
            fmt::print("{}", *ast);
            ast->optimize();
            fmt::print("Optimized {}", *ast);
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

    // FIXME
    inline static const std::unordered_map<char, uint8_t> operator_precedence{
        {'=', 0}, {'+', 1}, {'-', 1}, {'*', 2}, {'/', 2},
    };

    // TODO: Formely define wtf is an expression :)
    bool parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, uint8_t precedence) {
        const auto& begin = it;
        auto        end = begin + 1;

        bool search_for_matching_bracket = false;
        if(it->type == Tokenizer::Token::Type::Control && it->value == "(") {
            precedence = 0;
            search_for_matching_bracket = true;
        }

        uint32_t opened_brackets = 0;
        while(end != tokens.end() && end->value != ";") {
            // TODO: Check other types!
            if(end->type == Tokenizer::Token::Type::Operator && operator_precedence.at(end->value[0]) <= precedence)
                break;
            if(end->type == Tokenizer::Token::Type::Control && end->value == "(")
                ++opened_brackets;
            if(end->type == Tokenizer::Token::Type::Control && end->value == ")") {
                if(search_for_matching_bracket && opened_brackets == 0)
                    break;
                --opened_brackets;
            }
            ++end;
        }

        if(search_for_matching_bracket && (end == tokens.end() || end->value != ")")) {
            error("Unmatched '(' on line {}.\n", it->line);
            return false;
        }

        auto exprNode = currNode->add_child(new AST::Node(AST::Node::Type::Expression));
        auto result = parse({search_for_matching_bracket ? begin + 1 : begin, end}, exprNode);

        if(search_for_matching_bracket) // Skip ending bracket
            ++end;

        it = end;

        return result;
    }

    bool parse_while(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        ++it;
        if(it->value != "(") {
            error("Expected '(' after while on line {}, got {}.", it->line, it->value);
            return false;
        }
        /*
        do {
            ++it;
        } while(it != tokens.end() && it->value != ")");
        if(it == tokens.end()) {
            error("Unmatched '(' for while statement on line {}.", it->line);
            return false;
        }
        */
        auto whileNode = currNode->add_child(new AST::Node(AST::Node::Type::WhileStatement));
        // Parse condition and add it as first child
        if(!parse_next_expression(tokens, it, currNode, 0))
            return false;

        ++it;
        if(it == tokens.end()) {
            error("Expected while body on line {}, got end-of-file.", it->line);
            return false;
        }

        if(it->value == "{") {
            if(!parse_next_scope(tokens, it, whileNode))
                return false;
        } else {
            // FIXME: Probably
            auto end = it;
            while(end != tokens.end() && end->value != ";")
                ++end;
            if(!parse({it, end}, whileNode))
                return false;
        }
        return true;
    }

    bool parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        if(it->type != Tokenizer::Token::Type::Identifier) {
            error("Expected identifier in function declaration on line {}, got {}.\n", it->line, it->value);
            return false;
        }
        ++it;
        if(it->value != "(") {
            error("Expected '(' in function declaration on line {}, got {}.\n", it->line, it->value);
            return false;
        }

        auto functionNode = currNode->add_child(new AST::Node(AST::Node::Type::FunctionDeclaration));

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
                case Tokenizer::Token::Type::Digits: {
                    auto integer = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
                    integer->value.type = GenericValue::Type::Integer;
                    auto result = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), integer->value.value.as_int32_t);
                    ++it;
                    break;
                }
                case Tokenizer::Token::Type::Operator: {
                    if(currNode->children.empty()) {
                        error("Syntax error: unexpected binary operator: {}.\n", *it);
                        return false;
                    }
                    AST::Node* prevExpr = currNode->pop_child();
                    // TODO: Test type of previous node! (Must be an expression resolving to something operable)
                    AST::Node* binaryOperatorNode = currNode->add_child(new AST::Node(AST::Node::Type::BinaryOperator, *it));

                    binaryOperatorNode->add_child(prevExpr);

                    auto precedence = operator_precedence.at(it->value[0]);
                    if((it + 1) == tokens.end()) {
                        error("Syntax error: Reached end of document without a right-hand side operand for {} on line {}.\n", it->value, it->line);
                        return false;
                    }
                    ++it;
                    // Lookahead for rhs
                    if(!parse_next_expression(tokens, it, binaryOperatorNode, precedence))
                        return false;
                    // TODO: Test if types are compatible (with the operator and between each other)

                    break;
                }
                case Tokenizer::Token::Type::Identifier: {
                    auto maybe_variable = get(it->value);
                    if(!maybe_variable) {
                        error("Syntax Error: Variable '{}' has not been declared.\n", it->value);
                        return false;
                    }

                    const auto& variable = *maybe_variable;
                    auto        varNode = currNode->add_child(new AST::Node(AST::Node::Type::Variable, *it));
                    varNode->value.type = variable.type;
                    ++it;
                    break;
                }
                case Tokenizer::Token::Type::Keyword: {
                    if(it->value == "function") {
                        ++it;
                        if(!parse_function_declaration(tokens, it, currNode))
                            return false;
                    } else if(it->value == "while") {
                        ++it;
                        if(!parse_while(tokens, it, currNode))
                            return false;
                    } else {
                        warn("Unimplemented keywords '{}'.", it->value);
                        ++it;
                    }
                    break;
                }
                default:
                    warn("Unused token: {}.\n", *it);
                    ++it;
                    break;
            }
        }
    }
};
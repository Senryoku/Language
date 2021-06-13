#pragma once

#include <cassert>
#include <charconv>
#include <span>

#include <AST.hpp>
#include <Tokenizer.hpp>

class Parser {
  public:
    enum class State
    {};

    bool parse(const std::span<Tokenizer::Token>& tokens) {
        AST ast;
        bool r = parse(tokens, &ast.getRoot());
        if(!r)
            fmt::print("Error while parsing!");
        else
            fmt::print("{}", ast);
        return r;
    }

    bool parse_next_scope(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode) {
        if(it == tokens.end() || it->value != "{") {
            return false;
        }
        auto begin = it + 1;
        auto end = begin + 1;
        while(end != tokens.end() && end->value != "}")
            ++end;
        if(end == tokens.end()) {
            fmt::print("Syntax error: no matching 'closing bracket'.\n");
            return false;
        }

        auto scope = currNode->add_child(new AST::Node(AST::Node::Type::Scope)); // FIXME: push_scope ? (has to handle variable declaration)
        bool r = parse({begin, end}, scope);

        it = end + 1;
        return r;
    }

    // FIXME
    inline static const std::unordered_map<char, uint8_t> operator_precedence{
        {'=', 0}, {'*', 1}, {'/', 1}, {'+', 2}, {'-', 2},
    };

    bool parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, uint8_t precedence) {
        const auto& begin = it;
        auto end = begin + 1;
        while(end != tokens.end() && end->value != ";") {
            // TODO: Check other types!
            if(end->type == Tokenizer::Token::Type::Operator && operator_precedence.at(end->value[0]) <= precedence)
                break;
            ++end;
        }

        auto exprNode = currNode->add_child(new AST::Node(AST::Node::Type::Expression));
        parse({begin, end}, exprNode);

        if(end == tokens.end())
            it = end;
        else
            it = end + 1;

        return true;
    }

    bool parse(const std::span<Tokenizer::Token>& tokens, AST::Node* currNode) {
        auto it = tokens.begin();
        while(it != tokens.end()) {
            const auto& token = *it;
            switch(token.type) {
                case Tokenizer::Token::Type::Control: {
                    switch(token.value[0]) {
                        case '{': {
                            auto newNode = currNode->add_child(new AST::Node(AST::Node::Type::Scope, *it));
                            currNode = newNode;
                            ++it;
                            break;
                        }
                        case '}': {
                            // Close nearest scope
                            while(currNode->type != AST::Node::Type::Scope && currNode->parent != nullptr)
                                currNode = currNode->parent;
                            if(currNode->type != AST::Node::Type::Scope) {
                                fmt::print("Syntax error: Unmatched '{'.\n"); // TODO: Do better.
                                return false;
                            }
                            currNode = currNode->parent;
                            ++it;
                            break;
                        }
                        default:
                            fmt::print("Unused token: {}.\n", *it);
                            ++it;
                            break;
                    }
                    break;
                }
                case Tokenizer::Token::Type::If: {
                    if(it + 1 == tokens.end() || (it + 1)->value != "(") {
                        fmt::print("Syntax error: expected '(' after 'if'.\n");
                        return false;
                    }

                    // TODO: Abstract this ("parse_next_expression"?)
                    auto begin = it + 1;
                    auto end = begin + 1;
                    while(end != tokens.end() && end->value != ")")
                        ++end;
                    if(end == tokens.end()) {
                        fmt::print("Syntax error: no matching ')'.\n");
                        return false;
                    }

                    auto ifNode = currNode->add_child(new AST::Node(AST::Node::Type::IfStatement));
                    auto expr = ifNode->add_child(new AST::Node(AST::Node::Type::Expression)); // TODO: Should be re-using something else
                    parse({begin + 1, end}, expr);

                    it = end + 1;

                    if(!parse_next_scope(tokens, it, ifNode)) {
                        fmt::print("Syntax error: expected 'new scope' after 'if'.\n");
                        return false;
                    }
                    // TODO: Handle Else here?

                    break;
                }
                case Tokenizer::Token::Type::BuiltInType: {
                    if(it + 1 == tokens.end()) {
                        fmt::print("Syntax error: expected Identifier for variable declaration, got Nothing.\n");
                        return false;
                    }
                    auto next = *(it + 1);
                    switch(next.type) {
                        case Tokenizer::Token::Type::Identifier: {
                            currNode->add_child(new AST::Node(AST::Node::Type::VariableDeclaration, *it));
                            // FIXME: Where do we store the identifier?
                            // TODO: Actually declare the variable for syntax checking :)
                            it += 2;
                            // Also push a variable identifier for initialisation
                            if(it != tokens.end() && it->value != ";") // FIXME: Better check for initialisation ("= | {" I guess?)
                                currNode->add_child(new AST::Node(AST::Node::Type::Variable, next));
                            break;
                        }
                        default:
                            fmt::print("Syntax error: expected Identifier for variable declaration, got {}.\n", next);
                            return false;
                    }
                    break;
                }
                case Tokenizer::Token::Type::Digits: {
                    currNode->add_child(new AST::Node(AST::Node::Type::Digits, *it));
                    ++it;
                    break;
                }
                case Tokenizer::Token::Type::Operator: {
                    if(currNode->children.empty()) {
                        fmt::print("Syntax error: unexpected binary operator: {}.\n", *it);
                        return false;
                    }
                    AST::Node* prevExpr = currNode->pop_child();
                    // TODO: Test type of previous node! (Must be an expression resolving to something operable)
                    AST::Node* binaryOperatorNode = currNode->add_child(new AST::Node(AST::Node::Type::BinaryOperator, *it));

                    binaryOperatorNode->add_child(prevExpr);

                    auto precedence = operator_precedence.at(it->value[0]);
                    ++it;
                    // Lookahead for rhs
                    parse_next_expression(tokens, it, binaryOperatorNode, precedence);

                    break;
                }
                case Tokenizer::Token::Type::Identifier: {
                    // TODO: Check variable declaration
                    currNode->add_child(new AST::Node(AST::Node::Type::Variable, *it));
                    ++it;
                    break;
                }
                default:
                    fmt::print("Unused token: {}.\n", *it);
                    ++it;
                    break;
            }
        }
    }
};
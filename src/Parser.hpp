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

                    currNode = currNode->add_child(new AST::Node(AST::Node::Type::IfStatement));
                    auto expr = currNode->add_child(new AST::Node(AST::Node::Type::Expression)); // TODO: Should be re-using something else
                    parse({begin + 1, end}, expr);

                    // TODO: Abstract this ("parse_next_expression"?)
                    it = end + 1;
                    if(it == tokens.end() || it->value != "{") {
                        fmt::print("Syntax error: expected 'new scope' after 'if'.\n");
                        return false;
                    }
                    begin = it + 1;
                    end = begin + 1;
                    while(end != tokens.end() && end->value != "}")
                        ++end;
                    if(end == tokens.end()) {
                        fmt::print("Syntax error: no matching 'closing bracket'.\n");
                        return false;
                    }
                    auto scope = currNode->add_child(new AST::Node(AST::Node::Type::Scope)); // FIXME: push_scope ? (has to handle variable declaration)
                    parse({begin, end}, scope);
                    it = end + 1;

                    currNode = currNode->parent;

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
                            currNode->add_child(new AST::Node(AST::Node::Type::VariableDeclaration, next));
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
                    AST::Node* newNode = currNode->add_child(new AST::Node(AST::Node::Type::BinaryOperator, *it));

                    newNode->add_child(prevExpr);

                    // FIXME: This is broken.
                    // TODO: Abstract this ("parse_next_expression"?)
                    auto begin = it + 1;
                    auto end = begin + 1;
                    while(end != tokens.end() && end->value != ";")
                        ++end;
                    if(end == tokens.end()) {
                        fmt::print("Syntax error: no matching ';'.\n"); // FIXME
                        return false;
                    }
                    parse({begin, end + 1}, newNode);

                    it = end + 1;
                    break;
                }
                case Tokenizer::Token::Type::Identifier: {
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
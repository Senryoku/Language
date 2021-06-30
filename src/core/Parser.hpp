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
        {"=", (uint8_t)0},  {"||", (uint8_t)1}, {"&&", (uint8_t)2}, {"==", (uint8_t)3}, {"!=", (uint8_t)3}, {">", (uint8_t)3}, {"<", (uint8_t)3}, {">=", (uint8_t)3},
        {"<=", (uint8_t)3}, {"<=", (uint8_t)3}, {"-", (uint8_t)4},  {"+", (uint8_t)4},  {"*", (uint8_t)5},  {"/", (uint8_t)5}, {"^", (uint8_t)5},
    };

    static void resolve_operator_type(AST::Node* binaryOp) {
        assert(binaryOp->type == AST::Node::Type::BinaryOperator);
        auto lhs = binaryOp->children[0]->value.type;
        auto rhs = binaryOp->children[1]->value.type;
        // FIXME: Here we're getting the item type if we're accessing array items. This should be done automatically elsewhere I think (wrap the indexed access in an expression, or
        // another node type?)
        if(lhs == GenericValue::Type::Array && binaryOp->children[0]->children.size() > 0)
            lhs = binaryOp->children[0]->value.value.as_array.type;
        if(rhs == GenericValue::Type::Array && binaryOp->children[1]->children.size() > 0)
            rhs = binaryOp->children[1]->value.value.as_array.type;
        binaryOp->value.type = GenericValue::resolve_operator_type(binaryOp->token.value, lhs, rhs);
        if(binaryOp->value.type == GenericValue::Type::Undefined) {
            warn("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}. Children:\n", binaryOp->token.line);
            fmt::print("LHS: {}RHS: {}\n", *binaryOp->children[0], *binaryOp->children[1]);
        }
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

    // Append to an existing AST and return the added children
    std::span<AST::Node*> parse(const std::span<Tokenizer::Token>& tokens, AST& ast) {
        auto first = ast.getRoot().children.size();
        bool r = parse(tokens, &(ast.getRoot()));
        if(!r)
            error("Error while parsing!\n");
        return std::span<AST::Node*>{ast.getRoot().children.begin() + first, ast.getRoot().children.end()};
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
                        case ')': // Should have been handled by others parsing functions.
                            error("Unmatched ')' on line {}.\n", it->line);
                            return false;
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
                    ++it;
                    if(it == tokens.end() || it->value != "(") {
                        error("Syntax error: expected '(' after 'if'.\n");
                        return false;
                    }
                    auto ifNode = currNode->add_child(new AST::Node(AST::Node::Type::IfStatement));
                    ++it;
                    if(!parse_next_expression(tokens, it, ifNode, 0, true))
                        return false;
                    if(!parse_scope_or_single_statement(tokens, it, ifNode)) {
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
                    if(!parse_next_expression(tokens, it, returnNode, 0)) {
                        delete returnNode;
                        currNode->children.pop_back();
                        return false;
                    }
                    returnNode->value.type = returnNode->children[0]->value.type;
                    break;
                }

                // Constants
                case Tokenizer::Token::Type::Boolean: {
                    if(!parse_boolean(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::Digits: {
                    if(!parse_digits(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::Float: {
                    if(!parse_float(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::StringLiteral: {
                    if(!parse_string(tokens, it, currNode))
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

  private:
    bool parse_next_scope(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, uint8_t precedence,
                               bool search_for_matching_bracket = false);
    bool parse_identifier(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_scope_or_single_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_while(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_boolean(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_digits(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_float(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_string(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_binary_operator(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_variable_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
};
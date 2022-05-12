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
    // FIXME: Pre and postfix versions of --/++ should have different precedences
    inline static const std::unordered_map<std::string, uint32_t> operator_precedence{
        {"=", 0u}, {"||", 1u}, {"==", 3u}, {"!=", 3u}, {">", 3u},  {"<", 3u},  {">=", 3u}, {"<=", 3u}, {"<=", 3u}, {"-", 4u}, {"+", 4u},
        {"*", 5u}, {"/", 5u},  {"%", 5u},  {"^", 5u},  {"++", 6u}, {"--", 6u}, {"(", 7u},  {"[", 7u},  {")", 7u},  {"]", 7u},
    };

    static void resolve_operator_type(AST::Node* opNode) {
        assert(opNode->type == AST::Node::Type::BinaryOperator || opNode->type == AST::Node::Type::UnaryOperator);
        if(opNode->type == AST::Node::Type::UnaryOperator) {
            auto rhs = opNode->children[0]->value.type;
            if(rhs == GenericValue::Type::Array && opNode->children[0]->children.size() > 0)
                rhs = opNode->children[0]->value.value.as_array.type;
            opNode->value.type = rhs;
        } else {
            auto lhs = opNode->children[0]->value.type;
            auto rhs = opNode->children[1]->value.type;
            // FIXME: Here we're getting the item type if we're accessing array items. This should be done automatically elsewhere I think (wrap the indexed access in an
            // expression, or another node type?)
            if(lhs == GenericValue::Type::Array && opNode->children[0]->children.size() > 0)
                lhs = opNode->children[0]->value.value.as_array.type;
            if(rhs == GenericValue::Type::Array && opNode->children[1]->children.size() > 0)
                rhs = opNode->children[1]->value.value.as_array.type;
            opNode->value.type = GenericValue::resolve_operator_type(opNode->token.value, lhs, rhs);
        }
        if(opNode->value.type == GenericValue::Type::Undefined) {
            warn("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}. Node:\n", opNode->token.line);
            fmt::print("{}\n", *opNode);
        }
    }

    std::optional<AST> parse(const std::span<Tokenizer::Token>& tokens, bool optimize = true) {
        std::optional<AST> ast(AST{});
        bool               r = parse(tokens, &(*ast).getRoot());
        if(!r) {
            error("Error while parsing!\n");
            ast.reset();
        } else if(optimize) {
            ast->optimize();
        }

        return ast;
    }

    // Append to an existing AST and return the added children
    AST::Node* parse(const std::span<Tokenizer::Token>& tokens, AST& ast) {
        // Adds a dummy root node to easily get rid of it on error.
        auto root = ast.getRoot().add_child(new AST::Node{AST::Node::Type::Root});
        bool r = parse(tokens, root);
        if(!r) {
            error("Error while parsing!\n");
            delete ast.getRoot().pop_child();
            return nullptr;
        }
        return root;
    }

    // Returns true if the next token exists and matches the supplied type and value.
    // Doesn't advance the iterator.
    bool peek(const std::span<Tokenizer::Token>& tokens, const std::span<Tokenizer::Token>::iterator& it, const Tokenizer::Token::Type& type, const std::string& value) {
        return it + 1 != tokens.end() && (it + 1)->type == type && (it + 1)->value == value;
    }

  private:
    bool parse(const std::span<Tokenizer::Token>& tokens, AST::Node* currNode) {
        currNode = currNode->add_child(new AST::Node(AST::Node::Type::Statement));
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
                        case ';':
                            currNode = currNode->parent;
                            currNode = currNode->add_child(new AST::Node(AST::Node::Type::Statement, *it));
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
                    if(!parse_next_expression(tokens, it, ifNode, 0, true)) {
                        delete currNode->pop_child();
                        return false;
                    }
                    if(!parse_scope_or_single_statement(tokens, it, ifNode)) {
                        error("Syntax error: expected 'new scope' after 'if'.\n");
                        delete currNode->pop_child();
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
                        delete currNode->pop_child();
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
                case Tokenizer::Token::Type::CharLiteral: {
                    if(!parse_char(tokens, it, currNode))
                        return false;
                    break;
                }
                case Tokenizer::Token::Type::StringLiteral: {
                    if(!parse_string(tokens, it, currNode))
                        return false;
                    break;
                }

                case Tokenizer::Token::Type::Operator: {
                    if(!parse_operator(tokens, it, currNode))
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
        // Remove empty statements (at end of file)
        if(currNode->type == AST::Node::Type::Statement && currNode->children.empty()) {
            auto tmp = currNode;
            currNode = currNode->parent;
            currNode->children.erase(std::find(currNode->children.begin(), currNode->children.end(), tmp));
            delete tmp;
        }
        return true;
    }

    bool parse_next_scope(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, uint32_t precedence,
                               bool search_for_matching_bracket = false);
    bool parse_identifier(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_scope_or_single_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_while(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_boolean(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_digits(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_float(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_char(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_string(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_operator(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
    bool parse_variable_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode);
};

#pragma once

#include <cassert>
#include <charconv>
#include <span>

#include <fmt/color.h>

#include <AST.hpp>
#include <Logger.hpp>
#include <Tokenizer.hpp>

class Parser {
  public:
    Parser() {
        // FIXME: Pushing an empty scope for now, we'll probably use that to provide some built-ins
        push_scope();
    }

    bool parse(const std::span<Tokenizer::Token>& tokens) {
        AST  ast;
        bool r = parse(tokens, &ast.getRoot());
        if(!r) {

            error("Error while parsing!");
        } else {
            fmt::print("{}", ast);
            ast.optimize();
            fmt::print("Optimized {}", ast);
        }

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

    bool parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, uint8_t precedence) {
        const auto& begin = it;
        auto        end = begin + 1;
        while(end != tokens.end() && end->value != ";") {
            // TODO: Check other types!
            if(end->type == Tokenizer::Token::Type::Operator && operator_precedence.at(end->value[0]) <= precedence)
                break;
            ++end;
        }

        auto exprNode = currNode->add_child(new AST::Node(AST::Node::Type::Expression));
        auto result = parse({begin, end}, exprNode);
        // TODO: Simplify by removing the Expression node if it has only one child? (and bring the child up in its place, ofc)

        it = end;

        return result;
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
                                error("Syntax error: Unmatched '{'.\n"); // TODO: Do better.
                                return false;
                            }
                            currNode = currNode->parent;
                            pop_scope();
                            ++it;
                            break;
                        }
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
                    if(it + 1 == tokens.end()) {
                        error("Syntax error: expected Identifier for variable declaration, got Nothing.\n");
                        return false;
                    }
                    auto next = *(it + 1); // Hopefully a name
                    switch(next.type) {
                        case Tokenizer::Token::Type::Identifier: {
                            currNode->add_child(new AST::Node(AST::Node::Type::VariableDeclaration, *it));
                            // FIXME: Where do we store the identifier?
                            get_scope().declare_variable(*it, next);
                            it += 2;
                            // Also push a variable identifier for initialisation
                            if(it != tokens.end() && it->value != ";") // FIXME: Better check for initialisation ("= | {" I guess?)
                                currNode->add_child(new AST::Node(AST::Node::Type::Variable, next));
                            break;
                        }
                        default: error("Syntax error: expected Identifier for variable declaration, got {}.\n", next); return false;
                    }
                    break;
                }
                case Tokenizer::Token::Type::Digits: {
                    auto integer = currNode->add_child(new AST::Node(AST::Node::Type::ConstantValue, *it));
                    integer->value_type = AST::Node::ValueType::Integer;
                    auto result = std::from_chars(&*(it->value.begin()), &*(it->value.begin()) + it->value.length(), integer->value.as_int32_t);
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
                    ++it;
                    // Lookahead for rhs
                    parse_next_expression(tokens, it, binaryOperatorNode, precedence);

                    break;
                }
                case Tokenizer::Token::Type::Identifier: {
                    if(!get_scope().is_declared(it->value)) {
                        error("Syntax Error: Variable '{}' has not been declared.\n", it->value);
                        return false;
                    }

                    currNode->add_child(new AST::Node(AST::Node::Type::Variable, *it));
                    ++it;
                    break;
                }
                default:
                    warn("Unused token: {}.\n", *it);
                    ++it;
                    break;
            }
        }
    }

    class Variable {
      public:
        enum class Type
        { Integer };

        Type type = Type::Integer;

      private:
    };

    // Hash & Comparison to let unordered_map find work with string_views (Heterogeneous Lookup)
    struct TransparentEqual : public std::equal_to<> {
        using is_transparent = void;
    };
    struct string_hash {
        using is_transparent = void;
        using key_equal = std::equal_to<>;             // Pred to use
        using hash_type = std::hash<std::string_view>; // just a helper local type
        size_t operator()(std::string_view txt) const { return hash_type{}(txt); }
        size_t operator()(const std::string& txt) const { return hash_type{}(txt); }
        size_t operator()(const char* txt) const { return hash_type{}(txt); }
    };

    class Scope {
      public:
        bool declare_variable(Tokenizer::Token type, Tokenizer::Token name) {
            if(is_declared(name.value)) {
                error("Error on line {}: Variable '{}' already declared.\n", type.line, name.value);
                return false;
            }
            if(type.value == "int")
                _variables[std::string{name.value}] = Variable{Variable::Type::Integer};
            else
                error("Error on line {}: Unimplemented type '{}'.\n", type.line, type.value);
            return true;
        }

        bool is_declared(const std::string_view& name) const {
            return _variables.contains(std::string{name});
            // return _variables.find(name) != _variables.end();
        }

        const std::unordered_map<std::string, Variable, string_hash, TransparentEqual>& get_variables() const { return _variables; }

      private:
        // FIXME: At some point we'll have ton consolidate these string_view to their final home... Maybe the lexer should have done it already.
        std::unordered_map<std::string, Variable, string_hash, TransparentEqual> _variables;
    };

    Scope&       get_scope() { return _scopes.top(); }
    const Scope& get_scope() const { return _scopes.top(); }

    Scope& push_scope() {
        _scopes.push(Scope{});
        return get_scope();
    }

    void pop_scope() { _scopes.pop(); }

  private:
    std::stack<Scope> _scopes;
};
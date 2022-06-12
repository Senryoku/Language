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
        {"=", 0u}, {"||", 1u}, {"&&", 2u}, {"==", 3u}, {"!=", 3u}, {">", 3u},  {"<", 3u},  {">=", 3u}, {"<=", 3u}, {"<=", 3u}, {"-", 4u},
        {"+", 4u}, {"*", 5u},  {"/", 5u},  {"%", 5u},  {"^", 5u},  {"++", 6u}, {"--", 6u}, {"(", 7u},  {"[", 7u},  {")", 7u},  {"]", 7u},
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
    bool parse(const std::span<Tokenizer::Token>& tokens, AST::Node* currNode);

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
    bool parse_variable_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* currNode, bool is_const = false);
};

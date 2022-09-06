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
    Parser(const Parser&) = default;
    Parser(Parser&&) = default;
    Parser& operator=(const Parser&) = default;
    Parser& operator=(Parser&&) = default;
    virtual ~Parser() = default;

    static const uint32_t max_precedence = static_cast<uint32_t>(-1);
    // FIXME: Pre and postfix versions of --/++ should have different precedences
    inline static const std::unordered_map<Tokenizer::Token::Type, uint32_t> operator_precedence{
        {Tokenizer::Token::Type::Assignment, 16u},    {Tokenizer::Token::Type::Or, 15u},          {Tokenizer::Token::Type::And, 14u},
        {Tokenizer::Token::Type::Xor, 12u},           {Tokenizer::Token::Type::Equal, 10u},       {Tokenizer::Token::Type::Different, 10u},
        {Tokenizer::Token::Type::Greater, 9u},        {Tokenizer::Token::Type::Lesser, 9u},       {Tokenizer::Token::Type::GreaterOrEqual, 9u},
        {Tokenizer::Token::Type::LesserOrEqual, 9u},  {Tokenizer::Token::Type::Substraction, 6u}, {Tokenizer::Token::Type::Addition, 6u},
        {Tokenizer::Token::Type::Multiplication, 5u}, {Tokenizer::Token::Type::Division, 5u},     {Tokenizer::Token::Type::Modulus, 5u},
        {Tokenizer::Token::Type::Increment, 3u},      {Tokenizer::Token::Type::Decrement, 3u},    {Tokenizer::Token::Type::OpenParenthesis, 2u},
        {Tokenizer::Token::Type::OpenSubscript, 2u},  {Tokenizer::Token::Type::MemberAccess, 2u}, {Tokenizer::Token::Type::CloseParenthesis, 2u},
        {Tokenizer::Token::Type::CloseSubscript, 2u},
    };

    bool is_unary_operator(Tokenizer::Token::Type type) {
        return type == Tokenizer::Token::Type::Addition || type == Tokenizer::Token::Type::Substraction || type == Tokenizer::Token::Type::Increment ||
               type == Tokenizer::Token::Type::Decrement;
    }

    void resolve_operator_type(AST::Node* op_node) {
        assert(op_node->type == AST::Node::Type::BinaryOperator || op_node->type == AST::Node::Type::UnaryOperator);
        if(op_node->type == AST::Node::Type::UnaryOperator) {
            auto rhs = op_node->children[0]->value.type;
            if(rhs == GenericValue::Type::Array && op_node->children[0]->children.size() > 0)
                rhs = op_node->children[0]->value.value.as_array.type;
            op_node->value.type = rhs;
        } else {
            if(op_node->token.value == ".") {
                assert(op_node->children[0]->value.type == GenericValue::Type::Composite);
                auto type_node = get_type(op_node->children[0]->value.value.as_composite.type_id);
                for(const auto& c : type_node->children)
                    if(c->token.value == op_node->children[1]->token.value) {
                        op_node->value.type = c->value.type;
                        if(op_node->value.type == GenericValue::Type::Composite)
                            op_node->value.value.as_composite.type_id = c->value.value.as_composite.type_id;
                        return;
                    }
                assert(false);
            }

            auto lhs = op_node->children[0]->value.type;
            auto rhs = op_node->children[1]->value.type;
            // FIXME: Here we're getting the item type if we're accessing array items. This should be done automatically elsewhere I think (wrap the indexed access in an
            // expression, or another node type?)
            if(lhs == GenericValue::Type::Array && op_node->children[0]->children.size() > 0)
                lhs = op_node->children[0]->value.value.as_array.type;
            if(rhs == GenericValue::Type::Array && op_node->children[1]->children.size() > 0)
                rhs = op_node->children[1]->value.value.as_array.type;
            op_node->value.type = GenericValue::resolve_operator_type(op_node->token.value, lhs, rhs);
        }
        if(op_node->value.type == GenericValue::Type::Undefined) {
            warn("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}. Node:\n", op_node->token.line);
            fmt::print("{}\n", *op_node);
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
    bool peek(const std::span<Tokenizer::Token>& tokens, const std::span<Tokenizer::Token>::iterator& it, const Tokenizer::Token::Type& type) {
        return it + 1 != tokens.end() && (it + 1)->type == type;
    }

  private:
    bool parse(const std::span<Tokenizer::Token>& tokens, AST::Node* curr_node);

    bool parse_next_scope(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_next_expression(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node, uint32_t precedence = max_precedence,
                               bool search_for_matching_bracket = false);
    bool parse_identifier(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_scope_or_single_statement(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_while(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_for(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_function_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_type_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_boolean(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_digits(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_float(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_char(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_string(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_operator(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node);
    bool parse_variable_declaration(const std::span<Tokenizer::Token>& tokens, std::span<Tokenizer::Token>::iterator& it, AST::Node* curr_node, bool is_const = false);
};

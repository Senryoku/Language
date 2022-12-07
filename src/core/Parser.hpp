#pragma once

#include <cassert>
#include <charconv>
#include <filesystem>
#include <optional>
#include <span>

#include <fmt/color.h>

#include <AST.hpp>
#include <Exception.hpp>
#include <Logger.hpp>
#include <ModuleInterface.hpp>
#include <Scope.hpp>
#include <Source.hpp>
#include <Tokenizer.hpp>

class Parser : public Scoped {
  public:
    Parser() = default;
    Parser(const Parser&) = default;
    Parser(Parser&&) = default;
    Parser& operator=(const Parser&) = default;
    Parser& operator=(Parser&&) = default;
    virtual ~Parser() = default;

    void set_source(const std::string& src) { _source = &src; }
    void set_cache_folder(const std::filesystem::path& path) { _cache_folder = path; }

    static const uint32_t max_precedence = static_cast<uint32_t>(-1);
    // FIXME: Pre and postfix versions of --/++ should have different precedences
    inline static const std::unordered_map<Token::Type, uint32_t> operator_precedence{
        {Token::Type::Assignment, 16u},    {Token::Type::Or, 15u},          {Token::Type::And, 14u},
        {Token::Type::Xor, 12u},           {Token::Type::Equal, 10u},       {Token::Type::Different, 10u},
        {Token::Type::Greater, 9u},        {Token::Type::Lesser, 9u},       {Token::Type::GreaterOrEqual, 9u},
        {Token::Type::LesserOrEqual, 9u},  {Token::Type::Substraction, 6u}, {Token::Type::Addition, 6u},
        {Token::Type::Multiplication, 5u}, {Token::Type::Division, 5u},     {Token::Type::Modulus, 5u},
        {Token::Type::Increment, 3u},      {Token::Type::Decrement, 3u},    {Token::Type::OpenParenthesis, 2u},
        {Token::Type::OpenSubscript, 2u},  {Token::Type::MemberAccess, 2u}, {Token::Type::CloseParenthesis, 2u},
        {Token::Type::CloseSubscript, 2u},
    };

    bool is_unary_operator(Token::Type type) {
        return type == Token::Type::Addition || type == Token::Type::Substraction || type == Token::Type::Increment || type == Token::Type::Decrement;
    }

    ValueType resolve_operator_type(Token::Type op, const ValueType& lhs, const ValueType& rhs) {
        using enum Token::Type;
        if(op == MemberAccess)
            return rhs;
        if(op == Assignment)
            return lhs;
        if(op == Equal || op == Different || op == Lesser || op == Greater || op == GreaterOrEqual || op == LesserOrEqual || op == And || op == Or)
            return ValueType::boolean();

        if(lhs.is_array && op == OpenSubscript) {
            auto type = lhs;
            type.is_array = false;
            return type;
        }

        if(lhs == rhs && lhs.is_primitive())
            return lhs;

        // Promote integer to Float
        if(lhs.is_primitive() && rhs.is_primitive() &&
           ((lhs.primitive == PrimitiveType::Float && rhs.primitive == PrimitiveType::Integer) ||
            (lhs.primitive == PrimitiveType::Integer && rhs.primitive == PrimitiveType::Float))) {
            return ValueType::floating_point();
        }

        return ValueType::undefined();
    }

    void resolve_operator_type(AST::UnaryOperator* op_node) {
        auto rhs = op_node->children[0]->value_type;
        op_node->value_type = rhs;

        if(op_node->value_type.is_undefined()) {
            warn("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}. Node:\n", op_node->token.line);
            fmt::print("{}\n", *static_cast<AST::Node*>(op_node));
        }
    }

    void resolve_operator_type(AST::BinaryOperator* op_node) {
        if(op_node->token.type == Token::Type::MemberAccess) {
            assert(op_node->children[0]->value_type.is_composite());
            auto type_node = get_type(op_node->children[0]->value_type.type_id);
            for(const auto& c : type_node->members())
                if(c->token.value == op_node->children[1]->token.value) {
                    op_node->value_type = c->value_type;
                    if(op_node->value_type.is_composite())
                        op_node->value_type.type_id = c->value_type.type_id;
                    return;
                }
            assert(false);
        }

        auto lhs = op_node->children[0]->value_type;
        auto rhs = op_node->children[1]->value_type;
        op_node->value_type = resolve_operator_type(op_node->token.type, lhs, rhs);

        if(op_node->value_type.is_undefined()) {
            warn("[Parser] Couldn't resolve operator return type (Missing impl.) on line {}. Node:\n", op_node->token.line);
            fmt::print("{}\n", *static_cast<AST::Node*>(op_node));
        }
    }

    std::optional<AST> parse(const std::span<Token>& tokens) {
        std::optional<AST> ast(AST{});
        try {
            bool r = parse(tokens, &(*ast).getRoot());
            if(!r) {
                error("Error while parsing!\n");
                ast.reset();
            }
        } catch(const Exception& e) {
            e.display();
            ast.reset();
        }
        return ast;
    }

    // Append to an existing AST and return the added children
    AST::Node* parse(const std::span<Token>& tokens, AST& ast) {
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
    bool peek(const std::span<Token>& tokens, const std::span<Token>::iterator& it, const Token::Type& type, const std::string& value) {
        return it + 1 != tokens.end() && (it + 1)->type == type && (it + 1)->value == value;
    }
    bool peek(const std::span<Token>& tokens, const std::span<Token>::iterator& it, const Token::Type& type) { return it + 1 != tokens.end() && (it + 1)->type == type; }

    const ModuleInterface& get_module_interface() const { return _module_interface; }
    ModuleInterface&       get_module_interface() { return _module_interface; }
    bool                   write_export_interface(const std::filesystem::path&) const;

  private:
    const std::string*    _source = nullptr;
    std::filesystem::path _cache_folder{"./lang_cache/"};

    ModuleInterface _module_interface;

    bool parse(const std::span<Token>& tokens, AST::Node* curr_node);

    bool                 parse_next_scope(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_next_expression(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node, uint32_t precedence = max_precedence,
                                               bool search_for_matching_bracket = false);
    bool                 parse_identifier(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_statement(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_scope_or_single_statement(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_while(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_for(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_method_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_function_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node,
                                                    AST::FunctionDeclaration::Flag flags = AST::FunctionDeclaration::Flag::None);
    bool                 parse_function_arguments(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_type_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    ValueType            parse_type(const std::span<Token>& tokens, std::span<Token>::iterator& it);
    AST::BoolLiteral*    parse_boolean(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    AST::IntegerLiteral* parse_digits(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    AST::FloatLiteral*   parse_float(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    AST::CharLiteral*    parse_char(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_string(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_operator(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_import(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node);
    bool                 parse_variable_declaration(const std::span<Token>& tokens, std::span<Token>::iterator& it, AST::Node* curr_node, bool is_const = false);

    void skip(const std::span<Token>& tokens, std::span<Token>::iterator& it, Token::Type token_type) {
        if(it != tokens.end() && it->type == token_type)
            ++it;
    }

    template<typename... Args>
    std::string point_error(Args&&... args) {
        if(_source)
            return ::point_error_find_line(*_source, args...);
        return "";
    }

    Token expect(const std::span<Token>& tokens, std::span<Token>::iterator& it, Token::Type token_type) {
        if(it == tokens.end()) {
            throw Exception(fmt::format("[Parser] Syntax error: Expected '{}', got end-of-file.", token_type));
        } else if(it->type != token_type) {
            throw Exception(fmt::format("[Parser] Syntax error: Expected '{}', got {}.", token_type, *it), point_error(*it));
        }
        Token token = *it;
        ++it;
        return token;
    }

    void check_function_call(AST::FunctionCall*, const AST::FunctionDeclaration*);
};

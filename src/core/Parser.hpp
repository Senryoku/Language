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

    std::optional<AST> parse(const std::span<Token>& tokens);
    // Append to an existing AST and return the added children
    AST::Node* parse(const std::span<Token>& tokens, AST& ast);

    std::vector<std::string> parse_dependencies(const std::span<Token>& tokens);

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
    TypeID               parse_type(const std::span<Token>& tokens, std::span<Token>::iterator& it);
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
            return ::point_error(*_source, args...);
        return "[Parser] _source not defined, cannot display the line.";
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

    static TypeID resolve_operator_type(Token::Type op, TypeID lhs, TypeID rhs);

    void resolve_operator_type(AST::UnaryOperator* op_node);
    void resolve_operator_type(AST::BinaryOperator* op_node);

    void check_function_call(AST::FunctionCall*, const AST::FunctionDeclaration*);

    void insert_defer_node(AST::Node* curr_node);
};

#pragma once

#include <cassert>
#include <charconv>
#include <vector>

#include <Tokenizer.hpp>

class AST {
  public:
    class Node {
      public:
        enum class Type
        {
            Root,
            Scope,
            Expression,
            IfStatement,
            ElseStatement,
            WhileStatement,
            VariableDeclaration,
            Variable,
            Digits,
            BinaryOperator
        };

        Node(Type type) : type(type) {
        }

        Node(Type type, Node& parent) : type(type), parent(&parent) {
        }

        Node(Type type, Tokenizer::Token token) : type(type), token(token) {
        }

        Type type;
        Node* parent = nullptr;
        std::vector<Node*> children;

        Tokenizer::Token token;

        Node* add_child(Node* n) {
            assert(n->parent == nullptr);
            children.push_back(n);
            n->parent = this;
            return n;
        }

        Node* pop_child() {
            AST::Node* c = children.back();
            children.pop_back();
            c->parent = nullptr;
            return c;
        }

        ~Node() {
            for(auto pNode : children)
                delete pNode;
        }
    };

    // Should be Node&
    Node push_node(Node::Type type) {
        // TODO
        return Node{type};
    }

    void close_scope() {
    }

    Node& getRoot() {
        return _root;
    }
    const Node& getRoot() const {
        return _root;
    }

  private:
    Node _root{Node::Type::Root};
};

template <>
struct fmt::formatter<AST> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for AST");
        return it;
    }
    template <typename FormatContext>
    auto format(const AST& t, FormatContext& ctx) {
        return format_to(ctx.out(), "AST Dump: {:}\n", t.getRoot());
    }
};

template <>
struct fmt::formatter<AST::Node> {
    constexpr static bool is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    size_t indent = 0;

    constexpr auto parse(format_parse_context& ctx) {
        auto begin = ctx.begin(), end = ctx.end();
        auto it = begin;
        // Check if reached the end of the range:
        // Allow only empty format.
        while(it != end && is_digit(*it))
            ++it;
        if(it != end && *it != '}')
            throw format_error("Invalid format for AST::Node");
        if(it != begin)
            std::from_chars(&*begin, &*it, indent);
        // Return an iterator past the end of the parsed range:
        return it;
    }

    template <typename FormatContext>
    auto format(const AST::Node& t, FormatContext& ctx) {
        auto r = ctx.out();
        for(size_t i = 1; i < indent; ++i)
            r = format_to(ctx.out(), "\t");
        if(indent > 0)
            r = format_to(ctx.out(), " -> ");
        r = format_to(ctx.out(), "Node({}) : {}\n", t.type, t.token);
        // auto indent_str = fmt::format("{}", indent + 1);
        // fmt::print(format);
        for(const auto c : t.children)
            r = format_to(r, "{:" + std::to_string(indent + 1) + "}", *c);
        // r = format_to(r, "{}", *c);

        return r;
    }
};

template <>
struct fmt::formatter<AST::Node::Type> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for AST::Node::Type");
        return it;
    }

    template <typename FormatContext>
    auto format(const AST::Node::Type& t, FormatContext& ctx) {
        switch(t) {
            case AST::Node::Type::Root:
                return format_to(ctx.out(), "Root");
            case AST::Node::Type::Expression:
                return format_to(ctx.out(), "Expression");
            case AST::Node::Type::IfStatement:
                return format_to(ctx.out(), "IfStatement");
            case AST::Node::Type::ElseStatement:
                return format_to(ctx.out(), "ElseStatement");
            case AST::Node::Type::WhileStatement:
                return format_to(ctx.out(), "WhileStatement");
            case AST::Node::Type::Scope:
                return format_to(ctx.out(), "Scope {");
            case AST::Node::Type::VariableDeclaration:
                return format_to(ctx.out(), "VariableDeclaration");
            case AST::Node::Type::Variable:
                return format_to(ctx.out(), "Variable");
            case AST::Node::Type::Digits:
                return format_to(ctx.out(), "Digits");
            case AST::Node::Type::BinaryOperator:
                return format_to(ctx.out(), "BinaryOperator");
            default:
                assert(false);
                return format_to(ctx.out(), "MissingFormat for AST::Node::Type!");
        }
    }
};
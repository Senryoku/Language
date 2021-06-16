#pragma once

#include <cassert>
#include <charconv>
#include <stack>
#include <vector>

#include <GenericValue.hpp>
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
            ReturnStatement,
            VariableDeclaration,
            FunctionDeclaration,
            Variable,
            ConstantValue,
            BinaryOperator,

            Undefined
        };

        Node(Type type) noexcept : type(type) {}
        Node(Type type, Node& parent) noexcept : type(type), parent(&parent) {}
        Node(Type type, Tokenizer::Token token) noexcept : type(type), token(token) {}
        Node(Node&& o) noexcept : type(o.type), parent(o.parent), token(std::move(o.token)), children(std::move(o.children)), value(o.value) {}
        Node& operator=(Node&& o) noexcept {
            type = o.type;
            parent = o.parent;
            token = std::move(o.token);
            children = std::move(o.children);
            value = o.value;
            return *this;
        }

        Type               type = Type::Undefined;
        Node*              parent = nullptr;
        Tokenizer::Token   token;
        std::vector<Node*> children;

        // ConstantValue & Variable
        GenericValue value{.type = GenericValue::Type::Undefined, .value{0}};

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

    inline Node&       getRoot() { return _root; }
    inline const Node& getRoot() const { return _root; }

    // Will perform some really basic optimisations
    void optimize();

  private:
    Node _root{Node::Type::Root};

    Node* optimize(Node*);
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
    constexpr static bool is_digit(char c) { return c >= '0' && c <= '9'; }

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
            r = format_to(ctx.out(), "   ");
        if(indent > 0)
            r = format_to(ctx.out(), "|- ");

        if(t.type == AST::Node::Type::ConstantValue) {
            r = format_to(ctx.out(), "Node({}) : {}\n", t.type, t.value);
        } else if(t.type == AST::Node::Type::Variable) {
            r = format_to(ctx.out(), "Node({}:{}) : {}\n", t.type, t.token.value, t.value);
        } else if(t.type == AST::Node::Type::FunctionDeclaration) {
            r = format_to(ctx.out(), "Node({}:{}) : {}\n", t.type, t.value.value.as_string, t.token);
        } else if(t.token.type == Tokenizer::Token::Type::Unknown) {
            r = format_to(ctx.out(), "Node({})\n", t.type);
        } else
            r = format_to(ctx.out(), "Node({}) : {}\n", t.type, t.token);

        for(const auto c : t.children)
            r = format_to(r, "{:" + std::to_string(indent + 1) + "}", *c);

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
            case AST::Node::Type::Root: return format_to(ctx.out(), "{}", "Root");
            case AST::Node::Type::Expression: return format_to(ctx.out(), "{}", "Expression");
            case AST::Node::Type::IfStatement: return format_to(ctx.out(), fg(fmt::color::orchid), "{}", "IfStatement");
            case AST::Node::Type::ElseStatement: return format_to(ctx.out(), fg(fmt::color::orchid), "{}", "ElseStatement");
            case AST::Node::Type::WhileStatement: return format_to(ctx.out(), fg(fmt::color::orchid), "{}", "WhileStatement");
            case AST::Node::Type::ReturnStatement: return format_to(ctx.out(), fg(fmt::color::orchid), "{}", "ReturnStatement");
            case AST::Node::Type::Scope: return format_to(ctx.out(), "{}", "Scope {");
            case AST::Node::Type::VariableDeclaration: return format_to(ctx.out(), fg(fmt::color::light_blue), "{}", "VariableDeclaration");
            case AST::Node::Type::FunctionDeclaration: return format_to(ctx.out(), fg(fmt::color::light_yellow), "{}", "FunctionDeclaration");
            case AST::Node::Type::Variable: return format_to(ctx.out(), fg(fmt::color::light_blue), "{}", "Variable");
            case AST::Node::Type::ConstantValue: return format_to(ctx.out(), "{}", "ConstantValue");
            case AST::Node::Type::BinaryOperator: return format_to(ctx.out(), "{}", "BinaryOperator");
            default: assert(false); return format_to(ctx.out(), "{}", "MissingFormat for AST::Node::Type!");
        }
    }
};
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
            Variable,
            FunctionDeclaration,
            FunctionCall,
            BuiltInFunctionDeclaration,
            // BuiltInFunctionCall,
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

template<>
struct fmt::formatter<AST> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for AST");
        return it;
    }
    template<typename FormatContext>
    auto format(const AST& t, FormatContext& ctx) {
        return format_to(ctx.out(), "AST Dump: {:}\n", t.getRoot());
    }
};

template<>
struct fmt::formatter<AST::Node> {
    constexpr static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    std::string indent = "";

    constexpr auto parse(format_parse_context& ctx) {
        auto begin = ctx.begin(), end = ctx.end();
        auto it = begin;
        while(it != end && *it != '}')
            ++it;
        if(it != end && *it != '}')
            throw format_error("Invalid format for AST::Node");
        if(it != begin)
            indent = std::string{begin, it};
        // Return an iterator past the end of the parsed range:
        return it;
    }

    template<typename FormatContext>
    auto format(const AST::Node& t, FormatContext& ctx) {
        auto r = ctx.out();
        for(size_t i = 0; i < indent.length(); ++i) {
            const char c = indent[i];
            if(i == indent.length() - 1) {
                if(c == 'i')
                    r = format_to(ctx.out(), fg(fmt::color::dim_gray), "├─");
                else if(c == 'e')
                    r = format_to(ctx.out(), fg(fmt::color::dim_gray), "╰─");
            } else {
                if(c == 'i')
                    r = format_to(ctx.out(), fg(fmt::color::dim_gray), "│ ");
                else if(c == 'e')
                    r = format_to(ctx.out(), fg(fmt::color::dim_gray), "  ");
            }
        }

        switch(t.type) {
            case AST::Node::Type::ConstantValue: r = format_to(ctx.out(), "{}:{}", t.type, t.value); break;
            case AST::Node::Type::ReturnStatement: r = format_to(ctx.out(), "{}:{}", t.type, t.value.type); break;
            case AST::Node::Type::WhileStatement: r = format_to(ctx.out(), "{}", t.type); break;
            case AST::Node::Type::Variable: r = format_to(ctx.out(), "{}:{}:{}", t.type, t.token.value, t.value.type); break;
            case AST::Node::Type::FunctionDeclaration: r = format_to(ctx.out(), "{}:{}", t.type, t.token.value); break;
            case AST::Node::Type::FunctionCall: r = format_to(ctx.out(), "{}:{}()", t.type, t.token.value); break;
            case AST::Node::Type::VariableDeclaration: r = format_to(ctx.out(), "{}:{} {}", t.type, t.value.type, t.token.value); break;
            case AST::Node::Type::BinaryOperator:
                r = format_to(ctx.out(), "{} {}:{}", fmt::format(fmt::emphasis::bold | fg(fmt::color::black) | bg(fmt::color::dim_gray), t.token.value), t.type, t.value.type);
                break;
            default: r = format_to(ctx.out(), "{}", t.type);
        }

        auto token_str = t.token.type == Tokenizer::Token::Type::Unknown ? "None" : fmt::format("{}", t.token);
        // FIXME: This would be cool to use the actual token_str length here, but computing the printed length (stripping style/control characters) isn't trivial.
        const auto length = 60;
        // Forward then backward
        // r = format_to(ctx.out(), "\033[999C\033[{}D{}\n", length, token_str);
        // Backward then forward
        r = format_to(ctx.out(), "\033[999D\033[{}C{}\n", 50, token_str);

        for(size_t i = 0; i < t.children.size(); ++i)
            r = format_to(r, "{:" + indent + (i == t.children.size() - 1 ? "e" : "i") + "}", *t.children[i]);

        return r;
    }
};

template<>
struct fmt::formatter<AST::Node::Type> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for AST::Node::Type");
        return it;
    }

    template<typename FormatContext>
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
            case AST::Node::Type::FunctionCall: return format_to(ctx.out(), fg(fmt::color::light_yellow), "{}", "FunctionCall");
            case AST::Node::Type::Variable: return format_to(ctx.out(), fg(fmt::color::light_blue), "{}", "Variable");
            case AST::Node::Type::ConstantValue: return format_to(ctx.out(), "{}", "ConstantValue");
            case AST::Node::Type::BinaryOperator: return format_to(ctx.out(), "{}", "BinaryOperator");
            default: assert(false); return format_to(ctx.out(), "{}", "MissingFormat for AST::Node::Type!");
        }
    }
};
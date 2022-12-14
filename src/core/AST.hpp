#pragma once

#include <algorithm>
#include <cassert>
#include <charconv>
#include <span>
#include <stack>
#include <vector>

#include <Tokenizer.hpp>
#include <PrimitiveType.hpp>

class AST {
  public:
    struct Node {
        enum class Type {
            Root,
            Statement,
            Defer,
            Scope,
            Expression,
            IfStatement,
            ElseStatement,
            WhileStatement,
            ForStatement,
            ReturnStatement,
            VariableDeclaration,
            Variable,
            FunctionDeclaration, // First n children : Arguments, Last child: Function Body, Name stored in token, Flags in value.as_int
            FunctionCall,
            TypeDeclaration,
            MemberIdentifier,
            Cast,
            LValueToRValue,
            GetPointer,
            ConstantValue,
            UnaryOperator,
            BinaryOperator,
            Dereference,

            Undefined
        };

        enum class SubType {
            Prefix,
            Postfix,

            Const,

            Undefined,
        };

        Node() = default;
        Node(Token t) noexcept : token(t) {}
        Node(Type type) noexcept : type(type) {}
        Node(Type type, Node& parent) noexcept : type(type), parent(&parent) {}
        Node(Type type, Token token, SubType subtype = SubType::Undefined) noexcept : type(type), subtype(subtype), token(token) {}
        Node(Node&& o) noexcept : type(o.type), subtype(o.subtype), parent(o.parent), token(std::move(o.token)), children(std::move(o.children)) {}
        Node& operator=(Node&& o) noexcept {
            type = o.type;
            parent = o.parent;
            token = std::move(o.token);
            children = std::move(o.children);
            return *this;
        }

        virtual ~Node() {
            for(auto pNode : children)
                delete pNode;
        }

        Type               type = Type::Undefined;
        SubType            subtype = SubType::Undefined;
        Node*              parent = nullptr;
        TypeID             type_id = InvalidTypeID;
        Token              token;
        std::vector<Node*> children;

        Node* add_child(Node* n);
        Node* pop_child();
        // Insert a node between this and its nth child
        Node* insert_between(size_t n, Node* node);
    };

    struct TypeDeclaration : public Node {
        TypeDeclaration(Token t) : Node(Node::Type::TypeDeclaration, t) {}

        const auto& name() const { return token.value; }
        const auto& members() const { return children; }
    };

    struct FunctionDeclaration : public Node {
        FunctionDeclaration(Token t) : Node(Node::Type::FunctionDeclaration, t) {}

        enum Flag : uint8_t {
            None     = 0,
            Exported = 1 << 0,
            Variadic = 1 << 1,
            Extern   = 1 << 2, // Implemented in another module (no body) and disable name mangling.
            BuiltIn  = 1 << 3,
            Imported = 1 << 4, // Like Extern, but with name mangling
        };

        Flag flags = Flag::None;

        auto       name() const { return token.value; }
        AST::Node* body() {
            if(children.empty() || (children.back()->type != AST::Node::Type::Scope && children.back()->type != AST::Node::Type::Root))
                return nullptr;
            return children.back();
        }
        const AST::Node* body() const {
            // FIXME: This is really bad. There should be a consistent way to tell if a function is defined (i.e. has a body) or not.
            if(children.empty() || (children.back()->type != AST::Node::Type::Scope && children.back()->type != AST::Node::Type::Root))
                return nullptr;
            return children.back();
        }
        auto arguments() {
            if(children.size() == 0)
                return std::span<AST::Node*>();
            if(!body())
                return std::span<AST::Node*>(children);
            return std::span<AST::Node*>(children.data(), std::max<int>(0, static_cast<int>(children.size()) - 1));
        }
        const auto arguments() const {
            if(children.size() == 0)
                return std::span<const AST::Node* const>();
            if(!body())
                return std::span<const AST::Node* const>(children);
            return std::span<const AST::Node* const>(children.data(), std::max<int>(0, static_cast<int>(children.size()) - 1));
        }

        std::string mangled_name() const;
    };

    struct FunctionCall : public Node {
        FunctionCall(Token t) : Node(Node::Type::FunctionCall, t) {}

        FunctionDeclaration::Flag flags = FunctionDeclaration::None;

        auto       function() { return children[0]; }
        auto       arguments() { return std::span<AST::Node*>(children.data() + 1, std::max<int>(0, static_cast<int>(children.size()) - 1)); }
        const auto arguments() const { return std::span<const AST::Node* const>(children.data() + 1, std::max<int>(0, static_cast<int>(children.size()) - 1)); }

        void set_argument(size_t idx, AST::Node* n) {
            if(n)
                assert(n->parent == nullptr);
            if(children[idx + 1])
                children[idx + 1]->parent = nullptr;
            children[idx + 1] = n;
            if(n) n->parent = this;
        }

        std::string mangled_name() const;
    };

    struct Defer : public Node {
        Defer(Token t) : Node(Node::Type::Defer, t) {}
    };

    struct Scope : public Node {
        Scope(Token t) : Node(Node::Type::Scope, t) {}

        AST::Defer* defer = nullptr;

        ~Scope() override {
            if(defer)
                delete defer;
        }
    };

    struct VariableDeclaration : public Node {
        VariableDeclaration(Token t) : Node(Node::Type::VariableDeclaration, t) {}

        std::string_view name;
    };

    struct Variable : public Node {
        Variable(Token t) : Node(Node::Type::Variable, t) {}

        std::string_view name;
    };

    struct BinaryOperator : public Node {
        BinaryOperator(Token t) : Node(Node::Type::BinaryOperator, t) {}

        Token::Type operation() const { return token.type; }
        Node*       lhs() const { return children[0]; }
        Node*       rhs() const { return children[1]; }
    };

    struct UnaryOperator : public Node {
        UnaryOperator(Token t) : Node(Node::Type::UnaryOperator, t) {}

        enum class Flag {
            None = 0,
            Prefix,
            Postfix,
        };

        Flag flags = Flag::None;

        Node* argument() const { return children[0]; }
    };

    struct MemberIdentifier : public Node {
        MemberIdentifier(Token t) : Node(Node::Type::MemberIdentifier, t) {}

        uint32_t index = 0;

        std::string_view get_name() const { return token.value; }
    };

    template<typename T>
    struct Literal : public Node {
        Literal(Token t) : Node(Type::ConstantValue, t) {}
        T value = T{};
    };

    struct BoolLiteral : public Literal<bool> {
        BoolLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Boolean; }
    };

    struct CharLiteral : public Literal<char> {
        CharLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Char; }
    };

    struct IntegerLiteral : public Literal<int32_t> {
        IntegerLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Integer; }
    };

    struct FloatLiteral : public Literal<float> {
        FloatLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Float; }
    };

    struct StringLiteral : public Literal<std::string_view> {
        StringLiteral(Token t) : Literal(t) { type_id = PrimitiveType::CString; }
    };

    inline Node&       get_root() { return _root; }
    inline const Node& get_root() const { return _root; }

  private:
    Node _root{Node::Type::Root};
};

inline AST::UnaryOperator::Flag operator|(AST::UnaryOperator::Flag lhs, AST::UnaryOperator::Flag rhs) {
    return static_cast<AST::UnaryOperator::Flag>(static_cast<std::underlying_type_t<AST::UnaryOperator::Flag>>(lhs) |
                                                 static_cast<std::underlying_type_t<AST::UnaryOperator::Flag>>(rhs));
}

inline AST::UnaryOperator::Flag operator|=(AST::UnaryOperator::Flag& lhs, AST::UnaryOperator::Flag rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline AST::UnaryOperator::Flag operator&(AST::UnaryOperator::Flag lhs, AST::UnaryOperator::Flag rhs) {
    return static_cast<AST::UnaryOperator::Flag>(static_cast<std::underlying_type_t<AST::UnaryOperator::Flag>>(lhs) &
                                                 static_cast<std::underlying_type_t<AST::UnaryOperator::Flag>>(rhs));
}

inline AST::FunctionDeclaration::Flag operator|(AST::FunctionDeclaration::Flag lhs, AST::FunctionDeclaration::Flag rhs) {
    return static_cast<AST::FunctionDeclaration::Flag>(static_cast<std::underlying_type_t<AST::FunctionDeclaration::Flag>>(lhs) |
                                                       static_cast<std::underlying_type_t<AST::FunctionDeclaration::Flag>>(rhs));
}

inline AST::FunctionDeclaration::Flag operator|=(AST::FunctionDeclaration::Flag& lhs, AST::FunctionDeclaration::Flag rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline AST::FunctionDeclaration::Flag operator&(AST::FunctionDeclaration::Flag lhs, AST::FunctionDeclaration::Flag rhs) {
    return static_cast<AST::FunctionDeclaration::Flag>(static_cast<std::underlying_type_t<AST::FunctionDeclaration::Flag>>(lhs) &
                                                       static_cast<std::underlying_type_t<AST::FunctionDeclaration::Flag>>(rhs));
}

#include <formatters/ASTFormat.hpp>

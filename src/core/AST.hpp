#pragma once

#include <cassert>
#include <charconv>
#include <span>
#include <stack>
#include <vector>
#include <algorithm>

#include <Tokenizer.hpp>

using TypeID = uint64_t;
constexpr TypeID InvalidTypeID = static_cast<TypeID>(-1);

enum class PrimitiveType {
    Integer,
    Float,
    Char,
    Boolean,
    String,
    Void,
    Composite,
    Undefined
};

std::string   serialize(PrimitiveType type);
PrimitiveType parse_primitive_type(const std::string_view& str);

struct ValueType {
    bool is_primitive() const { return !is_array && primitive != PrimitiveType::Composite; }
    bool is_composite() const { return primitive == PrimitiveType::Composite; }
    bool is_undefined() const { return primitive == PrimitiveType::Undefined; }

    PrimitiveType primitive = PrimitiveType::Undefined;
    bool          is_array = false;
    size_t        capacity = 0;
    bool          is_reference = false; // FIXME: Do we need both?
    bool          is_pointer = false;

    TypeID type_id = InvalidTypeID;

    bool operator==(const ValueType&) const = default;

    ValueType get_element_type() const {
        auto copy = *this;
        copy.is_array = false; // FIXME: This is obviously wrong.
        return copy;
    }

    ValueType get_pointed_type() const { // FIXME: Also wrong, what about int**?
        assert(is_reference || is_pointer);
        auto copy = *this;
        copy.is_reference = false;
        copy.is_pointer = false;
        return copy;
    }

    std::string serialize() const;

    static ValueType integer() { return ValueType{.primitive = PrimitiveType::Integer}; }
    static ValueType floating_point() { return ValueType{.primitive = PrimitiveType::Float}; }
    static ValueType character() { return ValueType{.primitive = PrimitiveType::Char}; }
    static ValueType boolean() { return ValueType{.primitive = PrimitiveType::Boolean}; }
    static ValueType string() { return ValueType{.primitive = PrimitiveType::String}; }
    static ValueType void_t() { return ValueType{.primitive = PrimitiveType::Void}; }
    static ValueType undefined() { return ValueType{.primitive = PrimitiveType::Undefined}; }
};

struct TypeMember {
    std::string_view name;
    ValueType        type;
};

class AST {
  public:
    struct Node {
        enum class Type {
            Root,
            Statement,
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
            ConstantValue,
            UnaryOperator,
            BinaryOperator,

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
        ValueType          value_type;
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
        const auto& type_id() const { return value_type.type_id; }
        const auto& members() const { return children; }
    };

    struct FunctionDeclaration : public Node {
        FunctionDeclaration(Token t) : Node(Node::Type::FunctionDeclaration, t) {}

        enum Flag : int {
            None = 0,
            Exported = 1 << 0,
            Variadic = 1 << 1,
        };

        Flag flags = Flag::None;

        auto       name() const { return token.value; }
        auto       body() { return children.back(); }
        const auto body() const { return children.back(); }
        auto       arguments() { return std::span<AST::Node*>(children.data(), std::max<int>(0, static_cast<int>(children.size()) - 1));
        }
        const auto arguments() const { return std::span<const AST::Node* const>(children.data(), std::max<int>(0, static_cast<int>(children.size()) - 1));
        }
    };

    struct FunctionCall : public Node {
        FunctionCall(Token t) : Node(Node::Type::FunctionCall, t) {}

        FunctionDeclaration::Flag flags = FunctionDeclaration::None;

        auto       function() { return children[0]; }
        auto arguments() { return std::span<AST::Node*>(children.data() + 1, std::max<int>(0, static_cast<int>(children.size()) - 1));
        }
        const auto arguments() const { return std::span<const AST::Node* const>(children.data() + 1, std::max<int>(0, static_cast<int>(children.size()) - 1));
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
        BoolLiteral(Token t) : Literal(t) { value_type.primitive = PrimitiveType::Boolean; }
    };

    struct CharLiteral : public Literal<char> {
        CharLiteral(Token t) : Literal(t) { value_type.primitive = PrimitiveType::Char; }
    };

    struct IntegerLiteral : public Literal<int32_t> {
        IntegerLiteral(Token t) : Literal(t) { value_type.primitive = PrimitiveType::Integer; }
    };

    struct FloatLiteral : public Literal<float> {
        FloatLiteral(Token t) : Literal(t) { value_type.primitive = PrimitiveType::Float; }
    };

    struct StringLiteral : public Literal<std::string_view> {
        StringLiteral(Token t) : Literal(t) { value_type.primitive = PrimitiveType::String; }
    };

    struct ArrayLiteral : public Node {
        ArrayLiteral(Token t) : Node(t) { value_type.is_array = true; }
    };

    inline Node&       getRoot() { return _root; }
    inline const Node& getRoot() const { return _root; }

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

#pragma once

#include <algorithm>
#include <cassert>
#include <charconv>
#include <span>
#include <stack>
#include <vector>

#include <FlyString.hpp>
#include <PrimitiveType.hpp>
#include <Tokenizer.hpp>

class AST {
  public:
    struct Node {
        enum class Type {
            Root,
            Statement,
            Defer, // This is not used anymore. It used to hold calls to destructors, but they are now 'inlined' in the AST, allowing for more control. I keeping it around for now
                   // as it could be used for an actual defer feature.
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

        // Note: This was not actually tested, and isn't used anywhere!
        [[nodiscard]] virtual Node* clone() const {
            Node* n = new Node();
            clone_impl(n);
            return n;
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
        Node* add_child_front(Node* n);
        Node* add_child_after(Node* n, const Node* prev);
        Node* pop_child();
        // Insert a node between this and its nth child
        Node* insert_between(size_t n, Node* node);

      protected:
        void clone_impl(Node* n) const {
            n->type = type;
            n->subtype = subtype;
            n->parent = nullptr;
            n->type_id = type_id;
            n->token = token;
            n->token.value = *internalize_string(std::string(token.value));

            for(const auto c : children)
                n->add_child(c->clone());
        }
    };

    struct TypeDeclaration : public Node {
        TypeDeclaration() : Node(Node::Type::TypeDeclaration) {}
        TypeDeclaration(Token t) : Node(Node::Type::TypeDeclaration, t) {}

        const auto& name() const { return token.value; }
        const auto& members() const { return children; }

        [[nodiscard]] virtual TypeDeclaration* clone() const override {
            auto n = new TypeDeclaration();
            clone_impl(n);
            return n;
        }
    };
    /*
    struct TemplatedTypeDeclaration : public TypeDeclaration {
        TemplatedTypeDeclaration(Token t) : TypeDeclaration(t) {}
    };
    */
    struct FunctionDeclaration : public Node {
        FunctionDeclaration() : Node(Node::Type::FunctionDeclaration){};
        FunctionDeclaration(Token t) : Node(Node::Type::FunctionDeclaration, t) {}

        enum Flag : uint8_t {
            None = 0,
            Exported = 1 << 0,
            Variadic = 1 << 1,
            Extern = 1 << 2, // Implemented in another module (no body) and disable name mangling.
            BuiltIn = 1 << 3,
            Imported = 1 << 4, // Like Extern, but with name mangling
        };

        Flag flags = Flag::None;

        [[nodiscard]] virtual FunctionDeclaration* clone() const override {
            auto n = new FunctionDeclaration();
            clone_impl(n);
            n->flags = flags;
            return n;
        }

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

        bool is_templated() const;
    };

    struct FunctionCall : public Node {
        FunctionCall() : Node(Node::Type::FunctionCall) {}
        FunctionCall(Token t) : Node(Node::Type::FunctionCall, t) {}

        FunctionDeclaration::Flag flags = FunctionDeclaration::None;

        auto       function() { return children[0]; }
        auto       arguments() { return std::span<AST::Node*>(children.data() + 1, std::max<int>(0, static_cast<int>(children.size()) - 1)); }
        const auto arguments() const { return std::span<const AST::Node* const>(children.data() + 1, std::max<int>(0, static_cast<int>(children.size()) - 1)); }

        std::vector<TypeID> get_argument_types() const {
            std::vector<TypeID> r;
            for(const auto& c : arguments())
                r.push_back(c->type_id);
            return r;
        }

        void set_argument(size_t idx, AST::Node* n) {
            if(n)
                assert(n->parent == nullptr);
            if(children[idx + 1])
                children[idx + 1]->parent = nullptr;
            children[idx + 1] = n;
            if(n)
                n->parent = this;
        }

        AST::Node* insert_before_argument(size_t idx, AST::Node* node) {
            assert(node->children.size() == 0);
            children[idx + 1]->parent = nullptr;
            node->add_child(children[idx + 1]);
            children[idx + 1] = node;
            return node;
        }

        std::string mangled_name() const;

        [[nodiscard]] virtual FunctionCall* clone() const override {
            auto n = new FunctionCall();
            clone_impl(n);
            n->flags = flags;
            return n;
        }
    };

    struct Defer : public Node {
        Defer() : Node(Node::Type::Defer) {}
        Defer(Token t) : Node(Node::Type::Defer, t) {}

        [[nodiscard]] virtual Defer* clone() const override {
            auto n = new Defer();
            clone_impl(n);
            return n;
        }
    };

    struct Scope : public Node {
        Scope() : Node(Node::Type::Scope) {}
        Scope(Token t) : Node(Node::Type::Scope, t) {}

        AST::Defer* defer = nullptr;

        [[nodiscard]] virtual Scope* clone() const override {
            auto n = new Scope();
            clone_impl(n);
            if(defer)
                n->defer = defer->clone();
            else
                n->defer = nullptr;
            return n;
        }

        ~Scope() override {
            if(defer)
                delete defer;
        }
    };

    struct VariableDeclaration : public Node {
        VariableDeclaration() : Node(Node::Type::VariableDeclaration) {}
        VariableDeclaration(Token t) : Node(Node::Type::VariableDeclaration, t) {}

        enum Flag : uint8_t {
            None = 0,
            Moved = 1 << 0,
        };

        std::string_view name;
        Flag             flags = Flag::None;

        [[nodiscard]] virtual VariableDeclaration* clone() const override {
            auto n = new VariableDeclaration();
            clone_impl(n);
            n->name = name;
            n->flags = flags;
            return n;
        }
    };

    struct Variable : public Node {
        Variable() : Node(Node::Type::Variable) {}
        Variable(Token t) : Node(Node::Type::Variable, t) {}

        std::string_view name;

        [[nodiscard]] virtual Variable* clone() const override {
            auto n = new Variable();
            clone_impl(n);
            n->name = name;
            return n;
        }
    };

    struct BinaryOperator : public Node {
        BinaryOperator() : Node(Node::Type::BinaryOperator) {}
        BinaryOperator(Token t) : Node(Node::Type::BinaryOperator, t) {}

        Token::Type operation() const { return token.type; }
        Node*       lhs() const { return children[0]; }
        Node*       rhs() const { return children[1]; }

        [[nodiscard]] virtual BinaryOperator* clone() const override {
            auto n = new BinaryOperator();
            clone_impl(n);
            return n;
        }
    };

    struct UnaryOperator : public Node {
        UnaryOperator() : Node(Node::Type::UnaryOperator) {}
        UnaryOperator(Token t) : Node(Node::Type::UnaryOperator, t) {}

        enum class Flag {
            None = 0,
            Prefix,
            Postfix,
        };

        Flag flags = Flag::None;

        Node* argument() const { return children[0]; }

        [[nodiscard]] virtual UnaryOperator* clone() const override {
            auto n = new UnaryOperator();
            clone_impl(n);
            n->flags = flags;
            return n;
        }
    };

    struct MemberIdentifier : public Node {
        MemberIdentifier() : Node(Node::Type::MemberIdentifier) {}
        MemberIdentifier(Token t) : Node(Node::Type::MemberIdentifier, t) {}

        uint32_t index = 0;

        std::string_view get_name() const { return token.value; }

        [[nodiscard]] virtual MemberIdentifier* clone() const override {
            auto n = new MemberIdentifier();
            clone_impl(n);
            n->index = index;
            return n;
        }
    };

    template<typename T>
    struct Literal : public Node {
        Literal() : Node(Type::ConstantValue) {}
        Literal(Token t) : Node(Type::ConstantValue, t) {}
        T value = T{};

        [[nodiscard]] virtual Literal<typename T>* clone() const override {
            auto n = new Literal<T>();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct BoolLiteral : public Literal<bool> {
        BoolLiteral() : Literal() { type_id = PrimitiveType::Boolean; }
        BoolLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Boolean; }

        [[nodiscard]] virtual BoolLiteral* clone() const override {
            auto n = new BoolLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct CharLiteral : public Literal<char> {
        CharLiteral() : Literal() { type_id = PrimitiveType::Char; }
        CharLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Char; }

        [[nodiscard]] virtual CharLiteral* clone() const override {
            auto n = new CharLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct IntegerLiteral : public Literal<int32_t> {
        IntegerLiteral() : Literal() { type_id = PrimitiveType::Integer; }
        IntegerLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Integer; }

        [[nodiscard]] virtual IntegerLiteral* clone() const override {
            auto n = new IntegerLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct FloatLiteral : public Literal<float> {
        FloatLiteral() : Literal() { type_id = PrimitiveType::Float; }
        FloatLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Float; }

        [[nodiscard]] virtual FloatLiteral* clone() const override {
            auto n = new FloatLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct StringLiteral : public Literal<std::string_view> {
        StringLiteral() : Literal() { type_id = PrimitiveType::CString; }
        StringLiteral(Token t) : Literal(t) { type_id = PrimitiveType::CString; }

        [[nodiscard]] virtual StringLiteral* clone() const override {
            auto n = new StringLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct Cast : public Node {
        Cast() = default;
        Cast(TypeID _type_id) : Node(Type::Cast) { type_id = _type_id; }

        [[nodiscard]] virtual Cast* clone() const override {
            auto n = new Cast();
            clone_impl(n);
            return n;
        }
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

inline AST::VariableDeclaration::Flag operator|(AST::VariableDeclaration::Flag lhs, AST::VariableDeclaration::Flag rhs) {
    return static_cast<AST::VariableDeclaration::Flag>(static_cast<std::underlying_type_t<AST::VariableDeclaration::Flag>>(lhs) |
                                                       static_cast<std::underlying_type_t<AST::VariableDeclaration::Flag>>(rhs));
}

inline AST::VariableDeclaration::Flag operator|=(AST::VariableDeclaration::Flag& lhs, AST::VariableDeclaration::Flag rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline AST::VariableDeclaration::Flag operator&(AST::VariableDeclaration::Flag lhs, AST::VariableDeclaration::Flag rhs) {
    return static_cast<AST::VariableDeclaration::Flag>(static_cast<std::underlying_type_t<AST::VariableDeclaration::Flag>>(lhs) &
                                                       static_cast<std::underlying_type_t<AST::VariableDeclaration::Flag>>(rhs));
}

inline AST::VariableDeclaration::Flag operator&=(AST::VariableDeclaration::Flag& lhs, AST::VariableDeclaration::Flag rhs) {
    lhs = lhs & rhs;
    return lhs;
}

inline AST::VariableDeclaration::Flag operator&=(AST::VariableDeclaration::Flag& lhs, int rhs) {
    lhs = lhs & static_cast<AST::VariableDeclaration::Flag>(rhs);
    return lhs;
}

#include <formatters/ASTFormat.hpp>

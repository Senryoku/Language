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
    struct Scope;

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
            TypeIdentifier,
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
        explicit Node(Token t) noexcept : token(t) {}
        explicit Node(Type type) noexcept : type(type) {}
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
        template<typename T>
        T* add_child(T* n) {
            add_child(static_cast<Node*>(n));
            return n;
        }
        Node* add_child_front(Node* n);
        Node* add_child_after(Node* n, const Node* prev);
        Node* add_child_before(Node* n, const Node* next);
        Node* pop_child();
        // Insert a node between this and its nth child
        Node* insert_between(size_t n, Node* node);

        [[nodiscard]] Scope*       get_scope();
        [[nodiscard]] const Scope* get_scope() const;
        [[nodiscard]] Scope*       get_root_scope();
        [[nodiscard]] const Scope* get_root_scope() const;

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
        explicit TypeDeclaration(Token t) : Node(Node::Type::TypeDeclaration, t) {}

        const auto& name() const { return token.value; }
        const auto& members() const { return children[0]->children; }

        [[nodiscard]] virtual TypeDeclaration* clone() const override {
            auto n = new TypeDeclaration();
            clone_impl(n);
            return n;
        }
    };

    struct FunctionDeclaration : public Node {
        explicit FunctionDeclaration(Token t) : Node(Node::Type::FunctionDeclaration, t) { add_child(new AST::Scope()); }

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

        auto   name() const { return token.value; }
        Scope* function_scope() {
            assert(!children.empty());
            return dynamic_cast<Scope*>(children[0]);
        }
        const Scope* function_scope() const {
            assert(!children.empty());
            return dynamic_cast<const Scope*>(children[0]);
        }
        AST::Node* body() {
            if(function_scope()->children.empty() ||
               function_scope()->children.back()->type != AST::Node::Type::Scope && function_scope()->children.back()->type != AST::Node::Type::Root)
                return nullptr;
            return function_scope()->children.back();
        }
        const AST::Node* body() const {
            // FIXME: This is really bad. There should be a consistent way to tell if a function is defined (i.e. has a body) or not.
            if(function_scope()->children.empty() ||
               function_scope()->children.back()->type != AST::Node::Type::Scope && function_scope()->children.back()->type != AST::Node::Type::Root)
                return nullptr;
            return function_scope()->children.back();
        }
        auto arguments() {
            if(function_scope()->children.size() == 0)
                return std::span<AST::Node*>();
            if(!body())
                return std::span<AST::Node*>(function_scope()->children);
            return std::span<AST::Node*>(function_scope()->children.data(), std::max<int>(0, static_cast<int>(function_scope()->children.size()) - 1));
        }
        const auto arguments() const {
            if(function_scope()->children.size() == 0)
                return std::span<const AST::Node* const>();
            if(!body())
                return std::span<const AST::Node* const>(function_scope()->children);
            return std::span<const AST::Node* const>(function_scope()->children.data(), std::max<int>(0, static_cast<int>(function_scope()->children.size()) - 1));
        }

        std::string mangled_name() const;

        bool is_templated() const;

      private:
        FunctionDeclaration() : Node(Node::Type::FunctionDeclaration){}; // Only used for cloning
    };

    struct FunctionCall : public Node {
        FunctionCall() : Node(Node::Type::FunctionCall) {}
        explicit FunctionCall(Token t) : Node(Node::Type::FunctionCall, t) {}

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
        explicit Defer(Token t) : Node(Node::Type::Defer, t) {}

        [[nodiscard]] virtual Defer* clone() const override {
            auto n = new Defer();
            clone_impl(n);
            return n;
        }
    };

    struct VariableDeclaration : public Node {
        VariableDeclaration() : Node(Node::Type::VariableDeclaration) {}
        explicit VariableDeclaration(Token t) : Node(Node::Type::VariableDeclaration, t) {}
        VariableDeclaration(Token token, TypeID _type_id) : Node(Node::Type::VariableDeclaration, token) { type_id = _type_id; }

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
        explicit Variable(Token t) : Node(Node::Type::Variable, t) {}
        explicit Variable(const VariableDeclaration* var_dec) : Node(Node::Type::Variable, var_dec->token) { type_id = var_dec->type_id; }

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
        explicit BinaryOperator(Token t) : Node(Node::Type::BinaryOperator, t) {}

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
        explicit UnaryOperator(Token t) : Node(Node::Type::UnaryOperator, t) {}

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
        explicit MemberIdentifier(Token t) : Node(Node::Type::MemberIdentifier, t) {}

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
        explicit Literal(Token t) : Node(Type::ConstantValue, t) {}
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
        explicit BoolLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Boolean; }

        [[nodiscard]] virtual BoolLiteral* clone() const override {
            auto n = new BoolLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct CharLiteral : public Literal<char> {
        CharLiteral() : Literal() { type_id = PrimitiveType::Char; }
        explicit CharLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Char; }

        [[nodiscard]] virtual CharLiteral* clone() const override {
            auto n = new CharLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct FloatLiteral : public Literal<float> {
        FloatLiteral() : Literal() { type_id = PrimitiveType::Float; }
        explicit FloatLiteral(Token t) : Literal(t) { type_id = PrimitiveType::Float; }

        [[nodiscard]] virtual FloatLiteral* clone() const override {
            auto n = new FloatLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct StringLiteral : public Literal<std::string_view> {
        StringLiteral() : Literal() { type_id = PrimitiveType::CString; }
        explicit StringLiteral(Token t) : Literal(t) { type_id = PrimitiveType::CString; }

        [[nodiscard]] virtual StringLiteral* clone() const override {
            auto n = new StringLiteral();
            clone_impl(n);
            n->value = value;
            return n;
        }
    };

    struct Cast : public Node {
        Cast() = default;
        explicit Cast(TypeID _type_id) : Node(Type::Cast) { type_id = _type_id; }

        [[nodiscard]] virtual Cast* clone() const override {
            auto n = new Cast();
            clone_impl(n);
            return n;
        }
    };

    struct LValueToRValue : public Node {
        LValueToRValue() = default;
        explicit LValueToRValue(AST::Node* child) : Node(Type::LValueToRValue) {
            add_child(child);
            type_id = child->type_id;
        }

        [[nodiscard]] virtual LValueToRValue* clone() const override {
            auto n = new LValueToRValue();
            clone_impl(n);
            return n;
        }
    };

    struct Scope : public Node {
      public:
        Scope() : Node(Node::Type::Scope) {}
        explicit Scope(Token t) : Node(Node::Type::Scope, t) {}

        bool declare_variable(VariableDeclaration& decNode);
        bool declare_function(FunctionDeclaration& node);
        bool declare_type(TypeDeclaration& node);
        bool declare_template_placeholder_type(const std::string& name);

        bool                                     is_valid(const het_unordered_map<std::vector<FunctionDeclaration*>>::iterator& it) const { return it != _functions.end(); }
        bool                                     is_valid(const het_unordered_map<std::vector<FunctionDeclaration*>>::const_iterator& it) const { return it != _functions.cend(); }
        [[nodiscard]] const FunctionDeclaration* resolve_function(const std::string_view& name, const std::span<TypeID>& arguments) const;
        [[nodiscard]] const FunctionDeclaration* resolve_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const;

        [[nodiscard]] const FunctionDeclaration*              get_function(const std::string_view& name, const std::span<TypeID>& arguments) const;
        [[nodiscard]] const FunctionDeclaration*              get_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const;
        [[nodiscard]] std::vector<const FunctionDeclaration*> get_functions(const std::string_view& name) const;

        [[nodiscard]] TypeID find_type(const std::string_view& name) const;
        [[nodiscard]] TypeID get_type(const std::string_view& name) const;
        bool                 is_type(const std::string_view& name) const;

        bool is_declared(const std::string_view& name) const { return _variables.find(name) != _variables.end(); }

        VariableDeclaration*       get_variable(const std::string_view& name);
        const VariableDeclaration* get_variable(const std::string_view& name) const;

        const het_unordered_map<VariableDeclaration*>& get_variables() const { return _variables; }

        inline het_unordered_map<VariableDeclaration*>::iterator       find(const std::string_view& name) { return _variables.find(name); }
        inline het_unordered_map<VariableDeclaration*>::const_iterator find(const std::string_view& name) const { return _variables.find(name); }
        bool is_valid(const het_unordered_map<VariableDeclaration*>::iterator& it) const { return it != _variables.end(); }
        bool is_valid(const het_unordered_map<VariableDeclaration*>::const_iterator& it) const { return it != _variables.end(); }

        void                       set_this(VariableDeclaration* var) { _this = var; }
        const VariableDeclaration* get_this() const;
        VariableDeclaration*       get_this();

        // Note: Returns a copy.
        std::stack<VariableDeclaration*> get_ordered_variable_declarations() const { return _ordered_variable_declarations; }

        [[nodiscard]] virtual Scope* clone() const override;

        AST::Scope*       get_parent_scope();
        const AST::Scope* get_parent_scope() const;

      private:
        // FIXME: At some point we'll have ton consolidate these string_view to their final home... Maybe the lexer should have done it already.
        het_unordered_map<VariableDeclaration*>              _variables;
        het_unordered_map<std::vector<FunctionDeclaration*>> _functions;
        het_unordered_map<TypeID>                            _types;
        std::vector<std::string>                             _template_placeholder_types; // Local names for placeholder types

        std::stack<VariableDeclaration*> _ordered_variable_declarations;

        VariableDeclaration* _this = nullptr;
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

#pragma once

#include <cassert>
#include <charconv>
#include <stack>
#include <vector>

#include <Tokenizer.hpp>

using TypeID = uint64_t;
constexpr TypeID InvalidTypeID = static_cast<TypeID>(-1);

enum class PrimitiveType {
    Integer,
    Float,
    Char,
    Boolean,
    Void,
    Composite,
    Undefined
};

inline std::string serialize(PrimitiveType type) {
    switch(type) {
        using enum PrimitiveType;
        case Integer: return "int";
        case Float: return "float";
        case Char: return "char";
        case Boolean: return "bool";
        case Void: return "void";
        default: assert(false);
    }
    return "";
}

inline PrimitiveType parse_primitive_type(const std::string_view& str) {
    using enum PrimitiveType;
    if(str == "int")
        return Integer;
    else if(str == "float")
        return Float;
    else if(str == "bool")
        return Boolean;
    else if(str == "char")
        return Char;
    return Undefined;
}

struct ValueType {
    bool is_primitive() const { return !is_array && primitive != PrimitiveType::Composite; }
    bool is_composite() const { return primitive == PrimitiveType::Composite; }
    bool is_undefined() const { return primitive == PrimitiveType::Undefined; }

    PrimitiveType primitive = PrimitiveType::Undefined;
    bool          is_array = false;
    size_t        capacity = 0;
    bool          is_reference = false;
    bool          is_pointer = false;

    TypeID type_id = InvalidTypeID;

    bool operator==(const ValueType&) const = default;

    ValueType get_element_type() const { 
        auto copy = *this;
        copy.is_array = false; // FIXME: This is obviously wrong.
        return copy;
    }

    static ValueType integer() { return ValueType{.primitive = PrimitiveType::Integer}; }
    static ValueType floating_point() { return ValueType{.primitive = PrimitiveType::Float}; }
    static ValueType character() { return ValueType{.primitive = PrimitiveType::Char}; }
    static ValueType boolean() { return ValueType{.primitive = PrimitiveType::Boolean}; }
    static ValueType void_t() { return ValueType{.primitive = PrimitiveType::Void}; }
    static ValueType undefined() { return ValueType{.primitive = PrimitiveType::Undefined}; }
};

inline std::string serialize(const ValueType& type) {
    if(type.is_primitive())
        return serialize(type.primitive);

    assert(false && "TODO: Handle Non-Primitive types.");
    // GlobalTypeRegistry::instance().get_type(type.type_id);
    return "";
}

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

        TypeID           type_id = InvalidTypeID;
        std::string_view name;

        const auto& members() const { return children; }
    };

    struct FunctionDeclaration : public Node {
        FunctionDeclaration(Token t) : Node(Node::Type::FunctionDeclaration, t) {}

        enum Flag : int {
            None = 0,
            Exported = 1 << 0,
            Variadic = 1 << 1,
        };

        std::string_view name;
        Flag             flags = Flag::None;

        const std::vector<AST::Node*>& arguments() const { return children; }
    };

    struct FunctionCall : public Node {
        FunctionCall(Token t) : Node(Node::Type::FunctionCall, t) {}

        FunctionDeclaration::Flag flags = FunctionDeclaration::None;

        auto&       arguments() { return children; }
        const auto& arguments() const { return children; }
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

        uint32_t         index = 0;
      
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
        StringLiteral(Token t) : Literal(t) {
            value_type.primitive = PrimitiveType::Char;
            value_type.is_array = true;
        }
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

class GlobalTypeRegistry {
  public:
    AST::TypeDeclaration* get_type(TypeID id) {
        assert(id != InvalidTypeID);
        return _types[id];
    }

    TypeID next_id() const { return _types.size(); }

    TypeID register_type(AST::TypeDeclaration& type_node) {
        auto id = next_id();
        _types.push_back(&type_node);
        type_node.type_id = id;
        return id;
    }

    inline static GlobalTypeRegistry& instance() {
        static GlobalTypeRegistry gtr;
        return gtr;
    }

  private:
    std::vector<AST::TypeDeclaration*> _types;
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
    auto format(const AST& t, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "AST Dump: {:}\n", t.getRoot());
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
    auto format(const AST::Node& t, FormatContext& ctx) const -> decltype(ctx.out()) {
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
            case AST::Node::Type::ConstantValue: r = fmt::format_to(ctx.out(), "{}:{}", t.type, t.value_type); break;
            case AST::Node::Type::ReturnStatement: r = fmt::format_to(ctx.out(), "{}:{}", t.type, t.value_type); break;
            case AST::Node::Type::WhileStatement: r = fmt::format_to(ctx.out(), "{}", t.type); break;
            case AST::Node::Type::Variable: r = fmt::format_to(ctx.out(), "{}:{}:{}", t.type, t.token.value, t.value_type); break;
            case AST::Node::Type::FunctionDeclaration: r = fmt::format_to(ctx.out(), "{}:{}", t.type, t.token.value); break;
            case AST::Node::Type::FunctionCall: r = fmt::format_to(ctx.out(), "{}:{}():{}", t.type, t.token.value, t.value_type); break;
            case AST::Node::Type::VariableDeclaration: r = fmt::format_to(ctx.out(), "{}:{} {}", t.type, t.value_type, t.token.value); break;
            case AST::Node::Type::Cast: r = fmt::format_to(ctx.out(), "{}:{}", t.type, t.value_type); break;
            case AST::Node::Type::BinaryOperator:
                r = fmt::format_to(ctx.out(), "{} {}:{}", fmt::format(fmt::emphasis::bold | fg(fmt::color::black) | bg(fmt::color::dim_gray), t.token.value), t.type, t.value_type);
                break;
            default: r = fmt::format_to(ctx.out(), "{}", t.type);
        }

        auto token_str = t.token.type == Token::Type::Unknown ? "None" : fmt::format("{}", t.token);
        // FIXME: This would be cool to use the actual token_str length here, but computing the printed length (stripping style/control characters) isn't trivial.
        // const auto length = 60;
        // Forward then backward
        // r = format_to(ctx.out(), "\033[999C\033[{}D{}\n", length, token_str);
        // Backward then forward
        r = fmt::format_to(ctx.out(), "\033[999D\033[{}C{}\n", 80, token_str);

        for(size_t i = 0; i < t.children.size(); ++i)
            r = fmt::format_to(r, fmt::runtime("{:" + indent + (i == t.children.size() - 1 ? "e" : "i") + "}"), *t.children[i]);

        return r;
    }
};

template<>
struct fmt::formatter<AST::Node::Type> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for AST::Node::Type");
        return it;
    }

    template<typename FormatContext>
    auto format(const AST::Node::Type& t, FormatContext& ctx) -> decltype(ctx.out()) {
        switch(t) {
            case AST::Node::Type::Root: return fmt::format_to(ctx.out(), "{}", "Root");
            case AST::Node::Type::Statement: return fmt::format_to(ctx.out(), "{}", "Statement");
            case AST::Node::Type::Expression: return fmt::format_to(ctx.out(), "{}", "Expression");
            case AST::Node::Type::IfStatement: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{}", "IfStatement");
            case AST::Node::Type::ElseStatement: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{}", "ElseStatement");
            case AST::Node::Type::WhileStatement: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{}", "WhileStatement");
            case AST::Node::Type::ForStatement: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{}", "ForStatement");
            case AST::Node::Type::ReturnStatement: return fmt::format_to(ctx.out(), fg(fmt::color::orchid), "{}", "ReturnStatement");
            case AST::Node::Type::Scope: return fmt::format_to(ctx.out(), "{}", "Scope {");
            case AST::Node::Type::VariableDeclaration: return fmt::format_to(ctx.out(), fg(fmt::color::light_blue), "{}", "VariableDeclaration");
            case AST::Node::Type::FunctionDeclaration: return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "{}", "FunctionDeclaration");
            case AST::Node::Type::FunctionCall: return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "{}", "FunctionCall");
            case AST::Node::Type::TypeDeclaration: return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "{}", "TypeDeclaration");
            case AST::Node::Type::MemberIdentifier: return fmt::format_to(ctx.out(), fg(fmt::color::light_yellow), "{}", "MemberIdentifier");
            case AST::Node::Type::Variable: return fmt::format_to(ctx.out(), fg(fmt::color::light_blue), "{}", "Variable");
            case AST::Node::Type::Cast: return fmt::format_to(ctx.out(), "{}", "Cast");
            case AST::Node::Type::LValueToRValue: return fmt::format_to(ctx.out(), "{}", "LValueToRValueCast");
            case AST::Node::Type::ConstantValue: return fmt::format_to(ctx.out(), "{}", "ConstantValue");
            case AST::Node::Type::UnaryOperator: return fmt::format_to(ctx.out(), "{}", "UnaryOperator");
            case AST::Node::Type::BinaryOperator: return fmt::format_to(ctx.out(), "{}", "BinaryOperator");
            default: assert(false); return fmt::format_to(ctx.out(), "{}", "MissingFormat for AST::Node::Type!");
        }
    }
};

template<>
struct fmt::formatter<PrimitiveType> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for ValueType::PrimitiveType");
        return it;
    }
    template<typename FormatContext>
    auto format(const PrimitiveType& t, FormatContext& ctx) -> decltype(ctx.out()) {
        switch(t) {
            using enum PrimitiveType;
            case Integer: return fmt::format_to(ctx.out(), fg(fmt::color::golden_rod), "{}", "int");
            case Float: return fmt::format_to(ctx.out(), fg(fmt::color::golden_rod), "{}", "float");
            case Char: return fmt::format_to(ctx.out(), fg(fmt::color::burly_wood), "{}", "char");
            case Boolean: return fmt::format_to(ctx.out(), fg(fmt::color::royal_blue), "{}", "bool");
            case Void: return fmt::format_to(ctx.out(), fg(fmt::color::gray), "{}", "void");
            case Undefined: return fmt::format_to(ctx.out(), fg(fmt::color::gray), "{}", "undefined");
            default: return fmt::format_to(ctx.out(), fg(fmt::color::red), "{}: {}", "Unknown ValueType PrimitiveType [by the formatter]", static_cast<int>(t));
        }
    }
};

template<>
struct fmt::formatter<ValueType> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for ValueType");
        return it;
    }

    template<typename FormatContext>
    auto format(const ValueType& t, FormatContext& ctx) -> decltype(ctx.out()) {
        if(t.is_primitive())
            if(t.is_array) {
                return fmt::format_to(ctx.out(), "{}[{}]", t.primitive, t.capacity);
            } else {
                return fmt::format_to(ctx.out(), "{}", t.primitive);
            }
        assert(false);
        return fmt::format_to(ctx.out(), "{}", "ValueType");
    }
};

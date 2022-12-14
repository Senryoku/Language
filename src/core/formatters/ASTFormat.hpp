#pragma once

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
        return fmt::format_to(ctx.out(), "AST Dump: {:}\n", t.get_root());
    }
};

template<>
struct fmt::formatter<TypeID> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for TypeID");
        return it;
    }
    template<typename FormatContext>
    auto format(TypeID type_id, FormatContext& ctx) const -> decltype(ctx.out()) {
        if(type_id == InvalidTypeID)
            return fmt::format_to(ctx.out(), "InvalidTypeID");
        auto type_name = GlobalTypeRegistry::instance().get_type(type_id).type->designation;
        return fmt::format_to(ctx.out(), "{}", type_name);
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

        auto type_name = t.type_id != InvalidTypeID ? GlobalTypeRegistry::instance().get_type(t.type_id).type->designation : "InvalidTypeID";

        switch(t.type) {
            case AST::Node::Type::ConstantValue: r = fmt::format_to(ctx.out(), "{}:{}", t.type, type_name); break;
            case AST::Node::Type::ReturnStatement: r = fmt::format_to(ctx.out(), "{}:{}", t.type, type_name); break;
            case AST::Node::Type::WhileStatement: r = fmt::format_to(ctx.out(), "{}", t.type); break;
            case AST::Node::Type::Variable: r = fmt::format_to(ctx.out(), "{} {}:{}", t.type, t.token.value, type_name); break;
            case AST::Node::Type::FunctionDeclaration: r = fmt::format_to(ctx.out(), "{} {}():", t.type, t.token.value, type_name); break;
            case AST::Node::Type::FunctionCall: r = fmt::format_to(ctx.out(), "{}:{}():{}", t.type, t.token.value, type_name); break;
            case AST::Node::Type::VariableDeclaration: r = fmt::format_to(ctx.out(), "{} {}:{}", t.type, t.token.value, type_name); break;
            case AST::Node::Type::Cast: r = fmt::format_to(ctx.out(), "{}:{}", t.type, type_name); break;
            case AST::Node::Type::BinaryOperator:
                r = fmt::format_to(ctx.out(), "{} {}:{}", fmt::format(fmt::emphasis::bold | fg(fmt::color::black) | bg(fmt::color::dim_gray), t.token.value), t.type, type_name);
                break;
            default: r = fmt::format_to(ctx.out(), "{}: {}", t.type, type_name);
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

        // Display the defer block of Scope nodes.
        // FIXME: Yes, this is horrible.
        if (t.type == AST::Node::Type::Scope && dynamic_cast<const AST::Scope*>(&t)->defer != nullptr) {
            fmt::format_to(r, fmt::runtime("{:" + indent + "e}"), *dynamic_cast<const AST::Node*>(dynamic_cast<const AST::Scope*>(&t)->defer));
        }

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
            case AST::Node::Type::Defer: return fmt::format_to(ctx.out(), "{}", "Defer");
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
            case AST::Node::Type::LValueToRValue: return fmt::format_to(ctx.out(), fg(fmt::color::dim_gray), "{}", "LValueToRValueCast");
            case AST::Node::Type::GetPointer: return fmt::format_to(ctx.out(), fg(fmt::color::dim_gray), "{}", "GetPointer");
            case AST::Node::Type::Dereference: return fmt::format_to(ctx.out(), fg(fmt::color::dim_gray), "{}", "Dereference");
            case AST::Node::Type::ConstantValue: return fmt::format_to(ctx.out(), "{}", "ConstantValue");
            case AST::Node::Type::UnaryOperator: return fmt::format_to(ctx.out(), "{}", "UnaryOperator");
            case AST::Node::Type::BinaryOperator: return fmt::format_to(ctx.out(), "{}", "BinaryOperator");
            default: assert(false); return fmt::format_to(ctx.out(), "{}", "MissingFormat for AST::Node::Type!");
        }
    }
};
/*
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
            case String: return fmt::format_to(ctx.out(), fg(fmt::color::burly_wood), "{}", "string");
            case Void: return fmt::format_to(ctx.out(), fg(fmt::color::gray), "{}", "void");
            case Undefined: return fmt::format_to(ctx.out(), fg(fmt::color::gray), "{}", "undefined");
            default: return fmt::format_to(ctx.out(), fg(fmt::color::red), "{}: {}", "Unknown ValueType PrimitiveType [by the formatter]", static_cast<int>(t));
        }
    }
};
*/
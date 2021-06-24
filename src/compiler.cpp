#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <FileWatch.hpp>
#include <fmt/chrono.h>

#include <Parser.hpp>
#include <Tokenizer.hpp>
#include <utils/CLIArg.hpp>

std::string to_wasm_type(GenericValue::Type type) {
    switch(type) {
        case GenericValue::Type::Integer: return "i32";
        case GenericValue::Type::Boolean: return "i32";
        // case GenericValue::Type::Float: return "f32";
        default: {
            error("[WASMCompiler] Unimplemented GenericType:{}\n", type);
            assert(false);
        }
    }
    return "[InvalidType]";
}

void generate_wasm_s_expression(const AST::Node& n) {
    switch(n.type) {
        using enum AST::Node::Type;
        case Root: {
            fmt::print("(module\n");
            fmt::print("(import \"console\" \"log\" (func $print(param i32)))\n");
            generate_wasm_s_expression(*n.children[0]);
            fmt::print(")\n");
            break;
        }
        case Scope: {
            for(auto c : n.children)
                generate_wasm_s_expression(*c);
            break;
        }
        case IfStatement: {
            fmt::print("(if \n");
            generate_wasm_s_expression(*n.children[0]);
            fmt::print(" (then \n");
            generate_wasm_s_expression(*n.children[1]);
            fmt::print(")\n)\n");
            break;
        }
        case WhileStatement: {
            fmt::print("(block\n");
            fmt::print("  (loop\n");
            // Branch out if the condition is NOT met
            fmt::print("    (br_if 1 (i32.eqz\n");
            generate_wasm_s_expression(*n.children[0]);
            fmt::print("    ))\n");
            generate_wasm_s_expression(*n.children[1]);
            fmt::print("    (br 0)\n");
            fmt::print("  )\n");
            fmt::print(")\n");
            break;
        }
        case ReturnStatement: {
            // TODO: Some static check for a single value on the stack?
            fmt::print("(return\n");
            for(auto c : n.children)
                generate_wasm_s_expression(*c);
            fmt::print(")\n");
            break;
        }
        case ConstantValue: {
            fmt::print("({}.const {})", to_wasm_type(n.value.type), n.value.value.as_int32_t); // FIXME: Switch on type.
            break;
        }
        case FunctionDeclaration: {
            fmt::print("(func ${}", n.token.value);
            for(size_t i = 0; i < n.children.size() - 1; ++i)
                fmt::print(" (param ${} {})", n.children[i]->token.value, to_wasm_type(n.children[i]->value.type));
            if(n.value.type != GenericValue::Type::Undefined)
                fmt::print(" (result {})", to_wasm_type(n.value.type));
            fmt::print("\n");
            generate_wasm_s_expression(*n.children.back());
            fmt::print(")\n(export \"{}\" (func ${}))\n", n.token.value, n.token.value);
            break;
        }
        case FunctionCall: {
            fmt::print("(call ${}\n", n.token.value);
            for(auto c : n.children)
                generate_wasm_s_expression(*c);
            fmt::print(")\n");
            break;
        }
        case BinaryOperator: {
            if(n.token.value.length() == 1) {
                switch(n.token.value[0]) {
                    case '=': {
                        // TODO: We assume lhs is just a variable here.
                        fmt::print("(local.set ${} ", n.children[0]->token.value);
                        generate_wasm_s_expression(*n.children[1]);
                        fmt::print(")\n");
                        break;
                    }
                    case '+': {
                        fmt::print("({}.add\n", to_wasm_type(n.value.type));
                        for(auto c : n.children)
                            generate_wasm_s_expression(*c);
                        fmt::print(")\n");
                        break;
                    }
                    case '-': {
                        fmt::print("({}.sub\n", to_wasm_type(n.value.type));
                        for(auto c : n.children)
                            generate_wasm_s_expression(*c);
                        fmt::print(")\n");
                        break;
                    }
                    case '*': {
                        fmt::print("({}.mul\n", to_wasm_type(n.value.type));
                        for(auto c : n.children)
                            generate_wasm_s_expression(*c);
                        fmt::print(")\n");
                        break;
                    }
                    case '/': {
                        fmt::print("({}.div_s\n", to_wasm_type(n.value.type));
                        for(auto c : n.children)
                            generate_wasm_s_expression(*c);
                        fmt::print(")\n");
                        break;
                    }
                    case '<': {
                        fmt::print("({}.lt_s\n", to_wasm_type(n.value.type));
                        for(auto c : n.children)
                            generate_wasm_s_expression(*c);
                        fmt::print(")\n");
                        break;
                    }
                    default: {
                        error("[WASMCompiler] Unimplemented BinaryOperator {}.\n", n.token);
                        return;
                    }
                }
            } else if(n.token.value == "==") {
                fmt::print("({}.eq\n", to_wasm_type(n.value.type));
                for(auto c : n.children)
                    generate_wasm_s_expression(*c);
                fmt::print(")\n");
                break;
            } else {
                error("[WASMCompiler] Unimplemented BinaryOperator {}.\n", n.token);
                return;
            }
            break;
        }
        case VariableDeclaration: {
            fmt::print("(local ${} {})\n", n.token.value, to_wasm_type(n.value.type));
            break;
        }
        case Variable: {
            fmt::print("(local.get ${})\n", n.token.value);
            break;
        }
        default: {
            error("[WASMCompiler] Node type {} unimplemented.\n", n.type);
            return;
        }
    }
}

void generate_wasm_s_expression(const AST& ast) {
    generate_wasm_s_expression(ast.getRoot());
}

int main(int argc, char* argv[]) {
    fmt::print("╭{0:─^{2}}╮\n"
               "│{1: ^{2}}│\n"
               "╰{0:─^{2}}╯\n",
               "", "<insert language name> compiler.", 80);
    CLIArg args;
    args.add('a', "ast", false, "Dump the parsed AST to the command line.");
    args.add('t', "tokens", false, "Dump the state after the tokenizing stage.");
    args.add('w', "watch", false, "Watch the supplied file and re-run on changes.");
    args.parse(argc, argv);

    if(args.get_default_arg() == "") {
        error("No source file provided.\n");
        print("Usage: 'compiler path/to/source.lang'.\n");
        return -1;
    }

    auto handle_file = [&]() {
        std ::ifstream file(args.get_default_arg());
        if(!file) {
            error("Couldn't open file '{}' (Running from {}).\n", args.get_default_arg(), std::filesystem::current_path().string());
            return -1;
        }
        std::string source{(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};

        std::vector<Tokenizer::Token> tokens;
        Tokenizer                     tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());

        // Print tokens
        if(args['t'].set) {
            int i = 0;
            for(const auto& t : tokens) {
                fmt::print("  {}", t);
                if(++i % 6 == 0)
                    fmt::print("\n");
            }
            fmt::print("\n");
        }

        Parser parser;
        auto   ast = parser.parse(tokens);
        if(ast.has_value()) {
            if(args['a'].set)
                fmt::print("{}", *ast);
            generate_wasm_s_expression(*ast);
        }
        return 0;
    };

    if(args['w'].set) {
        handle_file();
        filewatch::FileWatch<std::string> watcher{args.get_default_arg(), [&](const std::string& path, const filewatch::Event) {
                                                      puts("\033[2J"); // Clear screen
                                                      fmt::print("[{:%T}] <insert lang name> compiler: {} changed, reprocessing...\n", std::chrono::system_clock::now(), path);
                                                      handle_file();
                                                      success("[{:%T}] Watching for changes on {}... ", std::chrono::system_clock::now(), args.get_default_arg());
                                                      fmt::print("(CTRL+C to exit)\n\n");
                                                  }};
        success("[{:%T}] Watching for changes on {}... ", std::chrono::system_clock::now(), args.get_default_arg());
        fmt::print("(CTRL+C to exit)\n");
        while(true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        return handle_file();
    }
}

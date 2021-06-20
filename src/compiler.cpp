#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <Parser.hpp>
#include <Tokenizer.hpp>
#include <utils/CLIArg.hpp>

int main(int argc, char* argv[]) {
    fmt::print("<insert language name> compiler.\n");
    CLIArg args;
    args.add('a', "ast", false, "Dump the parsed AST to the command line.");
    args.parse(argc, argv);

    if(args.get_default_arg() == "") {
        error("No source file provided.\n");
        print("Usage: 'compiler path/to/source.lang'.\n");
        return -1;
    }

    std::ifstream file(args.get_default_arg());
    if(!file) {
        error("Couldn't open file '{}' (Running from {}).\n", args.get_default_arg(), std::filesystem::current_path().string());
        return -1;
    }
    std::string source{(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};

    std::vector<Tokenizer::Token> tokens;
    Tokenizer                     tokenizer(source);
    while(tokenizer.has_more())
        tokens.push_back(tokenizer.consume());

    Parser parser;
    auto   ast = parser.parse(tokens);
    if(ast.has_value()) {
        if(args['a'].set)
            fmt::print("{}", *ast);
    }
}

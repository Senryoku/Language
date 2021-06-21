#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "../ext/FileWatch.hpp"
#include <fmt/chrono.h>

#include <Parser.hpp>
#include <Tokenizer.hpp>
#include <utils/CLIArg.hpp>

int main(int argc, char* argv[]) {
    fmt::print("<insert language name> compiler.\n");
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

        if(args['t'].set) {
            int i = 0;
            for(const auto& t : tokens) {
                fmt::print("\t{}", t);
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

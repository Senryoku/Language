#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

#include <Interpreter.hpp>
#include <Logger.hpp>
#include <Parser.hpp>
#include <Tokenizer.hpp>

int main() {
    fmt::print(R"(
# Welcome to <insert Langage name> prompt. Enter 'q' to quit, 'help' for more commands.
)");
    Indenter log;

    std::vector<std::string>      lines;
    std::vector<Tokenizer::Token> tokens;
    AST                           ast;
    Parser                        parser;
    Interpreter                   interpreter;
    std::string                   input;
    lines.reserve(64 * 1024); // FIXME: Tokens are referencing these strings, this a workaround to avoid reallocation of the vector, which would be fatal :)
    do {
        std::cout << " > ";
        getline(std::cin, input);
        if(std::cin.fail() || std::cin.eof()) {
            std::cin.clear();
            return 0;
        }
        lines.push_back(input);
        auto& line = lines.back();

        if(line == "q") {
            break;
        } else if(line == "help") {
            fmt::print(R"(Available commands:
    q        Exits the program.
    optimize Run optimize on the current AST.
    dump     Dump the current AST.
    clear    Resets everything (AST and Interpreter states included).
    rerun    Reinitialize the interpreter and re-execute the current AST.
    help     Displays this help.
)");
        } else if(line == "optimize") {
            ast.optimize();
        } else if(line == "dump") {
            fmt::print("{}", ast);
        } else if(line == "clear") {
            lines.clear();
            tokens.clear();
            ast = {};
            parser = {};
            interpreter = {};
        } else if(line == "rerun") {
            log.print("Reseting interpreter and re-running AST...\n");
            interpreter = {};
            auto clock = std::chrono::steady_clock();
            auto start = clock.now();
            interpreter.execute(ast.getRoot());
            auto end = clock.now();
            log.print("Done in {}ms, returned: '{}'.\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), interpreter.get_return_value());
        } else {
            log.group();
            log.print("Parsing '{}'...\n", line);

            Tokenizer tokenizer(line);
            auto      first = tokens.size();
            while(tokenizer.has_more())
                tokens.push_back(tokenizer.consume());

            auto newNodes = parser.parse(std::span<Tokenizer::Token>{tokens.begin() + first, tokens.end()}, ast);
            for(auto node : newNodes) {
                log.group();
                log.print("Executing ({}) using Interpreter...\n", node->type);
                auto clock = std::chrono::steady_clock();
                auto start = clock.now();
                interpreter.execute(*node);
                auto end = clock.now();
                log.print("Done in {}ms, returned: '{}'.\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), interpreter.get_return_value());
                log.end();
            }
            log.end();
        }
    } while(true);
}

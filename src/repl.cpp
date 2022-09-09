#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <Interpreter.hpp>
#include <Logger.hpp>
#include <Parser.hpp>
#include <Tokenizer.hpp>
#include <utils/CLIArg.hpp>

#ifdef WIN32
#include <tchar.h>
#include <windows.h>
#endif

#include "Prompt.hpp"

int main(int argc, char* argv[]) {
#ifdef WIN32
    SetConsoleTitle(_T("Lang REPL"));
#endif
    fmt::print(R"(
# Welcome to {} REPL. Enter 'q' to quit, 'help' for more commands.
)",
               link("http://lang.com", "<insert language name>"));

    CLIArg args;
    args.parse(argc, argv);

    Indenter log;

    std::vector<std::string>      lines;
    std::vector<Tokenizer::Token> tokens;
    AST                           ast;
    Parser                        parser;
    Interpreter                   interpreter;
    std::string                   input;

    Prompt prompt;

    bool debug = false;

    auto load = [&](const auto& path) {
        std::ifstream file(path);
        if(!file) {
            error("[repl::load] Couldn't open file '{}' (Running from {}).\n", path, std::filesystem::current_path().string());
            return;
        }
        lines.push_back(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
        auto& source = lines.back();
        log.group();
        log.print("Parsing '{}'...\n", path);
        Tokenizer tokenizer(source);
        auto      first = tokens.size();
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());

        auto newNode = parser.parse(std::span<Tokenizer::Token>{tokens.begin() + first, tokens.end()}, ast);
        ast.optimize();
        if(newNode) {
            log.group();
            log.print("Executing ({}) using Interpreter...\n", newNode->type);
            auto clock = std::chrono::steady_clock();
            auto start = clock.now();
            interpreter.execute(*newNode);
            auto end = clock.now();
            log.print("Done in {}ms, returned: '{}'.\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), interpreter.get_return_value());
            log.end();
        }
        log.end();
    };

    if(args.get_default_arg() != "") {
        load(args.get_default_arg());
    }

    lines.reserve(64 * 1024); // FIXME: Tokens are referencing these strings, this is a workaround to avoid reallocation of the vector, which would be fatal :)
    do {
        input = prompt.get_line();

        if(input == "q") {
            break;
        } else if(input == "help") {
            fmt::print(R"(Available commands:
    q           Exits the program.
    load [path] Loads, parse and interprets the file at specified path.
    optimize    Run optimize on the current AST.
    dump        Dump the current AST.
    clear       Resets everything (AST and Interpreter states included).
    rerun       Reinitialize the interpreter and re-execute the current AST.
    help        Displays this help.
)");
        } else if(input.starts_with("load ")) {
            load(input.substr(5));
        } else if(input == "optimize") {
            ast.optimize();
        } else if(input == "dump") {
            fmt::print("{}", ast);
        } else if(input == "clear") {
            lines.clear();
            tokens.clear();
            ast = {};
            parser = {};
            interpreter = {};
        } else if(input == "rerun") {
            log.print("Reseting interpreter and re-running AST...\n");
            interpreter = {};
            auto clock = std::chrono::steady_clock();
            auto start = clock.now();
            interpreter.execute(ast.getRoot());
            auto end = clock.now();
            log.print("Done in {}ms, returned: '{}'.\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), interpreter.get_return_value());
        } else if(input == "debug") {
            debug = !debug;
        } else {
            lines.push_back(input);
            auto& line = lines.back();

            log.group();
            Tokenizer tokenizer(line);
            auto      first = tokens.size();
            try {
                while(tokenizer.has_more()) {
                    auto t = tokenizer.consume();
                    if(debug)
                        print("{}\n", t);
                    tokens.push_back(t);
                }

                auto newNode = parser.parse(std::span<Tokenizer::Token>{tokens.begin() + first, tokens.end()}, ast);
                if(newNode) {
                    log.group();
                    log.print("Executing ({}) using Interpreter...\n", newNode->type);
                    auto clock = std::chrono::steady_clock();
                    auto start = clock.now();
                    interpreter.execute(*newNode);
                    auto end = clock.now();
                    log.print("Done in {}ms, returned: '{}'.\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), interpreter.get_return_value());
                    log.end();
                }
            } catch(const Exception& e) { e.display(); }
            log.end();
        }
    } while(true);
}

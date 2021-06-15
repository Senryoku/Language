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

int main(int argc, char* argv[]) {
    if(argc <= 1) {
        error("No test file provided. Usage: tester file.test");
        return -1;
    }

    std::ifstream file(argv[1]);
    if(!file) {
        error("Could not load {}", argv[1]);
        return -1;
    }

    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::vector<Tokenizer::Token> tokens;

    Tokenizer tokenizer(source);
    while(tokenizer.has_more())
        tokens.push_back(tokenizer.consume());

    Parser parser;
    auto   ast = parser.parse(tokens);
    if(ast.has_value()) {
        auto        clock = std::chrono::steady_clock();
        auto        start = clock.now();
        Interpreter interpreter;
        interpreter.execute(*ast);
        auto end = clock.now();
    }

    return 0;
}

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

#include <Interpreter.hpp>
#include <Parser.hpp>
#include <Tokenizer.hpp>

int main() {
    fmt::print(R"(Welcome to <insert Langage name> prompt.
  One day there will be an interpreter here, but right now you'll only get an AST dump :)
  Enter 'q' to quit.
)");

    std::string line;
    do {
        std::cout << " > ";
        getline(std::cin, line);

        if(line == "q") {
            break;
        } else {
            fmt::print("Parsing '{}'...\n", line);
            std::vector<Tokenizer::Token> tokens;

            Tokenizer tokenizer(line);
            while(tokenizer.has_more())
                tokens.push_back(tokenizer.consume());

            Parser parser;
            auto   ast = parser.parse(tokens);
            if(ast.has_value()) {
                fmt::print("Executing using Interpreter...\n");
                auto        clock = std::chrono::steady_clock();
                auto        start = clock.now();
                Interpreter interpreter;
                interpreter.execute(*ast);
                auto end = clock.now();
                fmt::print("Done in {}ms, returned: '{}'.\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), interpreter.get_return_value());
            }
        }
    } while(true);
}

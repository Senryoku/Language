#include <array>
#include <iostream>
#include <string>
#include <string_view>

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
            parser.parse(tokens);
        }
    } while(true);
}

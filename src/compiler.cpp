#include <array>
#include <string>
#include <string_view>

#include <Parser.hpp>
#include <Tokenizer.hpp>

int main() {
    fmt::print("This is a compiler. I swear.\n");

    const std::string source{R"(
	    int a = 1 + 2;
        if(a) {
            int b = 0;
        }
	)"};

    std::vector<Tokenizer::Token> tokens;

    Tokenizer tokenizer(source);
    while(tokenizer.has_more()) {
        const auto token = tokenizer.consume();
        tokens.push_back(token);
        fmt::print("Found token: '{}'\n", token);
    }

    Parser parser;
    parser.parse(tokens);
}

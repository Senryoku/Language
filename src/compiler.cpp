#include <array>
#include <string>
#include <string_view>

#include <Parser.hpp>
#include <Tokenizer.hpp>

int main() {
    fmt::print("This is a compiler. I swear.\n");

    const bool all_tests = false;

    if(false || all_tests) {
        const std::string source{R"(
	    int a = 1 + 2;
	)"};

        std::vector<Tokenizer::Token> tokens;

        Tokenizer tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());

        Parser parser;
        parser.parse(tokens);
    }

    if(false || all_tests) {
        const std::string source{R"(
        int b = 2 * 2 + 3 + 8 * 6;
	)"};

        std::vector<Tokenizer::Token> tokens;

        Tokenizer tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());

        Parser parser;
        parser.parse(tokens);
    }

    if(false || all_tests) {
        const std::string source{R"(
	    int a = 0;
        if(a) {
            int b = 0;
        }
	)"};

        std::vector<Tokenizer::Token> tokens;

        Tokenizer tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());

        Parser parser;
        parser.parse(tokens);
    }

    // Already declared
    if(true || all_tests) {
        const std::string source{R"(
	    int a = 0;
        a = 5;
        int a = 6;
	)"};

        std::vector<Tokenizer::Token> tokens;

        Tokenizer tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());

        Parser parser;
        parser.parse(tokens);
    }
}

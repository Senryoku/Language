#include <array>
#include <string>
#include <string_view>

#include <Parser.hpp>
#include <Tokenizer.hpp>

int main() {
    fmt::print("This is a compiler. I swear.\n");

    const bool all_tests = true;

    struct Test {
        bool        run = false;
        std::string source;
    };

    std::vector<Test> tests{
        {false, R"(int a = 1 + 2;)"},
        {false, R"(int b = 2 * 2 + 3 + 8 * 6;)"},
        {false, R"(
	    int a = 0;
        if(a) {
            int b = 0;
        }
        )"},
    };

    for(const auto& t : tests) {
        if(t.run || all_tests) {
            std::vector<Tokenizer::Token> tokens;

            Tokenizer tokenizer(t.source);
            while(tokenizer.has_more())
                tokens.push_back(tokenizer.consume());

            Parser parser;
            parser.parse(tokens);
        }
    }
}

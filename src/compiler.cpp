#include <array>
#include <string>
#include <string_view>

#include <Parser.hpp>
#include <Tokenizer.hpp>

int main() {
    fmt::print("This is a compiler. I swear.\n");

    const bool all_tests = false;

    struct Test {
        bool        run = false;
        std::string source;
    };

    std::vector<Test> tests{
        {true, R"(int i = 5;
        while(i > 0) {
            i = i - 1;
        })"},
        {true, R"(function multiply(int rhs, int lhs) {
            return rhs * lhs;
        })"},
    };

    for(const auto& t : tests) {
        if(t.run || all_tests) {
            std::vector<Tokenizer::Token> tokens;

            Tokenizer tokenizer(t.source);
            while(tokenizer.has_more())
                tokens.push_back(tokenizer.consume());

            Parser parser;
            auto   ast = parser.parse(tokens);
            if(ast.has_value()) {
                fmt::print("{}", *ast);
            }
        }
    }
}

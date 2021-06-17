#include <gtest/gtest.h>

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

const std::string PathPrefix = "H:/Source/Lang/test/"; // FIXME: This is a workaround for VS not honoring the WORKING_DIRECTORY setup in CMakeLists.txt

void tokenize(const char* filepath, std::vector<Tokenizer::Token>& tokens, std::string& source) {
    std::ifstream file(PathPrefix + filepath);
    ASSERT_TRUE(file) << "Couldn't open test file '" << filepath << "' (Run from " << std::filesystem::current_path() << ")";
    source = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Tokenizer tokenizer(source);
    while(tokenizer.has_more())
        tokens.push_back(tokenizer.consume());

    EXPECT_GT(tokens.size(), 0);
}

TEST(Arithmetic, Basic) {
    std::string                   source;
    std::vector<Tokenizer::Token> tokens;
    tokenize("basic/arithmetic.lang", tokens, source);

    Parser parser;
    auto   ast = parser.parse(tokens);
    ASSERT_TRUE(ast.has_value());

    Interpreter interpreter;
    interpreter.execute(*ast);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 69); // Actually check :^)
}

TEST(Basic, While) {
    std::string                   source;
    std::vector<Tokenizer::Token> tokens;
    tokenize("basic/while.lang", tokens, source);

    Parser parser;
    auto   ast = parser.parse(tokens);
    ASSERT_TRUE(ast.has_value());
    Interpreter interpreter;
    interpreter.execute(*ast);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 10);
}

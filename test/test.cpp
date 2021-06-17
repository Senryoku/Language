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

#define PARSE_INTERP(code)                           \
    std::string                   source{code};      \
    std::vector<Tokenizer::Token> tokens;            \
    Tokenizer                     tokenizer(source); \
    while(tokenizer.has_more())                      \
        tokens.push_back(tokenizer.consume());       \
    EXPECT_GT(tokens.size(), 0);                     \
    Parser parser;                                   \
    auto   ast = parser.parse(tokens);               \
    ASSERT_TRUE(ast.has_value());                    \
    Interpreter interpreter;                         \
    interpreter.execute(*ast);

#define LOAD_PARSE_INTERP(path)           \
    std::string                   source; \
    std::vector<Tokenizer::Token> tokens; \
    tokenize(path, tokens, source);       \
    Parser parser;                        \
    auto   ast = parser.parse(tokens);    \
    ASSERT_TRUE(ast.has_value());         \
    Interpreter interpreter;              \
    interpreter.execute(*ast);

TEST(Basic, Add) {
    PARSE_INTERP("return 25 + 97;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 122);
}

TEST(Basic, Sub) {
    PARSE_INTERP("return 57 - 26;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 31);
}

TEST(Basic, Expression) {
    PARSE_INTERP("return 125 * 45 + 24 / (4 + 3) - 5;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 5623);
}

TEST(Basic, Arithmetic) {
    LOAD_PARSE_INTERP("basic/arithmetic.lang");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 69); // Actually check :^)
}

TEST(Basic, While) {
    LOAD_PARSE_INTERP("basic/while.lang");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 10);
}

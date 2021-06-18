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

void tokenize(const char* filepath, std::vector<Tokenizer::Token>& tokens, std::string& source) {
    std::ifstream file(filepath);
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

TEST(Arithmetic, Add) {
    PARSE_INTERP("25 + 97;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 122);
}

TEST(Arithmetic, AddNoSpaces) {
    PARSE_INTERP("25+97;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 122);
}

TEST(Arithmetic, Sub) {
    PARSE_INTERP("57 - 26");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 31);
}

TEST(Arithmetic, Mul) {
    PARSE_INTERP("25 * 5");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 125);
}

TEST(Arithmetic, Div) {
    PARSE_INTERP("125 / 5");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 25);
}

TEST(Arithmetic, Expression) {
    PARSE_INTERP("125 * 45 + 24 / (4 + 3) - 5");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 5623);
}

TEST(Arithmetic, Order) {
    PARSE_INTERP("2 * (6 * 1 + 2) / 4 * (4 + 1)");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 20);
}

TEST(Arithmetic, OrderWithoutSpaces) {
    PARSE_INTERP("2*(6*1+2)/4*(4+1)");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 20);
}

TEST(Arithmetic, Assignment) {
    LOAD_PARSE_INTERP("basic/arithmetic.lang");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 69);
}

TEST(Keywords, Return) {
    PARSE_INTERP("return 458;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 458);
}

TEST(Keywords, ReturnWithExpression) {
    PARSE_INTERP("return  125 * 45 + 24 / (4 + 3) - 5;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 5623);
}

TEST(Keywords, While) {
    LOAD_PARSE_INTERP("basic/while.lang");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 10);
}

TEST(Function, Declaration) {
    LOAD_PARSE_INTERP("function/declaration.lang");
}

TEST(Function, DeclarationAndCall) {
    LOAD_PARSE_INTERP("function/call.lang");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 18 * 48);
}

bool is_prime(int number) {
    if(number < 2)
        return false;
    if(number == 2)
        return true;
    if(number % 2 == 0)
        return false;
    for(int i = 3; (i * i) <= number; i += 2)
        if(number % i == 0)
            return false;
    return true;
}

TEST(Function, isPrime) {
    LOAD_PARSE_INTERP("function/prime.lang");
    ast->optimize();
    for(uint32_t i = 2; i < 200; ++i) {
        auto      s = fmt::format("isPrime({});", i);
        Tokenizer next_tokens(s);
        auto      first = tokens.size();
        while(next_tokens.has_more())
            tokens.push_back(next_tokens.consume());
        EXPECT_GT(tokens.size(), first);
        auto newNodes = parser.parse(std::span<Tokenizer::Token>{tokens.begin() + first, tokens.end()}, *ast);
        ASSERT_TRUE(newNodes.size() > 0);
        for(auto node : newNodes) {
            interpreter.execute(*node);
        }
        interpreter.execute(*ast);
        EXPECT_EQ(interpreter.get_return_value().value.as_bool, is_prime(i));
    }
}
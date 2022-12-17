#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <Logger.hpp>
#include <Tokenizer.hpp>

#define TOKENIZE(code)                         \
    std::string        source{code};           \
    std::vector<Token> tokens;                 \
    Tokenizer          tokenizer(source);      \
    while(tokenizer.has_more())                \
        tokens.push_back(tokenizer.consume()); \
    EXPECT_GT(tokens.size(), 0);

TEST(Tokenizer, Add) {
    TOKENIZE("25 + 97;");
    EXPECT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, Token::Type::Digits);
    EXPECT_EQ(tokens[1].type, Token::Type::Addition);
    EXPECT_EQ(tokens[2].type, Token::Type::Digits);
    EXPECT_EQ(tokens[3].type, Token::Type::EndStatement);
}

TEST(Tokenizer, Assignment) {
    TOKENIZE("int i = 0;");
    EXPECT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type, Token::Type::Identifier);
    EXPECT_EQ(tokens[1].type, Token::Type::Identifier);
    EXPECT_EQ(tokens[2].type, Token::Type::Assignment);
    EXPECT_EQ(tokens[3].type, Token::Type::Digits);
    EXPECT_EQ(tokens[4].type, Token::Type::EndStatement);
}

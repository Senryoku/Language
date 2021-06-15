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

TEST(Arithmetic, Basic) {
    const auto    filepath = "basic/arithmetic.lang";
    std::ifstream file(PathPrefix + filepath);
    ASSERT_TRUE(file) << "Couldn't open test file '" << filepath << "' (Run from " << std::filesystem::current_path() << ")";
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::vector<Tokenizer::Token> tokens;
    Tokenizer                     tokenizer(source);
    while(tokenizer.has_more())
        tokens.push_back(tokenizer.consume());

    EXPECT_GT(tokens.size(), 0);

    Parser parser;
    auto   ast = parser.parse(tokens);
    EXPECT_TRUE(ast.has_value());
    if(ast.has_value()) {
        auto        clock = std::chrono::steady_clock();
        auto        start = clock.now();
        Interpreter interpreter;
        interpreter.execute(*ast);
        EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 29); // Actually check :^)
        auto end = clock.now();
    }
}

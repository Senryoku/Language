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

void tokenize(const char* filepath, std::vector<Token>& tokens, std::string& source) {
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
    std::vector<Token> tokens;            \
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
    std::vector<Token> tokens; \
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

TEST(Arithmetic, RightMul) {
    PARSE_INTERP("2 + (2) * 4");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 10);
}

TEST(Arithmetic, RightMul2) {
    PARSE_INTERP("4 * 5 + (4 + 5) * 7");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 83);
}

TEST(Arithmetic, Modulo) {
    PARSE_INTERP("5 % 2");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 5 % 2);
}

TEST(Arithmetic, ModuloPrecedence) {
    PARSE_INTERP("(46 + 20) % 12");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, (46 + 20) % 12);
}

TEST(Arithmetic, FloatModulo) {
    PARSE_INTERP("25.6 % 1.8");
    EXPECT_EQ(interpreter.get_return_value().value.as_float, std::fmod(25.6f, 1.8f));
}

TEST(Arithmetic, UnarySub) {
    PARSE_INTERP("-5684;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, -5684);
}

TEST(Arithmetic, UnaryAdd) {
    PARSE_INTERP("+4185;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, +4185);
}

TEST(Arithmetic, Increment) {
    PARSE_INTERP("int i = 0; ++i;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1);
}

TEST(Arithmetic, PostFixIncrement) {
    PARSE_INTERP("int i = 0; i++; return i;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1);
}

TEST(Arithmetic, PostFixIncrementReturnValue) {
    PARSE_INTERP("int i = 0; i++;");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 0);
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

TEST(Arithmetic, Float) {
    LOAD_PARSE_INTERP("basic/float.lang");
    EXPECT_EQ(static_cast<int>(interpreter.get_return_value().value.as_float), 11313); // We shouldn't compare floats directly.
}

TEST(Array, Declaration) {
    PARSE_INTERP("int[8] arr");
}

TEST(Array, Assignment) {
    PARSE_INTERP("int[8] arr; arr[2] = 1337; return arr[2];");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1337);
}

TEST(Array, ExternalSize) {
    PARSE_INTERP(R"(
const int size = 8;
int[size] arr;
)");
}

TEST(Array, VariableAccess) {
    PARSE_INTERP(R"(
const int size = 8;
int[size] arr;
int total = 0;
for(int i = 0; i < size; ++i) {
    arr[i] = i;
}
for(int i = 0; i < size; ++i) {
    total = total + arr[i];
}
return total;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7);
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

TEST(Keywords, For) {
    PARSE_INTERP(R"(
int ret = 0;
for(int i = 0; i < 8; ++i) {
    ret = ret + i;
}
return ret;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7);
}

TEST(Keywords, MultipleFor) {
    PARSE_INTERP(R"(
int ret = 0;
for(int i = 0; i < 8; ++i) {
    ret = ret + i;
}
for(int i = 0; i < 8; ++i) {
    ret = ret + i;
}
return ret;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 2 * (0 + 1 + 2 + 3 + 4 + 5 + 6 + 7));
}

TEST(Keywords, VariableDeclarationInWhile) {
    LOAD_PARSE_INTERP("basic/declare_in_while.lang");
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 5);
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
    std::vector<std::string> lines;
    for(uint32_t i = 2; i < 1000; ++i) {
        lines.push_back(fmt::format("isPrime({});", i));
        auto&     line = lines.back();
        Tokenizer next_tokens(line);
        auto      first = tokens.size();
        while(next_tokens.has_more())
            tokens.push_back(next_tokens.consume());
        EXPECT_GT(tokens.size(), first);
        auto newNode = parser.parse(std::span<Token>{tokens.begin() + first, tokens.end()}, *ast);
        ASSERT_TRUE(newNode);
        interpreter.execute(*newNode);
        EXPECT_EQ(interpreter.get_return_value().value.as_bool, is_prime(i));
    }
}

int fib(int n) {
    if(n == 0)
        return 0;
    if(n == 1)
        return 1;
    return fib(n - 1) + fib(n - 2);
}

TEST(Function, RecursionFibonacci) {
    LOAD_PARSE_INTERP("function/fib.lang");
    ast->optimize();
    std::vector<std::string> lines;
    for(uint32_t i = 0; i < 20; ++i) {
        lines.push_back(fmt::format("fib({});", i));
        auto&     line = lines.back();
        Tokenizer next_tokens(line);
        auto      first = tokens.size();
        while(next_tokens.has_more())
            tokens.push_back(next_tokens.consume());
        EXPECT_GT(tokens.size(), first);
        auto newNode = parser.parse(std::span<Token>{tokens.begin() + first, tokens.end()}, *ast);
        ASSERT_TRUE(newNode);
        interpreter.execute(*newNode);
        EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, fib(i));
    }
}

TEST(Function, ArrayFibonacci) {
    LOAD_PARSE_INTERP("function/fib_array.lang");
    ast->optimize();
    std::vector<std::string> lines;
    for(uint32_t i = 0; i < 32; ++i) {
        lines.push_back(fmt::format("fib({});", i));
        auto&     line = lines.back();
        Tokenizer next_tokens(line);
        auto      first = tokens.size();
        while(next_tokens.has_more())
            tokens.push_back(next_tokens.consume());
        EXPECT_GT(tokens.size(), first);
        auto newNode = parser.parse(std::span<Token>{tokens.begin() + first, tokens.end()}, *ast);
        ASSERT_TRUE(newNode);
        interpreter.execute(*newNode);
        EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, fib(i));
    }
}

TEST(Other, Mandelbrot) {
    LOAD_PARSE_INTERP("other/mandelbrot.lang");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Boolean);
    EXPECT_EQ(interpreter.get_return_value().value.as_bool, true);
}

TEST(Other, MandelbrotFor) {
    LOAD_PARSE_INTERP("other/mandelbrot_for.lang");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Boolean);
    EXPECT_EQ(interpreter.get_return_value().value.as_bool, true);
}

TEST(Type, BasicDeclaration) {
    PARSE_INTERP(R"(
    type type1 {
        int i;
    }
)");
}

TEST(Type, DefaultValue) {
    PARSE_INTERP(R"(
    type type1 {
        int i = 1337;
    }
)");
}

TEST(Type, MemberAccess) {
    PARSE_INTERP(R"(
    type user_type {
        int i = 1337;
    }
    user_type var;
    return var.i;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1337);
}

TEST(Type, MemberMutation) {
    PARSE_INTERP(R"(
    type user_type {
        int i = 1337;
    }
    user_type var;
    var.i = 1234;
    return var.i;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1234);
}

TEST(Type, TwoSimpleTypes) {
    PARSE_INTERP(R"(
    type type1 {
        int i;
    }
    type type2 {
        int j;
    }
    type1 var1;
    type2 var2;
)");
}

TEST(Type, MemberAccessAndMutation) {
    PARSE_INTERP(R"(
    type complex {
        float i = 0;
        float j = 0;
    }

    complex z;
    z.i = 2.55;
    z.j = 2.0 * z.i;
    return z.j;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Float);
    EXPECT_FLOAT_EQ(interpreter.get_return_value().value.as_float, 2.0f * 2.55f);
}

TEST(Type, TwoSimpleTypesMemberAccess) {
    PARSE_INTERP(R"(
    type type1 {
        int i;
    }
    type type2 {
        int j;
    }
    type1 var1;
    type2 var2;
    var1.i = 6;
    var2.j = 8;
    var2.j = var2.j * var1.i;
    return var2.j;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 6 * 8);
}

TEST(Type, Assignment) {
    PARSE_INTERP(R"(
    type NewType {
        int i = 0;
    }
    NewType var1;
    NewType var2;
    var2.i = 1337;
    var1 = var2;
    return var1.i;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1337);
}

TEST(Type, Nested) {
    PARSE_INTERP(R"(
    type type1 {
        int i = 1337;
    }
    type type2 {
        type1 j;
    }
    type2 var;
)");
}

TEST(Type, NestedAccess) {
    PARSE_INTERP(R"(
    type type1 {
        int i = 1337;
    }
    type type2 {
        type1 j;
    }
    type2 var;
    return var.j.i;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1337);
}

TEST(Type, NestedAssignment) {
    PARSE_INTERP(R"(
    type type1 {
        int i = 1337;
    }
    type type2 {
        type1 j;
    }
    type2 var;
    var.j.i = 1234;
    return var.j.i;
)");
    EXPECT_EQ(interpreter.get_return_value().type, GenericValue::Type::Integer);
    EXPECT_EQ(interpreter.get_return_value().value.as_int32_t, 1234);
}

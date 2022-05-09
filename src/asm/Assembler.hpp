#pragma once

#include <array>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Logger.hpp>

/* x86 Assembler. Intel Syntax, I guess.
 */
class Assembler {
  public:
    struct Operand {
        Operand() = default;
        Operand(const std::string_view& sv) {}
    };

    struct InstructionBytes {
        uint8_t size = 4;
        uint8_t bytes[4];
    };

    using InstructionStream = std::vector<uint8_t>;
    /*
    struct InstructionStream {
        size_t   size = 0;
        size_t   capacity = 0;
        uint8_t* bytes = nullptr;
    };
    */

    inline static const std::unordered_map<std::string_view, std::function<void(const std::array<Operand, 2>&)>> Mnemonics{
        {"mov", [](const std::array<Operand, 2>&) { fmt::print("mov"); }}};

    InstructionBytes parse_instruction(const std::string_view& line) {
        const auto first_separator = line.find_first_of(' ');
        const auto mnemonic = std::string_view(line.data(), first_separator);
        const auto operands_str = std::string_view{line.data() + first_separator + 1, line.size() - first_separator};
        const auto second_separator = operands_str.find_first_of(", ");

        std::array<Operand, 2> operands;
        if(second_separator != std::string::npos) {
            // operands[0] = Operand{std::string_view{operands_str.begin(), second_separator}};
            // operands[1] = Operand{std::string_view{second_separator, operands_str.end()}};
        } else {
            operands[0] = Operand{operands_str};
        }

        Mnemonics.at(mnemonic)(operands);

        return InstructionBytes{};
    }

    static std::string_view get_first_operand(const std::string_view& line) {}
    static std::string_view get_second_operand(const std::string_view& line) {}

  private:
};
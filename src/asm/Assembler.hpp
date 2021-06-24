#pragma once

#include <string_view>
#include <vector>

/* x86 Assembler. Intel Syntax, I guess.
 */
class Assembler {
  public:
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

    InstructionBytes parse_instruction(const std::string_view& line) {
        const auto first_separator = line.find_first_of(' ');
        const auto mnemonic = std::string_view{line.begin(), first_separator};
        const auto operands = std::string_view{first_separator, line.end()};
        const auto second_separator = operands.find_first_of(", ");

        line.substr()
    }

    static std::string_view get_first_operand(const std::string_view& line) {}
    static std::string_view get_second_operand(const std::string_view& line) {}

  private:
};
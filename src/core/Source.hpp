#pragma once

#include <limits>
#include <string>

#include <Token.hpp>

std::string point_error(const std::string& source, const Token& token) noexcept;
std::string point_error(const std::string& source, size_t at, size_t line, size_t from = (std::numeric_limits<size_t>::max)(),
                        size_t to = (std::numeric_limits<size_t>::max)()) noexcept;

#pragma once

#include <string>
#include <limits>

std::string point_error(const std::string& source, size_t at, size_t line, size_t from = (std::numeric_limits<size_t>::max)(),
                        size_t to = (std::numeric_limits<size_t>::max)()) noexcept;

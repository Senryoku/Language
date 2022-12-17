#pragma once

#include <cassert>
#include <string_view>
#include <variant>

class Error {
  public:
    Error(const std::string_view& sv) : _str(sv) {}

    std::string_view string() const { return _str; }

  private:
    std::string_view _str;
};

template<typename T>
class ErrorOr {
  public:
    ErrorOr(const T& value) : _value(value) {}
    ErrorOr(const Error& value) : _value(value) {}

    bool is_error() const { return std::holds_alternative<Error>(_value); }

    const Error& get_error() const {
        assert(is_error());
        return std::get<Error>(_value);
    }

    const T& get() const {
        assert(!is_error());
        return std::get<T>(_value);
    }

  private:
    std::variant<Error, T> _value;
};

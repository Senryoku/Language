#pragma once

#include <stdexcept>

#include <Logger.hpp>

class Exception : public std::logic_error {
  public:
    Exception(const std::string& what, const std::string& hint = "") : std::logic_error(what.c_str()), _hint(hint) {}

    const std::string& hint() const { return _hint; }

    void display() const {
        error("{}", what());
        if(!hint().empty()) {
            print("\n");
            info("{}", hint());
        }
    }

  private:
    std::string _hint;
};
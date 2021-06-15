#pragma once

#include <stack>

#include <VariableStore.hpp>

class Scope {
  public:
    bool declare_variable(GenericValue::Type type, const std::string_view& name, size_t line = 0) {
        if(is_declared(name)) {
            error("Error on line {}: Variable '{}' already declared.\n", line, name);
            return false;
        }
        switch(type) {
            case GenericValue::Type::Integer: _variables[std::string{name}] = Variable{Variable::Type::Integer}; break;
            default: error("Error on line {}: Unimplemented type '{}'.\n", line, type);
        }
        return true;
    }

    bool is_declared(const std::string_view& name) const { return _variables.find(name) != _variables.end(); }

    inline Variable&       operator[](const std::string_view& name) { return _variables.find(name)->second; }
    inline const Variable& operator[](const std::string_view& name) const { return _variables.find(name)->second; }
    inline Variable&       get(const std::string_view& name) { return _variables.find(name)->second; }
    inline const Variable& get(const std::string_view& name) const { return _variables.find(name)->second; }

    const VariableStore& get_variables() const { return _variables; }

  private:
    // FIXME: At some point we'll have ton consolidate these string_view to their final home... Maybe the lexer should have done it already.
    VariableStore _variables;
};

class Scoped {
  protected:
    Scope&       get_scope() { return _scopes.top(); }
    const Scope& get_scope() const { return _scopes.top(); }

    Scope& push_scope() {
        _scopes.push(Scope{});
        return get_scope();
    }

    void pop_scope() { _scopes.pop(); }

  private:
    std::stack<Scope> _scopes;
};
#pragma once

#include <vector>

#include <VariableStore.hpp>

class Scope {
  public:
    bool declare_variable(GenericValue::Type type, const std::string_view& name, size_t line = 0) {
        if(is_declared(name)) {
            error("[Scope] Error on line {}: Variable '{}' already declared.\n", line, name);
            return false;
        }
        switch(type) {
            case GenericValue::Type::Integer: _variables[std::string{name}] = Variable{Variable::Type::Integer}; break;
            case GenericValue::Type::Bool: _variables[std::string{name}] = Variable{Variable::Type::Bool}; break;
            default: error("[Scope] Error on line {}: Unimplemented type '{}'.\n", line, type);
        }
        return true;
    }

    bool is_declared(const std::string_view& name) const { return _variables.find(name) != _variables.end(); }

    inline Variable&       operator[](const std::string_view& name) { return _variables.find(name)->second; }
    inline const Variable& operator[](const std::string_view& name) const { return _variables.find(name)->second; }
    inline Variable&       get(const std::string_view& name) { return _variables.find(name)->second; }
    inline const Variable& get(const std::string_view& name) const { return _variables.find(name)->second; }

    const VariableStore& get_variables() const { return _variables; }

    inline VariableStore::iterator       find(const std::string_view& name) { return _variables.find(name); }
    inline VariableStore::const_iterator find(const std::string_view& name) const { return _variables.find(name); }
    bool                                 is_valid(const VariableStore::iterator& it) const { return it != _variables.end(); }
    bool                                 is_valid(const VariableStore::const_iterator& it) const { return it != _variables.end(); }

  private:
    // FIXME: At some point we'll have ton consolidate these string_view to their final home... Maybe the lexer should have done it already.
    VariableStore _variables;
};

// TODO: Fetch variables from others scopes
class Scoped {
  protected:
    Scope&       get_scope() { return _scopes.back(); }
    const Scope& get_scope() const { return _scopes.back(); }

    Scope& push_scope() {
        _scopes.push_back(Scope{});
        return get_scope();
    }

    void pop_scope() { _scopes.pop_back(); }

    /*
    inline std::optional<Variable&>       operator[](const std::string_view& name) { return get(name); }
    inline std::optional<const Variable&> operator[](const std::string_view& name) const { return get(name); }
    */
    inline Variable* get(const std::string_view& name) {
        auto it = _scopes.rbegin();
        auto val = it->find(name);
        while(it != _scopes.rend() && !it->is_valid(val)) {
            it++;
            val = it->find(name);
        }
        return it->is_valid(val) ? &val->second : nullptr;
    }
    inline const Variable* get(const std::string_view& name) const {
        auto it = _scopes.rbegin();
        auto val = it->find(name);
        while(it != _scopes.rend() && !it->is_valid(val)) {
            it++;
            if(it != _scopes.rend())
                val = it->find(name);
        }
        return it->is_valid(val) ? &val->second : nullptr;
    }

    bool is_declared(const std::string_view& name) const { return get(name) != nullptr; }

  private:
    std::vector<Scope> _scopes;
};
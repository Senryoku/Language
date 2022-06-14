#pragma once

#include <vector>

#include <VariableStore.hpp>
#include <het_unordered_map.hpp>

using TypeRegistry = std::unordered_map<TypeID, const AST::Node*>;

class Scope {
  public:
    Scope(TypeRegistry& type_registry) : _type_registry(&type_registry) {}

    struct FunctionDeclaration {
        const AST::Node* node;
    };

    struct TypeDeclaration {
        TypeID           id = 0;
        const AST::Node* node = nullptr;
    };

    bool declare_function(const AST::Node& node) {
        auto sv = node.token.value;
        if(is_valid(find_function(sv))) {
            error("[Scope] Function {} already declared in this scope.\n", sv);
            return false;
        }
        // TODO: Check & warn shadowing from other scopes?
        _functions[std::string{sv}] = {.node = &node};
        return true;
    }

    bool declare_type(const AST::Node& node) {
        auto sv = node.token.value;
        if(is_valid(find_type(sv))) {
            error("[Scope] Type {} already declared in this scope.\n", sv);
            return false;
        }
        TypeID next_id = _type_registry->size();
        (*_type_registry)[next_id] = &node;
        _types.emplace(std::string{sv}, TypeDeclaration{next_id, &node});
        return true;
    }

    bool                                                   is_valid(const het_unordered_map<FunctionDeclaration>::iterator& it) const { return it != _functions.end(); }
    bool                                                   is_valid(const het_unordered_map<FunctionDeclaration>::const_iterator& it) const { return it != _functions.cend(); }
    het_unordered_map<FunctionDeclaration>::iterator       find_function(const std::string_view& name) { return _functions.find(name); }
    het_unordered_map<FunctionDeclaration>::const_iterator find_function(const std::string_view& name) const { return _functions.find(name); }
    const AST::Node*                                       get_function(const std::string_view& name) { return _functions.find(name)->second.node; }
    const AST::Node*                                       get_function(const std::string_view& name) const { return _functions.find(name)->second.node; }

    bool                                               is_valid(const het_unordered_map<TypeDeclaration>::iterator& it) const { return it != _types.end(); }
    bool                                               is_valid(const het_unordered_map<TypeDeclaration>::const_iterator& it) const { return it != _types.cend(); }
    het_unordered_map<TypeDeclaration>::iterator       find_type(const std::string_view& name) { return _types.find(name); }
    het_unordered_map<TypeDeclaration>::const_iterator find_type(const std::string_view& name) const { return _types.find(name); }
    TypeDeclaration                                    get_type(const std::string_view& name) { return _types.find(name)->second; }
    TypeDeclaration                                    get_type(const std::string_view& name) const { return _types.find(name)->second; }

    bool declare_variable(const AST::Node& decNode, size_t line = 0) {
        const auto& name = decNode.token.value;
        if(is_declared(name)) {
            error("[Scope] Error on line {}: Variable '{}' already declared.\n", line, name);
            return false;
        }
        switch(decNode.value.type) {
            case GenericValue::Type::Boolean: [[fallthrough]];
            case GenericValue::Type::Integer: [[fallthrough]];
            case GenericValue::Type::Float: [[fallthrough]];
            case GenericValue::Type::Char: [[fallthrough]];
            case GenericValue::Type::String:
                _variables[std::string{name}] =
                    Variable{{decNode.value.type, decNode.subtype == AST::Node::SubType::Const ? GenericValue::Flags::Const : GenericValue::Flags::None}};
                break;
            case GenericValue::Type::Array: {
                Variable v{{Variable::Type::Array}};
                v.value.as_array.type = decNode.value.value.as_array.type;
                v.value.as_array.capacity = decNode.value.value.as_array.capacity;
                v.value.as_array.items = nullptr;
                _variables[std::string{name}] = v;
                break;
            };
            case GenericValue::Type::Composite: {
                Variable v{{Variable::Type::Composite}};
                v.value.as_array.type = Variable::Type::Composite;
                v.value.as_array.capacity = static_cast<uint32_t>(decNode.children.size());
                v.value.as_array.items = nullptr;
                _variables[std::string{name}] = v;
                break;
            }
            default: error("[Scope] Error on line {}: Unimplemented type '{}'.\n", line, decNode.value.type); return false;
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
    TypeRegistry* _type_registry; // Shared by the whole tree

    // FIXME: At some point we'll have ton consolidate these string_view to their final home... Maybe the lexer should have done it already.
    VariableStore                          _variables;
    het_unordered_map<FunctionDeclaration> _functions;
    het_unordered_map<TypeDeclaration>     _types;
};

// TODO: Fetch variables from others scopes
class Scoped {
  protected:
    Scope&       get_scope() { return _scopes.back(); }
    const Scope& get_scope() const { return _scopes.back(); }

    Scope& push_scope() {
        _scopes.push_back(Scope{_type_registry});
        return get_scope();
    }

    void pop_scope() { _scopes.pop_back(); }

    const AST::Node* get_function(const std::string_view& name) const {
        auto it = _scopes.rbegin();
        auto val = it->find_function(name);
        while(it != _scopes.rend() && !it->is_valid(val)) {
            it++;
            if(it != _scopes.rend())
                val = it->find_function(name);
        }
        return it != _scopes.rend() && it->is_valid(val) ? val->second.node : nullptr;
    }

    Scope::TypeDeclaration get_type(const std::string_view& name) const {
        auto it = _scopes.rbegin();
        auto val = it->find_type(name);
        while(it != _scopes.rend() && !it->is_valid(val)) {
            it++;
            if(it != _scopes.rend())
                val = it->find_type(name);
        }
        assert(it != _scopes.rend() && it->is_valid(val));
        return val->second;
    }

    const AST::Node* get_type(TypeID id) const { return _type_registry.at(id); }

    /*
    inline std::optional<Variable&>       operator[](const std::string_view& name) { return get(name); }
    inline std::optional<const Variable&> operator[](const std::string_view& name) const { return get(name); }
    */
    Variable* get(const std::string_view& name) {
        auto it = _scopes.rbegin();
        auto val = it->find(name);
        while(it != _scopes.rend() && !it->is_valid(val)) {
            it++;
            if(it != _scopes.rend())
                val = it->find(name);
        }
        return it != _scopes.rend() && it->is_valid(val) ? &val->second : nullptr;
    }
    const Variable* get(const std::string_view& name) const {
        auto it = _scopes.rbegin();
        auto val = it->find(name);
        while(it != _scopes.rend() && !it->is_valid(val)) {
            it++;
            if(it != _scopes.rend())
                val = it->find(name);
        }
        return it != _scopes.rend() && it->is_valid(val) ? &val->second : nullptr;
    }

    bool is_declared(const std::string_view& name) const { return get(name) != nullptr; }

    bool is_type(const std::string_view& name) const {
        auto builtin = GenericValue::parse_type(name);
        if(builtin != GenericValue::Type::Undefined)
            return true;
        auto it = _scopes.rbegin();
        auto val = it->find_type(name);
        while(it != _scopes.rend() && !it->is_valid(val)) {
            it++;
            if(it != _scopes.rend())
                val = it->find_type(name);
        }
        return it != _scopes.rend() && it->is_valid(val);
    }

  private:
    std::vector<Scope> _scopes;
    TypeRegistry       _type_registry;
};

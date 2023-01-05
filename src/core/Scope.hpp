#pragma once

#include <memory>
#include <stack>
#include <vector>

#include <het_unordered_map.hpp>

#include <AST.hpp>
#include <FlyString.hpp>
#include <GlobalTemplateCache.hpp>
#include <GlobalTypeRegistry.hpp>

class Scope {
  public:
    bool declare_function(AST::FunctionDeclaration& node) {
        auto sv = node.token.value;
        if(resolve_function(sv, node.arguments()) != nullptr)
            return false;
        // TODO: Check & warn shadowing from other scopes?
        _functions[std::string{sv}].push_back(&node);
        return true;
    }

    bool declare_type(AST::TypeDeclaration& node) {
        auto sv = node.token.value;
        if(find_type(sv) != InvalidTypeID)
            return false;
        auto type_id = GlobalTypeRegistry::instance().register_type(node);
        node.type_id = type_id;
        _types.emplace(std::string{sv}, type_id);
        return true;
    }

    bool declare_template_placeholder_type(const std::string& name) {
        _template_placeholder_types.push_back(name);
        return true;
    }

    bool is_valid(const het_unordered_map<std::vector<AST::FunctionDeclaration*>>::iterator& it) const { return it != _functions.end(); }
    bool is_valid(const het_unordered_map<std::vector<AST::FunctionDeclaration*>>::const_iterator& it) const { return it != _functions.cend(); }
    [[nodiscard]] const AST::FunctionDeclaration*                      resolve_function(const std::string_view& name, const std::span<TypeID>& arguments) const;
    [[nodiscard]] const AST::FunctionDeclaration*                      resolve_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const;
    inline [[nodiscard]] const std::vector<AST::FunctionDeclaration*>& get_functions(const std::string_view& name) const { return _functions.find(name)->second; };
    inline [[nodiscard]] bool                                          has_functions(const std::string_view& name) const { return is_valid(_functions.find(name)); };

    [[nodiscard]] TypeID find_type(const std::string_view& name) const {
        auto it = _types.find(name);
        if(it != _types.end())
            return it->second;
        auto template_it = std::find(_template_placeholder_types.begin(), _template_placeholder_types.end(), name);
        if(template_it != _template_placeholder_types.end())
            return PlaceholderTypeID_Min + std::distance(_template_placeholder_types.begin(), template_it);
        return InvalidTypeID;
    }

    bool declare_variable(AST::VariableDeclaration& decNode) {
        const auto& name = decNode.token.value;
        if(is_declared(name))
            return false;
        _variables.emplace(name, &decNode);
        _ordered_variable_declarations.push(&decNode);
        return true;
    }

    bool is_declared(const std::string_view& name) const { return _variables.find(name) != _variables.end(); }

    inline AST::VariableDeclaration&       operator[](const std::string_view& name) { return *_variables.find(name)->second; }
    inline const AST::VariableDeclaration& operator[](const std::string_view& name) const { return *_variables.find(name)->second; }
    inline AST::VariableDeclaration&       get(const std::string_view& name) { return *_variables.find(name)->second; }
    inline const AST::VariableDeclaration& get(const std::string_view& name) const { return *_variables.find(name)->second; }

    const het_unordered_map<AST::VariableDeclaration*>& get_variables() const { return _variables; }

    inline het_unordered_map<AST::VariableDeclaration*>::iterator       find(const std::string_view& name) { return _variables.find(name); }
    inline het_unordered_map<AST::VariableDeclaration*>::const_iterator find(const std::string_view& name) const { return _variables.find(name); }
    bool is_valid(const het_unordered_map<AST::VariableDeclaration*>::iterator& it) const { return it != _variables.end(); }
    bool is_valid(const het_unordered_map<AST::VariableDeclaration*>::const_iterator& it) const { return it != _variables.end(); }

    void                            set_this(AST::VariableDeclaration* var) { _this = var; }
    const AST::VariableDeclaration* get_this() const { return _this; }
    AST::VariableDeclaration*       get_this() { return _this; }

    // Note: Returns a copy.
    std::stack<AST::VariableDeclaration*> get_ordered_variable_declarations() const { return _ordered_variable_declarations; }

  private:
    // FIXME: At some point we'll have ton consolidate these string_view to their final home... Maybe the lexer should have done it already.
    het_unordered_map<AST::VariableDeclaration*>              _variables;
    het_unordered_map<std::vector<AST::FunctionDeclaration*>> _functions;
    het_unordered_map<TypeID>                                 _types;
    std::vector<std::string>                                  _template_placeholder_types; // Local names for placeholder types

    std::stack<AST::VariableDeclaration*> _ordered_variable_declarations;

    AST::VariableDeclaration* _this = nullptr;
};

// TODO: Fetch variables from others scopes
class Scoped {
  protected:
    Scoped() {
        push_scope(); // TODO: Push an empty scope for now.

        const auto register_builtin = [&](const std::string& name, TypeID type = PrimitiveType::Void, std::vector<std::string> args_names = {}, std::vector<TypeID> args_types = {},
                                          AST::FunctionDeclaration::Flag flags = AST::FunctionDeclaration::Flag::None) {
            if(!s_builtins[name]) {
                Token token;
                token.value = *internalize_string(name); // We have to provide a name via the token.
                s_builtins[name].reset(new AST::FunctionDeclaration(token));
                s_builtins[name]->type_id = type;
                s_builtins[name]->flags = flags | AST::FunctionDeclaration::Flag::BuiltIn;

                for(size_t i = 0; i < args_names.size(); ++i) {
                    Token arg_token;
                    arg_token.value = *internalize_string(args_names[i]);
                    auto arg = s_builtins[name]->add_child(new AST::VariableDeclaration(arg_token));
                    arg->type_id = args_types[i];
                }
            }
            get_scope().declare_function(*s_builtins[name]);
            return s_builtins[name].get();
        };

        register_builtin("put", PrimitiveType::Integer, {"character"}, {PrimitiveType::Char});
        register_builtin("printf", PrimitiveType::Integer, {}, {}, AST::FunctionDeclaration::Flag::Variadic);
    }

    const AST::VariableDeclaration* get_this() const {
        auto it = _scopes.rbegin();
        while(it != _scopes.rend()) {
            if(it->get_this())
                return it->get_this();
            it++;
        }
        return nullptr;
    }
    AST::VariableDeclaration* get_this() {
        auto it = _scopes.rbegin();
        while(it != _scopes.rend()) {
            if(it->get_this())
                return it->get_this();
            it++;
        }
        return nullptr;
    }

    Scope&       get_root_scope() { return _scopes.front(); }
    Scope&       get_scope() { return _scopes.back(); }
    const Scope& get_scope() const { return _scopes.back(); }

    Scope& push_scope() {
        _scopes.emplace_back();
        return get_scope();
    }

    void pop_scope() { _scopes.pop_back(); }

    [[nodiscard]] const AST::FunctionDeclaration* get_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const;
    [[nodiscard]] const AST::FunctionDeclaration* get_function(const std::string_view& name, const std::span<TypeID>& arguments) const;
    // Debug helper, gets all functions with the given name.
    std::vector<const AST::FunctionDeclaration*> get_functions(const std::string_view& name) const;

    TypeID get_type(const std::string_view& name) const;
    bool   is_type(const std::string_view& name) const;

    AST::VariableDeclaration*       get(const std::string_view& name);
    const AST::VariableDeclaration* get(const std::string_view& name) const;
    bool                            is_declared(const std::string_view& name) const { return get(name) != nullptr; }

  private:
    std::vector<Scope> _scopes;

    // FIXME: Declare put as a builtin function, should be handled elsewhere.
    inline static std::unordered_map<std::string, std::unique_ptr<AST::FunctionDeclaration>> s_builtins;
};

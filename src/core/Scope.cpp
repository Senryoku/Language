#include "Scope.hpp"

[[nodiscard]] const AST::FunctionDeclaration* Scope::resolve_function(const std::string_view& name, const std::span<TypeID>& arguments) const {
    auto candidate_functions = _functions.find(name);
    if(candidate_functions == _functions.end())
        return nullptr;

    for(const auto function : candidate_functions->second) {
        // TODO: Correctly handle vargs functions
        if(!(function->flags & AST::FunctionDeclaration::Flag::Variadic)) {
            // TODO: Handle default values
            if(arguments.size() != function->arguments().size())
                continue;
            bool args_types_match = true;
            for(auto idx = 0; idx < arguments.size(); ++idx) {

                if(arguments[idx] != function->arguments()[idx]->type_id &&
                   // Allow casting to the generic 'pointer' type
                   (function->arguments()[idx]->type_id != PrimitiveType::Pointer || !GlobalTypeRegistry::instance().get_type(arguments[idx])->is_pointer())) {
                    args_types_match = false;
                    break;
                }
            }
            if(!args_types_match)
                continue;
        }
        // We found a match.
        return function;
    }
    return nullptr;
}

[[nodiscard]] const AST::FunctionDeclaration* Scope::resolve_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const {
    std::vector<TypeID> argument_types;
    for(auto c : arguments)
        argument_types.push_back(c->type_id);
    return resolve_function(name, argument_types);
}

[[nodiscard]] const AST::FunctionDeclaration* Scoped::get_function(const std::string_view& name, const std::span<TypeID>& arguments) const {
    auto it = _scopes.rbegin();
    while(it != _scopes.rend()) {
        auto ret = it->resolve_function(name, arguments);
        if(ret)
            return ret;
        it++;
    }
    return nullptr;
}

[[nodiscard]] const AST::FunctionDeclaration* Scoped::get_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const {
    std::vector<TypeID> argument_types;
    for(auto c : arguments)
        argument_types.push_back(c->type_id);
    return get_function(name, argument_types);
}

std::vector<const AST::FunctionDeclaration*> Scoped::get_functions(const std::string_view& name) const {
    auto                                         it = _scopes.rbegin();
    std::vector<const AST::FunctionDeclaration*> r;
    while(it != _scopes.rend()) {
        if(it->has_functions(name)) {
            const auto& candidates = it->get_functions(name);
            r.insert(r.end(), candidates.begin(), candidates.end());
        }
        it++;
    }
    return r;
}

TypeID Scoped::get_type(const std::string_view& name) const {
    auto it = _scopes.rbegin();
    auto type_id = it->find_type(name);
    while(it != _scopes.rend() && type_id == InvalidTypeID) {
        it++;
        if(it != _scopes.rend())
            type_id = it->find_type(name);
    }
    // Search built-ins
    if(type_id == InvalidTypeID)
        return GlobalTypeRegistry::instance().get_type(std::string{name})->type_id;
    return type_id;
}

AST::VariableDeclaration* Scoped::get(const std::string_view& name) {
    auto it = _scopes.rbegin();
    auto val = it->find(name);
    while(it != _scopes.rend() && !it->is_valid(val)) {
        it++;
        if(it != _scopes.rend())
            val = it->find(name);
    }
    return it != _scopes.rend() && it->is_valid(val) ? val->second : nullptr;
}

const AST::VariableDeclaration* Scoped::get(const std::string_view& name) const {
    auto it = _scopes.rbegin();
    auto val = it->find(name);
    while(it != _scopes.rend() && !it->is_valid(val)) {
        it++;
        if(it != _scopes.rend())
            val = it->find(name);
    }
    return it != _scopes.rend() && it->is_valid(val) ? val->second : nullptr;
}

bool Scoped::is_type(const std::string_view& name) const {
    return get_type(name) != InvalidTypeID;
}
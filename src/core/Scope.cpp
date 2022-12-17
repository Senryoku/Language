#include "Scope.hpp"

const AST::TypeDeclaration* Scope::get_type(const std::string_view& name) const {
    auto r = _types.find(name);
    if(is_valid(r))
        return r->second;
    else
        return nullptr;
}

[[nodiscard]] const AST::FunctionDeclaration* Scope::resolve_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const {
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

                if(arguments[idx]->type_id != function->arguments()[idx]->type_id &&
                   // Allow casting to the generic 'pointer' type
                   (function->arguments()[idx]->type_id != PrimitiveType::Pointer || !GlobalTypeRegistry::instance().get_type(arguments[idx]->type_id)->is_pointer())) {
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

const AST::FunctionDeclaration* Scoped::get_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const {
    auto it = _scopes.rbegin();
    while(it != _scopes.rend()) {
        auto ret = it->resolve_function(name, arguments);
        if(ret)
            return ret;
        it++;
    }
    return nullptr;
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

const AST::TypeDeclaration* Scoped::get_type(const std::string_view& name) const {
    auto it = _scopes.rbegin();
    auto val = it->find_type(name);
    while(it != _scopes.rend() && !it->is_valid(val)) {
        it++;
        if(it != _scopes.rend())
            val = it->find_type(name);
    }
    if(it == _scopes.rend() || !it->is_valid(val))
        return nullptr;
    return val->second;
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
    auto builtin = GlobalTypeRegistry::instance().get_type(std::string(name))->type_id;
    if(builtin != InvalidTypeID)
        return true;
    // FIXME: Should be useless right now, but we may need to reintroduce some form of scoping for types, idk.
    // Search for a type declared with this name
    auto it = _scopes.rbegin();
    auto val = it->find_type(name);
    while(it != _scopes.rend() && !it->is_valid(val)) {
        it++;
        if(it != _scopes.rend())
            val = it->find_type(name);
    }
    return it != _scopes.rend() && it->is_valid(val);
}
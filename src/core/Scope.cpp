#include "Scope.hpp"

const AST::FunctionDeclaration* Scope::get_function(const std::string_view& name) const {
    auto r = _functions.find(name);
    if(is_valid(r))
        return r->second;
    else
        return nullptr;
}

const AST::TypeDeclaration* Scope::get_type(const std::string_view& name) const {
    auto r = _types.find(name);
    if(is_valid(r))
        return r->second;
    else
        return nullptr;
}

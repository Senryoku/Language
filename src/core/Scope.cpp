#include "Scope.hpp"

const AST::Node* Scope::get_function(const std::string_view& name) const {
    auto r = _functions.find(name);
    if(is_valid(r))
        return r->second.node;
    else
        return nullptr;
}

const Scope::TypeDeclaration Scope::get_type(const std::string_view& name) const {
    auto r = _types.find(name);
    if(is_valid(r))
        return r->second;
    else
        return {};
}

#include "GlobalTemplateCache.hpp"


const AST::FunctionDeclaration* GlobalTemplateCache::get_function(const std::string& name) {
    if(_functions.contains(name))
        return _functions.at(name).get();
    return nullptr;
}

const AST::TypeDeclaration* GlobalTemplateCache::get_type(const std::string& name) {
    if(_types.contains(name))
        return _types.at(name).get();
    return nullptr;
}
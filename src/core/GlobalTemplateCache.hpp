#pragma once

#include <unordered_map>
#include <memory>

#include <AST.hpp>

class GlobalTemplateCache {
  public:
    inline static GlobalTemplateCache& instance() {
        static GlobalTemplateCache gtc;
        return gtc;
    }

    const AST::FunctionDeclaration* get_function(const std::string& name);
    const AST::TypeDeclaration* get_type(const std::string& name);

    inline void register_function(const AST::FunctionDeclaration& node) { _functions.emplace(std::string(node.token.value), node.clone()); }
    inline void register_type(const AST::TypeDeclaration& node) { _types.emplace(std::string(node.token.value), node.clone()); }

  private:
    GlobalTemplateCache() =default;

    std::unordered_map<std::string, std::unique_ptr<const AST::FunctionDeclaration>> _functions;
    std::unordered_map<std::string, std::unique_ptr<const AST::TypeDeclaration>> _types;

};
#pragma once

#include <filesystem>
#include <fstream>
#include <span>
#include <tuple>
#include <vector>

#include <FlyString.hpp>
#include <GlobalTypeRegistry.hpp>
#include <Logger.hpp>

#include <Config.hpp>

// FIXME: Correctly set this path.
static const std::filesystem::path stdlib_folder(STDLIB_BASE_FOLDER);

std::filesystem::path resolve_dependency(const std::filesystem::path& working_directory, const std::string& dep);

class ModuleInterface {
  public:
    std::filesystem::path working_directory;

    std::vector<std::string>               dependencies;
    std::vector<AST::FunctionDeclaration*> exports;
    std::vector<AST::FunctionDeclaration*> imports;
    std::vector<AST::TypeDeclaration*>     type_exports;
    std::vector<AST::TypeDeclaration*>     type_imports;

    std::vector<std::unique_ptr<AST::TypeDeclaration>>     external_type_nodes;
    std::vector<std::unique_ptr<AST::FunctionDeclaration>> external_nodes;

    // Returns a span containing the newly imported nodes
    std::tuple<bool, std::span<AST::TypeDeclaration*>, std::span<AST::FunctionDeclaration*>> import_module(const std::filesystem::path& path);
    bool                                                                                     save(const std::filesystem::path& path) const;
    std::filesystem::path                                                                    resolve_dependency(const std::string& dep) const;

    static auto get_cache_filename(const std::filesystem::path& path) {
        return path.stem().concat("_").concat(std::to_string(std::hash<std::filesystem::path>{}(std::filesystem::absolute(path))));
    }
};

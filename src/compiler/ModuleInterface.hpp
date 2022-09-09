#pragma once

#include <filesystem>
#include <fstream>
#include <span>
#include <tuple>
#include <vector>

#include <FlyString.hpp>
#include <GenericValue.hpp>
#include <Logger.hpp>

class ModuleInterface {
  public:
    std::filesystem::path working_directory;

    std::vector<std::string>                dependencies;
    std::vector<AST::Node*>                 exports;
    std::vector<AST::Node*>                 imports;
    std::vector<std::unique_ptr<AST::Node>> external_nodes;

    // Returns a span containing the newly imported nodes
    std::tuple<bool, std::span<AST::Node*>> import_module(const std::filesystem::path& path) {
        std::ifstream module_file(path);
        if(!module_file) {
            error("[ModuleInterface] Could not find interface file {}.\n", path.string());
            return {false, std::span<AST::Node*>{}};
        }
        // FIXME: File format not specified
        std::string line;
        while(std::getline(module_file, line)) {
            if(line == "")
                break;
            dependencies.push_back(line);
        }
        auto        begin = imports.size();
        std::string name, type;
        while(std::getline(module_file, line)) {
            std::istringstream iss(line);
            iss >> name >> type;

            Tokenizer::Token token;
            token.type = Tokenizer::Token::Type::Identifier;
            token.value = *internalize_string(name);
            auto func_dec_node = external_nodes.emplace_back(new AST::Node(AST::Node::Type::FunctionDeclaration, token)).get(); // Keep it out of the AST
            imports.push_back(func_dec_node);
            func_dec_node->value.type = GenericValue::parse_type(type);

            print("     Imported {} : {}\n", name, type);

            while(iss >> type) {
                print("TODO: Exported function arguments {} !\n", type);
            }
        }

        return {true, std::span<AST::Node*>(imports.begin() + begin, imports.end())};
    }

    bool save(const std::filesystem::path& path) const {
        std::ofstream interface_file(path);
        if(!interface_file) {
            error("[ModuleInterface] Could not open interface file {} for writing.\n", path.string());
            return false;
        }
        // FIXME: Ultra TEMP, I didn't specify a file format for the module interface.
        for(const auto& dep : dependencies) {
            interface_file << dep << std::endl;
        }
        interface_file << std::endl;
        for(const auto& n : exports) {
            interface_file << n->token.value << " " << serialize(n->value.type) << std::endl;
        }

        return true;
    }

    std::filesystem::path resolve_dependency(const std::string& dep) const {
        // TODO: TEMP, define where (and how) we'll actually search for dependencies
        auto path = working_directory;
        return path.append(dep + ".lang").lexically_normal();
    }

    static auto get_cache_filename(const std::filesystem::path& path) { 
        return path.stem().concat("_").concat(fmt::format("{:x}", std::hash<std::filesystem::path>{}(std::filesystem::absolute(path))));
    }
};
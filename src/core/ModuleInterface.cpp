#include <ModuleInterface.hpp>

#include <Parser.hpp>

// Returns a span containing the newly imported nodes
std::tuple<bool, std::span<AST::TypeDeclaration*>, std::span<AST::FunctionDeclaration*>> ModuleInterface::import_module(const std::filesystem::path& path) {
    std::ifstream module_file(path);
    if(!module_file) {
        error("[ModuleInterface] Could not find interface file {}.\n", path.string());
        // TODO: Throw here, so we can actually directly address the issue? (i.e. 1/ Checking if the dependency exists, 2/ Compile it)
        return {false, std::span<AST::TypeDeclaration*>{}, std::span<AST::FunctionDeclaration*>{}};
    }
    // FIXME: File format not specified
    std::string line;
    while(std::getline(module_file, line)) {
        if(line == "")
            break;
        dependencies.push_back(line);
    }
    
    auto   type_begin = imports.size();
    {
        AST    type_ast;
        Parser type_parser;
        while(std::getline(module_file, line)) {
            if(line == "")
                break;
            Tokenizer          type_tokenizer(line);
            std::vector<Token> tokens;
            while(type_tokenizer.has_more()) {
                tokens.push_back(type_tokenizer.consume());
            }
            auto root_node = type_parser.parse(tokens, type_ast);
            // Remove superfluous nodes
            auto type_node = root_node;
            while(type_node->type != AST::Node::Type::TypeDeclaration && !type_node->children.empty())
                type_node = type_node->children[0];
            // Should be a simple node with a single child.
            assert(type_node->parent->children.size() == 1);
            type_node->parent->children.clear(); // Break the connection to keep our type_node intact.

            // Make sure token strings are still available.
            type_node->token.value = *internalize_string(std::string(type_node->token.value));
            for(auto member : dynamic_cast<AST::TypeDeclaration*>(type_node)->members()) {
                member->token.value = *internalize_string(std::string(member->token.value));
            }

            // FIXME: Move this to a log level of debug once we have that.
            // print("[ModuleInterface] Debug: Imported type '{}': \n{}", type_node->token.value, *type_node);
            external_type_nodes.emplace_back(dynamic_cast<AST::TypeDeclaration*>(type_node));
            type_imports.push_back(dynamic_cast<AST::TypeDeclaration*>(type_node));
        }
    }

    auto        begin = imports.size();
    std::string name, type;
    while(std::getline(module_file, line)) {
        std::istringstream iss(line);
        iss >> name >> type;

        Token token;
        token.type = Token::Type::Identifier;
        token.value = *internalize_string(name);
        auto func_dec_node = external_nodes.emplace_back(new AST::FunctionDeclaration(token)).get(); // Keep it out of the AST
        imports.push_back(func_dec_node);
        func_dec_node->flags |= AST::FunctionDeclaration::Flag::Imported;
        func_dec_node->type_id = GlobalTypeRegistry::instance().get_or_register_type(type)->type_id;

        while(iss >> type) {
            auto arg = func_dec_node->add_child(new AST::Node(AST::Node::Type::VariableDeclaration));
            arg->type_id = GlobalTypeRegistry::instance().get_or_register_type(type)->type_id;
        }
    }

    return {true, std::span<AST::TypeDeclaration*>(type_imports.begin() + type_begin, type_imports.end()),
            std::span<AST::FunctionDeclaration*>(imports.begin() + begin, imports.end())};
}

bool ModuleInterface::save(const std::filesystem::path& path) const {
    std::ofstream interface_file(path);
    if(!interface_file) {
        error("[ModuleInterface] Could not open interface file {} for writing.\n", path.string());
        return false;
    }
    // FIXME: Ultra TEMP, I didn't specify a file format for the module interface.
    
    // Dependencies
    for(const auto& dep : dependencies) {
        interface_file << dep << std::endl;
    }
    interface_file << std::endl;
    
    // Types
    for(const auto& n : type_exports) {
        interface_file << "type " << n->token.value << " { ";
        for(const auto& member : n->children) {
            interface_file << "let " << member->token.value << ": " << GlobalTypeRegistry::instance().get_type(member->type_id)->designation << "; ";
        }
        interface_file << "}" << std::endl;
    }
    interface_file << std::endl;
    
    // Functions
    for(const auto& n : exports) {
        interface_file << n->token.value << " " << GlobalTypeRegistry::instance().get_type(n->type_id)->designation;
        for(auto i = 0u; i < n->arguments().size(); ++i) {
            interface_file << " " << GlobalTypeRegistry::instance().get_type(n->arguments()[i]->type_id)->designation;
        }
        interface_file << std::endl;
    }

    return true;
}

std::filesystem::path ModuleInterface::resolve_dependency(const std::string& dep) const {
    // TODO: TEMP, define where (and how) we'll actually search for dependencies
    auto fullpath = working_directory / (dep + ".lang");

    // Look for a local version of a file, then in the 'global register', i.e. the standard library for now.

    if(!std::filesystem::exists(fullpath)) {
        const auto stdlib_candidate = (stdlib_folder / (dep + ".lang")).lexically_normal();
        if(std::filesystem::exists(stdlib_candidate)) {
            fullpath = stdlib_candidate;
            // FIXME: Move this to a log level of debug once we have that.
            // print("[ModuleInterface] Note: Import '{}' resolved to stdlib file '{}'.\n", dep, stdlib_candidate.string());
        }
    }

    return fullpath.lexically_normal();
}
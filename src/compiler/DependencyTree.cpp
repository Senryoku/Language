#include <DependencyTree.hpp>

#include <ModuleInterface.hpp>
#include <Parser.hpp>
#include <Tokenizer.hpp>

bool DependencyTree::construct(const std::filesystem::path& path) {
    const auto abs_path = std::filesystem::absolute(path).lexically_normal();
    _roots.insert(abs_path);
    return construct(abs_path, nullptr);
}

bool DependencyTree::construct(const std::filesystem::path& path, const File* from) {
    File& current_file = _files[path];
    current_file.path = path.lexically_normal();
    if(from)
        current_file.necessary_for.insert(from->path);

    std ::ifstream input_file(path);
    if(!input_file) {
        error("[DependencyTree::construct] Couldn't open file '{}' (Running from {}).\n", path.string(), std::filesystem::current_path().string());
        return false;
    }
    std::string source{(std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>()};

    std::vector<Token> tokens;
    try {
        Tokenizer tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());
    } catch(const Exception& e) {
        error("[DependencyTree::construct] Error tokenizing '{}':\n", path.string());
        e.display();
        return false;
    }

    Parser parser;
    parser.set_source(source);
    auto dependencies = parser.parse_dependencies(tokens);

    for(const auto& dep : dependencies) {
        auto resolved_path = resolve_dependency(current_file.path.parent_path(), dep);
        current_file.depends_on.insert(resolved_path);
        if(!construct(resolved_path, &current_file))
            return false;
    }

    return true;
}

// Note: Could be easily optimized.
ErrorOr<std::vector<std::vector<std::filesystem::path>>> DependencyTree::generate_processing_stages() const {
    auto deps_copy = _files;

    std::vector<std::vector<std::filesystem::path>> result;

    while(!deps_copy.empty()) {
        auto& ready = result.emplace_back();
        for(const auto& file : deps_copy) {
            if(file.second.depends_on.empty())
                ready.push_back(file.first);
        }

        if(ready.empty())
            return Error("Cyclic dependency detected.");

        for(const auto& path : ready) {
            for(const auto& depending : deps_copy[path].necessary_for)
                deps_copy[depending].depends_on.erase(path);
            deps_copy.erase(path);
        }
    }

    return result;
}
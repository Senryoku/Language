#pragma once

#include <set>
#include <unordered_map>
#include <filesystem>

#include <Error.hpp>

class DependencyTree {
  public:
    struct File {
        std::filesystem::path           path;
        std::set<std::filesystem::path> depends_on;
        std::set<std::filesystem::path> necessary_for;
    };
    
    void                                                     construct(const std::filesystem::path& path);
    ErrorOr<std::vector<std::vector<std::filesystem::path>>> generate_processing_stages() const;
  

  private:
    std::set<std::filesystem::path>                 _roots;
    std::unordered_map<std::filesystem::path, File> _files;

    void construct(const std::filesystem::path& path, const File* from);
};

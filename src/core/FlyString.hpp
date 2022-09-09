#pragma once

#include <unordered_map>

static inline std::unordered_map<std::string, std::unique_ptr<std::string>> _fly_strings;

// FIXME: Global interning? (Fly strings)
inline std::string* internalize_string(const std::string& str) {
    auto it = _fly_strings.find(str);
    if (it != _fly_strings.end()) {
        return it->second.get();
    }
    auto p = new std::string(str);
    _fly_strings.emplace(str, p);
    return p;
}

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

// Hash & Comparison to let unordered_map::find work with string_views (Heterogeneous Lookup)
struct string_equal : public std::equal_to<> {
    using is_transparent = void;
    bool operator()(std::string_view l, std::string_view r) const noexcept { return l == r; }
};
struct string_hash {
    using is_transparent = void;
    using key_equal = std::equal_to<>;             // Pred to use
    using hash_type = std::hash<std::string_view>; // just a helper local type
    size_t operator()(std::string_view txt) const { return hash_type{}(txt); }
    size_t operator()(const std::string& txt) const { return hash_type{}(txt); }
    size_t operator()(const char* txt) const { return hash_type{}(txt); }
};

template<typename T>
using het_unordered_map = std::unordered_map<std::string, T, string_hash, string_equal>;

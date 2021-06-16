#pragma once

#include <string_view>
#include <unordered_map>

#include <GenericValue.hpp>

// class Variable : public GenericValue {};
using Variable = GenericValue;

// Hash & Comparison to let unordered_map::find work with string_views (Heterogeneous Lookup)
struct TransparentEqual : public std::equal_to<> {
    using is_transparent = void;
};
struct string_hash {
    using is_transparent = void;
    using key_equal = std::equal_to<>;             // Pred to use
    using hash_type = std::hash<std::string_view>; // just a helper local type
    size_t operator()(std::string_view txt) const { return hash_type{}(txt); }
    size_t operator()(const std::string& txt) const { return hash_type{}(txt); }
    size_t operator()(const char* txt) const { return hash_type{}(txt); }
};

using VariableStore = std::unordered_map<std::string, Variable, string_hash, TransparentEqual>;
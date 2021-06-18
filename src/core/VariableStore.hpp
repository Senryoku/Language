#pragma once

#include <het_unordered_map.hpp>

#include <GenericValue.hpp>

// class Variable : public GenericValue {};
using Variable = GenericValue;

using VariableStore = het_unordered_map<Variable>;
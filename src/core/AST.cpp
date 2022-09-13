#include <AST.hpp>

bool compatible(GenericValue::Type lhs, GenericValue::Type rhs) {
    // TODO
    return lhs == rhs || (lhs == GenericValue::Type::Float && rhs == GenericValue::Type::Integer) || (lhs == GenericValue::Type::Integer && rhs == GenericValue::Type::Float);
}
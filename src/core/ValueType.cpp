#include <ValueType.hpp>

#include <GlobalTypeRegistry.hpp>

bool StructType::is_placeholder() const {
    for(const auto& [name, member] : members)
        if(GlobalTypeRegistry::instance().get_type(member.type_id)->is_placeholder())
            return true;
    return false;
}

bool PointerType::is_placeholder() const {
    return GlobalTypeRegistry::instance().get_type(pointee_type)->is_placeholder();
}

bool ArrayType::is_placeholder() const {
    return GlobalTypeRegistry::instance().get_type(element_type)->is_placeholder();
}

bool TemplatedType::is_placeholder() const {
    for(const auto& param : parameters)
        if(GlobalTypeRegistry::instance().get_type(param)->is_placeholder())
            return true;
    return false;
}
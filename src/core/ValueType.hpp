#pragma once

#include <cassert>

#include <PrimitiveType.hpp>

class Type {
  public:
    Type(std::string _designation, TypeID _type_id) : designation(_designation), type_id(_type_id) {}

    std::string designation;
    TypeID      type_id = InvalidTypeID;

    virtual bool        is_array() const { return false; }
    virtual bool        is_pointer() const { return false; }
};

class ScalarType : public Type {
  public:
    ScalarType(std::string _designation, TypeID _type_id) : Type(_designation, _type_id) {}
};

class PointerType : public Type {
  public:
    PointerType(std::string _designation, TypeID _type_id, TypeID _pointee_type) : Type(_designation, _type_id), pointee_type(_pointee_type) {}
    
    TypeID pointee_type = InvalidTypeID;

    bool is_pointer() const override { return true; }
};

class ArrayType : public Type {
  public:
    ArrayType(std::string _designation, TypeID _type_id, TypeID _element_type, size_t _capacity) : Type(_designation, _type_id), element_type(_element_type), capacity(_capacity) {}

    TypeID element_type = InvalidTypeID;
    size_t capacity = 0;

    bool is_array() const override { return true; }
};

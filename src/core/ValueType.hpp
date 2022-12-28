#pragma once

#include <cassert>

#include <AST.hpp>
#include <PrimitiveType.hpp>

class Type {
  public:
    Type(std::string _designation, TypeID _type_id) : designation(_designation), type_id(_type_id) {}
    virtual ~Type() = default;

    std::string designation;
    TypeID      type_id = InvalidTypeID;

    virtual bool is_array() const { return false; }
    virtual bool is_pointer() const { return false; }
    virtual bool is_struct() const { return false; }
    virtual bool is_placeholder() const { return false; }
    virtual bool is_templated() const { return false; }
};

class ScalarType : public Type {
  public:
    ScalarType(std::string _designation, TypeID _type_id) : Type(_designation, _type_id) {}
    virtual ~ScalarType() = default;
};

class PlaceholderType : public Type {
  public:
    PlaceholderType(std::string _designation, TypeID _type_id) : Type(_designation, _type_id) {}
    virtual ~PlaceholderType() = default;

    virtual bool is_placeholder() const { return true; }
};

class StructType : public Type {
  public:
    StructType(std::string _designation, TypeID _type_id) : Type(_designation, _type_id) {}
    virtual ~StructType() = default;

    struct Member {
        std::string name;
        uint32_t    index;
        TypeID      type_id;
    };

    std::unordered_map<std::string, Member> members;

    bool         is_struct() const override { return true; }
    virtual bool is_placeholder() const override;

    const Member& get_member(size_t idx) {
        assert(idx < members.size());
        for(const auto& [name, member] : members)
            if(member.index == idx)
                return member;
    }
};

class PointerType : public Type {
  public:
    PointerType(std::string _designation, TypeID _type_id, TypeID _pointee_type) : Type(_designation, _type_id), pointee_type(_pointee_type) { assert(_type_id != _pointee_type); }
    virtual ~PointerType() = default;

    TypeID pointee_type = InvalidTypeID;

    virtual bool is_pointer() const override { return true; }
    virtual bool is_placeholder() const override;
};

class ArrayType : public Type {
  public:
    ArrayType(std::string _designation, TypeID _type_id, TypeID _element_type, size_t _capacity) : Type(_designation, _type_id), element_type(_element_type), capacity(_capacity) {}
    virtual ~ArrayType() = default;

    TypeID element_type = InvalidTypeID;
    size_t capacity = 0;

    virtual bool is_array() const override { return true; }
    virtual bool is_placeholder() const override;
};

class TemplatedType : public Type {
  public:
    TemplatedType(std::string _designation, TypeID _type_id, TypeID _template_type_id, const std::vector<TypeID>& _parameters)
        : Type(_designation, _type_id), template_type_id(_template_type_id), parameters(_parameters) {}
    virtual ~TemplatedType() = default;

    TypeID              template_type_id = InvalidTypeID;
    std::vector<TypeID> parameters;

    virtual bool is_templated() const override { return true; }
    virtual bool is_placeholder() const override;
};

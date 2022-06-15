#pragma once

#include <het_unordered_map.hpp>

#include <GenericValue.hpp>

struct Variable : public GenericValue {
  public:
    Variable() : GenericValue() {}
    Variable(const GenericValue& v) : GenericValue(v) {}
	Variable(GenericValue::Type type, int32_t _value) : GenericValue(type) {
		value.as_int32_t = _value;
	} 

    Variable& operator++() {
        assert(!is_const());
        assert(type == Type::Integer);
        ++value.as_int32_t;
        return *this;
    }

    Variable operator++(int) {
        assert(!is_const());
        assert(type == Type::Integer);
        Variable r = *this;
        ++value.as_int32_t;
        return r;
    }

    Variable& operator--() {
        assert(!is_const());
        assert(type == Type::Integer);
        --value.as_int32_t;
        return *this;
    }

    Variable operator--(int) {
        assert(!is_const());
        assert(type == Type::Integer);
        Variable r = *this;
        --value.as_int32_t;
        return r;
    }
};

using VariableStore = het_unordered_map<Variable>;

template<>
struct fmt::formatter<Variable> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin(), end = ctx.end();
        if(it != end && *it != '}')
            throw format_error("Invalid format for Variable");
        return it;
    }
    template<typename FormatContext>
    auto format(const Variable& v, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", static_cast<const GenericValue&>(v));
    }
};

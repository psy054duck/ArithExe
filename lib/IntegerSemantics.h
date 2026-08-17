#ifndef INTEGER_SEMANTICS_H
#define INTEGER_SEMANTICS_H

#include "z3++.h"

namespace ari_exe::integer_semantics {

inline z3::expr power_of_two(z3::context& ctx, unsigned exponent);
inline z3::expr as_unsigned(const z3::expr& value, unsigned width);

inline z3::expr as_int(const z3::expr& value) {
    if (value.is_bool()) {
        return z3::ite(value, value.ctx().int_val(1), value.ctx().int_val(0));
    }
    return value;
}

inline z3::expr to_bv(const z3::expr& value, unsigned width) {
    return z3::int2bv(width, as_int(value));
}

inline z3::expr from_bv(const z3::expr& value, unsigned width) {
    if (width == 1) {
        return value == value.ctx().bv_val(1, 1);
    }
    return z3::bv2int(value, true);
}

inline z3::expr as_signed(const z3::expr& value, unsigned width) {
    if (width == 1 && value.is_bool()) {
        return z3::ite(value, value.ctx().int_val(-1),
                       value.ctx().int_val(0));
    }
    z3::expr unsigned_value = as_unsigned(value, width);
    z3::expr modulus = power_of_two(value.ctx(), width);
    return z3::ite(unsigned_value >= power_of_two(value.ctx(), width - 1),
                   unsigned_value - modulus, unsigned_value)
        .simplify();
}

inline z3::expr as_unsigned(const z3::expr& value, unsigned width) {
    if (value.is_bool()) return as_int(value);
    return (as_int(value) % power_of_two(value.ctx(), width)).simplify();
}

inline z3::expr signed_div(const z3::expr& lhs, const z3::expr& rhs) {
    z3::expr left = as_int(lhs);
    z3::expr right = as_int(rhs);
    z3::expr abs_left = z3::ite(left < 0, -left, left);
    z3::expr abs_right = z3::ite(right < 0, -right, right);
    z3::expr quotient = abs_left / abs_right;
    return z3::ite((left < 0) != (right < 0), -quotient, quotient)
        .simplify();
}

inline z3::expr signed_rem(const z3::expr& lhs, const z3::expr& rhs) {
    z3::expr left = as_int(lhs);
    z3::expr right = as_int(rhs);
    z3::expr abs_left = z3::ite(left < 0, -left, left);
    z3::expr abs_right = z3::ite(right < 0, -right, right);
    z3::expr remainder = abs_left % abs_right;
    return z3::ite(left < 0, -remainder, remainder).simplify();
}

inline z3::expr power_of_two(z3::context& ctx, unsigned exponent) {
    z3::expr result = ctx.int_val(1);
    for (unsigned i = 0; i < exponent; ++i) result = result * 2;
    return result.simplify();
}

inline z3::expr signed_min(z3::context& ctx, unsigned width) {
    return -power_of_two(ctx, width - 1);
}

inline z3::expr signed_max(z3::context& ctx, unsigned width) {
    return power_of_two(ctx, width - 1) - 1;
}

inline z3::expr in_signed_range(const z3::expr& value, unsigned width) {
    z3::expr integer = as_int(value);
    return integer >= signed_min(value.ctx(), width) &&
           integer <= signed_max(value.ctx(), width);
}

inline z3::expr in_unsigned_range(const z3::expr& value, unsigned width) {
    z3::expr integer = as_int(value);
    return integer >= 0 && integer < power_of_two(value.ctx(), width);
}

} // namespace ari_exe::integer_semantics

#endif

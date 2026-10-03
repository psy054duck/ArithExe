#ifndef RELAXED_INTEGER_SEMANTICS_H
#define RELAXED_INTEGER_SEMANTICS_H

#include <optional>
#include <string>
#include <utility>
#include <boost/multiprecision/cpp_int.hpp>
#include "IntegerSemantics.h"

namespace ari_exe::integer_semantics {

enum class IntegerBitwiseOperation { And, Or, Xor };

// Infinite two's-complement bitwise operations with a constant mask, encoded
// using only Int arithmetic. These moduli come from the mask, not an IR width.
inline std::optional<z3::expr> integer_bitwise(
    z3::expr lhs, z3::expr rhs, IntegerBitwiseOperation operation) {
    lhs = as_int(lhs).simplify();
    rhs = as_int(rhs).simplify();
    if (z3::eq(lhs, rhs)) {
        return operation == IntegerBitwiseOperation::Xor
                   ? lhs.ctx().int_val(0) : lhs;
    }
    if (!rhs.is_numeral()) std::swap(lhs, rhs);
    if (!rhs.is_numeral()) return std::nullopt;
    using boost::multiprecision::cpp_int;
    cpp_int mask(Z3_get_numeral_string(rhs.ctx(), rhs));
    const bool negative = mask < 0;
    if (negative) mask = -mask - 1;
    z3::expr intersection = lhs.ctx().int_val(0);
    cpp_int weight = 1;
    unsigned digits = 0;
    while (mask != 0) {
        // A resource limit, not a solver-side bit-width constraint.
        if (++digits > 4096) return std::nullopt;
        if ((mask & 1) != 0) {
            const auto text = weight.str();
            const z3::expr place = lhs.ctx().int_val(text.c_str());
            intersection = intersection + ((lhs / place) % 2) * place;
        }
        mask >>= 1;
        weight <<= 1;
    }
    if (negative) intersection = lhs - intersection;
    if (operation == IntegerBitwiseOperation::And) {
        return intersection.simplify();
    }
    return (lhs + rhs - intersection *
            (operation == IntegerBitwiseOperation::Or ? 1 : 2)).simplify();
}

inline std::optional<z3::expr> integer_shift_factor(z3::expr amount) {
    amount = as_int(amount).simplify();
    if (!amount.is_numeral()) return std::nullopt;
    using boost::multiprecision::cpp_int;
    const cpp_int shift(Z3_get_numeral_string(amount.ctx(), amount));
    if (shift < 0) return amount.ctx().int_val(1); // Caller marks undefined.
    if (shift > 4096) return std::nullopt;
    const cpp_int factor = cpp_int(1) << shift.convert_to<unsigned>();
    const auto text = factor.str();
    return amount.ctx().int_val(text.c_str());
}

} // namespace ari_exe::integer_semantics
#endif

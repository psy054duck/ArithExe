#pragma once
#include <z3++.h>

namespace ari_exe {
// Equivalent query over the original free symbols. Fresh quotient symbols are
// existentially bound, including beneath existing quantifiers.
// For solver assertions, bind_top_level=false leaves fresh witnesses free and
// conjoins their definitions (equisatisfiable, preserving original input models).
z3::expr integerize_rational_query(const z3::expr& query, bool bind_top_level = true);
}

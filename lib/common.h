#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <algorithm>
#include "z3++.h"

namespace ari_exe {
    // Prefix for all Z3 variables
    constexpr const char* Z3_PREFIX = "ari_";

    inline std::string get_z3_name(const std::string& name) {
        auto z3_name = name;
        std::replace(z3_name.begin(), z3_name.end(), '.', '_');
        return Z3_PREFIX + z3_name;
    }


}

#endif
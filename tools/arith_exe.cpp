#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>

#include <spdlog/spdlog.h>

// LLVM headers
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/IRReader/IRReader.h"

#include "engine.h"
#include "AnalysisManager.h"

#include "logics.h"
#include <assert.h>
#include <iostream>

using namespace ari_exe;

static void print_usage() {
    spdlog::error(
        "Usage: arith_exe "
        "[--enable-bounded-cfinite|--disable-bounded-cfinite] "
        "[--poly-expr-strategy=auto|special|algorithm2|general] "
        "[--poly-expr-degree=N] "
        "[--poly-expr-order=N] "
        "<source_file.c>"
    );
}

static bool is_poly_expr_strategy(const std::string& strategy) {
    return strategy == "auto" || strategy == "special" ||
           strategy == "algorithm2" || strategy == "algorithm-2" ||
           strategy == "nested" || strategy == "nondet" ||
           strategy == "spectral" || strategy == "spectral-nd" ||
           strategy == "general";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    spdlog::set_level(spdlog::level::info);

    bool bounded_cfinite_enabled = true;
    std::string poly_expr_strategy = "general"; // "auto";
    bool poly_expr_degree_set = false;
    int poly_expr_degree = 0;
    int poly_expr_order = 4;
    std::string source_file;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--enable-bounded-cfinite") {
            bounded_cfinite_enabled = true;
        } else if (arg == "--disable-bounded-cfinite") {
            bounded_cfinite_enabled = false;
        } else if (arg.rfind("--poly-expr-strategy=", 0) == 0) {
            poly_expr_strategy = arg.substr(std::string("--poly-expr-strategy=").size());
            if (!is_poly_expr_strategy(poly_expr_strategy)) {
                spdlog::error("Unknown polynomial-expression strategy: {}", poly_expr_strategy);
                print_usage();
                return 1;
            }
        } else if (arg.rfind("--poly-expr-degree=", 0) == 0) {
            auto degree_value = arg.substr(std::string("--poly-expr-degree=").size());
            try {
                poly_expr_degree = std::stoi(degree_value);
            } catch (...) {
                spdlog::error("Invalid polynomial-expression degree: {}", degree_value);
                print_usage();
                return 1;
            }
            if (poly_expr_degree < 0) {
                spdlog::error("Polynomial-expression degree must be nonnegative.");
                print_usage();
                return 1;
            }
            poly_expr_degree_set = true;
        } else if (arg.rfind("--poly-expr-order=", 0) == 0) {
            auto order_value = arg.substr(std::string("--poly-expr-order=").size());
            try {
                poly_expr_order = std::stoi(order_value);
            } catch (...) {
                spdlog::error("Invalid polynomial-expression order: {}", order_value);
                print_usage();
                return 1;
            }
            if (poly_expr_order < 1) {
                spdlog::error("Polynomial-expression order must be positive.");
                print_usage();
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg.starts_with("-")) {
            spdlog::error("Unknown option: {}", arg);
            print_usage();
            return 1;
        } else if (source_file.empty()) {
            source_file = arg;
        } else {
            spdlog::error("Unexpected extra argument: {}", arg);
            print_usage();
            return 1;
        }
    }

    if (source_file.empty()) {
        print_usage();
        return 1;
    }

    setenv("ARITHEXE_ENABLE_BOUNDED_CFINITE", bounded_cfinite_enabled ? "1" : "0", 1);
    setenv("ARITHEXE_POLY_EXPR_STRATEGY", poly_expr_strategy.c_str(), 1);
    if (poly_expr_degree_set) {
        std::string poly_expr_degree_str = std::to_string(poly_expr_degree);
        setenv("ARITHEXE_POLY_EXPR_DEGREE", poly_expr_degree_str.c_str(), 1);
    } else {
        unsetenv("ARITHEXE_POLY_EXPR_DEGREE");
    }
    std::string poly_expr_order_str = std::to_string(poly_expr_order);
    setenv("ARITHEXE_POLY_EXPR_ORDER", poly_expr_order_str.c_str(), 1);

    auto engine = Engine(source_file);
    auto res = engine.verify();

    delete State::func_summaries;
    delete State::loop_summaries;

    switch (res) {
        case HOLD:
            spdlog::info("The program is safe.");
            break;
        case FAIL:
            spdlog::error("The program is unsafe.");
            break;
        case VERIUNKNOWN:
            spdlog::warn("The verification result is unknown.");
            break;
    }
    for (auto& pair : AInstruction::cached_instructions) {
        delete pair.second;
    }
    return 0;
}

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <fstream>
#include <cctype>

#include <spdlog/spdlog.h>

// LLVM headers
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/IRReader/IRReader.h"

#include "engine.h"
#include "AnalysisManager.h"
#include "Witness.h"

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
        "[--witness=PATH|--no-witness] "
        "[--property-file=PATH] "
        "[--data-model=ILP32|LP64] "
        "<source_file.c>"
    );
}

static std::string read_specification(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot read property file: " + path);
    }
    std::ostringstream raw;
    raw << input.rdbuf();

    std::string normalized;
    bool pending_space = false;
    for (unsigned char ch : raw.str()) {
        if (std::isspace(ch)) {
            pending_space = !normalized.empty();
        } else {
            if (pending_space) normalized.push_back(' ');
            normalized.push_back(static_cast<char>(ch));
            pending_space = false;
        }
    }
    if (normalized.empty()) {
        throw std::runtime_error("Property file is empty: " + path);
    }
    return normalized;
}

static bool is_supported_specification(const std::string& specification) {
    return specification.find("G ! call(reach_error())") != std::string::npos ||
           specification.find("G ! call(__VERIFIER_error())") != std::string::npos;
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
    bool witness_enabled = true;
    std::string witness_path = "witness.yml";
    std::string property_file;
    std::string data_model = "LP64";
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
        } else if (arg == "--no-witness") {
            witness_enabled = false;
        } else if (arg.rfind("--witness=", 0) == 0) {
            witness_path = arg.substr(std::string("--witness=").size());
            if (witness_path.empty()) {
                spdlog::error("Witness output path must not be empty.");
                return 1;
            }
        } else if (arg == "--witness") {
            if (++i >= argc) {
                spdlog::error("--witness requires an output path.");
                return 1;
            }
            witness_path = argv[i];
        } else if (arg.rfind("--property-file=", 0) == 0) {
            property_file = arg.substr(std::string("--property-file=").size());
        } else if (arg == "--property-file") {
            if (++i >= argc) {
                spdlog::error("--property-file requires a path.");
                return 1;
            }
            property_file = argv[i];
        } else if (arg.rfind("--data-model=", 0) == 0) {
            data_model = arg.substr(std::string("--data-model=").size());
            if (data_model != "ILP32" && data_model != "LP64") {
                spdlog::error("Data model must be ILP32 or LP64.");
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
    setenv("ARITHEXE_DATA_MODEL", data_model.c_str(), 1);

    std::string specification = WitnessOptions{}.specification;
    if (!property_file.empty()) {
        try {
            specification = read_specification(property_file);
        } catch (const std::exception& error) {
            spdlog::error("{}", error.what());
            return 1;
        }
        if (!is_supported_specification(specification)) {
            spdlog::error(
                "Unsupported property. ArithExe witness generation currently "
                "supports unreachability of reach_error only."
            );
            return 1;
        }
    }

    auto engine = Engine(source_file);
    auto res = engine.verify();

    bool witness_written = true;
    if (witness_enabled && res != VERIUNKNOWN) {
        WitnessOptions witness_options;
        witness_options.output_path = witness_path;
        witness_options.input_file = source_file;
        witness_options.data_model = data_model;
        witness_options.specification = specification;
        if (witness_written) {
            WitnessWriter writer(std::move(witness_options));
            witness_written = writer.write(
                res, *engine.get_module(), engine.get_violation_instruction(),
                engine.get_counterexample_inputs(),
                engine.get_loop_certificates(),
                engine.get_function_certificates());
            if (!witness_written) {
                spdlog::error("Failed to write witness: {}", writer.error());
            } else {
                spdlog::info("Wrote SV-COMP witness to {}", witness_path);
            }
        }
    }

    delete State::func_summaries;
    delete State::loop_summaries;

    switch (res) {
        case HOLD:
            spdlog::info("The program is safe.");
            std::cout << "TRUE\n";
            break;
        case FAIL:
            spdlog::error("The program is unsafe.");
            std::cout << "FALSE(unreach-call)\n";
            break;
        case VERIUNKNOWN:
            spdlog::warn("The verification result is unknown.");
            std::cout << "UNKNOWN\n";
            break;
    }
    for (auto& pair : AInstruction::cached_instructions) {
        delete pair.second;
    }
    return witness_written ? 0 : 2;
}

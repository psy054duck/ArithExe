#ifndef ARITHEXE_WITNESS_H
#define ARITHEXE_WITNESS_H

#include <string>
#include <vector>

#include "common.h"

namespace llvm {
class Instruction;
class Module;
}

namespace ari_exe {

struct WitnessOptions {
    std::string output_path = "witness.yml";
    std::string input_file;
    std::string specification =
        "CHECK( init(main()), LTL(G ! call(reach_error())) )";
    std::string data_model = "LP64";
    std::string producer_name = "ArithExe";
    std::string producer_version = "development";
};

/** Write an SV-COMP YAML witness in exchange-format version 2.1. */
class WitnessWriter {
  public:
    explicit WitnessWriter(WitnessOptions options);

    /**
     * Write a correctness or violation witness for a conclusive result.
     * Returns false and sets error() if no source-level target can be found or
     * the output cannot be written.
     */
    bool write(VeriResult result, const llvm::Module& module,
               const llvm::Instruction* violation_instruction = nullptr,
               const std::vector<CounterexampleInput>& inputs = {},
               const std::vector<LoopCertificate>& loop_certificates = {},
               const std::vector<FunctionCertificate>&
                   function_certificates = {});

    const std::string& error() const { return error_message; }

  private:
    WitnessOptions options;
    std::string error_message;
};

} // namespace ari_exe

#endif

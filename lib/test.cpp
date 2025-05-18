#include "engine.h"

int main(int argc, char** argv) {
    llvm::LLVMContext ctx;
    llvm::SMDiagnostic Err;

    // Load the module from the given file
    std::unique_ptr<llvm::Module> mod(parseIRFile(argv[1], Err, ctx));

    ari_exe::Engine engine(mod);
    engine.run();
}
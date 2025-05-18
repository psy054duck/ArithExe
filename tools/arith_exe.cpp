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

#include <iostream>

using namespace ari_exe;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        spdlog::error("Usage: arith_exe <source_file.c>");
        return 1;
    }
    z3::context z3ctx = z3::context();
    auto engine = Engine(argv[1], z3ctx);
    auto res = engine.verify();
    delete State::summaries;
    switch (res) {
        case Engine::HOLD:
            spdlog::info("The program is safe.");
            break;
        case Engine::FAIL:
            spdlog::error("The program is unsafe.");
            break;
        case Engine::VERIUNKNOWN:
            spdlog::warn("The verification result is unknown.");
            break;
    }
    return 0;
}

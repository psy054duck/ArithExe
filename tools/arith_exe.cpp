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

#include <iostream>

using namespace ari_exe;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        spdlog::error("Usage: arith_exe <source_file.c>");
        return 1;
    }
    spdlog::set_level(spdlog::level::info);
    auto engine = Engine(argv[1]);
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

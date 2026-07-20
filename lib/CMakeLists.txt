add_library(AInstruction AInstruction.cpp)
add_library(MStack MStack.cpp)
add_library(engine engine.cpp)
add_library(state state.cpp)
add_library(rec_solver rec_solver.cpp)
add_library(FunctionSummarizer FunctionSummarizer.cpp)
add_library(FunctionSummary FunctionSummary.cpp)
add_library(LoopSummarizer LoopSummarizer.cpp)
add_library(LoopSummary LoopSummary.cpp)
add_library(AnalysisManager AnalysisManager.cpp)
add_library(logics logics.cpp)
add_library(Memory Memory.cpp)
add_library(MemoryObject MemoryObject.cpp)
add_library(common common.cpp)
add_library(cache cache.cpp)
add_library(Expression Expr.cpp)
add_library(Witness Witness.cpp)

target_compile_definitions(
    rec_solver
    PRIVATE
        ARITHEXE_DEFAULT_SOLVER_WORKER="${CMAKE_SOURCE_DIR}/solver_worker.py"
        ARITHEXE_DEFAULT_SOLVER_SCRIPT="${CMAKE_SOURCE_DIR}/solver.py"
)

target_link_libraries(engine PRIVATE spdlog::spdlog AInstruction)
target_link_libraries(state PRIVATE spdlog::spdlog MStack)
target_link_libraries(rec_solver PRIVATE spdlog::spdlog)
target_link_libraries(FunctionSummary PRIVATE spdlog::spdlog)
target_link_libraries(FunctionSummarizer PRIVATE spdlog::spdlog rec_solver FunctionSummary)
target_link_libraries(LoopSummary PRIVATE spdlog::spdlog)
target_link_libraries(LoopSummarizer PRIVATE spdlog::spdlog rec_solver LoopSummary)
target_link_libraries(MStack PRIVATE spdlog::spdlog)
target_link_libraries(AInstruction PRIVATE spdlog::spdlog cache FunctionSummarizer FunctionSummary LoopSummarizer LoopSummary MemoryObject common Expression)
target_link_libraries(AnalysisManager PRIVATE spdlog::spdlog)
target_compile_definitions(
    AnalysisManager
    PRIVATE ARITHEXE_DEFAULT_CLANG="${LLVM_TOOLS_BINARY_DIR}/clang"
)
target_link_libraries(logics PRIVATE spdlog::spdlog AnalysisManager ${llvm_libs})
target_link_libraries(Memory PRIVATE spdlog::spdlog)
target_link_libraries(MemoryObject PRIVATE spdlog::spdlog logics)
target_link_libraries(Expression PRIVATE common logics)
target_link_libraries(Witness PRIVATE ${llvm_libs})

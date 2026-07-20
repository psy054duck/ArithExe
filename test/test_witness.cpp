#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "AInstruction.h"
#include "Witness.h"
#include "engine.h"

using namespace ari_exe;

namespace {
#ifndef ARITHEXE_TEST_BENCHMARK_DIR
#define ARITHEXE_TEST_BENCHMARK_DIR "../../test/benchmark"
#endif

std::string benchmark_path(const std::string& relative_path) {
    return std::string(ARITHEXE_TEST_BENCHMARK_DIR) + "/" + relative_path;
}

void reset_caches() {
    for (auto& [_, instruction] : AInstruction::cached_instructions) {
        delete instruction;
    }
    AInstruction::cached_instructions.clear();
    AInstructionPhi::failed_loops.clear();
    AnalysisManager::unknown_counter = 0;

    delete State::func_summaries;
    State::func_summaries = new SymbolTable<FunctionSummary>();
    delete State::loop_summaries;
    State::loop_summaries = new SymbolTable<LoopSummary>();
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

size_t count_occurrences(const std::string& text, const std::string& needle) {
    size_t count = 0;
    for (size_t position = 0;
         (position = text.find(needle, position)) != std::string::npos;
         position += needle.size()) {
        ++count;
    }
    return count;
}

class WitnessTest : public ::testing::Test {
  protected:
    void SetUp() override { reset_caches(); }
    void TearDown() override { reset_caches(); }
};

TEST_F(WitnessTest, WritesCorrectnessWitnessForLoopProof) {
    const std::string source = benchmark_path("loops/true_3.c");
    const std::string output = "test-loop-witness.yml";
    Engine engine(source);
    ASSERT_EQ(engine.verify(), HOLD);

    WitnessOptions options;
    options.input_file = source;
    options.output_path = output;
    WitnessWriter writer(options);
    ASSERT_TRUE(writer.write(HOLD, *engine.get_module(), nullptr, {},
                             engine.get_loop_certificates()))
        << writer.error();

    const std::string witness = read_file(output);
    EXPECT_NE(witness.find("format_version: \"2.1\""), std::string::npos);
    EXPECT_NE(witness.find("input_file_hashes:"), std::string::npos);
    EXPECT_NE(witness.find("data_model: \"LP64\""), std::string::npos);
    EXPECT_NE(witness.find("entry_type: \"invariant_set\""), std::string::npos);
    EXPECT_NE(witness.find("type: \"loop_invariant\""), std::string::npos);
    EXPECT_NE(witness.find("entry_type: \"ghost_instrumentation\""),
              std::string::npos);
    EXPECT_NE(witness.find("arithexe_loop_0_iteration"), std::string::npos);
    EXPECT_NE(witness.find("type: \"location_invariant\""), std::string::npos);
    EXPECT_NE(witness.find("line: 19"), std::string::npos);
    EXPECT_NE(witness.find("value: \"0\""), std::string::npos);
    std::filesystem::remove(output);
}

TEST_F(WitnessTest, WritesViolationWitnessForRecursiveCounterexample) {
    const std::string source = benchmark_path("recursion/false_1.c");
    const std::string output = "test-recursion-witness.yml";
    Engine engine(source);
    ASSERT_EQ(engine.verify(), FAIL);

    WitnessOptions options;
    options.input_file = source;
    options.output_path = output;
    WitnessWriter writer(options);
    ASSERT_TRUE(writer.write(FAIL, *engine.get_module(),
                             engine.get_violation_instruction(),
                             engine.get_counterexample_inputs()))
        << writer.error();

    const std::string witness = read_file(output);
    EXPECT_NE(witness.find("entry_type: \"violation_sequence\""),
              std::string::npos);
    EXPECT_NE(witness.find("type: \"target\""), std::string::npos);
    EXPECT_NE(witness.find("line: 18"), std::string::npos);
    EXPECT_NE(witness.find("type: \"function_return\""), std::string::npos);
    EXPECT_NE(witness.find("constraint:"), std::string::npos);
    EXPECT_NE(witness.find("format: \"ext_c_expression\""),
              std::string::npos);
    EXPECT_NE(witness.find("\\\\result =="), std::string::npos);
    std::filesystem::remove(output);
}

TEST_F(WitnessTest, WritesFunctionContractForRecursiveProof) {
    const std::string source = benchmark_path("recursion/true_1.c");
    const std::string output = "test-recursion-correctness-witness.yml";
    Engine engine(source);
    ASSERT_EQ(engine.verify(), HOLD);

    WitnessOptions options;
    options.input_file = source;
    options.output_path = output;
    WitnessWriter writer(options);
    ASSERT_TRUE(writer.write(HOLD, *engine.get_module(), nullptr, {},
                             engine.get_loop_certificates(),
                             engine.get_function_certificates()))
        << writer.error();

    const std::string witness = read_file(output);
    EXPECT_NE(witness.find("type: \"function_contract\""),
              std::string::npos);
    EXPECT_NE(witness.find("ensures: \"\\\\result =="),
              std::string::npos);
    EXPECT_NE(witness.find("\\\\at(m, Old)"), std::string::npos);
    EXPECT_NE(witness.find("\\\\at(n, Old)"), std::string::npos);
    EXPECT_NE(witness.find("format: \"ext_c_expression\""),
              std::string::npos);
    std::filesystem::remove(output);
}

TEST_F(WitnessTest, SnapshotsLoopEntryValueInGhostVariable) {
    const std::string source =
        benchmark_path("loops/true_ghost_initial.c");
    const std::string output = "test-loop-initial-ghost-witness.yml";
    Engine engine(source);
    ASSERT_EQ(engine.verify(), HOLD);

    WitnessOptions options;
    options.input_file = source;
    options.output_path = output;
    WitnessWriter writer(options);
    ASSERT_TRUE(writer.write(HOLD, *engine.get_module(), nullptr, {},
                             engine.get_loop_certificates()))
        << writer.error();

    const std::string witness = read_file(output);
    EXPECT_NE(witness.find("entry_type: \"ghost_instrumentation\""),
              std::string::npos);
    EXPECT_NE(witness.find("arithexe_loop_0_value_initial"),
              std::string::npos);
    EXPECT_NE(witness.find("value: \"value\""), std::string::npos);
    EXPECT_NE(witness.find("type: \"loop_invariant\""), std::string::npos);
    std::filesystem::remove(output);
}

TEST_F(WitnessTest, WritesLoopInputsInDynamicExecutionOrder) {
    const std::string source = benchmark_path("loops/false_nondet.c");
    const std::string output = "test-loop-violation-witness.yml";
    Engine engine(source);
    ASSERT_EQ(engine.verify(), FAIL);

    WitnessOptions options;
    options.input_file = source;
    options.output_path = output;
    WitnessWriter writer(options);
    ASSERT_TRUE(writer.write(FAIL, *engine.get_module(),
                             engine.get_violation_instruction(),
                             engine.get_counterexample_inputs()))
        << writer.error();

    const std::string witness = read_file(output);
    EXPECT_GE(count_occurrences(witness, "type: \"function_return\""), 2U);
    EXPECT_NE(witness.find("line: 6"), std::string::npos);
    EXPECT_NE(witness.find("line: 13"), std::string::npos);
    EXPECT_NE(witness.find("type: \"target\""), std::string::npos);
    std::filesystem::remove(output);
}

} // namespace

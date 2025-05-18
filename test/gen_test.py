import os

BENCHMARK_DIR = "benchmark/"
OUTPUT_FILE = "test_engine.cpp"

header = '''#include <gtest/gtest.h>
#include <string>
#include "engine.h"
#include "z3++.h"

using namespace ari_exe;
'''

tests = []

for dir_path, _, filenames in os.walk(BENCHMARK_DIR):
    test_class = os.path.basename(dir_path)  # Get the base directory name
    for filename in sorted(filenames):
        if filename.endswith(".c"):
            ground_true = filename.split('_')[0]
            expected_result = "HOLD" if ground_true == "true" else "FAIL"
            test_name = os.path.splitext(filename)[0]
            # Replace invalid characters for test names
            test_name = test_name.replace('-', '_').replace('.', '_')
            file_path = os.path.join(dir_path, filename)
            tests.append(f'''
TEST({test_class.upper()}, {test_name}) {{
    z3::context z3ctx;
    auto engine = Engine("../../test/{file_path}", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::{expected_result}) << "Failed on: {file_path}";
}}
''')

with open(OUTPUT_FILE, "w") as f:
    f.write(header)
    for test in tests:
        f.write(test)
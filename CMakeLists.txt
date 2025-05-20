cmake_minimum_required(VERSION 3.20.0)
project(ArithExecution)

# copy rec_solver to the build directory
file(COPY ${CMAKE_SOURCE_DIR}/rec_solver DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/solver.py DESTINATION ${CMAKE_BINARY_DIR})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# uncomment the following line to enable debug symbols and detect memory errors
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)
find_package(Z3 REQUIRED CONFIG HINTS /opt/homebrew/Cellar/z3)
find_package(Boost REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Found Z3 ${Z3_VERSION_STRING}")
message(STATUS "Using Z3.cmake in: ${Z3_DIR}")
message(STATUS "Using z3++.h in: ${Z3_CXX_INCLUDE_DIRS}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.13.0 # or latest stable tag
)
FetchContent_MakeAvailable(spdlog)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${Z3_CXX_INCLUDE_DIRS})
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/lib")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs core irreader analysis passes)
message(STATUS "${llvm_libs}")

set(ARITHEXEC_LIBS
    AInstruction
    AStack
    engine
    state
    rec_solver
    FunctionSummarizer
    FunctionSummary
    LoopSummarizer
    LoopSummary
    AnalysisManager
    logics
)

add_subdirectory(lib)
add_subdirectory(tools)
add_subdirectory(test)

# copy rec_solver to the build/test directory
file(COPY ${CMAKE_SOURCE_DIR}/rec_solver DESTINATION ${CMAKE_BINARY_DIR}/test)
file(COPY ${CMAKE_SOURCE_DIR}/solver.py DESTINATION ${CMAKE_BINARY_DIR}/test)

### Gtest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
  test_engine
  test_engine.cpp
)

add_executable(
  test_logic
  test_logic.cpp
)

target_link_libraries(
  test_engine
  GTest::gtest_main
  ${ARITHEXEC_LIBS}
  ${Z3_LIBRARIES}
  ${llvm_libs} 
)

target_link_libraries(
  test_logic
  GTest::gtest_main
  logics
  ${Z3_LIBRARIES}
)


include(GoogleTest)
gtest_discover_tests(test_engine)
gtest_discover_tests(test_logic)
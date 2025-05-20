# Find the libraries that correspond to the LLVM components
# that we wish to use

# Link against LLVM libraries


add_executable(
    arith_exe 
    arith_exe.cpp
)

target_link_libraries(
    arith_exe 
    ${ARITHEXEC_LIBS}
    ${Z3_LIBRARIES} 
    ${llvm_libs}
    spdlog::spdlog
)
# add_subdirectory(IfConversion)

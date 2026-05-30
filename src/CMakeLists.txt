add_subdirectory(util)
add_subdirectory(tokenization)
add_subdirectory(parsing)
add_subdirectory(visitors)
add_subdirectory(semantics)

add_executable(mfcc main.cpp)
target_include_directories(mfcc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(
  mfcc
  PRIVATE ${LLVM_DEFINITIONS} MF_CLANG_PATH="${LLVM_TOOLS_BINARY_DIR}/clang"
)
target_link_libraries(mfcc PRIVATE llvm_ir)

add_library(print_visitor STATIC print_visitor.cpp)
target_include_directories(print_visitor PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(print_visitor PUBLIC parse)

add_library(interpreter STATIC interpreter.cpp)
target_include_directories(interpreter PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(interpreter PUBLIC parse semantics)

llvm_map_components_to_libnames(
  LLVM_LIBS
  Analysis
  CodeGen
  Core
  MC
  Passes
  Support
  Target
  TransformUtils
  nativecodegen
)

add_library(llvm_ir STATIC llvm_ir.cpp)
target_include_directories(llvm_ir PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(llvm_ir SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(llvm_ir PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(llvm_ir PUBLIC parse semantics PRIVATE ${LLVM_LIBS})

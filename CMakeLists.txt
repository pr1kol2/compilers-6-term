cmake_minimum_required(VERSION 3.20)

project(MFLang LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_BUILD_DIRECTORY "${PROJECT_SOURCE_DIR}/build")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib/${CMAKE_BUILD_TYPE}")

add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options(
  $<$<CONFIG:Debug>:-fsanitize=undefined>
  $<$<CONFIG:Debug>:-fsanitize=address>
  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Debug>:-g>
  $<$<CONFIG:Release>:-O2>
)

add_link_options(
  $<$<CONFIG:Debug>:-fsanitize=undefined>
  $<$<CONFIG:Debug>:-fsanitize=address>
)

find_program(
  LLVM_CONFIG_EXECUTABLE
  NAMES llvm-config
  PATHS
    /opt/homebrew/opt/llvm/bin
    /opt/homebrew/Cellar/llvm/22.1.1/bin
)

if(LLVM_CONFIG_EXECUTABLE)
  execute_process(
    COMMAND "${LLVM_CONFIG_EXECUTABLE}" --cmakedir
    OUTPUT_VARIABLE LLVM_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  list(APPEND CMAKE_PREFIX_PATH "${LLVM_CMAKE_DIR}")
endif()

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")

add_subdirectory(src)

include(CTest)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

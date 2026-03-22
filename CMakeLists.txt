cmake_minimum_required(VERSION 3.20)

project(MiniML LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_BUILD_DIRECTORY "${PROJECT_SOURCE_DIR}/build")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib")

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

add_subdirectory(src)

include(CTest)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

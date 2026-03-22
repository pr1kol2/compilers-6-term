include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_tests.cpp")

set(UNIT_TEST_MODULE_TARGETS)

foreach(test_source IN LISTS TEST_SOURCES)
  get_filename_component(test_target "${test_source}" NAME_WE)
  string(REGEX REPLACE "_tests$" "" module_target "${test_target}")

  if(NOT TARGET "${module_target}")
    message(FATAL_ERROR "Target ${module_target} is required for ${test_target}.")
  endif()

  get_target_property(module_target_type "${module_target}" TYPE)
  if(NOT module_target_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Target ${module_target} must be a STATIC library for ${test_target}.")
  endif()

  list(APPEND UNIT_TEST_MODULE_TARGETS "${module_target}")

  add_executable("${test_target}" EXCLUDE_FROM_ALL "${test_source}")
  target_link_libraries("${test_target}" PRIVATE GTest::gtest_main "${module_target}")

  gtest_discover_tests(
    "${test_target}"
    TEST_PREFIX "${module_target}."
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "${module_target};unit"
  )
endforeach()

list(REMOVE_DUPLICATES UNIT_TEST_MODULE_TARGETS)

add_executable(unit_tests EXCLUDE_FROM_ALL ${TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE GTest::gtest_main ${UNIT_TEST_MODULE_TARGETS})

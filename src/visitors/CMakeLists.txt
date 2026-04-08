add_library(print_visitor STATIC print_visitor.cpp)
target_include_directories(print_visitor PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(print_visitor PUBLIC parse)

add_library(interpreter STATIC interpreter.cpp)
target_include_directories(interpreter PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(interpreter PUBLIC parse)

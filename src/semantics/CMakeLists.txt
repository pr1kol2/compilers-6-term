add_library(semantics STATIC symbol_table.cpp type_table.cpp)
target_include_directories(semantics PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(semantics PUBLIC parse)

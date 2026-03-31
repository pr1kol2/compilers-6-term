add_library(parse STATIC parse.cpp)
target_include_directories(parse PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(parse PUBLIC tokenize)

add_library(tokenize STATIC tokenize.cpp)
target_include_directories(tokenize PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(tokenize PUBLIC util)

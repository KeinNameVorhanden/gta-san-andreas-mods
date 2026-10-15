# Public mod API headers (header-only target).
add_library(sacore_api INTERFACE)
target_include_directories(sacore_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

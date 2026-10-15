# Streaming memory: built as a SACore mod DLL (C++20, x86).
add_library(streaming_memory SHARED src/streaming_memory.cpp mod.toml)
sacore_target(streaming_memory)
target_link_libraries(streaming_memory PRIVATE sacore_api)

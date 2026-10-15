# Frame rate: built as a SACore mod DLL (C++20, x86).
add_library(frame_rate SHARED src/frame_rate.cpp src/high_fps.cpp src/high_fps.h mod.toml)
sacore_target(frame_rate)
target_link_libraries(frame_rate PRIVATE sacore_api)

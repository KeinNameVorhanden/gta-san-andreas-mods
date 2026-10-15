# Mission pass: built as a SACore mod DLL (C++20, x86).
add_library(mission_pass SHARED src/mission_pass.cpp src/story_runner.cpp src/pass.h mod.toml)
sacore_target(mission_pass)
target_link_libraries(mission_pass PRIVATE sacore_api)

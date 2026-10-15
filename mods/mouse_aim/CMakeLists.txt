# Mouse and aim: built as a SACore mod DLL (C++20, x86).
add_library(mouse_aim SHARED src/mouse_aim.cpp mod.toml)
sacore_target(mouse_aim)
target_link_libraries(mouse_aim PRIVATE sacore_api)

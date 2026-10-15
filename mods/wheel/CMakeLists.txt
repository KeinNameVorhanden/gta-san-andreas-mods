# Weapon and radio wheel: built as a SACore mod DLL (C++20, x86).
add_library(wheel SHARED src/wheel.cpp mod.toml)
sacore_target(wheel)
target_link_libraries(wheel PRIVATE sacore_api imgui_dx9 sacore_gta_ui)

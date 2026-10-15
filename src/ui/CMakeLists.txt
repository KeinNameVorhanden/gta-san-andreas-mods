# GTA-styled ImGui helpers shared by the overlay mods (devtools, QoL mods). Static: every mod DLL has its own copy.
add_library(sacore_gta_ui STATIC gta_ui.cpp include/sacore/gta_ui.h)
target_include_directories(sacore_gta_ui PUBLIC include)
target_link_libraries(sacore_gta_ui PUBLIC imgui_dx9)
sacore_target(sacore_gta_ui)

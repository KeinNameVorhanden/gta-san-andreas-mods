# Density and draw distance: built as a SACore mod DLL (C++20, x86).
add_library(world_density SHARED src/world_density.cpp mod.toml)
sacore_target(world_density)
target_link_libraries(world_density PRIVATE sacore_api)

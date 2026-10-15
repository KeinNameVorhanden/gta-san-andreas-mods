# Vehicle tweaks: built as a SACore mod DLL (C++20, x86).
add_library(vehicle_tweaks SHARED src/vehicle_tweaks.cpp mod.toml)
sacore_target(vehicle_tweaks)
target_link_libraries(vehicle_tweaks PRIVATE sacore_api)

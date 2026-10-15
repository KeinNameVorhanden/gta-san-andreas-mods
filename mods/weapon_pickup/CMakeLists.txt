# Weapon pickup by key: built as a SACore mod DLL (C++20, x86).
add_library(weapon_pickup SHARED src/weapon_pickup.cpp mod.toml)
sacore_target(weapon_pickup)
target_link_libraries(weapon_pickup PRIVATE sacore_api)

# XInput controller: built as a SACore mod DLL (C++20, x86).
add_library(controller SHARED src/controller.cpp mod.toml)
sacore_target(controller)
target_link_libraries(controller PRIVATE sacore_api)

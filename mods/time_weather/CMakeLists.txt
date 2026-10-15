# Time and weather: built as a SACore mod DLL (C++20, x86).
add_library(time_weather SHARED src/time_weather.cpp mod.toml)
sacore_target(time_weather)
target_link_libraries(time_weather PRIVATE sacore_api)

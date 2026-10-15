add_library(SACoreProbe SHARED probe.cpp game_state.cpp events.cpp probe.h)
sacore_asi(SACoreProbe)
target_link_libraries(SACoreProbe PRIVATE minhook bcrypt psapi ole32)

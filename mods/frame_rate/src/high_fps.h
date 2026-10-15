// High frame rate fixes: physics and timing patches for frame rates above 30 FPS (high_fps.cpp).
#pragma once

#include <sacore/sacore.hpp>

namespace frame_rate {

// Hooks CTimer::Update and applies the fixes when the settings allow it; call once from sacore_mod_main.
void InitHighFps(sacore::mod::Host& host);
// Applies or removes all fixes (live setting).
void SetHighFps(bool on);

}  // namespace frame_rate

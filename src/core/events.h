// Events published on the core event bus (game thread). Only events the game really provides.
#pragma once

#include <cstdint>

struct IDirect3DDevice9;
struct HWND__;

namespace sacore::events {

struct DeviceCreated {
    IDirect3DDevice9* device;
    HWND__* window;
};
struct DeviceReset {
    IDirect3DDevice9* device;
    bool beforeReset;  // true: about to reset (release resources), false: after reset
    bool succeeded;
};
struct FramePresented {
    IDirect3DDevice9* device;
    uint64_t frame;
};
struct GameWindowDestroyed {};

struct GameTick {};  // once per game logic step, after CGame::Process

struct GameStateChanged {
    int from;
    int to;
};
struct SaveStarted {};
struct SaveCompleted {
    bool success;
};
struct LoadStarted {};
struct LoadCompleted {
    bool success;
};

}  // namespace sacore::events

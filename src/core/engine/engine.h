// Build-independent engine layer: Direct3D 9 device hooks, game window handling, audio session.
// Publishes DeviceCreated, DeviceReset, FramePresented and GameWindowDestroyed on the event bus.
#pragma once

#include "event_bus.h"
#include "logger.h"
#include "patch_manager.h"

#include <windows.h>
#include <d3d9.h>

#include <functional>
#include <string>
#include <vector>

namespace sacore {

struct EngineSettings {
    bool windowed = true;
    int clientWidth = 0;         // 0: window client = game resolution; otherwise this width, game aspect ratio
    bool runInBackground = true;
    bool startMuted = false;
    bool focusOnStart = true;    // false: never activate the window from SACore code
};

class Engine {
public:
    // Returns true if it handled the message; `result` is then returned from the window procedure.
    using MessageFilter = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT& result)>;

    Engine(Logger& logger, PatchManager& patches, EventBus& bus);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Called once the build is known (or not). `allowGameFeatures` false keeps the engine passive.
    void Configure(const EngineSettings& settings, bool allowFeatures);
    // Called with every IDirect3D9 the game creates.
    void OnDirect3DCreated(IDirect3D9* d3d);

    // Filters run before the game's window procedure, in insertion order. Removal is safe inside a filter.
    void AddMessageFilter(std::string owner, MessageFilter filter);
    void RemoveMessageFilters(const std::string& owner);
    HWND GameWindow() const { return window_; }
    IDirect3DDevice9* Device() const { return device_; }
    uint64_t FrameCount() const { return frames_; }
    bool WindowedActive() const { return windowedActive_; }
    bool CloseRequested() const { return closeRequested_; }
    // Average and maximum time spent in SACore's Present work (event handlers included), microseconds.
    double AverageFrameOverheadUs() const;
    double MaxFrameOverheadUs() const { return maxOverheadUs_; }
    uint64_t BlockedMouseGrabs() const { return blockedMouseGrabs_; }
    uint64_t BlockedKeyReads() const { return blockedKeyReads_; }
    uint64_t BlockedMenuActivations() const { return blockedMenuActivations_; }

private:
    static HRESULT STDMETHODCALLTYPE HookedCreateDevice(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                       D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    static HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    static HRESULT STDMETHODCALLTYPE HookedPresent(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                                   const RGNDATA*);
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static BOOL WINAPI HookedClipCursor(const RECT* rect);
    static BOOL WINAPI HookedSetCursorPos(int x, int y);
    static HWND WINAPI HookedSetCapture(HWND window);
    static SHORT WINAPI HookedGetKeyState(int key);
    static SHORT WINAPI HookedGetAsyncKeyState(int key);
    void InstallKeyboardGuards();
    static LONG WINAPI HookedSetWindowLongA(HWND window, int index, LONG value);
    static BOOL WINAPI HookedSetWindowPos(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags);
    void InstallWindowGuards();
    bool MouseGrabAllowed() const;
    void InstallMouseGuards();

    void ApplyWindowStyle();
    void MuteAudioSession();
    void HookDevice(IDirect3DDevice9* device);

    Logger& logger_;
    PatchManager& patches_;
    EventBus& bus_;
    EngineSettings settings_;
    bool active_ = false;
    bool windowedActive_ = false;
    HWND window_ = nullptr;
    WNDPROC originalWndProc_ = nullptr;
    IDirect3DDevice9* device_ = nullptr;
    UINT backBufferWidth_ = 0;
    UINT backBufferHeight_ = 0;
    uint64_t frames_ = 0;
    ULONGLONG lastFpsTick_ = 0;
    uint64_t lastFpsFrames_ = 0;
    struct OwnedFilter {
        std::string owner;
        MessageFilter filter;
        bool removed = false;
    };
    std::vector<OwnedFilter> filters_;
    int filterDepth_ = 0;
    double totalOverheadUs_ = 0;
    double maxOverheadUs_ = 0;
    uint64_t measuredFrames_ = 0;
    int slowFrameReports_ = 0;
    bool minimized_ = false;
    bool closeRequested_ = false;
    uint64_t blockedMouseGrabs_ = 0;
    uint64_t blockedKeyReads_ = 0;
    uint64_t blockedMenuActivations_ = 0;
};

}  // namespace sacore

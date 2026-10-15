#include "engine.h"

#include "events.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <format>
#include <initializer_list>

namespace sacore {
namespace {

using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                                   IDirect3DDevice9**);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

// COM vtable slots in d3d9.h declaration order.
constexpr size_t kCreateDeviceSlot = 16;
constexpr size_t kResetSlot = 16;
constexpr size_t kPresentSlot = 17;
constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
constexpr ULONGLONG kFpsLogIntervalMs = 60'000;
// Frames whose SACore work exceeds this are logged with their number (spike attribution), at most 50 times.
constexpr double kSlowFrameUs = 4000.0;
constexpr int kMaxSlowFrameReports = 50;

// The hooks are plain functions; they reach the single engine instance through this pointer.
Engine* g_engine = nullptr;
CreateDeviceFn g_origCreateDevice = nullptr;
ResetFn g_origReset = nullptr;
PresentFn g_origPresent = nullptr;
BOOL(WINAPI* g_origClipCursor)(const RECT*) = nullptr;
BOOL(WINAPI* g_origSetCursorPos)(int, int) = nullptr;
HWND(WINAPI* g_origSetCapture)(HWND) = nullptr;
SHORT(WINAPI* g_origGetKeyState)(int) = nullptr;
SHORT(WINAPI* g_origGetAsyncKeyState)(int) = nullptr;
LONG(WINAPI* g_origSetWindowLongA)(HWND, int, LONG) = nullptr;
BOOL(WINAPI* g_origSetWindowPos)(HWND, HWND, int, int, int, int, UINT) = nullptr;

void* VtableEntry(void* object, size_t slot) {
    return (*static_cast<void***>(object))[slot];
}

}  // namespace

Engine::Engine(Logger& logger, PatchManager& patches, EventBus& bus) : logger_(logger), patches_(patches), bus_(bus) {
    g_engine = this;
}

Engine::~Engine() {
    g_engine = nullptr;
}

void Engine::Configure(const EngineSettings& settings, bool allowFeatures) {
    settings_ = settings;
    active_ = allowFeatures;
    const bool windowedPlugin = GetModuleHandleW(L"III.VC.SA.WindowedMode.asi") != nullptr;
    windowedActive_ = active_ && settings_.windowed && !windowedPlugin;
    logger_.Logf(LogLevel::Info, LogCategory::Runtime, "engine",
                 "windowed {} (setting {}{}), run in background {}, start muted {}", windowedActive_ ? "on" : "off",
                 settings_.windowed ? "on" : "off",
                 windowedPlugin ? ", III.VC.SA.WindowedMode.asi present and owns windowing" : "",
                 windowedActive_ && settings_.runInBackground ? "on" : "off", settings_.startMuted ? "on" : "off");
    if (active_ && settings_.startMuted) {
        MuteAudioSession();
    }
    if (windowedActive_ && settings_.runInBackground) {
        InstallMouseGuards();
        InstallKeyboardGuards();
    }
    if (windowedActive_) {
        InstallWindowGuards();
    }
}

// After device creation the game switches its window to WS_POPUP (and can move or size it on a resolution change).
// Repairing that afterwards made the frame disappear and reappear at every start, so in windowed mode SACore owns
// the window style and geometry: the game's own changes to its window are absorbed before they take effect.
void Engine::InstallWindowGuards() {
    patches_.InstallImportHook({"engine.window_style_guard", "core", "user32!SetWindowLongA (game imports)",
                                "keep the windowed frame instead of the game's WS_POPUP style",
                                "style and extended style of the game window stay SACore's windowed style",
                                "the frame flickers off and on after device creation (repaired on the next frame)"},
                               "USER32.dll", "SetWindowLongA", reinterpret_cast<void*>(&HookedSetWindowLongA),
                               reinterpret_cast<void**>(&g_origSetWindowLongA));
    patches_.InstallImportHook({"engine.window_geometry_guard", "core", "user32!SetWindowPos (game imports)",
                                "window size and position follow SACore's windowed layout",
                                "the game's SetWindowPos on its window keeps z-order and show flags only",
                                "the game may resize the window to its fullscreen layout"},
                               "USER32.dll", "SetWindowPos", reinterpret_cast<void*>(&HookedSetWindowPos),
                               reinterpret_cast<void**>(&g_origSetWindowPos));
}

LONG WINAPI Engine::HookedSetWindowLongA(HWND window, int index, LONG value) {
    Engine* self = g_engine;
    if (self && self->windowedActive_ && window && window == self->window_ && self->backBufferWidth_ != 0) {
        if (index == GWL_STYLE) {
            value = static_cast<LONG>(kWindowStyle);
        } else if (index == GWL_EXSTYLE) {
            value = WS_EX_APPWINDOW;
        }
    }
    return g_origSetWindowLongA(window, index, value);
}

BOOL WINAPI Engine::HookedSetWindowPos(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags) {
    Engine* self = g_engine;
    if (self && self->windowedActive_ && window && window == self->window_ && self->backBufferWidth_ != 0) {
        flags |= SWP_NOMOVE | SWP_NOSIZE;
        if (!self->settings_.focusOnStart) {
            flags |= SWP_NOACTIVATE;
        }
    }
    return g_origSetWindowPos(window, after, x, y, cx, cy, flags);
}

// The game keeps running in the background (run in background), and it clips and recentres the cursor every
// frame. Those calls act system-wide, so while the game window is not in front they are skipped: the game only
// takes the mouse when the player clicks into it.
void Engine::InstallMouseGuards() {
    patches_.InstallImportHook({"engine.mouse_guard_clip", "core", "user32!ClipCursor (game imports)",
                                "do not trap the mouse while the window is in the background",
                                "ClipCursor only takes effect while the game window is in front",
                                "the game traps the mouse even when another window is active"},
                               "USER32.dll", "ClipCursor", reinterpret_cast<void*>(&HookedClipCursor),
                               reinterpret_cast<void**>(&g_origClipCursor));
    patches_.InstallImportHook({"engine.mouse_guard_position", "core", "user32!SetCursorPos (game imports)",
                                "do not move the mouse while the window is in the background",
                                "SetCursorPos only takes effect while the game window is in front",
                                "the game recentres the mouse even when another window is active"},
                               "USER32.dll", "SetCursorPos", reinterpret_cast<void*>(&HookedSetCursorPos),
                               reinterpret_cast<void**>(&g_origSetCursorPos));
    patches_.InstallImportHook({"engine.mouse_guard_capture", "core", "user32!SetCapture (game imports)",
                                "do not capture the mouse while the window is in the background",
                                "SetCapture only takes effect while the game window is in front",
                                "the game captures mouse input even when another window is active"},
                               "USER32.dll", "SetCapture", reinterpret_cast<void*>(&HookedSetCapture),
                               reinterpret_cast<void**>(&g_origSetCapture));
}

// The game polls the Shift keys with GetKeyState every frame (newsteam FUN_0077bc10, the left/right Shift split the
// window messages cannot give) and 1.0 US also uses GetAsyncKeyState. Those report the physical keyboard even while
// another window is in front, so with run-in-background the player character reacted to Shift typed elsewhere (R23).
// While the game window is not in front, the game reads every key as released.
void Engine::InstallKeyboardGuards() {
    patches_.InstallImportHook({"engine.key_guard_state", "core", "user32!GetKeyState (game imports)",
                                "no keyboard input while the window is in the background",
                                "GetKeyState reports keys as released while the game window is not in front",
                                "the game reacts to Shift typed in other windows"},
                               "USER32.dll", "GetKeyState", reinterpret_cast<void*>(&HookedGetKeyState),
                               reinterpret_cast<void**>(&g_origGetKeyState));
    if (PatchManager::GameImports("USER32.dll", "GetAsyncKeyState")) {  // 1.0 US only
        patches_.InstallImportHook({"engine.key_guard_async", "core", "user32!GetAsyncKeyState (game imports)",
                                    "no keyboard input while the window is in the background",
                                    "GetAsyncKeyState reports keys as released while the game window is not in front",
                                    "the game reacts to keys typed in other windows"},
                                   "USER32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&HookedGetAsyncKeyState),
                                   reinterpret_cast<void**>(&g_origGetAsyncKeyState));
    }
}

SHORT WINAPI Engine::HookedGetKeyState(int key) {
    if (g_engine && !g_engine->MouseGrabAllowed()) {
        if (g_engine->blockedKeyReads_++ == 0) {
            g_engine->logger_.Log(LogLevel::Info, LogCategory::Runtime, "engine",
                                  "keyboard read while in the background answered as 'not pressed'");
        }
        return 0;
    }
    return g_origGetKeyState(key);
}

SHORT WINAPI Engine::HookedGetAsyncKeyState(int key) {
    if (g_engine && !g_engine->MouseGrabAllowed()) {
        if (g_engine->blockedKeyReads_++ == 0) {
            g_engine->logger_.Log(LogLevel::Info, LogCategory::Runtime, "engine",
                                  "keyboard read while in the background answered as 'not pressed'");
        }
        return 0;
    }
    return g_origGetAsyncKeyState(key);
}

bool Engine::MouseGrabAllowed() const {
    return !window_ || GetForegroundWindow() == window_;
}

BOOL WINAPI Engine::HookedClipCursor(const RECT* rect) {
    if (rect && g_engine && !g_engine->MouseGrabAllowed()) {
        ++g_engine->blockedMouseGrabs_;
        return TRUE;
    }
    return g_origClipCursor(rect);
}

BOOL WINAPI Engine::HookedSetCursorPos(int x, int y) {
    if (g_engine && !g_engine->MouseGrabAllowed()) {
        ++g_engine->blockedMouseGrabs_;
        return TRUE;
    }
    return g_origSetCursorPos(x, y);
}

HWND WINAPI Engine::HookedSetCapture(HWND window) {
    if (g_engine && !g_engine->MouseGrabAllowed()) {
        ++g_engine->blockedMouseGrabs_;
        return nullptr;
    }
    return g_origSetCapture(window);
}

void Engine::OnDirect3DCreated(IDirect3D9* d3d) {
    if (!active_ || !d3d || g_origCreateDevice) {
        return;
    }
    patches_.InstallHook({"engine.create_device", "core", "IDirect3D9::CreateDevice",
                          "observe device creation; force windowed presentation when enabled",
                          "device created as usual, windowed if [window] windowed = true",
                          "no windowed mode, no frame events, mods get no shutdown call"},
                         VtableEntry(d3d, kCreateDeviceSlot), reinterpret_cast<void*>(&HookedCreateDevice),
                         reinterpret_cast<void**>(&g_origCreateDevice));
}

HRESULT STDMETHODCALLTYPE Engine::HookedCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                                                     DWORD flags, D3DPRESENT_PARAMETERS* params,
                                                     IDirect3DDevice9** device) {
    Engine& self = *g_engine;
    const HWND window = params->hDeviceWindow ? params->hDeviceWindow : focus;
    self.logger_.Logf(LogLevel::Info, LogCategory::Runtime, "engine",
                      "CreateDevice: adapter {}, {}x{}, format {}, windowed {}", adapter, params->BackBufferWidth,
                      params->BackBufferHeight, static_cast<int>(params->BackBufferFormat), params->Windowed);
    if (self.windowedActive_) {
        params->Windowed = TRUE;
        params->FullScreen_RefreshRateInHz = 0;
        self.window_ = window;
        self.backBufferWidth_ = params->BackBufferWidth;
        self.backBufferHeight_ = params->BackBufferHeight;
        self.ApplyWindowStyle();
    }
    const HRESULT result = g_origCreateDevice(d3d, adapter, type, focus, flags, params, device);
    if (FAILED(result)) {
        self.logger_.Logf(LogLevel::Error, LogCategory::Runtime, "engine", "CreateDevice failed: {:#010x}",
                          static_cast<unsigned>(result));
        return result;
    }
    self.window_ = window;
    if (window && !self.originalWndProc_) {
        // The game registers an ANSI window class; the ANSI functions keep its messages unconverted.
        self.originalWndProc_ = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
    }
    if (device && *device && !self.device_) {
        self.device_ = *device;
        self.HookDevice(*device);
        self.bus_.Publish(events::DeviceCreated{*device, window});
    }
    return result;
}

void Engine::HookDevice(IDirect3DDevice9* device) {
    patches_.InstallHook({"engine.present", "core", "IDirect3DDevice9::Present", "per-frame events and window upkeep",
                          "FramePresented event once per frame", "no frame events (features that need them stay idle)"},
                         VtableEntry(device, kPresentSlot), reinterpret_cast<void*>(&HookedPresent),
                         reinterpret_cast<void**>(&g_origPresent));
    patches_.InstallHook({"engine.reset", "core", "IDirect3DDevice9::Reset",
                          "keep windowed mode across resets; let overlays release resources",
                          "DeviceReset events before and after every reset",
                          "windowed mode may be lost after resolution changes"},
                         VtableEntry(device, kResetSlot), reinterpret_cast<void*>(&HookedReset),
                         reinterpret_cast<void**>(&g_origReset));
}

HRESULT STDMETHODCALLTYPE Engine::HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    Engine& self = *g_engine;
    if (self.windowedActive_) {
        params->Windowed = TRUE;
        params->FullScreen_RefreshRateInHz = 0;
    }
    self.bus_.Publish(events::DeviceReset{device, true, true});
    const HRESULT result = g_origReset(device, params);
    self.logger_.Logf(SUCCEEDED(result) ? LogLevel::Debug : LogLevel::Error, LogCategory::Runtime, "engine",
                      "Reset {}x{}: {:#010x}", params->BackBufferWidth, params->BackBufferHeight,
                      static_cast<unsigned>(result));
    if (SUCCEEDED(result) && self.windowedActive_ && params->BackBufferWidth != 0 &&
        (params->BackBufferWidth != self.backBufferWidth_ || params->BackBufferHeight != self.backBufferHeight_)) {
        // Resolution changed in the game's display settings: the window follows it.
        self.backBufferWidth_ = params->BackBufferWidth;
        self.backBufferHeight_ = params->BackBufferHeight;
        self.ApplyWindowStyle();
    }
    self.bus_.Publish(events::DeviceReset{device, false, SUCCEEDED(result)});
    return result;
}

HRESULT STDMETHODCALLTYPE Engine::HookedPresent(IDirect3DDevice9* device, const RECT* source, const RECT* dest,
                                                HWND window, const RGNDATA* dirty) {
    Engine& self = *g_engine;
    LARGE_INTEGER begin;
    QueryPerformanceCounter(&begin);
    ++self.frames_;
    // The game's WinMain resets the window to WS_POPUP after device creation; restore the frame.
    if (self.windowedActive_ && self.window_ &&
        (static_cast<DWORD>(GetWindowLongW(self.window_, GWL_STYLE)) & WS_CAPTION) != WS_CAPTION) {
        self.ApplyWindowStyle();
    }
    const ULONGLONG now = GetTickCount64();
    if (self.lastFpsTick_ == 0) {
        self.lastFpsTick_ = now;
        self.logger_.Log(LogLevel::Info, LogCategory::Runtime, "engine", "first frame presented");
    } else if (now - self.lastFpsTick_ >= kFpsLogIntervalMs) {
        const double seconds = static_cast<double>(now - self.lastFpsTick_) / 1000.0;
        self.logger_.Logf(LogLevel::Debug, LogCategory::Diagnostics, "engine", "{:.1f} fps over {:.0f} s",
                          static_cast<double>(self.frames_ - self.lastFpsFrames_) / seconds, seconds);
        self.lastFpsTick_ = now;
        self.lastFpsFrames_ = self.frames_;
    }
    self.bus_.Publish(events::FramePresented{device, self.frames_});
    LARGE_INTEGER end;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    const double us = static_cast<double>(end.QuadPart - begin.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart);
    self.totalOverheadUs_ += us;
    self.maxOverheadUs_ = std::max(self.maxOverheadUs_, us);
    ++self.measuredFrames_;
    if (us > kSlowFrameUs && ++self.slowFrameReports_ <= kMaxSlowFrameReports) {
        self.logger_.Logf(LogLevel::Info, LogCategory::Diagnostics, "engine", "slow SACore frame work: {:.1f} ms in frame {}",
                          us / 1000.0, self.frames_);
    }
    return g_origPresent(device, source, dest, window, dirty);
}

LRESULT CALLBACK Engine::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Engine& self = *g_engine;
    ++self.filterDepth_;
    const size_t count = self.filters_.size();
    for (size_t i = 0; i < count; ++i) {
        if (self.filters_[i].removed) {
            continue;
        }
        const MessageFilter filter = self.filters_[i].filter;  // copy: the vector may change inside the call
        LRESULT result = 0;
        if (filter(window, message, wParam, lParam, result)) {
            --self.filterDepth_;
            return result;
        }
    }
    if (--self.filterDepth_ == 0) {
        std::erase_if(self.filters_, [](const OwnedFilter& f) { return f.removed; });
    }
    if (message == WM_DESTROY) {
        self.bus_.Publish(events::GameWindowDestroyed{});
    }
    // The game presents no frames while minimized (frame events and the test harness pause with it); log it so a
    // gap in the log has a visible cause. WM_SYSCOMMAND comes from the user (button, system menu, Win+Down).
    // F10 or a bare Alt press asks DefWindowProc for keyboard menu mode (SC_KEYMENU with no character). The windowed
    // frame has a system menu, so Windows entered a modal menu loop and the game froze until F10 was pressed again
    // (R22; exclusive fullscreen has no system menu). Alt+Space (character ' ') still opens the window menu.
    if (self.windowedActive_ && message == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_KEYMENU && lParam == 0) {
        if (self.blockedMenuActivations_++ == 0) {
            self.logger_.Log(LogLevel::Info, LogCategory::Runtime, "engine",
                             "keyboard menu activation (F10 / Alt) ignored: it would freeze the game in a menu loop");
        }
        return 0;
    }
    if (message == WM_CLOSE || (message == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_CLOSE)) {
        self.closeRequested_ = true;  // crash reports say whether the game was shutting down
    }
    if (message == WM_SYSCOMMAND && ((wParam & 0xFFF0) == SC_MINIMIZE || (wParam & 0xFFF0) == SC_CLOSE)) {
        self.logger_.Logf(LogLevel::Info, LogCategory::Runtime, "engine", "{} requested by the user",
                          (wParam & 0xFFF0) == SC_CLOSE ? "close" : "minimize");
    } else if (message == WM_SIZE && (wParam == SIZE_MINIMIZED) != self.minimized_) {
        self.minimized_ = wParam == SIZE_MINIMIZED;
        self.logger_.Logf(LogLevel::Info, LogCategory::Runtime, "engine", "game window {} after frame {}",
                          self.minimized_ ? "minimized: no frames until it is restored" : "restored", self.frames_);
    }
    if (self.windowedActive_ && self.settings_.runInBackground) {
        // The game sleeps while its foreground flag is 0; WM_ACTIVATE(WA_INACTIVE) and WM_KILLFOCUS clear it
        // (window procedure decompile, docs/ADDRESSES.md H5). Those go to DefWindowProc instead.
        const bool deactivate =
            (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || message == WM_KILLFOCUS;
        if (deactivate) {
            ClipCursor(nullptr);
            return DefWindowProcA(window, message, wParam, lParam);
        }
        // WM_SETFOCUS opens the pause menu (1.0: window procedure case 7 calls 0x53BC60, "menu next frame") and sets
        // the foreground flag, which never dropped here. A game that kept running in the background paused the moment
        // it got the focus back: a click into the window, or Windows passing the focus on when another window
        // closes (test copies paused each other at night when nothing else held the focus).
        if (message == WM_SETFOCUS) {
            return DefWindowProcA(window, message, wParam, lParam);
        }
        if (message == WM_ACTIVATEAPP && wParam == FALSE) {
            wParam = TRUE;
        }
    }
    return CallWindowProcA(self.originalWndProc_, window, message, wParam, lParam);
}

void Engine::AddMessageFilter(std::string owner, MessageFilter filter) {
    filters_.push_back(OwnedFilter{std::move(owner), std::move(filter)});
}

void Engine::RemoveMessageFilters(const std::string& owner) {
    for (OwnedFilter& f : filters_) {
        if (f.owner == owner) {
            f.removed = true;
        }
    }
    if (filterDepth_ == 0) {
        std::erase_if(filters_, [](const OwnedFilter& f) { return f.removed; });
    }
}

double Engine::AverageFrameOverheadUs() const {
    return measuredFrames_ ? totalOverheadUs_ / static_cast<double>(measuredFrames_) : 0.0;
}

void Engine::ApplyWindowStyle() {
    if (!window_ || backBufferWidth_ == 0 || backBufferHeight_ == 0) {
        return;
    }
    SetWindowLongW(window_, GWL_STYLE, static_cast<LONG>(kWindowStyle));  // W variant: not guarded
    SetWindowLongW(window_, GWL_EXSTYLE, WS_EX_APPWINDOW);
    // Client size: the game resolution (sharp, 1:1) unless [window] client_width asks for another width.
    const int gameWidth = static_cast<int>(backBufferWidth_);
    const int gameHeight = static_cast<int>(backBufferHeight_);
    int clientWidth = settings_.clientWidth > 0 ? std::max(settings_.clientWidth, 320) : gameWidth;
    int clientHeight = MulDiv(clientWidth, gameHeight, gameWidth);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT& work = monitor.rcWork;
    RECT frame{0, 0, clientWidth, clientHeight};
    AdjustWindowRectEx(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW);
    const int borderWidth = (frame.right - frame.left) - clientWidth;
    const int borderHeight = (frame.bottom - frame.top) - clientHeight;
    const int maxClientWidth = (work.right - work.left) - borderWidth;
    const int maxClientHeight = (work.bottom - work.top) - borderHeight;
    if (clientWidth > maxClientWidth || clientHeight > maxClientHeight) {
        // Larger than the monitor's work area (for example the full monitor resolution plus the frame): shrink
        // to fit, keeping the aspect ratio. The image is then scaled down slightly.
        const double scale = std::min(static_cast<double>(maxClientWidth) / clientWidth,
                                      static_cast<double>(maxClientHeight) / clientHeight);
        clientWidth = static_cast<int>(clientWidth * scale);
        clientHeight = static_cast<int>(clientHeight * scale);
    }
    const int width = clientWidth + borderWidth;
    const int height = clientHeight + borderHeight;
    const UINT activation = settings_.focusOnStart ? 0 : SWP_NOACTIVATE;
    // Through the original function: the geometry guard would otherwise swallow SACore's own size change.
    auto setWindowPos = g_origSetWindowPos ? g_origSetWindowPos : &::SetWindowPos;
    setWindowPos(window_, HWND_NOTOPMOST, work.left + ((work.right - work.left) - width) / 2,
                 work.top + ((work.bottom - work.top) - height) / 2, width, height,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | activation);
    logger_.Logf(LogLevel::Info, LogCategory::Runtime, "engine", "window client {}x{} for game resolution {}x{}",
                 clientWidth, clientHeight, gameWidth, gameHeight);
}

void Engine::MuteAudioSession() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* endpoint = nullptr;
    IAudioSessionManager* sessions = nullptr;
    ISimpleAudioVolume* volume = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
    }
    if (SUCCEEDED(hr)) {
        hr = endpoint->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&sessions));
    }
    if (SUCCEEDED(hr)) {
        hr = sessions->GetSimpleAudioVolume(nullptr, FALSE, &volume);
    }
    if (SUCCEEDED(hr)) {
        hr = volume->SetMute(TRUE, nullptr);
    }
    logger_.Logf(SUCCEEDED(hr) ? LogLevel::Info : LogLevel::Warn, LogCategory::Runtime, "engine",
                 "start muted: {} (hr {:#010x}); unmute in the Windows volume mixer",
                 SUCCEEDED(hr) ? "audio session muted" : "FAILED", static_cast<unsigned>(hr));
    for (IUnknown* object : std::initializer_list<IUnknown*>{volume, sessions, endpoint, enumerator}) {
        if (object) {
            object->Release();
        }
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
}

}  // namespace sacore

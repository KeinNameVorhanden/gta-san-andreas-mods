// Early probe: proves that the ASI loader loads our plugin, identifies the game build,
// observes rendering through Direct3D 9, offers a build-independent windowed mode and logs game state.
// Game memory writes are limited to the documented patches in docs/ADDRESSES.md.
// Superseded by the core runtime.

#include "probe.h"

#include <windows.h>
#include <audiopolicy.h>
#include <bcrypt.h>
#include <mmdeviceapi.h>
#include <d3d9.h>
#include <psapi.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace probe {
// Log state, owned by this file (opened in OpenLog, closed in Detach).
std::mutex g_logMutex;
FILE* g_log = nullptr;
ULONGLONG g_startTick = 0;

void Log(std::string_view message) {
    std::lock_guard lock(g_logMutex);
    if (!g_log) {
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::fprintf(g_log, "%02u:%02u:%02u.%03u [+%llums] %.*s\n", now.wHour, now.wMinute, now.wSecond,
                 now.wMilliseconds, GetTickCount64() - g_startTick, static_cast<int>(message.size()),
                 message.data());
    std::fflush(g_log);
}

uintptr_t ModuleBase() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

std::span<const unsigned char> MainTextSection() {
    const auto* base = reinterpret_cast<const unsigned char*>(ModuleBase());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 6) == 0) {
            return {base + section->VirtualAddress, section->Misc.VirtualSize};
        }
    }
    return {};
}

size_t CountCodeReferences(uintptr_t address) {
    unsigned char needle[4];
    std::memcpy(needle, &address, 4);
    const std::span<const unsigned char> text = MainTextSection();
    size_t count = 0;
    for (auto it = text.begin(); (it = std::search(it, text.end(), std::begin(needle), std::end(needle))) != text.end();
         ++it) {
        ++count;
    }
    return count;
}

bool SafeRead(uintptr_t address, void* out, size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace probe

namespace {

using probe::g_log;
using probe::g_logMutex;
using probe::g_startTick;
using probe::KnownBuild;
using probe::Log;
using probe::Logf;
using probe::MainTextSection;

// Same hashes as tools/data/known_builds.json; addresses documented in docs/ADDRESSES.md.
// Prologue signatures cover only bytes before the first relocated operand.
constexpr char kSteamSavePrologue[] = "\x55\x8B\xEC\x81\xEC\x38\x01\x00\x00";
constexpr char kSteamLoadPrologue[] = "\x55\x8B\xEC\x8B\x45\x08\x81\xEC\x7C\x02\x00\x00";
constexpr char kUs10SavePrologue[] = "\x81\xEC\x38\x01\x00\x00";
constexpr char kUs10LoadPrologue[] = "\x8B\x44\x24\x04\x81\xEC\x7C\x02\x00\x00";

constexpr KnownBuild kKnownBuilds[] = {
    {"newsteam_r2_lv", "aa07fcdc8723a2831f77a35491dea6cdd58f6733e3ac69a10ddf5cdf95034fe3", 0x919020, 0x833938,
     0x8100D0, 0x7FFBE4, 0x1ECD50, kSteamSavePrologue, sizeof(kSteamSavePrologue) - 1, 0x1ED120,
     kSteamLoadPrologue, sizeof(kSteamLoadPrologue) - 1},
    {"us10_hoodlum", "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26", 0x88D4C0, 0x7A6748,
     0x77CD98, 0x772914, 0x1D13E0, kUs10SavePrologue, sizeof(kUs10SavePrologue) - 1, 0x1D17B0,
     kUs10LoadPrologue, sizeof(kUs10LoadPrologue) - 1},
};

// Game states used by the WinMain loop (names from plugin-sdk / gta-reversed for 1.0 US).
constexpr int kGameStateStartUp = 0;
constexpr int kGameStateInitOnce = 5;
constexpr int kGameStateLogoMpeg = 2;

// COM vtable slots (d3d9.h declaration order).
constexpr size_t kD3D9CreateDeviceSlot = 16;
constexpr size_t kDeviceResetSlot = 16;
constexpr size_t kDevicePresentSlot = 17;
constexpr ULONGLONG kFpsReportIntervalMs = 10'000;
constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;

struct Settings {
    bool windowed = true;
    int windowClientWidth = 1280;
    bool skipIntroMovies = true;
    bool autoStartNewGame = false;  // test automation only
    bool runInBackground = true;    // windowed mode only
    bool startMuted = false;
    int giveMoneyOnStart = 0;       // W1, test only
};

HMODULE g_self = nullptr;
std::filesystem::path g_gameDir;
Settings g_settings;
const KnownBuild* g_build = nullptr;
bool g_buildKnown = false;
bool g_windowedActive = false;
bool g_initialized = false;
HWND g_gameWindow = nullptr;
UINT g_backBufferWidth = 0;
UINT g_backBufferHeight = 0;

std::atomic<unsigned long long> g_frames{0};
ULONGLONG g_lastReportTick = 0;
unsigned long long g_lastReportFrames = 0;

using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

Direct3DCreate9Fn g_origDirect3DCreate9 = nullptr;
CreateDeviceFn g_origCreateDevice = nullptr;
ResetFn g_origReset = nullptr;
PresentFn g_origPresent = nullptr;

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::filesystem::path ModulePath(HMODULE module) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

// SHA-256 of a file via Windows CNG; empty string on failure.
std::string Sha256OfFile(const std::filesystem::path& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
        return {};
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<unsigned char> chunk(1 << 20);
        size_t read = 0;
        bool ok = true;
        while (ok && (read = std::fread(chunk.data(), 1, chunk.size(), file)) > 0) {
            ok = BCRYPT_SUCCESS(BCryptHashData(hash, chunk.data(), static_cast<ULONG>(read), 0));
        }
        unsigned char digest[32];
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            for (unsigned char byte : digest) {
                result += std::format("{:02x}", byte);
            }
        }
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    std::fclose(file);
    return result;
}

Settings LoadSettings() {
    std::filesystem::path ini = ModulePath(g_self);
    ini.replace_extension(L".ini");
    Settings settings;
    settings.windowed = GetPrivateProfileIntW(L"Probe", L"Windowed", 1, ini.c_str()) != 0;
    const int width = static_cast<int>(GetPrivateProfileIntW(L"Probe", L"WindowClientWidth", 1280, ini.c_str()));
    settings.windowClientWidth = width >= 320 && width <= 7680 ? width : 1280;
    settings.skipIntroMovies = GetPrivateProfileIntW(L"Probe", L"SkipIntroMovies", 1, ini.c_str()) != 0;
    settings.autoStartNewGame = GetPrivateProfileIntW(L"Probe", L"AutoStartNewGame", 0, ini.c_str()) != 0;
    settings.runInBackground = GetPrivateProfileIntW(L"Probe", L"RunInBackground", 1, ini.c_str()) != 0;
    settings.startMuted = GetPrivateProfileIntW(L"Probe", L"StartMuted", 0, ini.c_str()) != 0;
    const int money = static_cast<int>(GetPrivateProfileIntW(L"Probe", L"GiveMoneyOnStart", 0, ini.c_str()));
    settings.giveMoneyOnStart = money >= 0 && money <= 1'000'000 ? money : 0;
    return settings;
}

// Start muted: mutes this process's default audio session on the default render device, exactly like the
// mute button in the Windows volume mixer (which can unmute it again). Needs no game addresses.
void MuteProcessAudio() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioSessionManager* sessions = nullptr;
    ISimpleAudioVolume* volume = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if (SUCCEEDED(hr)) {
        hr = device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&sessions));
    }
    if (SUCCEEDED(hr)) {
        hr = sessions->GetSimpleAudioVolume(nullptr, FALSE, &volume);
    }
    if (SUCCEEDED(hr)) {
        hr = volume->SetMute(TRUE, nullptr);
    }
    Logf("start muted: {} (hr {:#010x})", SUCCEEDED(hr) ? "process audio session muted" : "FAILED",
         static_cast<unsigned>(hr));
    for (IUnknown* object : std::initializer_list<IUnknown*>{volume, sessions, device, enumerator}) {
        if (object) {
            object->Release();
        }
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
}

// Run in background (windowed only): the game sleeps in its main loop while its foreground flag is 0.
// Its window procedure clears that flag on WM_ACTIVATE(WA_INACTIVE) and WM_KILLFOCUS and sets it on
// WM_SETFOCUS (newsteam decompile, FUN_0077c120). Those two deactivation messages go to DefWindowProc
// instead, and the cursor clip is released here so the mouse is free in other windows.
WNDPROC g_originalWndProc = nullptr;

LRESULT CALLBACK BackgroundWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const bool deactivate = (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || message == WM_KILLFOCUS;
    if (deactivate) {
        ClipCursor(nullptr);
        return DefWindowProcA(window, message, wParam, lParam);
    }
    if (message == WM_ACTIVATEAPP && wParam == FALSE) {
        wParam = TRUE;
    }
    return CallWindowProcA(g_originalWndProc, window, message, wParam, lParam);
}

void InstallRunInBackground(HWND window) {
    if (!window || g_originalWndProc) {
        return;
    }
    // The game registers an ANSI window class, so the ANSI variants keep messages unconverted.
    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&BackgroundWndProc)));
    Logf("run in background: window procedure {}", g_originalWndProc ? "subclassed" : "NOT subclassed");
}

// Signature for gGameState: the WinMain loop contains "mov dword ptr [gGameState], 2" (C7 05 <addr> 02 00 00 00).
bool GameStateSignatureMatches(uintptr_t address) {
    unsigned char pattern[10] = {0xC7, 0x05, 0, 0, 0, 0, 0x02, 0x00, 0x00, 0x00};
    std::memcpy(pattern + 2, &address, 4);
    const std::span<const unsigned char> text = MainTextSection();
    return std::search(text.begin(), text.end(), std::begin(pattern), std::end(pattern)) != text.end();
}

// First game-memory write of the project (documented in docs/ADDRESSES.md, patch P1):
// skip the DirectShow intro movies, which never finish in our test environment.
void SkipIntroMovies() {
    const uintptr_t address = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + g_build->gameStateRva;
    if (!GameStateSignatureMatches(address)) {
        Logf("skip intro: signature for gGameState at {:#010x} NOT found, feature disabled", address);
        return;
    }
    auto* gameState = reinterpret_cast<volatile int*>(address);
    const int current = *gameState;
    if (current != kGameStateStartUp) {
        Logf("skip intro: gGameState is {} (expected {}), not touching it", current, kGameStateStartUp);
        return;
    }
    *gameState = kGameStateInitOnce;
    Logf("skip intro: gGameState at {:#010x} set {} -> {} (logo state {} skipped)", address, kGameStateStartUp,
         kGameStateInitOnce, kGameStateLogoMpeg);
}

void LogLoadedModules() {
    HMODULE modules[512];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        Logf("EnumProcessModules failed: error {}", GetLastError());
        return;
    }
    std::string names;
    const size_t count = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));
    for (size_t i = 0; i < count; ++i) {
        std::string name = Narrow(ModulePath(modules[i]).filename().wstring());
        const bool interesting = name.ends_with(".asi") || name.ends_with(".ASI") ||
                                 _stricmp(name.c_str(), "dinput8.dll") == 0 ||
                                 _stricmp(name.c_str(), "vorbisFile.dll") == 0;
        if (interesting) {
            names += std::format("{}@{:#010x} ", name, reinterpret_cast<uintptr_t>(modules[i]));
        }
    }
    Logf("modules of interest: {}", names);
}

// Runs once on the game thread, from the first Direct3DCreate9 call (not under the loader lock).
void InitializeOnGameThread() {
    if (g_initialized) {
        return;
    }
    g_initialized = true;

    const std::filesystem::path exe = ModulePath(nullptr);
    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    // The loader rewrites OptionalHeader.ImageBase in memory after relocation, so the preferred
    // base must come from the file on disk.
    uintptr_t preferredBase = 0;
    if (FILE* file = nullptr; _wfopen_s(&file, exe.c_str(), L"rb") == 0 && file) {
        IMAGE_DOS_HEADER diskDos{};
        IMAGE_NT_HEADERS32 diskNt{};
        if (std::fread(&diskDos, sizeof(diskDos), 1, file) == 1 && std::fseek(file, diskDos.e_lfanew, SEEK_SET) == 0 &&
            std::fread(&diskNt, sizeof(diskNt), 1, file) == 1) {
            preferredBase = diskNt.OptionalHeader.ImageBase;
        }
        std::fclose(file);
    }
    Logf("exe: {}", Narrow(exe.wstring()));
    Logf("image base: runtime {:#010x}, preferred {:#010x} ({}), PE timestamp {:#x}",
         reinterpret_cast<uintptr_t>(base), preferredBase,
         reinterpret_cast<uintptr_t>(base) == preferredBase ? "not relocated" : "RELOCATED (ASLR)",
         nt->FileHeader.TimeDateStamp);

    const std::string sha = Sha256OfFile(exe);
    for (const KnownBuild& build : kKnownBuilds) {
        if (sha == build.sha256) {
            g_build = &build;
        }
    }
    g_buildKnown = g_build != nullptr;
    Logf("exe sha256: {} -> build {}", sha.empty() ? "<hash failed>" : sha,
         g_buildKnown ? g_build->id : "UNKNOWN (unsupported build, probe stays inert)");
    if (g_buildKnown && g_settings.skipIntroMovies) {
        SkipIntroMovies();
    }
    if (g_buildKnown) {
        probe::GameStateInit(*g_build, g_settings.autoStartNewGame);
        probe::EventsInit(*g_build, g_settings.giveMoneyOnStart);
    }
    if (g_settings.startMuted) {
        MuteProcessAudio();
    }

    LogLoadedModules();
    const bool windowedPluginPresent = GetModuleHandleW(L"III.VC.SA.WindowedMode.asi") != nullptr;
    g_windowedActive = g_buildKnown && g_settings.windowed && !windowedPluginPresent;
    Logf("windowed mode: setting {}, WindowedMode plugin {}, active {}", g_settings.windowed ? "on" : "off",
         windowedPluginPresent ? "present (it owns windowing)" : "absent", g_windowedActive ? "yes" : "no");
}

void ForceWindowedParameters(D3DPRESENT_PARAMETERS* params) {
    params->Windowed = TRUE;
    params->FullScreen_RefreshRateInHz = 0;
}

void ResizeGameWindow(HWND window, UINT backBufferWidth, UINT backBufferHeight) {
    if (!window || backBufferWidth == 0 || backBufferHeight == 0) {
        return;
    }
    SetWindowLongW(window, GWL_STYLE, static_cast<LONG>(kWindowStyle));
    SetWindowLongW(window, GWL_EXSTYLE, WS_EX_APPWINDOW);

    const int clientWidth = g_settings.windowClientWidth;
    const int clientHeight = MulDiv(clientWidth, static_cast<int>(backBufferHeight), static_cast<int>(backBufferWidth));
    RECT frame{0, 0, clientWidth, clientHeight};
    AdjustWindowRectEx(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW);

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT& work = monitor.rcWork;
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    SetWindowPos(window, HWND_NOTOPMOST, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    Logf("window resized: client {}x{} (back buffer {}x{}) at {},{}", clientWidth, clientHeight, backBufferWidth,
         backBufferHeight, x, y);
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDirect3DDevice9* device, const RECT* source, const RECT* dest,
                                        HWND window, const RGNDATA* dirty) {
    const unsigned long long frame = ++g_frames;
    const ULONGLONG now = GetTickCount64();
    // The game's WinMain switches the window to WS_POPUP after device creation; restore our frame once.
    if (g_windowedActive && g_gameWindow &&
        (static_cast<DWORD>(GetWindowLongW(g_gameWindow, GWL_STYLE)) & WS_CAPTION) != WS_CAPTION) {
        Logf("window style was reset by the game, restoring frame");
        ResizeGameWindow(g_gameWindow, g_backBufferWidth, g_backBufferHeight);
    }
    if (g_buildKnown) {
        probe::GameStateTick();
        probe::EventsTick();
    }
    if (frame == 1) {
        Logf("first Present");
        g_lastReportTick = now;
    } else if (now - g_lastReportTick >= kFpsReportIntervalMs) {
        const double seconds = static_cast<double>(now - g_lastReportTick) / 1000.0;
        Logf("frames: total {}, {:.1f} fps over last {:.1f}s", frame,
             static_cast<double>(frame - g_lastReportFrames) / seconds, seconds);
        g_lastReportTick = now;
        g_lastReportFrames = frame;
    }
    return g_origPresent(device, source, dest, window, dirty);
}

HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    Logf("Reset: {}x{} windowed={}", params->BackBufferWidth, params->BackBufferHeight, params->Windowed);
    if (g_windowedActive) {
        ForceWindowedParameters(params);
    }
    const HRESULT result = g_origReset(device, params);
    Logf("Reset result {:#010x}", static_cast<unsigned>(result));
    return result;
}

bool HookVtableSlot(void* object, size_t slot, void* detour, void** original, const char* name) {
    void* target = (*static_cast<void***>(object))[slot];
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    Logf("hook {} at {:#010x}: {}", name, reinterpret_cast<uintptr_t>(target), MH_StatusToString(status));
    return status == MH_OK;
}

HRESULT STDMETHODCALLTYPE HookedCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                                             DWORD flags, D3DPRESENT_PARAMETERS* params,
                                             IDirect3DDevice9** device) {
    Logf("CreateDevice: adapter {}, {}x{} format {} windowed={} refresh {} interval {:#x}", adapter,
         params->BackBufferWidth, params->BackBufferHeight, static_cast<int>(params->BackBufferFormat),
         params->Windowed, params->FullScreen_RefreshRateInHz, params->PresentationInterval);
    if (g_windowedActive) {
        ForceWindowedParameters(params);
        g_gameWindow = params->hDeviceWindow ? params->hDeviceWindow : focus;
        g_backBufferWidth = params->BackBufferWidth;
        g_backBufferHeight = params->BackBufferHeight;
        ResizeGameWindow(g_gameWindow, g_backBufferWidth, g_backBufferHeight);
        if (g_settings.runInBackground) {
            InstallRunInBackground(g_gameWindow);
        }
    }
    const HRESULT result = g_origCreateDevice(d3d, adapter, type, focus, flags, params, device);
    Logf("CreateDevice result {:#010x}", static_cast<unsigned>(result));
    static bool deviceHooked = false;
    if (SUCCEEDED(result) && g_buildKnown && !deviceHooked && device && *device) {
        deviceHooked = true;
        HookVtableSlot(*device, kDevicePresentSlot, reinterpret_cast<void*>(&HookedPresent),
                       reinterpret_cast<void**>(&g_origPresent), "IDirect3DDevice9::Present");
        HookVtableSlot(*device, kDeviceResetSlot, reinterpret_cast<void*>(&HookedReset),
                       reinterpret_cast<void**>(&g_origReset), "IDirect3DDevice9::Reset");
    }
    return result;
}

IDirect3D9* WINAPI HookedDirect3DCreate9(UINT sdkVersion) {
    InitializeOnGameThread();
    IDirect3D9* d3d = g_origDirect3DCreate9(sdkVersion);
    Logf("Direct3DCreate9({}) -> {:#010x}", sdkVersion, reinterpret_cast<uintptr_t>(d3d));
    static bool d3dHooked = false;
    if (d3d && g_buildKnown && !d3dHooked) {
        d3dHooked = true;
        HookVtableSlot(d3d, kD3D9CreateDeviceSlot, reinterpret_cast<void*>(&HookedCreateDevice),
                       reinterpret_cast<void**>(&g_origCreateDevice), "IDirect3D9::CreateDevice");
    }
    return d3d;
}

void OpenLog() {
    g_gameDir = ModulePath(nullptr).parent_path();
    const std::filesystem::path logDir = g_gameDir / L"sacore" / L"logs";
    std::error_code error;
    std::filesystem::create_directories(logDir, error);
    _wfopen_s(&g_log, (logDir / L"probe.log").c_str(), L"w");
}

void Attach() {
    g_startTick = GetTickCount64();
    OpenLog();
    g_settings = LoadSettings();
    Logf("SACoreProbe loaded from {}", Narrow(ModulePath(g_self).wstring()));

    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    void* target = d3d9 ? reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
    if (!target) {
        Log("d3d9.dll!Direct3DCreate9 not found at attach time; probe stays passive");
        return;
    }
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK) {
        status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedDirect3DCreate9),
                               reinterpret_cast<void**>(&g_origDirect3DCreate9));
    }
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    Logf("hook Direct3DCreate9 at {:#010x}: {}", reinterpret_cast<uintptr_t>(target), MH_StatusToString(status));
}

void Detach() {
    Logf("detach: {} frames presented", g_frames.load());
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    std::lock_guard lock(g_logMutex);
    if (g_log) {
        std::fclose(g_log);
        g_log = nullptr;
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        Attach();
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        Detach();
    }
    return TRUE;
}

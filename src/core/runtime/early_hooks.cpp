#include "early_hooks.h"

#include "build_info.h"
#include "config.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

namespace sacore::early {
namespace {

using RegQueryValueExAFn = LONG(WINAPI*)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
using SetForegroundWindowFn = BOOL(WINAPI*)(HWND);
using SetFocusFn = HWND(WINAPI*)(HWND);
using CreateEventAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
using CreateMutexAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
using CreateSemaphoreAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR);
using OpenSemaphoreAFn = HANDLE(WINAPI*)(DWORD, BOOL, LPCSTR);
using CreateFileMappingAFn = HANDLE(WINAPI*)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
using OpenFileMappingAFn = HANDLE(WINAPI*)(DWORD, BOOL, LPCSTR);

RegQueryValueExAFn g_origRegQueryValueExA = nullptr;
SetForegroundWindowFn g_origSetForegroundWindow = nullptr;
SetFocusFn g_origSetFocus = nullptr;
CreateEventAFn g_origCreateEventA = nullptr;
CreateMutexAFn g_origCreateMutexA = nullptr;
CreateSemaphoreAFn g_origCreateSemaphoreA = nullptr;
OpenSemaphoreAFn g_origOpenSemaphoreA = nullptr;
CreateFileMappingAFn g_origCreateFileMappingA = nullptr;
OpenFileMappingAFn g_origOpenFileMappingA = nullptr;
InstanceState g_instance;
std::filesystem::path g_gameDirectory;
RedirectState g_redirect;
FocusState g_focus;
std::once_flag g_decided;

void Decide() {
    g_redirect.decided = true;
    std::ifstream stream(g_gameDirectory / L"sacore" / L"config" / L"core.toml", std::ios::binary);
    if (!stream) {
        return;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    ConfigSchema schema;
    schema.settings = {SettingSpec{"saves.separate_user_files", false, "", {}, {}, {}},
                       SettingSpec{"window.focus_on_start", true, "", {}, {}, {}}};
    const ConfigLoadResult config = ConfigLoader::FromText(text.str(), schema);
    const bool separate = config.config.GetBool("saves.separate_user_files");
    const bool blockFocus = !config.config.GetBool("window.focus_on_start");
    if (!separate && !blockFocus) {
        return;
    }
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (!FindBuildBySha256(IdentifyExe(exe).sha256)) {
        g_redirect.problem = "unsupported game executable: SACore stays inactive";
        return;
    }
    g_focus.blockFocus = blockFocus;
    if (!separate) {
        return;
    }
    const std::filesystem::path base = g_gameDirectory / L"sacore" / L"userdata";
    std::error_code error;
    std::filesystem::create_directories(base, error);
    const std::wstring wide = base.wstring();
    BOOL lossy = FALSE;
    const int size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, nullptr, 0, nullptr, &lossy);
    if (lossy || size <= 0 || size >= MAX_PATH) {
        g_redirect.problem = "the install path cannot be passed to the game (non-ANSI characters or too long)";
        return;
    }
    g_redirect.base.resize(static_cast<size_t>(size));
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, g_redirect.base.data(), size, nullptr, nullptr);
    g_redirect.base.resize(static_cast<size_t>(size) - 1);
    g_redirect.active = true;
}

LONG WINAPI HookedRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (name && _stricmp(name, "Personal") == 0) {
        ++g_redirect.personalQueries;
        std::call_once(g_decided, Decide);
        if (g_redirect.active) {
            const DWORD needed = static_cast<DWORD>(g_redirect.base.size() + 1);
            if (type) {
                *type = REG_SZ;
            }
            if (data && size && *size >= needed) {
                std::memcpy(data, g_redirect.base.c_str(), needed);
                *size = needed;
                return ERROR_SUCCESS;
            }
            if (size) {
                *size = needed;
            }
            return data ? ERROR_MORE_DATA : ERROR_SUCCESS;
        }
    }
    return g_origRegQueryValueExA(key, name, reserved, type, data, size);
}

BOOL WINAPI HookedSetForegroundWindow(HWND window) {
    std::call_once(g_decided, Decide);
    if (g_focus.blockFocus) {
        ++g_focus.blockedCalls;
        return TRUE;
    }
    return g_origSetForegroundWindow(window);
}

HWND WINAPI HookedSetFocus(HWND window) {
    std::call_once(g_decided, Decide);
    if (g_focus.blockFocus && GetForegroundWindow() != window) {
        ++g_focus.blockedCalls;
        return nullptr;
    }
    return g_origSetFocus(window);
}

// The game refuses a second instance through a named event or mutex carrying its window class name ("Grand theft auto
// San Andreas"). While a test scenario is armed (sacore\testing\scenario.toml), the name gets this process's id, so
// the test runner can run both supported builds side by side. Normal play is unaffected.
// Every other named kernel object of the game (the 1.0 US executable also carries code with fixed-name mutexes,
// semaphores and file mappings) gets the process id too, so two copies of one build never share one.
std::string InstanceName(LPCSTR name) {
    std::string text = name ? name : "";
    if (!g_instance.parallelAllowed || text.empty() || text.starts_with("Global\\")) {
        return text;
    }
    if (g_instance.names.size() < 16 && g_instance.names.find(text) == std::string::npos) {
        g_instance.names += (g_instance.names.empty() ? "" : ", ") + text;
    }
    ++g_instance.renamed;
    g_instance.cdStreamRenamed = g_instance.cdStreamRenamed || text == "CdStream";
    return text + " (" + std::to_string(GetCurrentProcessId()) + ")";
}

HANDLE WINAPI HookedCreateSemaphoreA(LPSECURITY_ATTRIBUTES security, LONG initial, LONG maximum, LPCSTR name) {
    const std::string unique = InstanceName(name);
    return g_origCreateSemaphoreA(security, initial, maximum, name ? unique.c_str() : nullptr);
}

HANDLE WINAPI HookedOpenSemaphoreA(DWORD access, BOOL inherit, LPCSTR name) {
    const std::string unique = InstanceName(name);
    return g_origOpenSemaphoreA(access, inherit, name ? unique.c_str() : nullptr);
}

HANDLE WINAPI HookedCreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES security, DWORD protect, DWORD high, DWORD low,
                                       LPCSTR name) {
    const std::string unique = InstanceName(name);
    return g_origCreateFileMappingA(file, security, protect, high, low, name ? unique.c_str() : nullptr);
}

HANDLE WINAPI HookedOpenFileMappingA(DWORD access, BOOL inherit, LPCSTR name) {
    const std::string unique = InstanceName(name);
    return g_origOpenFileMappingA(access, inherit, name ? unique.c_str() : nullptr);
}

HANDLE WINAPI HookedCreateEventA(LPSECURITY_ATTRIBUTES security, BOOL manual, BOOL initial, LPCSTR name) {
    const std::string unique = InstanceName(name);
    return g_origCreateEventA(security, manual, initial, name ? unique.c_str() : nullptr);
}

HANDLE WINAPI HookedCreateMutexA(LPSECURITY_ATTRIBUTES security, BOOL owner, LPCSTR name) {
    const std::string unique = InstanceName(name);
    return g_origCreateMutexA(security, owner, name ? unique.c_str() : nullptr);
}

// Replaces the main module's import slot dll!function; returns the previous target or nullptr.
void* PatchImport(const char* dll, const char* function, void* detour) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
         directory.VirtualAddress && descriptor->Name != 0; ++descriptor) {
        if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), dll) != 0 ||
            descriptor->OriginalFirstThunk == 0) {
            continue;
        }
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
                continue;
            }
            const auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0) {
                continue;
            }
            DWORD oldProtect = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(DWORD), PAGE_READWRITE, &oldProtect)) {
                return nullptr;
            }
            void* previous = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<DWORD>(detour);
            VirtualProtect(&slots->u1.Function, sizeof(DWORD), oldProtect, &oldProtect);
            return previous;
        }
    }
    return nullptr;
}

}  // namespace

void Install(const std::filesystem::path& gameDirectory) {
    g_gameDirectory = gameDirectory;
    g_origRegQueryValueExA = reinterpret_cast<RegQueryValueExAFn>(
        PatchImport("ADVAPI32.dll", "RegQueryValueExA", reinterpret_cast<void*>(&HookedRegQueryValueExA)));
    g_redirect.hookInstalled = g_origRegQueryValueExA != nullptr;
    g_origSetForegroundWindow = reinterpret_cast<SetForegroundWindowFn>(
        PatchImport("USER32.dll", "SetForegroundWindow", reinterpret_cast<void*>(&HookedSetForegroundWindow)));
    g_origSetFocus = reinterpret_cast<SetFocusFn>(
        PatchImport("USER32.dll", "SetFocus", reinterpret_cast<void*>(&HookedSetFocus)));
    g_focus.hooksInstalled = g_origSetForegroundWindow && g_origSetFocus;
    std::error_code error;
    g_instance.parallelAllowed = std::filesystem::exists(gameDirectory / L"sacore" / L"testing" / L"scenario.toml", error);
    if (g_instance.parallelAllowed) {
        g_origCreateEventA = reinterpret_cast<CreateEventAFn>(
            PatchImport("KERNEL32.dll", "CreateEventA", reinterpret_cast<void*>(&HookedCreateEventA)));
        g_origCreateMutexA = reinterpret_cast<CreateMutexAFn>(
            PatchImport("KERNEL32.dll", "CreateMutexA", reinterpret_cast<void*>(&HookedCreateMutexA)));
        g_origCreateSemaphoreA = reinterpret_cast<CreateSemaphoreAFn>(
            PatchImport("KERNEL32.dll", "CreateSemaphoreA", reinterpret_cast<void*>(&HookedCreateSemaphoreA)));
        g_origOpenSemaphoreA = reinterpret_cast<OpenSemaphoreAFn>(
            PatchImport("KERNEL32.dll", "OpenSemaphoreA", reinterpret_cast<void*>(&HookedOpenSemaphoreA)));
        g_origCreateFileMappingA = reinterpret_cast<CreateFileMappingAFn>(
            PatchImport("KERNEL32.dll", "CreateFileMappingA", reinterpret_cast<void*>(&HookedCreateFileMappingA)));
        g_origOpenFileMappingA = reinterpret_cast<OpenFileMappingAFn>(
            PatchImport("KERNEL32.dll", "OpenFileMappingA", reinterpret_cast<void*>(&HookedOpenFileMappingA)));
        g_instance.hooksInstalled = g_origCreateEventA && g_origCreateMutexA;
    }
}

const InstanceState& Instance() {
    return g_instance;
}

void DecideNow() {
    std::call_once(g_decided, Decide);
}

const RedirectState& UserFilesRedirect() {
    return g_redirect;
}

const FocusState& Focus() {
    return g_focus;
}

}  // namespace sacore::early

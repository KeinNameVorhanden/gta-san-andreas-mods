// SACore.asi entry point. DllMain only installs one hook (H1); everything else runs on the game thread
// from the game's first Direct3DCreate9 call (docs/ARCHITECTURE.md section 5).

#include "core_runtime.h"
#include "early_hooks.h"

#include <windows.h>
#include <d3d9.h>

#include <MinHook.h>

namespace {

using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);

HMODULE g_self = nullptr;
Direct3DCreate9Fn g_origDirect3DCreate9 = nullptr;
sacore::CoreRuntime* g_runtime = nullptr;  // lives until process exit

IDirect3D9* WINAPI HookedDirect3DCreate9(UINT sdkVersion) {
    if (!g_runtime) {
        g_runtime = new sacore::CoreRuntime(g_self);
        g_runtime->Initialize();
    }
    IDirect3D9* d3d = g_origDirect3DCreate9(sdkVersion);
    g_runtime->OnDirect3DCreated(d3d);
    return d3d;
}

void Attach() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    sacore::early::Install(std::filesystem::path(exe).parent_path());
    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    void* target = d3d9 ? reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
    if (!target || MH_Initialize() != MH_OK) {
        return;  // no log exists yet; without this entry point the core cannot start
    }
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedDirect3DCreate9),
                      reinterpret_cast<void**>(&g_origDirect3DCreate9)) == MH_OK) {
        MH_EnableHook(target);
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        Attach();
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        // Explicit unload (not process exit): remove hooks. At process exit nothing is torn down.
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}

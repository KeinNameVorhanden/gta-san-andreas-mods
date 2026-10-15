// Stability layer: crash handler with minidumps, game message box logging, Device Selection suppression.
#pragma once

#include "event_bus.h"
#include "logger.h"
#include "patch_manager.h"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sacore {

struct StabilitySettings {
    bool crashDumps = true;
    bool skipDeviceSelection = true;
    std::string deviceSelectionResolution;  // "WxH" or "WxHxD"; empty: the dialog's default entry
    int testCrashAfterSeconds = 0;  // TEST ONLY: deliberate access violation in the frame hook
};

// Index of the Device Selection list entry ("1280 x 720 x 32 (WIDESCREEN)", the game's own text) matching `wanted`
// ("1280x720" takes the deepest colour depth offered, "1280x720x16" exactly that), or -1.
int PickVideoModeEntry(const std::vector<std::string>& entries, std::string_view wanted);

class Stability {
public:
    using ContextProvider = std::function<std::string()>;
    // Extra files for the crash package: (file on disk, name inside the zip).
    using PackageFiles = std::function<std::vector<std::pair<std::filesystem::path, std::string>>()>;

    Stability(Logger& logger, PatchManager& patches, EventBus& bus, const RingBufferSink* recentLog);
    ~Stability();
    Stability(const Stability&) = delete;
    Stability& operator=(const Stability&) = delete;

    // `crashDirectory` receives <timestamp>.dmp and <timestamp>.txt; `context` describes core, build and mods.
    void Install(const StabilitySettings& settings, const std::filesystem::path& crashDirectory,
                 ContextProvider context);
    // Files added to every crash package (session log, settings, diagnostics). Set before the first crash.
    void SetPackageFiles(PackageFiles files) { packageFiles_ = std::move(files); }

private:
    static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* pointers);
    static LRESULT CALLBACK DialogWatcher(int code, WPARAM wParam, LPARAM lParam);
    void SelectDialogResolution(HWND dialog);
    static int WINAPI HookedMessageBoxA(HWND, LPCSTR, LPCSTR, UINT);
    void WriteCrashReport(EXCEPTION_POINTERS* pointers);

    Logger& logger_;
    PatchManager& patches_;
    EventBus& bus_;
    const RingBufferSink* recentLog_;
    StabilitySettings settings_;
    std::filesystem::path crashDirectory_;
    ContextProvider context_;
    PackageFiles packageFiles_;
    void PackLeftoverReports();
};

}  // namespace sacore

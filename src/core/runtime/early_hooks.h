// Hooks that must exist before the game's WinMain runs (installed from DllMain; import-table writes only).
// Their behaviour is decided lazily on first use (game thread, outside the loader lock) from
// sacore\config\core.toml, and only for a known game build.
#pragma once

#include <filesystem>
#include <string>

namespace sacore::early {

void Install(const std::filesystem::path& gameDirectory);
// Reads core.toml now if no hook has done so yet (the game's first lookup happens before plugins load).
void DecideNow();

struct RedirectState {
    bool hookInstalled = false;
    bool active = false;          // separate_user_files enabled and the path is usable
    std::string base;             // ANSI path returned for "Personal"
    std::string problem;          // why the redirect is inactive although enabled
    int personalQueries = 0;      // "Personal" lookups seen so far (diagnostics)
    bool decided = false;         // core.toml was read
};
const RedirectState& UserFilesRedirect();

struct FocusState {
    bool hooksInstalled = false;
    bool blockFocus = false;      // [window] focus_on_start = false on a known build
    int blockedCalls = 0;         // SetForegroundWindow / SetFocus calls skipped
};
const FocusState& Focus();

struct InstanceState {
    bool parallelAllowed = false;  // a test scenario is armed: a second game instance may run beside this one
    bool hooksInstalled = false;
    int renamed = 0;               // named objects given this process's id
    std::string names;             // the first names seen (diagnostics)
    bool cdStreamRenamed = false;  // the stream semaphore was created after the hooks: already private
};
const InstanceState& Instance();

}  // namespace sacore::early

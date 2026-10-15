// Load last save: when the game reaches the main menu for the first time, load the newest valid save the
// way the menu does (core load_game: CheckSlotDataValid selects the file, then the menu's load flags). Holding the
// configured key (Shift) keeps the menu. Stays idle when the test harness starts new games automatically.
#include <sacore/sacore.hpp>

#include <windows.h>

#include <memory>
#include <string>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
bool g_done = false;          // only the first visit of the main menu
ULONGLONG g_menuSince = 0;

void Tick() {
    if (g_done) {
        return;
    }
    if (g_host->GameState() != 7) {
        g_menuSince = 0;
        if (g_host->GameState() > 7) {
            g_done = true;  // the player started or loaded a game already
        }
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_menuSince == 0) {
        g_menuSince = now;
    }
    if (now - g_menuSince < 1000) {
        return;  // let the menu settle (the same delay the menu itself needs before input)
    }
    g_done = true;
    if (!g_host->ConfigBool("enabled", true)) {
        return;
    }
    if (g_host->CoreSetting("testing.auto_start_new_game").value_or("false") == "true") {
        g_host->Info("not loading: the test harness starts a new game (testing.auto_start_new_game)");
        return;
    }
    const auto holdKey = static_cast<int>(g_host->ConfigInt("hold_key", VK_SHIFT));
    if (GetAsyncKeyState(holdKey) & 0x8000) {
        g_host->Info("key {} held: staying in the main menu", holdKey);
        return;
    }
    const bool all = g_host->ConfigBool("include_quick_and_auto", true);
    const sacore::mod::Host::SaveEntry* newest = nullptr;
    const auto saves = g_host->Saves();
    for (const auto& save : saves) {
        if (save.valid && (all || save.name.starts_with("slot")) && (!newest || save.modified > newest->modified)) {
            newest = &save;
        }
    }
    if (!newest) {
        g_host->Info("no valid save to load");
        return;
    }
    g_host->Info("loading the newest save: {} ({})", newest->name, newest->file);
    if (!g_host->LoadGame(newest->name)) {
        g_host->Warn("loading {} was refused (see the core log)", newest->name);
    }
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (!g_host->HasLoadGame() || !g_host->HasPatching()) {
        g_host->Error("needs SACore 1.2.0 or newer");
        return 1;
    }
    // The frame event also runs in the menu (the game tick only while playing).
    g_host->On(SACORE_EVENT_FRAME, [](const SacoreEvent&) { Tick(); });
    g_host->Info("ready: newest save at start ({}), hold key {} to stay in the menu",
                 g_host->ConfigBool("include_quick_and_auto", true) ? "menu, quick and auto saves" : "menu slots",
                 g_host->ConfigInt("hold_key", VK_SHIFT));
    return 0;
}

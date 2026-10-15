// Quick save: quick save and quick load by key, in their own rotating save files (GTASAqs1.b ...), never in the 8
// menu slots. Saves only in free roam: a save made during a mission reloads into a broken mission. Settings live in
// the F11 console (Settings page).
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
bool g_savePending = false;
std::string g_pendingName;
ULONGLONG g_loadArmedAt = 0;  // quick load needs a second press within 3 s

std::string KeyName(int64_t vk) {
    return vk >= VK_F1 && vk <= VK_F24 ? "F" + std::to_string(vk - VK_F1 + 1) : "key " + std::to_string(vk);
}

struct QuickSave {
    std::string name;
    long long modified = 0;
};

// Newest existing quick save (empty name if none).
QuickSave Newest() {
    QuickSave newest;
    std::istringstream lines(g_host->SaveList());
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name, file, modified, valid;
        std::getline(fields, name, '\t');
        std::getline(fields, file, '\t');
        std::getline(fields, modified, '\t');
        std::getline(fields, valid, '\t');
        if (name.starts_with("quick") && valid == "1" && std::atoll(modified.c_str()) >= newest.modified) {
            newest = {name, std::atoll(modified.c_str())};
        }
    }
    return newest;
}

void Message(const std::string& text) {
    if (g_host->ConfigBool("show_messages", true)) {
        g_game->ShowHelp(text.c_str());
    }
    g_host->Info("{}", text);
}

void QuickSaveNow() {
    if (!g_host->ConfigBool("enabled", true) || g_host->GameState() != 9) {
        return;
    }
    if (g_game->OnMission() || g_game->MiniGame()) {
        Message("Quick save is not possible during a mission.");
        return;
    }
    if (g_game->CutsceneRunning() || g_game->ControlsDisabled() || g_game->PlayerState() != 0) {
        const auto pad = g_host->GameFunction<uint8_t*(__cdecl*)(int)>("CPad::GetPad")(0);
        g_host->Info("refused: cutscene {}, controls disabled {} (raw {:#06x}), player state {}",
                     g_game->CutsceneRunning(), g_game->ControlsDisabled(),
                     pad ? *reinterpret_cast<const uint16_t*>(pad + 0x10E) : 0, g_game->PlayerState());
        Message("Quick save is not possible right now.");
        return;
    }
    if (g_game->InVehicle() && !g_host->ConfigBool("allow_in_vehicle", false)) {
        Message("Quick save: leave the vehicle first (vehicles are not part of a save).");
        return;
    }
    if (g_game->WantedLevel() > 0 && !g_host->ConfigBool("allow_when_wanted", true)) {
        Message("Quick save is not possible while wanted.");
        return;
    }
    // Rotate: the slot after the newest quick save.
    const int slots = static_cast<int>(std::clamp<int64_t>(g_host->ConfigInt("slots", 3), 1, 9));
    const QuickSave newest = Newest();
    const int last = newest.name.empty() ? 0 : newest.name.back() - '0';
    const int next = last % slots + 1;
    g_pendingName = "quick" + std::to_string(next);
    if (g_host->SaveGame(g_pendingName)) {
        g_savePending = true;
    } else {
        Message("Quick save is not possible here (interior or cutscene).");
    }
}

void QuickLoadNow() {
    if (!g_host->ConfigBool("enabled", true) || (g_host->GameState() != 9 && g_host->GameState() != 7)) {
        return;
    }
    const QuickSave newest = Newest();
    if (newest.name.empty()) {
        Message("No quick save yet.");
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_loadArmedAt > 3000) {
        g_loadArmedAt = now;
        Message("Press " + KeyName(g_host->ConfigInt("quick_load_key", VK_F9)) + " again to load quick save " +
                newest.name.substr(5) + ".");
        return;
    }
    g_loadArmedAt = 0;
    if (!g_host->LoadGame(newest.name)) {
        Message("Quick load failed (see the SACore log).");
    }
}

void BindKeys() {
    g_host->ClearKeys();
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("quick_save_key", VK_F5)), &QuickSaveNow);
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("quick_load_key", VK_F9)), &QuickLoadNow);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->HasSaveGame() || !g_host->HasLoadGame()) {
        g_host->Error("needs SACore 1.1.2 or newer");
        return 1;
    }
    BindKeys();
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { BindKeys(); });
    g_host->On(SACORE_EVENT_SAVE_COMPLETED, [](const SacoreEvent& e) {
        if (!g_savePending) {
            return;
        }
        g_savePending = false;
        Message(e.success ? "Quick saved (" + g_pendingName.substr(5) + ")." : "Quick save FAILED.");
    });
    g_host->Info("ready: quick save {}, quick load {} (press twice)",
                 KeyName(g_host->ConfigInt("quick_save_key", VK_F5)),
                 KeyName(g_host->ConfigInt("quick_load_key", VK_F9)));
    return 0;
}

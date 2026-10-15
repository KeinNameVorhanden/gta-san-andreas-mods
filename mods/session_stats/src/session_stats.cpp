// Session stats: the SACore example mod. Shows every part of the mod API a typical mod needs:
//   config        enabled, announce_every_minutes, track_distance (mod.toml [config])
//   logging       sacore\logs\sacore.log, tagged "session_stats"
//   events        game state, frames, save started/completed, load completed, shutdown
//   state read    player money and position (capability "player.read", with a graceful fallback)
//   patch         hook of kernel32!Sleep through the patch manager to measure how long the game idles
//   save data     lifetime totals stored with each save (side-car data) and restored when that save is loaded
// Keep the Host object alive for the whole session: event handlers live inside it.

#include <sacore/sacore.hpp>

#include <windows.h>

#include <atomic>
#include <format>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
bool g_playerAvailable = false;
bool g_trackDistance = true;
std::chrono::minutes g_announceEvery{10};

struct Totals {
    double distanceMeters = 0;
    long long moneyEarned = 0;
    long long moneySpent = 0;
};
Totals g_session;
Totals g_lifetime;  // restored from the loaded save, saved with every save
std::optional<SacorePlayer> g_lastPlayer;
std::chrono::steady_clock::time_point g_lastAnnounce = std::chrono::steady_clock::now();
std::chrono::steady_clock::time_point g_sessionStart = std::chrono::steady_clock::now();

// Patch: time spent in Sleep (frame limiter and background waits). Runs on any thread, so atomics.
std::atomic<unsigned long long> g_sleptMs{0};
VOID(WINAPI* g_originalSleep)(DWORD) = nullptr;
VOID WINAPI CountingSleep(DWORD milliseconds) {
    g_sleptMs += milliseconds;
    g_originalSleep(milliseconds);
}

long long ParseOr(const std::optional<std::string>& text, long long fallback) {
    try {
        return text ? std::stoll(*text) : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

void Announce(const char* reason) {
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::now() - g_sessionStart);
    g_host->Info("{}: session {} min, {:.0f} m travelled, ${} earned, ${} spent, game idled {} s; lifetime {:.0f} m, "
                 "${} earned",
                 reason, minutes.count(), g_session.distanceMeters, g_session.moneyEarned, g_session.moneySpent,
                 g_sleptMs.load() / 1000, g_lifetime.distanceMeters + g_session.distanceMeters,
                 g_lifetime.moneyEarned + g_session.moneyEarned);
}

void OnFrame(const SacoreEvent&) {
    if (!g_playerAvailable || g_host->GameState() != 9) {
        return;
    }
    const auto player = g_host->Player();
    if (!player) {
        g_lastPlayer.reset();  // loading screen, cutscene transitions: start again from the next sample
        return;
    }
    if (g_lastPlayer) {
        const int delta = player->money - g_lastPlayer->money;
        (delta > 0 ? g_session.moneyEarned : g_session.moneySpent) += std::abs(delta);
        if (g_trackDistance) {
            const float dx = player->position[0] - g_lastPlayer->position[0];
            const float dy = player->position[1] - g_lastPlayer->position[1];
            const double step = std::sqrt(dx * dx + dy * dy);
            if (step < 50.0) {  // ignore teleports, respawns and interior transitions
                g_session.distanceMeters += step;
            }
        }
    }
    g_lastPlayer = player;
    if (std::chrono::steady_clock::now() - g_lastAnnounce >= g_announceEvery) {
        g_lastAnnounce = std::chrono::steady_clock::now();
        Announce("progress");
    }
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    sacore::mod::Host& host = *g_host;
    if (!host.ConfigBool("enabled", true)) {
        host.Info("disabled in sacore\\config\\mods\\session_stats.toml");
        return 0;
    }
    g_trackDistance = host.ConfigBool("track_distance", true);
    g_announceEvery = std::chrono::minutes(host.ConfigInt("announce_every_minutes", 10));

    // Capability check with a fallback: without player access the mod still measures idle time.
    g_playerAvailable = host.Has("player.read");
    if (!g_playerAvailable) {
        host.Warn("player.read is unavailable on {}: money and distance are not tracked", host.GameBuild());
    }

    // Documented patch through the core patch manager (switchable in core.toml [patches] disabled).
    auto* sleep = reinterpret_cast<VOID(WINAPI*)(DWORD)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Sleep"));
    if (!host.Hook("sleep_counter", "kernel32!Sleep", "measure how long the game idles (frame limiter, background)",
                   sleep, &CountingSleep, &g_originalSleep)) {
        host.Warn("Sleep hook not installed: idle time is not measured");
    }

    host.On(SACORE_EVENT_FRAME, &OnFrame);
    host.On(SACORE_EVENT_GAME_STATE_CHANGED, [](const SacoreEvent& e) {
        if (e.state_to == 9) {
            g_session = {};
            g_lastPlayer.reset();
            g_sessionStart = std::chrono::steady_clock::now();
            g_host->Info("tracking started (announce every {} min)", g_announceEvery.count());
        }
    });
    host.On(SACORE_EVENT_SAVE_STARTED, [](const SacoreEvent&) {
        // Lifetime totals travel with the save; the core writes them next to it when the save completes.
        g_host->SetSaveValue("lifetime_distance_m", std::to_string(std::llround(g_lifetime.distanceMeters + g_session.distanceMeters)));
        g_host->SetSaveValue("lifetime_money_earned", std::to_string(g_lifetime.moneyEarned + g_session.moneyEarned));
        g_host->SetSaveValue("lifetime_money_spent", std::to_string(g_lifetime.moneySpent + g_session.moneySpent));
    });
    host.On(SACORE_EVENT_SAVE_COMPLETED, [](const SacoreEvent& e) {
        if (e.success) {
            Announce(std::format("saved totals with slot {}", g_host->SaveSlot()).c_str());
        }
    });
    host.On(SACORE_EVENT_LOAD_COMPLETED, [](const SacoreEvent& e) {
        if (!e.success) {
            return;
        }
        g_lifetime.distanceMeters = static_cast<double>(ParseOr(g_host->SaveValue("lifetime_distance_m"), 0));
        g_lifetime.moneyEarned = ParseOr(g_host->SaveValue("lifetime_money_earned"), 0);
        g_lifetime.moneySpent = ParseOr(g_host->SaveValue("lifetime_money_spent"), 0);
        g_host->Info("lifetime totals restored from slot {}", g_host->SaveSlot());
    });
    host.On(SACORE_EVENT_SHUTDOWN, [](const SacoreEvent&) { Announce("session summary"); });
    host.Info("ready on {} (player tracking {})", host.GameBuild(), g_playerAvailable ? "on" : "off");
    return 0;
}

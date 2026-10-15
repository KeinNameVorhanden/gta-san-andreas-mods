// Runtime test mod exercising the public API through the C++ wrapper. Not shipped.
// TEST_API_CRASH_IN_EVENT=1 builds a variant that crashes inside its frame handler.
#include <sacore/sacore.hpp>

#include <memory>

#ifndef TEST_API_CRASH_IN_EVENT
#define TEST_API_CRASH_IN_EVENT 0
#endif

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
uint64_t g_frames = 0;
bool g_playerLogged = false;
uint64_t g_playingSinceFrame = 0;

__declspec(noinline) int HookTarget(int value) {
    volatile int result = value + 1;
    return result;
}
int (*g_originalHookTarget)(int) = nullptr;
__declspec(noinline) int HookDetour(int value) {
    return g_originalHookTarget(value) * 100;
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    sacore::mod::Host& host = *g_host;
#if TEST_API_CRASH_IN_EVENT
    host.On(SACORE_EVENT_FRAME, [](const SacoreEvent& e) {
        if (e.frame >= 100) {
            g_host->Warn("crashing inside a frame handler on purpose");
            *static_cast<volatile int*>(nullptr) = 1;
        }
    });
    return 0;
#else
    host.Info("api: config speed = {}, label = {}, enabled = {}", host.ConfigDouble("speed"),
              host.ConfigString("label"), host.ConfigBool("enabled"));
    host.Info("api: capability player.read is {}", static_cast<int>(host.Capability("player.read")));
    if (!host.Hook("self_test", "test_api!HookTarget", "prove that mod hooks go through the patch manager",
                   &HookTarget, &HookDetour, &g_originalHookTarget)) {
        host.Error("api: hook could not be installed");
    } else {
        host.Info("api: hook works = {}", HookTarget(1) == 200);
    }
    host.On(SACORE_EVENT_FRAME, [](const SacoreEvent& e) {
        ++g_frames;
        if (e.frame % 600 == 0) {
            g_host->Info("api: frame events received ({})", g_frames);
        }
        if (g_host->GameState() == 9 && !g_playerLogged) {
            if (g_playingSinceFrame == 0) {
                g_playingSinceFrame = e.frame;
            } else if (e.frame - g_playingSinceFrame > 60) {
                if (const auto player = g_host->Player()) {
                    g_host->Info("api: player health {:.0f} money {} pos ({:.1f}, {:.1f})", player->health,
                                 player->money, player->position[0], player->position[1]);
                    g_playerLogged = true;
                }
            }
        }
    });
    host.On(SACORE_EVENT_GAME_STATE_CHANGED, [](const SacoreEvent& e) {
        g_host->Info("api: state {} -> {}", e.state_from, e.state_to);
        if (e.state_to == 9) {
            g_host->Info("api: side-car value stored = {}", g_host->SetSaveValue("marker", "set-at-state-9"));
            // Live settings: an out-of-range value is refused, a valid one is stored and reported back.
            const bool refused = !g_host->SetSetting("test_api", "speed", "99");
            const bool accepted = g_host->SetSetting("test_api", "speed", "4.5");
            g_host->Info("api: live setting refused out of range {}, accepted {}, now {:.1f}, listed {}", refused,
                         accepted, g_host->ConfigDouble("speed"),
                         g_host->SettingsList().find("test_api\tspeed\tdouble\t4.5") != std::string::npos);
            g_host->SetSetting("test_api", "speed", "2.5");  // the next run starts from the declared value again
        }
    });
    host.On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent& e) {
        g_host->Info("api: setting changed: {}", e.struct_size >= sizeof(SacoreEvent) && e.text ? e.text : "?");
    });
    host.On(SACORE_EVENT_SAVE_COMPLETED, [](const SacoreEvent& e) {
        g_host->Info("api: save completed {} in slot {}", e.success, g_host->SaveSlot());
    });
    host.On(SACORE_EVENT_LOAD_COMPLETED, [](const SacoreEvent& e) { g_host->Info("api: load completed {}", e.success); });
    host.On(SACORE_EVENT_SHUTDOWN, [](const SacoreEvent&) {
        g_host->Info("api: shutdown event after {} frames; mods seen: {}", g_frames,
                     g_host->Diagnostics("mods").empty() ? "none" : "listed");
    });
    return 0;
#endif
}

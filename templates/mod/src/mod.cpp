// {{NAME}}: SACore mod entry point. API reference: docs/API.md, header sacore/sacore.hpp.
#include <sacore/sacore.hpp>

#include <memory>

namespace {
std::unique_ptr<sacore::mod::Host> g_host;  // keeps event handlers alive for the whole session
}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    sacore::mod::Host& host = *g_host;
    host.Info("{{ID}} started on {} (core {})", host.GameBuild(), host.CoreVersion());
    if (!host.ConfigBool("enabled", true)) {
        host.Info("disabled in sacore\\config\\mods\\{{ID}}.toml");
        return 0;
    }
    host.On(SACORE_EVENT_GAME_STATE_CHANGED, [](const SacoreEvent& e) {
        if (e.state_to == 9) {
            g_host->Info("gameplay started");
        }
    });
    return 0;  // non-zero refuses loading
}

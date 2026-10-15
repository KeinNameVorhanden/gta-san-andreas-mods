// Runtime test mods for the core mod loader. Behaviour selected at compile time:
//   TEST_MOD_BEHAVIOUR 0 = start normally, 1 = refuse with error code 7, 2 = crash (null write) in main.
#include <sacore/sacore_api.h>

#include <cstdio>

#ifndef TEST_MOD_BEHAVIOUR
#error "define TEST_MOD_BEHAVIOUR"
#endif

namespace {
const SacoreHost* g_host = nullptr;

void Shutdown() {
    g_host->log(g_host, SACORE_LOG_INFO, "shutdown callback reached");
}
}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* host, SacoreModCallbacks* callbacks) {
    g_host = host;
    (void)callbacks;
    char line[256];
    std::snprintf(line, sizeof(line), "main called: core %s, build %s, api %u, dir %s", host->core_version,
                  host->game_build, host->api_version, host->mod_directory);
    host->log(host, SACORE_LOG_INFO, line);
#if TEST_MOD_BEHAVIOUR == 1
    host->log(host, SACORE_LOG_WARN, "refusing to start on purpose");
    return 7;
#elif TEST_MOD_BEHAVIOUR == 2
    host->log(host, SACORE_LOG_WARN, "crashing on purpose");
    *static_cast<volatile int*>(nullptr) = 1;
    return 0;
#else
    callbacks->shutdown = &Shutdown;
    return 0;
#endif
}

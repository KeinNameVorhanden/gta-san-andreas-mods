// Stat boost: every gradual stat gain goes through CStats::IncrementStat(stat, value) (101 / 105 call sites:
// sprinting, swimming, gym, driving, weapon hits). The hook scales `value` for the configured groups, only on the
// outermost call (IncrementStat calls itself for derived stats such as respect and max health), and caps one call
// at the skill maximum of 1000. Fat loss goes through DecrementStat.
#include <sacore/sacore.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
void(__cdecl* g_origIncrement)(uint16_t, float) = nullptr;
void(__cdecl* g_origDecrement)(uint16_t, float) = nullptr;
int g_depth = 0;  // game thread only

const char* GroupOf(uint16_t stat) {
    if (stat == 22 || stat == 23 || stat == 225) {
        return "body";  // stamina, muscle, lung capacity
    }
    if (stat == 160 || stat == 223 || stat == 229 || stat == 230) {
        return "vehicles";  // driving, flying, bike, cycling
    }
    if (stat >= 69 && stat <= 79) {
        return "weapons";
    }
    if (stat == 21) {
        return "fat_gain";
    }
    return nullptr;
}

float Scaled(const char* group, float value) {
    if (!group || !g_host->ConfigBool("enabled", true)) {
        return value;
    }
    const float factor = static_cast<float>(g_host->ConfigDouble(group, 1.0));
    return std::clamp(value * factor, -1000.0f, 1000.0f);
}

void __cdecl HookedIncrement(uint16_t stat, float value) {
    const float scaled = g_depth == 0 && value > 0.0f ? Scaled(GroupOf(stat), value) : value;
    ++g_depth;
    g_origIncrement(stat, scaled);
    --g_depth;
}

void __cdecl HookedDecrement(uint16_t stat, float value) {
    const float scaled = g_depth == 0 && stat == 21 && value > 0.0f ? Scaled("fat_loss", value) : value;
    ++g_depth;
    g_origDecrement(stat, scaled);
    --g_depth;
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (!g_host->Has("stats.scale")) {
        g_host->Warn("stat multipliers are not available on this build");
        return 0;
    }
    using StatFn = void __cdecl(uint16_t, float);
    if (!g_host->Hook<StatFn>("increment", "CStats::IncrementStat", "scale stat gains",
                              g_host->GameFunction<StatFn*>("CStats::IncrementStat"), &HookedIncrement,
                              &g_origIncrement) ||
        !g_host->Hook<StatFn>("decrement", "CStats::DecrementStat", "scale fat loss",
                              g_host->GameFunction<StatFn*>("CStats::DecrementStat"), &HookedDecrement,
                              &g_origDecrement)) {
        g_host->Error("could not hook the stat functions");
        return 1;
    }
    g_host->Info("ready: body x{}, vehicles x{}, weapons x{}, fat gain x{}, fat loss x{}",
                 g_host->ConfigDouble("body", 3.0), g_host->ConfigDouble("vehicles", 3.0),
                 g_host->ConfigDouble("weapons", 3.0), g_host->ConfigDouble("fat_gain", 1.0),
                 g_host->ConfigDouble("fat_loss", 1.0));
    return 0;
}

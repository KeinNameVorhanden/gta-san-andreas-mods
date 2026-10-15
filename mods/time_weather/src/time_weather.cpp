// Time and weather:
// - freeze: CClock::Update adds a minute only when now - ms_nLastClockTick exceeds ms_nMillisecondsPerGameMinute,
//   so holding ms_nLastClockTick at "now" every tick stops the clock (seconds included);
// - speed: ms_nMillisecondsPerGameMinute = game value / speed (scripts change the value too, so the game's own
//   value is re-read whenever it differs from what the mod wrote);
// - weather: CWeather::ForceWeatherNow(type) / ReleaseWeather(), re-asserted when a script changed it.
// Missions set their own time and weather; with yield_in_missions the mod steps back while one runs.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
uint32_t g_gameMinuteMs = 0;   // the game's value (0 = not read yet)
uint32_t g_writtenMinuteMs = 0;
int16_t g_forcedByUs = -1;

struct Weather {
    const char* label;
    int16_t type;  // eWeatherType
};
constexpr Weather kWeathers[] = {
    {"extra sunny", 0}, {"sunny", 1}, {"smog", 3}, {"cloudy", 4}, {"rain", 16},
    {"thunderstorm", 8}, {"fog", 9}, {"sandstorm", 19},
};

int16_t WantedWeather() {
    const std::string name = g_host->ConfigString("weather");
    for (const auto& [label, type] : kWeathers) {
        if (name == label) {
            return type;
        }
    }
    return -1;  // cycle
}

void SetClock(int hour, int minute) {
    *g_host->GameVariable<uint8_t>("CClock::ms_nGameClockHours") = static_cast<uint8_t>(hour);
    *g_host->GameVariable<uint8_t>("CClock::ms_nGameClockMinutes") = static_cast<uint8_t>(minute);
    *g_host->GameVariable<uint32_t>("CClock::ms_nLastClockTick") =
        *g_host->GameVariable<uint32_t>("CTimer::m_snTimeInMilliseconds");
    g_host->Info("clock set to {:02}:{:02}", hour, minute);
}

void RestoreClockSpeed() {
    auto* minuteMs = g_host->GameVariable<uint32_t>("CClock::ms_nMillisecondsPerGameMinute");
    if (g_gameMinuteMs && *minuteMs == g_writtenMinuteMs) {
        *minuteMs = g_gameMinuteMs;
    }
    g_gameMinuteMs = g_writtenMinuteMs = 0;
}

void ReleaseWeather() {
    if (g_forcedByUs >= 0) {
        g_host->GameFunction<void(__cdecl*)()>("CWeather::ReleaseWeather")();
        g_forcedByUs = -1;
    }
}

void Tick() {
    const bool active = g_host->ConfigBool("enabled", true) &&
                        !(g_host->ConfigBool("yield_in_missions", true) && g_game->OnMission());
    if (!active) {
        RestoreClockSpeed();
        ReleaseWeather();
        return;
    }
    if (g_host->Has("world.time")) {
        if (g_host->ConfigBool("freeze_time", false)) {
            *g_host->GameVariable<uint32_t>("CClock::ms_nLastClockTick") =
                *g_host->GameVariable<uint32_t>("CTimer::m_snTimeInMilliseconds");
        }
        const double speed = g_host->ConfigDouble("time_speed", 1.0);
        auto* minuteMs = g_host->GameVariable<uint32_t>("CClock::ms_nMillisecondsPerGameMinute");
        if (speed != 1.0) {
            if (*minuteMs != g_writtenMinuteMs) {
                g_gameMinuteMs = *minuteMs;  // first time, or a script changed it
            }
            g_writtenMinuteMs = static_cast<uint32_t>(g_gameMinuteMs / speed);
            if (g_writtenMinuteMs < 1) {
                g_writtenMinuteMs = 1;
            }
            *minuteMs = g_writtenMinuteMs;
        } else {
            RestoreClockSpeed();
        }
    }
    if (g_host->Has("world.weather")) {
        const int16_t wanted = WantedWeather();
        if (wanted < 0) {
            ReleaseWeather();
        } else if (*g_host->GameVariable<int16_t>("CWeather::ForcedWeatherType") != wanted) {
            g_host->GameFunction<void(__cdecl*)(int16_t)>("CWeather::ForceWeatherNow")(wanted);
            if (g_forcedByUs != wanted) {
                g_host->Info("weather locked to {} (type {})", g_host->ConfigString("weather"), wanted);
            }
            g_forcedByUs = wanted;
        }
    }
}

void ApplySetHour() {
    const int64_t hour = g_host->ConfigInt("set_hour", -1);
    if (hour < 0 || !g_host->Has("world.time") || g_host->GameState() != 9) {
        return;
    }
    SetClock(static_cast<int>(hour), 0);
    g_host->SetSetting(std::string(g_host->Id()), "set_hour", "-1");
}

void BindKeys() {
    g_host->ClearKeys();
    g_host->OnKey(static_cast<unsigned>(g_host->ConfigInt("hour_key", VK_ADD)), [] {
        if (!g_host->ConfigBool("enabled", true) || g_host->GameState() != 9 || !g_host->Has("world.time") ||
            g_game->OnMission()) {
            return;
        }
        const int hour = (*g_host->GameVariable<uint8_t>("CClock::ms_nGameClockHours") + 1) % 24;
        SetClock(hour, *g_host->GameVariable<uint8_t>("CClock::ms_nGameClockMinutes"));
        g_game->ShowHelp(("Time: " + std::to_string(hour) + ":00").c_str());
    });
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("world.time") && !g_host->Has("world.weather")) {
        g_host->Warn("time and weather controls are not available on this build");
        return 0;
    }
    BindKeys();
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) {
        ApplySetHour();
        Tick();
    });
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { BindKeys(); });
    g_host->On(SACORE_EVENT_LOAD_COMPLETED, [](const SacoreEvent&) {
        g_gameMinuteMs = g_writtenMinuteMs = 0;  // a load resets the clock values
        g_forcedByUs = -1;
    });
    g_host->Info("ready: freeze {}, speed {}, weather {}", g_host->ConfigBool("freeze_time", false),
                 g_host->ConfigDouble("time_speed", 1.0), g_host->ConfigString("weather"));
    return 0;
}

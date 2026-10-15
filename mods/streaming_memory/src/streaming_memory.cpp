// Streaming memory.
// CStreaming::Init2 sets the streaming budget CStreaming::ms_memoryAvailable to a hard-coded 50 MB and
// desiredNumVehiclesLoaded to 22 when a game starts; with that little memory distant objects and cars pop in late.
// The mod raises both once the game is running (and again whenever the game resets them) and, after a load, streams
// the area around CJ completely with CStreaming::LoadScene.
//
// Preload: every world model (with its textures) is requested once with GAME_REQUIRED (0x02), the
// flag that keeps a model out of the least-used list, so nothing seen once is unloaded again. It runs in the
// background, a batch per game tick while the request list is short, and stops when the free address space runs
// low. A new game or a load unloads world models again (CStreaming::ReInit), so the pass restarts after each one.
// The full set (about 980 MB of files) needs the 4 GB address space (Large Address Aware flag, tools/laa.py).
#include <sacore/sacore.hpp>

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<sacore::mod::Host> g_host;
bool g_loggedOriginal = false;
bool g_scenePending = false;

// CStreamingInfo (stride 0x14): +6 flags, +0xC size in 2048-byte sectors, +0x10 load state (1 = loaded).
constexpr size_t kInfoStride = 0x14;
constexpr uint8_t kGameRequired = 0x02;
constexpr int kDffCount = 20000;
constexpr int kRequestsInFlight = 64;  // keep the request list short (it is walked for every channel fill)
constexpr int kBatch = 96;
// Models, textures, collision, IPL and path files: everything the world streaming requests while CJ moves.
constexpr int kWorldResources = 25575;
constexpr uint8_t kLoadStateRequested = 2;

enum class Kind { World, Ped, Vehicle };

struct Preload {
    std::vector<int> ids;      // to pin in this pass
    size_t next = 0;
    std::vector<int> pinned;   // pinned so far (for undo and re-pinning)
    bool running = false;
    bool stopped = false;      // memory guard hit
    ULONGLONG started = 0;
    int reported = 0;          // last progress quarter logged
};
Preload g_preload;
ULONGLONG g_lastRepin = 0;

bool LargeAddressSpace() {
    MEMORYSTATUSEX status{sizeof(status)};
    return GlobalMemoryStatusEx(&status) && status.ullTotalVirtual > (3ull << 30);
}

// Free address space in the process and its largest free block (VirtualQuery walk).
void FreeAddressSpace(uint64_t& total, uint64_t& largest) {
    total = largest = 0;
    MEMORY_BASIC_INFORMATION info;
    for (uintptr_t address = 0x10000; VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info));) {
        if (info.State == MEM_FREE) {
            total += info.RegionSize;
            largest = info.RegionSize > largest ? info.RegionSize : largest;
        }
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= address) {
            break;
        }
        address = next;
    }
}

std::string Mode() {
    const std::string mode = g_host->ConfigString("preload");
    if (mode == "auto") {
        return LargeAddressSpace() ? "world" : "off";
    }
    return mode;
}

Kind KindOf(int id) {
    if (id >= 1 && id <= 289) {
        return Kind::Ped;
    }
    if (id >= 400 && id <= 611) {
        return Kind::Vehicle;
    }
    return Kind::World;
}

uint8_t* Info(int id) {
    return reinterpret_cast<uint8_t*>(g_host->Symbol("CStreaming::ms_aInfoForModel")) + static_cast<size_t>(id) * kInfoStride;
}

void BuildList(const std::string& mode) {
    g_preload = {};
    auto* const* models = g_host->GameVariable<void*>("CModelInfo::ms_modelInfoPtrs");
    for (int id = 1; id < kDffCount; ++id) {
        // 290..319 and 384..393 are renamed per mission and cutscene (special characters, cutscene objects).
        if ((id >= 290 && id <= 319) || (id >= 384 && id <= 393) || !models[id]) {
            continue;
        }
        const uint8_t* info = Info(id);
        if (*reinterpret_cast<const uint32_t*>(info + 0xC) == 0) {
            continue;  // no file in an IMG archive
        }
        const Kind kind = KindOf(id);
        if (kind == Kind::World || mode == "everything") {
            g_preload.ids.push_back(id);
        }
    }
    // In archive order (+7 archive, +8 position in sectors): a channel reads up to 16 requested files that follow each
    // other on the disc in one go, but in id order nearly every read was a single file (about 50 models a second).
    std::sort(g_preload.ids.begin(), g_preload.ids.end(), [](int a, int b) {
        const uint8_t* x = Info(a);
        const uint8_t* y = Info(b);
        const auto key = [](const uint8_t* info) {
            return (static_cast<uint64_t>(info[7]) << 32) | *reinterpret_cast<const uint32_t*>(info + 8);
        };
        return key(x) < key(y);
    });
    g_preload.running = !g_preload.ids.empty();
    g_preload.started = GetTickCount64();
    g_host->Info("preload ({}): {} models to load and keep, about {} MB of files", mode, g_preload.ids.size(), [] {
        uint64_t sectors = 0;
        for (int id : g_preload.ids) {
            sectors += *reinterpret_cast<const uint32_t*>(Info(id) + 0xC);
        }
        return sectors * 2048 >> 20;  // textures come with their models on top of this
    }());
}

// The game reads the request nearest on the disc next, not the oldest one, so preload requests in the list can keep
// a mission's model waiting for a long time (the opening mission did not start within 30 s). The preload therefore
// adds requests only while the game has none of its own: those are the requested entries without GAME_REQUIRED.
bool GameRequestsPending() {
    const uint8_t* info = Info(0);
    for (int id = 0; id < kWorldResources; ++id, info += kInfoStride) {
        if (info[0x10] == kLoadStateRequested && !(info[6] & kGameRequired)) {
            return true;
        }
    }
    return false;
}

void Unpin() {
    const auto deletable = g_host->GameFunction<void(__cdecl*)(int)>("CStreaming::SetModelIsDeletable");
    for (int id : g_preload.pinned) {
        if (Info(id)[6] & kGameRequired) {
            deletable(id);
        }
    }
    g_host->Info("preload off: {} models may be unloaded again", g_preload.pinned.size());
    g_preload = {};
}

void PreloadTick() {
    const std::string mode = Mode();
    if (mode == "off" || !g_host->Has("streaming.preload")) {
        if (!g_preload.pinned.empty() || g_preload.running) {
            Unpin();
        }
        return;
    }
    if (!g_preload.running && g_preload.ids.empty() && !g_preload.stopped) {
        BuildList(mode);
    }
    const auto request = g_host->GameFunction<void(__cdecl*)(int, int)>("CStreaming::RequestModel");
    if (g_preload.running) {
        const int inFlight = *g_host->GameVariable<int>("CStreaming::ms_numModelsRequested");
        const bool gameFirst = inFlight < kRequestsInFlight && GameRequestsPending();
        for (int n = 0; !gameFirst && n < kBatch && inFlight + n < kRequestsInFlight && g_preload.next < g_preload.ids.size();
             ++n) {
            const int id = g_preload.ids[g_preload.next++];
            request(id, kGameRequired);  // also requests its TXD (and the parent TXD) with the same flag
            g_preload.pinned.push_back(id);
        }
        if (g_preload.next % 512 < static_cast<size_t>(kBatch)) {
            uint64_t free = 0, largest = 0;
            FreeAddressSpace(free, largest);
            const uint64_t reserve = static_cast<uint64_t>(g_host->ConfigInt("preload_reserve_mb", 600)) << 20;
            if (free < reserve || largest < (64ull << 20)) {
                g_preload.running = false;
                g_preload.stopped = true;
                g_host->Warn("preload stopped at {} of {} models: {} MB address space left (largest block {} MB)",
                             g_preload.next, g_preload.ids.size(), free >> 20, largest >> 20);
                return;
            }
        }
        const int quarter = static_cast<int>(g_preload.next * 4 / g_preload.ids.size());
        if (quarter > g_preload.reported) {
            g_preload.reported = quarter;
            uint64_t free = 0, largest = 0;
            FreeAddressSpace(free, largest);
            g_host->Info("preload {}%: {} models after {:.0f} s, streaming memory {} MB, address space left {} MB",
                         quarter * 25, g_preload.next, (GetTickCount64() - g_preload.started) / 1000.0,
                         *g_host->GameVariable<uint32_t>("CStreaming::ms_memoryUsed") >> 20, free >> 20);
        }
        if (g_preload.next >= g_preload.ids.size()) {
            g_preload.running = false;
        }
        return;
    }
    // Peds and vehicles: the game's own population code makes them deletable again; pin them every 30 s.
    if (mode == "everything" && GetTickCount64() - g_lastRepin > 30000) {
        g_lastRepin = GetTickCount64();
        int repinned = 0;
        for (int id : g_preload.pinned) {
            if (KindOf(id) != Kind::World && !(Info(id)[6] & kGameRequired) && repinned < kBatch) {
                request(id, kGameRequired);
                ++repinned;
            }
        }
    }
}

void Tick() {
    if (!g_host->ConfigBool("enabled", true)) {
        return;
    }
    auto* available = g_host->GameVariable<uint32_t>("CStreaming::ms_memoryAvailable");
    // The preload needs the budget above its size, or every loaded file starts an eviction pass (preload.md 3.1).
    const int64_t budget = Mode() == "off" ? g_host->ConfigInt("memory_mb", 256)
                                           : std::max<int64_t>(g_host->ConfigInt("memory_mb", 256), 1536);
    const auto wanted = static_cast<uint32_t>(budget) << 20;
    if (*available < wanted) {
        if (!g_loggedOriginal) {
            g_host->Info("streaming memory {} MB -> {} MB", *available >> 20, wanted >> 20);
            g_loggedOriginal = true;
        }
        *available = wanted;
    }
    const auto vehicles = static_cast<uint32_t>(g_host->ConfigInt("vehicle_models", 0));
    auto* desired = g_host->GameVariable<uint32_t>("CStreaming::desiredNumVehiclesLoaded");
    if (vehicles > 0 && *desired != vehicles) {
        *desired = vehicles;
    }
    if (g_scenePending && g_host->ConfigBool("preload_after_load", true)) {
        g_scenePending = false;
        if (const auto player = g_host->Player(); player && player->area == 0) {
            g_host->GameFunction<void(__cdecl*)(const float*)>("CStreaming::LoadScene")(player->position);
            g_host->Info("area around CJ streamed in");
        }
    }
    PreloadTick();
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    if (!g_host->Has("streaming.memory") || !g_host->Symbol("CStreaming::LoadScene")) {
        g_host->Warn("streaming settings are not available on this build");
        return 0;
    }
    g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { Tick(); });
    g_host->On(SACORE_EVENT_LOAD_COMPLETED, [](const SacoreEvent& e) {
        g_scenePending = e.success != 0;
        g_loggedOriginal = false;
        g_preload = {};  // CStreaming::ReInit unloaded the world models: preload again
    });
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent& e) {
        if (e.text && std::string(e.text) == "preload" && Mode() != "off") {
            g_preload = {};
        }
    });
    g_host->Info("ready: {} MB, vehicle models {}, preload {} ({} address space)", g_host->ConfigInt("memory_mb", 256),
                 g_host->ConfigInt("vehicle_models", 0), Mode(), LargeAddressSpace() ? "4 GB" : "2 GB");
    return 0;
}

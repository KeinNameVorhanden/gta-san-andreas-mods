// Radar zoom and GPS.
// Zoom: CRadar::DrawMap computes CRadar::m_radarRange (180 on foot, 340..350 in vehicles) and calls DrawRadarMap;
// blips and the world-to-radar transform read the same value later in the frame, so scaling it in a DrawRadarMap
// hook zooms map and blips together.
// GPS: the road graph comes from data\Paths\NODES0..63.DAT (vehicle nodes and their links, identical files on both
// builds); the route from the nearest node to CJ to the nearest node to the map waypoint is an A* search in the mod,
// redone once a second. It is drawn on the radar after the frame: world -> radar space -> screen with the game's own
// CRadar transforms, clipped to the radar circle.
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <d3d9.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace {

struct Vec2 {
    float x, y;
};

struct Node {
    float x, y, z;
    uint32_t firstEdge, edgeCount;
};

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
void(__cdecl* g_origDrawRadarMap)() = nullptr;

std::vector<Node> g_nodes;
std::vector<uint32_t> g_edges;
std::vector<uint32_t> g_route;  // node indices, from CJ to the waypoint
float g_routeTarget[2] = {0, 0};
ULONGLONG g_lastRoute = 0;

void __cdecl HookedDrawRadarMap() {
    if (g_host->ConfigBool("enabled", true)) {
        *g_host->GameVariable<float>("CRadar::m_radarRange") *=
            static_cast<float>(std::clamp(g_host->ConfigDouble("zoom", 1.0), 0.5, 2.0));
    }
    g_origDrawRadarMap();
}

// NODESn.DAT: header (nodes, vehicle nodes, ped nodes, navi nodes, links), 28-byte path nodes (vehicle nodes first:
// int16 x/y/z * 8 at +8, link index at +16, link count in the low 4 bits of the flags at +24), 14-byte navi nodes,
// then 4-byte links (area, node). Checked against all 64 files (links of vehicle nodes always hit vehicle nodes).
bool LoadGraph() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path dir = std::filesystem::path(exe).parent_path() / L"data" / L"Paths";
    struct Area {
        std::vector<uint8_t> data;
        uint32_t vehicleNodes = 0, firstIndex = 0;
    };
    std::vector<Area> areas(64);
    uint32_t total = 0;
    for (int i = 0; i < 64; ++i) {
        std::ifstream file(dir / (L"NODES" + std::to_wstring(i) + L".DAT"), std::ios::binary);
        areas[i].data.assign(std::istreambuf_iterator<char>(file), {});
        if (areas[i].data.size() < 20) {
            g_host->Warn("GPS: {} missing or empty", (dir / (L"NODES" + std::to_wstring(i) + L".DAT")).string());
            return false;
        }
        std::memcpy(&areas[i].vehicleNodes, areas[i].data.data() + 4, 4);
        areas[i].firstIndex = total;
        total += areas[i].vehicleNodes;
    }
    g_nodes.reserve(total);
    for (int i = 0; i < 64; ++i) {
        const uint8_t* d = areas[i].data.data();
        uint32_t header[5];
        std::memcpy(header, d, sizeof(header));
        const size_t links = 20 + static_cast<size_t>(header[0]) * 28 + static_cast<size_t>(header[3]) * 14;
        for (uint32_t k = 0; k < areas[i].vehicleNodes; ++k) {
            const uint8_t* n = d + 20 + static_cast<size_t>(k) * 28;
            int16_t pos[3];
            uint16_t linkIndex;
            uint32_t flags;
            std::memcpy(pos, n + 8, 6);
            std::memcpy(&linkIndex, n + 16, 2);
            std::memcpy(&flags, n + 24, 4);
            Node node{pos[0] / 8.0f, pos[1] / 8.0f, pos[2] / 8.0f, static_cast<uint32_t>(g_edges.size()), 0};
            for (uint32_t j = 0; j < (flags & 0xF); ++j) {
                const size_t at = links + (static_cast<size_t>(linkIndex) + j) * 4;
                if (at + 4 > areas[i].data.size()) {
                    break;
                }
                uint16_t area, id;
                std::memcpy(&area, d + at, 2);
                std::memcpy(&id, d + at + 2, 2);
                if (area < 64 && id < areas[area].vehicleNodes) {
                    g_edges.push_back(areas[area].firstIndex + id);
                    ++node.edgeCount;
                }
            }
            g_nodes.push_back(node);
        }
    }
    g_host->Info("GPS: road graph with {} nodes and {} links", g_nodes.size(), g_edges.size());
    return !g_nodes.empty();
}

uint32_t Nearest(float x, float y) {
    uint32_t best = 0;
    float bestDistance = 1e30f;
    for (uint32_t i = 0; i < g_nodes.size(); ++i) {
        const float dx = g_nodes[i].x - x, dy = g_nodes[i].y - y;
        const float d = dx * dx + dy * dy;
        if (d < bestDistance && g_nodes[i].edgeCount > 0) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

float Distance(uint32_t a, uint32_t b) {
    const Node &p = g_nodes[a], &q = g_nodes[b];
    return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z));
}

std::vector<uint32_t> FindRoute(uint32_t start, uint32_t goal) {
    std::vector<float> cost(g_nodes.size(), 1e30f);
    std::vector<uint32_t> from(g_nodes.size(), UINT32_MAX);
    using Entry = std::pair<float, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    cost[start] = 0.0f;
    open.push({Distance(start, goal), start});
    while (!open.empty()) {
        const uint32_t current = open.top().second;
        open.pop();
        if (current == goal) {
            break;
        }
        const Node& node = g_nodes[current];
        for (uint32_t e = node.firstEdge; e < node.firstEdge + node.edgeCount; ++e) {
            const uint32_t next = g_edges[e];
            const float c = cost[current] + Distance(current, next);
            if (c < cost[next]) {
                cost[next] = c;
                from[next] = current;
                open.push({c + Distance(next, goal), next});
            }
        }
    }
    std::vector<uint32_t> route;
    if (cost[goal] >= 1e30f) {
        return route;
    }
    for (uint32_t at = goal; at != UINT32_MAX; at = from[at]) {
        route.push_back(at);
    }
    std::reverse(route.begin(), route.end());
    return route;
}

// The map waypoint: FrontEndMenuManager.m_nTargetBlipIndex (+0x2C) = index | counter << 16 of a coordinate blip.
bool Waypoint(float out[2]) {
    const uintptr_t menu = g_host->Symbol("FrontEndMenuManager");
    const uintptr_t traces = g_host->Symbol("CRadar::ms_RadarTrace");
    if (!menu || !traces) {
        return false;
    }
    const uint32_t handle = *reinterpret_cast<const uint32_t*>(menu + 0x2C);
    const uint32_t index = handle & 0xFFFF;
    if (handle == 0 || index >= 175) {
        return false;
    }
    const uint8_t* trace = reinterpret_cast<const uint8_t*>(traces + index * 0x28);
    if (*reinterpret_cast<const uint16_t*>(trace + 0x14) != (handle >> 16) || !(trace[0x25] & 2)) {
        return false;
    }
    out[0] = *reinterpret_cast<const float*>(trace + 0x08);
    out[1] = *reinterpret_cast<const float*>(trace + 0x0C);
    return true;
}

void UpdateRoute() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastRoute < 1000) {
        return;
    }
    g_lastRoute = now;
    float target[2];
    const auto player = g_host->Player();
    if (!g_host->ConfigBool("enabled", true) || !g_host->ConfigBool("gps", true) || g_nodes.empty() || !player ||
        player->area != 0 || !Waypoint(target)) {
        g_route.clear();
        return;
    }
    const uint32_t start = Nearest(player->position[0], player->position[1]);
    const uint32_t goal = Nearest(target[0], target[1]);
    const bool newTarget = target[0] != g_routeTarget[0] || target[1] != g_routeTarget[1];
    g_route = FindRoute(start, goal);
    if (newTarget) {
        g_routeTarget[0] = target[0];
        g_routeTarget[1] = target[1];
        g_host->Info("GPS: route to ({:.0f}, {:.0f}) with {} nodes", target[0], target[1], g_route.size());
    }
}

Vec2 ToRadar(float x, float y) {
    Vec2 in{x, y}, out{};
    g_host->GameFunction<void(__cdecl*)(Vec2*, const Vec2*)>("CRadar::TransformRealWorldPointToRadarSpace")(&out, &in);
    return out;
}

Vec2 ToScreen(Vec2 radar) {
    Vec2 out{};
    g_host->GameFunction<void(__cdecl*)(Vec2*, const Vec2*)>("CRadar::TransformRadarPointToScreenSpace")(&out, &radar);
    return out;
}

// Clips the segment a-b to the radar circle (radius 1 in radar space); false when it lies outside.
bool ClipToCircle(Vec2& a, Vec2& b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float qa = dx * dx + dy * dy, qb = 2 * (a.x * dx + a.y * dy), qc = a.x * a.x + a.y * a.y - 1.0f;
    if (qa < 1e-9f) {
        return qc <= 0.0f;
    }
    const float disc = qb * qb - 4 * qa * qc;
    if (disc < 0) {
        return false;
    }
    const float root = std::sqrt(disc);
    const float t0 = std::max(0.0f, (-qb - root) / (2 * qa)), t1 = std::min(1.0f, (-qb + root) / (2 * qa));
    if (t0 > t1) {
        return false;
    }
    const Vec2 start{a.x + dx * t0, a.y + dy * t0};
    b = {a.x + dx * t1, a.y + dy * t1};
    a = start;
    return true;
}

struct Vertex {
    float x, y, z, rhw;
    D3DCOLOR colour;
};

void DrawRoute() {
    if (g_route.size() < 2 || g_host->GameState() != 9 || g_game->CutsceneRunning()) {
        return;
    }
    const uintptr_t menu = g_host->Symbol("FrontEndMenuManager");
    if (menu && (*reinterpret_cast<const bool*>(menu + 0x5C) || *reinterpret_cast<const int*>(menu + 0x24) == 2)) {
        return;  // pause menu or map open, or the radar switched off in the display options
    }
    auto* device = static_cast<IDirect3DDevice9*>(g_host->Raw()->d3d_device(g_host->Raw()));
    if (!device) {
        return;
    }
    D3DVIEWPORT9 viewport{};
    device->GetViewport(&viewport);
    const float half = static_cast<float>(g_host->ConfigDouble("gps_width", 3.0)) * viewport.Height / 720.0f / 2.0f;
    const D3DCOLOR colour = D3DCOLOR_ARGB(230, 190, 80, 255);
    std::vector<Vertex> vertices;
    vertices.reserve(g_route.size() * 6);
    for (size_t i = 0; i + 1 < g_route.size(); ++i) {
        Vec2 a = ToRadar(g_nodes[g_route[i]].x, g_nodes[g_route[i]].y);
        Vec2 b = ToRadar(g_nodes[g_route[i + 1]].x, g_nodes[g_route[i + 1]].y);
        if (!ClipToCircle(a, b)) {
            continue;
        }
        const Vec2 p = ToScreen(a), q = ToScreen(b);
        const float dx = q.x - p.x, dy = q.y - p.y, length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.01f) {
            continue;
        }
        const float nx = -dy / length * half, ny = dx / length * half;
        const Vertex v0{p.x + nx, p.y + ny, 0, 1, colour}, v1{p.x - nx, p.y - ny, 0, 1, colour};
        const Vertex v2{q.x + nx, q.y + ny, 0, 1, colour}, v3{q.x - nx, q.y - ny, 0, 1, colour};
        vertices.insert(vertices.end(), {v0, v1, v2, v2, v1, v3});
    }
    if (vertices.empty()) {
        return;
    }
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) {
        return;
    }
    if (SUCCEEDED(device->BeginScene())) {
        device->SetTexture(0, nullptr);
        device->SetPixelShader(nullptr);
        device->SetVertexShader(nullptr);
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertices.size() / 3), vertices.data(),
                                sizeof(Vertex));
        device->EndScene();
    }
    saved->Apply();
    saved->Release();
}

void BindKeys() {
    g_host->ClearKeys();
    const auto key = static_cast<unsigned>(g_host->ConfigInt("zoom_key", 0));
    if (key == 0) {
        return;
    }
    g_host->OnKey(key, [] {
        const double zoom = g_host->ConfigDouble("zoom", 1.0);
        const double next = zoom < 1.25 && zoom >= 0.9 ? 1.5 : zoom < 1.75 && zoom >= 1.25 ? 2.0 : zoom >= 1.75 ? 0.75 : 1.0;
        g_host->SetSetting(std::string(g_host->Id()), "zoom", std::to_string(next));
    });
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (g_host->Has("radar.zoom")) {
        using DrawFn = void __cdecl();
        if (!g_host->Hook<DrawFn>("draw_radar_map", "CRadar::DrawRadarMap", "radar zoom",
                                  g_host->GameFunction<DrawFn*>("CRadar::DrawRadarMap"), &HookedDrawRadarMap,
                                  &g_origDrawRadarMap)) {
            g_host->Warn("radar zoom hook not installed");
        }
    }
    if (g_host->Symbol("CRadar::TransformRealWorldPointToRadarSpace") &&
        g_host->Symbol("CRadar::TransformRadarPointToScreenSpace") && LoadGraph()) {
        g_host->On(SACORE_EVENT_GAME_TICK, [](const SacoreEvent&) { UpdateRoute(); });
        g_host->On(SACORE_EVENT_FRAME, [](const SacoreEvent&) { DrawRoute(); });
    } else {
        g_host->Warn("GPS is not available on this build");
    }
    BindKeys();
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) { BindKeys(); });
    g_host->Info("ready: zoom {}, GPS {}", g_host->ConfigDouble("zoom", 1.0), g_host->ConfigBool("gps", true));
    return 0;
}

// Weapon and radio wheel. Holding the wheel key opens a
// wheel in slow motion (CTimer::ms_fTimeScale): on foot with the weapons CJ carries (CPed::m_aWeapons, +0x5A0,
// 13 slots of 0x1C), in a vehicle with the radio stations. The mouse picks a direction; the camera stands still
// meanwhile because the camera's mouse factors (CCamera::m_fMouseAccelHorzntl / Vertical) are 0 while the wheel is
// open. Releasing the key selects: CPed::SetCurrentWeapon(slot), which also updates the player's chosen weapon, or
// CAudioEngine::RetuneRadio(station). Drawn with ImGui and the game's own fonts (sacore_gta_ui).
#include <sacore/gta_ui.h>
#include <sacore/sa_game.hpp>
#include <sacore/sacore.hpp>

#include <imgui.h>
#include <imgui_impl_dx9.h>

#include <d3d9.h>
#include <windows.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace ui = sacore::gta_ui;

struct Entry {
    int value;  // weapon slot or radio station
    std::string label;
};

constexpr const char* kWeaponNames[] = {
    "Fist", "Brass knuckles", "Golf club", "Nightstick", "Knife", "Baseball bat", "Shovel", "Pool cue", "Katana",
    "Chainsaw", "Dildo", "Dildo", "Vibrator", "Vibrator", "Flowers", "Cane", "Grenades", "Tear gas", "Molotovs",
    "Rocket", "Rocket", "Bomb", "Pistol", "Silenced pistol", "Desert Eagle", "Shotgun", "Sawn-off", "Combat shotgun",
    "Micro SMG", "SMG", "AK-47", "M4", "Tec-9", "Rifle", "Sniper rifle", "Rocket launcher", "Heat-seeker",
    "Flamethrower", "Minigun", "Satchel charges", "Detonator", "Spray can", "Extinguisher", "Camera",
    "Night vision", "Thermal vision", "Parachute",
};

// eRadioID order of the menu's station scroller (1 .. 13).
constexpr const char* kStations[] = {
    "Playback FM", "K-Rose", "K-DST", "Bounce FM", "SF-UR", "Radio Los Santos", "Radio X", "CSR 103.9",
    "K-Jah West", "Master Sounds 98.3", "WCTR", "User Tracks", "Radio off",
};

std::unique_ptr<sacore::mod::Host> g_host;
std::unique_ptr<sacore::sa::Game> g_game;
std::filesystem::path g_gameDirectory;
bool g_ready = false;
bool g_open = false;
bool g_radio = false;
std::vector<Entry> g_entries;
int g_selected = -1;
float g_mouseX = 0.0f, g_mouseY = 0.0f;
float g_savedTimeScale = 1.0f;
float g_savedAccel[2] = {0.0f, 0.0f};

std::vector<Entry> Weapons() {
    std::vector<Entry> entries;
    const uint8_t* ped = g_game->PlayerPed();
    if (!ped) {
        return entries;
    }
    for (int slot = 0; slot < 13; ++slot) {
        const uint8_t* weapon = ped + 0x5A0 + slot * 0x1C;
        const int type = *reinterpret_cast<const int*>(weapon);
        const int ammo = *reinterpret_cast<const int*>(weapon + 0x0C);
        if ((type == 0 && slot != 0) || type < 0 || type >= static_cast<int>(std::size(kWeaponNames))) {
            continue;
        }
        std::string label = kWeaponNames[type];
        if (type >= 16 && ammo > 0) {
            label += " " + std::to_string(ammo);
        }
        entries.push_back({slot, label});
    }
    return entries;
}

std::vector<Entry> Stations() {
    std::vector<Entry> entries;
    for (int id = 1; id <= 13; ++id) {
        entries.push_back({id, kStations[id - 1]});
    }
    return entries;
}

void Open() {
    g_radio = g_game->InVehicle();
    if (g_radio && !g_host->Has("audio.radio")) {
        return;
    }
    g_entries = g_radio ? Stations() : Weapons();
    if (g_entries.empty()) {
        return;
    }
    g_open = true;
    g_selected = -1;
    g_mouseX = g_mouseY = 0.0f;
    auto* timeScale = g_host->GameVariable<float>("CTimer::ms_fTimeScale");
    g_savedTimeScale = *timeScale;
    *timeScale = static_cast<float>(g_host->ConfigDouble("slow_motion", 0.25));
    auto* horizontal = g_host->GameVariable<float>("CCamera::m_fMouseAccelHorzntl");
    auto* vertical = g_host->GameVariable<float>("CCamera::m_fMouseAccelVertical");
    g_savedAccel[0] = *horizontal;
    g_savedAccel[1] = *vertical;
    *horizontal = *vertical = 0.0f;
}

void Close(bool apply) {
    if (!g_open) {
        return;
    }
    g_open = false;
    *g_host->GameVariable<float>("CTimer::ms_fTimeScale") = g_savedTimeScale;
    *g_host->GameVariable<float>("CCamera::m_fMouseAccelHorzntl") = g_savedAccel[0];
    *g_host->GameVariable<float>("CCamera::m_fMouseAccelVertical") = g_savedAccel[1];
    if (!apply || g_selected < 0) {
        return;
    }
    const Entry& entry = g_entries[g_selected];
    if (g_radio) {
        auto* engine = reinterpret_cast<void*>(g_host->Symbol("AudioEngine"));
        g_host->GameFunction<void(__thiscall*)(void*, signed char)>("CAudioEngine::RetuneRadio")(
            engine, static_cast<signed char>(entry.value));
        g_host->Info("radio: {}", entry.label);
    } else if (uint8_t* ped = g_game->PlayerPed()) {
        g_host->GameFunction<void(__thiscall*)(void*, int)>("CPed::SetCurrentWeapon")(ped, entry.value);
        g_host->Info("weapon: {} (slot {})", entry.label, entry.value);
    }
}

void Draw(IDirect3DDevice9* device) {
    D3DVIEWPORT9 viewport{};
    device->GetViewport(&viewport);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(viewport.Width), static_cast<float>(viewport.Height));
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const float scale = viewport.Height / 720.0f * static_cast<float>(g_host->ConfigDouble("size", 1.0));
    const ImVec2 centre(viewport.Width * 0.5f, viewport.Height * 0.5f);
    const float outer = 230.0f * scale, inner = 90.0f * scale;
    const auto count = static_cast<int>(g_entries.size());
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i < count; ++i) {
        // Entry 0 at the top, clockwise.
        const float a0 = kPi / 2 - (i - 0.5f) * 2 * kPi / count, a1 = kPi / 2 - (i + 0.5f) * 2 * kPi / count;
        const ImU32 fill = i == g_selected ? IM_COL32(172, 203, 241, 210) : IM_COL32(0, 0, 0, 170);
        // A ring sector is not convex: fill it as thin quads.
        constexpr int kSteps = 8;
        for (int s = 0; s < kSteps; ++s) {
            const float t0 = a0 + (a1 - a0) * s / kSteps, t1 = a0 + (a1 - a0) * (s + 1) / kSteps;
            auto point = [&](float t, float r) { return ImVec2(centre.x + std::cos(t) * r, centre.y - std::sin(t) * r); };
            list->AddQuadFilled(point(t0, outer), point(t1, outer), point(t1, inner), point(t0, inner), fill);
        }
        const float mid = (a0 + a1) / 2, radius = (outer + inner) / 2;
        const ImVec2 at(centre.x + std::cos(mid) * radius, centre.y - std::sin(mid) * radius);
        const float height = 17.0f * scale;
        const ImVec2 size = ui::MeasureText(ui::Font::Subtitles, g_entries[i].label, height);
        ui::DrawText(list, ImVec2(at.x - size.x / 2, at.y - size.y / 2), ui::Font::Subtitles, g_entries[i].label,
                     height, i == g_selected ? ui::colour::Black : ui::colour::White, i != g_selected);
    }
    const char* title = g_radio ? "RADIO" : "WEAPONS";
    const ImVec2 titleSize = ui::MeasureText(ui::Font::Menu, title, 24.0f * scale);
    ui::DrawText(list, ImVec2(centre.x - titleSize.x / 2, centre.y - titleSize.y / 2), ui::Font::Menu, title,
                 24.0f * scale, ui::colour::LightBlue);
    ImGui::EndFrame();
    if (SUCCEEDED(device->BeginScene())) {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }
}

void OnFrame(const SacoreEvent& event) {
    auto* device = static_cast<IDirect3DDevice9*>(event.device);
    if (!device) {
        return;
    }
    if (!g_ready) {
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ui::LoadFonts(device, g_gameDirectory);
        ImGui_ImplDX9_Init(device);
        g_ready = true;
    }
    const bool usable = g_host->ConfigBool("enabled", true) && g_host->GameState() == 9 && g_game->PlayerState() == 0 &&
                        !g_game->CutsceneRunning() && !g_game->ControlsDisabled();
    const bool held = usable && (GetAsyncKeyState(static_cast<int>(g_host->ConfigInt("wheel_key", VK_CAPITAL))) & 0x8000);
    if (held && !g_open) {
        Open();
    } else if (!held && g_open) {
        Close(usable);
    }
    if (!g_open) {
        return;
    }
    // Direction from the mouse movement since the wheel opened (Y up is positive in the game's mouse state).
    if (const auto* mouse = reinterpret_cast<const float*>(g_host->Symbol("CPad::NewMouseControllerState"))) {
        g_mouseX += mouse[3];
        g_mouseY += mouse[4];
        const float length = std::sqrt(g_mouseX * g_mouseX + g_mouseY * g_mouseY);
        if (length > 40.0f) {
            g_mouseX *= 40.0f / length;  // keep the vector short so a new direction wins quickly
            g_mouseY *= 40.0f / length;
        }
        if (length > 12.0f) {
            constexpr float kPi = 3.14159265f;
            const float angle = std::atan2(g_mouseY, g_mouseX);              // 0 = right, counter-clockwise
            float fromTop = kPi / 2 - angle;                                   // clockwise from the top
            const auto count = static_cast<float>(g_entries.size());
            fromTop = std::fmod(fromTop + 2 * kPi + kPi / count, 2 * kPi);
            g_selected = static_cast<int>(fromTop / (2 * kPi / count)) % static_cast<int>(g_entries.size());
        }
    }
    Draw(device);
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_game = std::make_unique<sacore::sa::Game>(*g_host);
    if (!g_host->Has("player.weapon_select") || !g_host->Symbol("CCamera::m_fMouseAccelHorzntl") ||
        !g_host->Symbol("CPad::NewMouseControllerState")) {
        g_host->Warn("the wheel is not available on this build");
        return 0;
    }
    g_gameDirectory = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(raw->mod_directory)))
                          .parent_path()
                          .parent_path()
                          .parent_path();
    g_host->On(SACORE_EVENT_FRAME, &OnFrame);
    g_host->On(SACORE_EVENT_DEVICE_RESET, [](const SacoreEvent& e) {
        if (!g_ready) {
            return;
        }
        if (e.before_reset) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        } else if (e.success) {
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    });
    g_host->On(SACORE_EVENT_GAME_STATE_CHANGED, [](const SacoreEvent&) { Close(false); });
    g_host->On(SACORE_EVENT_SHUTDOWN, [](const SacoreEvent&) {
        Close(false);
        if (g_ready) {
            ImGui_ImplDX9_Shutdown();
            ui::ReleaseFonts();
            ImGui::DestroyContext();
            g_ready = false;
        }
    });
    g_host->Info("ready: hold key {} for the wheel", g_host->ConfigInt("wheel_key", VK_CAPITAL));
    return 0;
}

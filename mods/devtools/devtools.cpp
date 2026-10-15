// SACore developer console: an in-game overlay built only on the public mod API.
// Optional: removing the mod changes nothing else. Toggle with the key set in [config] toggle_key (default F11).

#include <sacore/sacore.hpp>

#include <windows.h>
#include <d3d9.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <sacore/gta_ui.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <format>
#include <cstdio>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace {

namespace fs = std::filesystem;
using sacore::mod::Host;

enum class View { Beginner = 0, Advanced = 1, Expert = 2 };

struct Tab {
    const char* name;
    View minimumView;
    void (*draw)();
};

std::unique_ptr<Host> g_host;
fs::path g_gameDirectory;
std::string g_iniPath;
bool g_ready = false;
bool g_visible = false;
View g_view = View::Advanced;
UINT g_toggleKey = VK_F11;
bool g_selfTest = false;
int g_selfTestTab = -1;
int g_selectedTab = 0;
// UI scale: the console follows the game resolution (720 px high = 1.0) times the user's [config] ui_scale.
ImGuiStyle g_baseStyle;
float g_userScale = 1.0f;
float g_scale = 0.0f;
bool g_layoutDirty = true;
ImVec4 g_lastRect;  // console x, y, width, height of the last frame (self-test report)
uint64_t g_selfTestFrame = 0;
uint64_t g_renderedFrames = 0;
std::array<float, 240> g_frameMs{};
size_t g_frameIndex = 0;
std::chrono::steady_clock::time_point g_lastFrame;
std::string g_selectedCrash;
std::string g_bundleMessage;

const char* StateName(int state) {
    static constexpr std::array kNames = {"start-up",   "init logo movie", "logo movie", "init intro movie",
                                          "intro movie", "init once",      "init menu",  "main menu",
                                          "loading",    "playing"};
    return state >= 0 && state < static_cast<int>(kNames.size()) ? kNames[static_cast<size_t>(state)] : "unknown";
}

std::string ReadFile(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

void Lines(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        ImGui::TextUnformatted(line.c_str());
    }
}

void DrawOverview() {
    const float average = [] {
        float sum = 0;
        for (float ms : g_frameMs) {
            sum += ms;
        }
        return sum / static_cast<float>(g_frameMs.size());
    }();
    ImGui::Text("SACore %s, game build %s", g_host->Raw()->core_version, g_host->Raw()->game_build);
    ImGui::Text("Game state: %s (%d)", StateName(g_host->GameState()), g_host->GameState());
    ImGui::Text("Frame time: %.1f ms average (%.0f fps)", average, average > 0 ? 1000.0f / average : 0.0f);
    ImGui::Text("Toggle this window with F%u; view level in sacore\\config\\mods\\devtools.toml", g_toggleKey - VK_F1 + 1);
}

void DrawPlayer() {
    const auto player = g_host->Player();
    if (!player) {
        ImGui::TextUnformatted("No player (main menu or loading), or player.read is unavailable on this build.");
        return;
    }
    ImGui::Text("Health %.0f   Armour %.0f   Money $%d   Wanted %d", player->health, player->armour, player->money,
                player->wanted_level);
    ImGui::Text("Position %.1f, %.1f, %.1f   Interior %d", player->position[0], player->position[1],
                player->position[2], player->area);
}

void DrawText(const char* kind) {
    ImGui::BeginChild(kind, ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    Lines(g_host->Diagnostics(kind));
    ImGui::EndChild();
}

void DrawMods() { DrawText("mods"); }
void DrawCapabilities() { DrawText("capabilities"); }
void DrawPatches() { DrawText("patches"); }
void DrawLog() { DrawText("log"); }

struct SettingRow {
    std::string mod, key, type, value, minimum, maximum, description;
    std::vector<std::string> allowed;
};

std::vector<SettingRow> ParseSettings(const std::string& text) {
    std::vector<SettingRow> rows;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::vector<std::string> f;
        size_t start = 0;
        for (size_t tab = line.find('\t'); tab != std::string::npos; tab = line.find('\t', start)) {
            f.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        f.push_back(line.substr(start));
        if (f.size() != 8) {
            continue;
        }
        SettingRow row{f[0], f[1], f[2], f[3], f[4], f[5], f[7], {}};
        std::istringstream options(f[6]);
        std::string option;
        while (std::getline(options, option, '|')) {
            if (!option.empty()) {
                row.allowed.push_back(option);
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// Every running mod's [config] settings, editable while playing; the mod is told about each change.
void DrawSettings() {
    namespace ui = sacore::gta_ui;
    if (!g_host->HasLiveSettings()) {
        ImGui::TextDisabled("Live settings need SACore 1.0.0 or newer.");
        return;
    }
    static std::vector<SettingRow> rows;
    static uint64_t refreshedAt = 0;
    if (rows.empty() || g_renderedFrames - refreshedAt > 120) {
        rows = ParseSettings(g_host->SettingsList());
        refreshedAt = g_renderedFrames;
    }
    bool changed = false;
    std::string currentMod;
    for (SettingRow& row : rows) {
        if (row.mod != currentMod) {
            currentMod = row.mod;
            std::string title = row.mod;
            for (char& c : title) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            ImGui::Dummy(ImVec2(0, 4));
            ui::DrawText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), ui::Font::Menu, title,
                         20.0f * g_scale, ui::colour::LightBlue);
            ImGui::Dummy(ImVec2(0, 22.0f * g_scale));
        }
        ImGui::PushID((row.mod + "/" + row.key).c_str());
        std::string next;
        const bool ranged = !row.minimum.empty() && !row.maximum.empty();
        if (row.type == "bool") {
            bool value = row.value == "true";
            if (ImGui::Checkbox(row.key.c_str(), &value)) {
                next = value ? "true" : "false";
            }
        } else if (row.type == "int") {
            int value = std::atoi(row.value.c_str());
            const bool edited = ranged ? ImGui::SliderInt(row.key.c_str(), &value, std::atoi(row.minimum.c_str()),
                                                          std::atoi(row.maximum.c_str()))
                                       : ImGui::InputInt(row.key.c_str(), &value);
            if (edited) {
                next = std::to_string(value);
            }
        } else if (row.type == "double") {
            float value = static_cast<float>(std::atof(row.value.c_str()));
            const bool edited = ranged ? ImGui::SliderFloat(row.key.c_str(), &value,
                                                            static_cast<float>(std::atof(row.minimum.c_str())),
                                                            static_cast<float>(std::atof(row.maximum.c_str())), "%.2f")
                                       : ImGui::InputFloat(row.key.c_str(), &value, 0.1f, 1.0f, "%.2f");
            if (edited) {
                next = std::format("{:.3f}", value);
            }
        } else if (!row.allowed.empty()) {
            if (ImGui::BeginCombo(row.key.c_str(), row.value.c_str())) {
                for (const std::string& option : row.allowed) {
                    if (ImGui::Selectable(option.c_str(), option == row.value)) {
                        next = option;
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            char buffer[256] = {};
            std::snprintf(buffer, sizeof(buffer), "%s", row.value.c_str());
            if (ImGui::InputText(row.key.c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                next = buffer;
            }
        }
        if (!row.description.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", row.description.c_str());
        }
        if (!next.empty() && next != row.value && g_host->SetSetting(row.mod, row.key, next)) {
            row.value = next;
            changed = true;
        }
        ImGui::PopID();
    }
    if (rows.empty()) {
        ImGui::TextDisabled("No running mod declares settings.");
    }
    if (changed) {
        refreshedAt = 0;  // re-read on the next frame (the mod may have adjusted related values)
        rows.clear();
    }
}

// Every save of this install: the 8 menu slots, quick saves and auto saves, newest first.
void DrawSaves() {
    if (!g_host->HasSaveGame()) {
        ImGui::TextDisabled("Needs SACore 1.0.0 or newer.");
        return;
    }
    static std::string text;
    static uint64_t refreshedAt = 0;
    if (text.empty() || g_renderedFrames - refreshedAt > 120) {
        text = g_host->SaveList();
        refreshedAt = g_renderedFrames;
    }
    struct Row {
        std::string name;
        long long modified;
        bool valid;
    };
    std::vector<Row> rows;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name, file, modified, valid;
        if (std::getline(fields, name, '\t') && std::getline(fields, file, '\t') && std::getline(fields, modified, '\t') &&
            std::getline(fields, valid, '\t')) {
            rows.push_back({name, std::atoll(modified.c_str()), valid == "1"});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.modified > b.modified; });
    if (ImGui::BeginTable("saves", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Save");
        ImGui::TableSetupColumn("Saved at");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (const Row& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool quick = row.name.starts_with("quick");
            const bool autoSave = row.name.starts_with("auto");
            const std::string label =
                std::string(quick ? "Quick save " : autoSave ? "Auto save " : "Slot ") + row.name.back();
            ImGui::TextUnformatted(label.c_str());
            ImGui::TableNextColumn();
            char when[64] = "?";
            const std::time_t seconds = static_cast<std::time_t>(row.modified);
            std::tm local{};
            if (localtime_s(&local, &seconds) == 0) {
                std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &local);
            }
            ImGui::TextUnformatted(when);
            ImGui::TableNextColumn();
            ImGui::TextColored(row.valid ? ImVec4(0.21f, 0.41f, 0.17f, 1) : ImVec4(0.71f, 0.1f, 0.11f, 1), "%s",
                               row.valid ? "valid" : "DAMAGED");
        }
        ImGui::EndTable();
    }
    if (rows.empty()) {
        ImGui::TextDisabled("No saves yet.");
    }

    // Community saves: files dropped into sacore\import, copied into a menu slot on request.
    ImGui::Separator();
    ImGui::TextUnformatted("IMPORT SAVES");
    if (!g_host->HasImport()) {
        ImGui::TextDisabled("Needs SACore 1.2.20 or newer.");
        return;
    }
    ImGui::TextWrapped("Put downloaded save files (GTASAsf1.b ...) into the folder sacore\\import of this game. Valid "
                       "files can be copied into a menu slot; the save they replace is backed up first.");
    static std::string imports;
    static uint64_t importsAt = 0;
    static int targetSlot = 1;
    static std::string armed;   // file waiting for the second click (target slot is not empty)
    static std::string status;
    if (g_renderedFrames - importsAt > 120) {
        imports = g_host->ImportList();
        importsAt = g_renderedFrames;
    }
    ImGui::SetNextItemWidth(200.0f * ImGui::GetIO().FontGlobalScale);
    if (ImGui::SliderInt("Target slot", &targetSlot, 1, 8)) {
        armed.clear();
    }
    const std::string target = "slot" + std::to_string(targetSlot);
    const bool slotUsed = std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.name == target; });
    std::istringstream importLines(imports);
    int count = 0;
    if (ImGui::BeginTable("imports", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        while (std::getline(importLines, line)) {
            std::istringstream fields(line);
            std::string file, size, valid, problem;
            if (!std::getline(fields, file, '\t') || !std::getline(fields, size, '\t') ||
                !std::getline(fields, valid, '\t')) {
                continue;
            }
            std::getline(fields, problem, '\t');
            ++count;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(file.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%lld KB", std::atoll(size.c_str()) / 1024);
            ImGui::TableNextColumn();
            if (valid == "1") {
                ImGui::TextColored(ImVec4(0.21f, 0.41f, 0.17f, 1), "valid");
            } else {
                ImGui::TextColored(ImVec4(0.71f, 0.1f, 0.11f, 1), "%s", problem.empty() ? "not a save" : problem.c_str());
            }
            ImGui::TableNextColumn();
            if (valid != "1") {
                continue;
            }
            const bool confirming = armed == file;
            const std::string label = (confirming ? "REPLACE SLOT " : "TO SLOT ") + std::to_string(targetSlot) + "##" + file;
            if (ImGui::Button(label.c_str())) {
                if (slotUsed && !confirming) {
                    armed = file;
                    status = "Slot " + std::to_string(targetSlot) + " has a save: click again to replace it (it is backed up).";
                } else {
                    armed.clear();
                    status = g_host->ImportSave(file, target) ? file + " is now in slot " + std::to_string(targetSlot) + "."
                                                              : "Import failed (see the Log page).";
                    text.clear();  // re-read the save list
                }
            }
        }
        ImGui::EndTable();
    }
    if (count == 0) {
        ImGui::TextDisabled("The import folder is empty.");
    }
    if (!status.empty()) {
        ImGui::TextWrapped("%s", status.c_str());
    }
}

void DrawConfig() {
    ImGui::TextUnformatted("sacore\\config\\core.toml (read-only here: edit the file, then restart the game)");
    ImGui::BeginChild("config", ImVec2(0, 0), ImGuiChildFlags_Borders);
    Lines(ReadFile(g_gameDirectory / "sacore" / "config" / "core.toml"));
    ImGui::EndChild();
}

void DrawCrashes() {
    const fs::path directory = g_gameDirectory / "sacore" / "crashes";
    std::error_code error;
    std::vector<fs::path> reports;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (entry.path().extension() == ".txt") {
            reports.push_back(entry.path());
        }
    }
    if (reports.empty()) {
        ImGui::TextUnformatted("No crash reports.");
        return;
    }
    for (const fs::path& report : reports) {
        const std::string name = report.filename().string();
        if (ImGui::Selectable(name.c_str(), g_selectedCrash == name)) {
            g_selectedCrash = name;
        }
    }
    if (!g_selectedCrash.empty()) {
        ImGui::Separator();
        ImGui::BeginChild("crash", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        Lines(ReadFile(directory / g_selectedCrash));
        ImGui::EndChild();
    }
}

void DrawPerformance() {
    ImGui::PlotLines("frame ms", g_frameMs.data(), static_cast<int>(g_frameMs.size()), static_cast<int>(g_frameIndex),
                     nullptr, 0.0f, 100.0f, ImVec2(0, 120));
    if (ImGui::Button("Write diagnostics bundle")) {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1);
        const fs::path target = g_gameDirectory / "sacore" / "diagnostics" / ("bundle-" + std::to_string(stamp));
        std::error_code error;
        for (const char* folder : {"logs", "crashes", "diagnostics", "config"}) {
            const fs::path source = g_gameDirectory / "sacore" / folder;
            if (fs::exists(source, error) && std::string(folder) != "diagnostics") {
                fs::copy(source, target / folder, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
            }
        }
        g_bundleMessage = error ? "bundle incomplete: " + error.message() : "bundle written to " + target.string();
        g_host->Info("{}", g_bundleMessage);
    }
    ImGui::TextUnformatted(g_bundleMessage.c_str());
}

void DrawCommands() {
    ImGui::TextUnformatted("Safe test commands (expert). They change the running game, not your save files.");
    const bool canWrite = g_host->Capability("player.money.write") == SACORE_CAPABILITY_SUPPORTED;
    if (!canWrite) {
        ImGui::TextUnformatted("player.money.write is not supported on this build.");
        return;
    }
    if (const auto player = g_host->Player(); player && ImGui::Button("Add $1000")) {
        g_host->SetMoney(player->money + 1000);
        g_host->Info("added $1000");
    }
}

constexpr std::array<Tab, 12> kTabs = {{
    {"Overview", View::Beginner, &DrawOverview},
    {"Settings", View::Beginner, &DrawSettings},
    {"Saves", View::Beginner, &DrawSaves},
    {"Player", View::Beginner, &DrawPlayer},
    {"Mods", View::Advanced, &DrawMods},
    {"Capabilities", View::Advanced, &DrawCapabilities},
    {"Config", View::Advanced, &DrawConfig},
    {"Log", View::Advanced, &DrawLog},
    {"Crashes", View::Advanced, &DrawCrashes},
    {"Performance", View::Advanced, &DrawPerformance},
    {"Patches", View::Expert, &DrawPatches},
    {"Commands", View::Expert, &DrawCommands},
}};

void DrawConsole() {
    namespace ui = sacore::gta_ui;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 maximum(display.x * 0.94f, display.y * 0.94f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(320.0f, maximum.x), std::min(240.0f, maximum.y)), maximum);
    if (g_layoutDirty) {
        // First open, resolution or scale change: fit the console into the picture, centred.
        ImGui::SetNextWindowSize(ImVec2(std::min(820.0f * g_scale, maximum.x), std::min(520.0f * g_scale, maximum.y)),
                                 ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        g_layoutDirty = false;
    }
    if (!ImGui::Begin("SACore developer console", &g_visible, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (g_selfTest && g_selfTestTab >= 0 && g_selfTestTab < static_cast<int>(kTabs.size())) {
        g_selectedTab = g_selfTestTab;
    }
    // Header like the pause menu: the game's Pricedown title, the build in the menu font.
    {
        // Dragged partly out of the picture: put it back next frame.
        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        if (position.x < 0 || position.y < 0 || position.x + size.x > display.x || position.y + size.y > display.y) {
            g_layoutDirty = true;
        }
    }
    g_lastRect = ImVec4(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    const float s = g_scale;
    ui::Heading("SACORE", 46.0f * s);
    ImGui::SameLine();
    const ImVec2 sub = ImGui::GetCursorScreenPos();
    ui::DrawText(ImGui::GetWindowDrawList(), ImVec2(sub.x + 8.0f * s, sub.y + 20.0f * s), ui::Font::Menu,
                 "DEVELOPER CONSOLE", 20.0f * s, ui::colour::LightBlue);
    ImGui::NewLine();
    ImGui::Separator();

    ImGui::BeginChild("menu", ImVec2(210.0f * s, 0), ImGuiChildFlags_None);
    for (size_t i = 0; i < kTabs.size(); ++i) {
        const Tab& tab = kTabs[i];
        if (static_cast<int>(tab.minimumView) > static_cast<int>(g_view)) {
            continue;
        }
        std::string label = tab.name;
        for (char& c : label) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        ImGui::PushID(static_cast<int>(i));
        if (ui::MenuEntry("entry", label, g_selectedTab == static_cast<int>(i), 22.0f * s)) {
            g_selectedTab = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 12));
    ui::DrawText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), ui::Font::Menu,
                 "F" + std::to_string(g_toggleKey - VK_F1 + 1) + " CLOSE", 16.0f * s, ui::colour::DarkGray, false);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (g_selectedTab >= 0 && g_selectedTab < static_cast<int>(kTabs.size())) {
        std::string title = kTabs[static_cast<size_t>(g_selectedTab)].name;
        for (char& c : title) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        ui::Heading(title, 30.0f * s);
        kTabs[static_cast<size_t>(g_selectedTab)].draw();
    }
    ImGui::EndChild();
    ImGui::End();
}

void Initialize(IDirect3DDevice9* device, HWND window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    g_iniPath = (g_gameDirectory / "sacore" / "config" / "devtools_imgui.ini").string();
    io.IniFilename = g_iniPath.c_str();
    ImGui::StyleColorsDark();
    sacore::gta_ui::ApplyStyle();
    g_baseStyle = ImGui::GetStyle();
    // The game's own fonts from this install (models\fonts.txd, data\fonts.dat); ImGui's font if they cannot be read.
    const bool gameFonts = sacore::gta_ui::LoadFonts(device, g_gameDirectory);
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX9_Init(device);
    g_ready = true;
    g_lastFrame = std::chrono::steady_clock::now();
    g_host->Info("ready (ImGui {}), toggle with F{}, game fonts {}", IMGUI_VERSION, g_toggleKey - VK_F1 + 1,
                 gameFonts ? "loaded" : "not found (ImGui font)");
}

void UpdateScale() {
    const float height = ImGui::GetIO().DisplaySize.y;
    if (height <= 0) {
        return;
    }
    const float scale = std::clamp(height / 720.0f, 0.6f, 3.0f) * g_userScale;
    if (std::abs(scale - g_scale) < 0.01f) {
        return;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style = g_baseStyle;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
    g_scale = scale;
    g_layoutDirty = true;
}

void AdvanceSelfTest(uint64_t frame) {
    if (!g_selfTest || frame - g_selfTestFrame < 30) {
        return;
    }
    g_selfTestFrame = frame;
    if (++g_selfTestTab >= static_cast<int>(kTabs.size())) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const bool inside = g_lastRect.x >= 0 && g_lastRect.y >= 0 && g_lastRect.x + g_lastRect.z <= display.x &&
                            g_lastRect.y + g_lastRect.w <= display.y;
        g_host->Info("self-test drew all {} tabs, {} frames rendered with the console open; console {:.0f}x{:.0f} {} "
                     "the {:.0f}x{:.0f} picture at scale {:.2f}",
                     kTabs.size(), g_renderedFrames, g_lastRect.z, g_lastRect.w, inside ? "inside" : "OUTSIDE",
                     display.x, display.y, g_scale);
        g_selfTest = false;
        g_visible = false;
    }
}

void OnFrame(const SacoreEvent& event) {
    const auto now = std::chrono::steady_clock::now();
    g_frameMs[g_frameIndex] = std::chrono::duration<float, std::milli>(now - g_lastFrame).count();
    g_frameIndex = (g_frameIndex + 1) % g_frameMs.size();
    g_lastFrame = now;
    if (!g_ready) {
        auto* device = static_cast<IDirect3DDevice9*>(g_host->Raw()->d3d_device(g_host->Raw()));
        auto* window = static_cast<HWND>(g_host->Raw()->game_window(g_host->Raw()));
        if (!device || !window) {
            return;
        }
        Initialize(device, window);
    }
    AdvanceSelfTest(event.frame);
    if (!g_visible) {
        return;
    }
    ImGui::GetIO().MouseDrawCursor = true;
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    UpdateScale();
    ImGui::NewFrame();
    DrawConsole();
    ImGui::EndFrame();
    auto* device = static_cast<IDirect3DDevice9*>(event.device);
    if (SUCCEEDED(device->BeginScene())) {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
        ++g_renderedFrames;
    }
}

int MessageFilter(void* window, uint32_t message, uintptr_t wParam, intptr_t lParam, intptr_t* result, void*) {
    if (!g_ready) {
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == g_toggleKey && (lParam & (1 << 30)) == 0) {
        g_visible = !g_visible;
        *result = 0;
        return 1;
    }
    if (!g_visible) {
        return 0;
    }
    ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(window), message, wParam, lParam);
    // While the console is open, keyboard and mouse messages belong to it.
    const bool input = (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
                       (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
    if (input) {
        *result = 0;
        return 1;
    }
    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<Host>(raw);
    g_gameDirectory = fs::path(std::u8string(reinterpret_cast<const char8_t*>(raw->mod_directory))).parent_path()
                          .parent_path()
                          .parent_path();
    const std::string view = g_host->ConfigString("view");
    g_view = view == "beginner" ? View::Beginner : view == "expert" ? View::Expert : View::Advanced;
    const int64_t key = g_host->ConfigInt("toggle_f_key", 11);
    g_toggleKey = VK_F1 + static_cast<UINT>(key - 1);
    g_selfTest = g_host->ConfigBool("self_test");
    g_visible = g_host->ConfigBool("open_on_start") || g_selfTest;
    g_userScale = static_cast<float>(g_host->ConfigDouble("ui_scale", 1.0));

    g_host->On(SACORE_EVENT_FRAME, &OnFrame);
    // Settings changed in the console itself (or by another mod) apply at once.
    g_host->On(SACORE_EVENT_CONFIG_CHANGED, [](const SacoreEvent&) {
        g_userScale = static_cast<float>(g_host->ConfigDouble("ui_scale", 1.0));
        const std::string changedView = g_host->ConfigString("view");
        g_view = changedView == "beginner" ? View::Beginner : changedView == "expert" ? View::Expert : View::Advanced;
    });
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
    g_host->On(SACORE_EVENT_SHUTDOWN, [](const SacoreEvent&) {
        if (g_ready) {
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_ready = false;
        }
    });
    raw->add_message_filter(raw, &MessageFilter, nullptr);
    return 0;
}

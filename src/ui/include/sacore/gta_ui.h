// GTA San Andreas look for Dear ImGui overlays: the game's own bitmap fonts (read from the user's install, never
// shipped) drawn through ImGui, and a colour theme from the game's HUD and menu palette.
#pragma once

#include <imgui.h>

#include <d3d9.h>

#include <filesystem>
#include <string_view>

namespace sacore::gta_ui {

enum class Font {
    Subtitles,  // models\fonts.txd "font1" upper part: mission text, the game's body font
    Pricedown,  // "font1" lower part: headings, money, "MISSION PASSED"
    Gothic,     // "font2" upper part: blackletter titles
    Menu,       // "font2" lower part: the pause menu entries (upper case only)
};

// SA HUD and menu colours (CHudColours on PC).
namespace colour {
inline constexpr ImU32 Red = IM_COL32(180, 25, 29, 255);
inline constexpr ImU32 Green = IM_COL32(54, 104, 44, 255);
inline constexpr ImU32 DarkBlue = IM_COL32(50, 60, 127, 255);
inline constexpr ImU32 LightBlue = IM_COL32(172, 203, 241, 255);
inline constexpr ImU32 LightGray = IM_COL32(225, 225, 225, 255);
inline constexpr ImU32 DarkGray = IM_COL32(74, 90, 107, 255);
inline constexpr ImU32 Gold = IM_COL32(144, 98, 16, 255);
inline constexpr ImU32 White = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 Black = IM_COL32(0, 0, 0, 255);
}  // namespace colour

// Loads models\fonts.txd and data\fonts.dat from the game folder into a texture of `device`.
// Returns false (and the text falls back to ImGui's own font) when the files are missing or malformed.
bool LoadFonts(IDirect3DDevice9* device, const std::filesystem::path& gameDirectory);
void ReleaseFonts();
bool FontsReady();

// Text size and drawing at a given cell height in pixels (the game's glyph cells are 32x40 at 1.0).
ImVec2 MeasureText(Font font, std::string_view text, float height);
void DrawText(ImDrawList* list, ImVec2 position, Font font, std::string_view text, float height, ImU32 colour,
              bool outline = true);

// Widgets in the game's style.
void Heading(std::string_view text, float height = 34.0f);              // Pricedown, white with outline
bool MenuEntry(const char* id, std::string_view text, bool selected,    // pause-menu entry in the menu font
               float height = 22.0f);

// ImGui style and colours in the game's palette; loads Arial Bold from Windows as body font when present.
void ApplyStyle(float scale = 1.0f);

}  // namespace sacore::gta_ui

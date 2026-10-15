#include <sacore/gta_ui.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sacore::gta_ui {
namespace {

constexpr int kTextureSize = 512;
constexpr float kCellWidth = 32.0f;
constexpr float kCellHeight = 40.0f;
constexpr int kGlyphs = 208;  // 16 columns x 13 rows (the last row is cut by the texture edge)

struct FontTexture {
    IDirect3DTexture9* texture = nullptr;
    std::array<int, kGlyphs> widths{};  // advance per glyph in texture pixels (data\fonts.dat [PROP])
};

FontTexture g_fonts[2];  // 0: "font1" (subtitles + pricedown), 1: "font2" (gothic + menu)
bool g_ready = false;

std::vector<uint8_t> ReadAll(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    return d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | (static_cast<uint32_t>(d[at + 3]) << 24);
}

// The PC texture dictionary holds "font1" and "font2" as 512x512 DXT3. Only the explicit 4-bit alpha matters:
// glyphs become white with that alpha, so ImGui's vertex colour tints them.
bool LoadTextures(IDirect3DDevice9* device, const std::vector<uint8_t>& txd) {
    if (txd.size() < 40 || U32(txd, 0) != 0x16) {
        return false;
    }
    size_t offset = 12 + 12 + 4;  // dictionary header, struct header, texture count
    int loaded = 0;
    while (offset + 12 <= txd.size()) {
        const uint32_t kind = U32(txd, offset);
        const uint32_t size = U32(txd, offset + 4);
        if (kind == 0x15 && offset + 24 + 92 <= txd.size()) {
            const size_t body = offset + 24;
            const std::string name(reinterpret_cast<const char*>(&txd[body + 8]));
            const uint32_t format = U32(txd, body + 76);
            const int width = txd[body + 80] | (txd[body + 81] << 8);
            const int height = txd[body + 82] | (txd[body + 83] << 8);
            const uint32_t dataSize = U32(txd, body + 88);
            const int slot = name == "font1" ? 0 : name == "font2" ? 1 : -1;
            if (slot >= 0 && format == MAKEFOURCC('D', 'X', 'T', '3') && width == kTextureSize &&
                height == kTextureSize && dataSize == kTextureSize * kTextureSize && body + 92 + dataSize <= txd.size()) {
                IDirect3DTexture9* texture = nullptr;
                if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture,
                                                 nullptr))) {
                    return false;
                }
                D3DLOCKED_RECT locked{};
                if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
                    texture->Release();
                    return false;
                }
                const uint8_t* blocks = &txd[body + 92];
                for (int by = 0; by < height / 4; ++by) {
                    for (int bx = 0; bx < width / 4; ++bx) {
                        const uint8_t* block = blocks + (static_cast<size_t>(by) * (width / 4) + bx) * 16;
                        uint64_t alpha = 0;
                        std::memcpy(&alpha, block, 8);
                        for (int py = 0; py < 4; ++py) {
                            auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(locked.pBits) +
                                                                    static_cast<size_t>(by * 4 + py) * locked.Pitch);
                            for (int px = 0; px < 4; ++px) {
                                const uint32_t a = static_cast<uint32_t>((alpha >> (4 * (py * 4 + px))) & 0xF) * 17;
                                row[bx * 4 + px] = (a << 24) | 0x00FFFFFF;
                            }
                        }
                    }
                }
                texture->UnlockRect(0);
                if (g_fonts[slot].texture) {
                    g_fonts[slot].texture->Release();
                }
                g_fonts[slot].texture = texture;
                ++loaded;
            }
        }
        offset += 12 + size;
    }
    return loaded == 2;
}

// data\fonts.dat: [FONT_ID] n then [PROP] with 208 numbers; font 0 belongs to "font2", font 1 to "font1".
bool LoadWidths(const std::vector<uint8_t>& dat) {
    std::istringstream text(std::string(dat.begin(), dat.end()));
    std::string line;
    int fontId = -1;
    int expectId = 0;
    bool inProp = false;
    std::array<int, 2> counts{};
    while (std::getline(text, line)) {
        line = line.substr(0, line.find('#'));
        if (line.find("[FONT_ID]") != std::string::npos) {
            expectId = 1;
            inProp = false;
            continue;
        }
        if (line.find("[PROP]") != std::string::npos) {
            inProp = fontId == 0 || fontId == 1;
            continue;
        }
        if (line.find('[') != std::string::npos) {
            inProp = false;
            continue;
        }
        std::istringstream numbers(line);
        int value = 0;
        while (numbers >> value) {
            if (expectId) {
                fontId = value;
                expectId = 0;
            } else if (inProp) {
                const int slot = fontId == 0 ? 1 : 0;
                if (counts[static_cast<size_t>(fontId)] < kGlyphs) {
                    g_fonts[slot].widths[static_cast<size_t>(counts[static_cast<size_t>(fontId)]++)] = value;
                }
            }
        }
    }
    return counts[0] == kGlyphs && counts[1] == kGlyphs;
}

int TextureFor(Font font) {
    return font == Font::Subtitles || font == Font::Pricedown ? 0 : 1;
}

// Glyph index in the 16x13 grid, the way the game maps characters (CFont: c - 0x20; Pricedown and the menu font keep
// digits at 144 and capitals at 155 and have no lower case).
int GlyphIndex(Font font, unsigned char c) {
    if (c < 32 || c > 127) {
        c = '?';
    }
    if (font == Font::Pricedown || font == Font::Menu) {
        if (c >= '0' && c <= '9') {
            return c + 96;
        }
        const int upper = std::toupper(c);
        if (upper >= 'A' && upper <= 'Z') {
            return upper - 32 + 122;
        }
    }
    return c - 32;
}

}  // namespace

bool LoadFonts(IDirect3DDevice9* device, const std::filesystem::path& gameDirectory) {
    ReleaseFonts();
    const std::vector<uint8_t> txd = ReadAll(gameDirectory / "models" / "fonts.txd");
    const std::vector<uint8_t> dat = ReadAll(gameDirectory / "data" / "fonts.dat");
    g_ready = device && LoadTextures(device, txd) && LoadWidths(dat);
    if (!g_ready) {
        ReleaseFonts();
    }
    return g_ready;
}

void ReleaseFonts() {
    for (FontTexture& font : g_fonts) {
        if (font.texture) {
            font.texture->Release();
            font.texture = nullptr;
        }
    }
    g_ready = false;
}

bool FontsReady() {
    return g_ready;
}

ImVec2 MeasureText(Font font, std::string_view text, float height) {
    if (!g_ready) {
        return ImGui::CalcTextSize(text.data(), text.data() + text.size());
    }
    const FontTexture& texture = g_fonts[TextureFor(font)];
    const float scale = height / kCellHeight;
    float width = 0;
    for (const char c : text) {
        width += static_cast<float>(texture.widths[static_cast<size_t>(GlyphIndex(font, static_cast<unsigned char>(c)))]) *
                 scale;
    }
    return {width, height};
}

void DrawText(ImDrawList* list, ImVec2 position, Font font, std::string_view text, float height, ImU32 colour,
              bool outline) {
    if (!g_ready) {
        list->AddText(position, colour, text.data(), text.data() + text.size());
        return;
    }
    const FontTexture& texture = g_fonts[TextureFor(font)];
    const float scale = height / kCellHeight;
    const ImVec2 cell(kCellWidth * scale, height);
    const auto draw = [&](ImVec2 origin, ImU32 tint) {
        float x = origin.x;
        for (const char c : text) {
            const int index = GlyphIndex(font, static_cast<unsigned char>(c));
            if (c != ' ') {
                const float u = static_cast<float>(index % 16) * kCellWidth / kTextureSize;
                const float v = static_cast<float>(index / 16) * kCellHeight / kTextureSize;
                list->AddImage(reinterpret_cast<ImTextureID>(texture.texture), ImVec2(x, origin.y),
                               ImVec2(x + cell.x, origin.y + cell.y), ImVec2(u, v),
                               ImVec2(u + kCellWidth / kTextureSize, v + kCellHeight / kTextureSize), tint);
            }
            x += static_cast<float>(texture.widths[static_cast<size_t>(index)]) * scale;
        }
    };
    if (outline) {
        // The game draws text with a black edge; four offset copies give the same look at any size.
        const float o = std::max(1.0f, height / 22.0f);
        const ImU32 edge = IM_COL32(0, 0, 0, (colour >> IM_COL32_A_SHIFT) & 0xFF);
        for (const ImVec2 d : {ImVec2(-o, 0), ImVec2(o, 0), ImVec2(0, -o), ImVec2(0, o)}) {
            draw(ImVec2(position.x + d.x, position.y + d.y), edge);
        }
    }
    draw(position, colour);
}

void Heading(std::string_view text, float height) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 size = MeasureText(Font::Pricedown, text, height);
    DrawText(list, position, Font::Pricedown, text, height, colour::White);
    ImGui::Dummy(ImVec2(size.x, size.y + 2.0f));
}

bool MenuEntry(const char* id, std::string_view text, bool selected, float height) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 size = MeasureText(Font::Menu, text, height);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(std::max(size.x, height * 5.5f), height * 1.2f));
    const ImU32 tint = selected ? colour::White : ImGui::IsItemHovered() ? colour::LightGray : colour::LightBlue;
    DrawText(ImGui::GetWindowDrawList(), ImVec2(position.x, position.y + 2.0f), Font::Menu, text, height, tint);
    return clicked;
}

void ApplyStyle(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = style.ChildRounding = style.FrameRounding = style.PopupRounding = 0.0f;
    style.ScrollbarRounding = style.GrabRounding = style.TabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = style.Colors;
    const auto rgba = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
    c[ImGuiCol_Text] = rgba(225, 225, 225);
    c[ImGuiCol_TextDisabled] = rgba(120, 130, 140);
    c[ImGuiCol_WindowBg] = rgba(0, 0, 0, 0.88f);
    c[ImGuiCol_ChildBg] = rgba(0, 0, 0, 0.0f);
    c[ImGuiCol_PopupBg] = rgba(10, 12, 20, 0.95f);
    c[ImGuiCol_Border] = rgba(144, 98, 16, 0.8f);
    c[ImGuiCol_FrameBg] = rgba(30, 34, 48);
    c[ImGuiCol_FrameBgHovered] = rgba(50, 60, 127, 0.7f);
    c[ImGuiCol_FrameBgActive] = rgba(50, 60, 127);
    c[ImGuiCol_TitleBg] = rgba(0, 0, 0);
    c[ImGuiCol_TitleBgActive] = rgba(50, 60, 127);
    c[ImGuiCol_TitleBgCollapsed] = rgba(0, 0, 0, 0.6f);
    c[ImGuiCol_MenuBarBg] = rgba(10, 12, 20);
    c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0.5f);
    c[ImGuiCol_ScrollbarGrab] = rgba(74, 90, 107);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(172, 203, 241, 0.8f);
    c[ImGuiCol_ScrollbarGrabActive] = rgba(172, 203, 241);
    c[ImGuiCol_CheckMark] = rgba(172, 203, 241);
    c[ImGuiCol_SliderGrab] = rgba(172, 203, 241);
    c[ImGuiCol_SliderGrabActive] = rgba(255, 255, 255);
    c[ImGuiCol_Button] = rgba(50, 60, 127, 0.8f);
    c[ImGuiCol_ButtonHovered] = rgba(70, 85, 170);
    c[ImGuiCol_ButtonActive] = rgba(144, 98, 16);
    c[ImGuiCol_Header] = rgba(50, 60, 127, 0.8f);
    c[ImGuiCol_HeaderHovered] = rgba(70, 85, 170);
    c[ImGuiCol_HeaderActive] = rgba(144, 98, 16);
    c[ImGuiCol_Separator] = rgba(144, 98, 16, 0.6f);
    c[ImGuiCol_Tab] = rgba(20, 24, 40);
    c[ImGuiCol_TabHovered] = rgba(70, 85, 170);
    c[ImGuiCol_TabSelected] = rgba(50, 60, 127);
    c[ImGuiCol_PlotLines] = rgba(54, 104, 44);
    c[ImGuiCol_PlotHistogram] = rgba(144, 98, 16);
    c[ImGuiCol_TableHeaderBg] = rgba(50, 60, 127, 0.6f);
    style.ScaleAllSizes(scale);

    // Body text: Arial Bold from Windows, close to the game's subtitle font (not shipped: the user's system font).
    wchar_t windows[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windows, MAX_PATH)) {
        const std::filesystem::path arial = std::filesystem::path(windows) / L"Fonts" / L"arialbd.ttf";
        if (std::filesystem::exists(arial)) {
            const std::u8string utf8 = arial.u8string();
            ImGui::GetIO().Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()), 16.0f * scale);
        }
    }
}

}  // namespace sacore::gta_ui

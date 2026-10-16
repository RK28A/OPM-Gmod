// Thème et widgets dessinés à la main (ImGui).
#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

namespace ui {

namespace col {
constexpr ImU32 Rgb(unsigned rgb, float a = 1.0f)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, static_cast<int>(a * 255.0f));
}
constexpr ImU32 Bg = Rgb(0x0c0e13);
constexpr ImU32 Card = Rgb(0x141720);
constexpr ImU32 Card2 = Rgb(0x1b1f2b);
constexpr ImU32 Hover = Rgb(0x222737);
constexpr ImU32 Border = Rgb(0x262b3a);
constexpr ImU32 Accent = Rgb(0x7c5cff);
constexpr ImU32 AccentHover = Rgb(0x9277ff);
constexpr ImU32 AccentDim = Rgb(0x251f48);
constexpr ImU32 Green = Rgb(0x22c55e);
constexpr ImU32 GreenHover = Rgb(0x3ad874);
constexpr ImU32 Red = Rgb(0xf04343);
constexpr ImU32 RedHover = Rgb(0xff5c5c);
constexpr ImU32 RedDim = Rgb(0x2c1317);
constexpr ImU32 Amber = Rgb(0xf5a524);
constexpr ImU32 Text = Rgb(0xe8eaf1);
constexpr ImU32 Muted = Rgb(0x8b91a3);
constexpr ImU32 Faint = Rgb(0x5d6375);
constexpr ImU32 White = Rgb(0xffffff);
}  // namespace col

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* caption = nullptr;
    ImFont* bold = nullptr;
    ImFont* boldLarge = nullptr;
    ImFont* title = nullptr;
};
extern Fonts fonts;
extern float scale;
inline float S(float v) { return v * scale; }

void Setup(float dpiScale);  // polices + style

// --- briques -------------------------------------------------------------
void TextAt(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 color, const char* text);
ImVec2 TextSize(ImFont* f, const char* text);
void TextColored(ImFont* f, ImU32 color, const char* text);

bool BeginCard(const char* id, const char* step, const char* title, ImVec2 size, bool autoHeight = false);
void EndCard();

bool Slider(const char* id, float* v, float vmin, float vmax);
bool Segmented(const char* id, const char* const* labels, int count, int* selected, float width);
bool Toggle(const char* label, bool* v);
bool KeyCap(const char* id, const char* text, bool recording, ImU32 textColor, float width);
bool FlatButton(const char* label, ImVec2 size, bool highlighted = false);
bool IconButton(const char* id, ImVec2 size, int icon, bool busy);  // icon 0 = actualiser

// Bouton principal (vert / rouge) et bouton d'urgence.
bool BigButton(const char* id, const char* text, const char* sub, ImVec2 size, ImU32 fill, ImU32 hover,
               ImU32 textColor, int icon);  // icon 0 = lecture, 1 = stop
bool PanicButton(const char* id, ImVec2 size);

void StatTile(const char* value, const char* label, ImVec2 size);
void Logo(ImVec2 pos, float size);

}  // namespace ui

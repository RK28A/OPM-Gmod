#include "ui.h"

#include "imgui_internal.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace ui {

Fonts fonts;
float scale = 1.0f;

namespace {

ImVec4 V4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

bool FileExists(const std::string& path)
{
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

ImFont* LoadFont(const char* file, float size)
{
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,  // latin + accents
        0x0152, 0x0153,  // Œ œ
        0x2010, 0x2030,  // tirets, guillemets, …
        0x2190, 0x2193,  // flèches
        0x2248, 0x2248,  // ≈
        0,
    };
    char windir[MAX_PATH] = {};
    GetWindowsDirectoryA(windir, MAX_PATH);
    std::string path = std::string(windir) + "\\Fonts\\" + file;
    ImGuiIO& io = ImGui::GetIO();
    if (!FileExists(path)) {
        ImFontConfig cfg;
        cfg.SizePixels = size;
        return io.Fonts->AddFontDefault(&cfg);
    }
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    return io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, ranges);
}

void CenterText(ImDrawList* dl, ImFont* f, ImVec2 a, ImVec2 b, ImU32 c, const char* t)
{
    ImVec2 ts = TextSize(f, t);
    TextAt(dl, f, ImVec2(std::floor(a.x + (b.x - a.x - ts.x) * 0.5f), std::floor(a.y + (b.y - a.y - ts.y) * 0.5f)), c, t);
}

void HandCursorIfHovered()
{
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

}  // namespace

void Setup(float dpiScale)
{
    scale = dpiScale;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->Clear();
    fonts.regular = LoadFont("segoeui.ttf", S(16));
    fonts.caption = LoadFont("segoeui.ttf", S(13.5f));
    fonts.bold = LoadFont("seguisb.ttf", S(16));
    fonts.boldLarge = LoadFont("segoeuib.ttf", S(19));
    fonts.title = LoadFont("segoeuib.ttf", S(26));
    io.FontDefault = fonts.regular;

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(22, 18);
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.WindowRounding = 0;
    st.ChildRounding = 16;
    st.FrameRounding = 9;
    st.PopupRounding = 10;
    st.GrabRounding = 9;
    st.ScrollbarRounding = 9;
    st.ScrollbarSize = 10;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 1;
    st.ScaleAllSizes(scale);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = V4(col::Bg);
    c[ImGuiCol_ChildBg] = V4(col::Card);
    c[ImGuiCol_PopupBg] = V4(col::Card2);
    c[ImGuiCol_Border] = V4(col::Border);
    c[ImGuiCol_Text] = V4(col::Text);
    c[ImGuiCol_TextDisabled] = V4(col::Faint);
    c[ImGuiCol_FrameBg] = V4(col::Card2);
    c[ImGuiCol_FrameBgHovered] = V4(col::Hover);
    c[ImGuiCol_FrameBgActive] = V4(col::Hover);
    c[ImGuiCol_Button] = V4(col::Card2);
    c[ImGuiCol_ButtonHovered] = V4(col::Hover);
    c[ImGuiCol_ButtonActive] = V4(col::AccentDim);
    c[ImGuiCol_Header] = V4(col::AccentDim);
    c[ImGuiCol_HeaderHovered] = V4(col::Hover);
    c[ImGuiCol_HeaderActive] = V4(col::AccentDim);
    c[ImGuiCol_CheckMark] = V4(col::White);
    c[ImGuiCol_SliderGrab] = V4(col::Accent);
    c[ImGuiCol_SliderGrabActive] = V4(col::AccentHover);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = V4(col::Border);
    c[ImGuiCol_ScrollbarGrabHovered] = V4(col::Faint);
    c[ImGuiCol_ScrollbarGrabActive] = V4(col::Muted);
    c[ImGuiCol_TextSelectedBg] = V4(col::Rgb(0x7c5cff, 0.35f));
    c[ImGuiCol_NavHighlight] = ImVec4(0, 0, 0, 0);
}

// ---------------------------------------------------------------- briques

void TextAt(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 color, const char* text)
{
    dl->AddText(f, f->FontSize, pos, color, text);
}

ImVec2 TextSize(ImFont* f, const char* text)
{
    return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, text);
}

void TextColored(ImFont* f, ImU32 color, const char* text)
{
    ImGui::PushFont(f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool BeginCard(const char* id, const char* step, const char* title, ImVec2 size, bool autoHeight)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Card);
    ImGui::PushStyleColor(ImGuiCol_Border, col::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (autoHeight) flags |= ImGuiChildFlags_AutoResizeY;
    bool open = ImGui::BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (!title) return open;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = S(26);
    float x = p.x;
    if (step) {
        dl->AddRectFilled(p, p + ImVec2(h, h), col::AccentDim, S(8));
        CenterText(dl, fonts.bold, p, p + ImVec2(h, h), col::Accent, step);
        x += h + S(10);
    }
    ImVec2 ts = TextSize(fonts.boldLarge, title);
    TextAt(dl, fonts.boldLarge, ImVec2(x, p.y + (h - ts.y) * 0.5f), col::Text, title);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, h));
    ImGui::Dummy(ImVec2(0, S(4)));
    return open;
}

void EndCard() { ImGui::EndChild(); }

bool Slider(const char* id, float* v, float vmin, float vmax)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(24);
    const float r = S(9);

    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    HandCursorIfHovered();

    const float x0 = p.x + r, x1 = p.x + w - r, cy = p.y + h * 0.5f;
    bool changed = false;
    if (active && x1 > x0) {
        float t = std::clamp((io.MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
        float nv = vmin + t * (vmax - vmin);
        if (nv != *v) {
            *v = nv;
            changed = true;
        }
    }
    const float t = vmax > vmin ? std::clamp((*v - vmin) / (vmax - vmin), 0.0f, 1.0f) : 0.0f;
    const float kx = x0 + t * (x1 - x0);
    const float th = S(6);

    dl->AddRectFilled(ImVec2(p.x, cy - th / 2), ImVec2(p.x + w, cy + th / 2), col::Card2, th);
    dl->AddRectFilled(ImVec2(p.x, cy - th / 2), ImVec2(kx, cy + th / 2), col::Accent, th);
    if (hovered || active) dl->AddCircleFilled(ImVec2(kx, cy), r * 1.9f, col::Rgb(0x7c5cff, 0.18f), 32);
    dl->AddCircleFilled(ImVec2(kx, cy), active ? r * 1.1f : r, col::White, 32);
    dl->AddCircleFilled(ImVec2(kx, cy), r * 0.42f, col::Accent, 24);
    return changed;
}

bool Segmented(const char* id, const char* const* labels, int count, int* selected, float width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = S(34);
    const float segW = width / static_cast<float>(count);
    bool changed = false;
    int hoveredIdx = -1;

    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        ImGui::SetCursorScreenPos(p + ImVec2(segW * static_cast<float>(i), 0));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", ImVec2(segW, h)) && *selected != i) {
            *selected = i;
            changed = true;
        }
        if (ImGui::IsItemHovered()) hoveredIdx = i;
        HandCursorIfHovered();
        ImGui::PopID();
    }
    ImGui::PopID();

    dl->AddRectFilled(p, p + ImVec2(width, h), col::Card2, S(10));
    for (int i = 0; i < count; ++i) {
        ImVec2 a = p + ImVec2(segW * static_cast<float>(i) + S(3), S(3));
        ImVec2 b = p + ImVec2(segW * static_cast<float>(i + 1) - S(3), h - S(3));
        if (i == *selected)
            dl->AddRectFilled(a, b, col::Accent, S(8));
        else if (i == hoveredIdx)
            dl->AddRectFilled(a, b, col::Hover, S(8));
        CenterText(dl, fonts.bold, a, b, i == *selected ? col::White : col::Muted, labels[i]);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(width, h));
    return changed;
}

bool Toggle(const char* label, bool* v)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float tw = S(38), th = S(22);
    const ImVec2 ls = TextSize(fonts.regular, label);
    bool clicked = ImGui::InvisibleButton(label, ImVec2(tw + S(10) + ls.x, std::max(th, ls.y)));
    HandCursorIfHovered();
    if (clicked) *v = !*v;

    const float cy = p.y + std::max(th, ls.y) * 0.5f;
    dl->AddRectFilled(ImVec2(p.x, cy - th / 2), ImVec2(p.x + tw, cy + th / 2), *v ? col::Accent : col::Card2, th);
    if (!*v) dl->AddRect(ImVec2(p.x, cy - th / 2), ImVec2(p.x + tw, cy + th / 2), col::Border, th);
    const float kr = th * 0.5f - S(3);
    const float kx = *v ? p.x + tw - th * 0.5f : p.x + th * 0.5f;
    dl->AddCircleFilled(ImVec2(kx, cy), kr, *v ? col::White : col::Muted, 24);
    TextAt(dl, fonts.regular, ImVec2(p.x + tw + S(10), cy - ls.y * 0.5f), col::Muted, label);
    return clicked;
}

bool KeyCap(const char* id, const char* text, bool recording, ImU32 textColor, float width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, S(36));
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    HandCursorIfHovered();

    dl->AddRectFilled(p + ImVec2(0, S(3)), p + size + ImVec2(0, S(3)), col::Rgb(0x07080b), S(9));
    dl->AddRectFilled(p, p + size, hovered ? col::Hover : col::Card2, S(9));
    dl->AddRect(p, p + size, recording ? col::Accent : col::Border, S(9), 0, recording ? S(2) : S(1));
    if (recording) {
        float a = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
        CenterText(dl, fonts.bold, p, p + size, col::Rgb(0x9277ff, a), "Appuyez…");
    } else {
        CenterText(dl, fonts.bold, p, p + size, textColor, text);
    }
    return clicked;
}

bool FlatButton(const char* label, ImVec2 size, bool highlighted)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();
    HandCursorIfHovered();
    ImU32 fill = highlighted ? col::AccentDim : (hovered ? col::Hover : col::Card2);
    dl->AddRectFilled(p, p + size, fill, S(8));
    dl->AddRect(p, p + size, highlighted ? col::Accent : col::Border, S(8));
    const char* end = ImGui::FindRenderedTextEnd(label);
    std::string text(label, end);
    CenterText(dl, fonts.bold, p, p + size, highlighted ? col::AccentHover : (hovered ? col::Text : col::Muted),
               text.c_str());
    return clicked;
}

bool IconButton(const char* id, ImVec2 size, int icon, bool busy)
{
    (void)icon;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    HandCursorIfHovered();
    dl->AddRectFilled(p, p + size, hovered ? col::Hover : col::Card2, S(10));
    dl->AddRect(p, p + size, col::Border, S(10));

    // Flèche circulaire « actualiser », qui tourne pendant le chargement.
    const ImVec2 c = p + size * 0.5f;
    const float r = S(7);
    const float rot = busy ? static_cast<float>(ImGui::GetTime()) * 6.0f : 0.0f;
    const float a0 = 0.5f + rot, a1 = 5.3f + rot;
    const ImU32 color = hovered || busy ? col::Text : col::Muted;
    dl->PathArcTo(c, r, a0, a1, 24);
    dl->PathStroke(color, 0, S(2));
    const ImVec2 tip(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r);
    const ImVec2 dir(-std::sin(a1), std::cos(a1));  // tangente
    const ImVec2 nrm(std::cos(a1), std::sin(a1));
    const float s = S(4);
    dl->AddTriangleFilled(tip + dir * s, tip - nrm * s, tip + nrm * s, color);
    return clicked && !busy;
}

bool BigButton(const char* id, const char* text, const char* sub, ImVec2 size, ImU32 fill, ImU32 hover,
               ImU32 textColor, int icon)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    HandCursorIfHovered();

    const ImVec2 off = held ? ImVec2(0, S(1)) : ImVec2(0, 0);
    dl->AddRectFilled(p + off, p + size + off, hovered ? hover : fill, S(14));

    const ImVec2 ts = TextSize(fonts.boldLarge, text);
    const ImVec2 ss = TextSize(fonts.caption, sub);
    const float iconW = S(20), gap = S(14);
    const float totalW = iconW + gap + std::max(ts.x, ss.x);
    const float x = p.x + (size.x - totalW) * 0.5f;
    const float cy = p.y + size.y * 0.5f + off.y;

    if (icon == 0) {
        dl->AddTriangleFilled(ImVec2(x + S(3), cy - S(9)), ImVec2(x + S(3), cy + S(9)), ImVec2(x + S(18), cy), textColor);
    } else {
        dl->AddRectFilled(ImVec2(x + S(2), cy - S(8)), ImVec2(x + S(18), cy + S(8)), textColor, S(3));
    }
    const float tx = x + iconW + gap;
    const float blockH = ts.y + ss.y - S(2);
    TextAt(dl, fonts.boldLarge, ImVec2(tx, cy - blockH * 0.5f), textColor, text);
    TextAt(dl, fonts.caption, ImVec2(tx, cy - blockH * 0.5f + ts.y - S(2)), (textColor & 0x00FFFFFF) | 0xB0000000, sub);
    return clicked;
}

bool PanicButton(const char* id, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    HandCursorIfHovered();

    dl->AddRectFilled(p, p + size, hovered ? col::Red : col::RedDim, S(14));
    dl->AddRect(p, p + size, col::Red, S(14), 0, S(2));

    const char* text = "ARRÊT D'URGENCE";
    const char* sub = "relâche tout, réseau normal";
    const ImVec2 ts = TextSize(fonts.boldLarge, text);
    const ImVec2 ss = TextSize(fonts.caption, sub);
    const float iconW = S(24), gap = S(12);
    const float totalW = iconW + gap + std::max(ts.x, ss.x);
    const float x = p.x + (size.x - totalW) * 0.5f;
    const float cy = p.y + size.y * 0.5f;

    // Panneau octogonal « sens interdit ».
    const ImVec2 c(x + iconW * 0.5f, cy);
    const float r = iconW * 0.5f;
    ImVec2 pts[8];
    for (int i = 0; i < 8; ++i) {
        float a = 3.14159265f / 8.0f + static_cast<float>(i) * 3.14159265f / 4.0f;
        pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
    dl->AddConvexPolyFilled(pts, 8, hovered ? col::White : col::Red);
    dl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.16f), ImVec2(c.x + r * 0.55f, c.y + r * 0.16f),
                      hovered ? col::Red : col::White, S(1));

    const ImU32 tc = hovered ? col::White : col::Rgb(0xffd6d6);
    const float tx = x + iconW + gap;
    const float blockH = ts.y + ss.y - S(2);
    TextAt(dl, fonts.boldLarge, ImVec2(tx, cy - blockH * 0.5f), tc, text);
    TextAt(dl, fonts.caption, ImVec2(tx, cy - blockH * 0.5f + ts.y - S(2)), (tc & 0x00FFFFFF) | 0xB0000000, sub);
    return clicked;
}

void StatTile(const char* value, const char* label, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    dl->AddRectFilled(p, p + size, col::Card2, S(12));
    const ImVec2 vs = TextSize(fonts.boldLarge, value);
    const ImVec2 ls = TextSize(fonts.caption, label);
    const float top = p.y + (size.y - vs.y - ls.y) * 0.5f;
    TextAt(dl, fonts.boldLarge, ImVec2(p.x + (size.x - vs.x) * 0.5f, top), col::Text, value);
    TextAt(dl, fonts.caption, ImVec2(p.x + (size.x - ls.x) * 0.5f, top + vs.y), col::Muted, label);
}

void Logo(ImVec2 p, float size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(size, size), col::Accent, size * 0.3f);
    // Éclair : deux triangles qui se chevauchent (dessin 24x30 recentré).
    const float k = size / 46.0f;
    const ImVec2 o = p + ImVec2(11 * k, 8 * k);
    auto P = [&](float x, float y) { return o + ImVec2(x * k, y * k); };
    dl->AddTriangleFilled(P(15, 0), P(3, 17), P(14, 17), col::White);
    dl->AddTriangleFilled(P(10, 13), P(21, 13), P(8, 30), col::White);
}

}  // namespace ui

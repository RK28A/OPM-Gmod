#include "app.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <thread>

using namespace ui;

namespace {

const char* kDirectionLabels[] = {"Entrant", "Sortant", "Les deux"};
const char* kModeLabels[] = {"Basculer", "Maintenir"};
const int kDelayPresets[] = {50, 100, 250, 500, 1000, 2000};
struct AutoOff {
    const char* label;
    int seconds;
};
const AutoOff kAutoOff[] = {{"Jamais", 0}, {"5 s", 5}, {"10 s", 10}, {"30 s", 30},
                            {"1 min", 60}, {"2 min", 120}, {"5 min", 300}};

double Now() { return static_cast<double>(GetTickCount64()) / 1000.0; }

std::string Fmt(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

std::string Thousands(unsigned long long v)
{
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), " ");
    return s;
}

std::wstring BaseName(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

ImU32 AvatarColor(const std::wstring& key)
{
    static const ImU32 colors[] = {col::Rgb(0x7c5cff), col::Rgb(0x3b82f6), col::Rgb(0x06b6d4), col::Rgb(0x10b981),
                                   col::Rgb(0xf59e0b), col::Rgb(0xef4444), col::Rgb(0xec4899), col::Rgb(0x8b5cf6)};
    unsigned h = 2166136261u;
    for (wchar_t c : key) h = (h ^ static_cast<unsigned>(c)) * 16777619u;
    return colors[h % 8];
}

// Ligne « libellé + sous-libellé » à gauche, contrôle aligné à droite.
template <class Fn>
void SettingRow(const char* title, const char* sub, float controlW, float controlH, Fn control)
{
    const float x0 = ImGui::GetCursorScreenPos().x;
    const float right = x0 + ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    TextColored(fonts.bold, col::Text, title);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(4));
    TextColored(fonts.caption, col::Faint, sub);
    ImGui::EndGroup();
    const ImVec2 gmin = ImGui::GetItemRectMin(), gmax = ImGui::GetItemRectMax();
    const float h = std::max(gmax.y - gmin.y, controlH);
    ImGui::SetCursorScreenPos(ImVec2(right - controlW, gmin.y + (h - controlH) * 0.5f));
    control();
    ImGui::SetCursorScreenPos(ImVec2(x0, gmin.y + h + S(9)));
    ImGui::Dummy(ImVec2(0, 0));
}

// Titre + champ numérique à droite + curseur pleine largeur.
bool ValueSlider(const char* id, const char* title, const char* hint, const char* unit, float* value, float sliderMax,
                 float inputMax, bool decimals)
{
    bool changed = false;
    ImGui::PushID(id);
    const float x0 = ImGui::GetCursorScreenPos().x;
    const float right = x0 + ImGui::GetContentRegionAvail().x;
    const float y0 = ImGui::GetCursorScreenPos().y;
    const float inputW = S(84), unitW = S(26);

    TextColored(fonts.bold, col::Text, title);
    ImGui::SameLine(0, S(8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(1));
    TextColored(fonts.caption, col::Faint, hint);

    ImGui::SetCursorScreenPos(ImVec2(right - inputW - unitW, y0 - S(5)));
    ImGui::SetNextItemWidth(inputW);
    ImGui::PushFont(fonts.bold);
    if (decimals) {
        if (ImGui::InputFloat("##in", value, 0, 0, "%.1f")) changed = true;
    } else {
        int v = static_cast<int>(*value + 0.5f);
        if (ImGui::InputInt("##in", &v, 0, 0)) {
            *value = static_cast<float>(v);
            changed = true;
        }
    }
    ImGui::PopFont();
    ImGui::SameLine(0, S(6));
    TextColored(fonts.regular, col::Muted, unit);

    ImGui::SetCursorScreenPos(ImVec2(x0, y0 + S(28)));
    float sv = std::min(*value, sliderMax);
    if (Slider("##slider", &sv, 0.0f, sliderMax)) {
        *value = decimals ? std::round(sv * 10.0f) / 10.0f : std::round(sv);
        changed = true;
    }
    *value = std::clamp(*value, 0.0f, inputMax);
    ImGui::PopID();
    return changed;
}

}  // namespace

// ---------------------------------------------------------------- cycle de vie

App::App(HWND hwnd) : hwnd_(hwnd)
{
    s_.Load();
    tracker_.SetTarget(s_.target);
    ApplyEngineSettings();
    ApplyTopmost();
    BindHotkeys();
    RefreshApps();
    if (message_.empty())
        Flash("Prêt. Choisissez une application, réglez le lag, puis appuyez sur " + HotkeyName(s_.toggleKey) + ".",
              col::Muted);
}

App::~App() { Shutdown(); }

void App::Shutdown()
{
    engine_.Stop();
    UnbindHotkeys();
    if (saveAt_ != 0) {
        s_.Save();
        saveAt_ = 0;
    }
}

// ---------------------------------------------------------------- logique

void App::OnHotkey(int id)
{
    if (recorder_.Active()) return;
    if (id == kHotkeyPanic) Panic();
    if (id == kHotkeyToggle && !s_.holdMode) Toggle();
}

void App::Update()
{
    // Liste des applications chargée en arrière-plan.
    if (appsFuture_.valid() && appsFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        apps_ = appsFuture_.get();

    // Enregistrement d'un nouveau raccourci.
    if (recorder_.Active()) {
        Hotkey hk;
        bool cancelled = false;
        if (recorder_.Poll(hk, cancelled)) {
            Hotkey& slot = recordingSlot_ == kHotkeyToggle ? s_.toggleKey : s_.panicKey;
            const Hotkey& other = recordingSlot_ == kHotkeyToggle ? s_.panicKey : s_.toggleKey;
            if (cancelled) {
                Flash("Enregistrement annulé.", col::Muted);
            } else if (hk == other) {
                Flash("Ce raccourci est déjà utilisé par l'autre action.", col::Amber);
            } else {
                slot = hk;
                Flash("Raccourci enregistré : " + HotkeyName(hk), col::Green);
                Changed();
            }
            BindHotkeys();
        }
        return;
    }

    // Mode « Maintenir » : lag tant que la touche est enfoncée.
    if (s_.holdMode) {
        const bool pressed = IsHotkeyDown(s_.toggleKey);
        if (panicLatch_) {
            if (!pressed) panicLatch_ = false;
        } else if (pressed && !engine_.Active()) {
            holdActive_ = Activate();
            if (!holdActive_) panicLatch_ = true;  // ne pas réessayer en boucle
        } else if (!pressed && engine_.Active() && holdActive_) {
            holdActive_ = false;
            Deactivate();
        }
    }

    if (engine_.Faulted()) {
        engine_.Stop();
        Beep(3);
        Flash("Le pilote réseau a rencontré une erreur : lag coupé, réseau rétabli.", col::Red);
    }

    if (engine_.Active() && s_.autoOffSec > 0 && Now() - activatedAt_ >= s_.autoOffSec)
        Deactivate("auto");

    if (saveAt_ != 0 && Now() >= saveAt_) {
        s_.Save();
        saveAt_ = 0;
    }
}

void App::Toggle()
{
    if (engine_.Active())
        Deactivate();
    else
        Activate();
}

bool App::Activate()
{
    if (engine_.Active()) return true;
    if (s_.target.empty()) {
        Flash("Choisissez d'abord une application dans la liste.", col::Amber);
        Beep(3);
        return false;
    }
    engine_.ResetStats();
    std::string error;
    if (!engine_.Start(error)) {
        Beep(3);
        Flash(error.substr(0, error.find('\n')), col::Red);
        MessageBoxW(hwnd_, FromUtf8(error).c_str(), L"LagControl", MB_OK | MB_ICONERROR);
        return false;
    }
    panicLatch_ = false;
    activatedAt_ = Now();
    Beep(0);
    Flash("Lag activé sur " + ToUtf8(s_.target) + ".", col::Green);
    return true;
}

void App::Deactivate(const char* reason)
{
    if (!engine_.Active()) return;
    engine_.Stop();
    holdActive_ = false;
    Beep(1);
    Flash(reason ? "Désactivation automatique : réseau rétabli." : "Lag désactivé.", col::Muted);
}

void App::Panic()
{
    panicLatch_ = true;  // en mode Maintenir : attendre que la touche soit relâchée
    holdActive_ = false;
    engine_.Stop();
    Beep(2);
    Flash("ARRÊT D'URGENCE — tous les paquets ont été relâchés, le réseau est rétabli.", col::Red);
}

void App::SetTarget(const std::wstring& exeName)
{
    std::wstring name = BaseName(exeName);
    if (name.empty()) return;
    s_.target = name;
    tracker_.SetTarget(name);
    Changed();
    Flash("Cible : " + ToUtf8(name), col::Text);
}

void App::BindHotkeys()
{
    UnbindHotkeys();
    std::string errors;
    if (!s_.holdMode &&
        !RegisterHotKey(hwnd_, kHotkeyToggle, s_.toggleKey.mods | MOD_NOREPEAT, s_.toggleKey.vk))
        errors += HotkeyName(s_.toggleKey) + " ";
    if (!RegisterHotKey(hwnd_, kHotkeyPanic, s_.panicKey.mods | MOD_NOREPEAT, s_.panicKey.vk))
        errors += HotkeyName(s_.panicKey) + " ";
    if (!errors.empty())
        Flash("Raccourci déjà pris par une autre application : " + errors + "— choisissez-en un autre.", col::Amber);
}

void App::UnbindHotkeys()
{
    UnregisterHotKey(hwnd_, kHotkeyToggle);
    UnregisterHotKey(hwnd_, kHotkeyPanic);
}

void App::RefreshApps()
{
    if (appsFuture_.valid()) return;
    appsFuture_ = std::async(std::launch::async, ListApps);
}

void App::BrowseExe()
{
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Programmes (*.exe)\0*.exe\0Tous les fichiers\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choisir l'application à ralentir";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) SetTarget(file);
}

void App::ApplyEngineSettings()
{
    engine_.delayMs = s_.delayMs;
    engine_.jitterMs = s_.jitterMs;
    engine_.lossPct = s_.lossPct;
    engine_.direction = s_.direction;
}

void App::Changed()
{
    s_.Sanitize();
    ApplyEngineSettings();
    saveAt_ = Now() + 0.8;
}

void App::Flash(const std::string& text, unsigned color)
{
    message_ = text;
    messageColor_ = color;
}

void App::Beep(int kind)
{
    if (!s_.sounds) return;
    std::thread([kind] {
        switch (kind) {
        case 0: ::Beep(880, 70); ::Beep(1320, 90); break;               // activé
        case 1: ::Beep(1320, 70); ::Beep(660, 90); break;               // désactivé
        case 2: for (int i = 0; i < 3; ++i) ::Beep(1500, 80); break;    // urgence
        default: ::Beep(220, 180); break;                               // erreur
        }
    }).detach();
}

void App::ApplyTopmost()
{
    SetWindowPos(hwnd_, s_.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

// ---------------------------------------------------------------- interface

void App::Draw()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("LagControl", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

    DrawHeader();

    const float gap = S(16);
    const float barH = S(124);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float contentH = std::max(S(200), avail.y - barH - gap);
    const float leftW = std::floor((avail.x - gap) * 0.44f);

    DrawAppPicker(ImVec2(leftW, contentH));
    ImGui::SameLine(0, gap);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, 0);
    ImGui::BeginChild("right", ImVec2(0, contentH), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    DrawSettings();
    ImGui::Dummy(ImVec2(0, S(2)));
    DrawHotkeys();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y * 2));
    DrawActionBar(ImVec2(avail.x, barH));
    ImGui::End();
}

void App::DrawHeader()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = S(48);
    const float w = ImGui::GetContentRegionAvail().x;
    Logo(p, h);
    TextAt(dl, fonts.title, ImVec2(p.x + h + S(14), p.y - S(4)), col::Text, "LagControl");
    TextAt(dl, fonts.caption, ImVec2(p.x + h + S(15), p.y + S(28)), col::Muted,
           "Ajoute de la latence réseau à une seule application — les autres ne sont pas touchées");

    // Pastille d'état.
    const bool active = engine_.Active();
    std::string status = "INACTIF";
    if (active) {
        status = Fmt("LAG ACTIF  ·  %d ms", s_.delayMs);
        if (s_.autoOffSec > 0)
            status += Fmt("  ·  arrêt dans %d s", std::max(0, s_.autoOffSec - static_cast<int>(Now() - activatedAt_)));
    }
    const ImVec2 ts = TextSize(fonts.bold, status.c_str());
    const ImVec2 pillSize(ts.x + S(52), S(40));
    const ImVec2 pa(p.x + w - pillSize.x, p.y + (h - pillSize.y) * 0.5f);
    dl->AddRectFilled(pa, pa + pillSize, active ? col::RedDim : col::Card, pillSize.y * 0.5f);
    dl->AddRect(pa, pa + pillSize, active ? col::Red : col::Border, pillSize.y * 0.5f, 0, S(1.5f));
    const ImVec2 dot(pa.x + S(22), pa.y + pillSize.y * 0.5f);
    if (active) {
        float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.0f);
        dl->AddCircleFilled(dot, S(5) + S(4) * pulse, col::Rgb(0xf04343, 0.25f * (1.0f - pulse) + 0.1f), 24);
    }
    dl->AddCircleFilled(dot, S(5), active ? col::Red : col::Faint, 16);
    TextAt(dl, fonts.bold, ImVec2(pa.x + S(36), pa.y + (pillSize.y - ts.y) * 0.5f), active ? col::Rgb(0xffd6d6) : col::Muted,
           status.c_str());

    ImGui::Dummy(ImVec2(w, h));
    ImGui::Dummy(ImVec2(0, S(8)));
}

void App::DrawAppPicker(ImVec2 size)
{
    BeginCard("picker", "1", "Application cible", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Recherche + actualiser.
    const float btn = S(38);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12), (btn - fonts.regular->FontSize) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - S(8));
    ImGui::InputTextWithHint("##search", "Rechercher une application…", search_, sizeof(search_));
    ImGui::PopStyleVar(2);
    ImGui::SameLine(0, S(8));
    if (IconButton("refresh", ImVec2(btn, btn), 0, appsFuture_.valid())) RefreshApps();

    ImGui::Dummy(ImVec2(0, S(2)));
    if (ui::Toggle("Seulement les applications connectées", &s_.networkOnly)) Changed();
    ImGui::Dummy(ImVec2(0, S(4)));

    // Liste.
    const float targetH = S(78);
    const float listH = ImGui::GetContentRegionAvail().y - targetH - S(12);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(6), S(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(4), S(3)));
    ImGui::BeginChild("list", ImVec2(0, listH), ImGuiChildFlags_AlwaysUseWindowPadding);

    std::wstring query = ToLower(FromUtf8(search_));
    while (!query.empty() && query.back() == L' ') query.pop_back();
    const std::wstring target = ToLower(s_.target);
    int shown = 0;
    for (const AppInfo& app : apps_) {
        const bool selected = app.key == target;
        if (!query.empty() && app.key.find(query) == std::wstring::npos) continue;
        if (s_.networkOnly && app.connections == 0 && !selected) continue;
        ++shown;

        ImGui::PushID(app.utf8.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = S(52);
        const bool clicked = ImGui::InvisibleButton("row", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::PopID();
        if (clicked) SetTarget(app.name);

        ImDrawList* ldl = ImGui::GetWindowDrawList();
        if (selected) {
            ldl->AddRectFilled(p, p + ImVec2(w, h), col::AccentDim, S(10));
            ldl->AddRect(p, p + ImVec2(w, h), col::Accent, S(10), 0, S(1.5f));
        } else if (hovered) {
            ldl->AddRectFilled(p, p + ImVec2(w, h), col::Hover, S(10));
        }
        const float av = S(34);
        const ImVec2 ap(p.x + S(10), p.y + (h - av) * 0.5f);
        ldl->AddRectFilled(ap, ap + ImVec2(av, av), AvatarColor(app.key), S(10));
        std::wstring first = app.name.substr(0, 1);
        CharUpperBuffW(first.data(), static_cast<DWORD>(first.size()));
        const std::string letter = ToUtf8(first);
        const ImVec2 ls = TextSize(fonts.boldLarge, letter.c_str());
        TextAt(ldl, fonts.boldLarge, ap + (ImVec2(av, av) - ls) * 0.5f, col::White, letter.c_str());

        TextAt(ldl, fonts.bold, ImVec2(p.x + S(56), p.y + S(7)), col::Text, app.utf8.c_str());
        std::string sub = Fmt("%d processus", static_cast<int>(app.pids.size()));
        sub += app.connections ? Fmt("  ·  %d connexion%s", app.connections, app.connections > 1 ? "s" : "")
                               : std::string("  ·  hors ligne");
        TextAt(ldl, fonts.caption, ImVec2(p.x + S(56), p.y + S(28)), col::Muted, sub.c_str());
        ldl->AddCircleFilled(ImVec2(p.x + w - S(18), p.y + h * 0.5f), S(4.5f),
                             app.connections ? col::Green : col::Faint, 16);
    }

    if (shown == 0) {
        ImGui::Dummy(ImVec2(0, S(24)));
        const char* msg = appsFuture_.valid() ? "Chargement des applications…"
                          : query.empty()    ? "Aucune application connectée trouvée."
                                             : "Aucune application ne correspond à la recherche.";
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - TextSize(fonts.regular, msg).x) * 0.5f);
        TextColored(fonts.regular, col::Muted, msg);
        if (!query.empty()) {
            std::wstring name = FromUtf8(search_);
            while (!name.empty() && name.back() == L' ') name.pop_back();
            if (ToLower(name).size() < 4 || ToLower(name).substr(ToLower(name).size() - 4) != L".exe") name += L".exe";
            std::string label = "Cibler quand même « " + ToUtf8(name) + " »";
            const float bw = std::min(w, TextSize(fonts.bold, label.c_str()).x + S(30));
            ImGui::Dummy(ImVec2(0, S(6)));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - bw) * 0.5f);
            if (FlatButton((label + "##use").c_str(), ImVec2(bw, S(34)), true)) SetTarget(name);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // Cible actuelle.
    ImGui::Dummy(ImVec2(0, S(4)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p, p + ImVec2(w, targetH), col::Card2, S(12));
    TextAt(dl, fonts.caption, p + ImVec2(S(16), S(9)), col::Faint, "CIBLE ACTUELLE");
    const std::string targetName = s_.target.empty() ? "Aucune" : ToUtf8(s_.target);
    TextAt(dl, fonts.boldLarge, p + ImVec2(S(16), S(25)), s_.target.empty() ? col::Faint : col::Text, targetName.c_str());
    std::string info;
    if (s_.target.empty()) {
        info = "Cliquez sur une application dans la liste";
    } else if (tracker_.ProcessCount() == 0) {
        info = "Pas lancée pour l'instant — ciblée dès son démarrage";
    } else {
        int n = tracker_.PortCount();
        info = Fmt("%d processus  ·  %d connexion%s suivie%s", tracker_.ProcessCount(), n, n > 1 ? "s" : "",
                   n > 1 ? "s" : "");
    }
    TextAt(dl, fonts.caption, p + ImVec2(S(16), S(51)), col::Muted, info.c_str());

    const ImVec2 bs(S(130), S(36));
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - bs.x - S(14), p.y + (targetH - bs.y) * 0.5f));
    if (FlatButton("Parcourir .exe", bs)) BrowseExe();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, targetH));
    EndCard();
}

void App::DrawSettings()
{
    BeginCard("settings", "2", "Réglages du lag", ImVec2(0, 0), true);

    // Ping estimé, dans l'en-tête de la carte.
    {
        const std::string ping = Fmt("≈ +%d ms de ping", lag::AddedPingMs(s_.delayMs, s_.direction));
        const ImVec2 ts = TextSize(fonts.bold, ping.c_str());
        const ImVec2 wp = ImGui::GetWindowPos();
        const float right = wp.x + ImGui::GetWindowWidth() - S(18);
        ImGui::GetWindowDrawList()->AddText(fonts.bold, fonts.bold->FontSize,
                                            ImVec2(right - ts.x, wp.y + S(16) + (S(26) - ts.y) * 0.5f), col::Amber,
                                            ping.c_str());
    }

    float delay = static_cast<float>(s_.delayMs);
    if (ValueSlider("delay", "Latence", "ajoutée à chaque paquet", "ms", &delay, 2000, 10000, false)) {
        s_.delayMs = static_cast<int>(delay);
        Changed();
    }

    // Préréglages.
    ImGui::Dummy(ImVec2(0, S(2)));
    const float avail = ImGui::GetContentRegionAvail().x;
    const int n = static_cast<int>(sizeof(kDelayPresets) / sizeof(kDelayPresets[0]));
    const float pw = (avail - S(6) * static_cast<float>(n - 1)) / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0, S(6));
        const int ms = kDelayPresets[i];
        std::string label = ms >= 1000 ? Fmt("%d s##p%d", ms / 1000, i) : Fmt("%d##p%d", ms, i);
        if (FlatButton(label.c_str(), ImVec2(pw, S(28)), s_.delayMs == ms)) {
            s_.delayMs = ms;
            Changed();
        }
    }
    ImGui::Dummy(ImVec2(0, S(6)));

    float jitter = static_cast<float>(s_.jitterMs);
    if (ValueSlider("jitter", "Variation", "lag irrégulier (±)", "ms", &jitter, 500, 2000, false)) {
        s_.jitterMs = static_cast<int>(jitter);
        Changed();
    }
    ImGui::Dummy(ImVec2(0, S(6)));

    if (ValueSlider("loss", "Perte de paquets", "paquets jetés", "%", &s_.lossPct, 100, 100, true)) Changed();
    ImGui::Dummy(ImVec2(0, S(6)));

    SettingRow("Sens du trafic", "« Les deux » double le ping ajouté", S(270), S(34), [&] {
        if (Segmented("dir", kDirectionLabels, 3, &s_.direction, S(270))) Changed();
    });
    EndCard();
}

void App::DrawHotkeys()
{
    BeginCard("hotkeys", "3", "Raccourcis & sécurité", ImVec2(0, 0), true);
    const float kw = S(150);

    SettingRow("Activer / désactiver", "cliquez puis appuyez sur la touche voulue", kw, S(36), [&] {
        if (KeyCap("kc_toggle", HotkeyName(s_.toggleKey).c_str(), recorder_.Active() && recordingSlot_ == kHotkeyToggle,
                   col::Text, kw) &&
            !recorder_.Active()) {
            UnbindHotkeys();
            recordingSlot_ = kHotkeyToggle;
            recorder_.Begin();
            Flash("Appuyez sur la touche ou la combinaison voulue (Échap pour annuler).", col::AccentHover);
        }
    });

    SettingRow("Mode du raccourci", "Maintenir = lag tant que la touche est enfoncée", S(220), S(34), [&] {
        int mode = s_.holdMode ? 1 : 0;
        if (Segmented("mode", kModeLabels, 2, &mode, S(220))) {
            s_.holdMode = mode == 1;
            if (engine_.Active()) Deactivate();
            BindHotkeys();
            Changed();
        }
    });

    SettingRow("Arrêt d'urgence", "coupe tout et rétablit le réseau", kw, S(36), [&] {
        if (KeyCap("kc_panic", HotkeyName(s_.panicKey).c_str(), recorder_.Active() && recordingSlot_ == kHotkeyPanic,
                   col::Red, kw) &&
            !recorder_.Active()) {
            UnbindHotkeys();
            recordingSlot_ = kHotkeyPanic;
            recorder_.Begin();
            Flash("Appuyez sur la touche ou la combinaison voulue (Échap pour annuler).", col::AccentHover);
        }
    });

    SettingRow("Désactivation automatique", "sécurité si vous oubliez de couper", kw, S(34), [&] {
        const char* current = "Personnalisé";
        for (const auto& a : kAutoOff)
            if (a.seconds == s_.autoOffSec) current = a.label;
        ImGui::SetNextItemWidth(kw);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12), (S(34) - fonts.regular->FontSize) * 0.5f));
        if (ImGui::BeginCombo("##autooff", current)) {
            for (const auto& a : kAutoOff) {
                if (ImGui::Selectable(a.label, a.seconds == s_.autoOffSec)) {
                    s_.autoOffSec = a.seconds;
                    if (engine_.Active()) activatedAt_ = Now();
                    Changed();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
    });

    if (ui::Toggle("Sons de confirmation", &s_.sounds)) Changed();
    ImGui::SameLine(0, S(28));
    if (ui::Toggle("Toujours au premier plan", &s_.topmost)) {
        ApplyTopmost();
        Changed();
    }
    EndCard();
}

void App::DrawActionBar(ImVec2 size)
{
    BeginCard("actions", nullptr, nullptr, size);
    const bool active = engine_.Active();
    const float gap = S(12);
    const float rowH = S(66);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float toggleW = S(300), panicW = S(250);
    const float tileW = std::min(S(112), (avail - toggleW - panicW - gap * 5) / 4.0f);

    const std::string hk = HotkeyName(s_.toggleKey);
    if (active) {
        const std::string sub = s_.holdMode ? "relâchez " + hk : "ou touche " + hk;
        if (BigButton("toggle", "DÉSACTIVER LE LAG", sub.c_str(), ImVec2(toggleW, rowH), col::Red, col::RedHover,
                      col::White, 1))
            Deactivate();
    } else {
        const std::string sub = s_.holdMode ? "maintenez " + hk : "ou touche " + hk;
        if (BigButton("toggle", "ACTIVER LE LAG", sub.c_str(), ImVec2(toggleW, rowH), col::Green, col::GreenHover,
                      col::Rgb(0x05230f), 0))
            Activate();
    }
    ImGui::SameLine(0, gap);
    if (PanicButton("panic", ImVec2(panicW, rowH))) Panic();

    ImGui::SameLine(0, 0);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - toggleW - panicW - gap - tileW * 4 - gap * 3);
    const std::string delayed = Thousands(engine_.delayed.load());
    const std::string dropped = Thousands(engine_.dropped.load());
    const std::string queued = Thousands(engine_.Queued());
    const std::string ports = std::to_string(tracker_.PortCount());
    StatTile(delayed.c_str(), "Retardés", ImVec2(tileW, rowH));
    ImGui::SameLine(0, gap);
    StatTile(dropped.c_str(), "Perdus", ImVec2(tileW, rowH));
    ImGui::SameLine(0, gap);
    StatTile(queued.c_str(), "En attente", ImVec2(tileW, rowH));
    ImGui::SameLine(0, gap);
    StatTile(ports.c_str(), "Connexions", ImVec2(tileW, rowH));

    ImGui::Dummy(ImVec2(0, S(2)));
    TextColored(fonts.regular, messageColor_ ? messageColor_ : col::Muted, message_.c_str());
    EndCard();
}

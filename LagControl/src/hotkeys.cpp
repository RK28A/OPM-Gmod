#include "hotkeys.h"

#include <windows.h>

#include "processes.h"

namespace {

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool IsModifier(unsigned vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool IsExtended(unsigned vk)
{
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_NUMLOCK: case VK_DIVIDE:
    case VK_SNAPSHOT: case VK_APPS:
        return true;
    default:
        return false;
    }
}

unsigned CurrentMods()
{
    unsigned mods = 0;
    if (Down(VK_CONTROL)) mods |= MOD_CONTROL;
    if (Down(VK_SHIFT)) mods |= MOD_SHIFT;
    if (Down(VK_MENU)) mods |= MOD_ALT;
    if (Down(VK_LWIN) || Down(VK_RWIN)) mods |= MOD_WIN;
    return mods;
}

std::string KeyName(unsigned vk)
{
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    switch (vk) {
    case VK_PAUSE: return "PAUSE";
    case VK_SNAPSHOT: return "IMPR. ÉCRAN";
    case VK_SCROLL: return "ARRÊT DÉFIL";
    case VK_SPACE: return "ESPACE";
    }
    LONG scan = static_cast<LONG>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
    if (IsExtended(vk)) scan |= 1 << 24;
    wchar_t name[64] = {};
    if (GetKeyNameTextW(scan, name, 64) > 0) {
        CharUpperW(name);
        return ToUtf8(name);
    }
    return "TOUCHE " + std::to_string(vk);
}

}  // namespace

std::string HotkeyName(const Hotkey& hk)
{
    if (hk.vk == 0) return "—";
    std::string s;
    if (hk.mods & MOD_CONTROL) s += "CTRL+";
    if (hk.mods & MOD_SHIFT) s += "MAJ+";
    if (hk.mods & MOD_ALT) s += "ALT+";
    if (hk.mods & MOD_WIN) s += "WIN+";
    return s + KeyName(hk.vk);
}

bool IsHotkeyDown(const Hotkey& hk)
{
    if (hk.vk == 0 || !Down(static_cast<int>(hk.vk))) return false;
    if ((hk.mods & MOD_CONTROL) && !Down(VK_CONTROL)) return false;
    if ((hk.mods & MOD_SHIFT) && !Down(VK_SHIFT)) return false;
    if ((hk.mods & MOD_ALT) && !Down(VK_MENU)) return false;
    if ((hk.mods & MOD_WIN) && !Down(VK_LWIN) && !Down(VK_RWIN)) return false;
    return true;
}

void HotkeyRecorder::Begin()
{
    active_ = true;
    waitRelease_ = true;
}

bool HotkeyRecorder::Poll(Hotkey& out, bool& cancelled)
{
    cancelled = false;
    if (!active_) return false;

    if (waitRelease_) {  // ignorer la touche éventuellement encore enfoncée
        for (unsigned vk = 0x08; vk <= 0xFE; ++vk)
            if (!IsModifier(vk) && Down(static_cast<int>(vk))) return false;
        waitRelease_ = false;
        return false;
    }

    if (Down(VK_ESCAPE)) {
        active_ = false;
        cancelled = true;
        return true;
    }
    for (unsigned vk = 0x08; vk <= 0xFE; ++vk) {
        if (IsModifier(vk) || vk == VK_ESCAPE || !Down(static_cast<int>(vk))) continue;
        out = Hotkey{vk, CurrentMods()};
        active_ = false;
        return true;
    }
    return false;
}

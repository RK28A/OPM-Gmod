#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace {

const wchar_t* kSection = L"LagControl";

std::wstring ConfigPath()
{
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"LagControl.ini";
    std::wstring dir = std::wstring(appdata) + L"\\LagControl";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

int ReadInt(const std::wstring& path, const wchar_t* key, int def)
{
    return static_cast<int>(GetPrivateProfileIntW(kSection, key, def, path.c_str()));
}

void WriteInt(const std::wstring& path, const wchar_t* key, long long value)
{
    WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), path.c_str());
}

}  // namespace

void Settings::Load()
{
    const std::wstring path = ConfigPath();
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(kSection, L"target", L"", buf, MAX_PATH, path.c_str());
    target = buf;
    delayMs = ReadInt(path, L"delay_ms", delayMs);
    jitterMs = ReadInt(path, L"jitter_ms", jitterMs);
    lossPct = static_cast<float>(ReadInt(path, L"loss_tenths", static_cast<int>(lossPct * 10))) / 10.0f;
    direction = ReadInt(path, L"direction", direction);
    toggleKey.vk = static_cast<unsigned>(ReadInt(path, L"toggle_vk", static_cast<int>(toggleKey.vk)));
    toggleKey.mods = static_cast<unsigned>(ReadInt(path, L"toggle_mods", static_cast<int>(toggleKey.mods)));
    panicKey.vk = static_cast<unsigned>(ReadInt(path, L"panic_vk", static_cast<int>(panicKey.vk)));
    panicKey.mods = static_cast<unsigned>(ReadInt(path, L"panic_mods", static_cast<int>(panicKey.mods)));
    holdMode = ReadInt(path, L"hold_mode", holdMode) != 0;
    autoOffSec = ReadInt(path, L"auto_off_s", autoOffSec);
    sounds = ReadInt(path, L"sounds", sounds) != 0;
    topmost = ReadInt(path, L"topmost", topmost) != 0;
    networkOnly = ReadInt(path, L"network_only", networkOnly) != 0;
    Sanitize();
}

void Settings::Save() const
{
    const std::wstring path = ConfigPath();
    WritePrivateProfileStringW(kSection, L"target", target.c_str(), path.c_str());
    WriteInt(path, L"delay_ms", delayMs);
    WriteInt(path, L"jitter_ms", jitterMs);
    WriteInt(path, L"loss_tenths", static_cast<long long>(lossPct * 10.0f + 0.5f));
    WriteInt(path, L"direction", direction);
    WriteInt(path, L"toggle_vk", toggleKey.vk);
    WriteInt(path, L"toggle_mods", toggleKey.mods);
    WriteInt(path, L"panic_vk", panicKey.vk);
    WriteInt(path, L"panic_mods", panicKey.mods);
    WriteInt(path, L"hold_mode", holdMode);
    WriteInt(path, L"auto_off_s", autoOffSec);
    WriteInt(path, L"sounds", sounds);
    WriteInt(path, L"topmost", topmost);
    WriteInt(path, L"network_only", networkOnly);
}

void Settings::Sanitize()
{
    delayMs = std::clamp(delayMs, 0, 10000);
    jitterMs = std::clamp(jitterMs, 0, 2000);
    lossPct = std::clamp(lossPct, 0.0f, 100.0f);
    autoOffSec = std::clamp(autoOffSec, 0, 3600);
    if (direction < lag::DirIn || direction > lag::DirBoth) direction = lag::DirBoth;
    if (toggleKey.vk == 0 || toggleKey.vk > 0xFE) toggleKey = Hotkey{0x77, 0};
    if (panicKey.vk == 0 || panicKey.vk > 0xFE) panicKey = Hotkey{0x7B, MOD_CONTROL};
    toggleKey.mods &= MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
    panicKey.mods &= MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
}

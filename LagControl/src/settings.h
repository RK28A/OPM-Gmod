// Réglages persistants : %APPDATA%\LagControl\config.ini
#pragma once

#include <string>

#include "hotkeys.h"
#include "lag_core.h"

struct Settings {
    std::wstring target;
    int delayMs = 300;
    int jitterMs = 0;
    float lossPct = 0.0f;
    int direction = lag::DirBoth;
    Hotkey toggleKey{0x77 /* F8 */, 0};
    Hotkey panicKey{0x7B /* F12 */, 0x0002 /* MOD_CONTROL */};
    bool holdMode = false;
    int autoOffSec = 0;
    bool sounds = true;
    bool topmost = false;
    bool networkOnly = true;

    void Load();
    void Save() const;
    void Sanitize();
};

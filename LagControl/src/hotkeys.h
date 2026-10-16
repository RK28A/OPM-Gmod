// Raccourcis clavier globaux (fonctionnent même quand le jeu a le focus).
#pragma once

#include <string>

struct Hotkey {
    unsigned vk = 0;    // code de touche virtuelle (VK_*)
    unsigned mods = 0;  // MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN

    bool operator==(const Hotkey& o) const { return vk == o.vk && mods == o.mods; }
    bool operator!=(const Hotkey& o) const { return !(*this == o); }
};

std::string HotkeyName(const Hotkey& hk);  // ex. "CTRL+F12"
bool IsHotkeyDown(const Hotkey& hk);       // état réel du clavier, global

// Enregistrement d'une nouvelle combinaison : appeler Poll() à chaque image.
class HotkeyRecorder {
public:
    void Begin();
    bool Active() const { return active_; }
    // Renvoie true quand l'enregistrement est terminé ; cancelled = Échap.
    bool Poll(Hotkey& out, bool& cancelled);

private:
    bool active_ = false;
    bool waitRelease_ = false;
};

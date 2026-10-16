// État de l'application et dessin de l'interface.
#pragma once

#include <windows.h>

#include <future>
#include <string>
#include <vector>

#include "engine.h"
#include "hotkeys.h"
#include "processes.h"
#include "settings.h"
#include "ui.h"

class App {
public:
    explicit App(HWND hwnd);
    ~App();

    void OnHotkey(int id);  // WM_HOTKEY
    void Update();          // logique (appelée à chaque tour, même fenêtre réduite)
    void Draw();            // interface ImGui
    void Shutdown();

    enum : int { kHotkeyToggle = 1, kHotkeyPanic = 2 };

private:
    // actions
    void Toggle();
    bool Activate();
    void Deactivate(const char* reason = nullptr);
    void Panic();
    void SetTarget(const std::wstring& exeName);
    void BindHotkeys();
    void UnbindHotkeys();
    void RefreshApps();
    void BrowseExe();
    void ApplyEngineSettings();
    void Changed();  // sauvegarde différée
    void Flash(const std::string& text, unsigned color);
    void Beep(int kind);
    void ApplyTopmost();

    // interface
    void DrawHeader();
    void DrawAppPicker(ImVec2 size);
    void DrawSettings();
    void DrawHotkeys();
    void DrawActionBar(ImVec2 size);

    HWND hwnd_;
    Settings s_;
    PortTracker tracker_;
    LagEngine engine_{tracker_};

    std::vector<AppInfo> apps_;
    std::future<std::vector<AppInfo>> appsFuture_;
    char search_[128] = {};

    std::string message_;
    unsigned messageColor_ = 0;

    HotkeyRecorder recorder_;
    int recordingSlot_ = 0;  // kHotkeyToggle / kHotkeyPanic quand recorder_ est actif

    double activatedAt_ = 0;
    bool holdActive_ = false;
    bool panicLatch_ = false;
    double saveAt_ = 0;
};

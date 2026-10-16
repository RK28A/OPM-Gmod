// LagControl — fenêtre Win32 + Direct3D 11 + Dear ImGui.
#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <memory>

#include "app.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "ui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;
App* g_app = nullptr;

void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    if (back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

bool CreateDevice(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)  // pas de GPU compatible : rendu logiciel
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swapChain, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDevice()
{
    CleanupRenderTarget();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
    g_swapChain = nullptr;
    g_context = nullptr;
    g_device = nullptr;
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_resizeW = LOWORD(lParam);
            g_resizeH = HIWORD(lParam);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = static_cast<LONG>(ui::S(1000));
        mmi->ptMinTrackSize.y = static_cast<LONG>(ui::S(700));
        return 0;
    }
    case WM_HOTKEY:
        if (g_app) g_app->OnHotkey(static_cast<int>(wParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // pas de menu sur ALT
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Une seule instance : sinon deux moteurs se battraient pour les paquets.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LagControlSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"LagControlWindow", nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    ui::scale = dpi;

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, LoadIconW(instance, L"IDI_APP"),
                   LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"LagControlWindow", nullptr};
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = std::min(static_cast<int>(1160 * dpi), static_cast<int>(work.right - work.left));
    const int h = std::min(static_cast<int>(900 * dpi), static_cast<int>(work.bottom - work.top));
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"LagControl", WS_OVERLAPPEDWINDOW,
                                work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                                w, h, nullptr, nullptr, instance, nullptr);

    // Barre de titre sombre (Windows 10 20H1+ / 11).
    BOOL dark = TRUE;
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        if (auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(
                reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute"))))
            fn(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    }

    if (!CreateDevice(hwnd)) {
        CleanupDevice();
        MessageBoxW(nullptr, L"Impossible d'initialiser Direct3D 11.", L"LagControl", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ui::Setup(dpi);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    auto app = std::make_unique<App>(hwnd);
    g_app = app.get();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        app->Update();

        // Fenêtre réduite : on garde la logique (raccourcis) mais on ne dessine pas.
        if (IsIconic(hwnd)) {
            Sleep(15);
            continue;
        }
        if (g_resizeW != 0 && g_resizeH != 0) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app->Draw();
        ImGui::Render();

        const float clear[4] = {0.047f, 0.055f, 0.075f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);

        // Fenêtre en arrière-plan (jeu au premier plan) : ~30 images/s suffisent.
        if (GetForegroundWindow() != hwnd) Sleep(16);
    }

    app->Shutdown();
    g_app = nullptr;
    app.reset();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (mutex) CloseHandle(mutex);
    return 0;
}

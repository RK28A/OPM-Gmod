#include <Windows.h>
#include <string>
#include <string_view>
#include <iostream>
#include <fstream>
#include <thread>

#include "Memory.h"

#include "Interface.h"
#include "globals.hpp"
#include "hacks/Debug.h"

#include "hooks/DrawModelExecute.h"
#include "hooks/CreateMove.h"
#include "hooks/FrameStageNotify.h"
#include "hooks/RenderView.h"
#include "hooks/Present.h"
#include "hooks/PaintTraverse.h"
#include "hooks/RunStringEx.h"
#include "hooks/ProcessGMODServerToClient.h"
#include "hooks/RunCommand.h"
#include "hooks/Paint.h"


#include "hacks/ConVarSpoofing.h"
#include "engine/inetmessage.h"

// Resolves one signature-scanned pointer, keeping the null out of the
// arithmetic.  findPattern(...) + OFFSET and
// GetRealFromRelative(findPattern(...), ...) both read through the result,
// so a failed scan used to crash here, one line *before* the `if
// (Globals::bSendpacket)` guards the review added downstream, which is why
// those guards never fired.
static char* ResolveScan(const char* module, std::string_view pattern, std::string_view name,
    int preOffset, int offset, int instructionSize, bool relative = true)
{
    const char* match = findPattern(module, pattern, name);
    if (!match)
        return nullptr; // findPattern has already recorded the failure

    return GetRealFromRelative((char*)match + preOffset, offset, instructionSize, relative);
}

// Installs a VMT hook, but first checks the target object and the specific
// vtable slot are actually mapped, and logs the outcome by name.  After a
// Garry's Mod update a hard-coded offset/scan can resolve an interface to a
// wild pointer; hooking through it used to take the whole game down (the
// RenderView hook was the usual casualty).  Now it is logged and skipped, so
// the rest of the module still loads and debug.log points at the stale offset.
template<typename T>
static T GuardedVMTHook(const char* name, PVOID** src, PVOID dst, int index, bool noRestore = false)
{
    if (!MemIsReadable(src) || !MemIsReadable(*src, (static_cast<size_t>(index) + 1) * sizeof(PVOID)))
    {
        DBG_ERROR("Hook '%s' skipped: object %p unreadable (stale offset/scan after a game update?)", name, (void*)src);
        return (T)nullptr;
    }
    DBG_INFO("Hook '%s' installed (object %p, vtable index %d)", name, (void*)src, index);
    return VMTHook<T>(src, dst, index, noRestore);
}

// Obtains the IDirect3DDevice9 vtable by spinning up a throwaway device, so
// Present can be VMT-hooked (index 17) instead of scanning gameoverlayrenderer
// for the overlay's stored Present pointer -- a byte pattern that broke on every
// Steam/overlay update. The vtable lives in d3d9.dll and is shared by every
// device instance, so patching its Present slot also redirects the game's real
// device. The COM vtable layout is fixed by the OS, so unlike the pattern this
// does not rot. Returns nullptr (logged) if the probe device cannot be made.
static void** GetD3D9DeviceVTable()
{
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d)
    {
        DBG_ERROR("Direct3DCreate9 failed; cannot resolve the Present vtable");
        return nullptr;
    }

    // A dedicated, never-shown window for the probe device, so it never fights
    // the game's own device for its window (an exclusive-fullscreen game device
    // owns "Valve001"). The window and its class are torn down before returning.
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "GmodSdkD3DProbe";
    RegisterClassExA(&wc); // harmless if already registered from a prior probe

    // Never shown; given a real (non-zero) size anyway so no HAL driver balks at
    // the focus window.
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "", WS_OVERLAPPED,
        0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);

    // Explicit back-buffer size and a format taken from the adapter's current
    // display mode. A 1x1 WS_OVERLAPPED window has a 0x0 client area, so leaving
    // these zero makes the runtime derive a 0x0 back buffer and CreateDevice
    // fails with E_INVALIDARG (0x80070057) -- which is exactly what happened.
    D3DDISPLAYMODE dm = {};
    if (FAILED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &dm)))
        dm.Format = D3DFMT_X8R8G8B8;

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferWidth = 2;
    pp.BackBufferHeight = 2;
    pp.BackBufferCount = 1;
    pp.BackBufferFormat = dm.Format;

    IDirect3DDevice9* device = nullptr;
    // HAL only. Every HAL IDirect3DDevice9 from this d3d9.dll shares one vtable
    // -- the same one the game's device uses -- which is exactly what we must
    // patch. A REF device's vtable lives in d3dref9.dll: it is a *different*
    // table (so patching it would not touch the game), and d3dref9 unloads when
    // the probe is released, leaving a dangling pointer that reads as
    // "unreadable". So REF is deliberately not a fallback; only the T&L flag is
    // retried.
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr))
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);

    void** vtable = nullptr;
    if (SUCCEEDED(hr) && device)
    {
        vtable = *reinterpret_cast<void***>(device);
        DBG_INFO("Present probe: HAL device %p, vtable %p, Present(vtable[17]) %p",
            (void*)device, (void*)vtable, vtable ? vtable[17] : nullptr);
        device->Release();
    }
    else
    {
        DBG_ERROR("Probe CreateDevice failed (0x%08lX); the game is likely in "
            "exclusive fullscreen -- try borderless/windowed",
            static_cast<unsigned long>(hr));
    }

    d3d->Release();
    if (hwnd)
        DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return vtable;
}

void Main()
{
    ZeroMemory(Settings::ScriptInput, sizeof(Settings::ScriptInput));

    // Still needed: the movement optimiser in Misc.h calls rand() directly, and
    // so does tier0/Vector.h's Random().  The two call sites this project owns
    // (RandomString and the anti-aim patterns) moved to <random>, but leaving
    // these unseeded would make them produce the same sequence every launch.
    srand(static_cast<unsigned int>(time(nullptr)));
#ifdef _DEBUG
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONIN$", "r", stdin);
    SetConsoleTitle(L"GMod SDK - WIP - Coded by t.me/Gaztoof");
#endif
    ConColorMsg = (MsgFn)GetProcAddress(GetModuleHandleW(L"tier0.dll"), ConColorMsgDec);

    // Install the crash reporter / logger before the signature scans below, so
    // an early fault (e.g. a pattern that no longer matches after an update)
    // still produces a crash.log instead of a silent game crash.
    Debug::Install();
    DBG_INFO("Main() start");

    ConPrint("Successfully injected!", Color(0, 255, 0));
    ConfigSystem::HandleConfig("Default", ConfigSystem::configHandle::Load);

    // Every scan first, then one check.  Nothing is installed if any of them
    // failed: a half-resolved module is a crash waiting for the first frame,
    // and reporting all four at once is what makes a game update diagnosable.
    char* sendPacket = ResolveScan("engine", CL_MovePattern, "CL_MOVE", 0, 0x1, 5);
    Globals::bSendpacket = sendPacket ? (bool*)(sendPacket + BSendPacketOffset) : nullptr;

    // These three are x64-only in globals.hpp: PredictionSeedPattern,
    // HostNamePattern and MoveHelperPattern are undefined for Win32 (the
    // upstream baseline never provided x86 signatures for them, and MSBuild
    // Win32 never ran to notice).  In a Win32 build the pointers stay null and
    // the null-guards downstream keep the affected features (prediction, fake
    // lag path, script dumper's host label, movement replay) off.  Left in
    // place until someone re-reverses the signatures for the x86 build.
#ifdef _WIN64
    Globals::predictionRandomSeed = (unsigned int*)ResolveScan("client", PredictionSeedPattern, "predictionRandomSeed", 0x3, 0x2, 6);
    Globals::hostName = ResolveScan("client", HostNamePattern, "HostName", 0, 0x3, 7);
    MoveHelper = ResolveScan("client", MoveHelperPattern, "MoveHelper", 0, 0x3, 7); // https://i.imgur.com/p3C93PT.png
#else
    Globals::predictionRandomSeed = nullptr;
    Globals::hostName = nullptr;
    MoveHelper = nullptr;
#endif

    // Present is no longer scanned here: it is hooked through the D3D9 device
    // vtable (see GetD3D9DeviceVTable / the Present hook below), so a stale Steam
    // overlay pattern can no longer land in missingPatterns and abort the whole
    // load.
    if (!missingPatterns.empty())
    {
        std::string report = "Signature scan failed for:";
        for (const std::string& name : missingPatterns)
            report += " " + name;

        report += " -- the game has probably updated.  Nothing was hooked.";
        ConPrint(report.c_str(), Color(255, 80, 80));
        return;
    }

    // The protection was changed and never put back, leaving a page of
    // engine.dll permanently PAGE_EXECUTE_READWRITE.  BytePatch and VMTHook both
    // restore theirs; this one did not.
    // It has to stay writable for the whole session -- bSendpacket is written
    // on most frames -- so this cannot be restored straight away.  What it does
    // not need is execute permission: the page was left PAGE_EXECUTE_READWRITE
    // for good, where BytePatch and VMTHook both restore theirs.  The saved
    // value is put back by PerformUnload.
    if (!VirtualProtect(Globals::bSendpacket, sizeof(bool), PAGE_READWRITE, &Globals::bSendpacketProtection))
    {
        Globals::bSendpacketProtection = 0;
        ConPrint("Could not make bSendpacket writable", Color(255, 200, 0));
    }
    
    EngineClient = (CEngineClient*)GetInterface("engine.dll", "VEngineClient015");
    LuaShared = (CLuaShared*)GetInterface("lua_shared.dll", "LUASHARED003");
    ClientEntityList = (CClientEntityList*)GetInterface("client.dll", "VClientEntityList003");
    CHLclient = (CHLClient*)GetInterface("client.dll", "VClient017");
    MaterialSystem = (CMaterialSystem*)GetInterface("materialsystem.dll", "VMaterialSystem080");
    InputSystem = (CInputSystem*)GetInterface("inputsystem.dll", "InputSystemVersion001");
    CVar = (CCvar*)GetInterface("vstdlib.dll", "VEngineCvar007");
    ModelRender = (CModelRender*)GetInterface("engine.dll", "VEngineModel016");
    RenderView = (CVRenderView*)GetInterface("engine.dll", "VEngineRenderView014");
    EngineTrace = (IEngineTrace*)GetInterface("engine.dll", "EngineTraceClient003");
    IVDebugOverlay = (CIVDebugOverlay*)GetInterface("engine.dll", "VDebugOverlay003");
    GameEventManager = (CGameEventManager*)GetInterface("engine.dll", "GAMEEVENTSMANAGER002");
    MatSystemSurface = (CMatSystemSurface*)GetInterface("vguimatsurface.dll", "VGUI_Surface030");
    PanelWrapper = (VPanelWrapper*)GetInterface("vgui2.dll", "VGUI_Panel009");
    PhysicsSurfaceProps = (CPhysicsSurfaceProps*)GetInterface("vphysics.dll", "VPhysicsSurfaceProps001");
    Prediction = (CPrediction*)GetInterface("client.dll", "VClientPrediction001");
    GameMovement = (CGameMovement*)GetInterface("client.dll", "GameMovement001");
    EngineVGui = (void*)GetInterface("engine.dll", "VEngineVGui001"); // Eventually implement that?
    ModelInfo = (CModelInfo*)GetInterface("engine.dll", "VModelInfoClient006");

    // x64: thats directly the vtable pointer // CEngineClient::IsPaused points to clientstate https://i.imgur.com/4aWvQbs.png
    ClientState = GetRealFromRelative((*(char***)(EngineClient))[84], CClientStateOffset, CClientStateSize, false);
    ViewRender = GetVMT<CViewRender>((uintptr_t)CHLclient, 2, ViewRenderOffset); // CHLClient::Shutdown points to _view https://i.imgur.com/3Ad96gY.png
    ClientMode = GetVMT<ClientModeShared>((uintptr_t)CHLclient, 10, ClientModeOffset); // HudProcessInput points to g_pClientMode, and we retrieve it.  https://i.imgur.com/h0qYd5q.png I got the information from the .dylib -> https://i.imgur.com/kBaS7Vq.png
    GlobalVars = GetVMT<CGlobalVarsBase>((uintptr_t)CHLclient, 0, GlobalVarsOffset); // CHLClient::Init points to gpGlobals https://i.imgur.com/aIwpS45.png
    Input = GetVMT<CInput>((uintptr_t)CHLclient, 20, InputOffset); // CHLClient::CreateMove points to input https://i.imgur.com/TnEcetn.png
    UniformRandomStream = GetVMT<CUniformRandomStream>((uintptr_t)GetProcAddress(GetModuleHandleA("vstdlib.dll"), "RandomSeed"), RandomSeedOffset); // RandomSeed points to s_pUniformStream https://i.imgur.com/bddk0QK.png
    
    localPlayer = (C_BasePlayer*)ClientEntityList->GetClientEntity(EngineClient->GetLocalPlayer());

    // Debug: a null critical interface or a null scan-derived pointer is the
    // usual root cause of an early crash after a game update. Log them once so
    // the reason is in debug.log instead of guessed at.
    {
        const struct { const char* name; const void* ptr; } criticals[] = {
            { "EngineClient", EngineClient }, { "ClientEntityList", ClientEntityList },
            { "CHLclient", CHLclient }, { "MaterialSystem", MaterialSystem },
            { "CVar", CVar }, { "ModelRender", ModelRender }, { "RenderView", RenderView },
            { "EngineTrace", EngineTrace }, { "GameEventManager", GameEventManager },
            { "MatSystemSurface", MatSystemSurface }, { "ModelInfo", ModelInfo },
            { "LuaShared", LuaShared }, { "Prediction", Prediction }, { "GameMovement", GameMovement },
            { "bSendpacket", Globals::bSendpacket }, { "predictionRandomSeed", Globals::predictionRandomSeed },
            { "hostName", Globals::hostName },
        };
        for (const auto& c : criticals)
            if (!c.ptr) DBG_WARN("%s resolved to null", c.name);
        DBG_INFO("Init complete: localPlayer=%p, slot=%d", (void*)localPlayer, EngineClient->GetLocalPlayer());

        // These come from hard-coded byte offsets into game functions
        // (ViewRenderOffset, GlobalVarsOffset, ...) rather than named
        // interfaces, so they are what rots first after a game update. A stale
        // offset produces a non-null but *garbage* pointer that crashes the
        // moment it is used, so log the resolved value AND whether it points at
        // readable memory -- that single line in debug.log tells you exactly
        // which offset to re-reverse.
        const struct { const char* name; void* ptr; } derived[] = {
            { "ViewRender", ViewRender }, { "ClientMode", ClientMode },
            { "GlobalVars", GlobalVars }, { "Input", Input },
            { "ClientState", ClientState }, { "UniformRandomStream", UniformRandomStream },
        };
        for (const auto& d : derived)
        {
            if (!d.ptr)
                DBG_WARN("%s resolved to null (stale offset?)", d.name);
            else if (!MemIsReadable(d.ptr))
                DBG_ERROR("%s resolved to unreadable %p -- stale offset, re-reverse it", d.name, d.ptr);
            else
                DBG_INFO("%s = %p", d.name, d.ptr);
        }
    }

    if(Lua = LuaShared->GetLuaInterface((unsigned char)LuaInterfaceType::LUA_CLIENT))
        oRunStringEx = GuardedVMTHook< _RunStringEx>("RunStringEx", (PVOID**)Lua, (PVOID)hkRunStringEx, 111);
    oCreateLuaInterfaceFn = GuardedVMTHook<_CreateLuaInterfaceFn>("CreateLuaInterface", (PVOID**)LuaShared, (PVOID)hkCreateLuaInterfaceFn, 4);
    oCloseLuaInterfaceFn = GuardedVMTHook<_CloseLuaInterfaceFn>("CloseLuaInterface", (PVOID**)LuaShared, (PVOID)hkCloseInterfaceLuaFn, 5);

    oCreateMove = GuardedVMTHook<_CreateMove>("CreateMove", (PVOID**)ClientMode, (PVOID)hkCreateMove, 21);
    oFrameStageNotify = GuardedVMTHook< _FrameStageNotify>("FrameStageNotify", (PVOID**)CHLclient, hkFrameStageNotify, 35);
    oRenderView = GuardedVMTHook<_RenderView>("RenderView", (PVOID**)ViewRender, (PVOID)hkRenderView, 6);
    oPaintTraverse = GuardedVMTHook< _PaintTraverse>("PaintTraverse", (PVOID**)PanelWrapper, (PVOID)hkPaintTraverse, 41);
    oDrawModelExecute = GuardedVMTHook< _DrawModelExecute>("DrawModelExecute", (PVOID**)ModelRender, (PVOID)hkDrawModelExecute, 20);
    oProcessGMOD_ServerToClient = GuardedVMTHook< _ProcessGMOD_ServerToClient>("ProcessGMOD_ServerToClient", (PVOID**)ClientState, (PVOID)hkProcessGMOD_ServerToClient, 64);
    oRunCommand = GuardedVMTHook< _RunCommand>("RunCommand", (PVOID**)Prediction, (PVOID)hkRunCommand, 19);
    oPaint = GuardedVMTHook<_Paint>("Paint", (PVOID**)EngineVGui, (PVOID)hkPaint, 13);

    // Owns the listeners, registers them once, and reports a failure instead of
    // assuming AddListener() worked.  They used to be raw `new` results parked
    // in two void* globals, with nothing to unregister them on unload.
    if (!GameEvents::Register())
        ConPrint("Game event listeners are not fully registered", Color(255, 200, 0));

    GUI::categories.emplace_back(GUI::GUICategory{ &GUI::DrawAimbot, "A", true, true, true });
    GUI::categories.emplace_back(GUI::GUICategory{ &GUI::DrawVisuals, "C", false, true, true });
    GUI::categories.emplace_back(GUI::GUICategory{ &GUI::DrawMisc, "D", false, true, true });
    GUI::categories.emplace_back(GUI::GUICategory{ &GUI::DrawFilters, "F", false, true, true });
    GUI::categories.emplace_back(GUI::GUICategory{ &GUI::DrawLua, "LUA", false, false, true });

    // This can be easily detected(for instance, perphead will ban you for it), so disabled unless you need it and researched enough the server you're on.
    if (false)
    {
        ConVar* cvar = CVar->FindVar("mat_fullbright");
        cvar->RemoveFlags(FCVAR_CHEAT);

        //This'll let you change your name ingame freely
        cvar = CVar->FindVar("name");
        cvar->RemoveFlags(FCVAR_SERVER_CAN_EXECUTE);
        cvar->DisableCallback();
    }
    

    //GlobalVars->maxClients
    //GlobalVars + 0x14 = 1 will let u do anything lua related
    // Present: hook the shared D3D9 device vtable at index 17. Registered through
    // GuardedVMTHook like the others, so RestoreVMTHooks() puts it back on
    // unload. presentDeviceVTable is a global (see globals.hpp) precisely so its
    // address is still valid at that point.
    presentDeviceVTable = GetD3D9DeviceVTable();
    if (presentDeviceVTable)
        oPresent = GuardedVMTHook<_Present>("Present", (PVOID**)&presentDeviceVTable, (PVOID)hkPresent, 17);
    else
        DBG_ERROR("Hook 'Present' skipped: could not obtain the D3D9 device vtable");

    //EngineClient->ClientCmd_Unrestricted("play \"items/suitchargeok1.wav\"");
        //Sleep(2200);
    Sleep(1000);
    MatSystemSurface->PlaySound("HL1/fvox/bell.wav");
    Sleep(1100);
    MatSystemSurface->PlaySound("HL1/fvox/activated.wav");
    Globals::openMenu = true;
}

BOOL APIENTRY DllMain(HMODULE hModule, uintptr_t ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        std::thread(Main).detach();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        // The engine holds the raw listener pointers; if the module goes away
        // without taking them back, the next game event calls into unmapped
        // memory.  This runs under the loader lock, so it does nothing but
        // remove the pointers and free the objects -- no threads, no I/O.
        GameEvents::Unregister();
    }

    return TRUE;
}


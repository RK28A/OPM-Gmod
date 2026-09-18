#pragma once

#include <Windows.h>
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_dx9.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui-notify/imgui_notify.h"
#include "../globals.hpp"
#include "../hacks/menu/GUI.h"
#include "../hacks/menu/drawing.h"
#include "../hacks/ESP.h"
#include "../hacks/AdminEsp.h"
#include "../hacks/menu/MenuControls.h"
#include "../hacks/menu/MenuBackground.h"
#include "../hacks/menu/Fonts.h"

#ifndef GWL_WNDPROC
#define GWL_WNDPROC GWLP_WNDPROC
#endif
IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	static bool lastState = false;
	if (wParam == InputSystem->ButtonCodeToVirtualKey(Settings::menuKey))
	{
		if (uMsg == WM_KEYDOWN && lastState == false) {
			lastState = true;
			Globals::openMenu = !Globals::openMenu;
		}
		else if (uMsg == WM_KEYUP)
		{
			lastState = false;
		}
	}

	if (Globals::openMenu) {
		ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
		return true;
	}

	return CallWindowProcA(Globals::oWndProc, hWnd, uMsg, wParam, lParam);
}

ImFont* menuFont;
ImFont* boldMenuFont;
ImFont* tabFont;
ImFont* massiveFont;
#ifdef _DEBUG
ImFont* executorFont;
#endif

ImGuiStyle* style;
IDirect3DTexture9* menuBg = nullptr;

// --- Device reset ----------------------------------------------------------
//
// Everything this module creates on the device lives in D3DPOOL_DEFAULT: the
// D3DX line and fonts in drawing.h, ImGui's own vertex/index buffers and font
// texture, and the menu background texture below.  A device reset -- alt-tab,
// a resolution or mode change, the game losing focus in exclusive fullscreen --
// invalidates all of it, and using any of it afterwards is undefined.  Nothing
// handled that, which is why REVIEW.md's manual pass listed "DirectX 9 device
// reset, alt-tab, resolution change" as the thing to try before merging: it was
// not implemented, so there was nothing to try.
//
// IDirect3DDevice9::Reset is vtable index 16 (Present is 17).
typedef HRESULT(__stdcall* _Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
_Reset oReset = nullptr;

// Recreated after every reset because the texture is D3DPOOL_DEFAULT.
static void CreateMenuBackground(IDirect3DDevice9* device, D3DFORMAT backBufferFormat)
{
	if (!device || menuBg)
		return;

	if (FAILED(D3DXCreateTextureFromFileInMemoryEx(device, menuBackground, sizeof(menuBackground),
		4096, 4096, D3DX_DEFAULT, 0, backBufferFormat, D3DPOOL_DEFAULT,
		D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &menuBg)))
		menuBg = nullptr;
}

static void ReleaseMenuBackground()
{
	if (menuBg)
	{
		menuBg->Release();
		menuBg = nullptr;
	}
}

HRESULT __stdcall hkReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	if (!oReset)
		return D3DERR_INVALIDCALL;

	// Release every DEFAULT-pool resource before the reset, or the reset fails
	// with D3DERR_DEVICELOST and the game never recovers.
	OnLostDevice();
	ImGui_ImplDX9_InvalidateDeviceObjects();
	ReleaseMenuBackground();

	const HRESULT result = oReset(device, pPresentationParameters);

	// A failed reset leaves the device lost; the game will call Reset again, so
	// recreate nothing and stay in the invalidated state until one succeeds.
	if (SUCCEEDED(result))
	{
		ImGui_ImplDX9_CreateDeviceObjects();
		OnResetDevice();

		CreateMenuBackground(device, pPresentationParameters
			? pPresentationParameters->BackBufferFormat
			: D3DFMT_UNKNOWN);

		// The overlay maths is all in screen space, so the cached size has to
		// follow the new back buffer.
		if (EngineClient)
			EngineClient->GetScreenSize(Globals::screenWidth, Globals::screenHeight);
	}

	return result;
}

// Runs at the end of a frame, never from inside the menu that requested it.
//
// Order matters: stop the engine calling into this module first, then hand the
// input back, and only then release the device objects -- by that point ImGui's
// draw list for the frame has already been rendered, so nothing still points at
// menuBg or at the D3DX objects.
static void PerformUnload()
{
	if (Globals::window && Globals::oWndProc)
		SetWindowLongPtrA(Globals::window, GWLP_WNDPROC, (LONG_PTR)Globals::oWndProc);

	// Unregisters and destroys both listeners.  The C casts were only needed
	// because the listeners inherited privately; they are
	// `public IGameEventListener2` now.
	GameEvents::Unregister();

	ConVarSpoofing::RestoreAll();

	// Takes back every vtable hook, Present (D3D9 device vtable index 17) and
	// the device Reset hook included.
	RestoreVMTHooks();

	if (Globals::bSendpacket)
	{
		*Globals::bSendpacket = true;

		if (Globals::bSendpacketProtection)
		{
			DWORD ignored = 0;
			VirtualProtect(Globals::bSendpacket, sizeof(bool), Globals::bSendpacketProtection, &ignored);
			Globals::bSendpacketProtection = 0;
		}
	}

	if (InputSystem)
		InputSystem->EnableInput(true);

	if (PanelWrapper && Globals::lastPanelIdentifier)
	{
		PanelWrapper->SetKeyBoardInputEnabled(Globals::lastPanelIdentifier, false);
		PanelWrapper->SetMouseInputEnabled(Globals::lastPanelIdentifier, false);
	}

	// Nothing will draw again: the hooks are gone and this is past
	// RenderDrawData for the current frame.
	ReleaseMenuBackground();
	ShutdownRenderer();

	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	Globals::openMenu = false;

#if _DEBUG
	FreeConsole();
#endif

	ConPrint("Successfully unloaded!", Color(0, 255, 0));

	// The module stays mapped.  Freeing it would mean calling FreeLibrary from
	// a thread that is not executing module code, and the only thread available
	// here is the one currently inside this hook -- it has to return through
	// this function's own code first.  The usual workaround is a detached
	// thread that sleeps and then calls FreeLibraryAndExitThread, which is a
	// race dressed up as a fix.  Every hook is removed and every device object
	// released, so the module is inert; re-injecting needs a fresh game
	// process.
}

HRESULT __stdcall hkPresent(IDirect3DDevice9* pDevice, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
	static bool initialized = false;
	if (!initialized)
	{
		EngineClient->GetScreenSize(Globals::screenWidth, Globals::screenHeight);
		InitRenderer(pDevice);

		ImGui::CreateContext();

		Globals::window = FindWindowA("Valve001", nullptr);
		Globals::oWndProc = (WNDPROC)SetWindowLongPtrA(Globals::window, GWL_WNDPROC, (LONG_PTR)WndProc);

		IDirect3DSwapChain9* pChain = nullptr;
		D3DPRESENT_PARAMETERS pp = {};
		D3DDEVICE_CREATION_PARAMETERS param = {};
		pDevice->GetCreationParameters(&param);
		pDevice->GetSwapChain(0, &pChain);
		if (pChain)
		{
			pChain->GetPresentParameters(&pp);
			pChain->Release(); // GetSwapChain AddRefs; this reference was leaked
		}

		ImGui_ImplWin32_Init(Globals::window);
		ImGui_ImplDX9_Init(pDevice);

		CreateMenuBackground(pDevice, pp.BackBufferFormat);

		// Installed here rather than in Main(): the device only exists once the
		// game has reached its first Present.  RestoreVMTHooks() takes it back
		// on unload like every other vtable hook.
		oReset = VMTHook<_Reset>((PVOID**)pDevice, (PVOID)hkReset, 16);

		style = &ImGui::GetStyle();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;

		// Every font below is a `const unsigned char[]` with static storage
		// duration compiled into the module.  ImFontConfig defaults
		// FontDataOwnedByAtlas to true, which makes ImFontAtlas::ClearInputData()
		// call IM_FREE() on those arrays -- a free() of a pointer that never
		// came from the allocator, on every atlas rebuild (a DX9 device reset,
		// among others).  The bytes outlive the atlas, so the atlas must not own
		// them.
		ImFontConfig staticFontCfg;
		staticFontCfg.FontDataOwnedByAtlas = false;

		menuFont = io.Fonts->AddFontFromMemoryTTF((void*)verdanaBytes, sizeof(verdanaBytes), 11.f, &staticFontCfg);

		// Merged into menuFont, which is Fonts[0] and therefore ImGui's default
		// font -- the one the notifications draw with.
		//
		// The PR added a second, unnamed copy of Verdana just to have something
		// to merge into, and merged the icons into that instead of menuFont.
		// Runs exactly once, inside this `if (!initialized)` block: merging
		// twice would add the glyph range to the atlas twice.
		if (!ImGui::MergeIconsWithLatestFont(11.f, false))
			ConPrint("Failed to merge the icon font; notifications will render without icons", Color(255, 200, 0));

		boldMenuFont = io.Fonts->AddFontFromMemoryTTF((void*)verdanaBoldBytes, sizeof(verdanaBoldBytes), 11.f, &staticFontCfg);
		massiveFont = io.Fonts->AddFontFromMemoryTTF((void*)verdanaBoldBytes, sizeof(verdanaBoldBytes), 34.f, &staticFontCfg);
		tabFont = io.Fonts->AddFontFromMemoryTTF((void*)rawTabBytes, sizeof(rawTabBytes), 42.f, &staticFontCfg);
#ifdef _DEBUG
		executorFont = io.Fonts->AddFontFromMemoryTTF((void*)verdanaBytes, sizeof(verdanaBytes), 14.f, &staticFontCfg);
#endif

		initialized = true;
	}

	// https://www.unknowncheats.me/forum/3191157-post4.html Thanks to copypaste for this :)
	ITexture* rt = nullptr;
	auto context = MaterialSystem->GetRenderContext();
	//IMatRenderContext* context = NULL;
	if (context)
	{
		context->BeginRender();
		rt = context->GetRenderTarget();
		context->SetRenderTarget(nullptr);
		context->EndRender();
	}

	// https://www.unknowncheatsme/forum/3137288-post2.html Thanks to him :)
	// If you don't do that, the color of the menu will match to VGUI's.
	DWORD colorwrite, srgbwrite;
	pDevice->GetRenderState(D3DRS_COLORWRITEENABLE, &colorwrite);
	pDevice->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgbwrite);

	pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xffffffff);
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, false);

	IDirect3DVertexDeclaration9* vertexDeclaration;
	IDirect3DVertexShader9* vertexShader;
	pDevice->GetVertexDeclaration(&vertexDeclaration);
	pDevice->GetVertexShader(&vertexShader);

	ImGui::GetIO().MouseDrawCursor = Globals::openMenu;

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Notifications draw on their own windows, outside the menu, so they are
	// rendered whether or not the menu is open.  RenderNotifications() returns
	// early without a valid context, and the push/pop pair stays balanced.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f));
	ImGui::RenderNotifications();
	ImGui::PopStyleColor(1);
	ImGui::PopStyleVar(1);

	if (EngineClient->IsInGame())
	{
		rainbowColor(Settings::Aimbot::fovColor, Settings::Misc::rainbowSpeed);
		rainbowColor(Settings::Misc::crossHairColor, Settings::Misc::rainbowSpeed);

		if (Settings::Aimbot::drawAimbotFov)
		{
			DrawCircle(Vector(Globals::screenWidth / 2, Globals::screenHeight / 2, 0), Settings::Aimbot::aimbotFOV, kCircleMaxSegments, ColorToRGBA(Settings::Aimbot::fovColor));
		}
		if (Settings::Misc::drawCrosshair) {
			DrawLine(Vector(Globals::screenWidth / 2 - Settings::Misc::crosshairSize, Globals::screenHeight / 2, 0), Vector(Globals::screenWidth / 2 + Settings::Misc::crosshairSize, Globals::screenHeight / 2, 0), ColorToRGBA(Settings::Misc::crossHairColor));
			DrawLine(Vector(Globals::screenWidth / 2, Globals::screenHeight / 2 - Settings::Misc::crosshairSize, 0), Vector(Globals::screenWidth / 2, Globals::screenHeight / 2 + Settings::Misc::crosshairSize, 0), ColorToRGBA(Settings::Misc::crossHairColor));
		}
		if ((Settings::lastHitmarkerTime + 0.08f) > EngineClient->Time() && Settings::Misc::hitmarker)
		{
			DrawLine(Vector(Globals::screenWidth / 2 - 2, Globals::screenHeight / 2 - 2, 0), Vector(Globals::screenWidth / 2 - Settings::Misc::hitmarkerSize, Globals::screenHeight / 2 - Settings::Misc::hitmarkerSize, 0), 0xFFFFFFFF);
			DrawLine(Vector(Globals::screenWidth / 2 + 2, Globals::screenHeight / 2 - 2, 0), Vector(Globals::screenWidth / 2 + Settings::Misc::hitmarkerSize, Globals::screenHeight / 2 - Settings::Misc::hitmarkerSize, 0), 0xFFFFFFFF);

			DrawLine(Vector(Globals::screenWidth / 2 - 2, Globals::screenHeight / 2 + 2, 0), Vector(Globals::screenWidth / 2 - Settings::Misc::hitmarkerSize, Globals::screenHeight / 2 + Settings::Misc::hitmarkerSize, 0), 0xFFFFFFFF);
			DrawLine(Vector(Globals::screenWidth / 2 + 2, Globals::screenHeight / 2 + 2, 0), Vector(Globals::screenWidth / 2 + Settings::Misc::hitmarkerSize, Globals::screenHeight / 2 + Settings::Misc::hitmarkerSize, 0), 0xFFFFFFFF);
		}
		
		if (false)
		{
			float tickrate = 100;
			float strafes = 10;

			Vector screenPos;
			Vector screenPosLeftBar;
			Vector screenPosRightBar;

			Vector middleBar = localPlayer->GetAbsOrigin();
			Vector leftBar = localPlayer->GetAbsOrigin();
			Vector rightBar = localPlayer->GetAbsOrigin();

			auto currVel = localPlayer->getVelocity();
			currVel.z = 0;
			float maxVel = sqrt(pow(30, 2) + pow(currVel.Length(), 2)); // max possible new velocity in this tick given a perfect strafe angle
			float A = atan(30 / currVel.Length()) * (180 / PI); // difference of angle to the next tick's optimal strafe angle
			float D = (0.75 * tickrate * A) / strafes;// optimal number of degrees per strafe given the desired number of strafes per jump, the tickrate of the server, and the current player velocity defined in v_1

			auto eyeAng = localPlayer->EyeAngles();
			eyeAng.y += D;
			rightBar += (Vector(std::cos(degreesToRadians(0)) * std::cos(degreesToRadians(eyeAng.y)), std::cos(degreesToRadians(0)) * std::sin(degreesToRadians(eyeAng.y)), -std::sin(degreesToRadians(0))) * 100);
			eyeAng.y -= (D*2);
			leftBar += (Vector(std::cos(degreesToRadians(0)) * std::cos(degreesToRadians(eyeAng.y)), std::cos(degreesToRadians(0)) * std::sin(degreesToRadians(eyeAng.y)), -std::sin(degreesToRadians(0))) * 100);

			if (WorldToScreen(middleBar, screenPos) && WorldToScreen(leftBar, screenPosLeftBar) && WorldToScreen(rightBar, screenPosRightBar))
			{
				DrawLine(screenPos, screenPosLeftBar, 0xFFFFFFFF);
				DrawLine(screenPos, screenPosRightBar, 0xFFFFFFFF);
			}
		}
	}

	doEsp();
	doAdminEsp(); // separate admin overlay; no-op unless Settings::ESP::adminEsp
#ifdef _DEBUG
		if (EngineClient->IsInGame())
		{

			auto cmd = Globals::lastEndCmd;
			if (cmd.command_number != 0 || true)
			{
				std::wstring userCmdDebug = L"UserCMD Data\nCommandNumber: " + std::to_wstring(cmd.command_number)
					+ L"\nMove: " + std::to_wstring(cmd.forwardmove) + L", " + std::to_wstring(cmd.sidemove) + L", " + std::to_wstring(cmd.upmove)
					+ L"\ntick_count: " + std::to_wstring(cmd.tick_count)
					+ L"\nviewangles: " + std::to_wstring(cmd.viewangles.x) + L", " + std::to_wstring(cmd.viewangles.y) + L", " + std::to_wstring(cmd.viewangles.z);
				DebugDrawString(Vector(10, 50, 0), userCmdDebug, ColorToRGBA(Color(255, 255, 255)), true);
			}
		}
#endif // DEBUG
	ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImColor(9, 8, 9,255);

	ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(187.f, 34.f));
	ImGui::BeginMenuBackground("Credits window", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar /*| ImGuiWindowFlags_NoMove*/, 0.3f);
	{
		ImGui::ColorBar("rainbowBar2", ImVec2(55.f, 2.f));
		style->ItemSpacing = ImVec2(4, 2);
		style->WindowPadding = ImVec2(4, 4);
		ImGui::NewLine();
		ImGui::SameLine(15.f);
		ImGui::Text(std::string("Coded by t.me/Gaztoof - v" + std::string(CheatVersion)).c_str());
	}
	ImGui::End();	

	if (Globals::openMenu)
	{
		rainbowColor(Settings::menuColor, Settings::Misc::rainbowSpeed);

		ImGui::GetStyle().Colors[ImGuiCol_MenuTheme].x = Settings::menuColor.fCol[0];
		ImGui::GetStyle().Colors[ImGuiCol_MenuTheme].y = Settings::menuColor.fCol[1];
		ImGui::GetStyle().Colors[ImGuiCol_MenuTheme].z = Settings::menuColor.fCol[2];

		static int tab = 0;
		style->ScrollbarSize = 5.f;

		ImGui::SetNextWindowSize(ImVec2(660.f, 560.f));
		ImGui::BeginMenuBackground("Main Windows", &Globals::openMenu, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar); 
		{
			ImGui::SameLine(6.f);
			ImGui::BeginChild("Complete Border", ImVec2(648.f, 550.f), false); {

				ImGui::Image(menuBg, ImVec2(648.f, 550.f));

			} ImGui::EndChild();	

			ImGui::SameLine(6.f);

			ImGui::PushFont(menuFont);

			ImGui::BeginChild("Menu Contents", ImVec2(648.f, 548.f), false); {

				ImGui::ColorBar("rainbowBar1", ImVec2(648.f, 3.f));

				style->ItemSpacing = ImVec2(0.f, -1.f);

				ImGui::BeginTabs("Tabs", ImVec2(75.f, 542.f), false); {

					style->ItemSpacing = ImVec2(0.f, 0.f);

					style->ButtonTextAlign = ImVec2(0.5f, 0.47f);

					ImGui::PopFont();
					ImGui::PushFont(tabFont);

					ImGui::TabSpacer("##Top Spacer", ImVec2(75.f, 10.f));
					for (int i = 0; i < GUI::categories.size(); i++)
					{
						if (!GUI::categories[i].m_bIsVisible)
						{
							continue;
						}
						if (!GUI::categories[i].m_bHasIcon)
						{
							ImGui::PushFont(massiveFont);
						}
						if (tab == i) {
							if (ImGui::SelectedTab(GUI::categories[i].m_szCategoryName, ImVec2(75.f, 75.f)))
							{
								tab = i;
							}
						}
						else if (ImGui::Tab(GUI::categories[i].m_szCategoryName, ImVec2(75.f, 75.f)))
						{
							tab = i;
						}
						
						if (!GUI::categories[i].m_bHasIcon)
						{
							ImGui::PopFont();
						}
					}
					for (int i = 0; i < 7 - GUI::categories.size(); i++)
					{
						ImGui::Tab(" ", ImVec2(75.f, 75.f));
					}
					
					ImGui::TabSpacer2("##Bottom Spacer", ImVec2(75.f, 7.f));
					ImGui::PopFont();
					style->ButtonTextAlign = ImVec2(0.5f, 0.5f);

				} ImGui::EndTabs();

				ImGui::SameLine(75.f);

				ImGui::PushFont(menuFont);
				ImGui::BeginChild("Tab Contents", ImVec2(572.f, 542.f), false); 
				{
					if (tab >= GUI::categories.size())
					{
						tab = 0;
					}
					GUI::categories.at(tab).m_pCategoryHandler();

					style->Colors[ImGuiCol_Border] = ImColor(10, 10, 10, 255);

				} ImGui::EndChild();

				style->ItemSpacing = ImVec2(4.f, 4.f);
				style->Colors[ImGuiCol_ChildBg] = ImColor(17, 17, 17, 255);

			} ImGui::EndChild();
			ImGui::PopFont();
		}
		ImGui::End();
	}
	localPlayer = (C_BasePlayer*)ClientEntityList->GetClientEntity(EngineClient->GetLocalPlayer());
	SpectatorList();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());


	pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, colorwrite);
	pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, srgbwrite);
	pDevice->SetVertexDeclaration(vertexDeclaration);
	pDevice->SetVertexShader(vertexShader);
	if (rt)
	{
		if ((context = MaterialSystem->GetRenderContext()) != nullptr)
		{
			context->BeginRender();
			context->SetRenderTarget(rt);
			context->EndRender();
		}
	}

	// Deferred from the menu's Unload button: the frame is fully rendered, so
	// releasing the device objects here cannot pull the rug out from under a
	// draw list that is still pending.  oPresent is captured first because
	// PerformUnload restores the pointer it is read from.
	const _Present presentToCall = oPresent;
	if (Globals::pendingUnload.exchange(false))
		PerformUnload();

	return presentToCall(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

#pragma once

#include "../globals.hpp"
#include "../client/usercmd.h"
#include "../hacks/Misc.h"
#include "../hacks/Utils.h"

typedef bool(__thiscall* _RenderView)(CViewRender*, CViewSetup&, int, int);
_RenderView oRenderView;

bool __fastcall hkRenderView(CViewRender* ViewRender, 
#ifndef _WIN64
	void*, // __fastcall does literally nothing in x64, so that's why we make it inactive
#endif
CViewSetup& view, int nClearFlags, int whatToDraw)
{
	bool zoomKeyDown = false;
	getKeyState(Settings::Misc::zoomKey, Settings::Misc::zoomKeyStyle, &zoomKeyDown, henlo69, henlo70, henlo71);

	// Braced, but the behaviour is unchanged: the else belongs to the inner if,
	// so the world FOV is only touched while the option is on.
	if (Settings::Visuals::fovEnabled)
	{
		if (zoomKeyDown && Settings::Misc::zoom)
		{
			view.fov = Settings::Visuals::ClampFov(Settings::Misc::zoomFOV);
		}
		else
		{
			view.fov = Settings::Visuals::ClampFov(Settings::Visuals::fov);
		}
	}

	//view.angles = Globals::lastCmd.viewangles;

	// Capture the engine's own view model FOV once, on the first frame that
	// reports a usable one.
	//
	// Upstream used `viewModelFOV == -1.f` as both the "not captured yet"
	// sentinel and the user's setting.  Once the PR gave the setting a default
	// of 90.f the sentinel could never match, so the native value was never
	// recorded and there was nothing to restore.  The two roles are now two
	// variables.  (The capture happens once per injection: a mid-session change
	// to the engine's own viewmodel_fov cvar is not picked up.)
	if (!Settings::Visuals::hasOriginalViewModelFov && view.fovViewmodel > 0.f)
	{
		Settings::Visuals::originalViewModelFov = view.fovViewmodel;
		Settings::Visuals::hasOriginalViewModelFov = true;
	}

	if (Settings::Visuals::viewModelFovEnabled)
	{
		// Clamped here too: the slider bounds the value, a hand-edited config
		// does not.
		view.fovViewmodel = Settings::Visuals::ClampFov(Settings::Visuals::viewModelFov);
	}
	else if (Settings::Visuals::hasOriginalViewModelFov)
	{
		// The engine does not necessarily rewrite fovViewmodel every frame, so
		// switching the option off has to put the captured value back.
		view.fovViewmodel = Settings::Visuals::originalViewModelFov;
	}

	
	static Vector camPos = Vector(0,0,0);

	bool thirdpKeyDown = false;
	getKeyState(Settings::Misc::thirdpersonKey, Settings::Misc::thirdpersonKeyStyle, &thirdpKeyDown, henlo1, henlo2, henlo3);
	static bool lastThirdPersonState = false;
	if (localPlayer && Settings::Misc::thirdperson && thirdpKeyDown) {
		lastThirdPersonState = true;
		ThirdPerson(view);
		view.angles = Globals::lastCmd.viewangles;

		Input->m_fCameraInThirdPerson = true;		
	}
	else {
		if (lastThirdPersonState)
			Input->m_fCameraInThirdPerson = false;
		lastThirdPersonState = false;
		
	}

	bool freeCamKeyDown = false;
	getKeyState(Settings::Misc::freeCamKey, Settings::Misc::freeCamKeyStyle, &freeCamKeyDown, henlo4, henlo5, henlo6);

	static bool lastFreeCamState = false;
	if (localPlayer && Settings::Misc::freeCam && freeCamKeyDown)
	{
		view.angles = Globals::lastCmd.viewangles;
		lastFreeCamState = true;
		Settings::currentlyInFreeCam = true;
		FreeCam(view, camPos);
		Input->m_fCameraInThirdPerson = true;
	}
	else {
		if (lastFreeCamState)
		{
			EngineClient->SetViewAngles(Globals::lastRealCmd.viewangles);
			Input->m_fCameraInThirdPerson = false;
		}
		lastFreeCamState = false;
		camPos = Vector(0, 0, 0);
		Settings::currentlyInFreeCam = false;
	}

	if ((!freeCamKeyDown || !Settings::Misc::freeCam) && (!thirdpKeyDown || !Settings::Misc::thirdperson))
	{
		//view.angles = Globals::lastCmd.viewangles;
	}
	
	return oRenderView(ViewRender, view, nClearFlags, whatToDraw);
}
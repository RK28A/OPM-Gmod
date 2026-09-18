#pragma once

#include "../globals.hpp"
#include "../client/usercmd.h"

// PollKey lives here.  This header used to rely on dllmain.cpp including
// CreateMove.h (and through it Utils.h) first -- the single-translation-unit
// build hides a missing include until the day the order changes.
#include "../hacks/Utils.h"
#include <Windows.h>

typedef void(__thiscall* _FrameStageNotify)(CHLClient*, ClientFrameStage_t);
_FrameStageNotify oFrameStageNotify;

void __fastcall hkFrameStageNotify(CHLClient* client, 
#ifndef _WIN64
	void*, // __fastcall does literally nothing in x64, so that's why we make it inactive
#endif
ClientFrameStage_t stage)
{

	localPlayer = (C_BasePlayer*)ClientEntityList->GetClientEntity(EngineClient->GetLocalPlayer());

	static ConVar* fullbrightCvar = CVar->FindVar("mat_fullbright");
	// static_cast around the bool: intValue is int32_t, and comparing them
	// directly triggers C4805 (unsafe mix of int and bool).  The value 0/1 is
	// exactly what SetValue below will store anyway.
	if (fullbrightCvar && fullbrightCvar->intValue != static_cast<int>(Settings::Visuals::fullBright)) {
		if (Settings::Visuals::fullBright)
			fullbrightCvar->RemoveFlags(FCVAR_CHEAT);
		else fullbrightCvar->AddFlags(FCVAR_CHEAT);
		fullbrightCvar->SetValue(Settings::Visuals::fullBright);
	}
	// Acquire() is idempotent and owns the instance, so this no longer leaks a
	// raw `new` into a global that nothing frees.  It also copes with FindVar()
	// returning null, which used to build a SpoofedConVar around nullptr and
	// then dereference it on the very next line.
	if (Settings::Misc::svAllowCsLua)
		ConVarSpoofing::Acquire(ConVarSpoofing::allowCsLua, "sv_allowcslua");

	if (Settings::Misc::svCheats)
		ConVarSpoofing::Acquire(ConVarSpoofing::cheats, "sv_cheats");

	// static_cast<int>: intValue is int32_t and the settings are bool -- see
	// the note above the fullbright check.
	if (ConVarSpoofing::allowCsLua && ConVarSpoofing::allowCsLua->m_pOriginalCVar->intValue != static_cast<int>(Settings::Misc::svAllowCsLua))
		ConVarSpoofing::allowCsLua->m_pOriginalCVar->SetValue(Settings::Misc::svAllowCsLua);

	if (ConVarSpoofing::cheats && ConVarSpoofing::cheats->m_pOriginalCVar->intValue != static_cast<int>(Settings::Misc::svCheats))
		ConVarSpoofing::cheats->m_pOriginalCVar->SetValue(Settings::Misc::svCheats);

	//Input->cameraoffset

	if(Settings::Visuals::noVisualRecoil && localPlayer && localPlayer->IsAlive())
	{
		//https://i.imgur.com/Y5hSyqS.png
		if (Settings::Visuals::noVisualRecoil)
			localPlayer->GetViewPunch() = QAngle(0, 0, 0);
	}

	// i guess this enum's wrong as norecoil in renderstart didnt work at all
	if (false && stage == ClientFrameStage_t::FRAME_RENDER_START)
	{

		// do thirdperson
	}
	static bool appliedNightMode = false;
	static bool waitingForLoadingEnd = false;
	static bool lastNightModeState = false;
	static Color lastNightModeColor = Color(255, 255, 255);
	if (lastNightModeState != Settings::Visuals::changeWorldColor || lastNightModeColor != Settings::Visuals::worldColor)
	{
		lastNightModeColor = Settings::Visuals::worldColor;
		lastNightModeState = Settings::Visuals::changeWorldColor;
		appliedNightMode = false;
	}

	if (EngineClient->IsDrawingLoadingImage())
	{
		waitingForLoadingEnd = true;
	}
	else if (waitingForLoadingEnd) {
		appliedNightMode = false;
		waitingForLoadingEnd = false;
	}

	if (!waitingForLoadingEnd && stage == ClientFrameStage_t::FRAME_RENDER_END)
	{
		for (MaterialHandle_t i = MaterialSystem->FirstMaterial(); i != MaterialSystem->InvalidMaterial(); i = MaterialSystem->NextMaterial(i))
		{
			auto material = MaterialSystem->GetMaterial(i);
			if (!material || material->IsErrorMaterial() || !material->IsPrecached())
				continue;
			// that's quite a big FPS killer
#pragma region WorldColor
			if (!strcmp(material->GetTextureGroupName(), TEXTURE_GROUP_WORLD))
			{
				if (Settings::Visuals::changeWorldColor && !appliedNightMode)
				{
					material->AlphaModulate(Settings::Visuals::worldColor.fCol[3]);
					material->ColorModulate(Settings::Visuals::worldColor.fCol[0], Settings::Visuals::worldColor.fCol[1], Settings::Visuals::worldColor.fCol[2]);
				}
				else if(!Settings::Visuals::changeWorldColor && !appliedNightMode)
				{
					material->AlphaModulate(1.f);
					material->ColorModulate(1.f, 1.f, 1.f);
				}
			}
#pragma endregion

		}
		appliedNightMode = true;
		static bool lastSkyBox = false;
		if (Settings::Visuals::disableSkyBox != lastSkyBox)
		{
			lastSkyBox = Settings::Visuals::disableSkyBox;
			if (lastSkyBox)
			{
				EngineClient->ClientCmd("r_3dsky 0");
			}
			else {
				EngineClient->ClientCmd("r_3dsky 1");
			}
		}
	}
	// Shares Settings::Misc::thirdpersonKeyState / freeCamKeyState with
	// RenderView.h.  Under the old macro each of the two hooks kept its own
	// toggle latch, so in toggle mode this hook and the camera hook could
	// disagree about whether the feature was on.
	const bool thirdpKeyDown = PollKey(Settings::Misc::thirdpersonKey,
		Settings::Misc::thirdpersonKeyStyle, Settings::Misc::thirdpersonKeyState);
	const bool freecamKeyDown = PollKey(Settings::Misc::freeCamKey,
		Settings::Misc::freeCamKeyStyle, Settings::Misc::freeCamKeyState);

	bool needsSetViewAngles = (Settings::Misc::thirdperson && thirdpKeyDown) || (Settings::Misc::freeCam && freecamKeyDown);

	if(needsSetViewAngles && localPlayer && localPlayer->IsAlive() && stage == ClientFrameStage_t::FRAME_RENDER_START)
		localPlayer->SetLocalViewAngles(Globals::lastNetworkedCmd.viewangles);
	if(oFrameStageNotify)
	oFrameStageNotify(client, stage);
	if (needsSetViewAngles && localPlayer && localPlayer->IsAlive() && stage == ClientFrameStage_t::FRAME_RENDER_START)
		localPlayer->SetLocalViewAngles(Globals::lastRealCmd.viewangles);
	return;
}
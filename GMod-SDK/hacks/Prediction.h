#pragma once

#include "../globals.hpp"
#include "../client/usercmd.h"
#include "Utils.h"
#include "../tier1/checksum_md5.h"
#include "../client/IGameMovement.h"

// Movement prediction.
//
// Gated behind Settings::Misc::edgeJump with the note "Temporarily making
// prediction disabled cause far from being perfect", so in practice this is
// dead unless that option is on.  It is kept rather than deleted because the
// option is in the menu and in every saved config, but it is now guarded like
// code that can actually run.
namespace PredictionState
{
	// These were three file-scope globals carrying the m_ prefix that belongs to
	// members.
	inline float oldCurtime = 0.f;
	inline float oldFrametime = 0.f;
	inline CMoveData moveData;

	// Set by StartPrediction, consumed by EndPrediction.  Without it, a
	// localPlayer that goes null between the two calls -- a disconnect, a death,
	// a map change mid-frame -- meant either restoring curtime/frametime that
	// were never saved, or not restoring the ones that were.
	inline bool active = false;

	// Every interface here comes from a vtable walk or a signature scan in
	// Main(), any of which can fail and leave the pointer null -- the same class
	// of crash the review fixed for bSendpacket, which got its null checks while
	// predictionRandomSeed, sitting two lines away, did not.
	[[nodiscard]] inline bool CanRun()
	{
		return localPlayer != nullptr
			&& GlobalVars != nullptr
			&& Prediction != nullptr
			&& GameMovement != nullptr
			&& MoveHelper != nullptr
			&& ClientEntityList != nullptr
			&& Globals::predictionRandomSeed != nullptr;
	}
}

void StartPrediction(CUserCmd* cmd)
{
	if (!Settings::Misc::edgeJump || !cmd || !PredictionState::CanRun())
		return;

	*Globals::predictionRandomSeed = MD5_PseudoRandom(cmd->command_number) & 0x7FFFFFFF;

	PredictionState::oldCurtime = GlobalVars->curtime;
	PredictionState::oldFrametime = GlobalVars->frametime;

	GlobalVars->curtime = localPlayer->getTickBase() * GlobalVars->interval_per_tick;
	GlobalVars->frametime = GlobalVars->interval_per_tick;

	GameMovement->StartTrackPredictionErrors(localPlayer);
	memset((void*)&PredictionState::moveData, 0, sizeof(PredictionState::moveData));

	PredictionState::active = true;

	// The condition used to read:
	//
	//     if (cmd->weaponselect; auto wp = ...GetClientEntity(cmd->weaponselect))
	//
	// where `cmd->weaponselect;` is an init-statement whose value is discarded --
	// a no-op.  The only real condition was `wp != nullptr`, so a weaponselect of
	// 0 still looked up entity 0 (the worldspawn) and called SelectItem on it.
	if (cmd->weaponselect > 0)
	{
		if (auto* weapon = static_cast<C_BaseCombatWeapon*>(ClientEntityList->GetClientEntity(cmd->weaponselect)))
			localPlayer->SelectItem(weapon->GetName(), cmd->weaponsubtype);
	}

	Prediction->SetupMove(localPlayer, cmd, MoveHelper, &PredictionState::moveData);
	GameMovement->ProcessMovement(localPlayer, &PredictionState::moveData);
	Prediction->FinishMove(localPlayer, cmd, &PredictionState::moveData);
}

void EndPrediction(CUserCmd* cmd)
{
	(void)cmd;

	// Paired with StartPrediction rather than re-testing the same conditions:
	// what matters here is whether the save actually happened, not whether it
	// would happen again now.
	if (!PredictionState::active)
		return;

	PredictionState::active = false;

	if (!GlobalVars || !GameMovement || !localPlayer || !Globals::predictionRandomSeed)
		return;

	GlobalVars->curtime = PredictionState::oldCurtime;
	GlobalVars->frametime = PredictionState::oldFrametime;

	GameMovement->FinishTrackPredictionErrors(localPlayer);

	// predictionRandomSeed is an unsigned int*; -1 was being assigned to it,
	// which is the engine's "not predicting" sentinel spelled as a wrap-around.
	// Written explicitly so it is not read as an accident.
	*Globals::predictionRandomSeed = 0xFFFFFFFFu;
}

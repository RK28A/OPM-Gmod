#pragma once

#include <Windows.h>
#include <random>

#include "../globals.hpp"
#include "../mathlib/AngleMath.h"

namespace AntiAim
{
	// rand() is biased modulo a range and shares global state with anything else
	// in the process that calls srand().  Misc.h already moved to <random> in
	// the review pass; these three call sites were left behind.
	[[nodiscard]] inline int RandomInt(int minInclusive, int maxInclusive)
	{
		if (maxInclusive <= minInclusive)
			return minInclusive;

		static std::mt19937 engine{ std::random_device{}() };
		std::uniform_int_distribution<int> distribution(minInclusive, maxInclusive);
		return distribution(engine);
	}

	// The yaw patterns accumulate.  Their accumulator used to be a function
	// local `static float yaw = cmd->viewangles.y;` -- initialised once, on the
	// very first call, from whatever the view angle happened to be then, and
	// then incremented forever with nothing bringing it back into range.
	//
	// After a few minutes it is in the tens of thousands of degrees, where a
	// float's ULP is already ~0.008; past 1e7 it is ~1.0 and the angle quantises
	// visibly.  The behaviour of the feature therefore depended on how long the
	// module had been injected.
	//
	// NormalizeYaw() is the fix, and it is the same unit-tested helper the aim
	// path uses.
	inline float yawAccumulator = 0.f;

	inline void AdvanceYaw(float degrees)
	{
		yawAccumulator = AngleMath::NormalizeYaw(yawAccumulator + degrees);
	}

	[[nodiscard]] inline bool CanChangeAngles()
	{
		if (!localPlayer)
			return false;

		// Overriding the view angles while noclipping or on a ladder fights the
		// movement code instead of the server.
		const int moveType = localPlayer->getMoveType();
		return moveType != MOVETYPE_NOCLIP && moveType != MOVETYPE_LADDER;
	}
}

// Captures the movement command before the anti-aim rewrites the view angles,
// and rebuilds it afterwards so the player still walks where they aimed.
//
// This was one function with a `bool run` parameter selecting between two
// unrelated behaviours, returning a reference to a function-local static.  Two
// named functions and an explicit state struct say the same thing.
namespace AntiAim
{
	struct MovementBackup
	{
		float forwardMove = 0.f;
		float sideMove = 0.f;
		float upMove = 0.f;
		QAngle viewAngles{};
		bool valid = false;
	};

	inline MovementBackup movementBackup;
}

void BackupCMD(CUserCmd* cmd, bool restore)
{
	if (!cmd)
		return;

	if (!restore)
	{
		AntiAim::movementBackup.forwardMove = cmd->forwardmove;
		AntiAim::movementBackup.sideMove = cmd->sidemove;
		AntiAim::movementBackup.upMove = cmd->upmove;
		AntiAim::movementBackup.viewAngles = cmd->viewangles;
		AntiAim::movementBackup.valid = true;
		return;
	}

	// Nothing was captured this tick -- the capture path is inside a guard the
	// restore path is not -- so there is nothing to rebuild from.
	if (!AntiAim::movementBackup.valid)
		return;

	AntiAim::movementBackup.valid = false;

	if (!AntiAim::CanChangeAngles())
		return;

	// The movement vector is expressed relative to the view, so rotating the
	// view by deltaYaw means rotating the movement back by the same amount.
	// https://i.imgur.com/8cED0pl.png
	const float deltaYaw = AngleMath::NormalizeYaw(cmd->viewangles.y - AntiAim::movementBackup.viewAngles.y);

	const float cosDelta = std::cos(DEG2RAD(deltaYaw));
	const float sinDelta = std::sin(DEG2RAD(deltaYaw));

	const float forward = AntiAim::movementBackup.forwardMove;
	const float side = AntiAim::movementBackup.sideMove;

	cmd->forwardmove = cosDelta * forward + std::cos(DEG2RAD(deltaYaw + 90.f)) * side;
	cmd->sidemove = sinDelta * forward + std::sin(DEG2RAD(deltaYaw + 90.f)) * side;

	// Sub-unit movement is rounded away.
	if (cmd->forwardmove > -1.f && cmd->forwardmove < 1.f)
		cmd->forwardmove = 0.f;
	if (cmd->sidemove > -1.f && cmd->sidemove < 1.f)
		cmd->sidemove = 0.f;
}

// fake doesn't works yet
void StaticPitch(CUserCmd* cmd, bool down)
{
	// 90 is out of the engine's valid pitch domain; Untrusted is the opt-in for
	// sending it anyway.
	const float pitch = Globals::Untrusted ? 90.f : 89.f;
	cmd->viewangles.x = down ? -pitch : pitch;
}

void JitterPitch(CUserCmd* cmd)
{
	cmd->viewangles.x = AntiAim::RandomInt(0, 1) ? -89.f : 89.f;
}

void FastSpin(CUserCmd* cmd)
{
	AntiAim::AdvanceYaw(static_cast<float>(AntiAim::RandomInt(0, 359)));

	// NOTE: this one *adds* the accumulator to the current view angle, where
	// SlowSpin and BackJitter below *assign* it.  That asymmetry is almost
	// certainly a typo, but changing it changes how the pattern feels in game,
	// which is a tuning decision rather than a defect fix -- left as it was, and
	// written down instead of being silently changed.
	cmd->viewangles.y = AngleMath::NormalizeYaw(cmd->viewangles.y + AntiAim::yawAccumulator);
}

void SlowSpin(CUserCmd* cmd)
{
	AntiAim::AdvanceYaw(static_cast<float>(AntiAim::RandomInt(0, 9)));
	cmd->viewangles.y = AntiAim::yawAccumulator;
}

void BackJitter(CUserCmd* cmd)
{
	AntiAim::AdvanceYaw(50.f);

	if (AntiAim::RandomInt(0, 49) == 0)
		AntiAim::AdvanceYaw(180.f);

	cmd->viewangles.y = AntiAim::yawAccumulator;
}

void Inverse(CUserCmd* pCmd)
{
	pCmd->viewangles.y = AngleMath::NormalizeYaw(pCmd->viewangles.y - 180.f);
}

void Sideways(CUserCmd* cmd)
{
	cmd->viewangles.y = AngleMath::NormalizeYaw(cmd->viewangles.y - 90.f);
}

void AntiAimPitch(CUserCmd* cmd, int kind)
{
	if (!cmd || !AntiAim::CanChangeAngles())
		return;

	switch (kind)
	{
	case 0:
		break;
	case 1:
		StaticPitch(cmd, false);
		break;
	case 2:
		StaticPitch(cmd, true);
		break;
	case 3:
		JitterPitch(cmd);
		break;
	default:
		// A hand-edited config is not bounded by the menu combo.
		break;
	}
}

void AntiAimYaw(CUserCmd* cmd, int kind)
{
	if (!cmd || !AntiAim::CanChangeAngles())
		return;

	switch (kind)
	{
	case 0:
		break;
	case 1:
		FastSpin(cmd);
		break;
	case 2:
		SlowSpin(cmd);
		break;
	case 3:
		BackJitter(cmd);
		break;
	case 4:
		Inverse(cmd);
		break;
	case 5:
		Sideways(cmd);
		break;
	default:
		break;
	}
}

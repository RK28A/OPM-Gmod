#pragma once

#include "../globals.hpp"
#include "Utils.h"
#include "AutoWall.h"

namespace Triggerbot
{
	// Source's own maximum trace length: 8192 units across the diagonal of the
	// world box.  It was the literal 69696.f, which is past every map's bounds
	// and therefore worked, but said nothing about why.
	inline constexpr float kMaxTraceLength = 8192.f * 1.732f;

	[[nodiscard]] inline bool IsEnabledHitgroup(int hitgroup)
	{
		return (Settings::Triggerbot::triggerBotHead && hitgroup == HITGROUP_HEAD)
			|| (Settings::Triggerbot::triggerBotChest && hitgroup == HITGROUP_CHEST)
			|| (Settings::Triggerbot::triggerBotStomach && hitgroup == HITGROUP_STOMACH);
	}
}

void TriggerBot(CUserCmd* cmd)
{
	if (!Settings::Triggerbot::triggerBot)
		return;

	// Every other module in hacks/ got these guards in the review pass; this one
	// dereferenced localPlayer directly two lines in.
	if (!cmd || !localPlayer || !EngineTrace)
		return;

	const Vector eyePosition = localPlayer->EyePosition();

	trace_t trace;
	CTraceFilter filter;
	filter.pSkip = localPlayer;

	Ray_t ray;
	ray.Init(eyePosition, eyePosition + cmd->viewangles.toVector() * Triggerbot::kMaxTraceLength);
	EngineTrace->TraceRay(ray, MASK_SHOT, &filter, &trace);

	C_BasePlayer* target = (C_BasePlayer*)trace.m_pEnt;
	if (!target || !target->IsPlayer() || !target->IsAlive())
		return;

	if (!Settings::Aimbot::aimAtTeammates && target->InLocalTeam())
		return;

	if (!Triggerbot::IsEnabledHitgroup(trace.hitgroup))
		return;

	// This used to read:
	//
	//     static bool toggle = false;
	//     toggle = !toggle;
	//     if (Settings::Triggerbot::triggerbotFastShoot) {
	//         if (toggle || Settings::Triggerbot::triggerbotFastShoot)
	//             cmd->buttons |= IN_ATTACK;
	//         else cmd->buttons &= ~IN_ATTACK;
	//     }
	//     else cmd->buttons |= IN_ATTACK;
	//
	// The inner condition is a tautology -- the outer `if` already guarantees
	// triggerbotFastShoot is true -- so the `else` was unreachable, `toggle` was
	// computed and never read, and both branches collapsed to the single line
	// below.  The setting has never done anything.
	//
	// Written out as what it does, rather than inverted into what the dead
	// branch suggests it was meant to do: that would be a behaviour change, and
	// it is the author's call, not a defect fix.  The menu entry is relabelled
	// to match, the way the review relabelled "Auto wall" to "Require line of
	// sight"; Settings::Triggerbot::triggerbotFastShoot is still read and
	// written by the config so existing files keep loading.
	cmd->buttons |= IN_ATTACK;
}

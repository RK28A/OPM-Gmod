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

// Runs from hkCreateMove (main thread). Traces from the eye along the current
// command's view angles; on a live enemy player at an enabled hitgroup it
// presses attack for this command. It never touches Lua and never removes
// entities, so it cannot raise the engine's "!ThreadInMainThread" /
// "EntityRemoved" Lua errors.
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

	// The review pass found the old fast-shoot toggle was dead code (a
	// tautology whose `else` was unreachable), left the setting inert, and
	// deferred to the author on whether it should do anything. The author's
	// call is to make it work: alternate IN_ATTACK every other command so
	// semi-automatic weapons re-fire instead of the button staying held down.
	// With the toggle off, hold attack (the classic full-auto behaviour). The
	// menu entry is relabelled from "(not implemented)" to match.
	if (Settings::Triggerbot::triggerbotFastShoot)
	{
		static bool shootThisTick = false;
		shootThisTick = !shootThisTick;

		if (shootThisTick)
			cmd->buttons |= IN_ATTACK;
		else
			cmd->buttons &= ~IN_ATTACK;

		return;
	}

	cmd->buttons |= IN_ATTACK;
}

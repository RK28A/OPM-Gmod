#pragma once

#include "../engine/trace.h"
#include "../engine/gametrace.h"
#include "../globals.hpp"
#include "Utils.h"

namespace AutoWall
{
	// A trace that stops this close to its endpoint hit the target rather than
	// the world.  Was the bare literal 0.98f.
	inline constexpr float kNearEndpointFraction = 0.98f;
}

// Line of sight from `from` to `to`.
//
// This is *not* auto wall.  There is no penetration maths behind it -- no
// surface properties, no damage falloff, no wall thickness -- it is a single
// MASK_SHOT trace that answers "can I see the target".  The review already
// relabelled the menu entry to "Require line of sight" for that reason; the
// name of this function is kept so the call sites read unchanged.
//
// The dead weight that used to sit here is gone: ScaleDamage(), a CS:GO hitgroup
// damage table that nothing called (its only caller was commented out) and that
// would not have applied to GMod anyway, plus three blocks of commented-out
// penetration sketching.
bool CanHit(C_BasePlayer* target, Vector from, Vector to)
{
	if (!localPlayer || !EngineTrace || !localPlayer->GetActiveWeapon())
		return false;

	trace_t trace;
	CTraceFilter filter;
	filter.pSkip = localPlayer;

	Ray_t ray;
	ray.Init(from, to);
	EngineTrace->TraceRay(ray, MASK_SHOT, &filter, &trace);

	return trace.m_pEnt == target || trace.fraction >= AutoWall::kNearEndpointFraction;
}

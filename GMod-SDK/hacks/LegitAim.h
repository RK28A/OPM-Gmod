#pragma once

#include <Windows.h>
#include <mutex>
#include "../globals.hpp"
#include "../mathlib/AngleMath.h"
#include "AutoWall.h"
#include "Utils.h"

namespace LegitAim
{
	// SetupBones() writes at most this many matrices, and every bone index we
	// get back has to stay inside the same bound.
	inline constexpr int kMaxBones = 128;

	// How close the crosshair has to be to the target before auto-fire is
	// allowed, in degrees.
	//
	// Upstream counted smoothing ticks instead and fired once the counter
	// reached smoothSteps.  With an exponential approach (a constant fraction
	// of the *remaining* error per tick) the aim is never mathematically
	// arrived after N ticks, so the counter said nothing about where the
	// crosshair actually was.
	inline constexpr float kAimReadyDegrees = 1.f;

	enum class Selection
	{
		Distance = 0,
		Health = 1,
		Fov = 2,
	};

	[[nodiscard]] inline bool HasValidInterfaces()
	{
		return localPlayer != nullptr
			&& EngineClient != nullptr
			&& ClientEntityList != nullptr
			&& ModelInfo != nullptr
			&& GlobalVars != nullptr
			&& EngineTrace != nullptr;
	}

	// Globals::screenWidth/Height stay zero until the game reports a
	// resolution; dividing by them before that skews every FOV comparison.
	[[nodiscard]] inline bool HasValidScreenSize()
	{
		return Globals::screenWidth > 0 && Globals::screenHeight > 0;
	}

	[[nodiscard]] inline bool IsFriend(C_BasePlayer* entity)
	{
		// lock_guard rather than a bare lock()/unlock() pair: the mutex is
		// released on every path, including one added later that returns from
		// the middle of the block.
		const std::lock_guard<std::mutex> lock(Settings::friendListMutex);

		// find() rather than walking the whole map -- and it replaces the old
		// `friendList.find(entity) != friendList.find(entity)` check further
		// down, which compared a value with itself, was therefore always false,
		// and read the map without holding the mutex.
		const auto it = Settings::friendList.find(entity);
		return it != Settings::friendList.end() && it->second.first;
	}

	[[nodiscard]] inline bool IsTargetable(C_BasePlayer* entity)
	{
		if (!entity || !localPlayer || entity == localPlayer)
			return false;

		if (!entity->IsPlayer() || !entity->IsAlive() || entity->IsDormant())
			return false;

		if (!Settings::Aimbot::aimAtTeammates && entity->getTeamNum() == localPlayer->getTeamNum())
			return false;

		const bool isFriend = IsFriend(entity);

		if (isFriend && !Settings::Aimbot::aimAtFriends && !Settings::Aimbot::onlyAimAtFriends)
			return false;

		if (Settings::Aimbot::onlyAimAtFriends && !isFriend)
			return false;

		return true;
	}

	// Resolves the world position to aim at for `entity`.
	//
	// Returns false instead of aiming at a garbage position whenever anything
	// along the way is missing: the renderable, the model, the studio header,
	// the bone setup, or the configured hitbox.  Upstream dereferenced
	// GetClientRenderable() and GetStudiomodel() unchecked and indexed
	// bones[selectedHitBox] with whatever the bone lookup happened to leave in
	// that variable -- including the previous entity's index, or 0 on a miss.
	[[nodiscard]] inline bool GetAimPosition(C_BasePlayer* entity, Vector& out)
	{
		if (!entity || !ModelInfo || !GlobalVars)
			return false;

		IClientRenderable* renderable = entity->GetClientRenderable();
		if (!renderable)
			return false;

		const model_t* model = static_cast<const model_t*>(renderable->GetModel());
		if (!model)
			return false;

		studiohdr_t* studioModel = ModelInfo->GetStudiomodel(model);
		if (!studioModel)
			return false;

		// studiohdr_t::name is a fixed 64 byte field that the engine is not
		// obliged to terminate, so bound the comparison explicitly.
		if (_strnicmp(studioModel->name, "error.mdl", sizeof("error.mdl")) == 0)
		{
			// error.mdl has no usable skeleton; the eyes are the best we have.
			out = entity->EyePosition();
			return true;
		}

		const char* boneName = IntToBoneName(Settings::Aimbot::aimbotHitbox);
		if (!boneName)
			return false; // hitbox id outside the configured range

		matrix3x4_t bones[kMaxBones];
		if (!renderable->SetupBones(bones, kMaxBones, BONE_USED_BY_HITBOX, GlobalVars->curtime))
			return false;

		int boneIndex = -1;
		if (!Studio_BoneIndexByName(studioModel, boneName, &boneIndex))
			return false; // the model does not have that bone

		if (boneIndex < 0 || boneIndex >= kMaxBones)
			return false;

		out = Vector(bones[boneIndex][0][3], bones[boneIndex][1][3], bones[boneIndex][2][3]);
		return true;
	}

	// Distance in pixels between the target and the centre of the screen.
	[[nodiscard]] inline bool GetFovDistance(const Vector& entPos, float& out)
	{
		if (!HasValidScreenSize())
			return false;

		Vector screenPos;
		if (!WorldToScreen(entPos, screenPos))
			return false;

		screenPos.z = 0.f;
		out = Vector(Globals::screenWidth / 2.f, Globals::screenHeight / 2.f, 0.f).DistTo(screenPos);
		return true;
	}

	// Picks the best candidate according to Settings::Aimbot::aimbotSelection.
	// Returns nullptr when nothing qualifies.
	[[nodiscard]] inline C_BasePlayer* SelectTarget(const Vector& eyePos)
	{
		C_BasePlayer* best = nullptr;
		float bestScore = FLT_MAX;

		// Hoisted: this is a virtual call into the SDK, and it does not change
		// while we walk the list.
		const int highestEntityIndex = ClientEntityList->GetHighestEntityIndex();

		for (int i = 0; i < highestEntityIndex; i++)
		{
			C_BasePlayer* entity = static_cast<C_BasePlayer*>(ClientEntityList->GetClientEntity(i));
			if (!IsTargetable(entity))
				continue;

			Vector entPos;
			if (!GetAimPosition(entity, entPos))
				continue;

			// CanHit() is a plain line-of-sight trace: the penetration maths it
			// would need to deserve the "auto wall" name is not implemented
			// (see AutoWall.h).  So today this option means "only consider
			// targets I can actually see", which is why it rejects a target it
			// cannot reach rather than accepting one behind a wall.
			if (Settings::Aimbot::aimbotAutoWall && !CanHit(entity, eyePos, entPos))
				continue;

			float fovDistance = FLT_MAX;
			float score = FLT_MAX;

			switch (static_cast<Selection>(Settings::Aimbot::aimbotSelection))
			{
			case Selection::Distance:
				score = localPlayer->GetAbsOrigin().DistTo(entPos);
				break;

			case Selection::Health:
				score = static_cast<float>(entity->GetHealth());
				break;

			case Selection::Fov:
				if (!GetFovDistance(entPos, fovDistance))
					continue;
				score = fovDistance;
				break;

			default:
				// An out-of-range config value used to leave `distance` at
				// FLT_MAX for every candidate, so the target ended up being
				// whichever entity happened to come last in the list.  Fall
				// back to the default mode instead.
				score = localPlayer->GetAbsOrigin().DistTo(entPos);
				break;
			}

			if (score > bestScore)
				continue;

			if (Settings::Aimbot::aimbotFovEnabled)
			{
				if (fovDistance == FLT_MAX && !GetFovDistance(entPos, fovDistance))
					continue;

				if (fovDistance > Settings::Aimbot::aimbotFOV)
					continue;
			}

			bestScore = score;
			best = entity;
		}

		return best;
	}
} // namespace LegitAim

void DoLegitAimbot(CUserCmd* cmd)
{
	static C_BasePlayer* lastTarget = nullptr;
	static bool autoFireToggle = false;

	// Everything keyed to a particular target/session, cleared together: the
	// auto-fire toggle must not survive a new target, a disabled aimbot or a
	// reconnect.
	const auto reset = []()
	{
		Settings::Aimbot::finalTarget = nullptr;
		lastTarget = nullptr;
		autoFireToggle = false;
	};

	if (!cmd || !LegitAim::HasValidInterfaces())
	{
		reset();
		return;
	}

	const bool keyDown = PollKey(Settings::Aimbot::aimbotKey,
		Settings::Aimbot::aimbotKeyStyle, Settings::Aimbot::aimbotKeyState);

	if (!keyDown || !Settings::Aimbot::enableAimbot)
	{
		reset();
		return;
	}

	const Vector eyePos = localPlayer->EyePosition();

	// A raw C_BasePlayer* stays non-null after the entity behind it is gone, so
	// the locked target is re-validated every tick rather than trusted from the
	// previous one.  "No target" is nullptr: upstream parked localPlayer in
	// finalTarget as a sentinel, which every consumer then had to know about.
	if (Settings::Aimbot::finalTarget && !LegitAim::IsTargetable(Settings::Aimbot::finalTarget))
		Settings::Aimbot::finalTarget = nullptr;

	const bool keepLockedTarget = Settings::Aimbot::lockOnTarget && Settings::Aimbot::finalTarget != nullptr;

	if (!keepLockedTarget)
		Settings::Aimbot::finalTarget = LegitAim::SelectTarget(eyePos);

	C_BasePlayer* target = Settings::Aimbot::finalTarget;
	if (!target)
	{
		reset();
		return;
	}

	// Recomputed every tick.  On the locked path upstream never wrote finalPos
	// at all -- `Vector finalPos;` went straight into AngleTo() uninitialised --
	// and on the other path it held a position captured during selection, which
	// is stale the moment the target moves.
	Vector finalPos;
	if (!LegitAim::GetAimPosition(target, finalPos))
	{
		reset();
		return;
	}

	// AngleTo() builds its delta as (*this - vOther), so finalPos.AngleTo(eyePos)
	// is the eye -> target direction.  Sanitize() clamps the pitch and wraps the
	// yaw: AngleTo() returns (0,0,0) for a degenerate delta, and the engine must
	// never be handed an out-of-domain angle.
	const QAngle desired = AngleMath::Sanitize(finalPos.AngleTo(eyePos));
	QAngle calc = desired;

	if (lastTarget != target)
		autoFireToggle = false;

	const bool canHit = CanHit(target, eyePos, finalPos);

	// Smoothing only applies to the angles the player actually sees; with silent
	// aim the view never moves, so there is nothing to smooth.
	if (Settings::Aimbot::smoothing && !Settings::Aimbot::silentAim)
	{
		// SmoothingFactor() guards 1.f / smoothSteps against zero, negatives and
		// NaN: the slider is bounded to 10-50, a hand-edited config is not.
		// SmoothTowards() takes the *shortest* way round on each axis, so
		// +179 -> -179 is a 2 degree turn and not a 358 degree one backwards.
		calc = AngleMath::SmoothTowards(cmd->viewangles, desired,
			AngleMath::SmoothingFactor(Settings::Aimbot::smoothSteps));
	}

	const bool aimIsOnTarget = AngleMath::RemainingError(calc, desired) <= LegitAim::kAimReadyDegrees;

	if (!Settings::Aimbot::silentAim)
		EngineClient->SetViewAngles(calc);

	if (Settings::Aimbot::aimbotAutoFire && aimIsOnTarget && canHit)
	{
		// Alternating so a semi-automatic weapon re-triggers between ticks.
		autoFireToggle = !autoFireToggle;

		if (autoFireToggle)
		{
			cmd->buttons |= IN_ATTACK;
		}
		else if (Settings::Aimbot::pistolFastShoot)
		{
			cmd->buttons &= ~IN_ATTACK;
		}
	}

	if (cmd->buttons & IN_ATTACK)
	{
		cmd->viewangles = calc;

		// bSendpacket is resolved by a signature scan at start-up and is null if
		// that scan failed.
		if (Settings::Aimbot::silentAim && Globals::bSendpacket)
			*Globals::bSendpacket = false;
	}

	lastTarget = target;
}

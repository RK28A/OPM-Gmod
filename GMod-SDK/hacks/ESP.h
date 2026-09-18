#pragma once
#include <Windows.h>
#include <string>

#include "../globals.hpp"
#include "menu/drawing.h"
#include "Utils.h"

namespace Esp
{
	// SetupBones() writes at most this many matrices, and every index into the
	// array has to be checked against the same bound.
	inline constexpr int kMaxBones = 128;

	// A renderable pointer below this is not a pointer at all.  Same threshold
	// the bone path already used, named rather than inline.
	inline constexpr uintptr_t kMinValidPointer = 0x1000;

	// Heuristic for "this entity pointer is stale or bogus", kept verbatim from
	// upstream.
	//
	// It reads a member 231 pointers into the object and treats a null there as
	// "the vtable has null functions and thats how we know".  It is unverified,
	// arch-dependent, and it is a raw read at a hard-coded offset -- if the
	// object is ever smaller than this, it is an out-of-bounds read that happens
	// not to fault.  The right check is GetClientClass(), but swapping it in
	// blind, with no way to run the game, would risk more than it repays: it is
	// named and documented here instead, and it is the one thing in this file
	// that was deliberately left as it was.
	inline constexpr std::size_t kEntityProbeOffset = 231;

	[[nodiscard]] inline bool LooksLikeLiveEntity(C_BasePlayer* entity)
	{
		return entity != nullptr
			&& *(uintptr_t*)((char*)entity + kEntityProbeOffset * sizeof(uintptr_t)) != 0;
	}

	// rainbowColor() is a pure function of GlobalVars->realtime, so calling it
	// once per entity produced the same values as calling it once per frame --
	// it was 21 sin() evaluations per entity of pure waste, not a behaviour bug.
	inline void AdvanceRainbows()
	{
		rainbowColor(Settings::ESP::espNameColor, Settings::Misc::rainbowSpeed);
		rainbowColor(Settings::ESP::espBoundingBoxColor, Settings::Misc::rainbowSpeed);
		rainbowColor(Settings::ESP::skeletonEspColor, Settings::Misc::rainbowSpeed);
		rainbowColor(Settings::ESP::espWeaponColor, Settings::Misc::rainbowSpeed);
		rainbowColor(Settings::ESP::espHealthColor, Settings::Misc::rainbowSpeed);
		rainbowColor(Settings::ESP::espAmmoColor, Settings::Misc::rainbowSpeed);
		rainbowColor(Settings::ESP::espDistanceColor, Settings::Misc::rainbowSpeed);
	}

	// Where the info block goes relative to the player's screen box.
	//
	// Cases 2 and 3 -- labelled "Right" and "Left" in the menu -- used to
	// compute the exact same position, so one of the two options did nothing.
	// And with no default, an out-of-range value from a hand-edited config left
	// textPos uninitialised and it was drawn at whatever was on the stack.
	[[nodiscard]] inline Vector InfoPosition(int placement, const Vector& feet, const Vector& head)
	{
		const float halfWidth = (head.y - feet.y) / 4.f;

		switch (placement)
		{
		case 1: // Below
			return Vector(head.x, feet.y, 0.f);
		case 2: // Right
			return Vector(head.x + halfWidth, head.y, 0.f);
		case 3: // Left
			return Vector(head.x - halfWidth, head.y, 0.f);
		case 0: // Above
		default:
			return Vector(head.x, head.y, 0.f);
		}
	}

	// A Lua entity from the user's watch list: name plus an optional 3D box.
	inline void DrawLuaEntity(C_BasePlayer* entity, CCollisionProperty* collideable,
		const std::string& entName, const Vector& screenTopPos)
	{
		DrawString(Vector(screenTopPos.x, screenTopPos.y, 0), StringToWString(entName),
			ColorToRGBA(Settings::ESP::espNameColor), true);

		// Entities are 3D-box only; it reads better than the 2D box.
		if (Settings::ESP::espBoundingBox)
			DrawEspBox3D(collideable->OBBMaxs(), collideable->OBBMins(),
				entity->GetAbsOrigin(), entity->GetAbsAngles(),
				ColorToRGBA(Settings::ESP::espBoundingBoxColor));
	}

	// The skeleton, drawn from a bone matrix array the caller already filled.
	inline void DrawSkeleton(studiohdr_t* studioHdr, const matrix3x4_t* bones)
	{
		for (int i = 0; i < studioHdr->numbones && i < kMaxBones; i++)
		{
			auto bone = studioHdr->pBone(i);
			// numbones can exceed what SetupBones() filled in, and parent is an
			// index into the same array.
			if (!bone || bone->parent < 0 || bone->parent >= kMaxBones)
				continue;

			if (!Settings::ESP::skeletonDetails && !(bone->flags & 256))
				continue;

			const Vector bonePos(bones[i][0][3], bones[i][1][3], bones[i][2][3]);
			const Vector parentPos(bones[bone->parent][0][3], bones[bone->parent][1][3], bones[bone->parent][2][3]);

			if (bonePos == Vector(0, 0, 0) || parentPos == Vector(0, 0, 0))
				continue;

			Vector boneScreen;
			Vector parentScreen;
			if (!WorldToScreen(bonePos, boneScreen) || !WorldToScreen(parentPos, parentScreen))
				continue;

			DrawLine(boneScreen, parentScreen, ColorToRGBA(Settings::ESP::skeletonEspColor));
		}
	}

	// The text block next to a player.
	inline void DrawPlayerInfo(C_BasePlayer* entity, const player_info_s& info,
		const Vector& feet, const Vector& head)
	{
		Vector textPos = InfoPosition(Settings::ESP::infosEmplacement, feet, head);

		if (Settings::ESP::espName)
		{
			DrawString(textPos, StringToWString(strutil::FromBounded(info.name, sizeof(info.name))),
				ColorToRGBA(Settings::ESP::espNameColor), true);
			textPos.y += DrawingFontSize;
		}

		// Both of these need the active weapon.  The ammo branch used to
		// dereference GetActiveWeapon() without the null check the weapon-name
		// branch directly above it already had -- a guaranteed crash on any
		// player holding nothing.
		C_BaseCombatWeapon* weapon = entity->GetActiveWeapon();

		if (Settings::ESP::weaponText && weapon)
		{
			DrawString(textPos, L"Weapon: " + StringToWString(weapon->GetName()),
				ColorToRGBA(Settings::ESP::espWeaponColor), true);
			textPos.y += DrawingFontSize;
		}

		if (Settings::ESP::espHealthBar)
		{
			DrawString(textPos,
				L"Health: " + std::to_wstring(entity->GetHealth()) + L"/" + std::to_wstring(entity->GetMaxHealth()),
				ColorToRGBA(Settings::ESP::espHealthColor), true);
			textPos.y += DrawingFontSize;
		}

		if (Settings::ESP::weaponAmmo && weapon)
		{
			DrawString(textPos, L"Ammos: " + std::to_wstring(weapon->PrimaryAmmoCount()),
				ColorToRGBA(Settings::ESP::espAmmoColor), true);
			textPos.y += DrawingFontSize;
		}

		if (Settings::ESP::espDistance && localPlayer)
		{
			DrawString(textPos,
				L"Distance: " + std::to_wstring((int)entity->GetAbsOrigin().DistTo(localPlayer->GetAbsOrigin())),
				ColorToRGBA(Settings::ESP::espDistanceColor), true);
			textPos.y += DrawingFontSize;
		}
	}
} // namespace Esp

void doEsp()
{
	if (!ClientEntityList || !EngineClient || !ModelInfo)
		return;

	// Once per frame, not once per entity: these depend only on realtime.
	Esp::AdvanceRainbows();

	const int highestEntityIndex = ClientEntityList->GetHighestEntityIndex();

	for (int i = 0; i < highestEntityIndex; i++)
	{
		C_BasePlayer* entity = (C_BasePlayer*)ClientEntityList->GetClientEntity(i);
		if (entity == nullptr || entity == localPlayer) // https://wiki.facepunch.com/gmod/Enums/TEAM
			continue;
		if (!Settings::ESP::espDormant && entity->IsDormant())
			continue;

		bool isEntity = false;
		std::string entName = GetClassName(entity);

		if (Settings::ESP::entEsp && Esp::LooksLikeLiveEntity(entity) && entity->UsesLua())
		{
			const std::lock_guard<std::mutex> lock(Settings::luaEntListMutex);
			isEntity = std::find(Settings::selectedLuaEntList.begin(), Settings::selectedLuaEntList.end(), entName)
				!= Settings::selectedLuaEntList.end();
		}

		if (!isEntity && (!entity->IsPlayer() || !entity->IsAlive()))
			continue;

		// Fetched once instead of eight times across the next few lines, and
		// checked -- it was not.
		CCollisionProperty* collideable = entity->GetCollideable();
		if (!collideable)
			continue;

		const Vector entityAbsOrig = entity->GetAbsOrigin();
		const Vector obbMins = collideable->OBBMins();
		const Vector obbMaxs = collideable->OBBMaxs();
		const Vector entCollMid(obbMins.x + obbMaxs.x, obbMins.y + obbMaxs.y, obbMins.z);

		Vector screenPos;
		Vector screenTopPos;
		if (!WorldToScreen(entityAbsOrig + entCollMid, screenPos)
			|| !WorldToScreen(entityAbsOrig + entCollMid + Vector(0, 0, obbMaxs.z), screenTopPos))
			continue;

		if (isEntity)
		{
			Esp::DrawLuaEntity(entity, collideable, entName, screenTopPos);
			continue;
		}

		const bool foundFriend = std::find(Settings::selectedFriendList.begin(),
			Settings::selectedFriendList.end(), entity) != Settings::selectedFriendList.end();
		if (Settings::ESP::onlyFriends && !foundFriend)
			continue;

		const bool needsBones = Settings::ESP::skeletonEsp || Settings::Aimbot::drawAimbotHeadlines;

		matrix3x4_t bones[Esp::kMaxBones];
		IClientRenderable* renderable = entity->GetClientRenderable();

		// Sometimes SetupBones will crash, so the features that need it are the
		// only ones that call it -- disabling them must not take the rest of the
		// ESP down with it.
		if (needsBones
			&& ((uintptr_t)renderable < Esp::kMinValidPointer
				|| !renderable->SetupBones(bones, Esp::kMaxBones, BONE_USED_BY_HITBOX, EngineClient->Time())))
			continue;

		studiohdr_t* studioHdr = needsBones && renderable
			? ModelInfo->GetStudiomodel((const model_t*)renderable->GetModel())
			: nullptr;

		if (needsBones && !studioHdr)
			continue;

		if (Settings::ESP::skeletonEsp && studioHdr)
			Esp::DrawSkeleton(studioHdr, bones);

		// The lookup can fail (unknown hitbox id, or a model without that bone)
		// and used to leave selectedHitBox at 0 -- or, worse, at an index the
		// caller never validated -- before indexing bones[].
		if (Settings::Aimbot::drawAimbotHeadlines && studioHdr)
		{
			int selectedHitBox = -1;
			const char* headlineBone = IntToBoneName(Settings::Aimbot::aimbotHitbox);
			const bool hasHitBox = Studio_BoneIndexByName(studioHdr, headlineBone, &selectedHitBox) != nullptr
				&& selectedHitBox >= 0 && selectedHitBox < Esp::kMaxBones;

			Vector screenEyePos;
			if (hasHitBox
				&& WorldToScreen(Vector(bones[selectedHitBox][0][3], bones[selectedHitBox][1][3], bones[selectedHitBox][2][3]), screenEyePos)
				&& Vector(Globals::screenWidth / 2.f, Globals::screenHeight / 2.f, 0.f).DistTo(screenEyePos) < Settings::Aimbot::aimbotFOV)
			{
				// white if random person, blue'ish if target
				const int color = entity == Settings::Aimbot::finalTarget ? 0xFF3333FF : 0xFFFFFFFF;
				DrawLine(Vector(Globals::screenWidth / 2.f, Globals::screenHeight / 2.f, 0.f), screenEyePos, color);
			}
		}

		// Zero-initialised and checked: a failed lookup used to leave the struct
		// indeterminate, and info.name was read anyway.
		player_info_s info{};
		if (!EngineClient->GetPlayerInfo(i, &info))
			continue;

		Vector feet;
		Vector head;
		if (!WorldToScreen(entity->GetAbsOrigin(), feet) || !WorldToScreen(entity->EyePosition(), head))
			continue;

		Esp::DrawPlayerInfo(entity, info, feet, head);

		if (Settings::ESP::espBoundingBox)
		{
			if (Settings::ESP::espShapeInt == 0)
				DrawEsp2D(feet, head, ColorToRGBA(Settings::ESP::espBoundingBoxColor));
			else
				DrawEspBox3D(obbMaxs, obbMins, entity->GetAbsOrigin(), entity->EyeAngles(),
					ColorToRGBA(Settings::ESP::espBoundingBoxColor));
		}
	}
}

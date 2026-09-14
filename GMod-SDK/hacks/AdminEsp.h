#pragma once

// Admin ESP -- deliberately kept as its own module, separate from the classic
// player ESP in ESP.h.  It reuses the exact same techniques the rest of the
// cheat already uses (WorldToScreen + the D3DX drawing helpers for the box and
// name, EngineClient->GetPlayerInfo for the name, and a Lua call for the
// staff check).  It adds no concealment of its own and does nothing to make the
// module harder to detect -- it is drawn on screen like every other overlay.

#include <Windows.h>
#include <string>

#include "../globals.hpp"
#include "menu/drawing.h"
#include "Utils.h"

namespace AdminEsp
{
	// Calls Player:IsAdmin() through the Lua interface.
	//
	// IsAdmin() is true for the admin and superadmin usergroups (and any
	// usergroup granted admin rights), which is what "staff" means on the
	// ULX / ServerGuard-style admin mods.  This runs from the Present hook,
	// which in GMod's single-threaded DX9 path is the same thread the engine
	// runs Lua on, so the call is safe -- the same pattern GunHacks.h already
	// uses.  Every path leaves the Lua stack balanced and returns false on any
	// failure.
	[[nodiscard]] inline bool IsAdmin(C_BasePlayer* entity)
	{
		if (!entity || !Lua)
			return false;

		entity->PushEntity();                 // [ent]

		Lua->GetField(-1, "IsAdmin");         // [ent][IsAdmin?]
		if (!Lua->IsType(-1, LuaObjectType::FUNCTION))
		{
			Lua->Pop(2);                      // pop the non-function + the entity
			return false;
		}

		Lua->Push(-2);                        // [ent][fn][ent] (self)
		Lua->Call(1, 1);                      // [ent][bool]
		const bool admin = Lua->GetBool(-1);
		Lua->Pop(2);                          // pop the result + the entity
		return admin;
	}

	// Best-effort usergroup label (e.g. "superadmin", "admin", "moderator") for
	// the tag next to the name.  Empty string on any failure.
	[[nodiscard]] inline std::string GetUserGroup(C_BasePlayer* entity)
	{
		if (!entity || !Lua)
			return std::string();

		entity->PushEntity();
		Lua->GetField(-1, "GetUserGroup");
		if (!Lua->IsType(-1, LuaObjectType::FUNCTION))
		{
			Lua->Pop(2);
			return std::string();
		}

		Lua->Push(-2);
		Lua->Call(1, 1);
		const char* group = Lua->GetString(-1);
		std::string out = group ? group : std::string();
		Lua->Pop(2);
		return out;
	}
}

// Draws the admin overlay.  Called from the Present hook next to doEsp(); it is
// gated on its own toggle and shares nothing with the classic ESP path.
void doAdminEsp()
{
	if (!Settings::ESP::adminEsp)
		return;

	if (!ClientEntityList || !EngineClient || !localPlayer)
		return;

	rainbowColor(Settings::ESP::adminEspColor, Settings::Misc::rainbowSpeed);
	const ULONG color = ColorToRGBA(Settings::ESP::adminEspColor);

	const int highest = ClientEntityList->GetHighestEntityIndex();
	for (int i = 0; i < highest; i++)
	{
		C_BasePlayer* entity = (C_BasePlayer*)ClientEntityList->GetClientEntity(i);
		if (!entity || entity == localPlayer)
			continue;
		if (!entity->IsPlayer() || !entity->IsAlive())
			continue;
		if (!Settings::ESP::espDormant && entity->IsDormant())
			continue;

		if (!AdminEsp::IsAdmin(entity))
			continue;

		// Same two-point box the classic ESP uses: origin at the feet, the
		// eye position at the top.  WorldToScreen ignores occlusion, so this is
		// visible through walls exactly like the rest of the ESP -- no extra
		// work is done to achieve that.
		Vector feet;
		Vector head;
		if (!WorldToScreen(entity->GetAbsOrigin(), feet) || !WorldToScreen(entity->EyePosition(), head))
			continue;

		if (Settings::ESP::adminEspBox)
			DrawEsp2D(feet, head, color);

		if (Settings::ESP::adminEspName)
		{
			// Zero-initialised and checked, like the ESP fix in ESP.h: a failed
			// lookup used to leave player_info_s indeterminate.
			player_info_s info{};
			if (!EngineClient->GetPlayerInfo(i, &info))
				continue;
			info.name[sizeof(info.name) - 1] = '\0';

			const std::string group = AdminEsp::GetUserGroup(entity);
			std::string label = group.empty() ? "[ADMIN] " : ("[" + group + "] ");
			label += info.name;

			DrawTextW(Vector(head.x, head.y, 0), StringToWString(label), color, true);
		}
	}
}

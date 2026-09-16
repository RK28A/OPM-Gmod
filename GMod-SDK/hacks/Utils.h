#pragma once
#include "../mathlib/math_pfns.h"
#include "../tier0/Color.h"
#include "../globals.hpp"
#include "../engine/vmatrix.h"
#include "../tier0/Vector.h"
#include "../client/C_BaseCombatWeapon.h"
#include "../core/KeyState.h"
#include "../core/LuaStack.h"
#include "../core/StringUtil.h"

#include <string>

// RAII balancing for every Lua round trip below.  See core/LuaStack.h for why
// this counts pushes instead of reading Top().
using LuaGuard = lua::StackGuard<CLuaInterface>;

/*bool WorldToScreen(Vector in, Vector& out)
{
	return !IVDebugOverlay->ScreenPosition(in, out);
}*/
bool WorldToScreen(Vector in, Vector& out)
{
	auto matrix = Globals::viewMatr.load().m;

	float w = matrix[3][0] * in.x + matrix[3][1] * in.y + matrix[3][2] * in.z + matrix[3][3];
	if (w > 0.001f)
	{
		float fl1DBw = 1 / w;
		out.x = (Globals::screenWidth / 2) + (0.5f * ((matrix[0][0] * in.x + matrix[0][1] * in.y + matrix[0][2] * in.z + matrix[0][3]) * fl1DBw) * Globals::screenWidth + 0.5f);
		out.y = (Globals::screenHeight / 2) - (0.5f * ((matrix[1][0] * in.x + matrix[1][1] * in.y + matrix[1][2] * in.z + matrix[1][3]) * fl1DBw) * Globals::screenHeight + 0.5f);

		// Screen positions are 2D.  This used to be left at whatever the
		// caller's Vector happened to hold, so every consumer that goes on to
		// call DistTo() -- the aimbot FOV test, the ESP head line -- had to
		// remember to zero it by hand, and doEsp() did not.
		out.z = 0.f;
		return true;
	}
	return false;
}

// --- Lua helpers ------------------------------------------------------------
//
// Every one of these used to return a `const char*` obtained from
// Lua->GetString(), *after* popping the value it points into.  GetString hands
// back a pointer into the Lua stack, so popping makes it collectable: the
// callers in GunHacks.h were comparing freed memory with strcmp, up to four
// times per shot.  They return std::string now, copied before the pop --
// the pattern AdminEsp.h already used.

[[nodiscard]] std::string GetLuaEntBase(C_BaseCombatWeapon* _this)
{
	if (!Lua || !_this || !_this->UsesLua())
		return std::string();

	LuaGuard guard(Lua);

	_this->PushEntity();
	guard.Pushed();

	Lua->GetField(-1, "Base");
	guard.Pushed();

	std::string out;
	if (Lua->IsType(-1, LuaObjectType::STRING))
		if (const char* value = Lua->GetString(-1))
			out = value; // copied while the value is still on the stack

	return out;
}

[[nodiscard]] std::string GetLuaEntName(C_BaseCombatWeapon* _this)
{
	if (!Lua || !_this || !_this->UsesLua())
		return std::string();

	LuaGuard guard(Lua);

	_this->PushEntity();
	guard.Pushed();

	Lua->GetField(-1, "PrintName");
	guard.Pushed();

	std::string out;
	if (Lua->IsType(-1, LuaObjectType::STRING))
		if (const char* value = Lua->GetString(-1))
			out = value;

	return out;
}

double GetLuaWeaponDamage(C_BaseCombatWeapon* _this)
{
	if (!Lua || !_this || !_this->UsesLua())
		return 0.;

	LuaGuard guard(Lua);

	double damage = 1.;

	_this->PushEntity();
	guard.Pushed();

	// SWEP.Primary is a table on most bases but not all; when it is missing the
	// Damage field is read off the entity itself.  The hand-rolled `topop`
	// counter this replaces had to be decremented in exactly the right branch.
	Lua->GetField(-1, "Primary");
	guard.Pushed();

	if (!Lua->IsType(-1, LuaObjectType::TABLE))
		guard.Pop(1);

	Lua->GetField(-1, "Damage");
	guard.Pushed();

	if (Lua->IsType(-1, LuaObjectType::NUMBER))
		damage = Lua->GetNumber(-1);

	return damage;
}

ButtonCode_t VKToButtonCode(int input)
{
	return InputSystem->VirtualKeyToButtonCode(input);
};

// Resolves one key binding.
//
// This replaces the getKeyState macro, whose brace-enclosed body carried its
// own `static bool toggleState`.  Each expansion therefore had private state,
// so the two hooks that both read the thirdperson key (FrameStageNotify, which
// decides whether to rewrite the local view angles, and RenderView, which
// decides whether to move the camera) kept two toggles, advanced at different
// rates, and drifted apart.  Six of the eight call sites also passed six
// arguments to the three-parameter macro -- `henlo1, henlo2, henlo3`, which are
// declared nowhere and which only MSVC's preprocessor silently discards.
//
// `state` is owned per feature (see Settings), not per call site, so every
// reader of a key agrees on its value.  The state machine itself is in
// core/KeyState.h and is unit-tested.
[[nodiscard]] inline bool PollKey(ButtonCode_t key, int style, input::KeyState& state)
{
	const bool rawDown = InputSystem != nullptr
		&& MatSystemSurface != nullptr
		&& InputSystem->IsButtonDown(key)
		&& !MatSystemSurface->IsCursorVisible();

	return input::Resolve(style, rawDown, state);
}

// Returns nullptr for an out-of-range hitbox id.  It used to read
// Settings::Aimbot::aimbotHitbox directly and ignore `input`, and to return ""
// for an unknown value -- which Studio_BoneIndexByName() then matched against
// every bone name in the model.
// https://wiki.facepunch.com/gmod/Entity:GetBoneName
const char* IntToBoneName(int input)
{
	switch (input)
	{
	case 0:
		return "ValveBiped.Bip01_Head1";
	case 1:
		return "ValveBiped.Bip01_Spine2"; // chest
	case 2:
		return "ValveBiped.Bip01_Pelvis"; // stomach
	default:
		return nullptr;
	}
}

// UTF-8 aware.  The previous implementation was
// `std::wstring(input.begin(), input.end())`, which widens one byte at a time:
// any multi-byte sequence -- and GMod nicknames are UTF-8 -- came out as
// mojibake, and with a signed char every byte >= 0x80 sign-extended into a
// five-digit wchar_t.  globals.hpp already carried the correct conversion.
std::wstring StringToWString(const std::string& input)
{
	return s2ws(input);
}

mstudiobone_t* Studio_BoneIndexByName(studiohdr_t* pStudioHdr, char const* pName, int* outIndex = nullptr)
{
	if (!pStudioHdr || !pName)
		return nullptr;

	int start = 0, end = pStudioHdr->numbones - 1;
	const BYTE* pBoneTable = pStudioHdr->GetBoneTableSortedByName();
	mstudiobone_t* pbones = pStudioHdr->pBone(0);

	while (start <= end)
	{
		int mid = (start + end) >> 1;
		const char* boneName = pbones[pBoneTable[mid]].pszName();
		int cmp = strcmp(boneName, pName);

		if (cmp < 0)
		{
			start = mid + 1;
		}
		else if (cmp > 0)
		{
			end = mid - 1;
		}
		else
		{
			if (outIndex != nullptr)
				*outIndex = pBoneTable[mid];

			return pStudioHdr->pBone(pBoneTable[mid]);
		}
	}
	return nullptr;
}

// player_info_s is left indeterminate by a failed GetPlayerInfo(), and its
// return value used to be discarded here.  GetSteamID() was worse: it returned
// `info.guid`, a pointer into a local that had already gone out of scope.
int GetUserId(int entListIndex)
{
	if (!EngineClient)
		return -1;

	player_info_s info{};
	if (!EngineClient->GetPlayerInfo(entListIndex, &info))
		return -1;

	return info.userID;
}

[[nodiscard]] std::string GetSteamID(int entListIndex)
{
	if (!EngineClient)
		return std::string();

	player_info_s info{};
	if (!EngineClient->GetPlayerInfo(entListIndex, &info))
		return std::string();

	// The engine is not obliged to terminate the field.
	return strutil::FromBounded(info.guid, sizeof(info.guid));
}

#include "../Memory.h"

// The class name is not exposed on C_BasePlayer, so it is reached through a
// signature scan.  findPattern() returns null when the scan fails, and
// GetRealFromRelative() dereferences its argument, so the resolution has to be
// guarded before it is used -- not after, which is where the null checks added
// for the other scanned pointers sit.
[[nodiscard]] std::string GetClassName(C_BasePlayer* _this)
{
	if (!_this)
		return std::string();

	static _GetClassName getClassName = []() -> _GetClassName {
		const char* match = findPattern("client", GetClassNamePattern, "GetClassName");
		if (!match)
			return nullptr;

		return (_GetClassName)(GetRealFromRelative((char*)match, 1, 5, true));
	}();

	if (!getClassName)
		return std::string();

	const char* name = getClassName(_this);
	return name ? std::string(name) : std::string();
}

// math.Rand, math.randomseed and CurTime are plain Lua globals: a server script
// can replace any of them with a non-function, and Lua->Call() on a non-function
// raises a Lua error across the FFI boundary.  Every one of these now checks the
// type first, the way AdminEsp.h does.

double LuaMathRand(double min, double max)
{
	if (!Lua)
		return 0.;

	LuaGuard guard(Lua);

	Lua->PushSpecial(0); // SPECIAL_GLOB
	guard.Pushed();

	Lua->GetField(-1, "math");
	guard.Pushed();
	if (!Lua->IsType(-1, LuaObjectType::TABLE))
		return 0.;

	Lua->GetField(-1, "Rand");
	guard.Pushed();
	if (!Lua->IsType(-1, LuaObjectType::FUNCTION))
		return 0.;

	Lua->PushNumber(min);
	Lua->PushNumber(max);
	guard.Pushed(2);

	// Call pops the function and its two arguments and pushes one result.
	Lua->Call(2, 1);
	guard.Consumed(3);
	guard.Pushed();

	return Lua->GetNumber(-1);
}

void LuaMathSetSeed(double seed)
{
	if (!Lua)
		return;

	LuaGuard guard(Lua);

	Lua->PushSpecial(0); // SPECIAL_GLOB
	guard.Pushed();

	Lua->GetField(-1, "math");
	guard.Pushed();
	if (!Lua->IsType(-1, LuaObjectType::TABLE))
		return;

	Lua->GetField(-1, "randomseed");
	guard.Pushed();
	if (!Lua->IsType(-1, LuaObjectType::FUNCTION))
		return;

	Lua->PushNumber(seed);
	guard.Pushed();

	// Call pops the function and its argument and pushes nothing.
	Lua->Call(1, 0);
	guard.Consumed(2);
}

double LuaCurTime()
{
	if (!Lua)
		return 0.;

	LuaGuard guard(Lua);

	Lua->PushSpecial(0); // SPECIAL_GLOB
	guard.Pushed();

	Lua->GetField(-1, "CurTime");
	guard.Pushed();
	if (!Lua->IsType(-1, LuaObjectType::FUNCTION))
		return 0.;

	// Call pops the function and pushes one result.
	Lua->Call(0, 1);
	guard.Consumed(1);
	guard.Pushed();

	return Lua->GetNumber(-1);
}

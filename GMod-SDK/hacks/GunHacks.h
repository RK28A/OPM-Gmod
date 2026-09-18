#pragma once

#include <optional>
#include <string>

#include "../globals.hpp"
#include "Utils.h"
#include "../tier1/checksum_md5.h"

namespace GunHacksDetail
{
	// Size of CInput::m_pVerifiedCommands, as the engine indexes it.
	//
	// This was the bare literal 90 at three call sites.  Source's own constant
	// is MULTIPLAYER_BACKUP = 150; 90 is what upstream used and it is NOT
	// verified against GMod's build, so the modulus may not match the engine's.
	// It is named here so there is one place to correct it, and the index is
	// bounds-checked below -- the write lands in an engine-owned array, and a
	// negative index would land *before* it.
	inline constexpr int kVerifiedCommandBackup = 90;

	// Writes a command back into the verified-command ring, if the slot index is
	// sane.
	//
	// command_number is a signed int and the loops below walk backwards from the
	// current command, so it can reach 0 and then -1 -- and in C++ `-1 % 90` is
	// -1, an out-of-bounds write one element before the array.
	inline void StoreVerifiedCommand(CUserCmd* command)
	{
		if (!Input || !Input->m_pVerifiedCommands || !command)
			return;

		if (command->command_number < 0)
			return;

		const int slot = command->command_number % kVerifiedCommandBackup;
		if (slot < 0 || slot >= kVerifiedCommandBackup)
			return;

		Input->m_pVerifiedCommands[slot].m_cmd = *command;
		Input->m_pVerifiedCommands[slot].m_crc = command->GetChecksum();
	}

	// Reversing the previous usercmd's angles maxes out the recoil the weapon
	// script applies.  Shared by the fas2 and cw paths, which carried two copies
	// differing only in how many commands back they walked.
	inline void ReverseRecoil(CUserCmd* cmd, int steps)
	{
		if (!cmd || !Input || cmd->command_number <= 0)
			return;

		CUserCmd* current = cmd;
		for (int i = 0; i < steps; i++)
		{
			if (current->command_number <= 0)
				break;

			// GetUserCmd() returns null for a slot with no backing command; the
			// result used to be dereferenced straight away.
			CUserCmd* previous = Input->GetUserCmd(current->command_number - 1);
			if (!previous)
				break;

			previous->viewangles.x = -current->viewangles.x;
			previous->viewangles.y = current->viewangles.y - 180.f;
			previous->viewangles.FixAngles();

			StoreVerifiedCommand(previous);

			current = previous;
		}
	}
}

/*
* How does this works?
* Basically, it calculates the spread angles the exact same way as the server does, so the prediction is simply perfect.
* You set the seed the same way the server does, and do the calculation just like the server does.
* Result? Not a single bullet will deviate.
*/
void NoSpread(CUserCmd* cmd, C_BaseCombatWeapon* gun, CLuaInterface* luaInterface)
{
	if (!cmd || !gun || !localPlayer || !UniformRandomStream)
		return;

	if (!(cmd->buttons & IN_ATTACK) || gun->PrimaryAmmoCount() < 1)
		return;

	// `spread == FLT_MAX` as a "not resolved" sentinel meant a float equality
	// test against a magic value; optional says it directly.
	std::optional<double> spread;

	if (!gun->UsesLua())
	{
		const Vector gunSpread = gun->GetBulletSpread();
		spread = (gunSpread.x + gunSpread.y + gunSpread.z) / 3.0;
	}
	else if (luaInterface)
	{
		// Resolved once.  The if/else chain below used to call GetLuaEntBase()
		// again in every branch it tested -- up to three full Lua round trips
		// per shot, each pushing the entity, reading a field and popping again,
		// and each returning a pointer into the part of the stack it had just
		// popped.
		const std::string base = GetLuaEntBase(gun);

		LuaGuard guard(luaInterface);

		gun->PushEntity();
		guard.Pushed();

		if (base == "tfa_gun_base")
		{
			luaInterface->GetField(-1, "CalculateConeRecoil");
			guard.Pushed();

			if (luaInterface->IsType(-1, LuaObjectType::FUNCTION))
			{
				luaInterface->Push(-2); // self
				guard.Pushed();

				luaInterface->Call(1, 1);
				guard.Consumed(2);
				guard.Pushed();

				spread = luaInterface->GetNumber(-1);
			}
		}
		// god that's annoying, i got it to work a few minutes ago, did some code cleaning, and suddenly it stopped working... even the backups...
		else if (base == "fas2_base" || base == "cw_base")
		{
			const bool isCw = (base == "cw_base");

			// 	self.CurCone = math.Clamp(cone + self.AddSpread * (self.dt.Bipod and 0.5 or 1) + (vel / 10000 * self.VelocitySensitivity) * (self.dt.Status == FAS_STAT_ADS and 0.25 or 1) + self.Owner.ViewAff, 0, 0.09 + self.MaxSpreadInc)
			luaInterface->GetField(-1, "MaxSpreadInc");
			guard.Pushed();

			double curCone = 0.09 + luaInterface->GetNumber(-1);
			guard.Release(); // done with the Lua stack on this path

			//if self.Owner:Crouching() then cone = cone * 0.85 end
			if (isCw && (cmd->buttons & IN_DUCK))
				curCone *= 0.85;

			// 	math.randomseed(commandNumber)
			if (isCw)
				LuaMathSetSeed(static_cast<double>(cmd->command_number));
			else
				LuaMathSetSeed(LuaCurTime());

			QAngle spreadAng;
			spreadAng.x = static_cast<float>(LuaMathRand(-curCone, curCone));
			spreadAng.y = static_cast<float>(LuaMathRand(-curCone, curCone));
			spreadAng.z = 0.f;

			cmd->viewangles -= (spreadAng * 25.f);
			cmd->viewangles -= localPlayer->GetViewPunch();

			// 		Dir = (self.Owner:EyeAngles() + self.Owner:GetViewPunchAngles() + Angle(math.Rand(-cone, cone), math.Rand(-cone, cone), 0) * 25):Forward()
			GunHacksDetail::ReverseRecoil(cmd, isCw ? 3 : 1);

			/* This is wip.
			* This is almost identical to CW2's / FAS2's spread.
			*
			* Source spread isn't being used at all, because before it calls FireBullets, it sets bul.Spread to 0
			*/
			return;
		}
		else
		{
			luaInterface->GetField(-1, "Primary");
			guard.Pushed();

			if (!luaInterface->IsType(-1, LuaObjectType::TABLE))
				guard.Pop(1);

			luaInterface->GetField(-1, "Spread");
			guard.Pushed();

			if (luaInterface->IsType(-1, LuaObjectType::NUMBER))
			{
				spread = luaInterface->GetNumber(-1);
			}
			else
			{
				guard.Pop(1);

				luaInterface->GetField(-1, "Cone");
				guard.Pushed();

				if (luaInterface->IsType(-1, LuaObjectType::NUMBER))
					spread = luaInterface->GetNumber(-1);
			}
		}
	}
	else
	{
		return;
	}

	if (!spread.has_value())
		return;

	const BYTE seed = MD5_PseudoRandom(cmd->command_number) & 0xFF;
	UniformRandomStream->SetSeed(seed);

	const QAngle engineSpread(
		UniformRandomStream->RandomFloat(-0.5f, 0.5f) + UniformRandomStream->RandomFloat(-0.5f, 0.5f),
		UniformRandomStream->RandomFloat(-0.5f, 0.5f) + UniformRandomStream->RandomFloat(-0.5f, 0.5f),
		0.f);

	// X will not be accounted, Z is the equivalent of Y, and Y the equivalent of X
	Vector shootDirection(1.f, 1.f, 1.f);
	shootDirection.y = static_cast<float>(*spread * engineSpread.y);
	shootDirection.z = -static_cast<float>(*spread * engineSpread.x);

	cmd->viewangles += shootDirection.toAngle();
}

void GunHacks(CUserCmd* cmd, C_BaseCombatWeapon* _this)
{
	if (!cmd || !_this || !Lua)
		return;

	if (Settings::Misc::noSpread)
		NoSpread(cmd, _this, Lua);

	if (!_this->UsesLua())
		return;

	// One lookup for the whole chain, as in NoSpread above.
	const std::string base = GetLuaEntBase(_this);

	LuaGuard guard(Lua);

	_this->PushEntity();
	guard.Pushed();

	/* Basically what this does is:
	* Get the gun's base name.
	* If it is M9K, or CW, or FAS, modify it's exact fields.
	* If it is another base, just apply every other base's fields to it too.
	* Universal norecoil :-)
	*/
	if (base == "bobs_gun_base") // if the gun's base == m9k
	{
		Lua->GetField(-1, "Primary");
		guard.Pushed();

		if (!Lua->IsType(-1, LuaObjectType::TABLE)) // if SWEP.Primary is a table
			return;

		// Set whether or not noRecoil is on, exactly as upstream had it: only
		// the Kick* fields below are gated.
		Lua->PushNumber(0);
		Lua->SetField(-2, "IronAccuracy"); // SWEP.Primary.IronAccuracy = 0

		if (Settings::Misc::noRecoil)
		{
			Lua->PushNumber(0);
			Lua->SetField(-2, "KickHorizontal"); // SWEP.Primary.KickHorizontal = 0
			Lua->PushNumber(0);
			Lua->SetField(-2, "KickUp"); // SWEP.Primary.KickUp = 0
			Lua->PushNumber(0);
			Lua->SetField(-2, "KickDown"); // SWEP.Primary.KickDown = 0
		}
	}
	else if (base == "cw_base") // if the gun's base == cw2
	{
		if (Settings::Misc::noRecoil)
		{
			Lua->PushNumber(0);
			Lua->SetField(-2, "Recoil");
			Lua->PushBool(true);
			Lua->SetField(-2, "NoFreeAim"); // <-- That is the secret :)
		}
	}
	else if (base == "fas2_base") // if the gun's base == fas2
	{
		if (Settings::Misc::noRecoil)
		{
			Lua->PushNumber(0);
			Lua->SetField(-2, "Recoil"); // SWEP.Recoil = 0
			Lua->PushNumber(0);
			Lua->SetField(-2, "ViewKick"); // SWEP.ViewKick = 0
		}
	}
	else if (base == "tfa_gun_base")
	{
		// Every statement in this branch is commented out upstream, so the
		// option does nothing for TFA weapons while the menu says otherwise.
		// Left as it was: writing the fields blind, with no way to test against
		// a TFA weapon, would be a guess rather than a fix.
	}
	else
	{
		if (Settings::Misc::noRecoil)
		{
			Lua->GetField(-1, "Primary");
			guard.Pushed();

			if (!Lua->IsType(-1, LuaObjectType::TABLE))
				guard.Pop(1);

			Lua->PushNumber(0);
			Lua->SetField(-2, "Recoil");
			Lua->PushNumber(0);
			Lua->SetField(-2, "ViewKick");
			Lua->PushNumber(0);
			Lua->SetField(-2, "KickHorizontal");
			Lua->PushNumber(0);
			Lua->SetField(-2, "KickUp");
			Lua->PushNumber(0);
			Lua->SetField(-2, "KickDown");
		}
	}
}

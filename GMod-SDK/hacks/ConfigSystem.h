#pragma once

#include "../globals.hpp"
#include <fstream>
#include <filesystem>
#include <system_error>
#include "../json.h"
#include <iomanip>
#pragma comment( user, "Compiled on " __DATE__ " at " __TIME__ )

using nlohmann::json;
namespace ConfigSystem
{
	// A config file is external input.  Every read below used to be `j["key"]`,
	// which throws nlohmann::json::type_error or out_of_range the moment a key
	// is missing or holds the wrong type -- and the whole load was wrapped in
	// one catch-all that responded by overwriting the file and reloading it, so
	// a single bad key discarded every setting in it.  `.value(key, default)`
	// reads what is there and falls back per field instead.
	json to_jsonfcol(const Color color) {
		json j;
		j["r"] = color.fCol[0];
		j["g"] = color.fCol[1];
		j["b"] = color.fCol[2];
		j["a"] = color.fCol[3];
		j["c"] = color.rainbow;
		return j;
	}
	Color from_jsonfcol(const json& j, Color& color) {
		// Taken by const reference: it was by value, so every colour in the file
		// was deep-copied once per load.
		if (!j.is_object())
			return color;

		color.fCol[0] = j.value("r", color.fCol[0]);
		color.fCol[1] = j.value("g", color.fCol[1]);
		color.fCol[2] = j.value("b", color.fCol[2]);
		color.fCol[3] = j.value("a", color.fCol[3]);
		color.rainbow = j.value("c", color.rainbow);
		return color;
	}

	json to_jsonchams(const chamsSetting& setting) {
		json j;
		j["hiddenColor"] = to_jsonfcol(setting.hiddenColor);
		j["visibleColor"] = to_jsonfcol(setting.visibleColor);
		j["hiddenMaterial"] = setting.hiddenMaterial;
		j["visibleMaterial"] = setting.visibleMaterial;
		return j;
	}
	chamsSetting from_jsonchams(const json& j) {
		chamsSetting setting(Color(255, 255, 255), Color(255, 255, 255), 0, 0);
		if (!j.is_object())
			return setting;

		if (j.contains("hiddenColor"))
			from_jsonfcol(j["hiddenColor"], setting.hiddenColor);
		if (j.contains("visibleColor"))
			from_jsonfcol(j["visibleColor"], setting.visibleColor);

		setting.hiddenMaterial = j.value("hiddenMaterial", setting.hiddenMaterial);
		setting.visibleMaterial = j.value("visibleMaterial", setting.visibleMaterial);
		return setting;
	}
	enum configHandle {
		Save,
		Load,
		Reset
	};
	// Not constexpr any more: it never could be -- it does I/O-shaped work on a
	// json object -- and C++17 does not allow a try block in a constexpr
	// function.  The redundant (T*) casts on a pointer that is already T* are
	// gone too.
	template <class T>
	T HandleConfigItem(json& j, configHandle handle, T* setting, T defaultValue)
	{
		switch (handle)
		{
		case configHandle::Save:
			j = *setting;
			break;

		case configHandle::Load:
			// Per field, so one corrupt or missing key costs that key alone
			// rather than the entire file.
			try
			{
				*setting = j.is_null() ? defaultValue : j.get<T>();
			}
			catch (const json::exception&)
			{
				*setting = defaultValue;
			}
			break;

		case configHandle::Reset:
			*setting = defaultValue;
			break;
		}
		return *setting;
	}
	void HandleConfigC(json& j, configHandle handle, Color& setting, Color defaultValue)
	{
		switch (handle)
		{
		case configHandle::Save:
			j = to_jsonfcol(setting);
			break;
		case configHandle::Load:
			from_jsonfcol(j, setting);
			break;
		case configHandle::Reset:
			setting = defaultValue;
			break;
		}
	}
	void HandleConfigCS(json& j, configHandle handle, chamsSetting* setting, chamsSetting defaultValue)
	{
		switch (handle)
		{
		case configHandle::Save:
			j = to_jsonchams(*setting);
			break;
		case configHandle::Load:
			*setting = from_jsonchams(j);
			break;
		case configHandle::Reset:
			*setting = defaultValue;
			break;
		}
	}

	// A config file is external input: the sliders bound these values in the
	// UI, nothing bounds them on the way in from disk.  Anything that ends up
	// indexing an array or dividing must be checked here.
	void ValidateSettings()
	{
		Settings::Visuals::fov = Settings::Visuals::ClampFov(Settings::Visuals::fov);
		Settings::Visuals::viewModelFov = Settings::Visuals::ClampFov(Settings::Visuals::viewModelFov);
		Settings::Misc::zoomFOV = Settings::Visuals::ClampFov(Settings::Misc::zoomFOV);

		// AngleMath::SmoothingFactor() divides by this every tick.
		if (!std::isfinite(Settings::Aimbot::smoothSteps) || Settings::Aimbot::smoothSteps < 1.f)
			Settings::Aimbot::smoothSteps = 1.f;
		else if (Settings::Aimbot::smoothSteps > 50.f)
			Settings::Aimbot::smoothSteps = 50.f;

		if (!std::isfinite(Settings::Aimbot::aimbotFOV) || Settings::Aimbot::aimbotFOV < 0.f)
			Settings::Aimbot::aimbotFOV = 5.f;

		if (!std::isfinite(Settings::Aimbot::aimbotMinDmg) || Settings::Aimbot::aimbotMinDmg < 0.f)
			Settings::Aimbot::aimbotMinDmg = 1.f;

		// Array indexes.
		if (Settings::Misc::hitmarkerSound < 0 || Settings::Misc::hitmarkerSound >= Settings::Misc::kHitmarkerSoundCount)
			Settings::Misc::hitmarkerSound = 0;

		if (Settings::Aimbot::aimbotHitbox < 0 || Settings::Aimbot::aimbotHitbox >= Settings::Aimbot::kHitboxCount)
			Settings::Aimbot::aimbotHitbox = 0;

		if (Settings::Aimbot::aimbotSelection < 0 || Settings::Aimbot::aimbotSelection >= Settings::Aimbot::kSelectionCount)
			Settings::Aimbot::aimbotSelection = 0;

		if (!std::isfinite(Settings::Misc::thirdpersonDistance) || Settings::Misc::thirdpersonDistance < 0.f)
			Settings::Misc::thirdpersonDistance = 100.f;

		if (!std::isfinite(Settings::Misc::freeCamSpeed) || Settings::Misc::freeCamSpeed < 0.f)
			Settings::Misc::freeCamSpeed = 1.f;
	}

	// Where configs live.
	//
	// Was hard-coded to the root of the system drive ("C:\GMOD-SDK-Settings"),
	// which simply fails on a machine where that is not writable -- and
	// CreateDirectory's result was discarded, so the failure was silent and the
	// subsequent open failed for reasons nothing reported.  New saves go under
	// %LOCALAPPDATA%; the old location is still *read* so existing configs keep
	// loading.
	[[nodiscard]] inline std::filesystem::path SettingsDirectory()
	{
		char buffer[MAX_PATH] = {};
		const DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", buffer, sizeof(buffer));

		if (length > 0 && length < sizeof(buffer))
			return std::filesystem::path(buffer) / "GMod-SDK" / "Settings";

		return std::filesystem::path("C:\\GMOD-SDK-Settings");
	}

	[[nodiscard]] inline std::filesystem::path LegacySettingsDirectory()
	{
		return std::filesystem::path("C:\\GMOD-SDK-Settings");
	}

	// Depth guard for the "file is missing, write defaults and read them back"
	// path below.  It used to recurse unconditionally: if the save could not be
	// written -- an unwritable directory being exactly the case that gets you
	// there -- Load called Save called Load for as long as the stack held.
	inline int handleConfigDepth = 0;

	struct DepthGuard
	{
		DepthGuard() { ++handleConfigDepth; }
		~DepthGuard() { --handleConfigDepth; }
	};

	void _HandleConfig(const char* configName, configHandle handle)
	{
		const DepthGuard depthGuard;

		const std::filesystem::path directory = SettingsDirectory();

		std::error_code ec;
		std::filesystem::create_directories(directory, ec);

		json j;
		if (handle == configHandle::Load)
		{
			std::filesystem::path file = directory / configName;

			// Fall back to the pre-%LOCALAPPDATA% location so an existing
			// config is not silently replaced by defaults.
			if (!std::filesystem::exists(file))
			{
				const std::filesystem::path legacy = LegacySettingsDirectory() / configName;
				if (std::filesystem::exists(legacy))
					file = legacy;
			}

			std::ifstream i(file);
			if (!i.is_open()) {
				if (handleConfigDepth > 2)
					return; // cannot write the defaults either; leave them in memory

				_HandleConfig(configName, configHandle::Save);
				return _HandleConfig(configName, configHandle::Load);
			}

			try
			{
				i >> j;
			}
			catch (const json::exception&)
			{
				// Malformed file: carry on with an empty object so every field
				// falls back to its default, rather than throwing out of here.
				j = json::object();
			}
		}

		try {
			HandleConfigItem(j["Globals"]["menuKey"], handle, &Settings::menuKey, KEY_INSERT);
			HandleConfigItem(j["Globals"]["menuKeyStyle"], handle, &Settings::menuKeyStyle, 1);
			HandleConfigC(j["Globals"]["menuColor"], handle, Settings::menuColor, Color(0, 255, 0));
			HandleConfigItem(j["Globals"]["untrusted"], handle, &Globals::Untrusted, false);

			HandleConfigCS(j["Chams"]["playerChams"], handle, &Settings::Chams::playerChamsSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));
			HandleConfigCS(j["Chams"]["teamMateSettings"], handle, &Settings::Chams::teamMateSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));
			HandleConfigCS(j["Chams"]["ragdollChamsSettings"], handle, &Settings::Chams::ragdollChamsSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));
			HandleConfigCS(j["Chams"]["weaponChamsSettings"], handle, &Settings::Chams::weaponChamsSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));
			HandleConfigCS(j["Chams"]["npcChamsSettings"], handle, &Settings::Chams::npcChamsSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));
			HandleConfigCS(j["Chams"]["armChamsSettings"], handle, &Settings::Chams::armChamsSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));
			HandleConfigCS(j["Chams"]["localPlayerChamsSettings"], handle, &Settings::Chams::localPlayerChamsSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));
			HandleConfigCS(j["Chams"]["netLocalChamsSettings"], handle, &Settings::Chams::netLocalChamsSettings, chamsSetting(Color(255, 255, 255), Color(255, 255, 255), 0, 0));

			HandleConfigItem(j["ESP"]["infosEmplacement"], handle, &Settings::ESP::infosEmplacement, NULL);
			HandleConfigItem(j["ESP"]["espDormant"], handle, &Settings::ESP::espDormant, false);
			HandleConfigItem(j["ESP"]["espBoundingBox"], handle, &Settings::ESP::espBoundingBox, false);
			HandleConfigC(j["ESP"]["espBoundingBoxColor"], handle, Settings::ESP::espBoundingBoxColor, Color(255, 255, 255));
			HandleConfigItem(j["ESP"]["espHealthBar"], handle, &Settings::ESP::espHealthBar, false);
			HandleConfigItem(j["ESP"]["espName"], handle, &Settings::ESP::espName, false);
			HandleConfigItem(j["ESP"]["weaponText"], handle, &Settings::ESP::weaponText, false);
			HandleConfigItem(j["ESP"]["weaponAmmo"], handle, &Settings::ESP::weaponAmmo, false);
			HandleConfigItem(j["ESP"]["espDistance"], handle, &Settings::ESP::espDistance, false);
			HandleConfigItem(j["ESP"]["skeletonEsp"], handle, &Settings::ESP::skeletonEsp, false);
			HandleConfigItem(j["ESP"]["skeletonDetails"], handle, &Settings::ESP::skeletonDetails, false);
			HandleConfigC(j["ESP"]["skeletonEspColor"], handle, Settings::ESP::skeletonEspColor, Color(255, 255, 255));
			HandleConfigC(j["ESP"]["espDistanceColor"], handle, Settings::ESP::espDistanceColor, Color(255, 255, 255));
			HandleConfigC(j["ESP"]["espAmmoColor"], handle, Settings::ESP::espAmmoColor, Color(255, 255, 255));
			HandleConfigC(j["ESP"]["espWeaponColor"], handle, Settings::ESP::espWeaponColor, Color(255, 255, 255));
			HandleConfigC(j["ESP"]["espNameColor"], handle, Settings::ESP::espNameColor, Color(255, 255, 255));
			HandleConfigC(j["ESP"]["espHealthColor"], handle, Settings::ESP::espHealthColor, Color(255, 255, 255));
			HandleConfigItem(j["ESP"]["espShapeInt"], handle, &Settings::ESP::espShapeInt, NULL);
			HandleConfigItem(j["ESP"]["entEsp"], handle, &Settings::ESP::entEsp, false);
			HandleConfigItem(j["ESP"]["onlyFriends"], handle, &Settings::ESP::onlyFriends, false);

			HandleConfigItem(j["ESP"]["adminEsp"], handle, &Settings::ESP::adminEsp, false);
			HandleConfigItem(j["ESP"]["adminEspName"], handle, &Settings::ESP::adminEspName, true);
			HandleConfigItem(j["ESP"]["adminEspBox"], handle, &Settings::ESP::adminEspBox, true);
			HandleConfigC(j["ESP"]["adminEspColor"], handle, Settings::ESP::adminEspColor, Color(255, 140, 0));

			HandleConfigItem(j["Visuals"]["fovEnabled"], handle, &Settings::Visuals::fovEnabled, false);
			HandleConfigItem(j["Visuals"]["fov"], handle, &Settings::Visuals::fov, 130.f);
			// JSON key kept as-is so existing config files still load; only the
			// C++ identifier was renamed for consistency with the enable flag.
			HandleConfigItem(j["Visuals"]["viewModelFovEnabled"], handle, &Settings::Visuals::viewModelFovEnabled, false);
			HandleConfigItem(j["Visuals"]["viewModelFOV"], handle, &Settings::Visuals::viewModelFov, 90.f);
			HandleConfigItem(j["Visuals"]["noVisualRecoil"], handle, &Settings::Visuals::noVisualRecoil, false);
			HandleConfigC(j["Visuals"]["worldColor"], handle, Settings::Visuals::worldColor, Color(255, 255, 255));
			HandleConfigItem(j["Visuals"]["changeWorldColor"], handle, &Settings::Visuals::changeWorldColor, false);
			HandleConfigItem(j["Visuals"]["disableSkyBox"], handle, &Settings::Visuals::disableSkyBox, false);
			HandleConfigItem(j["Visuals"]["fullBright"], handle, &Settings::Visuals::fullBright, false);

			HandleConfigItem(j["AntiAim"]["currentAntiAimPitch"], handle, &Settings::AntiAim::currentAntiAimPitch, NULL);
			HandleConfigItem(j["AntiAim"]["currentAntiAimYaw"], handle, &Settings::AntiAim::currentAntiAimYaw, NULL);
			HandleConfigItem(j["AntiAim"]["enableAntiAim"], handle, &Settings::AntiAim::enableAntiAim, false);
			HandleConfigItem(j["AntiAim"]["antiAimKey"], handle, &Settings::AntiAim::antiAimKey, KEY_NONE);
			HandleConfigItem(j["AntiAim"]["antiAimKeyStyle"], handle, &Settings::AntiAim::antiAimKeyStyle, 1);
			HandleConfigItem(j["AntiAim"]["fakePitch"], handle, &Settings::AntiAim::fakePitch, 0.f);

			HandleConfigItem(j["Aimbot"]["aimbotFOV"], handle, &Settings::Aimbot::aimbotFOV, 5.f);
			HandleConfigItem(j["Aimbot"]["silentAim"], handle, &Settings::Aimbot::silentAim, false);
			HandleConfigItem(j["Aimbot"]["lockOnTarget"], handle, &Settings::Aimbot::lockOnTarget, false);
			HandleConfigItem(j["Aimbot"]["aimbotKey"], handle, &Settings::Aimbot::aimbotKey, KEY_NONE);
			HandleConfigItem(j["Aimbot"]["aimbotKeyStyle"], handle, &Settings::Aimbot::aimbotKeyStyle, NULL);
			HandleConfigItem(j["Aimbot"]["enableAimbot"], handle, &Settings::Aimbot::enableAimbot, false);
			HandleConfigItem(j["Aimbot"]["aimbotHitbox"], handle, &Settings::Aimbot::aimbotHitbox, NULL);
			HandleConfigItem(j["Aimbot"]["aimbotAutoWall"], handle, &Settings::Aimbot::aimbotAutoWall, false);
			HandleConfigItem(j["Aimbot"]["aimbotAutoFire"], handle, &Settings::Aimbot::aimbotAutoFire, false);
			HandleConfigItem(j["Aimbot"]["aimbotMinDmg"], handle, &Settings::Aimbot::aimbotMinDmg, 1.f);
			HandleConfigItem(j["Aimbot"]["aimbotFovEnabled"], handle, &Settings::Aimbot::aimbotFovEnabled, false);
			HandleConfigItem(j["Aimbot"]["drawAimbotFov"], handle, &Settings::Aimbot::drawAimbotFov, false);
			HandleConfigItem(j["Aimbot"]["aimbotSelection"], handle, &Settings::Aimbot::aimbotSelection, NULL);
			HandleConfigItem(j["Aimbot"]["drawAimbotHeadlines"], handle, &Settings::Aimbot::drawAimbotHeadlines, false);
			HandleConfigItem(j["Aimbot"]["aimAtTeammates"], handle, &Settings::Aimbot::aimAtTeammates, false);
			HandleConfigItem(j["Aimbot"]["aimAtFriends"], handle, &Settings::Aimbot::aimAtFriends, false);
			HandleConfigItem(j["Aimbot"]["onlyAimAtFriends"], handle, &Settings::Aimbot::onlyAimAtFriends, false);
			HandleConfigItem(j["Aimbot"]["pistolFastShoot"], handle, &Settings::Aimbot::pistolFastShoot, false);
			HandleConfigItem(j["Aimbot"]["smoothing"], handle, &Settings::Aimbot::smoothing, false);
			// Default raised from 1.f: smoothSteps is now the divisor of the
			// remaining error per tick, and 1 means "no smoothing at all".
			HandleConfigItem(j["Aimbot"]["smoothSteps"], handle, &Settings::Aimbot::smoothSteps, 10.f);
			HandleConfigC(j["Aimbot"]["fovColor"], handle, Settings::Aimbot::fovColor, Color(255, 255, 255));

			HandleConfigItem(j["Misc"]["drawSpectators"], handle, &Settings::Misc::drawSpectators, false);
			HandleConfigItem(j["Misc"]["drawCrosshair"], handle, &Settings::Misc::drawCrosshair, false);
			HandleConfigItem(j["Misc"]["quickStop"], handle, &Settings::Misc::quickStop, false);
			HandleConfigItem(j["Misc"]["killMessage"], handle, &Settings::Misc::killMessage, false);
			HandleConfigItem(j["Misc"]["killMessageOOC"], handle, &Settings::Misc::killMessageOOC, false);
			HandleConfigItem(j["Misc"]["bunnyHop"], handle, &Settings::Misc::bunnyHop, false);
			HandleConfigItem(j["Misc"]["autoStrafe"], handle, &Settings::Misc::autoStrafe, false);
			HandleConfigItem(j["Misc"]["autoStrafeStyle"], handle, &Settings::Misc::autoStrafeStyle, NULL);
			HandleConfigItem(j["Misc"]["fastWalk"], handle, &Settings::Misc::fastWalk, false);
			HandleConfigItem(j["Misc"]["edgeJump"], handle, &Settings::Misc::edgeJump, false);
			HandleConfigItem(j["Misc"]["optiClamp"], handle, &Settings::Misc::optiClamp, false);
			HandleConfigItem(j["Misc"]["optiStrength"], handle, &Settings::Misc::optiStrength, 100.f);
			HandleConfigItem(j["Misc"]["optiStyle"], handle, &Settings::Misc::optiStyle, false);
			HandleConfigItem(j["Misc"]["optiRandomization"], handle, &Settings::Misc::optiRandomization, false);
			HandleConfigItem(j["Misc"]["optiAutoStrafe"], handle, &Settings::Misc::optiAutoStrafe, false);
			HandleConfigItem(j["Misc"]["crosshairSize"], handle, &Settings::Misc::crosshairSize, 5.f);
			HandleConfigItem(j["Misc"]["thirdperson"], handle, &Settings::Misc::thirdperson, false);
			HandleConfigItem(j["Misc"]["thirdpersonKey"], handle, &Settings::Misc::thirdpersonKey, KEY_NONE);
			HandleConfigItem(j["Misc"]["thirdpersonKeyStyle"], handle, &Settings::Misc::thirdpersonKeyStyle, 1);
			HandleConfigItem(j["Misc"]["thirdpersonDistance"], handle, &Settings::Misc::thirdpersonDistance, 20.f);
			HandleConfigItem(j["Misc"]["removeHands"], handle, &Settings::Misc::removeHands, false);
			HandleConfigItem(j["Misc"]["flashlightSpam"], handle, &Settings::Misc::flashlightSpam, false);
			HandleConfigItem(j["Misc"]["useSpam"], handle, &Settings::Misc::useSpam, false);
			HandleConfigItem(j["Misc"]["noRecoil"], handle, &Settings::Misc::noRecoil, false);
			HandleConfigItem(j["Misc"]["noSpread"], handle, &Settings::Misc::noSpread, false);
			HandleConfigItem(j["Misc"]["freeCam"], handle, &Settings::Misc::freeCam, false);
			HandleConfigItem(j["Misc"]["freeCamKey"], handle, &Settings::Misc::freeCamKey, KEY_NONE);
			HandleConfigItem(j["Misc"]["freeCamKeyStyle"], handle, &Settings::Misc::freeCamKeyStyle, 1);
			HandleConfigItem(j["Misc"]["freeCamSpeed"], handle, &Settings::Misc::freeCamSpeed, 1.f);
			HandleConfigItem(j["Misc"]["hitmarkerSoundEnabled"], handle, &Settings::Misc::hitmarkerSoundEnabled, false);
			HandleConfigItem(j["Misc"]["hitmarkerSound"], handle, &Settings::Misc::hitmarkerSound, 0);
			HandleConfigItem(j["Misc"]["damageNotifications"], handle, &Settings::Misc::damageNotifications, false);
			HandleConfigItem(j["Misc"]["hitmarker"], handle, &Settings::Misc::hitmarker, false);
			HandleConfigItem(j["Misc"]["hitmarkerSize"], handle, &Settings::Misc::hitmarkerSize, 10.f);
			HandleConfigItem(j["Misc"]["fakeLag"], handle, &Settings::Misc::fakeLag, false);
			HandleConfigItem(j["Misc"]["fakeLagTicks"], handle, &Settings::Misc::fakeLagTicks, 1.f);
			HandleConfigItem(j["Misc"]["fakeLagKey"], handle, &Settings::Misc::fakeLagKey, KEY_NONE);
			HandleConfigItem(j["Misc"]["fakeLagKeyStyle"], handle, &Settings::Misc::fakeLagKeyStyle, 1);
			HandleConfigItem(j["Misc"]["zoom"], handle, &Settings::Misc::zoom, false);
			HandleConfigItem(j["Misc"]["zoomKey"], handle, &Settings::Misc::zoomKey, KEY_NONE);
			HandleConfigItem(j["Misc"]["zoomKeyStyle"], handle, &Settings::Misc::zoomKeyStyle, 1);
			HandleConfigItem(j["Misc"]["zoomFOV"], handle, &Settings::Misc::zoomFOV, 90.f);
			HandleConfigItem(j["Misc"]["svCheats"], handle, &Settings::Misc::svCheats, false);
			HandleConfigItem(j["Misc"]["svAllowCsLua"], handle, &Settings::Misc::svAllowCsLua, false);
			HandleConfigItem(j["Misc"]["rainbowSpeed"], handle, &Settings::Misc::rainbowSpeed, 1.f);
			HandleConfigItem(j["Misc"]["scriptDumper"], handle, &Settings::Misc::scriptDumper, false);
			HandleConfigC(j["Misc"]["crossHairColor"], handle, Settings::Misc::crossHairColor, Color(255, 255, 255));

			HandleConfigItem(j["Triggerbot"]["triggerBot"], handle, &Settings::Triggerbot::triggerBot, false);
			HandleConfigItem(j["Triggerbot"]["triggerBotHead"], handle, &Settings::Triggerbot::triggerBotHead, false);
			HandleConfigItem(j["Triggerbot"]["triggerBotChest"], handle, &Settings::Triggerbot::triggerBotChest, false);
			HandleConfigItem(j["Triggerbot"]["triggerBotStomach"], handle, &Settings::Triggerbot::triggerBotStomach, false);
			HandleConfigItem(j["Triggerbot"]["triggerbotFastShoot"], handle, &Settings::Triggerbot::triggerbotFastShoot, false);
		}
		catch (const json::exception& e)
		{
			// HandleConfigItem already falls back per field, so reaching here
			// means something structural.  Say so instead of rewriting the
			// user's file and reloading it.
			ConPrint((std::string("Config error: ") + e.what()).c_str(), Color(255, 100, 100));
		}
		// Applies to Load and Reset alike: both can leave a setting outside the
		// domain the rest of the code assumes.
		if (handle != configHandle::Save)
			ValidateSettings();

		if (handle == configHandle::Save)
		{
			// `o.bad()` is a write error on an already-open stream, not a
			// failure to open: an unwritable path sailed past this check and the
			// save silently did nothing.
			std::ofstream o(directory / configName);
			if (!o.is_open())
			{
				ConPrint("Could not write the config file", Color(255, 200, 0));
				return;
			}

			o << std::setw(4) << j << std::endl;
		}
	}

	// This was SEH -- __try / __except(EXCEPTION_EXECUTE_HANDLER) -- wrapped
	// around code whose failures are C++ exceptions.  Three things were wrong
	// with that:
	//
	//   * EXCEPTION_EXECUTE_HANDLER catches *everything*, access violations
	//     included, so a genuine memory bug in the config path was silently
	//     converted into "rewrite the file and try again".
	//   * The recovery path was not itself protected: if the Save or the Load
	//     inside the handler failed in turn, it propagated straight out.
	//   * It needed the split into two functions at all only because __try
	//     cannot appear in a function requiring object unwinding -- which is a
	//     statement about SEH, not about this code.
	//
	// The failures this actually has to survive are json::exception (handled per
	// field above) and filesystem errors (handled through error_code and
	// is_open), so a plain catch is both sufficient and honest.
	void HandleConfig(const char* configName, configHandle handle) {
		try
		{
			_HandleConfig(configName, handle);
		}
		catch (const std::exception& e)
		{
			ConPrint((std::string("Config error: ") + e.what()).c_str(), Color(255, 100, 100));
		}
		catch (...)
		{
			ConPrint("Config error", Color(255, 100, 100));
		}
	}
}
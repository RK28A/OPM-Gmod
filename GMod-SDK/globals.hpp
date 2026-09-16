#pragma once

#define CheatVersion "1.2.2"

#include <map>
#include <mutex>
#include <d3dx9.h>
#include <d3d9.h>

#include "tier1/checksum_crc.h"
#include "engine/vmatrix.h"
#include "lua_shared/CLuaInterface.h"
#include "lua_shared/CLuaShared.h"
#include "client/CClientEntityList.h"
#include "client/CHLClient.h"
#include "client/ClientModeShared.h"
#include "client/C_BasePlayer.h"
#include "engine/CEngineClient.h"
#include "engine/CVRenderView.h"
#include "client/CInputSystem.h"
#include "engine/CModelRender.h"
#include "tier1/KeyValues.h"
#include "engine/CMaterialSystem.h"
#include "client/CViewRender.h"
#include "tier0/Color.h"
#include "tier0/Vector.h"
#include "client/ConVar.h"
#include "client/CUniformRandomStream.h"
#include "engine/CModelInfo.h"
#include "client/CInput.h"
#include "engine/CModelInfo.h"
#include "engine/CIVDebugOverlay.h"
#include "engine/CGameEventManager.h"
#include "vgui/VPanelWrapper.h"
#include "vphysics/CPhysicsSurfaceProps.h"
#include "vguimatsurface/CMatSystemSurface.h"
#include "client/IPrediction.h"
#include "client/IGameMovement.h"
#include "hacks/ConVarSpoofing.h"
#include "core/KeyState.h"

#include <cmath>
#include <math.h>


#ifdef _WIN64
#define ViewRenderOffset 0xC4
#define GlobalVarsOffset 0x94
#define ClientModeOffset 0x0
#define InputOffset 0x0
#define RandomSeedOffset 0x2
#define PresentModule "gameoverlayrenderer64"
#define PresentPattern "\xFF\x15????\x8B\xF8\xEB\x1E"
#define GetClassNamePattern "\xE8????\x4D\x8B\x47\x10"
#define CL_MovePattern "\xE8????\xFF\x15????\xF2\x0F\x10\x0D????\x85\xFF"
#define PredictionSeedPattern "\x48\x8B\xD1\x8B\x0D????"
#define BSendPacketOffset 0x62
#define ConColorMsgDec "?ConColorMsg@@YAXAEBVColor@@PEBDZZ"
#define CClientStateOffset 0x3
#define CClientStateSize 0x7
#define HostNamePattern "\x48\x8D\x15????\x45\x33\xC0\x48\x8B\x01\xFF\x90????\xB8????\x48\x83\xC4\x28\xC3\xCC\xCC\xCC\xCC\xCC\xCC\xCC"
#define MoveHelperPattern "\x48\x89\x05????\xE9????\xCC\xCC\xCC\xCC\xCC\xCC\x48\x83\xEC\x28"
#else
#define ViewRenderOffset 0xA6
#define GlobalVarsOffset 0x59
#define ClientModeOffset 0x5
#define InputOffset 0x5
#define RandomSeedOffset 0x5
#define PresentModule "gameoverlayrenderer"
#define PresentPattern  "\xFF\x15????\x8B\xF0\x85\xFF"
#define GetClassNamePattern "\xE8????\x50\x8B\x43\x08"
#define CL_MovePattern "\xE8????\x83\xC4\x08\xFF\x15????\xDC\x25????"
#define BSendPacketOffset 0x2F
#define ConColorMsgDec "?ConColorMsg@@YAXABVColor@@PBDZZ"
#define CClientStateOffset 0x1
#define CClientStateSize 0x5
#endif

typedef HRESULT(__stdcall* _Present)(IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
typedef bool(__thiscall* _FireEvent)(CGameEventManager*, IGameEvent*);
typedef void(__thiscall* _PaintTraverse)(void*, VPanel*, bool, bool);
typedef const char* (__thiscall* _GetClassName)(C_BasePlayer*);
typedef void(__cdecl* MsgFn)(Color const& color, const char* msg, ...);

CLuaShared* LuaShared;
CLuaInterface* Lua;
CClientEntityList* ClientEntityList;
CHLClient* CHLclient;
ClientModeShared* ClientMode;
CGlobalVarsBase* GlobalVars;
IEngineTrace* EngineTrace;
C_BasePlayer* localPlayer;
CEngineClient *EngineClient;
CViewRender* ViewRender;
CInputSystem* InputSystem;
CModelRender* ModelRender;
CMaterialSystem* MaterialSystem;
CVRenderView* RenderView;
CCvar* CVar;
CUniformRandomStream* UniformRandomStream;
CModelInfo* ModelInfo;
CInput* Input;
CIVDebugOverlay* IVDebugOverlay;
CGameEventManager* GameEventManager;
VPanelWrapper* PanelWrapper;
CPhysicsSurfaceProps* PhysicsSurfaceProps;
CMatSystemSurface* MatSystemSurface;
void* ClientState; // implement that?
CPrediction* Prediction; // implement that?
CGameMovement* GameMovement;
void* EngineVGui;
void* MoveHelper;

_PaintTraverse oPaintTraverse;
_FireEvent oFireEvent;
char* present; // clean that
_Present oPresent;
MsgFn ConColorMsg;

// `text` reaches ConColorMsg as an *argument*, never as the format string.
// It carries Lua error messages and other server-controlled data (see
// PaintTraverse.h), so "%s %n" in it used to be interpreted as a format.
void ConPrint(const char* text, Color col)
{
	if (!ConColorMsg)
		return;

	Color color(153, 204, 255);
	ConColorMsg(color, "%s", "[GMOD-SDK] ");
	ConColorMsg(col, "%s\n", text ? text : "(null)");
}


SpoofedConVar* spoofedAllowCsLua;
SpoofedConVar* spoofedCheats;

struct chamsSetting {
	Color hiddenColor = Color(255, 255, 255, 255);
	Color visibleColor = Color(255, 255, 255, 255);
	int hiddenMaterial = 0;
	int visibleMaterial = 0;
	chamsSetting(){
		hiddenColor = Color(255, 255, 255, 255);
		visibleColor = Color(255, 255, 255, 255);
		hiddenMaterial = 0;
		visibleMaterial = 0;
	}
	chamsSetting(Color hiddenCol, Color visibleCol, int hiddenmaterial, int visiblematerial)
	{
		hiddenColor = hiddenCol;
		visibleColor = visibleCol;
		hiddenMaterial = hiddenmaterial;
		visibleMaterial = visiblematerial;
	}
};
#define ColorToRGBA(x) D3DCOLOR_ARGB((uint8_t)(x.fCol[3] * 255), (uint8_t)(x.fCol[0] * 255), (uint8_t)(x.fCol[1] * 255), (uint8_t)(x.fCol[2] * 255))
namespace Globals {
	bool openMenu = false;
	bool nothing;
	bool Untrusted;
	CUserCmd lastCmd;
	CUserCmd lastEndCmd;
	CUserCmd lastRealCmd;
	CUserCmd lastNetworkedCmd;
	matrix3x4_t lastMatrix[128];
	bool choke;
	VPanel* lastPanelIdentifier;

	SpoofedConVar* spoofedAllowCsLua;
	SpoofedConVar* spoofedCheats;

	std::atomic<vmatrix_t> viewMatr;
	std::atomic<std::pair<bool, LPCSTR>> waitingToBeExecuted;
	int executeState = 0;

	int screenWidth, screenHeight;

	bool* bSendpacket;
	unsigned int* predictionRandomSeed;
	char* hostName; // UTF-8 encoding C7 05 ? ? ? ? ? ? ? ? E8 ? ? ? ? 59 C3 CC CC CC CC CC CC CC CC CC CC 68 ? ? ? ?

	HWND window;
	WNDPROC oWndProc;
}
namespace Settings {
	ButtonCode_t menuKey = KEY_INSERT;
	int menuKeyStyle = 1;
	Color menuColor(0, 255, 0);

	std::map<C_BasePlayer*, std::pair<bool, int>> friendList;
	std::vector<C_BasePlayer*> selectedFriendList;

	std::map<std::string, bool> luaEntList;
	std::vector<std::string> selectedLuaEntList;

	float lastHitmarkerTime = -1.f;
	std::mutex friendListMutex;
	std::mutex luaEntListMutex;
	char ScriptInput[131070];

	bool currentlyInFreeCam;
	namespace Chams {
		chamsSetting playerChamsSettings;

		chamsSetting teamMateSettings;

		chamsSetting ragdollChamsSettings;
		chamsSetting weaponChamsSettings;

		chamsSetting npcChamsSettings;

		chamsSetting armChamsSettings;

		chamsSetting localPlayerChamsSettings;
		chamsSetting netLocalChamsSettings;

	}
	namespace ESP {
		int infosEmplacement;
		bool espDormant;
		bool espBoundingBox;
		Color espBoundingBoxColor(255, 255, 255);
		bool espHealthBar;
		Color espHealthColor(255, 255, 255);
		bool espName;
		Color espNameColor(255, 255, 255);
		bool weaponText;
		Color espWeaponColor(255, 255, 255);
		bool weaponAmmo;
		Color espAmmoColor(255, 255, 255);
		bool espDistance;
		Color espDistanceColor(255, 255, 255);
		bool skeletonEsp;		
		bool skeletonDetails;
		Color skeletonEspColor(255, 255, 255);
		int espShapeInt = 0;

		bool entEsp = false;

		bool onlyFriends = false;

		// --- Admin ESP -----------------------------------------------------
		// A separate module (AdminEsp.h), not part of the player ESP above:
		// highlights server staff (Player:IsAdmin() via Lua) with their name
		// and a box, drawn through walls like the rest of the ESP.  Its own
		// toggles and its own colour so nothing here touches the classic path.
		bool adminEsp = false;
		bool adminEspName = true;
		bool adminEspBox = true;
		Color adminEspColor(255, 140, 0); // orange, distinct from the white ESP default

	}
	namespace Visuals {
		float fov = 130.f;
		bool fovEnabled = false;

		// The value the user configured.  Kept separate from the engine's own
		// view model FOV below: upstream used viewModelFOV == -1.f as both the
		// "not captured yet" sentinel and the setting, so once the default
		// became 90.f the capture never ran again.
		float viewModelFov = 90.f;
		bool viewModelFovEnabled = false;

		// The engine's native view model FOV, captured on the first RenderView
		// and restored when the option is switched off.
		float originalViewModelFov = 0.f;
		bool hasOriginalViewModelFov = false;

		// Shared by the slider, the config loader and the RenderView hook, so a
		// value coming from a file is bounded the same way the UI bounds it.
		inline constexpr float kMinFov = 30.f;
		inline constexpr float kMaxFov = 150.f;

		[[nodiscard]] inline float ClampFov(float fov) noexcept
		{
			if (!std::isfinite(fov))
				return kMaxFov;

			return (fov < kMinFov) ? kMinFov : ((fov > kMaxFov) ? kMaxFov : fov);
		}

		bool noVisualRecoil = false;
		Color worldColor(17.f, 33.f, 71.f, 255.f);
		bool changeWorldColor = false;
		bool fullBright = false;
		bool disableSkyBox = false;

	}
	namespace AntiAim {
		int currentAntiAimPitch = 0;
		int currentAntiAimYaw = 0;
		bool enableAntiAim;
		ButtonCode_t antiAimKey = KEY_NONE;
		int antiAimKeyStyle = 1; // KEY_NONE;

		// Toggle latch for the binding above.  One per *feature*: see PollKey()
		// in hacks/Utils.h for why the getKeyState macro's per-expansion
		// statics were a bug rather than an implementation detail.
		input::KeyState antiAimKeyState;

		float fakePitch;
	}
	namespace Aimbot {
		float aimbotFOV = 5.f;
		bool silentAim = false;
		bool lockOnTarget = false;
		ButtonCode_t aimbotKey = KEY_NONE;
		int aimbotKeyStyle = 1;
		input::KeyState aimbotKeyState;
		bool enableAimbot = false;
		int aimbotHitbox = 0;
		bool aimbotAutoWall = false;
		bool aimbotAutoFire = false;
		float aimbotMinDmg = 1.f;
		bool aimbotFovEnabled = false;
		bool drawAimbotFov = false;

		// Domains for the values the config loader has to bound.  They live
		// here rather than next to the tables they index so ConfigSystem.h can
		// validate without depending on Misc.h / Utils.h include order.
		inline constexpr int kHitboxCount = 3;    // see IntToBoneName()
		inline constexpr int kSelectionCount = 3; // distance / health / fov

		int aimbotSelection = 0;
		bool drawAimbotHeadlines = false;
		bool aimAtTeammates = false;

		// nullptr means "no target".  Never localPlayer -- that sentinel forced
		// every consumer to know about it (see LegitAim.h).
		C_BasePlayer* finalTarget = nullptr;

		bool aimAtFriends = false;
		bool onlyAimAtFriends = false;

		bool pistolFastShoot = false;

		bool smoothing = false;

		// Divisor of the *remaining* angular error consumed per tick, so a
		// larger value is slower.  It is not a count of ticks to completion:
		// the approach is exponential and only tends towards the target.
		// AngleMath::SmoothingFactor() is what turns it into a usable fraction.
		float smoothSteps = 10.f;

		Color fovColor(255, 255, 255);

	}
	namespace Misc {
		bool scriptDumper;

		bool drawSpectators;

		Color crossHairColor(255, 255, 255);
		bool drawCrosshair;

		bool quickStop;

		bool killMessage;
		bool killMessageOOC;

		bool bunnyHop;
		bool autoStrafe;
		int autoStrafeStyle;

		bool fastWalk;
		bool edgeJump;

		bool optiClamp;
		float optiStrength;
		bool optiStyle;
		bool optiRandomization;
		bool optiAutoStrafe;

		float crosshairSize;
		
		bool thirdperson;
		ButtonCode_t thirdpersonKey = KEY_NONE;
		int thirdpersonKeyStyle = 1;

		// Read from two hooks -- FrameStageNotify (whether to rewrite the local
		// view angles) and RenderView (whether to move the camera).  They have
		// to share one latch or, in toggle mode, they answer differently on the
		// same frame and the feature half-applies.  The getKeyState macro gave
		// each call site its own statics, which is exactly that bug.
		input::KeyState thirdpersonKeyState;

		float thirdpersonDistance;

		bool removeHands;

		bool flashlightSpam;
		bool useSpam;

		bool noRecoil;
		bool noSpread;

		bool freeCam;
		ButtonCode_t freeCamKey = KEY_NONE;
		int freeCamKeyStyle = 1;

		// Shared for the same reason as thirdpersonKeyState above.
		input::KeyState freeCamKeyState;

		float freeCamSpeed;

		bool hitmarkerSoundEnabled = false;

		inline constexpr int kHitmarkerSoundCount = 2; // see hitMarkers[] in Misc.h
		int hitmarkerSound = 0;

		// Raise a toast for each hit the local player lands.
		bool damageNotifications = false;
		bool hitmarker;
		float hitmarkerSize = 10.f;

		bool fakeLag;
		float fakeLagTicks;
		ButtonCode_t fakeLagKey = KEY_NONE;
		int fakeLagKeyStyle = 1;
		input::KeyState fakeLagKeyState;

		bool zoom;
		ButtonCode_t zoomKey = KEY_NONE;
		int zoomKeyStyle = 1;
		input::KeyState zoomKeyState;
		float zoomFOV = 90.f;

		bool svCheats;
		bool svAllowCsLua;

		float rainbowSpeed = 1.f;

	}
	namespace Triggerbot {
		bool triggerBot;
		bool triggerBotHead;
		bool triggerBotChest;
		bool triggerBotStomach;
		bool triggerbotFastShoot;
	}
}
void rainbowColor(Color& col, float speed) noexcept
{
	if (!col.rainbow)return;
	col.fCol[0] = std::sin(speed * GlobalVars->realtime) * 0.5f + 0.5f;
	col.fCol[1] = std::sin(speed * GlobalVars->realtime + 2 * PI / 3) * 0.5f + 0.5f;
	col.fCol[2] = std::sin(speed * GlobalVars->realtime + 4 * PI / 3) * 0.5f + 0.5f;
}
// Make sure to add everything to ConfigSystem.h too! both ResetConfig, LoadConfig, and SaveConfig!!
std::wstring s2ws(const std::string& str)
{
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), nullptr, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), &wstrTo[0], size_needed);
	return wstrTo;
}

std::string ws2s(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &strTo[0], size_needed, nullptr, nullptr);
	return strTo;
}

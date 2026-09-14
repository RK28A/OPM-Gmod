#pragma once

#include "../tier0/Vector.h"
#include "../globals.hpp"
#include "../ImGui/imgui.h"
// Relative like every other include in this directory.  The bare
// "ImGui/imgui-notify/..." form only resolved through the Visual Studio include
// directories and broke as soon as the project was built another way.
#include "../ImGui/imgui-notify/imgui_notify.h"

#include <array>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>

inline constexpr std::array<const char*, 8> killMessages{
    "Hah, you died!",
    "too bad you're dead",
    "lmao you ded",
    "i feel bad for you lmao",
    "get yourself more skills",
    "ez noob",
    "ezzzzz",
    "you got outplayed by GMOD-SDK",
};

inline constexpr std::array<const char*, 2> hitMarkers{
    "physics/metal/metal_solid_impact_bullet2.wav",
    "training/timer_bell.wav",
};

// The config loader bounds Settings::Misc::hitmarkerSound against this count
// without being able to see the table itself; keep the two in step.
static_assert(hitMarkers.size() == static_cast<std::size_t>(Settings::Misc::kHitmarkerSoundCount),
    "Settings::Misc::kHitmarkerSoundCount is out of sync with hitMarkers[]");

namespace EventUtils
{
    // Player names come straight off the wire.  Cap them well below the toast
    // limit so one very long nickname cannot push the rest of the line away.
    inline constexpr std::size_t kMaxPlayerNameLength = 32;

    // rand() % n is biased and shares global state with anything else in the
    // process that seeds it; <random> costs nothing here.
    [[nodiscard]] inline std::size_t RandomIndex(std::size_t count)
    {
        if (count == 0)
            return 0;

        static std::mt19937 engine{ std::random_device{}() };
        std::uniform_int_distribution<std::size_t> distribution(0, count - 1);
        return distribution(engine);
    }

    [[nodiscard]] inline bool IsValidPlayerIndex(int index)
    {
        if (!EngineClient || index <= 0)
            return false;

        const int maxClients = EngineClient->GetMaxClients();
        return maxClients > 0 && index <= maxClients;
    }

    // Returns the player's sanitised name, or an empty string when the slot does
    // not resolve to a real player.
    //
    // GetPlayerInfo()'s return value used to be discarded and player_info_s left
    // uninitialised, so a failed lookup meant strlen() walked whatever happened
    // to be on the stack.
    [[nodiscard]] inline std::string GetPlayerName(int index)
    {
        if (!IsValidPlayerIndex(index))
            return std::string();

        player_info_s info{};
        if (!EngineClient->GetPlayerInfo(index, &info))
            return std::string();

        // Do not assume the engine terminated the buffer for us.
        info.name[sizeof(info.name) - 1] = '\0';

        return notify::Sanitize(info.name, kMaxPlayerNameLength);
    }
} // namespace EventUtils

// https://wiki.facepunch.com/gmod/Game_Events
//
// `public` inheritance is load-bearing: `class DamageEvent : IGameEventListener2`
// is private inheritance, which is why registration needed a C cast to paper
// over the inaccessible base.
class DamageEvent : public IGameEventListener2
{
public:
    DamageEvent() = default;
    ~DamageEvent() override = default;

    void FireGameEvent(IGameEvent* event) override
    {
        if (!event || !EngineClient)
            return;

        const int localPlayerID = EngineClient->GetLocalPlayer();
        const int target = EngineClient->GetPlayerForUserID(event->GetInt("userid"));
        const int attacker = EngineClient->GetPlayerForUserID(event->GetInt("attacker"));

        // player_hurt fires for every player on the server.  Upstream raised a
        // toast for all of them before this filter, so a busy server opened a
        // notification window per hit between two strangers.
        const bool localPlayerDealtDamage = (attacker == localPlayerID && target != localPlayerID);
        if (!localPlayerDealtDamage)
            return;

        if (Settings::Misc::damageNotifications)
        {
            const std::string attackerName = EventUtils::GetPlayerName(attacker);
            const std::string targetName = EventUtils::GetPlayerName(target);

            if (!attackerName.empty() && !targetName.empty())
            {
                const std::string message = notify::Format("%s attacked %s. NEW HP: %i",
                    attackerName.c_str(), targetName.c_str(), event->GetInt("health"));

                std::cout << message << std::endl;

                notify::Toast toast(notify::Type::Info, 3000);
                toast.SetTitle("Damage");
                // SetContent and not SetContentFormat: `message` embeds a player
                // nickname, and a nickname must never be read as a printf format.
                toast.SetContent(message.c_str());
                ImGui::InsertNotification(std::move(toast));
            }
        }

        if (Settings::Misc::hitmarkerSoundEnabled && MatSystemSurface)
        {
            // A hand-edited config could point this anywhere.
            const int soundIndex = Settings::Misc::hitmarkerSound;
            if (soundIndex >= 0 && static_cast<std::size_t>(soundIndex) < hitMarkers.size())
                MatSystemSurface->PlaySound(hitMarkers[static_cast<std::size_t>(soundIndex)]);
        }

        Settings::lastHitmarkerTime = EngineClient->Time();
    }
};

class DeathEvent : public IGameEventListener2
{
public:
    DeathEvent() = default;
    ~DeathEvent() override = default;

    void FireGameEvent(IGameEvent* event) override
    {
        if (!event || !EngineClient)
            return;

        const int localPlayerID = EngineClient->GetLocalPlayer();
        const int target = event->GetInt("entindex_killed");
        const int attacker = event->GetInt("entindex_attacker");

        if (target == localPlayerID || attacker != localPlayerID)
            return;

        if (Settings::Misc::killMessage)
        {
            std::string command = "say \"";
            if (Settings::Misc::killMessageOOC)
                command += "/ooc ";
            command += killMessages[EventUtils::RandomIndex(killMessages.size())];
            command += "\"";
            EngineClient->ClientCmd_Unrestricted(command.c_str());
        }
    }
};

namespace GameEvents
{
    // Owning handles.  Upstream stored these as raw `new` results in two
    // `void*` globals: no type, no ownership, and no way to take them back off
    // the engine before the module went away.
    inline std::unique_ptr<DamageEvent> damageEvent;
    inline std::unique_ptr<DeathEvent> deathEvent;

    // The engine keeps the raw pointers we hand to AddListener(), so they have
    // to be removed before the objects die -- otherwise the next event calls
    // into freed memory.
    inline void Unregister()
    {
        if (GameEventManager)
        {
            if (damageEvent)
                GameEventManager->RemoveListener(damageEvent.get());
            if (deathEvent)
                GameEventManager->RemoveListener(deathEvent.get());
        }

        damageEvent.reset();
        deathEvent.reset();
    }

    // Idempotent: if Main() ever runs twice, the engine must not end up with two
    // copies of each listener firing the same notification.
    inline bool Register()
    {
        if (!GameEventManager)
        {
            ConPrint("GameEventManager unavailable: game events disabled", Color(255, 0, 0));
            return false;
        }

        if (damageEvent || deathEvent)
            return true;

        damageEvent = std::make_unique<DamageEvent>();
        deathEvent = std::make_unique<DeathEvent>();

        GameEventManager->AddListener(damageEvent.get(), "player_hurt", false);
        GameEventManager->AddListener(deathEvent.get(), "entity_killed", false);

        // AddListener is typed void* in this vtable, so confirm through
        // FindListener instead of assuming it worked.
        const bool damageRegistered = GameEventManager->FindListener(damageEvent.get(), "player_hurt") != nullptr;
        const bool deathRegistered = GameEventManager->FindListener(deathEvent.get(), "entity_killed") != nullptr;

        if (!damageRegistered || !deathRegistered)
            ConPrint("Warning: a game event listener may not have registered", Color(255, 200, 0));

        return damageRegistered && deathRegistered;
    }
} // namespace GameEvents

void ThirdPerson(CViewSetup& view)
{
    trace_t trace;
    Ray_t ray;
    CTraceFilter filter;
    filter.pSkip = localPlayer;
    ray.Init(view.origin, view.origin + ((Globals::lastCmd.viewangles.toVector() * -1) * Settings::Misc::thirdpersonDistance));
    EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);

    view.origin = trace.endpos;
}
void FreeCam(CViewSetup &view, Vector& camPos) 
{
    if (camPos == Vector(0, 0, 0))
        camPos = view.origin;

    float speed = Settings::Misc::freeCamSpeed;

    if (Globals::lastCmd.buttons & IN_SPEED)
        speed *= 5.f;
    if (Globals::lastCmd.buttons & IN_DUCK)
        speed *= 0.5f;
    if (Globals::lastCmd.buttons & IN_JUMP)
        camPos.z += speed;

    if (Globals::lastCmd.buttons & IN_FORWARD)
        camPos += (view.angles.toVector() * speed);

    if (Globals::lastCmd.buttons & IN_BACK)
        camPos -= (view.angles.toVector() * speed);

    if (Globals::lastCmd.buttons & IN_MOVELEFT)
        camPos += (view.angles.SideVector() * speed);

    if (Globals::lastCmd.buttons & IN_MOVERIGHT)
        camPos -= (view.angles.SideVector() * speed);

    view.origin = camPos;
}
void SpectatorList() 
{
    if (!Settings::Misc::drawSpectators || !localPlayer || !localPlayer->IsAlive())
        return;

    ImGui::SetNextWindowSize(ImVec2(200.f, 200.f));
    ImGui::BeginMenuBackground("Spectators window", &Globals::openMenu, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);
    {
        ImGui::ColorBar("rainbowBar3", ImVec2(648.f, 2.f));
        std::string names = "";
        for (int i = 0; i < ClientEntityList->GetHighestEntityIndex(); i++)
        {
            C_BasePlayer* entity = (C_BasePlayer*)ClientEntityList->GetClientEntity(i);
            if (entity == nullptr || !entity->IsPlayer() || entity == localPlayer) // https://wiki.facepunch.com/gmod/Enums/TEAM
                continue;
            if (entity->GetObserverTarget() != localPlayer)
                continue;

            player_info_s info;
            EngineClient->GetPlayerInfo(i, &info);

            names += std::string(info.name) + "\n";
        }
        ImGui::GetStyle().ItemSpacing = ImVec2(4, 2);
        ImGui::GetStyle().WindowPadding = ImVec2(4, 4);
        ImGui::SameLine(15.f);
        ImGui::Text(names.c_str());

    }
    ImGui::End();
}
int flagsPrePred = 0;
void QuickStop(CUserCmd* cmd)
{
	if (!Settings::Misc::quickStop || cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT) || !(localPlayer->getFlags() & FL_ONGROUND))
		return;

    Vector velocity = localPlayer->getVelocity();
    float speed = velocity.Length2D();
    if (speed < 15.0f)
        return;

    QAngle direction = velocity.toAngle();
    direction.y = cmd->viewangles.y - direction.y;

    const auto negatedDirection = direction.toVector() * -speed;
    cmd->forwardmove = negatedDirection.x;
    cmd->sidemove = negatedDirection.y;
}
void FlashSpam(CUserCmd* cmd)
{
    if (Settings::Misc::flashlightSpam && InputSystem->IsButtonDown(KEY_F) && !MatSystemSurface->IsCursorVisible())
        cmd->impulse = 100; // FlashLight spam
}
void UseSpam(CUserCmd* cmd)
{
    if (Settings::Misc::useSpam && InputSystem->IsButtonDown(KEY_E) && !MatSystemSurface->IsCursorVisible())
    {
        if(cmd->command_number % 2)
        cmd->buttons |= IN_USE;
        else cmd->buttons &= ~IN_USE;
    }
}
void BunnyHopOptimizer(CUserCmd* cmd)
{
    /*float dStack92 = cmd->viewangles.y;
    float dStack76 = 0.f;
    dStack76 = (dStack76 * 180.f) / M_PI;

    float dStack84 = dStack76 - dStack92;
    float dStack68 = dStack84 + 180.f;
    float dVar17 = dStack68 - 180.f;
    if (180.f <= dVar17)
    {
        dVar17 = dVar17 - 360.f;
    }
    if (dVar17 < 24.f)
    {
        dVar17 = dStack76;
        float dVar18 = dStack92;
        if (180.f < dStack84)
        {
            dVar18 = cmd->viewangles.y;
            if (dStack76 <= dVar18)
                dVar17 = dStack76 + 360.f;
            else dVar18 = dVar18 - 360.f;
        }
        dStack68 = (dVar17 - dVar18) + dVar18 + 180.f;
        cmd->viewangles.y = (dStack68 - 180.f);
    }

    return;

    */
    if (!(localPlayer->getFlags() & FL_ONGROUND))
    {
        cmd->sidemove = -10000.f;
        cmd->forwardmove = 0.f;
        cmd->mousedx = -1.f;
    }
    QAngle viewAngles = cmd->viewangles;
    static QAngle lastViewAng = viewAngles;
    Vector absVel = localPlayer->getVelocity();
    absVel.z = 0;

    if((cmd->sidemove > 0.f && cmd->mousedx > 0.f) || (cmd->sidemove < 0.f && cmd->mousedx < 0.f))
    if ((InputSystem->IsButtonDown(KEY_SPACE) && !(localPlayer->getFlags() & FL_ONGROUND)) && localPlayer->getVelocity().Length() > 50.f && cmd->sidemove != 0.f && absVel.Length() != 0)
    {
        float tickrate = (1.f / GlobalVars->interval_per_tick);
        float strafes = (1.f / GlobalVars->frametime) / tickrate; // framerate / tickrate
        QAngle currVelAng = localPlayer->getVelocity().toAngle();
        QAngle angDiff = (currVelAng - lastViewAng).FixAngles();
        std::cout << "old angDiff: " << angDiff.y << " : absVel: " << absVel.x << " : "<< absVel.y << " : " << absVel.Length() << std::endl;
        angDiff.y = RAD2DEG(asin(32.8f / absVel.Length()));// * 57.29577951308;
        std::cout << "new angDiff: " << angDiff.y << std::endl;
        std::cout << strafes << std::endl;
        if (Settings::Misc::optiRandomization)
            angDiff.y *= (0.6 + (float)(rand()) / ((float)(RAND_MAX / (1.4 - 0.6)))); // Randomization for anticheats

        viewAngles.y += (angDiff.y * (Settings::Misc::optiStrength/100.f));
        viewAngles.y = lastViewAng.y + (angDiff.y * (Settings::Misc::optiStrength / 100.f) / strafes);
        viewAngles.FixAngles();
        //viewAngles = currVelAng;

        cmd->viewangles = viewAngles;
        EngineClient->SetViewAngles(viewAngles);
    }
    //lastViewAng = localPlayer->getVelocity().toAngle();
    lastViewAng = viewAngles;

    return;

    static QAngle previousAngles = cmd->viewangles;
    if (InputSystem->IsButtonDown(KEY_SPACE) && !MatSystemSurface->IsCursorVisible()) {
        float tickrate = (1.f / GlobalVars->interval_per_tick);
        float strafes = (1.f / GlobalVars->frametime) / tickrate; // framerate / tickrate

        auto currVel = localPlayer->getVelocity();
        currVel.z = 0;
        float A = RAD2DEG(atan(32.8f / currVel.Length())); // difference of angle to the next tick's optimal strafe angle
        float D = (0.75* A) / strafes;// optimal number of degrees per strafe given the desired number of strafes per jump, the tickrate of the server, and the current player velocity defined in v_1

        QAngle viewAngles;
        EngineClient->GetViewAngles(viewAngles);
        viewAngles.FixAngles();
        if (currVel.Length())
        {
            float angDiff = 0.f;
            if (!Settings::Misc::optiStyle)
            {
                if (cmd->mousedx < 0.f && (cmd->sidemove < 0.f))
                angDiff = (viewAngles.y) - (previousAngles.y);
                else if (cmd->mousedx > 0.f && (cmd->sidemove > 0.f))
                    angDiff = (previousAngles.y) - (viewAngles.y);
                while (angDiff < 0) angDiff += 360.f;

                if (angDiff < D)
                    angDiff += ((D - angDiff) * (Settings::Misc::optiStrength / 100));
                else if (Settings::Misc::optiClamp) angDiff = D;
            }
            else angDiff = D * (Settings::Misc::optiStrength / 100);

            if(Settings::Misc::optiRandomization)
            angDiff += (0.05 + (float)(rand()) / ((float)(RAND_MAX / (0.1 - 0.05)))); // Randomization for anticheats

            if (!(localPlayer->getFlags() & FL_ONGROUND)) {
                if (cmd->mousedx < 0)
                {
                    cmd->sidemove = -10000.f;
                    cmd->buttons |= IN_MOVELEFT;
                }
                else if (cmd->mousedx > 0) {
                    cmd->sidemove = 10000.f;
                    cmd->buttons |= IN_MOVERIGHT;
                }
            }

            if (cmd->mousedx < 0.f && (cmd->sidemove < 0.f)) { // Left
                viewAngles.y = (previousAngles.y + angDiff);
            }
            else if (cmd->mousedx > 0.f && (cmd->sidemove > 0.f)) { // Right
                viewAngles.y = (previousAngles.y - angDiff);
                angDiff = -angDiff;
            }

            cmd->mousedx = angDiff;
            viewAngles.FixAngles();
            cmd->viewangles = viewAngles;
            EngineClient->SetViewAngles(viewAngles);
        }
    }
    previousAngles = cmd->viewangles;
}
void BunnyHop(CUserCmd* cmd)
{
    int flags = localPlayer->getFlags();
    if (false && Settings::Misc::fastWalk && flags & FL_ONGROUND && (cmd->forwardmove != 0.f && cmd->sidemove == 0.f)) // Fastwalk
    {
        if (cmd->command_number % 2 == 0)
            cmd->sidemove = -5000.f;
        else cmd->sidemove = 5000.f;
    }
    if (InputSystem->IsButtonDown(KEY_SPACE) && !MatSystemSurface->IsCursorVisible() && localPlayer->getMoveType() != MOVETYPE_NOCLIP) {
        if (Settings::Misc::bunnyHop)
        {
            if (!(flagsPrePred & FL_ONGROUND))
                cmd->buttons &= ~IN_JUMP;
            else
                cmd->buttons |= IN_JUMP;
        }
        if ((Settings::Misc::autoStrafe || Settings::Misc::optiAutoStrafe) && !(flags & FL_ONGROUND))
        {
            if (Settings::Misc::autoStrafeStyle == 0 || (Settings::Misc::optiAutoStrafe && Settings::Misc::autoStrafeStyle == 2)) // Legit
            {
                if (!(flags & FL_ONGROUND)) {
                    if (cmd->mousedx > 0.f)
                        cmd->sidemove = 10000.f;
                    else if (cmd->mousedx < 0.f) cmd->sidemove = -10000.f;
                }
            }
            else if (Settings::Misc::autoStrafeStyle == 1) { // Silent-strafe
                if (cmd->mousedx == 0.f)
                {
                    cmd->viewangles.y += (cmd->command_number % 2) ? 1.f : -1.f;
                    cmd->sidemove = (cmd->command_number % 2) ? 10000.f : -10000.f;

                    /*QAngle absVelAng = localPlayer->getVelocity().toAngle();

                    cmd->viewangles.y = absVelAng.y;
                    cmd->viewangles.FixAngles();*/

                }
                else  cmd->sidemove = cmd->mousedx < 0 ? -10000.f : 10000.f;
                if (cmd->sidemove > 0)
                    cmd->buttons |= IN_MOVELEFT;
                else if (cmd->sidemove < 0)cmd->buttons |= IN_MOVERIGHT;

                cmd->viewangles.FixAngles();
            }
        }

        if (!(flags & FL_ONGROUND))
            cmd->buttons &= ~IN_SPEED;
    }

    if (Settings::Misc::autoStrafeStyle == 2) { // Optimizer
        //BunnyHopOptimizer(cmd);
    }
}


void PrePredOptimizer(CUserCmd* cmd)
{
    if (localPlayer->getMoveType() == MOVETYPE_LADDER || localPlayer->getMoveType() == MOVETYPE_NOCLIP)
        return;

    flagsPrePred = localPlayer->getFlags();

    if (!(InputSystem->IsButtonDown(KEY_SPACE) || cmd->buttons & IN_JUMP) || localPlayer->getFlags() & FL_ONGROUND)
        return;
    cmd->sidemove = (cmd->command_number % 2) ? 10000.f : -10000.f;
    cmd->forwardmove = 0.f;

}
void PostPredOptimizer(CUserCmd* cmd)
{
    if (localPlayer->getMoveType() == MOVETYPE_LADDER || localPlayer->getMoveType() == MOVETYPE_NOCLIP)
        return;

    if (!(flagsPrePred & FL_ONGROUND) && localPlayer->getFlags() & FL_ONGROUND)
    {
        cmd->buttons &= ~IN_DUCK;
        cmd->buttons |= IN_JUMP;
    }
    else if(!(localPlayer->getFlags() & FL_ONGROUND)/* && !(flagsPrePred & FL_ONGROUND)*/)
        cmd->buttons |= IN_DUCK;

    //if ((cmd->sidemove > 0.f && cmd->mousedx > 0.f) || (cmd->sidemove < 0.f && cmd->mousedx < 0.f))
    if ((InputSystem->IsButtonDown(KEY_SPACE) && !(localPlayer->getFlags() & FL_ONGROUND)) && localPlayer->getVelocity().Length() > 50.f && cmd->sidemove != 0.f)
    {
        QAngle viewAngles = cmd->viewangles;
        QAngle currVelAng = localPlayer->getVelocity().toAngle();
        viewAngles.y = currVelAng.y;
        viewAngles.FixAngles();
        //viewAngles = currVelAng;

        cmd->viewangles = viewAngles;
        EngineClient->SetViewAngles(viewAngles);
    }
}

void PrePrediction(CUserCmd* cmd)
{
    if (Settings::Misc::autoStrafeStyle == 2)
        PrePredOptimizer(cmd);

    flagsPrePred = localPlayer->getFlags();
}
void PostPrediction(CUserCmd* cmd)
{
    if (Settings::Misc::autoStrafeStyle == 2)
        PostPredOptimizer(cmd);

    if (!Settings::Misc::edgeJump) return;
    int flags = localPlayer->getFlags();
    if (localPlayer->getMoveType() == MOVETYPE_LADDER || localPlayer->getMoveType() == MOVETYPE_NOCLIP)
        return;
    if (flagsPrePred & FL_ONGROUND && !(flags & FL_ONGROUND))
        cmd->buttons |= IN_JUMP;

    if ( false && cmd->buttons & IN_JUMP) // Crouchboost
    {
        if(!(flagsPrePred & FL_ONGROUND))
            cmd->buttons |= IN_DUCK;

        if (!(flagsPrePred & FL_ONGROUND) && flags & FL_ONGROUND && cmd->buttons & IN_DUCK)
            cmd->buttons &= ~IN_DUCK;
    }
}

void DoMisc(CUserCmd* cmd)
{
    if (localPlayer->getMoveType() == MOVETYPE_NOCLIP || localPlayer->getMoveType() == MOVETYPE_LADDER)
        return;
    QuickStop(cmd);
    FlashSpam(cmd);
    UseSpam(cmd);
    BunnyHop(cmd);
}
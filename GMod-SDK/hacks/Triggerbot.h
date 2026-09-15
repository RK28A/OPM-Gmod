#pragma once

#include "../globals.hpp"
#include "Utils.h"
#include "AutoWall.h"

// Runs from hkCreateMove (main thread). Traces a ray from the eye along the
// current command's view angles; if it lands on a live enemy player on an
// enabled hitgroup, it presses attack for this command. It never touches Lua
// and never removes entities, so it cannot raise the engine's
// "!ThreadInMainThread" / "EntityRemoved" Lua errors.
void TriggerBot(CUserCmd* cmd)
{
    if (!cmd || !Settings::Triggerbot::triggerBot || !localPlayer || !EngineTrace)
        return;

    if (!localPlayer->IsAlive())
        return;

    const Vector start = localPlayer->EyePosition();
    const Vector end = start + cmd->viewangles.toVector() * 8192.0f;

    Ray_t ray;
    ray.Init(start, end);

    CTraceFilter filter;
    filter.pSkip = localPlayer;

    trace_t trace{};
    EngineTrace->TraceRay(ray, MASK_SHOT, &filter, &trace);

    auto* target = static_cast<C_BasePlayer*>(trace.m_pEnt);

    if (!target ||
        target == localPlayer ||
        !target->IsPlayer() ||
        !target->IsAlive())
    {
        return;
    }

    if (!Settings::Aimbot::aimAtTeammates && target->InLocalTeam())
        return;

    bool validHitgroup = false;

    switch (trace.hitgroup)
    {
    case HITGROUP_HEAD:
        validHitgroup = Settings::Triggerbot::triggerBotHead;
        break;

    case HITGROUP_CHEST:
        validHitgroup = Settings::Triggerbot::triggerBotChest;
        break;

    case HITGROUP_STOMACH:
        validHitgroup = Settings::Triggerbot::triggerBotStomach;
        break;

    default:
        break;
    }

    if (!validHitgroup)
        return;

    // Fast-shoot alternates attack every other command so semi-automatic
    // weapons actually re-fire instead of the button staying held down.
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

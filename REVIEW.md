# PR #64 review — what was fixed, and what was not

Upstream pull request:
[Gaztoof/GMod-SDK#64](https://github.com/Gaztoof/GMod-SDK/pull/64) (head
`67e6e85`, imported here as the baseline commit).

The review raised 200 numbered points. This file says where each one landed.
Item numbers in brackets refer to that list.

The work is four commits, so the diff reads as a review rather than one "fix
bugs" blob:

| Commit | Scope |
| --- | --- |
| `chore: import GMod-SDK (PR #64 head) as the project baseline` | The unmodified PR head, so everything after it is exactly the review |
| `fix: memory safety and format strings` | Undefined behaviour and externally-controlled input |
| `fix: target and event state validation` | Pointer lifetime, bounds, null checks, angle maths |
| `refactor: notification ownership, config and GUI plumbing` | Ownership, config round-trip, menu, build flags |
| `test: add unit tests, CI and static analysis config` | 24 tests, four-configuration CI, clang-tidy |

## Merge blockers [200] — all addressed

| Blocker | Where |
| --- | --- |
| Dangling `get_color()` / `get_phase()` references | `notify_core.h` — return by value |
| Format strings | `notify_core.h`, `imgui_notify.h`, `Misc.h`, `globals.hpp` |
| `GetPlayerInfo()` validation | `Misc.h` `EventUtils::GetPlayerName()`, `ESP.h` |
| `erase()` in the render loop | `notify_core.h` `Queue::CollectLive()` |
| Uninitialised `finalPos` | `LegitAim.h` — recomputed every tick |
| Bone index bounds | `LegitAim.h`, `ESP.h`, `Utils.h` |
| Null model / renderable | `LegitAim.h` `GetAimPosition()`, `ESP.h` |
| Smoothing division | `AngleMath::SmoothingFactor()` |
| Angle normalisation | `AngleMath::ShortestDelta()` / `SmoothTowards()` |
| RAII mutex | `LegitAim::IsFriend()` |
| `public` listener + ownership + unregister | `Misc.h` `GameEvents::`, `dllmain.cpp` |
| `hitmarkerSound` index | `Misc.h`, `ConfigSystem.h`, and the menu combo itself |
| ViewModel FOV / config | `globals.hpp`, `RenderView.h`, `ConfigSystem.h` |
| CI x86/x64 | `.github/workflows/ci.yml` |

## Fixed

**Notification library** [1-6, 11-14, 18-32, 34-41, 44-47, 50-54]
Values returned by value; colours normalised to 0-1; `TextUnformatted()` and
explicit `"%s"`; single-pass `remove_if` removal; `std::size_t` and
bounds-checked indices; mutex-guarded queue with a hard cap and
de-duplication; `steady_clock`; `enum class`; `constexpr` instead of the
`NOTIFY_*` macros; `std::string` instead of two 4 KB buffers with detected
truncation; stable per-toast window ids; `DisplaySize` instead of
`Globals::screenWidth`; balanced `Begin`/`End` and `PushTextWrapPos`/`Pop`;
`AddFontFromMemoryTTF()`'s return value checked; the `FontDataOwnedByAtlas`
contract documented — and, separately, fixed for the five *other* fonts this
project loads, which were handing static arrays to an atlas that would
`IM_FREE()` them.

**Aim path** [55-79, 81-87, 90-96]
Target position recomputed per tick; every interface, renderable, model,
studio header and bone index checked; `find()` under a `lock_guard`; the
`find(entity) != find(entity)` tautology gone; shortest-path angle
interpolation with clamping; a guarded smoothing factor; auto-fire gated on
remaining angular error rather than a tick count; `nullptr` instead of
`localPlayer` as the "no target" sentinel; `GetHighestEntityIndex()` hoisted;
selection mode and hitbox validated; named bone constant; braces throughout.

**Events** [7-11, 123-146]
Zero-initialised and checked `player_info_s`; sanitised, length-capped names;
`<random>` instead of `rand() % n`; `std::array` and `.size()`;
`= default`; `public IGameEventListener2`; `unique_ptr` ownership; idempotent
registration with the result verified through `FindListener()`; unregistration
from both the Unload button and `DLL_PROCESS_DETACH`; `string_format()` (which
threw) replaced by `notify::Format()` (which cannot).

**ViewModel FOV and config** [100, 101, 103-115]
Configured value and captured engine value separated; explicit restore when the
option is off; both new settings in reset/load/save; a post-load validation
pass, because a config file is external input and the sliders bound nothing on
the way in from disk; sliders greyed out when inapplicable; tooltips instead of
bare magic ranges; `viewModelFov` spelled consistently.

**Build and tooling** [88, 89, 116, 151-157, 175, 176, 182]
`#pragma message` removed; include path made relative; `ConPrint` returns
`void` and no longer forwards server-controlled Lua errors as a format string;
`/permissive-` was already on and the diagnostics that would have caught this
review's findings are now errors (C4172, C4700, C4703, C4715, C4716) with
signed/unsigned and shadowing warnings enabled; `.clang-tidy`; CI building
Debug and Release for Win32 and x64, plus tests on gcc and clang under ASan and
UBSan.

**Tests** [158-167] — 24 cases in `tests/`, run by CI.
Phase boundaries, fade clamping, `%s %x %n %%` as literal text, an
8192-character message, empty and null text, control characters, two
consecutive expirations, out-of-bounds removal, 1000 inserts, de-duplication,
id stability, concurrent insert/collect; yaw wrapping, `+179 -> -179`,
`smoothSteps` of 0/negative/NaN/infinity, convergence without overshoot.

## Decided differently

- **[80] "Auto wall".** The option is contradictory because `CanHit()` is a
  plain line-of-sight trace — there is no penetration maths behind it. Rather
  than invert untestable behaviour, the menu label is now "Require line of
  sight", which is what the code does. The config key is unchanged.
- **[123-125] Damage feed scope.** The PR notified on every `player_hurt` on
  the server. Narrowed to the local player's own hits, behind a new
  `damageNotifications` setting (default off). Flip the filter in
  `DamageEvent::FireGameEvent` if a global feed was the intent.
- **[42, 43] `ImGuiToastPos`.** Deleted rather than implemented — a partial API
  nobody called.
- **[78, 79] Fire condition.** Tick counter replaced by an angular-error
  threshold (`LegitAim::kAimReadyDegrees`, 1°). This changes when auto-fire
  triggers; it is the point of the fix, but it is a feel change worth trying.
- **[133] Kill messages.** The crude and sexual-violence entries and the raw
  forum links were dropped while the table was being converted to `std::array`.

## Not done

- **[102] Re-capturing the native ViewModel FOV on a map change.** Captured
  once per injection. A mid-session change to the engine's own `viewmodel_fov`
  is not picked up. Noted in `RenderView.h`.
- **[98] A test for `AngleTo()`'s direction.** It lives in `Vector.h`, which
  does not compile off Windows, so it stayed out of the portable test build.
  Read instead: `AngleTo()` builds its delta as `(*this - vOther)`, so
  `finalPos.AngleTo(eyePos)` is the eye→target direction — the PR's argument
  order is correct. [97, 99] The velocity compensation the PR removed was a
  separate behaviour change and was left removed.
- **[149, 150, 190-192] Moving the globals and the header-only implementations
  into `.cpp` files.** This is a single-translation-unit build: `dllmain.cpp`
  includes everything. The change is right and is a restructuring of the build,
  not a review fix; doing it blind, with no compiler here, would risk more than
  it repays.
- **[120, 121, 171-174] Runtime QA.** DirectX 9 device reset, alt-tab,
  resolution change with toasts on screen, map change, repeated toggling. These
  need the game. **They are the manual pass to run before merging.**
- **[178-181, 183-185] Font licensing.** `GMod-SDK/ImGui/imgui-notify/NOTICE.md`
  records the local changes and pins down the problem: `font_awesome_5.h`
  defines 1817 icons including Pro-only ones (`acorn`, `alicorn`, `abacus`,
  `air-conditioner`), and `ICON_MAX_FA` is `0xf950`. **That is Font Awesome 5
  Pro, which may not be redistributed.** Swapping to the Free Solid build is a
  small change — the toasts use four icons, all present in Free. Not done here
  because it is a decision about the project's licensing, not a code fix. The
  embedded font is 198 KB, and the resulting DLL size has not been measured.
- **[188, 189, 196, 197] `[[nodiscard]]`, `noexcept`, const-correctness and
  cast passes.** Applied to the code this review touched, not retrofitted
  across the whole SDK.

## Follow-up crash fixes

A second pass for null-dereference crashes of the same class as the aim-path
fixes — pointers that come from a signature scan or an engine call and were used
without a check. These prevent the injected module from crashing its host; they
change nothing when the pointers are valid.

- `CreateMove.h` — `Globals::bSendpacket` (from a signature scan, null if it
  failed) was written and read on several paths unchecked; the end-of-frame
  bone backup also ran `localPlayer->GetClientRenderable()->SetupBones(...)`
  outside the `localPlayer` guard, so both `localPlayer` and the renderable
  could be null.
- `PaintTraverse.h` — `LuaShared->GetLuaInterface()` can return null; the Lua
  executor path dereferenced it immediately.
- `GunHacks.h` — `Input->GetUserCmd()` returns null for a slot with no backing
  command; the recoil-reversal code (fas2 and cw bases) dereferenced the result
  straight away. Also un-shadowed the inner `cmd` in the cw loop.

## Verified here

```
$ cd tests && make test
24 test(s), 0 failed, 0 assertion failure(s)

$ make asan          # ASan + UBSan
24 test(s), 0 failed, 0 assertion failure(s)
```

The MSBuild job has **not** been run — there is no Windows toolchain in the
environment these commits were written in. Treat its first run as part of
review.

---

# Second pass — the whole tree

The review above was scoped to PR #64's diff.  This pass went through every
module the project actually owns (`hacks/`, `hooks/`, `core/`, `Memory.*`,
`dllmain.cpp`, the project file), not just the lines that PR touched.

## The defects that mattered

| Where | What |
| --- | --- |
| `Utils.h` | `GetLuaEntBase` / `GetLuaEntName` / `GetClassName` returned a `const char*` from `Lua->GetString()` **after popping the value it points into** — GunHacks ran `strcmp` against it up to four times per shot |
| `Utils.h` | `GetSteamID` returned `info.guid`, a pointer into a local that had gone out of scope |
| `Utils.h` + 8 call sites | The `getKeyState` macro held per-expansion `static` state, so the two hooks that each read the thirdperson and freecam keys kept **separate toggle latches** and drifted out of phase; six call sites passed six arguments to a three-parameter macro, which only MSVC's preprocessor tolerates |
| `dllmain.cpp` | Five signature-scanned pointers fed straight into arithmetic that dereferences them — a failed scan crashed *before* the null guards the first review added, which is why those guards could never fire |
| `Present.h` / `GUI.h` | The Unload button released `menuBg` while it was still referenced by the ImGui draw list rendered at the end of that same frame |
| `drawing.h` | No `OnLostDevice` / `OnResetDevice` at all, and both `D3DXCreate*` results discarded |
| `ConVarSpoofing.h` | Wrote `"XD_" + name` back into `ConVar::pszName`, three bytes longer than the string pool entry it points at; `new` + explicit `->~SpoofedConVar()` meant leak, dangling pointer, and double `free` on a second unload |
| `Executor.h` | `std::atomic<std::pair<bool, LPCSTR>>` is atomic in the *pointer*; the script text was a plain race against the ImGui editor buffer. `strcpy` into a fixed 128 KB buffer with no size check |
| `ESP.h` | `weaponAmmo` dereferenced `GetActiveWeapon()` without the null check the branch four lines above already had |
| `GunHacks.h` | `m_pVerifiedCommands[command_number % 90]` with a signed `command_number` that the cw loop walks down past zero — `-1 % 90` is `-1`, a write *before* an engine-owned array |
| `ScriptDumper.h` | Server-supplied filenames, sanitised against neither reserved device names (`CON`, `NUL`, `COM1`…) nor trailing dots, and with no disk quota — the quota being something upstream's own comment asked for |
| `ConfigSystem.h` | SEH (`__except(EXCEPTION_EXECUTE_HANDLER)`) around code whose failures are C++ exceptions, catching access violations too; one bad key discarded the whole file; the recovery path recursed without a depth limit |
| `AntiAim.h` | Yaw accumulators were function-local `static`s initialised once and never wrapped — past 1e7 degrees a `float`'s ULP is ~1.0, so behaviour depended on injection uptime |
| `Prediction.h` | `if (cmd->weaponselect; auto wp = …)` — the init-statement is discarded, so `SelectItem` ran on entity 0 |
| `Triggerbot.h` | `if (fastShoot) { if (toggle \|\| fastShoot) … else … }` — tautology, unreachable `else`, unread `toggle`; the option has never done anything |
| `notify_core.h` | **The clang CI leg has been red since it was added**: `-Wformat=2` implies `-Wformat-nonliteral`, which clang applies to the `va_list` forwarder.  The first review's "verified here" block only ever ran under gcc |

## New portable headers

The module is one MSVC/DirectX translation unit, so almost none of its logic
could be compiled outside Visual Studio, let alone tested.  Four headers now
carry the parts that have no business depending on Windows — `core/KeyState.h`,
`core/PathSanitize.h`, `core/StringUtil.h`, `core/LuaStack.h` — each replacing
something a module did inline and wrongly.  The suite goes from 24 cases to 67.

Growing that directory is how coverage grows: anything moved into it becomes
testable, and everything in it is exercised.

## Verified

```
$ cd tests && make test                  # gcc, warnings as errors
67 test(s), 0 failed, 0 assertion failure(s)

$ make test CXX=clang++                  # clang, warnings as errors
67 test(s), 0 failed, 0 assertion failure(s)

$ make asan                              # ASan + UBSan
67 test(s), 0 failed, 0 assertion failure(s)
```

## Not verified — read this before trusting the diff

**The Windows module has not been compiled.** There is no MSVC, no Windows SDK
and no DirectX SDK in the environment these commits were written in, and no
MinGW either (which would not have helped: `d3dx9.h` ships only with the DirectX
SDK).  Everything above that is not in `core/` or `tests/` has been reviewed and
reasoned about, not built.  **Treat the first green MSBuild run as part of the
review, not as a formality.**

**The device reset path needs the game.** Hooking `IDirect3DDevice9::Reset`
(vtable index 16) is the single riskiest change here, and it is exactly the one
that cannot be exercised without a running GMod. It lives in its own commit so
it can be reverted alone. The manual pass to run:

- alt-tab in and out of exclusive fullscreen, with toasts on screen
- change resolution, then change it back
- map change, then reconnect
- open and close the menu repeatedly, then press Unload and confirm the overlay
  is gone and the game keeps rendering

## Deliberately left alone

These are decisions, not oversights.

- **`ESP.h`'s entity probe.** `*(uintptr_t*)((char*)entity + 231 * sizeof(uintptr_t))`
  is an unverified, architecture-dependent raw read at a hard-coded offset.
  `GetClientClass()` is almost certainly the right check, but swapping it in
  with no way to run the game would risk more than it repays. Named and
  documented instead.
- **`GunHacks.h`'s `% 90`.** Source's own `MULTIPLAYER_BACKUP` is 150. 90 is
  what upstream used and it is not verified against GMod's build. Named, and the
  index is bounds-checked so a wrong modulus can no longer write out of bounds.
- **`FastSpin` adds its accumulator where its two siblings assign theirs.**
  Almost certainly a typo, but changing it retunes how the pattern feels, which
  is the author's call.
- **`GunHacks.h`'s TFA branch** is entirely commented out upstream, so the
  option does nothing for TFA weapons. Writing those fields blind would be a
  guess.
- **The module stays mapped after Unload.** Freeing it needs a `FreeLibrary`
  from a thread that is not inside the Present hook; the usual workaround is a
  detached thread that sleeps first, which is a race dressed up as a fix. Every
  hook is removed and every device object released, so the module is inert.
- **Font Awesome 5 Pro** is still embedded — see the first review's "Not done".
  It remains the only licensing blocker in the repository.

## Offset refresh + crash-free resolution

Two related changes, both x64 only (the x86 branches are untouched):

### Netvar offsets re-synced to a fresh dump

Hard-coded x64 netvar offsets had drifted after a Garry's Mod update. Corrected
against a current `client.dll` netvar dump (table names in comments):

- `c_basecombatweapon.h` — `m_iClip1` `0x1C48 → 0x1C50`, `m_iClip2`
  `0x1C4C → 0x1C54`, `m_flNextPrimaryAttack` `0x1BFC → 0x1C04`,
  `m_flNextSecondaryAttack` `0x1C00 → 0x1C08` (the whole active/local weapon
  block shifted +8).
- `C_BasePlayer.h` — `m_nTickBase` `0x2D48 → 0x2D90`, `m_nHitboxSet`
  `0x16D0 → 0x16D8` (the old value was `m_nSkin`), view punch
  `0x2DB0 → 0x29E4` (= `m_Local` `0x29B0` + `m_vecPunchAngle` `0x34`; the old
  value now lands on `m_hLastWeapon`, so "no visual recoil" was writing into a
  weapon handle).

Left as-is because they are **not** networked and so are absent from a netvar
dump — they can only be re-reversed against the binary: `getMoveType`
(`m_MoveType`), `IsDormant`, `GetModelPtr`.

### Init no longer crashes on a stale offset/scan

Separate from the netvar values above, several pointers come from hard-coded
byte offsets into game functions (`ViewRenderOffset`, `GlobalVarsOffset`, …) or
from signature scans. When one of those rots after an update it resolves to a
wild pointer, and the code dereferenced it immediately — this is what took the
game down at the RenderView VMT hook. Now:

- `MemIsReadable()` (Memory.h) — `VirtualQuery`-based check that a range is
  committed and readable.
- `GetVMT` / `GetRealFromRelative` / `VMTHook` validate every dereference and
  return null instead of faulting; `GuardedVMTHook` (dllmain.cpp) and the
  `present` install site log the offender by name and skip it.
- Main() logs each offset/scan-derived pointer and whether it is readable, so
  `debug.log` names exactly which offset to re-reverse.

This makes a stale offset a *degraded load* (that feature is off, logged) rather
than a crash. It does **not** fix the offsets it can't see: `ViewRenderOffset`
(and the other code-scan offsets in `globals.hpp`) still need re-reversing
against the current binary — a netvar dump does not contain them. Until then the
RenderView-dependent visuals stay disabled and say so in the log.

### Present hooked through the D3D9 device vtable, not the Steam overlay

`Present` used to be found by scanning `gameoverlayrenderer64` for the overlay's
stored Present pointer (`PresentPattern`). That pattern rots on every Steam
overlay update, and in the merged start-up path a failed scan lands in
`missingPatterns` and aborts the **whole** load — so one stale overlay pattern
took the entire cheat down (no menu, no ESP, nothing).

Present is now hooked the update-proof way (the "kiero" method):
`GetD3D9DeviceVTable()` (dllmain.cpp) spins up a throwaway `IDirect3DDevice9` on
a dedicated hidden window, reads its vtable, and releases it. That vtable lives
in `d3d9.dll` and is shared by every device instance, so `GuardedVMTHook`-ing
its Present slot (index 17, via the `presentDeviceVTable` global) also redirects
the game's real device. The COM vtable layout is fixed by the OS, so it does not
rot. Consequences:

- `PresentModule` / `PresentPattern` (both arches) and the `char* present`
  global are gone; Present is no longer part of the signature scan, so a Steam
  update can no longer block the load.
- Restore is now uniform: `RestoreVMTHooks()` takes Present back on unload like
  every other vtable hook (the lazy Reset hook at index 16 already worked this
  way), so the manual `present`-slot restore in `PerformUnload` was removed.
- `d3d9.lib` was already linked (the `#pragma comment(lib, ...)` in
  `hacks/menu/drawing.h`).

Not built here (no Windows toolchain); first MSBuild run is the check. If the
probe `CreateDevice` ever fails it is logged and Present is skipped (degraded,
no crash) rather than aborting the load.

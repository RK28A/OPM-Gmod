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

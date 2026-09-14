# Vendored dependency: imgui-notify

## What is in this directory

| File | Origin | Size |
| --- | --- | --- |
| `imgui_notify.h` | [patrickcjk/imgui-notify](https://github.com/patrickcjk/imgui-notify), **substantially rewritten** — see below | ~9 KB |
| `notify_core.h` | Written for this project, not upstream | ~11 KB |
| `font_awesome_5.h` | Font Awesome 5 icon-name defines, 1817 icons | ~1 800 lines |
| `fa_solid_900.h` | Font Awesome 5 "Solid 900" TTF, embedded as a C byte array | 198 KB of font data |

## ⚠️ Licensing: unresolved

**`fa_solid_900.h` and `font_awesome_5.h` look like Font Awesome 5 *Pro*, not Free.**

`font_awesome_5.h` defines 1817 icons, among them `ICON_FA_ACORN`,
`ICON_FA_ALICORN`, `ICON_FA_ABACUS`, `ICON_FA_ALARM_CLOCK` and
`ICON_FA_AIR_CONDITIONER`. Those are Pro-only icons; Font Awesome 5 Free Solid
ships roughly a thousand and none of them. `ICON_MAX_FA` is `0xf950`, well past
the Free range.

Font Awesome Pro is sold under a commercial licence that does **not** permit
redistributing the font files in a public repository. Font Awesome *Free* would
be fine — fonts under SIL OFL 1.1, icons under CC BY 4.0, code under MIT — with
attribution.

Nothing here should be treated as settled until someone confirms the
provenance. Two ways out:

1. Replace both headers with the Font Awesome 5 **Free** Solid build (the
   notifications only use four icons: `check-circle`, `exclamation-triangle`,
   `times-circle`, `info-circle`, all of which are in Free), then record the
   exact release below and add the OFL/CC-BY notices.
2. Drop the icon font altogether. `ImGui::MergeIconsWithLatestFont()` is
   optional and `notify_detail::IconFor()` already returns `""` for a type with
   no icon, so the toasts render fine without it.

Option 1 is the small one and keeps the look.

### Upstream imgui-notify licence

imgui-notify is MIT (Copyright (c) Patrick Kalbermatter). The upstream
`LICENSE` file was not vendored along with the header; it should be, next to
this file, if the code stays.

### Pinning

The exact upstream commit this was taken from is not recorded — the header only
carries a link to the repository. Whoever refreshes it next should write the
commit hash here so the local changes below can be diffed against a known base.

## Local changes to `imgui_notify.h`

This is **not** a pristine copy. The file was rewritten while addressing the
code review of PR #64; the notable differences from upstream are:

- All non-UI logic moved to `notify_core.h`, which has no ImGui, Windows or
  DirectX dependency so it can be unit tested (`tests/notify_tests.cpp`).
- `get_color()` and `get_phase()` returned references bound to temporaries;
  they now return by value.
- Colours were built from 0-255 literals in an `ImVec4`, whose components are
  0-1. They are normalised now.
- `Text(content)` / `Text(title)` / `TextColored(color, icon)` passed
  player-controlled text as a printf format string. They use
  `TextUnformatted()` and an explicit `"%s"`.
- `set_title()` / `set_content()` took a format string. They are now
  `SetTitle()` / `SetContent()` (plain text) and `SetTitleFormat()` /
  `SetContentFormat()` (literal format + arguments). The old names are
  deliberately *not* aliased, so an old call site fails to compile instead of
  silently keeping the unsafe behaviour.
- The render loop called `erase(i)` and then `i++`, skipping the element
  shifted into slot `i`. Removal is a single `std::remove_if` pass, and the
  renderer walks a snapshot taken by value.
- The global `std::vector<ImGuiToast>` is now a `notify::Queue` guarded by a
  mutex, capped at `notify::kMaxNotifications`, and de-duplicating identical
  toasts inside a short window.
- ImGui window ids were `"##TOAST%d"` keyed on the vector index, so they
  changed under the survivors whenever a toast disappeared. They are keyed on a
  stable per-toast id.
- Positioning read `Globals::screenWidth/Height` (a local patch from this
  project, zero until the game reports a resolution). It uses
  `ImGui::GetIO().DisplaySize`, which also removes the dependency on
  `globals.hpp`.
- `GetTickCount64()` replaced with `std::chrono::steady_clock`.
- `#include <Windows.h>`, `imgui_impl_dx9.h` and `imgui_impl_win32.h` dropped:
  the notification logic never needed the platform backend.
- `MergeIconsWithLatestFont()` checks the ImGui context, the atlas and the
  return value of `AddFontFromMemoryTTF()` instead of ignoring all three.

## Build size

`fa_solid_900.h` adds about 198 KB of font data to the module, embedded in the
binary rather than loaded from disk. Worth measuring against the previous
release before shipping.

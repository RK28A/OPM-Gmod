// KeyState.h -- the key-binding state machine, split out of the getKeyState
// macro that used to live in hacks/Utils.h.
//
// The macro was a brace-enclosed block holding its own `static bool toggleState`
// and `static bool lastButtonState`.  Two consequences, both bugs:
//
//   * Each expansion got its *own* statics.  The thirdperson and freecam keys
//     are read from two different hooks (FrameStageNotify.h decides whether to
//     rewrite the local view angles, RenderView.h decides whether to move the
//     camera), so in toggle mode the two sites kept two independent toggle
//     states, updated at different rates, and drifted out of phase.
//   * Six of the eight call sites passed six arguments to a three-parameter
//     macro (`..., henlo1, henlo2, henlo3`).  Those identifiers are declared
//     nowhere; MSVC warns C4002 and silently discards them, every other
//     preprocessor rejects the translation unit outright.
//
// The state is an explicit object now, one per *feature* rather than one per
// call site, so both readers of the thirdperson key share a single toggle.
//
// This header is deliberately free of Windows, DirectX and SDK dependencies:
// Resolve() is pure, so tests/keystate_tests.cpp drives it directly and the
// polling of the actual input device stays in hacks/Utils.h.

#ifndef GMOD_SDK_KEY_STATE_H
#define GMOD_SDK_KEY_STATE_H

namespace input
{
	// Values are the ones the menu combo and the config file already store, so
	// existing configs keep working.
	enum class KeyStyle
	{
		Always = 0, // the feature is always on, the key is ignored
		Hold = 1,   // active while the key is held
		Toggle = 2, // flips on each press
		Never = 3,  // the feature is bound but disabled
	};

	// Per-feature latch.  Two readers of the same key must share one of these,
	// otherwise their toggles diverge.
	struct KeyState
	{
		bool toggle = false;
		bool lastDown = false;
	};

	// `rawDown` means "the key is physically down and the game, not the menu,
	// owns the input".  Reading the device is the caller's job (see PollKey in
	// hacks/Utils.h); everything here is pure so it can be tested off Windows.
	//
	// An out-of-range style -- which a hand-edited config can produce, since
	// nothing bounds this value on the way in from disk -- returns false rather
	// than leaving the caller's variable untouched.  The macro had no default
	// case, so `bool antiAimKeyDown;` in CreateMove.h was read uninitialised.
	[[nodiscard]] inline bool Resolve(int style, bool rawDown, KeyState& state) noexcept
	{
		switch (static_cast<KeyStyle>(style))
		{
		case KeyStyle::Always:
			return true;

		case KeyStyle::Hold:
			// The latch is still advanced so that switching a binding from hold
			// to toggle does not inherit a stale edge from before the switch.
			state.lastDown = rawDown;
			return rawDown;

		case KeyStyle::Toggle:
		{
			const bool pressed = rawDown && !state.lastDown; // rising edge only
			if (pressed)
				state.toggle = !state.toggle;

			state.lastDown = rawDown;
			return state.toggle;
		}

		case KeyStyle::Never:
			state.lastDown = rawDown;
			return false;
		}

		return false;
	}
} // namespace input

#endif // GMOD_SDK_KEY_STATE_H

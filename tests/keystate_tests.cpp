// Tests for core/KeyState.h -- the state machine behind the key bindings,
// extracted from the getKeyState macro.

#include "test_harness.h"

#include "../GMod-SDK/core/KeyState.h"

using input::KeyState;
using input::Resolve;

namespace
{
	constexpr int kAlways = 0;
	constexpr int kHold = 1;
	constexpr int kToggle = 2;
	constexpr int kNever = 3;
}

TEST(KeyStyleAlwaysIgnoresTheKey)
{
	KeyState state;
	CHECK(Resolve(kAlways, false, state));
	CHECK(Resolve(kAlways, true, state));
}

TEST(KeyStyleNeverIsAlwaysFalse)
{
	KeyState state;
	CHECK(!Resolve(kNever, false, state));
	CHECK(!Resolve(kNever, true, state));
}

TEST(KeyStyleHoldFollowsTheKey)
{
	KeyState state;
	CHECK(!Resolve(kHold, false, state));
	CHECK(Resolve(kHold, true, state));
	CHECK(Resolve(kHold, true, state));
	CHECK(!Resolve(kHold, false, state));
}

TEST(KeyStyleToggleFlipsOnTheRisingEdgeOnly)
{
	KeyState state;

	CHECK(!Resolve(kToggle, false, state));

	// Press: flips on.
	CHECK(Resolve(kToggle, true, state));
	// Held: stays on, does not flip again.
	CHECK(Resolve(kToggle, true, state));
	CHECK(Resolve(kToggle, true, state));
	// Released: stays on.
	CHECK(Resolve(kToggle, false, state));
	CHECK(Resolve(kToggle, false, state));

	// Second press: flips off.
	CHECK(!Resolve(kToggle, true, state));
	CHECK(!Resolve(kToggle, true, state));
	CHECK(!Resolve(kToggle, false, state));
}

// The bug this whole header exists for: the thirdperson key is read from both
// FrameStageNotify and RenderView.  With the macro each site owned a private
// `static bool toggleState`, so the two answers drifted apart; sharing one
// KeyState keeps them identical whatever the polling order is.
TEST(TwoReadersSharingOneStateStayInSync)
{
	KeyState shared;

	// One frame: the hook that runs first sees the press, the second one must
	// observe the same result rather than flipping the toggle a second time.
	const bool firstReader = Resolve(kToggle, true, shared);
	const bool secondReader = Resolve(kToggle, true, shared);
	CHECK(firstReader);
	CHECK_EQ(firstReader, secondReader);

	// Key released over the next frame.
	CHECK_EQ(Resolve(kToggle, false, shared), Resolve(kToggle, false, shared));

	// Second press turns it back off, once, for both readers.
	const bool firstAfter = Resolve(kToggle, true, shared);
	const bool secondAfter = Resolve(kToggle, true, shared);
	CHECK(!firstAfter);
	CHECK_EQ(firstAfter, secondAfter);
}

// Two *separate* states are what the macro effectively gave every call site.
// This pins the failure mode down so the regression is recognisable if anyone
// reintroduces per-site state.
TEST(TwoReadersWithSeparateStatesDiverge)
{
	KeyState a;
	KeyState b;

	// The key is polled by reader A on a frame where B does not run at all --
	// exactly what happens when one hook fires more often than the other.
	CHECK(Resolve(kToggle, true, a));
	CHECK(Resolve(kToggle, false, a));
	CHECK(!Resolve(kToggle, true, a)); // A has now flipped twice: off

	CHECK(Resolve(kToggle, true, b));  // B has flipped once: on

	CHECK(!Resolve(kToggle, true, a));
	CHECK(Resolve(kToggle, true, b));
}

// A hand-edited config can put anything in the style field; nothing bounds it
// on the way in from disk.  The macro had no default case, so the caller's
// `bool keyDown;` kept whatever was on the stack.
TEST(OutOfRangeStyleIsFalseNotIndeterminate)
{
	KeyState state;
	CHECK(!Resolve(-1, true, state));
	CHECK(!Resolve(4, true, state));
	CHECK(!Resolve(99999, true, state));
	CHECK(!Resolve(-99999, false, state));
}

// Switching a binding from hold to toggle must not inherit a stale edge: if
// hold left lastDown at false while the key was really down, the first toggle
// poll would see a rising edge that never happened.
TEST(SwitchingStyleDoesNotInheritAStaleEdge)
{
	KeyState state;

	// Key is held down under the hold style.
	CHECK(Resolve(kHold, true, state));

	// User switches the binding to toggle while still holding the key.  No
	// rising edge occurred, so the toggle must not flip.
	CHECK(!Resolve(kToggle, true, state));

	// Releasing and pressing again is a real edge.
	CHECK(!Resolve(kToggle, false, state));
	CHECK(Resolve(kToggle, true, state));
}

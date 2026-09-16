// Tests for core/LuaStack.h.
//
// Driven by a mock rather than a live CLuaInterface: the guard only ever calls
// Pop(int), which is the whole point of counting pushes instead of reading
// lua_gettop.

#include "test_harness.h"

#include "../GMod-SDK/core/LuaStack.h"

#include <vector>

namespace
{
	// Stands in for CLuaInterface.  Records every Pop so a test can assert on
	// the exact sequence, and tracks a notional stack depth so an underflow --
	// popping more than was pushed -- is detectable.
	struct MockLua
	{
		std::vector<int> pops;
		int depth = 0;
		bool underflowed = false;

		void Pop(int amount)
		{
			pops.push_back(amount);
			depth -= amount;
			if (depth < 0)
				underflowed = true;
		}
	};

	using Guard = lua::StackGuard<MockLua>;
}

TEST(GuardPopsEverythingItWasToldAbout)
{
	MockLua lua;
	{
		Guard guard(&lua);
		lua.depth += 3;
		guard.Pushed(3);
	}

	CHECK_EQ(lua.pops.size(), static_cast<std::size_t>(1));
	CHECK_EQ(lua.pops[0], 3);
	CHECK_EQ(lua.depth, 0);
	CHECK(!lua.underflowed);
}

TEST(GuardPopsNothingWhenNothingWasPushed)
{
	MockLua lua;
	{
		Guard guard(&lua);
	}

	CHECK(lua.pops.empty());
	CHECK_EQ(lua.depth, 0);
}

TEST(GuardAccumulatesSuccessivePushes)
{
	MockLua lua;
	{
		Guard guard(&lua);
		lua.depth += 1; guard.Pushed();
		lua.depth += 1; guard.Pushed();
		lua.depth += 2; guard.Pushed(2);
		CHECK_EQ(guard.Depth(), 4);
	}

	CHECK_EQ(lua.pops.size(), static_cast<std::size_t>(1));
	CHECK_EQ(lua.pops[0], 4);
	CHECK_EQ(lua.depth, 0);
}

// The bug the guard exists for: a branch that returns from the middle used to
// skip the single Pop at the bottom of the function.
TEST(GuardBalancesOnAnEarlyReturn)
{
	MockLua lua;

	const auto earlyReturn = [&lua]() -> int {
		Guard guard(&lua);
		lua.depth += 1; guard.Pushed();
		lua.depth += 1; guard.Pushed();

		return 42; // the path that used to leak two values per shot

		// unreachable, but this is what the old code relied on
	};

	CHECK_EQ(earlyReturn(), 42);
	CHECK_EQ(lua.depth, 0);
	CHECK(!lua.underflowed);
}

TEST(GuardBalancesWhenAnExceptionUnwinds)
{
	MockLua lua;

	try
	{
		Guard guard(&lua);
		lua.depth += 2; guard.Pushed(2);
		throw 1;
	}
	catch (int)
	{
	}

	CHECK_EQ(lua.depth, 0);
	CHECK(!lua.underflowed);
}

TEST(ExplicitPopReducesWhatIsLeftForTheDestructor)
{
	MockLua lua;
	{
		Guard guard(&lua);
		lua.depth += 3; guard.Pushed(3);

		guard.Pop(1);
		CHECK_EQ(guard.Depth(), 2);
		CHECK_EQ(lua.depth, 2);
	}

	CHECK_EQ(lua.pops.size(), static_cast<std::size_t>(2));
	CHECK_EQ(lua.pops[0], 1);
	CHECK_EQ(lua.pops[1], 2);
	CHECK_EQ(lua.depth, 0);
}

// Over-popping must never reach into an enclosing scope's values: the guard
// clamps to what it was told about.
TEST(PoppingMoreThanWasPushedIsClamped)
{
	MockLua lua;
	lua.depth = 5; // values owned by an enclosing scope

	{
		Guard guard(&lua);
		lua.depth += 2; guard.Pushed(2);
		guard.Pop(10);
	}

	CHECK_EQ(lua.pops.size(), static_cast<std::size_t>(1));
	CHECK_EQ(lua.pops[0], 2);
	CHECK_EQ(lua.depth, 5); // the enclosing scope's values are untouched
	CHECK(!lua.underflowed);
}

// Lua->Call() removes the function and its arguments itself.  Accounting for
// them with Consumed() rather than Pop() is the difference between forgetting
// them and popping them a second time.
TEST(ConsumedForgetsValuesWithoutPopping)
{
	MockLua lua;
	{
		Guard guard(&lua);

		// PushSpecial, GetField, GetField, PushNumber, PushNumber
		lua.depth += 5;
		guard.Pushed(5);

		// Call(2, 1): pops the function plus two arguments, pushes one result.
		lua.depth -= 3;
		guard.Consumed(3);
		lua.depth += 1;
		guard.Pushed(1);

		CHECK_EQ(guard.Depth(), 3);
		CHECK_EQ(lua.depth, 3);

		// Nothing has been popped by the guard yet.
		CHECK(lua.pops.empty());
	}

	CHECK_EQ(lua.pops.size(), static_cast<std::size_t>(1));
	CHECK_EQ(lua.pops[0], 3);
	CHECK_EQ(lua.depth, 0);
	CHECK(!lua.underflowed);
}

TEST(ConsumedIsClampedAndIgnoresNonPositiveCounts)
{
	MockLua lua;
	{
		Guard guard(&lua);
		guard.Pushed(2);

		guard.Consumed(0);
		guard.Consumed(-4);
		CHECK_EQ(guard.Depth(), 2);

		guard.Consumed(10); // more than was ever pushed
		CHECK_EQ(guard.Depth(), 0);
	}

	CHECK(lua.pops.empty());
}

TEST(ReleaseIsIdempotent)
{
	MockLua lua;
	{
		Guard guard(&lua);
		lua.depth += 2; guard.Pushed(2);

		guard.Release();
		guard.Release();
		CHECK_EQ(guard.Depth(), 0);
	}

	CHECK_EQ(lua.pops.size(), static_cast<std::size_t>(1));
	CHECK_EQ(lua.pops[0], 2);
	CHECK_EQ(lua.depth, 0);
}

TEST(NonPositivePushesAndPopsAreIgnored)
{
	MockLua lua;
	{
		Guard guard(&lua);
		guard.Pushed(0);
		guard.Pushed(-5);
		CHECK_EQ(guard.Depth(), 0);

		guard.Pop(0);
		guard.Pop(-3);
	}

	CHECK(lua.pops.empty());
}

// The Lua interface pointer is null whenever GetLuaInterface() has not resolved
// yet; the guard must be usable anyway rather than being a second null deref.
TEST(GuardOnANullInterfaceDoesNothing)
{
	Guard guard(nullptr);
	guard.Pushed(3);
	guard.Pop(1);
	guard.Release();

	// Nothing to assert beyond "it did not dereference": reaching here is the
	// test.
	CHECK(true);
}

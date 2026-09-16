// LuaStack.h -- RAII balancing for the Lua stack.
//
// Utils.h and GunHacks.h used to track how much to pop in a local `int topop`
// that was decremented from inside conditionals, then spent in a single
// Lua->Pop(topop) on the way out.  It balances today, but it is not verifiable
// by reading, and it only balances on the paths that reach the bottom of the
// function: GunHacks::NoSpread has three `return` statements inside the branch
// chain, each of which leaves a different amount on the stack.  A leak there is
// not a one-off -- these run once per shot, every shot.
//
// The guard counts what the caller says it pushed and pops exactly that much on
// every exit, `return` and exception alike.  It deliberately does not read
// lua_gettop: CLuaInterface declares Top() as returning void*, which on x64
// reads all of RAX for a callee that only set EAX, so the high half is
// whatever was there before.  Counting pushes needs no such assumption.
//
// Templated on the interface type so tests/luastack_tests.cpp can drive it with
// a mock instead of a live CLuaInterface.

#ifndef GMOD_SDK_LUA_STACK_H
#define GMOD_SDK_LUA_STACK_H

namespace lua
{
	template <class TLua>
	class StackGuard
	{
	public:
		explicit StackGuard(TLua* lua) noexcept : lua_(lua) {}

		~StackGuard() { Release(); }

		StackGuard(const StackGuard&) = delete;
		StackGuard& operator=(const StackGuard&) = delete;
		StackGuard(StackGuard&&) = delete;
		StackGuard& operator=(StackGuard&&) = delete;

		// Record values the caller has just pushed.
		void Pushed(int count = 1) noexcept
		{
			if (count > 0)
				depth_ += count;
		}

		// Record values Lua removed on its own, without popping anything here.
		//
		// Call(nargs, nresults) takes the function and its arguments off the
		// stack before pushing the results, so the guard must forget them or it
		// would pop that many values a second time on the way out -- eating
		// whatever the enclosing scope had underneath.
		void Consumed(int count) noexcept
		{
			if (count <= 0)
				return;

			depth_ -= (count > depth_) ? depth_ : count;
		}

		// Pop early, for the paths that need the stack unwound before they do
		// something else.  Never pops more than was recorded, so an over-count
		// here cannot eat a value that belongs to an enclosing scope.
		void Pop(int count = 1) noexcept
		{
			if (!lua_ || count <= 0)
				return;

			const int amount = (count > depth_) ? depth_ : count;
			if (amount <= 0)
				return;

			lua_->Pop(amount);
			depth_ -= amount;
		}

		// Pops everything still outstanding.  Idempotent.
		void Release() noexcept { Pop(depth_); }

		[[nodiscard]] int Depth() const noexcept { return depth_; }

	private:
		TLua* lua_;
		int depth_ = 0;
	};
} // namespace lua

#endif // GMOD_SDK_LUA_STACK_H

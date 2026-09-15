// AngleMath.h -- view-angle helpers shared by the aim code.
//
// Kept free of every SDK/Windows dependency and templated on the angle type so
// the same functions can be driven by QAngle in the module and by a plain
// struct in tests/anglemath_tests.cpp.
//
// TAngle only has to expose float members x (pitch), y (yaw), z (roll) and a
// TAngle(x, y, z) constructor -- which is exactly QAngle's shape.

#ifndef GMOD_SDK_ANGLE_MATH_H
#define GMOD_SDK_ANGLE_MATH_H

#include <algorithm>
#include <cmath>

namespace AngleMath
{
	inline constexpr float kMaxPitch = 89.f;
	inline constexpr float kMinPitch = -89.f;

	// Wraps a yaw into [-180, 180).  fmod rather than a `while (y > 180) y -= 360`
	// loop: a NaN or a huge value out of a corrupt config would spin that loop
	// forever instead of returning.
	[[nodiscard]] inline float NormalizeYaw(float yaw) noexcept
	{
		if (!std::isfinite(yaw))
			return 0.f;

		yaw = std::fmod(yaw + 180.f, 360.f);
		if (yaw < 0.f)
			yaw += 360.f;

		return yaw - 180.f;
	}

	[[nodiscard]] inline float ClampPitch(float pitch) noexcept
	{
		if (!std::isfinite(pitch))
			return 0.f;

		// std::clamp, not std::max(std::min(...)): the module compiles this
		// header into a translation unit that has already included <Windows.h>,
		// whose min()/max() function-like macros turn a bare std::min/std::max
		// into "std::(" -- MSVC C2589.  There is no clamp macro to collide with.
		return std::clamp(pitch, kMinPitch, kMaxPitch);
	}

	// Signed shortest way round from `current` to `target`.
	//
	// This is the fix for the +179 -> -179 case: the naive `target - current`
	// gives -358 and swings the view almost a full turn the wrong way, while
	// this returns +2.
	[[nodiscard]] inline float ShortestDelta(float target, float current) noexcept
	{
		if (!std::isfinite(target) || !std::isfinite(current))
			return 0.f;

		return NormalizeYaw(target - current);
	}

	// Fraction of the remaining error to consume per tick.
	//
	// Guards `1.f / steps` against zero, negatives and NaN: the GUI slider is
	// bounded but a hand-edited or corrupt config file is not.  A factor of 1
	// means "no smoothing", which is the safe degradation.
	[[nodiscard]] inline float SmoothingFactor(float steps) noexcept
	{
		if (!std::isfinite(steps) || steps <= 1.f)
			return 1.f;

		return 1.f / steps;
	}

	template <class TAngle>
	[[nodiscard]] bool IsFinite(const TAngle& angle) noexcept
	{
		return std::isfinite(angle.x) && std::isfinite(angle.y) && std::isfinite(angle.z);
	}

	// Brings any angle back into the engine's valid domain.
	template <class TAngle>
	[[nodiscard]] TAngle Sanitize(const TAngle& angle) noexcept
	{
		return TAngle(ClampPitch(angle.x), NormalizeYaw(angle.y), 0.f);
	}

	// Exponential approach: consume `factor` of the *shortest* remaining error
	// on each axis, then re-normalise so an invalid angle is never propagated.
	template <class TAngle>
	[[nodiscard]] TAngle SmoothTowards(const TAngle& current, const TAngle& target, float factor) noexcept
	{
		const float f = std::isfinite(factor) ? std::clamp(factor, 0.f, 1.f) : 1.f;

		const float pitch = current.x + ShortestDelta(target.x, current.x) * f;
		const float yaw = current.y + ShortestDelta(target.y, current.y) * f;

		return TAngle(ClampPitch(pitch), NormalizeYaw(yaw), 0.f);
	}

	// Angular distance still to cover, in degrees.
	//
	// Used instead of a tick counter to decide "the aim has arrived": with an
	// exponential approach the error only tends towards zero, so counting N
	// ticks says nothing about how close the crosshair actually is.
	template <class TAngle>
	[[nodiscard]] float RemainingError(const TAngle& current, const TAngle& target) noexcept
	{
		const float pitch = ShortestDelta(target.x, current.x);
		const float yaw = ShortestDelta(target.y, current.y);

		return std::sqrt(pitch * pitch + yaw * yaw);
	}

} // namespace AngleMath

#endif // GMOD_SDK_ANGLE_MATH_H

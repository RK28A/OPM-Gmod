// Tests for the view-angle helpers used by the legit aim smoothing.

#include "test_harness.h"

#include "../GMod-SDK/mathlib/AngleMath.h"

#include <cmath>
#include <limits>

namespace
{
	// Stand-in for QAngle: same member layout and constructor shape, no SDK.
	struct Angle
	{
		float x = 0.f;
		float y = 0.f;
		float z = 0.f;

		Angle() = default;
		Angle(float pitch, float yaw, float roll) : x(pitch), y(yaw), z(roll) {}
	};

	TEST(NormalizeYawWrapsIntoRange)
	{
		CHECK_NEAR(AngleMath::NormalizeYaw(0.f), 0.0, 1e-4);
		CHECK_NEAR(AngleMath::NormalizeYaw(179.f), 179.0, 1e-4);
		CHECK_NEAR(AngleMath::NormalizeYaw(-179.f), -179.0, 1e-4);
		CHECK_NEAR(AngleMath::NormalizeYaw(190.f), -170.0, 1e-4);
		CHECK_NEAR(AngleMath::NormalizeYaw(-190.f), 170.0, 1e-4);
		CHECK_NEAR(AngleMath::NormalizeYaw(360.f), 0.0, 1e-4);
		CHECK_NEAR(AngleMath::NormalizeYaw(720.f + 45.f), 45.0, 1e-4);

		// A value big enough to hang a `while (y > 180) y -= 360` loop.
		CHECK_NEAR(AngleMath::NormalizeYaw(1.0e9f + 45.f), AngleMath::NormalizeYaw(1.0e9f + 45.f), 1e-4);
		CHECK(std::isfinite(AngleMath::NormalizeYaw(1.0e9f)));
	}

	TEST(ClampPitchStaysInEngineDomain)
	{
		CHECK_NEAR(AngleMath::ClampPitch(0.f), 0.0, 1e-4);
		CHECK_NEAR(AngleMath::ClampPitch(45.f), 45.0, 1e-4);
		CHECK_NEAR(AngleMath::ClampPitch(120.f), 89.0, 1e-4);
		CHECK_NEAR(AngleMath::ClampPitch(-120.f), -89.0, 1e-4);
	}

	// The wrap-around case: aiming from +179 to -179 is a 2 degree turn, not a
	// 358 degree one in the opposite direction.
	TEST(ShortestDeltaTakesTheShortWayRound)
	{
		CHECK_NEAR(AngleMath::ShortestDelta(-179.f, 179.f), 2.0, 1e-3);
		CHECK_NEAR(AngleMath::ShortestDelta(179.f, -179.f), -2.0, 1e-3);

		CHECK_NEAR(AngleMath::ShortestDelta(10.f, 0.f), 10.0, 1e-4);
		CHECK_NEAR(AngleMath::ShortestDelta(-10.f, 0.f), -10.0, 1e-4);
		CHECK_NEAR(AngleMath::ShortestDelta(0.f, 0.f), 0.0, 1e-4);

		// Never larger than half a turn, whatever the inputs.
		for (float target = -540.f; target <= 540.f; target += 13.f)
		{
			for (float current = -540.f; current <= 540.f; current += 37.f)
			{
				const float delta = AngleMath::ShortestDelta(target, current);
				CHECK(std::fabs(delta) <= 180.0001f);
			}
		}
	}

	// smoothSteps == 0 fed 1.f / 0.f straight into the interpolation.
	TEST(SmoothingFactorRejectsDegenerateStepCounts)
	{
		CHECK_NEAR(AngleMath::SmoothingFactor(0.f), 1.0, 1e-6);
		CHECK_NEAR(AngleMath::SmoothingFactor(-5.f), 1.0, 1e-6);
		CHECK_NEAR(AngleMath::SmoothingFactor(1.f), 1.0, 1e-6);
		CHECK_NEAR(AngleMath::SmoothingFactor(std::numeric_limits<float>::quiet_NaN()), 1.0, 1e-6);
		CHECK_NEAR(AngleMath::SmoothingFactor(std::numeric_limits<float>::infinity()), 1.0, 1e-6);

		CHECK_NEAR(AngleMath::SmoothingFactor(10.f), 0.1, 1e-6);
		CHECK_NEAR(AngleMath::SmoothingFactor(50.f), 0.02, 1e-6);

		// Always a usable fraction of the remaining error.
		for (float steps = -100.f; steps <= 100.f; steps += 0.5f)
		{
			const float factor = AngleMath::SmoothingFactor(steps);
			CHECK(factor > 0.f);
			CHECK(factor <= 1.f);
		}
	}

	TEST(SmoothTowardsCrossesTheWrapCorrectly)
	{
		const Angle current(0.f, 179.f, 0.f);
		const Angle target(0.f, -179.f, 0.f);

		const Angle step = AngleMath::SmoothTowards(current, target, 0.5f);

		// Half of a +2 degree turn lands on -180/+180, not somewhere near 0.
		CHECK_NEAR(std::fabs(AngleMath::ShortestDelta(step.y, 180.f)), 0.0, 1e-2);
		CHECK(std::fabs(AngleMath::ShortestDelta(target.y, step.y)) < 2.f);
	}

	TEST(SmoothTowardsConvergesAndNeverOvershoots)
	{
		Angle current(0.f, 179.f, 0.f);
		const Angle target(35.f, -179.f, 0.f);

		float previous = AngleMath::RemainingError(current, target);

		for (int tick = 0; tick < 200; ++tick)
		{
			current = AngleMath::SmoothTowards(current, target, AngleMath::SmoothingFactor(10.f));

			const float remaining = AngleMath::RemainingError(current, target);
			CHECK(remaining <= previous + 1e-3f); // monotonically decreasing
			CHECK(std::isfinite(current.x));
			CHECK(std::isfinite(current.y));
			CHECK(current.x <= AngleMath::kMaxPitch && current.x >= AngleMath::kMinPitch);
			CHECK(current.y >= -180.f && current.y < 180.f);
			previous = remaining;
		}

		CHECK_NEAR(AngleMath::RemainingError(current, target), 0.0, 1e-2);
	}

	// With factor 1 the aim arrives in a single tick -- the degraded mode used
	// when the configured step count is invalid.
	TEST(SmoothTowardsWithFullFactorSnaps)
	{
		const Angle current(0.f, 0.f, 0.f);
		const Angle target(30.f, 90.f, 0.f);

		const Angle step = AngleMath::SmoothTowards(current, target, AngleMath::SmoothingFactor(0.f));

		CHECK_NEAR(step.x, 30.0, 1e-3);
		CHECK_NEAR(step.y, 90.0, 1e-3);
		CHECK_NEAR(step.z, 0.0, 1e-6);
	}

	// Angles reconstructed from a corrupt config or a degenerate trace must not
	// propagate NaN/inf into the view angles sent to the engine.
	TEST(NonFiniteAnglesAreContained)
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();

		const Angle poisoned(nan, inf, nan);
		CHECK(!AngleMath::IsFinite(poisoned));

		const Angle clean = AngleMath::Sanitize(poisoned);
		CHECK(std::isfinite(clean.x));
		CHECK(std::isfinite(clean.y));
		CHECK(std::isfinite(clean.z));

		const Angle smoothed = AngleMath::SmoothTowards(Angle(0.f, 0.f, 0.f), poisoned, 0.5f);
		CHECK(std::isfinite(smoothed.x));
		CHECK(std::isfinite(smoothed.y));

		const Angle from_poison = AngleMath::SmoothTowards(poisoned, Angle(10.f, 20.f, 0.f), 0.5f);
		CHECK(std::isfinite(from_poison.x));
		CHECK(std::isfinite(from_poison.y));

		CHECK(std::isfinite(AngleMath::RemainingError(poisoned, Angle(0.f, 0.f, 0.f))));
	}

	TEST(SanitizeAlwaysZeroesRoll)
	{
		const Angle angle = AngleMath::Sanitize(Angle(200.f, 400.f, 33.f));

		CHECK_NEAR(angle.x, 89.0, 1e-4);
		CHECK_NEAR(angle.y, 40.0, 1e-4);
		CHECK_NEAR(angle.z, 0.0, 1e-6);
	}
} // namespace

// Tests for the notification core.
//
// Every case here maps to a finding from the PR #64 review; the comment on each
// test says which defect it pins down.

#include "test_harness.h"

#include "../GMod-SDK/ImGui/imgui-notify/notify_core.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{
	constexpr int kDismiss = 3000;
	constexpr std::uint64_t kFade = notify::kFadeInOutTimeMs;

	// ------------------------------------------------------------------
	// get_phase(): exact boundaries.  Upstream used `>` comparisons, so the
	// boundary tick belongs to the *earlier* phase -- pinned here so a future
	// refactor to `>=` does not silently shift every transition by one ms.
	// ------------------------------------------------------------------
	TEST(PhaseBoundaries)
	{
		CHECK(notify::PhaseFor(0, kDismiss) == notify::Phase::FadeIn);
		CHECK(notify::PhaseFor(kFade, kDismiss) == notify::Phase::FadeIn);
		CHECK(notify::PhaseFor(kFade + 1, kDismiss) == notify::Phase::Wait);

		CHECK(notify::PhaseFor(kFade + kDismiss, kDismiss) == notify::Phase::Wait);
		CHECK(notify::PhaseFor(kFade + kDismiss + 1, kDismiss) == notify::Phase::FadeOut);

		CHECK(notify::PhaseFor(kFade + kDismiss + kFade, kDismiss) == notify::Phase::FadeOut);
		CHECK(notify::PhaseFor(kFade + kDismiss + kFade + 1, kDismiss) == notify::Phase::Expired);
	}

	// A negative dismiss_time used to make every phase comparison nonsense.
	TEST(PhaseWithOutOfRangeDismissTime)
	{
		CHECK(notify::ClampDismissTime(-5000) == notify::kMinDismissMs);
		CHECK(notify::ClampDismissTime(999999) == notify::kMaxDismissMs);

		// Still a well-ordered lifetime, never an immediate expiry.
		CHECK(notify::PhaseFor(0, -5000) == notify::Phase::FadeIn);
		CHECK(notify::PhaseFor(kFade + notify::kMinDismissMs + kFade + 1, -5000) == notify::Phase::Expired);
	}

	// ------------------------------------------------------------------
	// get_fade_percent(): must stay inside [0,1] whatever the timings.
	// ------------------------------------------------------------------
	TEST(FadePercentIsClampedToUnitRange)
	{
		CHECK_NEAR(notify::FadePercentFor(0, kDismiss), 0.0, 1e-5);
		CHECK_NEAR(notify::FadePercentFor(kFade / 2, kDismiss), 0.5, 1e-2);
		CHECK_NEAR(notify::FadePercentFor(kFade, kDismiss), 1.0, 1e-5);

		CHECK_NEAR(notify::FadePercentFor(kFade + kDismiss / 2, kDismiss), 1.0, 1e-5);

		CHECK_NEAR(notify::FadePercentFor(kFade + kDismiss + kFade / 2, kDismiss), 0.5, 1e-2);
		CHECK_NEAR(notify::FadePercentFor(kFade + kDismiss + kFade, kDismiss), 0.0, 1e-5);

		// Past expiry, and absurdly far past it.
		CHECK_NEAR(notify::FadePercentFor(kFade + kDismiss + kFade + 1, kDismiss), 0.0, 1e-5);
		CHECK_NEAR(notify::FadePercentFor(1000ull * 1000ull, kDismiss), 0.0, 1e-5);

		for (std::uint64_t t = 0; t < 4000; t += 7)
		{
			const float percent = notify::FadePercentFor(t, kDismiss);
			CHECK(percent >= 0.f);
			CHECK(percent <= 1.f);
		}
	}

	// ------------------------------------------------------------------
	// Colours are ImVec4 components, i.e. 0..1 -- not 0..255.
	// ------------------------------------------------------------------
	TEST(ColorsAreNormalised)
	{
		const notify::Type types[] = {
			notify::Type::None, notify::Type::Success, notify::Type::Warning,
			notify::Type::Error, notify::Type::Info
		};

		for (notify::Type type : types)
		{
			const notify::Color color = notify::ColorFor(type);
			CHECK(color.r >= 0.f && color.r <= 1.f);
			CHECK(color.g >= 0.f && color.g <= 1.f);
			CHECK(color.b >= 0.f && color.b <= 1.f);
			CHECK(color.a >= 0.f && color.a <= 1.f);
		}

		CHECK_NEAR(notify::ColorFor(notify::Type::Info).g, 157.0 / 255.0, 1e-5);
	}

	// An out-of-domain enum must still produce a usable colour and title:
	// IM_ASSERT compiles away in release, so it cannot be the only guard.
	TEST(InvalidTypeFallsBackSafely)
	{
		const notify::Type invalid = static_cast<notify::Type>(42);

		CHECK(!notify::IsValidType(invalid));

		const notify::Color color = notify::ColorFor(invalid);
		CHECK_NEAR(color.a, 1.0, 1e-5);
		CHECK_STREQ(notify::DefaultTitleFor(invalid), "");

		// The constructor normalises rather than trusting the caller.
		notify::Toast toast(invalid, 1000);
		CHECK(toast.GetType() == notify::Type::None);
	}

	// ------------------------------------------------------------------
	// Format-string safety.  This is the one that mattered: a player nickname
	// reached ImGui::Text() as a *format*, so "%n" was an arbitrary write.
	// ------------------------------------------------------------------
	TEST(UserTextIsNeverTreatedAsAFormatString)
	{
		const char* hostile = "%s %x %n %% %p %d";

		notify::Toast toast(notify::Type::None, 3000, hostile);
		CHECK_STREQ(toast.GetContent(), hostile);

		notify::Toast other(notify::Type::Info);
		other.SetTitle(hostile);
		other.SetContent(hostile);
		CHECK_STREQ(other.GetTitle(), hostile);
		CHECK_STREQ(other.GetContent(), hostile);

		// And the explicit formatting path keeps the user text as an argument.
		CHECK_STREQ(notify::Format("%s attacked %s", hostile, "bob"),
			std::string(hostile) + " attacked bob");
	}

	// ------------------------------------------------------------------
	// Long input: bounded, and the truncation is visible rather than silent.
	// ------------------------------------------------------------------
	TEST(OverlongTextIsTruncatedNotOverflowed)
	{
		const std::string huge(8192, 'A');

		notify::Toast toast(notify::Type::None, 3000, huge.c_str());
		CHECK(toast.GetContent().size() <= notify::kMaxMessageLength + std::strlen(notify::kTruncationMarker));
		CHECK(toast.GetContent().size() > 0);
		CHECK(toast.GetContent().find(notify::kTruncationMarker) != std::string::npos);

		const std::string formatted = notify::Format("%s", huge.c_str());
		CHECK(formatted.size() <= notify::kMaxMessageLength + std::strlen(notify::kTruncationMarker));
	}

	TEST(EmptyAndNullTextAreHandled)
	{
		notify::Toast toast(notify::Type::Success);
		CHECK_STREQ(toast.GetTitle(), "");
		CHECK_STREQ(toast.GetContent(), "");

		toast.SetContent(nullptr);
		toast.SetTitle(nullptr);
		CHECK_STREQ(toast.GetContent(), "");
		CHECK_STREQ(toast.GetTitle(), "");

		toast.SetContent("");
		CHECK_STREQ(toast.GetContent(), "");

		// A typed toast without a title still renders under its default one.
		CHECK_STREQ(toast.GetDefaultTitle(), "Success");
		CHECK(notify::DefaultTitleFor(notify::Type::None) != nullptr);
	}

	TEST(ControlCharactersAreNeutralised)
	{
		notify::Toast toast(notify::Type::None, 3000, "ab\ncd\tef\x01gh\x7f");
		CHECK_STREQ(toast.GetContent(), "ab cd ef gh ");

		// Multi-byte UTF-8 survives untouched.
		CHECK_STREQ(notify::Sanitize("joueur-\xC3\xA9\xC3\xA8"), "joueur-\xC3\xA9\xC3\xA8");
	}

	// ------------------------------------------------------------------
	// Queue behaviour.
	// ------------------------------------------------------------------

	// The upstream render loop did erase(i) then i++, so the toast shifted into
	// slot i was skipped.  Two expiring back to back left one on screen forever.
	TEST(ConsecutiveExpiredNotificationsAreAllRemoved)
	{
		notify::Queue queue;

		queue.Insert(notify::Toast(notify::Type::None, notify::kMinDismissMs, "expires-1"));
		queue.Insert(notify::Toast(notify::Type::None, notify::kMinDismissMs, "expires-2"));
		queue.Insert(notify::Toast(notify::Type::None, notify::kMinDismissMs, "expires-3"));
		queue.Insert(notify::Toast(notify::Type::None, 30000, "survivor"));
		CHECK_EQ(queue.Size(), static_cast<std::size_t>(4));

		// Past kMinDismissMs + both fade windows.
		std::this_thread::sleep_for(std::chrono::milliseconds(
			notify::kMinDismissMs + 2 * static_cast<int>(notify::kFadeInOutTimeMs) + 100));

		const std::vector<notify::Toast> live = queue.CollectLive();
		CHECK_EQ(live.size(), static_cast<std::size_t>(1));
		CHECK_EQ(queue.Size(), static_cast<std::size_t>(1));
		if (live.size() == 1)
			CHECK_STREQ(live[0].GetContent(), "survivor");
	}

	TEST(RemoveRejectsOutOfBoundsIndex)
	{
		notify::Queue queue;
		queue.Insert(notify::Type::Info, "only");

		CHECK(!queue.Remove(1));
		CHECK(!queue.Remove(99));
		CHECK(!queue.Remove(static_cast<std::size_t>(-1)));
		CHECK_EQ(queue.Size(), static_cast<std::size_t>(1));

		CHECK(queue.Remove(0));
		CHECK_EQ(queue.Size(), static_cast<std::size_t>(0));
		CHECK(!queue.Remove(0));
	}

	// A spammed event must not grow the vector without bound.
	TEST(QueueIsCapped)
	{
		notify::Queue queue;

		for (int i = 0; i < 1000; ++i)
			queue.Insert(notify::Type::Info, notify::Format("message %d", i).c_str(), 30000);

		CHECK(queue.Size() <= notify::kMaxNotifications);

		const std::vector<notify::Toast> live = queue.CollectLive();
		CHECK(live.size() <= notify::kMaxNotifications);
	}

	// Identical toasts inside the dedup window refresh one entry instead of
	// opening a new ImGui window per frame.
	TEST(IdenticalToastsAreDeduplicated)
	{
		notify::Queue queue;

		const std::uint64_t first = queue.Insert(notify::Type::Info, "same message", 30000);
		const std::uint64_t second = queue.Insert(notify::Type::Info, "same message", 30000);

		CHECK_EQ(first, second);
		CHECK_EQ(queue.Size(), static_cast<std::size_t>(1));

		queue.Insert(notify::Type::Info, "other message", 30000);
		CHECK_EQ(queue.Size(), static_cast<std::size_t>(2));
	}

	// Window ids are derived from the toast id, not from its index, so removing
	// one toast does not renumber the windows of the others.
	TEST(ToastIdsAreUniqueAndStable)
	{
		notify::Queue queue;
		const std::uint64_t a = queue.Insert(notify::Type::Info, "a", 30000);
		const std::uint64_t b = queue.Insert(notify::Type::Info, "b", 30000);
		const std::uint64_t c = queue.Insert(notify::Type::Info, "c", 30000);

		CHECK(a != b);
		CHECK(b != c);

		CHECK(queue.RemoveById(b));
		CHECK_EQ(queue.Size(), static_cast<std::size_t>(2));

		const std::vector<notify::Toast> live = queue.CollectLive();
		CHECK_EQ(live.size(), static_cast<std::size_t>(2));
		if (live.size() == 2)
		{
			CHECK_EQ(live[0].GetId(), a);
			CHECK_EQ(live[1].GetId(), c);
		}

		CHECK(!queue.RemoveById(b));
	}

	// Insert() from another thread while the render thread is collecting must
	// not be able to invalidate what the renderer is walking.
	TEST(ConcurrentInsertAndCollectAreSafe)
	{
		notify::Queue queue;
		bool stop = false;

		std::thread producer([&queue, &stop]() {
			for (int i = 0; i < 2000 && !stop; ++i)
				queue.Insert(notify::Type::Info, notify::Format("evt %d", i).c_str(), 30000);
		});

		for (int i = 0; i < 2000; ++i)
		{
			const std::vector<notify::Toast> live = queue.CollectLive();
			for (const notify::Toast& toast : live)
				CHECK(toast.GetContent().size() <= notify::kMaxMessageLength + 8);
		}

		stop = true;
		producer.join();

		CHECK(queue.Size() <= notify::kMaxNotifications);
	}
} // namespace

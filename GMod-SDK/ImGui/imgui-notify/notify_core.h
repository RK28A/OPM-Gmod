// notify_core.h -- UI-independent core of the toast notification system.
//
// This header deliberately pulls in neither <Windows.h>, ImGui nor DirectX, so
// that the timing, phase, text handling and queue logic can be compiled and
// unit tested on any toolchain (see tests/notify_tests.cpp).  The ImGui binding
// lives in imgui_notify.h and is the only part that needs a render context.
//
// Threading contract
// ------------------
//   notify::Queue is the only shared state and it is internally synchronised.
//   Producers (game event callbacks, which the engine may invoke from a thread
//   other than the render thread) call Insert().  The render thread calls
//   CollectLive(), which hands back a *snapshot by value*: the renderer never
//   holds a pointer or index into the live vector, so a concurrent Insert()
//   that reallocates the vector cannot invalidate anything it is using.

#ifndef GMOD_SDK_NOTIFY_CORE_H
#define GMOD_SDK_NOTIFY_CORE_H

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace notify
{
	// ---------------------------------------------------------------------
	// Tunables.  These used to be preprocessor macros; constexpr keeps them
	// typed and scoped instead of leaking into every translation unit.
	// ---------------------------------------------------------------------
	inline constexpr std::size_t kMaxMessageLength = 1024;   // per title / content
	inline constexpr std::size_t kMaxNotifications = 32;     // hard queue cap
	inline constexpr std::uint64_t kFadeInOutTimeMs = 150;
	inline constexpr int kDefaultDismissMs = 3000;
	inline constexpr int kMinDismissMs = 250;
	inline constexpr int kMaxDismissMs = 60 * 1000;
	inline constexpr std::uint64_t kDedupWindowMs = 500;     // identical toasts merge
	inline constexpr float kOpacity = 1.0f;
	inline constexpr const char* kTruncationMarker = "[...]";

	enum class Type
	{
		None,
		Success,
		Warning,
		Error,
		Info,
		Count
	};

	enum class Phase
	{
		FadeIn,
		Wait,
		FadeOut,
		Expired
	};

	// Normalised colour, components in [0,1] -- ImVec4's actual domain.  The
	// upstream library built these from 0-255 literals, which ImGui silently
	// interprets as enormous out-of-range floats.
	struct Color
	{
		float r = 1.f;
		float g = 1.f;
		float b = 1.f;
		float a = 1.f;
	};

	[[nodiscard]] inline bool IsValidType(Type type) noexcept
	{
		return type >= Type::None && type < Type::Count;
	}

	// Returns by value: the upstream version returned `const ImVec4&` bound to a
	// braced temporary, so every caller read freed stack memory.
	[[nodiscard]] inline Color ColorFor(Type type) noexcept
	{
		switch (type)
		{
		case Type::Success: return Color{ 0.f, 1.f, 0.f, 1.f };                 // green
		case Type::Warning: return Color{ 1.f, 1.f, 0.f, 1.f };                 // yellow
		case Type::Error:   return Color{ 1.f, 0.f, 0.f, 1.f };                 // red
		case Type::Info:    return Color{ 0.f, 157.f / 255.f, 1.f, 1.f };       // blue
		case Type::None:
		case Type::Count:
		default:            return Color{ 1.f, 1.f, 1.f, 1.f };                 // white fallback
		}
	}

	// Never returns nullptr: callers treat "" as "no title", which removes a
	// whole class of null-deref/strlen-on-null paths in the renderer.
	[[nodiscard]] inline const char* DefaultTitleFor(Type type) noexcept
	{
		switch (type)
		{
		case Type::Success: return "Success";
		case Type::Warning: return "Warning";
		case Type::Error:   return "Error";
		case Type::Info:    return "Info";
		case Type::None:
		case Type::Count:
		default:            return "";
		}
	}

	[[nodiscard]] inline int ClampDismissTime(int dismiss_ms) noexcept
	{
		if (dismiss_ms < kMinDismissMs)
			return kMinDismissMs;
		if (dismiss_ms > kMaxDismissMs)
			return kMaxDismissMs;
		return dismiss_ms;
	}

	// Pure, so the boundaries (0ms, fade-in edge, wait, fade-out edge, expiry)
	// can be asserted directly without waiting on a real clock.
	[[nodiscard]] inline Phase PhaseFor(std::uint64_t elapsed_ms, int dismiss_ms) noexcept
	{
		const std::uint64_t dismiss = static_cast<std::uint64_t>(ClampDismissTime(dismiss_ms));

		if (elapsed_ms > kFadeInOutTimeMs + dismiss + kFadeInOutTimeMs)
			return Phase::Expired;
		if (elapsed_ms > kFadeInOutTimeMs + dismiss)
			return Phase::FadeOut;
		if (elapsed_ms > kFadeInOutTimeMs)
			return Phase::Wait;
		return Phase::FadeIn;
	}

	// Always in [0, kOpacity]: rounding at the phase boundaries could otherwise
	// hand ImGui a slightly negative or >1 alpha.
	[[nodiscard]] inline float FadePercentFor(std::uint64_t elapsed_ms, int dismiss_ms) noexcept
	{
		const int dismiss = ClampDismissTime(dismiss_ms);
		const float elapsed = static_cast<float>(elapsed_ms);
		const float fade = static_cast<float>(kFadeInOutTimeMs);

		float percent = 1.f;
		switch (PhaseFor(elapsed_ms, dismiss))
		{
		case Phase::FadeIn:
			percent = elapsed / fade;
			break;
		case Phase::FadeOut:
			percent = 1.f - ((elapsed - fade - static_cast<float>(dismiss)) / fade);
			break;
		case Phase::Expired:
			percent = 0.f;
			break;
		case Phase::Wait:
		default:
			percent = 1.f;
			break;
		}

		percent = std::max(0.f, std::min(1.f, percent));
		return percent * kOpacity;
	}

	// ---------------------------------------------------------------------
	// Text handling
	// ---------------------------------------------------------------------

	// Replaces control characters (newlines and tabs included) with spaces and
	// truncates to `max_length`.  Player-controlled strings reach the renderer
	// through here, so a name full of \n or \b cannot wreck the toast layout.
	[[nodiscard]] inline std::string Sanitize(const char* text, std::size_t max_length = kMaxMessageLength)
	{
		if (text == nullptr)
			return std::string();

		std::string out;
		out.reserve(std::min(max_length, std::strlen(text)));

		for (const char* it = text; *it != '\0'; ++it)
		{
			if (out.size() >= max_length)
			{
				out += kTruncationMarker;
				break;
			}

			const unsigned char ch = static_cast<unsigned char>(*it);
			// Keep printable ASCII and anything >= 0x80 (UTF-8 continuation and
			// lead bytes) so non-latin names survive intact.
			out += (ch < 0x20 || ch == 0x7F) ? ' ' : *it;
		}

		return out;
	}

	[[nodiscard]] inline std::string Sanitize(const std::string& text, std::size_t max_length = kMaxMessageLength)
	{
		return Sanitize(text.c_str(), max_length);
	}

	// vsnprintf wrapper that reports rather than hides truncation, and never
	// relies on the buffer being NUL-terminated by a failed call.
	[[nodiscard]] inline std::string FormatV(const char* format, va_list args)
	{
		if (format == nullptr)
			return std::string();

		va_list measure;
		va_copy(measure, args);
		const int needed = std::vsnprintf(nullptr, 0, format, measure);
		va_end(measure);

		if (needed < 0)
			return std::string(); // encoding error: give the caller an empty string, not garbage

		const std::size_t length = static_cast<std::size_t>(needed);
		std::string out(length, '\0');
		if (length > 0)
			std::vsnprintf(&out[0], length + 1, format, args);

		if (out.size() > kMaxMessageLength)
		{
			out.resize(kMaxMessageLength);
			out += kTruncationMarker;
		}

		return out;
	}

	[[nodiscard]] inline std::string Format(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		std::string out = FormatV(format, args);
		va_end(args);
		return out;
	}

	// ---------------------------------------------------------------------
	// Clock
	// ---------------------------------------------------------------------

	// steady_clock rather than GetTickCount64(): monotonic, standard, and it
	// keeps this header free of <Windows.h>.
	[[nodiscard]] inline std::uint64_t NowMs() noexcept
	{
		using namespace std::chrono;
		static const steady_clock::time_point origin = steady_clock::now();
		return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now() - origin).count());
	}

	// ---------------------------------------------------------------------
	// Toast
	// ---------------------------------------------------------------------

	class Toast
	{
	public:
		explicit Toast(Type type = Type::None, int dismiss_ms = kDefaultDismissMs)
			: type_(IsValidType(type) ? type : Type::None)
			, dismiss_ms_(ClampDismissTime(dismiss_ms))
			, creation_ms_(NowMs())
			, id_(NextId())
		{
		}

		// `content` is TEXT, never a format string.  Taking it as a format is
		// how a player nickname containing %n became an arbitrary-write.
		Toast(Type type, int dismiss_ms, const char* content)
			: Toast(type, dismiss_ms)
		{
			SetContent(content);
		}

		void SetType(Type type) noexcept { type_ = IsValidType(type) ? type : Type::None; }

		void SetTitle(const char* text) { title_ = Sanitize(text); }
		void SetContent(const char* text) { content_ = Sanitize(text); }

		// Explicit *Format overloads for the cases where a literal format plus
		// arguments really is what the caller wants.
		void SetTitleFormat(const char* format, ...)
		{
			va_list args;
			va_start(args, format);
			title_ = Sanitize(FormatV(format, args));
			va_end(args);
		}

		void SetContentFormat(const char* format, ...)
		{
			va_list args;
			va_start(args, format);
			content_ = Sanitize(FormatV(format, args));
			va_end(args);
		}

		[[nodiscard]] Type GetType() const noexcept { return type_; }
		[[nodiscard]] const std::string& GetTitle() const noexcept { return title_; }
		[[nodiscard]] const std::string& GetContent() const noexcept { return content_; }
		[[nodiscard]] int GetDismissTime() const noexcept { return dismiss_ms_; }
		[[nodiscard]] std::uint64_t GetId() const noexcept { return id_; }
		[[nodiscard]] Color GetColor() const noexcept { return ColorFor(type_); }
		[[nodiscard]] const char* GetDefaultTitle() const noexcept { return DefaultTitleFor(type_); }

		[[nodiscard]] std::uint64_t GetElapsedMs() const noexcept
		{
			const std::uint64_t now = NowMs();
			return (now >= creation_ms_) ? now - creation_ms_ : 0;
		}

		// The *At() variants keep the time-dependent logic testable.
		[[nodiscard]] Phase GetPhaseAt(std::uint64_t elapsed_ms) const noexcept { return PhaseFor(elapsed_ms, dismiss_ms_); }
		[[nodiscard]] float GetFadePercentAt(std::uint64_t elapsed_ms) const noexcept { return FadePercentFor(elapsed_ms, dismiss_ms_); }

		[[nodiscard]] Phase GetPhase() const noexcept { return GetPhaseAt(GetElapsedMs()); }
		[[nodiscard]] float GetFadePercent() const noexcept { return GetFadePercentAt(GetElapsedMs()); }

		[[nodiscard]] bool IsExpired() const noexcept { return GetPhase() == Phase::Expired; }

		// Restarts the lifetime; used when an identical toast is re-inserted.
		void Refresh() noexcept { creation_ms_ = NowMs(); }

		[[nodiscard]] bool HasSameContentAs(const Toast& other) const noexcept
		{
			return type_ == other.type_ && title_ == other.title_ && content_ == other.content_;
		}

	private:
		[[nodiscard]] static std::uint64_t NextId() noexcept
		{
			// Stable, monotonic identity so ImGui window IDs do not shift when a
			// toast in the middle of the list disappears.
			static std::uint64_t counter = 0;
			return ++counter;
		}

		Type type_ = Type::None;
		std::string title_;
		std::string content_;
		int dismiss_ms_ = kDefaultDismissMs;
		std::uint64_t creation_ms_ = 0;
		std::uint64_t id_ = 0;
	};

	// ---------------------------------------------------------------------
	// Queue
	// ---------------------------------------------------------------------

	class Queue
	{
	public:
		// Returns the id of the live toast (a fresh one, or the refreshed
		// duplicate).  Never grows past kMaxNotifications.
		std::uint64_t Insert(Toast toast)
		{
			std::lock_guard<std::mutex> lock(mutex_);

			// Rate limit: an event firing every frame refreshes one toast
			// instead of stacking a new ImGui window per frame.
			for (Toast& existing : toasts_)
			{
				if (existing.HasSameContentAs(toast) && existing.GetElapsedMs() <= kDedupWindowMs)
				{
					existing.Refresh();
					return existing.GetId();
				}
			}

			// Drop expired entries before enforcing the cap so a burst of new
			// events does not evict toasts that are still visible.
			toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
				[](const Toast& t) { return t.IsExpired(); }), toasts_.end());

			while (toasts_.size() >= kMaxNotifications)
				toasts_.erase(toasts_.begin());

			const std::uint64_t id = toast.GetId();
			toasts_.push_back(std::move(toast));
			return id;
		}

		std::uint64_t Insert(Type type, const char* content, int dismiss_ms = kDefaultDismissMs)
		{
			return Insert(Toast(type, dismiss_ms, content));
		}

		// Bounds checked -- the upstream RemoveNotification() fed any index
		// straight into vector::erase().
		bool Remove(std::size_t index)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (index >= toasts_.size())
				return false;
			toasts_.erase(toasts_.begin() + static_cast<std::ptrdiff_t>(index));
			return true;
		}

		bool RemoveById(std::uint64_t id)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			const auto it = std::find_if(toasts_.begin(), toasts_.end(),
				[id](const Toast& t) { return t.GetId() == id; });
			if (it == toasts_.end())
				return false;
			toasts_.erase(it);
			return true;
		}

		void Clear()
		{
			std::lock_guard<std::mutex> lock(mutex_);
			toasts_.clear();
		}

		[[nodiscard]] std::size_t Size() const
		{
			std::lock_guard<std::mutex> lock(mutex_);
			return toasts_.size();
		}

		// Drops every expired toast and returns a copy of the rest.
		//
		// std::remove_if does the erasing in one pass, which is what the
		// upstream render loop got wrong: it called erase(i) and then i++, so
		// the element shifted into slot i was never looked at and two toasts
		// expiring back to back left one of them stuck on screen.
		[[nodiscard]] std::vector<Toast> CollectLive()
		{
			std::lock_guard<std::mutex> lock(mutex_);
			toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
				[](const Toast& t) { return t.IsExpired(); }), toasts_.end());
			return toasts_; // by value: the renderer cannot be invalidated by a concurrent Insert
		}

	private:
		mutable std::mutex mutex_;
		std::vector<Toast> toasts_;
	};

} // namespace notify

#endif // GMOD_SDK_NOTIFY_CORE_H

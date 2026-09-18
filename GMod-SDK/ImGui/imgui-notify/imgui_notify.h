// imgui-notify -- ImGui binding for the toast notification system.
//
// Based on imgui-notify by patrickcjk (MIT):
//     https://github.com/patrickcjk/imgui-notify
// See NOTICE.md in this directory for the upstream revision and the list of
// local changes.
//
// Everything that does not need a render context lives in notify_core.h; this
// file is only the drawing layer.  It intentionally does not include
// <Windows.h>, imgui_impl_dx9.h or imgui_impl_win32.h -- the original did, and
// that pulled the whole platform backend into every translation unit that
// wanted to raise a notification.

#ifndef GMOD_SDK_IMGUI_NOTIFY_H
#define GMOD_SDK_IMGUI_NOTIFY_H

#include "../imgui.h"
#include "notify_core.h"
#include "font_awesome_5.h"
#include "fa_solid_900.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace ImGui
{
	namespace notify_detail
	{
		inline constexpr float kPaddingX = 20.f;         // right-hand screen padding
		inline constexpr float kPaddingY = 20.f;         // bottom screen padding
		inline constexpr float kPaddingMessageY = 10.f;  // gap between two toasts
		inline constexpr float kMinWrapWidth = 180.f;
		inline constexpr float kMaxWrapWidth = 600.f;

		inline constexpr ImGuiWindowFlags kToastFlags =
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

		// Comment out to drop the rule between title and content.
		inline constexpr bool kUseSeparator = true;

		inline ImVec4 ToImVec4(const notify::Color& color, float alpha)
		{
			return ImVec4(color.r, color.g, color.b, alpha);
		}

		inline const char* IconFor(notify::Type type)
		{
			switch (type)
			{
			case notify::Type::Success: return ICON_FA_CHECK_CIRCLE;
			case notify::Type::Warning: return ICON_FA_EXCLAMATION_TRIANGLE;
			case notify::Type::Error:   return ICON_FA_TIMES_CIRCLE;
			case notify::Type::Info:    return ICON_FA_INFO_CIRCLE;
			case notify::Type::None:
			case notify::Type::Count:
			default:                    return ""; // never nullptr: "" means "no icon"
			}
		}
	} // namespace notify_detail

	// The single queue every producer pushes into.  Internally synchronised;
	// see the threading contract at the top of notify_core.h.
	inline notify::Queue& NotificationQueue()
	{
		static notify::Queue queue;
		return queue;
	}

	inline void InsertNotification(notify::Toast toast)
	{
		NotificationQueue().Insert(std::move(toast));
	}

	// `content` is plain text, never a printf format.
	inline void InsertNotification(notify::Type type, const char* content,
		int dismiss_ms = notify::kDefaultDismissMs)
	{
		NotificationQueue().Insert(type, content, dismiss_ms);
	}

	inline bool RemoveNotification(std::size_t index)
	{
		return NotificationQueue().Remove(index);
	}

	/// Render every live toast.  Call once per frame, between NewFrame() and
	/// Render(), with a valid ImGui context.
	inline void RenderNotifications()
	{
		// An event can fire before the overlay has a context, or after a DX9
		// device reset tore it down; bail out instead of dereferencing it.
		if (GetCurrentContext() == nullptr)
			return;

		const ImGuiIO& io = GetIO();
		if (io.DisplaySize.x <= 0.f || io.DisplaySize.y <= 0.f)
			return;

		// Snapshot by value: expired toasts are dropped inside the queue in a
		// single pass, and nothing below holds an index or pointer into the
		// live vector, so a concurrent InsertNotification() is harmless.
		const std::vector<notify::Toast> toasts = NotificationQueue().CollectLive();

		// Derived from the actual display size rather than Globals::screenWidth,
		// which is only set once the game reports a resolution.
		float wrap_width = io.DisplaySize.x / 3.f;
		// std::clamp, not std::max(std::min(...)): <Windows.h>'s min()/max()
		// macros are live in this translation unit and would break a bare
		// std::min/std::max (MSVC C2589).  clamp has no colliding macro.
		wrap_width = std::clamp(wrap_width, notify_detail::kMinWrapWidth, notify_detail::kMaxWrapWidth);

		float height = 0.f;

		for (const notify::Toast& toast : toasts)
		{
			const float opacity = toast.GetFadePercent();
			const ImVec4 text_color = notify_detail::ToImVec4(toast.GetColor(), opacity);

			const char* icon = notify_detail::IconFor(toast.GetType());
			const std::string& title = toast.GetTitle();
			const std::string& content = toast.GetContent();
			const char* default_title = toast.GetDefaultTitle();

			const bool has_icon = icon[0] != '\0';
			const bool has_title = !title.empty();
			const bool has_default_title = default_title[0] != '\0';
			const bool has_content = !content.empty();

			// Window id keyed on the toast's own id, not on its position in the
			// list: removing a toast used to renumber every window after it,
			// which reset ImGui's per-window state for the survivors.
			char window_name[64];
			std::snprintf(window_name, sizeof(window_name), "##TOAST%llu",
				static_cast<unsigned long long>(toast.GetId()));

			SetNextWindowBgAlpha(opacity);
			SetNextWindowPos(
				ImVec2(io.DisplaySize.x - notify_detail::kPaddingX,
					io.DisplaySize.y - notify_detail::kPaddingY - height),
				ImGuiCond_Always, ImVec2(1.0f, 1.0f));

			// Begin() must always be matched by End(), including when it returns
			// false -- these flags never collapse the window, but keep the pair
			// unconditional so an early return can never leak the window stack.
			Begin(window_name, nullptr, notify_detail::kToastFlags);
			{
				PushTextWrapPos(wrap_width);

				bool rendered_header = false;

				if (has_icon)
				{
					// "%s" guard: TextColored is varargs, so passing the icon
					// directly makes it the format string.
					TextColored(text_color, "%s", icon);
					rendered_header = true;
				}

				// TextUnformatted, not Text(): `title` and `content` can carry a
				// player nickname, and Text() would take it as a printf format.
				if (has_title)
				{
					if (has_icon)
						SameLine();

					TextUnformatted(title.c_str());
					rendered_header = true;
				}
				else if (has_default_title)
				{
					if (has_icon)
						SameLine();

					TextUnformatted(default_title);
					rendered_header = true;
				}

				if (rendered_header && has_content)
				{
					// Nudge the content down so the header reads as vertically
					// centred against the icon.
					SetCursorPosY(GetCursorPosY() + 5.f);

					if (notify_detail::kUseSeparator)
						Separator();
				}

				if (has_content)
					TextUnformatted(content.c_str());

				PopTextWrapPos();
			}
			height += GetWindowHeight() + notify_detail::kPaddingMessageY;
			End();
		}
	}

	/// Merge the Font Awesome glyph ranges into the most recently added font.
	/// Must be called once, during ImGui initialisation, right after the font it
	/// should merge into.
	///
	/// With font_data_owned_by_atlas = false the atlas does *not* copy or free
	/// the TTF bytes, so they must outlive it.  fa_solid_900 is a namespace-scope
	/// array with static storage duration, which satisfies that for the lifetime
	/// of the module -- do not pass a heap or stack buffer here.
	///
	/// Returns the merged font, or nullptr if the merge could not happen.
	inline ImFont* MergeIconsWithLatestFont(float font_size, bool font_data_owned_by_atlas = false)
	{
		if (GetCurrentContext() == nullptr)
			return nullptr;

		ImFontAtlas* atlas = GetIO().Fonts;
		if (atlas == nullptr)
			return nullptr;

		// MergeMode needs a font already in the atlas to merge into.
		if (atlas->Fonts.Size == 0)
			return nullptr;

		static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		icons_config.PixelSnapH = true;
		icons_config.FontDataOwnedByAtlas = font_data_owned_by_atlas;

		ImFont* font = atlas->AddFontFromMemoryTTF(
			const_cast<void*>(static_cast<const void*>(fa_solid_900)),
			static_cast<int>(sizeof(fa_solid_900)), font_size, &icons_config, icons_ranges);

		IM_ASSERT(font != nullptr && "Font Awesome merge failed");
		return font;
	}
} // namespace ImGui

// ---------------------------------------------------------------------------
// Source compatibility with the upstream API.
//
// The setters were deliberately NOT aliased: set_title/set_content took a
// printf format, which is exactly the vulnerability this rewrite removes.  Old
// call sites therefore fail to compile and have to be looked at, instead of
// silently keeping the unsafe behaviour.  Use SetTitle/SetContent for text and
// SetTitleFormat/SetContentFormat for a literal format plus arguments.
// ---------------------------------------------------------------------------
using ImGuiToast = notify::Toast;

inline constexpr notify::Type ImGuiToastType_None = notify::Type::None;
inline constexpr notify::Type ImGuiToastType_Success = notify::Type::Success;
inline constexpr notify::Type ImGuiToastType_Warning = notify::Type::Warning;
inline constexpr notify::Type ImGuiToastType_Error = notify::Type::Error;
inline constexpr notify::Type ImGuiToastType_Info = notify::Type::Info;

#endif // GMOD_SDK_IMGUI_NOTIFY_H

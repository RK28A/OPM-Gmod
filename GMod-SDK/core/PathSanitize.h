// PathSanitize.h -- filename hardening for the script dumper.
//
// The names ScriptDumper.h writes to disk come off the wire: they are the
// paths the *server* passes to RunStringEx.  The sanitiser it used to carry
// (SanitizePath, whose own comment read "hm that's bad") replaced the obvious
// separators and `..`, and stopped there.  It let through:
//
//   * Windows reserved device names -- CON, PRN, AUX, NUL, COM1-9, LPT1-9.
//     Opening "CON" for writing does not create a file, it writes to the
//     console; "COM1" talks to a serial port.  An extension does not help:
//     "CON.lua" still resolves to the device.
//   * Trailing dots and spaces, which Windows strips silently, so "a.lua." and
//     "a.lua" are the same file -- enough to escape a directory that was
//     supposed to be distinct.
//   * `*`, and every other character outside the small set below.
//   * Unbounded length, so a long enough name pushes the full path past
//     MAX_PATH and the write fails in a way nothing reports.
//
// A whitelist is the only defensible answer for externally supplied names, so
// that is what this is.  No Windows dependency, so tests/pathsanitize_tests.cpp
// exercises it directly.

#ifndef GMOD_SDK_PATH_SANITIZE_H
#define GMOD_SDK_PATH_SANITIZE_H

#include <cstddef>
#include <string>

namespace pathsafe
{
	// Long enough for a readable script name, short enough that a handful of
	// nested components cannot approach MAX_PATH.
	inline constexpr std::size_t kMaxComponentLength = 64;

	// Substituted for any rejected character, and used as the whole component
	// when nothing survives.
	inline constexpr char kReplacement = '_';

	[[nodiscard]] inline bool IsAllowed(char c) noexcept
	{
		return (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z')
			|| (c >= '0' && c <= '9')
			|| c == '.' || c == '_' || c == '-';
	}

	[[nodiscard]] inline char ToLowerAscii(char c) noexcept
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}

	// True for the MS-DOS device names Windows still resolves in every
	// directory.  Compared against the stem (the part before the first dot),
	// case-insensitively, because "con.lua", "CON.TXT" and "Con" all hit the
	// same device.
	[[nodiscard]] inline bool IsReservedDeviceName(const std::string& component) noexcept
	{
		std::string stem;
		for (const char c : component)
		{
			if (c == '.')
				break;

			stem += ToLowerAscii(c);
		}

		if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul")
			return true;

		// COM0-COM9 and LPT0-LPT9.  COM0/LPT0 are not real devices on current
		// Windows but they were, and rejecting them costs nothing.
		if (stem.size() == 4 && stem[3] >= '0' && stem[3] <= '9')
		{
			if (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0)
				return true;
		}

		return false;
	}

	// Turns one externally supplied path component into something that is safe
	// to concatenate into a path.  Never returns an empty string, never returns
	// "." or "..", never returns a device name, never exceeds
	// kMaxComponentLength.
	[[nodiscard]] inline std::string SanitizeComponent(const std::string& input)
	{
		std::string out;
		out.reserve(input.size() < kMaxComponentLength ? input.size() : kMaxComponentLength);

		for (const char c : input)
		{
			if (out.size() >= kMaxComponentLength)
				break;

			// Control characters and everything outside the whitelist collapse
			// to the replacement rather than being dropped, so two distinct
			// names cannot sanitise to the same string just by deletion.
			out += IsAllowed(c) ? c : kReplacement;
		}

		// Windows strips trailing dots and spaces when resolving a path, so a
		// component ending in one is not the component it looks like.  (Spaces
		// are already gone -- they are not in the whitelist -- but the rule is
		// written out so it stays correct if the whitelist ever grows.)
		while (!out.empty() && (out.back() == '.' || out.back() == ' '))
			out.pop_back();

		// Leading dots would make "." and ".." reachable again, and hide the
		// file on the platforms this may later be ported to.
		std::size_t firstKept = 0;
		while (firstKept < out.size() && (out[firstKept] == '.' || out[firstKept] == ' '))
			++firstKept;

		out.erase(0, firstKept);

		if (out.empty())
			return std::string(1, kReplacement);

		if (IsReservedDeviceName(out))
			out.insert(out.begin(), kReplacement);

		// The insert above can push a maximum-length component one over.
		if (out.size() > kMaxComponentLength)
			out.resize(kMaxComponentLength);

		return out;
	}
} // namespace pathsafe

#endif // GMOD_SDK_PATH_SANITIZE_H

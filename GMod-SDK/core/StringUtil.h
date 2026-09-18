// StringUtil.h -- bounded copies for the places that read C strings owned by
// the engine or by a file on disk.
//
// Three call sites motivated this, all of them unbounded strcpy into a fixed
// buffer:
//
//   * ConVarSpoofing.h copies ConVar::pszName and ::pszValueStr, which are
//     engine-owned and not length-checked, into char[128] members -- and then
//     copies them back out again in the destructor.
//   * Executor.h copies the whole contents of a user-picked .lua file into
//     Settings::ScriptInput (char[131070]) with no size check at all.
//
// CopyBounded always NUL-terminates and reports truncation, so a caller can
// tell the difference between "copied" and "silently cut short" -- which
// matters for ConVarSpoofing, where writing a truncated name back into the
// engine's own ConVar would corrupt it.
//
// No Windows or SDK dependency: tests/stringutil_tests.cpp drives it directly.

#ifndef GMOD_SDK_STRING_UTIL_H
#define GMOD_SDK_STRING_UTIL_H

#include <cstddef>
#include <string>

namespace strutil
{
	// Copies `source` into `dest` (capacity `destSize`, including the
	// terminator).  Returns false when the source did not fit, or when the
	// arguments are unusable; `dest` is always left NUL-terminated if there is
	// room for a terminator at all.
	inline bool CopyBounded(char* dest, std::size_t destSize, const char* source) noexcept
	{
		if (!dest || destSize == 0)
			return false;

		if (!source)
		{
			dest[0] = '\0';
			return false;
		}

		std::size_t i = 0;
		for (; i + 1 < destSize && source[i] != '\0'; ++i)
			dest[i] = source[i];

		dest[i] = '\0';

		return source[i] == '\0'; // false means the source was longer than the buffer
	}

	inline bool CopyBounded(char* dest, std::size_t destSize, const std::string& source) noexcept
	{
		return CopyBounded(dest, destSize, source.c_str());
	}

	// Reads an engine-owned C string that is not guaranteed to be terminated
	// inside `maxLength` bytes.  Returns an empty string for nullptr.
	[[nodiscard]] inline std::string FromBounded(const char* source, std::size_t maxLength)
	{
		if (!source)
			return std::string();

		std::size_t length = 0;
		while (length < maxLength && source[length] != '\0')
			++length;

		return std::string(source, length);
	}
} // namespace strutil

#endif // GMOD_SDK_STRING_UTIL_H

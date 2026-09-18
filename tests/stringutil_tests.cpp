// Tests for core/StringUtil.h.

#include "test_harness.h"

#include "../GMod-SDK/core/StringUtil.h"

#include <cstring>

using strutil::CopyBounded;
using strutil::FromBounded;

TEST(CopyBoundedCopiesWhenItFits)
{
	char buffer[16];
	CHECK(CopyBounded(buffer, sizeof(buffer), "hello"));
	CHECK_STREQ(buffer, "hello");
}

TEST(CopyBoundedFillsTheBufferExactly)
{
	char buffer[6];
	// Five characters plus the terminator is exactly sizeof(buffer).
	CHECK(CopyBounded(buffer, sizeof(buffer), "abcde"));
	CHECK_STREQ(buffer, "abcde");
	CHECK_EQ(buffer[5], '\0');
}

TEST(CopyBoundedTruncatesAndReportsIt)
{
	char buffer[4];
	CHECK(!CopyBounded(buffer, sizeof(buffer), "abcdef"));
	CHECK_STREQ(buffer, "abc");
	CHECK_EQ(buffer[3], '\0');
}

TEST(CopyBoundedAlwaysTerminates)
{
	char buffer[8];
	std::memset(buffer, 'X', sizeof(buffer));

	CopyBounded(buffer, sizeof(buffer), "abcdefghijkl");
	CHECK_EQ(buffer[7], '\0');
	CHECK_EQ(std::strlen(buffer), static_cast<std::size_t>(7));
}

TEST(CopyBoundedHandlesDegenerateArguments)
{
	char buffer[4] = { 'a', 'b', 'c', '\0' };

	// A null source empties the buffer rather than leaving it stale.
	CHECK(!CopyBounded(buffer, sizeof(buffer), static_cast<const char*>(nullptr)));
	CHECK_EQ(buffer[0], '\0');

	// A null destination, or no room at all, is a refusal, not a write.
	CHECK(!CopyBounded(nullptr, 16, "abc"));
	CHECK(!CopyBounded(buffer, 0, "abc"));
}

TEST(CopyBoundedCopiesAnEmptyString)
{
	char buffer[4] = { 'a', 'b', 'c', '\0' };
	CHECK(CopyBounded(buffer, sizeof(buffer), ""));
	CHECK_EQ(buffer[0], '\0');
}

TEST(CopyBoundedAcceptsStdString)
{
	char buffer[8];
	CHECK(CopyBounded(buffer, sizeof(buffer), std::string("abc")));
	CHECK_STREQ(buffer, "abc");

	CHECK(!CopyBounded(buffer, sizeof(buffer), std::string(64, 'z')));
	CHECK_EQ(std::strlen(buffer), static_cast<std::size_t>(7));
}

// The engine hands out fixed-size character fields it is not obliged to
// terminate -- studiohdr_t::name and player_info_s::name among them.
TEST(FromBoundedStopsAtTheLimitWithoutATerminator)
{
	const char unterminated[4] = { 'a', 'b', 'c', 'd' };
	CHECK_STREQ(FromBounded(unterminated, sizeof(unterminated)), "abcd");
}

TEST(FromBoundedStopsAtAnEarlyTerminator)
{
	const char terminated[8] = { 'a', 'b', '\0', 'X', 'Y', 'Z', 'W', 'V' };
	CHECK_STREQ(FromBounded(terminated, sizeof(terminated)), "ab");
}

TEST(FromBoundedHandlesDegenerateArguments)
{
	CHECK_STREQ(FromBounded(nullptr, 32), "");
	CHECK_STREQ(FromBounded("abc", 0), "");
}

// Tests for core/PathSanitize.h.
//
// The input to this code is a filename chosen by the game server, so every
// case below is something a hostile (or merely careless) server can send.

#include "test_harness.h"

#include "../GMod-SDK/core/PathSanitize.h"

using pathsafe::IsReservedDeviceName;
using pathsafe::SanitizeComponent;
using pathsafe::kMaxComponentLength;

TEST(OrdinaryNamesArePreserved)
{
	CHECK_STREQ(SanitizeComponent("autorun.lua"), "autorun.lua");
	CHECK_STREQ(SanitizeComponent("shared_init.lua"), "shared_init.lua");
	CHECK_STREQ(SanitizeComponent("cl-init-2.lua"), "cl-init-2.lua");
	CHECK_STREQ(SanitizeComponent("ABC123"), "ABC123");
}

TEST(SeparatorsAndTraversalCannotSurvive)
{
	CHECK_STREQ(SanitizeComponent(".."), "_");
	CHECK_STREQ(SanitizeComponent("."), "_");
	CHECK_STREQ(SanitizeComponent("../../windows/system32"), "_.._windows_system32");
	CHECK_STREQ(SanitizeComponent("a/b"), "a_b");
	CHECK_STREQ(SanitizeComponent("a\\b"), "a_b");
	CHECK_STREQ(SanitizeComponent("C:evil"), "C_evil");
}

TEST(WildcardsAndQuotingCharactersAreReplaced)
{
	CHECK_STREQ(SanitizeComponent("a*b"), "a_b");
	CHECK_STREQ(SanitizeComponent("a?b"), "a_b");
	CHECK_STREQ(SanitizeComponent("a\"b"), "a_b");
	CHECK_STREQ(SanitizeComponent("a<b>c"), "a_b_c");
	CHECK_STREQ(SanitizeComponent("a|b"), "a_b");
	CHECK_STREQ(SanitizeComponent("a b"), "a_b");
}

TEST(ControlCharactersAreReplacedNotDropped)
{
	// Dropping them would let "a\x01b" and "ab" collide.
	CHECK_STREQ(SanitizeComponent(std::string("a\x01" "b")), "a_b");
	CHECK_STREQ(SanitizeComponent(std::string("a\nb")), "a_b");
	CHECK_STREQ(SanitizeComponent(std::string("a\tb")), "a_b");

	// An embedded NUL ends the std::string literal's content only if the caller
	// built it that way; built explicitly, the byte is replaced like any other.
	CHECK_STREQ(SanitizeComponent(std::string("a\0b", 3)), "a_b");
}

TEST(ReservedDeviceNamesAreDetected)
{
	CHECK(IsReservedDeviceName("con"));
	CHECK(IsReservedDeviceName("CON"));
	CHECK(IsReservedDeviceName("CoN"));
	CHECK(IsReservedDeviceName("con.lua"));
	CHECK(IsReservedDeviceName("CON.TXT"));
	CHECK(IsReservedDeviceName("prn"));
	CHECK(IsReservedDeviceName("aux"));
	CHECK(IsReservedDeviceName("nul"));
	CHECK(IsReservedDeviceName("com1"));
	CHECK(IsReservedDeviceName("COM9.lua"));
	CHECK(IsReservedDeviceName("lpt1"));
	CHECK(IsReservedDeviceName("LPT0"));

	CHECK(!IsReservedDeviceName("console"));
	CHECK(!IsReservedDeviceName("conf.lua"));
	CHECK(!IsReservedDeviceName("com"));
	CHECK(!IsReservedDeviceName("com10"));
	CHECK(!IsReservedDeviceName("lptx"));
	CHECK(!IsReservedDeviceName("autorun.lua"));
}

TEST(ReservedDeviceNamesAreNeutralised)
{
	CHECK_STREQ(SanitizeComponent("CON"), "_CON");
	CHECK_STREQ(SanitizeComponent("con.lua"), "_con.lua");
	CHECK_STREQ(SanitizeComponent("COM1.lua"), "_COM1.lua");
	CHECK_STREQ(SanitizeComponent("nul"), "_nul");

	// And the result is no longer reserved.
	CHECK(!IsReservedDeviceName(SanitizeComponent("CON")));
	CHECK(!IsReservedDeviceName(SanitizeComponent("com1.lua")));
}

// Windows strips trailing dots and spaces when resolving a path, so a name
// ending in one is not the name it looks like -- "a.lua." opens "a.lua".
TEST(TrailingDotsAndSpacesAreStripped)
{
	CHECK_STREQ(SanitizeComponent("a.lua."), "a.lua");
	CHECK_STREQ(SanitizeComponent("a.lua..."), "a.lua");

	// A space is not in the whitelist, so it becomes '_' before the trailing
	// strip ever sees it.  That is the stronger outcome: "a.lua_" is a real,
	// distinct filename, where stripping would collide it with "a.lua".
	CHECK_STREQ(SanitizeComponent("a.lua "), "a.lua_");

	// "CON." resolves to the device too, so the strip has to happen before the
	// reserved-name check.
	CHECK_STREQ(SanitizeComponent("CON."), "_CON");
}

TEST(LeadingDotsAreStripped)
{
	CHECK_STREQ(SanitizeComponent(".hidden"), "hidden");
	CHECK_STREQ(SanitizeComponent("...hidden"), "hidden");
}

TEST(NothingSurvivingYieldsAPlaceholderNotAnEmptyName)
{
	CHECK_STREQ(SanitizeComponent(""), "_");
	CHECK_STREQ(SanitizeComponent("..."), "_");

	// Spaces and control bytes map to '_' one for one rather than vanishing,
	// so distinct inputs stay distinct.
	CHECK_STREQ(SanitizeComponent("   "), "___");
	CHECK_STREQ(SanitizeComponent(std::string("\x01\x02\x03")), "___");
}

TEST(LengthIsCapped)
{
	const std::string longName(500, 'a');
	const std::string out = SanitizeComponent(longName);
	CHECK_EQ(out.size(), kMaxComponentLength);

	// A name that is all rejected characters is capped the same way.
	const std::string longJunk(500, '*');
	CHECK_EQ(SanitizeComponent(longJunk).size(), kMaxComponentLength);
}

// A maximum-length component that also happens to be a device name gets a
// prefix, which must not push it past the cap.
TEST(PrefixingADeviceNameStillRespectsTheCap)
{
	std::string name = "con.";
	name += std::string(kMaxComponentLength, 'a');

	const std::string out = SanitizeComponent(name);
	CHECK(out.size() <= kMaxComponentLength);
	CHECK(!IsReservedDeviceName(out));
}

TEST(TheResultIsIdempotent)
{
	const char* inputs[] = {
		"autorun.lua", "..", "CON", "a/b/c", "...", "", "a.lua.", ".hidden",
		"COM1.lua", "a*b?c", "nul.txt",
	};

	for (const char* input : inputs)
	{
		const std::string once = SanitizeComponent(input);
		const std::string twice = SanitizeComponent(once);
		CHECK_STREQ(twice, once);
	}
}

TEST(EveryOutputCharacterIsWhitelisted)
{
	std::string all;
	for (int c = 0; c < 256; ++c)
		all += static_cast<char>(c);

	const std::string out = SanitizeComponent(all);
	for (const char c : out)
		CHECK(pathsafe::IsAllowed(c));
}

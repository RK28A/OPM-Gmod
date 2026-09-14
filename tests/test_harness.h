// Minimal self-registering test harness.
//
// A full framework would be another vendored dependency to license, document
// and keep in sync; the review asked for tests, not for gtest.

#ifndef GMOD_SDK_TEST_HARNESS_H
#define GMOD_SDK_TEST_HARNESS_H

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace testing
{
	struct TestCase
	{
		const char* name;
		void (*fn)();
	};

	inline std::vector<TestCase>& Registry()
	{
		static std::vector<TestCase> registry;
		return registry;
	}

	inline int& FailureCount()
	{
		static int failures = 0;
		return failures;
	}

	struct Registrar
	{
		Registrar(const char* name, void (*fn)()) { Registry().push_back(TestCase{ name, fn }); }
	};

	inline void ReportFailure(const char* file, int line, const std::string& what)
	{
		std::printf("    %s:%d: %s\n", file, line, what.c_str());
		++FailureCount();
	}

	inline int RunAll()
	{
		int failed = 0;

		for (const TestCase& test : Registry())
		{
			const int before = FailureCount();
			std::printf("[ RUN  ] %s\n", test.name);
			test.fn();

			if (FailureCount() == before)
			{
				std::printf("[  OK  ] %s\n", test.name);
			}
			else
			{
				std::printf("[ FAIL ] %s\n", test.name);
				++failed;
			}
		}

		std::printf("\n%zu test(s), %d failed, %d assertion failure(s)\n",
			Registry().size(), failed, FailureCount());

		return failed == 0 ? 0 : 1;
	}
} // namespace testing

#define TEST(name)                                                             \
	static void name();                                                        \
	static ::testing::Registrar test_registrar_##name(#name, &name);           \
	static void name()

#define CHECK(expr)                                                            \
	do {                                                                       \
		if (!(expr))                                                           \
			::testing::ReportFailure(__FILE__, __LINE__,                       \
				std::string("CHECK failed: ") + #expr);                        \
	} while (false)

#define CHECK_EQ(actual, expected)                                             \
	do {                                                                       \
		const auto check_actual_ = (actual);                                   \
		const auto check_expected_ = (expected);                               \
		if (!(check_actual_ == check_expected_))                               \
			::testing::ReportFailure(__FILE__, __LINE__,                       \
				std::string("CHECK_EQ failed: ") + #actual + " != " + #expected); \
	} while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                \
	do {                                                                       \
		const double check_actual_ = static_cast<double>(actual);              \
		const double check_expected_ = static_cast<double>(expected);          \
		if (!(std::fabs(check_actual_ - check_expected_) <= (tolerance))) {     \
			char buffer_[256];                                                 \
			std::snprintf(buffer_, sizeof(buffer_),                            \
				"CHECK_NEAR failed: %s = %f, expected %f", #actual,            \
				check_actual_, check_expected_);                               \
			::testing::ReportFailure(__FILE__, __LINE__, buffer_);             \
		}                                                                      \
	} while (false)

#define CHECK_STREQ(actual, expected)                                          \
	do {                                                                       \
		const std::string check_actual_ = (actual);                            \
		const std::string check_expected_ = (expected);                        \
		if (check_actual_ != check_expected_)                                  \
			::testing::ReportFailure(__FILE__, __LINE__,                       \
				"CHECK_STREQ failed: got \"" + check_actual_ +                 \
				"\", expected \"" + check_expected_ + "\"");                   \
	} while (false)

#endif // GMOD_SDK_TEST_HARNESS_H

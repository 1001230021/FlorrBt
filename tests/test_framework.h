#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace test
{
struct TestCase
{
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& FailureCount()
{
    static int failures = 0;
    return failures;
}

inline void ReportFailure(const char* file, int line, const std::string& message)
{
    std::printf("FAIL %s:%d  %s\n", file, line, message.c_str());
    ++FailureCount();
}

inline int RunAll()
{
    int passed = 0;
    for (const TestCase& t : Registry())
    {
        const int before = FailureCount();
        t.fn();
        if (FailureCount() == before)
        {
            ++passed;
            std::printf("PASS %s\n", t.name);
        } else
        {
            std::printf("FAIL %s\n", t.name);
        }
    }
    std::printf("%d/%zu tests passed\n", passed, Registry().size());
    return FailureCount() == 0 ? 0 : 1;
}
} // namespace test

#define TEST(name)                                                             \
    static void test_##name();                                                 \
    static const bool test_registered_##name = [] {                            \
        test::Registry().push_back({ #name, &test_##name });                   \
        return true;                                                           \
    }();                                                                       \
    static void test_##name()

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
            test::ReportFailure(__FILE__, __LINE__, "CHECK failed: " #condition); \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                \
    do                                                                         \
    {                                                                          \
        const double check_near_actual_ = static_cast<double>(actual);         \
        const double check_near_expected_ = static_cast<double>(expected);     \
        const double check_near_tolerance_ = static_cast<double>(tolerance);   \
        if (check_near_actual_ < check_near_expected_ - check_near_tolerance_ || \
            check_near_actual_ > check_near_expected_ + check_near_tolerance_) \
            test::ReportFailure(__FILE__, __LINE__,                            \
                                "CHECK_NEAR failed: " #actual " = " +          \
                                    std::to_string(check_near_actual_) +       \
                                    " expected " + std::to_string(check_near_expected_) + \
                                    " +/- " + std::to_string(check_near_tolerance_)); \
    } while (0)

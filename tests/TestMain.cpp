#include "Test.h"

// Runs every registered TEST and returns nonzero if any check failed (which ctest reports).
int main()
{
    i32 failedTests = 0;
    for (const Test::Case& test : Test::Registry()) {
        Test::CurrentFailures() = 0;
        test.Function();
        const bool passed = Test::CurrentFailures() == 0;
        std::printf("[%s] %s\n", passed ? " OK " : "FAIL", test.Name);
        if (!passed)
            ++failedTests;
    }
    std::printf("%zu tests, %d failed\n", Test::Registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}

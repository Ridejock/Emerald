#include <cstring>

#include "Test.h"

// Runs every registered TEST, or with an argument only those whose name contains it
// (e.g. `EmeraldAssetTests HotReload`), and returns nonzero if any check failed.
int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    i32 ran = 0;
    i32 failedTests = 0;
    for (const Test::Case& test : Test::Registry()) {
        if (filter && !std::strstr(test.Name, filter))
            continue;
        ++ran;
        Test::CurrentFailures() = 0;
        test.Function();
        const bool passed = Test::CurrentFailures() == 0;
        std::printf("[%s] %s\n", passed ? " OK " : "FAIL", test.Name);
        if (!passed)
            ++failedTests;
    }
    std::printf("%d tests, %d failed\n", ran, failedTests);
    if (filter && ran == 0) {
        std::printf("No test name contains \"%s\"\n", filter);
        return 1;
    }
    return failedTests == 0 ? 0 : 1;
}

#include <cstring>

#if defined(_WIN32)
#include <stdlib.h>
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "Test.h"

// Runs every registered TEST, or with an argument only those whose name contains it
// (e.g. `EmeraldAssetTests HotReload`), and returns nonzero if any check failed.
int main(int argc, char** argv)
{
#if defined(_WIN32)
    // Don't let the Windows abort reporter sit on a dialog until CI kills the job.
    _set_abort_behavior(0, _CALL_REPORTFAULT);
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    const char* filter = argc > 1 ? argv[1] : nullptr;
    i32 ran = 0;
    i32 failedTests = 0;
    for (const Test::Case& test : Test::Registry()) {
        if (filter && !std::strstr(test.Name, filter))
            continue;
        ++ran;
        Test::CurrentFailures() = 0;
        std::fprintf(stderr, "RUN %s\n", test.Name);
        std::fflush(stderr);
        test.Function();
        const bool passed = Test::CurrentFailures() == 0;
        std::printf("[%s] %s\n", passed ? " OK " : "FAIL", test.Name);
        std::fflush(stdout);
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

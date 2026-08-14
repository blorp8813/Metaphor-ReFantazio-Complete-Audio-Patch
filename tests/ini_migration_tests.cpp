#include "ini_migration.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    {
        const std::string original =
            "; old shipped settings\r\n"
            "[Diagnostics]\r\n"
            "LogPath = custom.log\r\n"
            "[Recovery]\r\n"
            "ResetRestartFallback = false ; old default\r\n"
            "RecoveryLogging = false\r\n";
        const auto result = InstallerConfig::EnableResetRestartFallback(original);
        const std::string expected =
            "; old shipped settings\r\n"
            "[Diagnostics]\r\n"
            "LogPath = custom.log\r\n"
            "[Recovery]\r\n"
            "ResetRestartFallback = true ; old default\r\n"
            "RecoveryLogging = false\r\n";
        Expect(result.replacements == 1 && result.text == expected,
               "old INI is upgraded without changing custom settings or CRLF endings");
    }

    {
        const std::string original =
            "[Other]\n"
            "ResetRestartFallback = false\n"
            "[Recovery]\n"
            "; ResetRestartFallback = false\n"
            "# ResetRestartFallback = off\n"
            "ResetRestartFallback = OFF\n"
            "RESETRESTARTFALLBACK=0\n"
            "ResetRestartFallback = no\n"
            "ResetRestartFallback = true\n";
        const auto result = InstallerConfig::EnableResetRestartFallback(original);
        const std::string expected =
            "[Other]\n"
            "ResetRestartFallback = false\n"
            "[Recovery]\n"
            "; ResetRestartFallback = false\n"
            "# ResetRestartFallback = off\n"
            "ResetRestartFallback = true\n"
            "RESETRESTARTFALLBACK=true\n"
            "ResetRestartFallback = true\n"
            "ResetRestartFallback = true\n";
        Expect(result.replacements == 3 && result.text == expected,
               "all active false variants are upgraded only in Recovery");
    }

    {
        const std::string original =
            "\xEF\xBB\xBF[recovery]\n"
            "  ResetRestartFallback  =  false  # keep this comment\n"
            "[Spatial]\n"
            "WrapperEnabled = true";
        const auto result = InstallerConfig::EnableResetRestartFallback(original);
        const std::string expected =
            "\xEF\xBB\xBF[recovery]\n"
            "  ResetRestartFallback  =  true  # keep this comment\n"
            "[Spatial]\n"
            "WrapperEnabled = true";
        Expect(result.replacements == 1 && result.text == expected,
               "UTF-8 BOM, spacing, comments, and missing final newline are preserved");
    }

    {
        const std::string original =
            "[Recovery]\n"
            "ResetRestartFallback = maybe\n"
            "AdaptiveBufferRetry = true\n";
        const auto result = InstallerConfig::EnableResetRestartFallback(original);
        Expect(result.replacements == 0 && result.text == original,
               "already-effective or malformed settings remain byte-identical");
    }

    std::cout << "ini_migration_tests: PASS\n";
    return 0;
}

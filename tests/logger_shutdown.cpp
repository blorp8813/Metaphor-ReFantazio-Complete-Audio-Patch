#include "stdafx.h"

#include "log.hpp"

#include <cstdio>

int main()
{
    wchar_t temporary_directory[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temporary_directory) == 0) {
        return 1;
    }

    std::filesystem::path log_path = std::filesystem::path(temporary_directory) / L"MetaphorAudioFix-logger-shutdown-test.log";
    DeleteFileW(log_path.c_str());

    Log::Init(log_path, true, 1024 * 1024, 2);
    if (!Log::Enabled()) {
        return 2;
    }

    for (int index = 0; index < 50000; ++index) {
        Log::Info("queue flood record=%d", index);
    }

    const bool shutdown_succeeded = Log::Shutdown(10000);
    const bool handles_closed = Log::ShutdownComplete();
    DeleteFileW(log_path.c_str());

    if (!shutdown_succeeded || !handles_closed) {
        std::fprintf(stderr, "logger_shutdown: FAIL shutdown=%d handles_closed=%d\n",
                     shutdown_succeeded, handles_closed);
        return 3;
    }

    std::puts("logger_shutdown: PASS");
    return 0;
}

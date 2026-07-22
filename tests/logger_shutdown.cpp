#include "stdafx.h"

#include "log.hpp"

#include <cstdio>

namespace {
struct ProducerContext {
    std::atomic<bool> stop{false};
};

DWORD WINAPI ProducerThread(void* raw_context)
{
    auto* context = static_cast<ProducerContext*>(raw_context);
    int record = 0;
    while (!context->stop.load(std::memory_order_acquire)) {
        Log::Info("concurrent producer record=%d", record++);
    }
    return 0;
}
}

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

    ProducerContext producer_context;
    HANDLE producers[4]{};
    for (HANDLE& producer : producers) {
        producer = CreateThread(nullptr, 0, &ProducerThread, &producer_context, 0, nullptr);
        if (!producer) {
            return 3;
        }
    }

    const bool shutdown_succeeded = Log::Shutdown(10000);
    producer_context.stop.store(true, std::memory_order_release);
    const DWORD producer_wait = WaitForMultipleObjects(4, producers, TRUE, 10000);
    for (HANDLE producer : producers) {
        CloseHandle(producer);
    }
    const bool handles_closed = Log::ShutdownComplete();
    DeleteFileW(log_path.c_str());

    if (!shutdown_succeeded || !handles_closed || producer_wait != WAIT_OBJECT_0) {
        std::fprintf(stderr, "logger_shutdown: FAIL shutdown=%d handles_closed=%d producer_wait=0x%08lX\n",
                     shutdown_succeeded, handles_closed, static_cast<unsigned long>(producer_wait));
        return 4;
    }

    std::puts("logger_shutdown: PASS");
    return 0;
}

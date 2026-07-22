#include "stdafx.h"

#include "log.hpp"

#include <cstdio>

namespace {
struct ProducerContext {
    std::atomic<bool> stop{false};
    std::atomic<std::uint32_t> ready{0};
    std::atomic<std::uint64_t> attempts{0};
    HANDLE start_event = nullptr;
};

DWORD WINAPI ProducerThread(void* raw_context)
{
    auto* context = static_cast<ProducerContext*>(raw_context);
    context->ready.fetch_add(1, std::memory_order_release);
    if (WaitForSingleObject(context->start_event, 10000) != WAIT_OBJECT_0) {
        return 1;
    }
    int record = 0;
    while (!context->stop.load(std::memory_order_acquire)) {
        context->attempts.fetch_add(1, std::memory_order_relaxed);
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

    std::filesystem::path log_path = std::filesystem::path(temporary_directory) / L"MetaphorCompleteAudioPatch-logger-shutdown-test.log";
    DeleteFileW(log_path.c_str());

    Log::Init(log_path, true, false, 1024 * 1024, 2);
    if (!Log::Enabled()) {
        return 2;
    }

    for (int index = 0; index < 50000; ++index) {
        Log::Info("queue flood record=%d", index);
    }

    ProducerContext producer_context;
    producer_context.start_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!producer_context.start_event) {
        return 3;
    }
    HANDLE producers[4]{};
    for (HANDLE& producer : producers) {
        producer = CreateThread(nullptr, 0, &ProducerThread, &producer_context, 0, nullptr);
        if (!producer) {
            return 4;
        }
    }

    const ULONGLONG producer_deadline = GetTickCount64() + 10000;
    while (producer_context.ready.load(std::memory_order_acquire) != 4 &&
           GetTickCount64() < producer_deadline) {
        SwitchToThread();
    }
    if (producer_context.ready.load(std::memory_order_acquire) != 4 ||
        !SetEvent(producer_context.start_event)) {
        return 5;
    }
    while (producer_context.attempts.load(std::memory_order_acquire) < 10000 &&
           GetTickCount64() < producer_deadline) {
        SwitchToThread();
    }
    if (producer_context.attempts.load(std::memory_order_acquire) < 10000) {
        return 6;
    }

    // Producers deliberately keep calling Log::Info until Shutdown returns.
    // Calls that begin after enabled=false must not delay teardown or touch handles.
    const bool shutdown_succeeded = Log::Shutdown(10000);
    producer_context.stop.store(true, std::memory_order_release);
    const DWORD producer_wait = WaitForMultipleObjects(4, producers, TRUE, 10000);
    for (HANDLE producer : producers) {
        CloseHandle(producer);
    }
    CloseHandle(producer_context.start_event);
    const bool handles_closed = Log::ShutdownComplete();

    if (!shutdown_succeeded || !handles_closed || producer_wait != WAIT_OBJECT_0) {
        std::fprintf(stderr, "logger_shutdown: FAIL shutdown=%d handles_closed=%d producer_wait=0x%08lX\n",
                     shutdown_succeeded, handles_closed, static_cast<unsigned long>(producer_wait));
        return 7;
    }

    // Exercise the production category independently of full diagnostics.
    Log::Init(log_path, false, true, 1024 * 1024, 2);
    if (!Log::Enabled()) {
        return 8;
    }
    Log::Info("diagnostics-disabled record must be ignored");
    Log::RecoveryWarn("BUFFER_TOO_LARGE_CAUGHT recovery-only logger test");
    if (!Log::Shutdown(10000) || !Log::ShutdownComplete()) {
        return 9;
    }
    DeleteFileW(log_path.c_str());

    std::puts("logger_shutdown: PASS");
    return 0;
}

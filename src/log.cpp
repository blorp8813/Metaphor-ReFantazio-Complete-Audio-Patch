#include "log.hpp"

namespace Log {
namespace {
constexpr size_t kQueueCapacity = 2048;
constexpr size_t kMessageCapacity = 768;

enum class Level : std::uint8_t {
    Info,
    Warning,
    Error,
};

struct Entry {
    Level level = Level::Info;
    DWORD thread_id = 0;
    std::uint64_t qpc = 0;
    std::uint64_t wall_time_100ns = 0;
    char message[kMessageCapacity]{};
};

struct LoggerState {
    std::atomic<bool> enabled{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> writer_terminated{false};
    std::atomic<std::uint64_t> dropped{0};
    SRWLOCK queue_lock = SRWLOCK_INIT;
    Entry queue[kQueueCapacity]{};
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
    HANDLE wake_event = nullptr;
    HANDLE thread = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path path;
    std::uint64_t max_bytes = 0;
    int max_files = 1;
    LARGE_INTEGER qpc_frequency{};
};

LoggerState g_state;

const char* LevelName(Level level)
{
    switch (level) {
    case Level::Info:
        return "INFO";
    case Level::Warning:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

std::filesystem::path RotatedPath(int index)
{
    std::filesystem::path rotated = g_state.path;
    rotated += L"." + std::to_wstring(index);
    return rotated;
}

void RotateFiles()
{
    if (g_state.file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_state.file);
        g_state.file = INVALID_HANDLE_VALUE;
    }

    DeleteFileW(RotatedPath(g_state.max_files).c_str());
    for (int index = g_state.max_files - 1; index >= 1; --index) {
        MoveFileExW(RotatedPath(index).c_str(), RotatedPath(index + 1).c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    MoveFileExW(g_state.path.c_str(), RotatedPath(1).c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool OpenLogFile()
{
    g_state.file = CreateFileW(
        g_state.path.c_str(),
        FILE_APPEND_DATA | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    return g_state.file != INVALID_HANDLE_VALUE;
}

std::uint64_t CurrentFileSize()
{
    LARGE_INTEGER size{};
    if (g_state.file == INVALID_HANDLE_VALUE || !GetFileSizeEx(g_state.file, &size) || size.QuadPart < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(size.QuadPart);
}

bool PopEntry(Entry& entry)
{
    AcquireSRWLockExclusive(&g_state.queue_lock);
    if (g_state.count == 0) {
        ReleaseSRWLockExclusive(&g_state.queue_lock);
        return false;
    }
    entry = g_state.queue[g_state.head];
    g_state.head = (g_state.head + 1) % kQueueCapacity;
    --g_state.count;
    ReleaseSRWLockExclusive(&g_state.queue_lock);
    return true;
}

void WriteEntry(const Entry& entry)
{
    if (g_state.file == INVALID_HANDLE_VALUE && !OpenLogFile()) {
        return;
    }

    char line[1024]{};
    const std::uint64_t frequency = g_state.qpc_frequency.QuadPart > 0
        ? static_cast<std::uint64_t>(g_state.qpc_frequency.QuadPart)
        : 1;
    const std::uint64_t qpc_ns = (entry.qpc / frequency) * 1000000000ULL +
                                 ((entry.qpc % frequency) * 1000000000ULL) / frequency;
    const int length = std::snprintf(
        line,
        sizeof(line),
        "wall_100ns=%llu qpc=%llu qpc_ns=%llu tid=%lu level=%s %s\r\n",
        static_cast<unsigned long long>(entry.wall_time_100ns),
        static_cast<unsigned long long>(entry.qpc),
        static_cast<unsigned long long>(qpc_ns),
        static_cast<unsigned long>(entry.thread_id),
        LevelName(entry.level),
        entry.message
    );
    if (length <= 0) {
        return;
    }

    const DWORD bytes = static_cast<DWORD>(std::min<int>(length, static_cast<int>(sizeof(line) - 1)));
    if (g_state.max_bytes > 0 && CurrentFileSize() + bytes > g_state.max_bytes) {
        RotateFiles();
        if (!OpenLogFile()) {
            return;
        }
    }

    DWORD written = 0;
    WriteFile(g_state.file, line, bytes, &written, nullptr);
    if (entry.level == Level::Error) {
        FlushFileBuffers(g_state.file);
    }
}

DWORD WINAPI WriterThread(void*)
{
    while (true) {
        WaitForSingleObject(g_state.wake_event, 250);

        Entry entry{};
        while (PopEntry(entry)) {
            WriteEntry(entry);
        }

        const std::uint64_t dropped = g_state.dropped.exchange(0, std::memory_order_acq_rel);
        if (dropped > 0) {
            Entry warning{};
            warning.level = Level::Warning;
            warning.thread_id = GetCurrentThreadId();
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter(&qpc);
            warning.qpc = static_cast<std::uint64_t>(qpc.QuadPart);
            FILETIME wall{};
            GetSystemTimePreciseAsFileTime(&wall);
            warning.wall_time_100ns = (static_cast<std::uint64_t>(wall.dwHighDateTime) << 32) | wall.dwLowDateTime;
            std::snprintf(warning.message, sizeof(warning.message), "logger dropped=%llu messages because queue was busy/full",
                          static_cast<unsigned long long>(dropped));
            WriteEntry(warning);
        }

        if (g_state.stopping.load(std::memory_order_acquire)) {
            break;
        }
    }

    if (g_state.file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_state.file);
        CloseHandle(g_state.file);
        g_state.file = INVALID_HANDLE_VALUE;
    }
    g_state.writer_terminated.store(true, std::memory_order_release);
    return 0;
}

void Enqueue(Level level, const char* format, va_list args)
{
    if (!g_state.enabled.load(std::memory_order_relaxed) || !format) {
        return;
    }

    Entry entry{};
    entry.level = level;
    entry.thread_id = GetCurrentThreadId();
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    entry.qpc = static_cast<std::uint64_t>(qpc.QuadPart);
    FILETIME wall{};
    GetSystemTimePreciseAsFileTime(&wall);
    entry.wall_time_100ns = (static_cast<std::uint64_t>(wall.dwHighDateTime) << 32) | wall.dwLowDateTime;
    std::vsnprintf(entry.message, sizeof(entry.message), format, args);
    entry.message[sizeof(entry.message) - 1] = '\0';

    if (!TryAcquireSRWLockExclusive(&g_state.queue_lock)) {
        g_state.dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (g_state.count == kQueueCapacity) {
        ReleaseSRWLockExclusive(&g_state.queue_lock);
        g_state.dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_state.queue[g_state.tail] = entry;
    g_state.tail = (g_state.tail + 1) % kQueueCapacity;
    ++g_state.count;
    ReleaseSRWLockExclusive(&g_state.queue_lock);
    SetEvent(g_state.wake_event);
}
}

void Init(const std::filesystem::path& path, bool enabled, std::uint64_t max_bytes, int max_files)
{
    if (!enabled || g_state.enabled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    g_state.path = path;
    g_state.stopping = false;
    g_state.writer_terminated = false;
    g_state.max_bytes = max_bytes;
    g_state.max_files = std::max(1, max_files);
    QueryPerformanceFrequency(&g_state.qpc_frequency);
    std::error_code directory_error;
    if (!g_state.path.parent_path().empty()) {
        std::filesystem::create_directories(g_state.path.parent_path(), directory_error);
    }

    g_state.wake_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_state.wake_event) {
        g_state.enabled = false;
        return;
    }
    g_state.thread = CreateThread(nullptr, 0, &WriterThread, nullptr, 0, nullptr);
    if (!g_state.thread) {
        CloseHandle(g_state.wake_event);
        g_state.wake_event = nullptr;
        g_state.enabled = false;
    }
}

bool Enabled()
{
    return g_state.enabled.load(std::memory_order_relaxed);
}

void Info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Enqueue(Level::Info, format, args);
    va_end(args);
}

void Warn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Enqueue(Level::Warning, format, args);
    va_end(args);
}

void Error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Enqueue(Level::Error, format, args);
    va_end(args);
}

bool Shutdown(DWORD timeout_ms)
{
    const bool was_enabled = g_state.enabled.exchange(false, std::memory_order_acq_rel);
    if (!was_enabled && !g_state.thread) {
        return ShutdownComplete();
    }
    if (was_enabled) {
        g_state.stopping = true;
        if (g_state.wake_event) {
            SetEvent(g_state.wake_event);
        }
    }
    if (g_state.thread) {
        const DWORD wait_result = WaitForSingleObject(g_state.thread, timeout_ms);
        if (wait_result != WAIT_OBJECT_0 || !g_state.writer_terminated.load(std::memory_order_acquire)) {
            OutputDebugStringA("MetaphorAudioFix: logger writer did not terminate; handles retained and DLL unload must be refused.\n");
            return false;
        }
        CloseHandle(g_state.thread);
        g_state.thread = nullptr;
    }
    if (g_state.wake_event) {
        CloseHandle(g_state.wake_event);
        g_state.wake_event = nullptr;
    }
    return ShutdownComplete();
}

bool ShutdownComplete()
{
    return g_state.thread == nullptr &&
           g_state.wake_event == nullptr &&
           g_state.file == INVALID_HANDLE_VALUE;
}
}

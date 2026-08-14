#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION
#define METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION 0
#endif

namespace BufferRecovery {

using Hr = std::int32_t;

constexpr Hr kOk = 0;
constexpr Hr kBufferTooLarge = static_cast<Hr>(0x88890006u);
constexpr Hr kNotAttempted = static_cast<Hr>(0x80004001u);

enum class Stage : std::uint8_t {
    Stop,
    Reset,
    Start,
    GetCurrentPadding,
    GetBuffer,
};

constexpr bool Succeeded(Hr hr)
{
    return hr >= 0;
}

struct Config {
    bool enabled = false;
    bool adaptive_retry = true;
    bool reset_restart_fallback = false;
    std::uint32_t maximum_attempts_per_failure = 1;
    std::uint32_t maximum_recoveries_per_window = 3;
    std::uint64_t recovery_window_ticks = 0;
    std::uint64_t recovery_cooldown_ticks = 0;
    std::uint64_t fault_inject_after_successes = 0;
    bool fault_inject_zero_availability = false;
};

struct AcquireOutcome {
    Hr hr = kOk;
    void* buffer = nullptr;
    std::uint32_t frames = 0;
    std::uint32_t original_request_frames = 0;
    std::uint32_t buffer_capacity_frames = 0;
    std::uint32_t get_buffer_calls = 0;

    bool fault_injected = false;
    bool fault_injected_zero_availability = false;
    bool buffer_too_large_caught = false;
    bool adaptive_attempted = false;
    bool adaptive_succeeded = false;
    bool recovery_started = false;
    bool reset_succeeded = false;
    bool started_after_reset = false;
    bool started_after_start = false;
    bool recovery_succeeded = false;
    bool recovery_failed = false;
    bool circuit_breaker_open = false;
    bool padding_consistent = false;

    Hr initial_hr = kOk;
    Hr padding_hr = kNotAttempted;
    Hr adaptive_hr = kNotAttempted;
    Hr stop_hr = kNotAttempted;
    Hr reset_hr = kNotAttempted;
    Hr start_hr = kNotAttempted;
    Hr recovery_padding_hr = kNotAttempted;
    Hr recovery_retry_hr = kNotAttempted;
    std::uint32_t padding_frames = 0;
    std::uint32_t available_frames = 0;
    std::uint32_t adaptive_retry_frames = 0;
    std::uint32_t recovery_padding_frames = 0;
    std::uint32_t recovery_available_frames = 0;
    std::uint32_t recovery_retry_frames = 0;
    std::uint64_t recovery_elapsed_ticks = 0;
};

class Coordinator {
public:
    explicit Coordinator(Config config) : config_(config) {}

    bool ShouldInject(std::uint64_t successful_get_buffer_cycles)
    {
#if METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION
        if (!config_.enabled || config_.fault_inject_after_successes == 0 ||
            successful_get_buffer_cycles < config_.fault_inject_after_successes) {
            return false;
        }
        bool expected = false;
        return fault_injected_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
#else
        (void)successful_get_buffer_cycles;
        return false;
#endif
    }

    template <typename Backend>
    AcquireOutcome Acquire(Backend& backend,
                           std::uint32_t period_frames,
                           std::uint32_t buffer_capacity_frames,
                           bool inject_buffer_too_large)
    {
        AcquireOutcome outcome{};
        outcome.original_request_frames = period_frames;
        outcome.buffer_capacity_frames = buffer_capacity_frames;
        outcome.fault_injected = inject_buffer_too_large;
        outcome.fault_injected_zero_availability =
            inject_buffer_too_large && config_.fault_inject_zero_availability;

        if (inject_buffer_too_large) {
            outcome.initial_hr = kBufferTooLarge;
        } else {
            outcome.initial_hr = backend.GetBuffer(period_frames, &outcome.buffer);
            ++outcome.get_buffer_calls;
        }
        outcome.hr = outcome.initial_hr;
        if (Succeeded(outcome.initial_hr)) {
            outcome.frames = period_frames;
            return outcome;
        }
        if (outcome.initial_hr != kBufferTooLarge || !config_.enabled) {
            return outcome;
        }

        outcome.buffer_too_large_caught = true;
        outcome.adaptive_hr = outcome.initial_hr;
        bool fallback_needed = !config_.adaptive_retry;
        if (config_.adaptive_retry) {
            if (outcome.fault_injected_zero_availability) {
                // A test build can supply the confirmed full-buffer geometry
                // without touching a real render buffer before recovery.
                outcome.padding_hr = kOk;
                outcome.padding_frames = buffer_capacity_frames;
            } else {
                outcome.padding_hr = backend.GetCurrentPadding(&outcome.padding_frames);
            }
            outcome.padding_consistent = Succeeded(outcome.padding_hr) &&
                                         outcome.padding_frames <= buffer_capacity_frames;
            if (outcome.padding_consistent) {
                outcome.available_frames = buffer_capacity_frames - outcome.padding_frames;
            }

            if (outcome.padding_consistent && outcome.available_frames > 0) {
                outcome.adaptive_attempted = true;
                outcome.adaptive_retry_frames = std::min(period_frames, outcome.available_frames);
                outcome.adaptive_hr = backend.GetBuffer(outcome.adaptive_retry_frames, &outcome.buffer);
                ++outcome.get_buffer_calls;
                outcome.hr = outcome.adaptive_hr;
                if (Succeeded(outcome.adaptive_hr)) {
                    outcome.frames = outcome.adaptive_retry_frames;
                    outcome.adaptive_succeeded = true;
                    return outcome;
                }
                if (outcome.adaptive_hr != kBufferTooLarge) {
                    return outcome;
                }
            }
            fallback_needed = !outcome.padding_consistent || outcome.available_frames == 0 ||
                              (outcome.adaptive_attempted && outcome.adaptive_hr == kBufferTooLarge);
        }

        if (!fallback_needed || !config_.reset_restart_fallback) {
            return outcome;
        }
        if (config_.maximum_attempts_per_failure == 0 || recovery_gate_.test_and_set(std::memory_order_acquire)) {
            outcome.circuit_breaker_open = true;
            outcome.hr = outcome.initial_hr;
            return outcome;
        }

        struct GateGuard {
            std::atomic_flag& gate;
            ~GateGuard() { gate.clear(std::memory_order_release); }
        } gate_guard{recovery_gate_};

        const std::uint64_t recovery_start = backend.NowTicks();
        const auto outside_window = [&](std::uint64_t timestamp) {
            return recovery_start >= timestamp &&
                   recovery_start - timestamp >= config_.recovery_window_ticks;
        };
        std::size_t retained_count = 0;
        for (std::size_t index = 0; index < recovery_timestamp_count_; ++index) {
            if (!outside_window(recovery_timestamps_[index])) {
                recovery_timestamps_[retained_count++] = recovery_timestamps_[index];
            }
        }
        recovery_timestamp_count_ = retained_count;

        const std::size_t recovery_limit = std::min<std::size_t>(
            config_.maximum_recoveries_per_window, recovery_timestamps_.size());
        if (recovery_timestamp_count_ >= recovery_limit ||
            (recovery_timestamp_count_ > 0 && recovery_start >= recovery_timestamps_[recovery_timestamp_count_ - 1] &&
             recovery_start - recovery_timestamps_[recovery_timestamp_count_ - 1] < config_.recovery_cooldown_ticks)) {
            outcome.circuit_breaker_open = true;
            outcome.hr = outcome.initial_hr;
            return outcome;
        }

        recovery_timestamps_[recovery_timestamp_count_++] = recovery_start;
        outcome.recovery_started = true;
        backend.StageBefore(Stage::Stop);
        outcome.stop_hr = backend.Stop();
        backend.StageAfter(Stage::Stop, outcome.stop_hr);
        if (!Succeeded(outcome.stop_hr)) {
            outcome.recovery_failed = true;
            outcome.hr = outcome.initial_hr;
            outcome.recovery_elapsed_ticks = backend.NowTicks() - recovery_start;
            return outcome;
        }

        backend.StageBefore(Stage::Reset);
        outcome.reset_hr = backend.Reset();
        backend.StageAfter(Stage::Reset, outcome.reset_hr);
        if (!Succeeded(outcome.reset_hr)) {
            outcome.recovery_failed = true;
            outcome.hr = outcome.initial_hr;
            outcome.recovery_elapsed_ticks = backend.NowTicks() - recovery_start;
            return outcome;
        }
        outcome.reset_succeeded = true;
        outcome.started_after_reset = backend.IsStarted();

        backend.StageBefore(Stage::Start);
        outcome.start_hr = backend.Start();
        backend.StageAfter(Stage::Start, outcome.start_hr);
        outcome.started_after_start = backend.IsStarted();
        if (!Succeeded(outcome.start_hr)) {
            outcome.recovery_failed = true;
            outcome.hr = outcome.initial_hr;
            outcome.recovery_elapsed_ticks = backend.NowTicks() - recovery_start;
            return outcome;
        }

        backend.StageBefore(Stage::GetCurrentPadding);
        outcome.recovery_padding_hr = backend.GetCurrentPadding(&outcome.recovery_padding_frames);
        backend.StageAfter(Stage::GetCurrentPadding, outcome.recovery_padding_hr);
        const bool recovery_padding_consistent = Succeeded(outcome.recovery_padding_hr) &&
                                                 outcome.recovery_padding_frames <= buffer_capacity_frames;
        if (recovery_padding_consistent) {
            outcome.recovery_available_frames = buffer_capacity_frames - outcome.recovery_padding_frames;
        }
        if (!recovery_padding_consistent || outcome.recovery_available_frames == 0) {
            outcome.recovery_failed = true;
            outcome.hr = outcome.initial_hr;
            outcome.recovery_elapsed_ticks = backend.NowTicks() - recovery_start;
            return outcome;
        }

        outcome.recovery_retry_frames = std::min(period_frames, outcome.recovery_available_frames);
        backend.StageBefore(Stage::GetBuffer);
        outcome.recovery_retry_hr = backend.GetBuffer(outcome.recovery_retry_frames, &outcome.buffer);
        backend.StageAfter(Stage::GetBuffer, outcome.recovery_retry_hr);
        ++outcome.get_buffer_calls;
        outcome.recovery_elapsed_ticks = backend.NowTicks() - recovery_start;
        outcome.hr = outcome.recovery_retry_hr;
        if (Succeeded(outcome.recovery_retry_hr)) {
            outcome.frames = outcome.recovery_retry_frames;
            outcome.recovery_succeeded = true;
        } else {
            outcome.recovery_failed = true;
        }
        return outcome;
    }

private:
    Config config_{};
#if METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION
    std::atomic<bool> fault_injected_{false};
#endif
    std::atomic_flag recovery_gate_ = ATOMIC_FLAG_INIT;
    std::array<std::uint64_t, 100> recovery_timestamps_{};
    std::size_t recovery_timestamp_count_ = 0;
};

} // namespace BufferRecovery

#include "buffer_recovery.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {
using BufferRecovery::AcquireOutcome;
using BufferRecovery::Config;
using BufferRecovery::Coordinator;
using BufferRecovery::Hr;
using BufferRecovery::Stage;

constexpr Hr kFail = -1;
constexpr Hr kOtherAudioFailure = -2;

void Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct MockBackend {
    MockBackend(std::vector<Hr> get = {}, std::vector<std::uint32_t> padding = {})
        : get_results(std::move(get)), padding_values(std::move(padding))
    {
    }

    std::vector<Hr> get_results;
    std::vector<std::uint32_t> padding_values;
    std::vector<Hr> padding_results;
    Hr stop_result = BufferRecovery::kOk;
    Hr reset_result = BufferRecovery::kOk;
    Hr start_result = BufferRecovery::kOk;
    bool started = true;
    bool stop_changes_started = true;
    bool reset_observed_started = false;
    bool start_observed_started = true;
    std::vector<std::uint32_t> get_requests;
    std::vector<std::uint32_t> release_requests;
    std::vector<std::pair<bool, Stage>> stage_events;
    std::function<void()> on_stop;
    std::size_t get_index = 0;
    std::size_t padding_index = 0;
    std::uint32_t stop_calls = 0;
    std::uint32_t reset_calls = 0;
    std::uint32_t start_calls = 0;
    std::uint64_t now = 1000;
    float buffer[4096]{};

    Hr GetBuffer(std::uint32_t frames, void** out)
    {
        get_requests.push_back(frames);
        const Hr result = get_index < get_results.size() ? get_results[get_index++] : kFail;
        if (BufferRecovery::Succeeded(result)) {
            *out = buffer;
        }
        return result;
    }

    Hr GetCurrentPadding(std::uint32_t* padding)
    {
        const std::size_t index = padding_index++;
        const Hr result = index < padding_results.size() ? padding_results[index] : BufferRecovery::kOk;
        if (BufferRecovery::Succeeded(result)) {
            *padding = index < padding_values.size() ? padding_values[index] : 0;
        }
        return result;
    }

    Hr Stop()
    {
        ++stop_calls;
        if (on_stop) {
            on_stop();
        }
        if (BufferRecovery::Succeeded(stop_result) && stop_changes_started) {
            started = false;
        }
        return stop_result;
    }

    Hr Reset()
    {
        ++reset_calls;
        reset_observed_started = started;
        if (BufferRecovery::Succeeded(reset_result)) {
            started = false;
        }
        return reset_result;
    }

    Hr Start()
    {
        ++start_calls;
        start_observed_started = started;
        if (BufferRecovery::Succeeded(start_result)) {
            started = true;
        }
        return start_result;
    }

    std::uint64_t NowTicks() { return now++; }
    bool IsStarted() const { return started; }
    void ReleaseBuffer(std::uint32_t frames) { release_requests.push_back(frames); }
    void StageBefore(Stage stage) { stage_events.emplace_back(true, stage); }
    void StageAfter(Stage stage, Hr) { stage_events.emplace_back(false, stage); }
};

Config RecoveryConfig()
{
    Config config{};
    config.enabled = true;
    config.adaptive_retry = true;
    config.reset_restart_fallback = true;
    config.maximum_attempts_per_failure = 1;
    config.maximum_recoveries_per_window = 3;
    config.recovery_window_ticks = 30000;
    config.recovery_cooldown_ticks = 1000;
    return config;
}
}

int main()
{
    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kOk}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(BufferRecovery::Succeeded(outcome.hr), "full-period GetBuffer succeeds");
        Expect(outcome.frames == 480, "full-period frame count propagates");
        Expect(backend.get_requests == std::vector<std::uint32_t>{480}, "normal path makes one request");
    }

    {
        Config config = RecoveryConfig();
        config.reset_restart_fallback = false;
        Coordinator coordinator(config);
        MockBackend backend{{BufferRecovery::kBufferTooLarge, BufferRecovery::kOk, BufferRecovery::kOk}, {992}};
        const AcquireOutcome short_outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(short_outcome.adaptive_succeeded && short_outcome.frames == 448,
               "1440/480 stream with padding 992 retries exactly 448 frames");
        for (std::uint32_t frame = 0; frame < short_outcome.frames; ++frame) {
            backend.buffer[frame * 2] = 1.0f;
            backend.buffer[frame * 2 + 1] = 1.0f;
        }
        backend.ReleaseBuffer(short_outcome.frames);
        Expect(backend.release_requests == std::vector<std::uint32_t>{448},
               "regression path releases exactly the acquired 448 frames");

        const AcquireOutcome next_outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(BufferRecovery::Succeeded(next_outcome.hr) && next_outcome.frames == 480,
               "next processing pass returns to the full 480-frame period");
        Expect(backend.get_requests == std::vector<std::uint32_t>({480, 448, 480}),
               "regression request sequence is 480, 448, then 480");
        Expect(backend.stop_calls == 0 && backend.reset_calls == 0 && backend.start_calls == 0,
               "adaptive-only regression never calls Stop, Reset, or Start");
        Expect(backend.started, "adaptive-only regression preserves the started state");
    }

    {
        Config config = RecoveryConfig();
        config.reset_restart_fallback = false;
        Coordinator coordinator(config);
        MockBackend backend{{BufferRecovery::kBufferTooLarge, BufferRecovery::kOk}, {480}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.adaptive_succeeded && outcome.frames == 480,
               "same-size adaptive retry can recover when one full period is available");
        Expect(backend.get_requests == std::vector<std::uint32_t>({480, 480}),
               "same-size retry repeats the full period exactly once");
        Expect(backend.stop_calls == 0 && backend.reset_calls == 0 && backend.start_calls == 0,
               "same-size adaptive retry never calls Stop, Reset, or Start");
        Expect(backend.started, "same-size adaptive retry preserves the started state");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kBufferTooLarge, BufferRecovery::kOk}, {1200}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.adaptive_succeeded, "smaller adaptive retry succeeds");
        Expect(outcome.frames == 240, "smaller adaptive frame count propagates");
        Expect(backend.get_requests == std::vector<std::uint32_t>({480, 240}),
               "adaptive retry uses calculated availability");

        std::uint32_t mixed_frames = 0;
        for (std::uint32_t frame = 0; frame < outcome.frames; ++frame) {
            backend.buffer[frame * 2] = 1.0f;
            backend.buffer[frame * 2 + 1] = 1.0f;
            ++mixed_frames;
        }
        backend.ReleaseBuffer(outcome.frames);
        Expect(mixed_frames == 240, "mixing is bounded by smaller update frame count");
        Expect(backend.release_requests == std::vector<std::uint32_t>{240},
               "ReleaseBuffer uses smaller update frame count");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kBufferTooLarge, BufferRecovery::kOk}, {1440, 0}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.recovery_started, "zero availability triggers reset/restart");
        Expect(outcome.reset_succeeded && outcome.recovery_succeeded,
               "Stop/Reset/Start and retry succeed");
        Expect(outcome.frames == 480, "fallback retry propagates frame count");
        Expect(backend.started, "successful fallback leaves stream started");
        const std::vector<std::pair<bool, Stage>> expected_stages{
            {true, Stage::Stop}, {false, Stage::Stop},
            {true, Stage::Reset}, {false, Stage::Reset},
            {true, Stage::Start}, {false, Stage::Start},
            {true, Stage::GetCurrentPadding}, {false, Stage::GetCurrentPadding},
            {true, Stage::GetBuffer}, {false, Stage::GetBuffer},
        };
        Expect(backend.stage_events == expected_stages,
               "fallback publishes before/after markers around every blocking call");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kBufferTooLarge,
                             BufferRecovery::kBufferTooLarge,
                             BufferRecovery::kOk},
                            {1200, 0}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.adaptive_attempted && !outcome.adaptive_succeeded,
               "repeated BUFFER_TOO_LARGE exhausts adaptive retry");
        Expect(outcome.recovery_succeeded, "repeated BUFFER_TOO_LARGE enters fallback");
        Expect(backend.get_requests == std::vector<std::uint32_t>({480, 240, 480}),
               "fallback retry follows the smaller adaptive attempt");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kOk}, {1200}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, true);
        Expect(outcome.fault_injected && outcome.buffer_too_large_caught,
               "fault injection enters the real BUFFER_TOO_LARGE path");
        Expect(outcome.adaptive_succeeded && outcome.frames == 240,
               "fault injection is recovered by the same adaptive path");
        Expect(backend.get_requests == std::vector<std::uint32_t>{240},
               "fault injection does not touch or corrupt a real full-period buffer");
    }

    {
        Config config = RecoveryConfig();
        config.fault_inject_zero_availability = true;
        Coordinator coordinator(config);
        MockBackend backend{{BufferRecovery::kOk}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, true);
        Expect(outcome.fault_injected_zero_availability && outcome.available_frames == 0,
               "zero-availability injection reproduces full-buffer geometry");
        Expect(!outcome.adaptive_attempted && outcome.recovery_succeeded,
               "zero-availability injection completes the bounded fallback");
        Expect(backend.padding_index == 1,
               "zero-availability injection reads real padding only after restart");
        Expect(backend.get_requests == std::vector<std::uint32_t>{480},
               "zero-availability injection acquires a real buffer only after restart");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kBufferTooLarge}, {1440}};
        backend.reset_result = kFail;
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.recovery_failed && outcome.reset_hr == kFail, "Reset failure is reported");
        Expect(!backend.started, "failed recovery remains stopped after Reset failure");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kBufferTooLarge}, {1440}};
        backend.start_result = kFail;
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.recovery_failed && outcome.start_hr == kFail, "restart failure is reported");
        Expect(!backend.started, "failed Start does not invent started state");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kBufferTooLarge, BufferRecovery::kBufferTooLarge}, {1440, 0}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.recovery_failed && outcome.recovery_retry_hr == BufferRecovery::kBufferTooLarge,
               "retry failure after restart is reported");
    }

    {
        Config config = RecoveryConfig();
        config.maximum_recoveries_per_window = 1;
        config.recovery_cooldown_ticks = 0;
        Coordinator coordinator(config);
        MockBackend first{{BufferRecovery::kBufferTooLarge, BufferRecovery::kOk}, {1440, 0}};
        Expect(coordinator.Acquire(first, 480, 1440, false).recovery_succeeded,
               "first recovery is allowed");
        MockBackend second{{BufferRecovery::kBufferTooLarge}, {1440}};
        second.now = 2000;
        Expect(coordinator.Acquire(second, 480, 1440, false).circuit_breaker_open,
               "window limit opens circuit breaker");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{BufferRecovery::kBufferTooLarge, BufferRecovery::kOk}, {1440, 0}};
        backend.stop_changes_started = false;
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.recovery_succeeded, "Reset-state test recovery succeeds");
        Expect(backend.reset_observed_started, "Reset test begins with prior started state");
        Expect(!backend.start_observed_started, "successful Reset clears started state until Start succeeds");
        Expect(!outcome.started_after_reset && outcome.started_after_start,
               "recovery state records Reset false and Start true");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        std::optional<AcquireOutcome> nested_outcome;
        MockBackend backend{{BufferRecovery::kBufferTooLarge, BufferRecovery::kOk}, {1440, 0}};
        backend.on_stop = [&] {
            MockBackend nested{{BufferRecovery::kBufferTooLarge}, {1440}};
            nested_outcome = coordinator.Acquire(nested, 480, 1440, false);
        };
        Expect(coordinator.Acquire(backend, 480, 1440, false).recovery_succeeded,
               "outer recovery succeeds");
        Expect(nested_outcome.has_value() && nested_outcome->circuit_breaker_open,
               "concurrent/reentrant recovery request is rejected");
    }

    {
        Config config = RecoveryConfig();
        config.fault_inject_after_successes = 5;
        Coordinator coordinator(config);
        Expect(!coordinator.ShouldInject(4), "fault injection waits for threshold");
        Expect(coordinator.ShouldInject(5), "fault injection triggers at threshold");
        Expect(!coordinator.ShouldInject(5) && !coordinator.ShouldInject(100),
               "fault injection triggers exactly once per stream");
    }

    {
        Coordinator coordinator(RecoveryConfig());
        MockBackend backend{{kOtherAudioFailure}};
        const AcquireOutcome outcome = coordinator.Acquire(backend, 480, 1440, false);
        Expect(outcome.hr == kOtherAudioFailure, "non-BUFFER_TOO_LARGE error propagates unchanged");
        Expect(!outcome.adaptive_attempted && !outcome.recovery_started,
               "non-BUFFER_TOO_LARGE error never enters recovery");
    }

    std::cout << "buffer_recovery_tests: PASS\n";
    return 0;
}

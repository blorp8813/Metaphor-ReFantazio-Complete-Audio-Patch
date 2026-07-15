#pragma once

#include <cstdint>

struct StallDetectorConfig {
    std::uint64_t stall_timeout_ticks = 0;
    std::uint64_t non_silent_window_ticks = 0;
    std::uint64_t buffer_activity_window_ticks = 0;
};

struct StallObservation {
    std::uint64_t now_ticks = 0;
    bool started = false;
    bool playback_measurement_valid = false;
    std::uint64_t playback_position = 0;
    std::uint64_t last_non_silent_ticks = 0;
    std::uint64_t last_buffer_operation_ticks = 0;
    std::uint64_t callback_count = 0;
    bool buffer_operations_expected = false;
};

enum class StallTransition {
    None,
    SuspectedClockStallWithSubmissions,
    SuspectedRenderCallbackStarvation,
    Cleared,
};

bool StartedStateAfterStartResult(bool previously_started, std::int32_t start_hresult);

class StallDetector {
public:
    explicit StallDetector(StallDetectorConfig config);

    StallTransition Observe(const StallObservation& observation);
    void Reset();
    bool IsSuspected() const { return condition_ != Condition::None; }

private:
    enum class Condition {
        None,
        ClockStallWithSubmissions,
        RenderCallbackStarvation,
    };

    static bool IsRecent(std::uint64_t now, std::uint64_t then, std::uint64_t window);

    StallDetectorConfig config_{};
    bool have_position_ = false;
    bool active_non_silent_latched_ = false;
    std::uint64_t last_position_ = 0;
    std::uint64_t last_progress_ticks_ = 0;
    std::uint64_t callback_count_at_progress_ = 0;
    Condition condition_ = Condition::None;
};

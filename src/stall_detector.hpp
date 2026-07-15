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
    bool buffer_operations_expected = false;
};

enum class StallTransition {
    None,
    Suspected,
    Cleared,
};

class StallDetector {
public:
    explicit StallDetector(StallDetectorConfig config);

    StallTransition Observe(const StallObservation& observation);
    void Reset();
    bool IsSuspected() const { return suspected_; }

private:
    static bool IsRecent(std::uint64_t now, std::uint64_t then, std::uint64_t window);

    StallDetectorConfig config_{};
    bool have_position_ = false;
    bool suspected_ = false;
    std::uint64_t last_position_ = 0;
    std::uint64_t last_progress_ticks_ = 0;
};

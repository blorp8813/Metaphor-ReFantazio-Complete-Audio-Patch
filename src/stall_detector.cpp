#include "stall_detector.hpp"

StallDetector::StallDetector(StallDetectorConfig config) : config_(config)
{
}

bool StallDetector::IsRecent(std::uint64_t now, std::uint64_t then, std::uint64_t window)
{
    return then != 0 && now >= then && now - then <= window;
}

void StallDetector::Reset()
{
    have_position_ = false;
    suspected_ = false;
    last_position_ = 0;
    last_progress_ticks_ = 0;
}

StallTransition StallDetector::Observe(const StallObservation& observation)
{
    if (!observation.started || !observation.playback_measurement_valid) {
        const bool was_suspected = suspected_;
        Reset();
        return was_suspected ? StallTransition::Cleared : StallTransition::None;
    }

    if (!have_position_) {
        have_position_ = true;
        last_position_ = observation.playback_position;
        last_progress_ticks_ = observation.now_ticks;
        return StallTransition::None;
    }

    if (observation.playback_position != last_position_) {
        last_position_ = observation.playback_position;
        last_progress_ticks_ = observation.now_ticks;
        if (suspected_) {
            suspected_ = false;
            return StallTransition::Cleared;
        }
        return StallTransition::None;
    }

    const bool non_silent_recent = IsRecent(
        observation.now_ticks,
        observation.last_non_silent_ticks,
        config_.non_silent_window_ticks
    );
    const bool buffer_active = observation.buffer_operations_expected || IsRecent(
        observation.now_ticks,
        observation.last_buffer_operation_ticks,
        config_.buffer_activity_window_ticks
    );
    const bool timed_out = last_progress_ticks_ != 0 && observation.now_ticks >= last_progress_ticks_ &&
                           observation.now_ticks - last_progress_ticks_ >= config_.stall_timeout_ticks;

    const bool should_suspect = non_silent_recent && buffer_active && timed_out;
    if (should_suspect && !suspected_) {
        suspected_ = true;
        return StallTransition::Suspected;
    }
    if (!should_suspect && suspected_) {
        suspected_ = false;
        return StallTransition::Cleared;
    }
    return StallTransition::None;
}

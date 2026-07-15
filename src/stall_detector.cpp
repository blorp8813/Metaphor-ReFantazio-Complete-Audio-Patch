#include "stall_detector.hpp"

StallDetector::StallDetector(StallDetectorConfig config) : config_(config)
{
}

bool StartedStateAfterStartResult(bool previously_started, std::int32_t start_hresult)
{
    return start_hresult >= 0 || previously_started;
}

bool StallDetector::IsRecent(std::uint64_t now, std::uint64_t then, std::uint64_t window)
{
    return then != 0 && now >= then && now - then <= window;
}

void StallDetector::Reset()
{
    have_position_ = false;
    active_non_silent_latched_ = false;
    last_position_ = 0;
    last_progress_ticks_ = 0;
    callback_count_at_progress_ = 0;
    condition_ = Condition::None;
}

StallTransition StallDetector::Observe(const StallObservation& observation)
{
    if (!observation.started || !observation.playback_measurement_valid) {
        const bool was_suspected = condition_ != Condition::None;
        Reset();
        return was_suspected ? StallTransition::Cleared : StallTransition::None;
    }

    if (!have_position_) {
        have_position_ = true;
        last_position_ = observation.playback_position;
        last_progress_ticks_ = observation.now_ticks;
        callback_count_at_progress_ = observation.callback_count;
        active_non_silent_latched_ = IsRecent(observation.now_ticks, observation.last_non_silent_ticks,
                                               config_.non_silent_window_ticks) &&
                                     IsRecent(observation.now_ticks, observation.last_buffer_operation_ticks,
                                              config_.buffer_activity_window_ticks);
        return StallTransition::None;
    }

    if (observation.playback_position != last_position_) {
        last_position_ = observation.playback_position;
        last_progress_ticks_ = observation.now_ticks;
        callback_count_at_progress_ = observation.callback_count;
        active_non_silent_latched_ = IsRecent(observation.now_ticks, observation.last_non_silent_ticks,
                                               config_.non_silent_window_ticks) &&
                                     IsRecent(observation.now_ticks, observation.last_buffer_operation_ticks,
                                              config_.buffer_activity_window_ticks);
        if (condition_ != Condition::None) {
            condition_ = Condition::None;
            return StallTransition::Cleared;
        }
        return StallTransition::None;
    }

    const bool non_silent_recent = IsRecent(
        observation.now_ticks,
        observation.last_non_silent_ticks,
        config_.non_silent_window_ticks
    );
    const bool buffer_active = IsRecent(
        observation.now_ticks,
        observation.last_buffer_operation_ticks,
        config_.buffer_activity_window_ticks
    );
    if (non_silent_recent && buffer_active) {
        active_non_silent_latched_ = true;
    }
    const bool timed_out = last_progress_ticks_ != 0 && observation.now_ticks >= last_progress_ticks_ &&
                           observation.now_ticks - last_progress_ticks_ >= config_.stall_timeout_ticks;

    Condition desired = Condition::None;
    if (timed_out && non_silent_recent && buffer_active &&
        observation.callback_count > callback_count_at_progress_ &&
        observation.last_non_silent_ticks >= last_progress_ticks_) {
        desired = Condition::ClockStallWithSubmissions;
    } else if (timed_out && observation.buffer_operations_expected && active_non_silent_latched_ &&
               observation.last_buffer_operation_ticks != 0 && !buffer_active) {
        desired = Condition::RenderCallbackStarvation;
    }

    if (desired == condition_) {
        return StallTransition::None;
    }
    if (desired == Condition::ClockStallWithSubmissions) {
        condition_ = desired;
        return StallTransition::SuspectedClockStallWithSubmissions;
    }
    if (desired == Condition::RenderCallbackStarvation) {
        condition_ = desired;
        return StallTransition::SuspectedRenderCallbackStarvation;
    }
    if (condition_ != Condition::None) {
        condition_ = Condition::None;
        return StallTransition::Cleared;
    }
    return StallTransition::None;
}

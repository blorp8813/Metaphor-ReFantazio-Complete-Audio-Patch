#include "stall_detector.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

StallObservation BaseObservation(std::uint64_t now, std::uint64_t position)
{
    StallObservation value{};
    value.now_ticks = now;
    value.started = true;
    value.playback_measurement_valid = true;
    value.playback_position = position;
    value.last_non_silent_ticks = now;
    value.last_buffer_operation_ticks = now;
    return value;
}
}

int main()
{
    const StallDetectorConfig config{2000, 750, 750};

    {
        StallDetector detector(config);
        Expect(detector.Observe(BaseObservation(100, 10)) == StallTransition::None, "first position seeds state");
        Expect(detector.Observe(BaseObservation(1000, 20)) == StallTransition::None, "advancing clock is healthy");
        Expect(detector.Observe(BaseObservation(2999, 20)) == StallTransition::None, "timeout is not early");
        Expect(detector.Observe(BaseObservation(3000, 20)) == StallTransition::Suspected, "stalled clock is reported");
        Expect(detector.Observe(BaseObservation(3100, 20)) == StallTransition::None, "stall is reported once");
        Expect(detector.Observe(BaseObservation(3200, 21)) == StallTransition::Cleared, "clock progress clears stall");
    }

    {
        StallDetector detector(config);
        auto observation = BaseObservation(100, 10);
        detector.Observe(observation);
        observation = BaseObservation(2200, 10);
        observation.last_non_silent_ticks = 100;
        Expect(detector.Observe(observation) == StallTransition::None, "stale non-silent activity suppresses stall");
    }

    {
        StallDetector detector(config);
        detector.Observe(BaseObservation(100, 10));
        auto observation = BaseObservation(2200, 10);
        observation.last_buffer_operation_ticks = 100;
        Expect(detector.Observe(observation) == StallTransition::None, "stale buffer activity suppresses stall");
        observation.buffer_operations_expected = true;
        Expect(detector.Observe(observation) == StallTransition::Suspected, "expected event-driven buffers qualify");
    }

    {
        StallDetector detector(config);
        detector.Observe(BaseObservation(100, 10));
        auto observation = BaseObservation(2200, 10);
        observation.started = false;
        Expect(detector.Observe(observation) == StallTransition::None, "stopped streams cannot stall");
        Expect(!detector.IsSuspected(), "stop resets detector");
    }

    std::cout << "stall_detector_tests: PASS\n";
    return 0;
}

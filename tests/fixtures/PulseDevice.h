#pragma once
#include <pulse/pulseaudio.h>
#include <cstdint>
#include <vector>

namespace pulse_test {
struct Capture {
    pa_sample_spec format{};
    pa_buffer_attr buffering{};
    std::size_t largestWrite=0;
    int flushes=0;
    int underruns=0,timingQueries=0,missingTiming=0;
    pa_stream_flags_t flags=PA_STREAM_NOFLAGS;
    std::vector<float> samples;
};
void reset(std::int64_t deviceLatency=0,std::int64_t firstTimingDelay=0);
void disconnect();
Capture capture();
std::int64_t playedMicroseconds();
}

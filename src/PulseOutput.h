#pragma once
#include <QString>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <fluidsynth.h>
#include <pulse/pulseaudio.h>

namespace music {
class PulseOutput {
public:
    PulseOutput(fluid_synth_t* synth,fluid_sequencer_t* sequencer,int frames,
                std::function<void(QString)> failed);
    ~PulseOutput();
    [[nodiscard]] bool ready() const { return state_.load()==1; }
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] std::int64_t presentedMicroseconds() const { return presented_.load(); }
    [[nodiscard]] std::int64_t latencyMicroseconds() const { return latency_.load(); }
    void discard();
private:
    void contextChanged();
    void streamChanged();
    void write(std::size_t bytes);
    void updateClock();
    void fail(int error);
    using LoopPtr=std::unique_ptr<pa_mainloop,decltype(&pa_mainloop_free)>;
    using ContextPtr=std::unique_ptr<pa_context,decltype(&pa_context_unref)>;
    using StreamPtr=std::unique_ptr<pa_stream,decltype(&pa_stream_unref)>;
    LoopPtr loop_{nullptr,pa_mainloop_free};
    ContextPtr context_{nullptr,pa_context_unref};
    StreamPtr stream_{nullptr,pa_stream_unref};
    fluid_synth_t* synth_; // Non-owning; Playback stops output before destroying either object.
    fluid_sequencer_t* sequencer_;
    int frames_;
    std::function<void(QString)> failed_;
    std::jthread worker_;
    std::mutex readyMutex_;
    std::condition_variable readySignal_;
    std::atomic<int> state_{0};
    std::atomic<std::uint64_t> discard_{0};
    std::uint64_t discarded_=0;
    bool flushing_=false;
    std::atomic<std::int64_t> presented_{0},latency_{0};
    std::atomic<int> error_{0};
};
}

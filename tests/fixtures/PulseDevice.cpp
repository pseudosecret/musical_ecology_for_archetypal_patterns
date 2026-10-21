#include "PulseDevice.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

struct pa_mainloop {
    pa_mainloop_api api{};
    pa_context* context=nullptr;
    std::mutex mutex;
    std::condition_variable wake;
    bool signaled=false;
};
struct pa_context {
    pa_mainloop* loop=nullptr;
    pa_stream* stream=nullptr;
    pa_context_state_t state=PA_CONTEXT_UNCONNECTED;
    pa_context_notify_cb_t changed=nullptr;
    void* data=nullptr;
    int error=0;
};
struct pa_stream {
    pa_context* context=nullptr;
    pa_stream_state_t state=PA_STREAM_UNCONNECTED;
    pa_stream_notify_cb_t changed=nullptr,timing=nullptr;
    pa_stream_request_cb_t writable=nullptr;
    pa_stream_success_cb_t flushed=nullptr;
    void* stateData=nullptr;
    void* writeData=nullptr;
    void* timingData=nullptr;
    void* flushData=nullptr;
    std::array<float,512> buffer{};
};
struct pa_operation {};
namespace {
using Clock=std::chrono::steady_clock;
std::mutex mutex;
pulse_test::Capture recording;
std::atomic<bool> disconnected=false;
std::int64_t deviceLatency=0,queued=0,written=0,firstTimingDelay=0;
bool playing=false;
Clock::time_point last,timingOrigin;
void advance() {
    const auto now=Clock::now();
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(now-last).count();
    if(playing) {
        if(elapsed>queued+2000) { ++recording.underruns; playing=false; }
        queued=std::max<std::int64_t>(0,queued-elapsed);
    }
    last=now;
}
}
namespace pulse_test {
void reset(std::int64_t latency,std::int64_t timingDelay) {
    std::lock_guard lock(mutex);
    recording={}; disconnected=false; deviceLatency=latency; queued=written=0;
    firstTimingDelay=timingDelay; playing=false; last=timingOrigin=Clock::now();
}
void disconnect() { disconnected=true; }
Capture capture() { std::lock_guard lock(mutex); return recording; }
std::int64_t playedMicroseconds() {
    std::lock_guard lock(mutex);
    advance();
    return std::max<std::int64_t>(0,written-queued-deviceLatency);
}
}
extern "C" {
pa_mainloop* pa_mainloop_new() { return std::make_unique<pa_mainloop>().release(); }
void pa_mainloop_free(pa_mainloop* loop) { const std::unique_ptr<pa_mainloop> own(loop); }
pa_mainloop_api* pa_mainloop_get_api(pa_mainloop* loop) { loop->api.userdata=loop; return &loop->api; }
void pa_mainloop_wakeup(pa_mainloop* loop) {
    { std::lock_guard lock(loop->mutex); loop->signaled=true; }
    loop->wake.notify_all();
}
int pa_mainloop_iterate(pa_mainloop* loop,int block,int*) {
    if(block) {
        std::unique_lock lock(loop->mutex);
        loop->wake.wait_for(lock,std::chrono::milliseconds(1),[loop] { return loop->signaled; });
        loop->signaled=false;
    }
    auto* context=loop->context;
    if(!context)return 0;
    if(context->state==PA_CONTEXT_CONNECTING) {
        context->state=PA_CONTEXT_READY;
        if(context->changed)context->changed(context,context->data);
    }
    auto* stream=context->stream;
    if(!stream)return 0;
    if(disconnected) {
        context->error=PA_ERR_CONNECTIONTERMINATED;
        stream->state=PA_STREAM_FAILED;
        if(stream->changed)stream->changed(stream,stream->stateData);
        return 0;
    }
    if(stream->state==PA_STREAM_CREATING) {
        stream->state=PA_STREAM_READY;
        if(stream->changed)stream->changed(stream,stream->stateData);
    }
    if(stream->flushed) {
        { std::lock_guard lock(mutex); ++recording.flushes; queued=0; playing=false; last=timingOrigin=Clock::now(); }
        const auto callback=stream->flushed;
        stream->flushed=nullptr;
        callback(stream,1,stream->flushData);
    }
    std::size_t minimum;
    { std::lock_guard lock(mutex); minimum=recording.buffering.minreq; }
    const auto bytes=pa_stream_writable_size(stream);
    if(bytes>=minimum && stream->writable)stream->writable(stream,bytes,stream->writeData);
    if(stream->timing)stream->timing(stream,stream->timingData);
    return 0;
}
pa_context* pa_context_new(pa_mainloop_api* api,const char*) {
    auto context=std::make_unique<pa_context>();
    context->loop=static_cast<pa_mainloop*>(api->userdata);
    context->loop->context=context.get();
    return context.release();
}
void pa_context_unref(pa_context* context) { const std::unique_ptr<pa_context> own(context); }
void pa_context_set_state_callback(pa_context* c,pa_context_notify_cb_t cb,void* data) { c->changed=cb; c->data=data; }
int pa_context_connect(pa_context* context,const char*,pa_context_flags_t,const pa_spawn_api*) { context->state=PA_CONTEXT_CONNECTING; return 0; }
void pa_context_disconnect(pa_context* context) { context->state=PA_CONTEXT_TERMINATED; }
int pa_context_errno(const pa_context* context) { return context->error; }
pa_context_state_t pa_context_get_state(const pa_context* context) { return context->state; }
pa_stream* pa_stream_new(pa_context* context,const char*,const pa_sample_spec* format,const pa_channel_map*) {
    auto stream=std::make_unique<pa_stream>();
    stream->context=context;
    context->stream=stream.get();
    { std::lock_guard lock(mutex); recording.format=*format; }
    return stream.release();
}
void pa_stream_unref(pa_stream* stream) { const std::unique_ptr<pa_stream> own(stream); }
int pa_stream_disconnect(pa_stream* stream) { stream->state=PA_STREAM_TERMINATED; return 0; }
pa_stream_state_t pa_stream_get_state(const pa_stream* stream) { return stream->state; }
void pa_stream_set_state_callback(pa_stream* s,pa_stream_notify_cb_t cb,void* data) { s->changed=cb; s->stateData=data; }
void pa_stream_set_write_callback(pa_stream* s,pa_stream_request_cb_t cb,void* data) { s->writable=cb; s->writeData=data; }
void pa_stream_set_latency_update_callback(pa_stream* s,pa_stream_notify_cb_t cb,void* data) { s->timing=cb; s->timingData=data; }
int pa_stream_connect_playback(pa_stream* s,const char*,const pa_buffer_attr* buffering,pa_stream_flags_t flags,const pa_cvolume*,pa_stream*) {
    { std::lock_guard lock(mutex); recording.buffering=*buffering; recording.flags=flags; last=timingOrigin=Clock::now(); }
    s->state=PA_STREAM_CREATING;
    return 0;
}
std::size_t pa_stream_writable_size(const pa_stream*) {
    std::lock_guard lock(mutex);
    advance();
    const auto capacity=recording.buffering.tlength;
    const auto used=static_cast<std::uint64_t>(queued)*48000/1'000'000*2*sizeof(float);
    return used<capacity?capacity-used:0;
}
int pa_stream_begin_write(pa_stream* s,void** data,std::size_t* bytes) {
    *bytes=std::min(*bytes,sizeof(s->buffer));
    *data=s->buffer.data();
    return 0;
}
int pa_stream_cancel_write(pa_stream*) { return 0; }
int pa_stream_write(pa_stream*,const void* data,std::size_t bytes,pa_free_cb_t,std::int64_t,pa_seek_mode_t) {
    std::lock_guard lock(mutex);
    advance();
    const auto duration=static_cast<std::int64_t>(bytes/(2*sizeof(float)))*1'000'000/48000;
    queued+=duration; written+=duration;
    const auto prebuf=static_cast<std::int64_t>(recording.buffering.prebuf/(2*sizeof(float)))*1'000'000/48000;
    if(queued>=prebuf-32)playing=true;
    recording.largestWrite=std::max(recording.largestWrite,bytes);
    const auto* samples=static_cast<const float*>(data);
    recording.samples.insert(recording.samples.end(),samples,samples+bytes/sizeof(float));
    return 0;
}
int pa_stream_get_latency(pa_stream* stream,pa_usec_t* latency,int* negative) {
    std::lock_guard lock(mutex);
    ++recording.timingQueries;
    if(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-timingOrigin).count()<firstTimingDelay) {
        ++recording.missingTiming;
        stream->context->error=PA_ERR_NODATA;
        return -PA_ERR_NODATA;
    }
    advance();
    *latency=queued+deviceLatency; *negative=0;
    return 0;
}
pa_operation* pa_stream_flush(pa_stream* stream,pa_stream_success_cb_t cb,void* data) {
    stream->flushed=cb; stream->flushData=data;
    return std::make_unique<pa_operation>().release();
}
void pa_operation_unref(pa_operation* operation) { const std::unique_ptr<pa_operation> own(operation); }
}

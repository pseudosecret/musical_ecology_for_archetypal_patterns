#include "PulseOutput.h"
#include <algorithm>
#include <chrono>

namespace music {
namespace { constexpr int frameBytes=2*sizeof(float); }

PulseOutput::PulseOutput(fluid_synth_t* synth,fluid_sequencer_t* sequencer,int frames,
                         std::function<void(QString)> failed)
    :synth_(synth),sequencer_(sequencer),frames_(frames),failed_(std::move(failed)) {
    loop_.reset(pa_mainloop_new());
    if(!loop_) { fail(PA_ERR_INTERNAL); return; }
    context_.reset(pa_context_new(pa_mainloop_get_api(loop_.get()),"Music as Becoming"));
    if(!context_) { fail(PA_ERR_INTERNAL); return; }
    pa_context_set_state_callback(context_.get(),[](pa_context*,void* data) {
        static_cast<PulseOutput*>(data)->contextChanged();
    },this);
    worker_=std::jthread([this](std::stop_token stop) {
        if(pa_context_connect(context_.get(),nullptr,PA_CONTEXT_NOFLAGS,nullptr)<0) {
            fail(pa_context_errno(context_.get())); return;
        }
        while(!stop.stop_requested() && state_.load()!=2) {
            if(ready() && !flushing_ && discarded_!=discard_.load()) {
                discarded_=discard_.load();
                flushing_=true;
                auto* operation=pa_stream_flush(stream_.get(),[](pa_stream*,int success,void* data) {
                    auto* output=static_cast<PulseOutput*>(data);
                    if(!success) { output->fail(pa_context_errno(output->context_.get())); return; }
                    output->flushing_=false;
                    output->write(pa_stream_writable_size(output->stream_.get()));
                },this);
                if(!operation) { fail(pa_context_errno(context_.get())); break; }
                pa_operation_unref(operation);
            }
            if(pa_mainloop_iterate(loop_.get(),1,nullptr)<0) { fail(PA_ERR_CONNECTIONTERMINATED); break; }
        }
    });
    // Connection setup waits only on Playback's initialization worker.
    std::unique_lock lock(readyMutex_);
    if(!readySignal_.wait_for(lock,std::chrono::seconds(10),[this] { return state_.load()!=0; })) {
        error_=PA_ERR_TIMEOUT; state_=2;
        pa_mainloop_wakeup(loop_.get());
    }
}
PulseOutput::~PulseOutput() {
    if(worker_.joinable()) { worker_.request_stop(); pa_mainloop_wakeup(loop_.get()); worker_.join(); }
    if(stream_) {
        pa_stream_set_state_callback(stream_.get(),nullptr,nullptr);
        pa_stream_disconnect(stream_.get());
    }
    if(context_) {
        pa_context_set_state_callback(context_.get(),nullptr,nullptr);
        pa_context_disconnect(context_.get());
    }
}
void PulseOutput::contextChanged() {
    const auto state=pa_context_get_state(context_.get());
    if(state==PA_CONTEXT_FAILED || state==PA_CONTEXT_TERMINATED) { fail(pa_context_errno(context_.get())); return; }
    if(state!=PA_CONTEXT_READY || stream_)return;
    const pa_sample_spec format{PA_SAMPLE_FLOAT32NE,48000,2};
    stream_.reset(pa_stream_new(context_.get(),"Score and previews",&format,nullptr));
    if(!stream_) { fail(pa_context_errno(context_.get())); return; }
    pa_stream_set_state_callback(stream_.get(),[](pa_stream*,void* data) { static_cast<PulseOutput*>(data)->streamChanged(); },this);
    pa_stream_set_write_callback(stream_.get(),[](pa_stream*,std::size_t bytes,void* data) { static_cast<PulseOutput*>(data)->write(bytes); },this);
    pa_stream_set_latency_update_callback(stream_.get(),[](pa_stream*,void* data) { static_cast<PulseOutput*>(data)->updateClock(); },this);
    const auto bytes=static_cast<std::uint32_t>(frames_*frameBytes);
    // Prime the full safety buffer; a 5 ms prebuffer is insufficient for the WSLg sink.
    const pa_buffer_attr buffering{bytes,bytes,bytes,static_cast<std::uint32_t>(std::min(frames_,1024)*frameBytes),0};
    const auto flags=static_cast<pa_stream_flags_t>(PA_STREAM_AUTO_TIMING_UPDATE|PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_ADJUST_LATENCY);
    if(pa_stream_connect_playback(stream_.get(),nullptr,&buffering,flags,nullptr,nullptr)<0)fail(pa_context_errno(context_.get()));
}
void PulseOutput::streamChanged() {
    const auto state=pa_stream_get_state(stream_.get());
    if(state==PA_STREAM_FAILED || state==PA_STREAM_TERMINATED) { fail(pa_context_errno(context_.get())); return; }
    if(state!=PA_STREAM_READY || state_.load()!=0)return;
    write(pa_stream_writable_size(stream_.get()));
    {
        std::lock_guard lock(readyMutex_);
        if(state_.load()==0)state_=1;
    }
    readySignal_.notify_all();
}
void PulseOutput::write(std::size_t bytes) {
    const auto available=pa_stream_writable_size(stream_.get());
    if(bytes==static_cast<std::size_t>(-1) || available==static_cast<std::size_t>(-1)) { fail(pa_context_errno(context_.get())); return; }
    bytes=std::min(bytes,available);
    while(bytes>=frameBytes && state_.load()!=2 && !flushing_ && discarded_==discard_.load()) {
        auto count=std::min<std::size_t>(bytes,256*frameBytes);
        void* samples=nullptr;
        if(pa_stream_begin_write(stream_.get(),&samples,&count)<0) { fail(pa_context_errno(context_.get())); return; }
        if(!samples || count<frameBytes) { pa_stream_cancel_write(stream_.get()); fail(PA_ERR_INTERNAL); return; }
        count=std::min({count,bytes,static_cast<std::size_t>(256*frameBytes)});
        count-=count%frameBytes;
        if(fluid_synth_write_float(synth_,count/frameBytes,samples,0,2,samples,1,2)!=FLUID_OK) {
            pa_stream_cancel_write(stream_.get()); fail(PA_ERR_INTERNAL); return;
        }
        if(pa_stream_write(stream_.get(),samples,count,nullptr,0,PA_SEEK_RELATIVE)<0) { fail(pa_context_errno(context_.get())); return; }
        bytes-=count;
    }
    updateClock();
}
void PulseOutput::updateClock() {
    pa_usec_t latency=0;
    int negative=0;
    // Cached timing is nonblocking. Missing startup/flush timing must never stall audio.
    if(pa_stream_get_latency(stream_.get(),&latency,&negative)<0)return;
    latency_=negative?0:static_cast<std::int64_t>(latency);
    const auto rendered=static_cast<std::int64_t>(fluid_sequencer_get_tick(sequencer_))*1000;
    presented_=std::max(presented_.load(),rendered-latency_.load());
}
void PulseOutput::discard() { ++discard_; if(loop_)pa_mainloop_wakeup(loop_.get()); }
void PulseOutput::fail(int error) {
    int previous;
    {
        std::lock_guard lock(readyMutex_);
        error_=error?error:PA_ERR_INTERNAL;
        previous=state_.exchange(2);
    }
    readySignal_.notify_all();
    if(previous==1)failed_(errorMessage());
}
QString PulseOutput::errorMessage() const { return QString::fromUtf8(pa_strerror(error_.load())); }
}

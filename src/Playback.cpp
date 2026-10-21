#include "Playback.h"
#ifdef MUSIC_PULSE_OUTPUT
#include "PulseOutput.h"
#endif
#include <QByteArray>
#include <QFileInfo>
#include <QMetaObject>
#include <algorithm>
#include <chrono>
#include <cmath>
namespace music {
    namespace {
        constexpr int previewChannel=255;
    }
    Playback::Playback(QObject* parent):QObject(parent) {
        initWorker_=std::jthread([this] {
            auto fail=[this](QString message) {
                audioError_=std::move(message);
                audioState_.store(2);
                QMetaObject::invokeMethod(this,[this] {
                    emit readyChanged();
                },Qt::QueuedConnection);
            };
            QByteArray path=qgetenv("MUSIC_SOUNDFONT");
            if(path.isEmpty())path="/usr/share/sounds/sf2/FluidR3_GM.sf2";
            if(!QFileInfo::exists(QString::fromUtf8(path))) {
                fail("General MIDI SoundFont is missing.");
                return;
            }
            SettingsPtr settings(new_fluid_settings(),delete_fluid_settings);
            if(!settings) {
                fail("Could not initialize FluidSynth.");
                return;
            }
            QByteArray audioDriver=qgetenv("MUSIC_AUDIO_DRIVER");
            if(audioDriver.isEmpty() && qEnvironmentVariableIsSet("PULSE_SERVER"))audioDriver="pulseaudio";
            if(!audioDriver.isEmpty())fluid_settings_setstr(settings.get(),"audio.driver",audioDriver.constData());
            char name[128]{};
            fluid_settings_copystr(settings.get(),"audio.driver",name,sizeof name);
            const bool pulse=QByteArray(name)=="pulseaudio";
            const bool wslg=qgetenv("PULSE_SERVER").contains("/mnt/wslg/");
            const int period=qEnvironmentVariableIntValue("MUSIC_AUDIO_BUFFER");
            // PulseAudio uses the period size as its latency request; periods do not multiply it.
            // WSLg's remote audio sink needs more headroom than a native PulseAudio sink.
            const int frames=period>=64 && period<=8192?period:pulse?(wslg?4096:2048):512;
            fluid_settings_setnum(settings.get(),"synth.sample-rate",48000.0);
            fluid_settings_setnum(settings.get(),"synth.gain",0.2*volume_.load()/100.0);
            fluid_settings_setint(settings.get(),"audio.period-size",frames);
            fluid_settings_setint(settings.get(),"audio.periods",4);
            fluid_settings_setint(settings.get(),"synth.midi-channels",256);
            fluid_settings_setint(settings.get(),"synth.polyphony",512);
            fluid_settings_setint(settings.get(),"synth.threadsafe-api",1);
            if(pulse)fluid_settings_setint(settings.get(),"audio.pulseaudio.adjust-latency",0);
            const auto audioFile=qgetenv("MUSIC_AUDIO_FILE");
            if(!audioFile.isEmpty())fluid_settings_setstr(settings.get(),"audio.file.name",audioFile.constData());
            SynthPtr synth(new_fluid_synth(settings.get()),delete_fluid_synth);
            if(!synth) {
                fail("Could not initialize the synthesizer.");
                return;
            }
            const int font=fluid_synth_sfload(synth.get(),path.constData(),1);
            if(font<0) {
                fail("Could not load the General MIDI SoundFont.");
                return;
            }
            for(int c=0;c<256;++c)fluid_synth_set_channel_type(synth.get(),c,CHANNEL_TYPE_MELODIC);
            SeqPtr seq(new_fluid_sequencer2(0),delete_fluid_sequencer);
            if(!seq) {
                fail("Could not initialize audio sequencing.");
                return;
            }
            fluid_sequencer_set_time_scale(seq.get(),1000.0);
            const auto dest=fluid_sequencer_register_fluidsynth(seq.get(),synth.get());
            const auto source=fluid_sequencer_register_client(seq.get(),"Score",nullptr,nullptr);
            const auto preview=fluid_sequencer_register_client(seq.get(),"Preview",nullptr,nullptr);
            if(dest<0 || source<0 || preview<0) {
                fail("Could not bind audio sequencing.");
                return;
            }
            DriverPtr driver(nullptr,delete_fluid_audio_driver);
#ifdef MUSIC_PULSE_OUTPUT
            std::unique_ptr<PulseOutput> output;
            if(pulse) {
                output=std::make_unique<PulseOutput>(synth.get(),seq.get(),frames,[this](QString message) {
                    QMetaObject::invokeMethod(this,[this,message=std::move(message)] {
                        stop();
                        audioError_="Audio output disconnected: "+message;
                        audioState_.store(2);
                        emit readyChanged();
                    },Qt::QueuedConnection);
                });
                if(!output->ready()) { fail("Could not open PulseAudio: "+output->errorMessage()); return; }
            } else
#endif
            driver.reset(new_fluid_audio_driver(settings.get(),synth.get()));
            if(!driver
#ifdef MUSIC_PULSE_OUTPUT
               && !output
#endif
               ) {
                fail("Could not open an audio output device.");
                return;
            }
            configuration_=QString("%1 · 48 kHz · %2-frame %3").arg(QString::fromUtf8(name)).arg(frames).arg(pulse?"queue":"blocks");
            if(pulse)configuration_+=QString(" · %1 ms queue limit · %2-frame rendering").arg(frames/48.0,0,'f',1).arg(std::min(frames,256));
            configuration_+=pulse?" · audible-output clock":" · synthesis-clock sequencing";
            soundFont_=font;
            destination_=dest;
            scoreSource_=source;
            previewSource_=preview;
            settings_=std::move(settings);
            synth_=std::move(synth);
            seq_=std::move(seq);
            driver_=std::move(driver);
#ifdef MUSIC_PULSE_OUTPUT
            pulseOutput_=std::move(output);
#endif
            int initializing=0;
            if(!audioState_.compare_exchange_strong(initializing,1))return;
            QMetaObject::invokeMethod(this,[this] {
                setVolume(volume());
                emit readyChanged();
            },Qt::QueuedConnection);
        });
    }
    Playback::~Playback() {
        stop();
        if(initWorker_.joinable())initWorker_.join();
#ifdef MUSIC_PULSE_OUTPUT
        pulseOutput_.reset();
#endif
        driver_.reset();
        seq_.reset();
        synth_.reset();
        settings_.reset();
    }
    std::vector<TempoSegment> Playback::timeline(const Score& score) {
        std::vector<TempoSegment> result {
            {
                0,0,score.tempoAt(0)
            }
        };
        for(std::size_t m=1;m<score.measureCount();++m)if(score.bar(m).tempo) {
            const Tick tick=score.measureStart(m);
            const auto micros=timeAt(result,tick);
            result.push_back(TempoSegment {
                tick,micros,*score.bar(m).tempo
            });
        }
        return result;
    }
    std::int64_t Playback::timeAt(const std::vector<TempoSegment>& line,Tick tick) {
        if(line.empty())return 0;
        auto it=std::upper_bound(line.begin(),line.end(),tick,[](Tick t,const TempoSegment& s) {
            return t<s.tick;
        });
        const auto& s=it==line.begin()?line.front():*std::prev(it);
        return s.microseconds+(tick-s.tick)*60'000'000/(s.tempo.beat*s.tempo.bpm);
    }
    Tick Playback::tickAt(const std::vector<TempoSegment>& line,std::int64_t micros) {
        if(line.empty())return 0;
        auto it=std::upper_bound(line.begin(),line.end(),micros,[](std::int64_t t,const TempoSegment& s) {
            return t<s.microseconds;
        });
        const auto& s=it==line.begin()?line.front():*std::prev(it);
        return s.tick+(micros-s.microseconds)*s.tempo.beat*s.tempo.bpm/60'000'000;
    }
    std::vector<ScheduledNote> Playback::schedule(const Score& score,Tick from,Tick until) {
        const auto line=timeline(score);
        const auto offset=timeAt(line,from);
        std::vector<ScheduledNote> result;
        for(std::size_t staff=0;staff<score.staffCount();++staff)for(std::size_t m=0;m<score.measureCount();++m) {
            Tick onset=score.measureStart(m);
            for(const auto& e:score.measure(m,staff).events) {
                const auto* previous=score.event(score.previousEventId(e.id));
                for(std::size_t tone=0;tone<e.pitches.size();++tone) {
                    const auto pitch=e.pitches[tone];
                    bool incoming=false;
                    if(previous)for(std::size_t t=0;t<previous->pitches.size();++t)if(previous->tieTarget(t)==e.id && previous->pitches[t]==pitch)incoming=true;
                    if(incoming)continue;
                    Tick end=onset+e.ticks;
                    const Event* current=&e;
                    std::size_t currentTone=tone;
                    while(current->tieTarget(currentTone)) {
                        const auto* target=score.event(current->tieTarget(currentTone));
                        if(!target)break;
                        const auto found=std::find(target->pitches.begin(),target->pitches.end(),pitch);
                        if(found==target->pitches.end())break;
                        end+=target->ticks;
                        currentTone=found-target->pitches.begin();
                        current=target;
                    }
                    if(until>=0)end=std::min(end,until);
                    if(end>std::max(onset,from)) {
                        result.push_back(ScheduledNote {
                            timeAt(line,std::max(onset,from))-offset,pitch.midi(),true,static_cast<int>(staff)
                        });
                        result.push_back(ScheduledNote {
                            timeAt(line,end)-offset,pitch.midi(),false,static_cast<int>(staff)
                        });
                    }
                }
                onset+=e.ticks;
            }
        }
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) {
            if(a.microseconds!=b.microseconds)return a.microseconds<b.microseconds;
            if(a.on!=b.on)return !a.on;
            if(a.channel!=b.channel)return a.channel<b.channel;
            return a.midi<b.midi;
        });
        return result;
    }
    void Playback::send(int channel,int midi,bool on,unsigned int time,fluid_seq_id_t source) {
        std::unique_ptr<fluid_event_t,decltype(&delete_fluid_event)> event(new_fluid_event(),delete_fluid_event);
        if(!event)return;
        fluid_event_set_source(event.get(),source);
        fluid_event_set_dest(event.get(),destination_);
        if(on)fluid_event_noteon(event.get(),channel,midi,100);
        else fluid_event_noteoff(event.get(),channel,midi);
        fluid_sequencer_send_at(seq_.get(),event.get(),time,1);
    }
    bool Playback::start(const Score& score,Tick from,QString* error,Tick until,QObject* owner) {
        stop();
        if(!isReady()) {
            if(error)*error=audioState_.load()==0?"Audio is initializing.":audioError_;
            return false;
        }
        if(score.staffCount()>255) {
            if(error)*error="Playback supports up to 255 pitched staves.";
            return false;
        }
        from=std::clamp<Tick>(from,0,score.totalTicks());
        timeline_=timeline(score);
        startTick_=from;
        endTick_=until<0?score.totalTicks():std::clamp(until,from,score.totalTicks());
        const auto events=schedule(score,from,endTick_);
        for(std::size_t s=0;s<score.staffCount();++s) {
            if(fluid_synth_program_select(synth_.get(),s,soundFont_,0,score.program(s))!=FLUID_OK) {
                if(error)*error="The SoundFont does not contain the selected instrument.";
                return false;
            }
        }
        activeChannels_=static_cast<int>(score.staffCount());
        startSeqTick_=fluid_sequencer_get_tick(seq_.get())+10;
        for(const auto& e:events)send(e.channel,e.midi,e.on,startSeqTick_+static_cast<unsigned int>((e.microseconds+500)/1000),scoreSource_);
        owner_=owner;
        playing_=true;
        const auto generation=generation_.load();
        const auto finishTick=startSeqTick_+static_cast<unsigned int>((timeAt(timeline_,endTick_)-timeAt(timeline_,startTick_)+999)/1000);
        monitor_=std::jthread([this,generation,finishTick](std::stop_token stopToken) {
            std::unique_lock lock(mutex_);
            while(!stopToken.stop_requested()) {
                if(audibleTick()>=finishTick) {
                    QMetaObject::invokeMethod(this,[this,generation] {
                        if(generation_.load()==generation)emit finished();
                    },Qt::QueuedConnection);
                    return;
                }
                wake_.wait_for(lock,std::chrono::milliseconds(10),[&] {
                    return stopToken.stop_requested();
                });
            }
        });
        emit stateChanged();
        return true;
    }
    void Playback::cancelPreview() {
        if(!isReady())return;
        fluid_sequencer_remove_events(seq_.get(),previewSource_,destination_,-1);
        fluid_synth_all_sounds_off(synth_.get(),previewChannel);
    }
    void Playback::audition(const std::vector<Pitch>& pitches,int program) {
        if(!isReady() || isPlaying())return;
        fluid_sequencer_remove_events(seq_.get(),previewSource_,destination_,-1);
        fluid_synth_all_notes_off(synth_.get(),previewChannel);
        if(fluid_synth_program_select(synth_.get(),previewChannel,soundFont_,0,program)!=FLUID_OK)return;
        const auto now=fluid_sequencer_get_tick(seq_.get())+2;
        for(const auto& p:pitches) {
            send(previewChannel,p.midi(),true,now,previewSource_);
            send(previewChannel,p.midi(),false,now+250,previewSource_);
        }
    }
    void Playback::stop() {
        ++generation_;
        if(monitor_.joinable()) {
            monitor_.request_stop();
            wake_.notify_all();
            monitor_.join();
        }
        const bool changed=playing_.exchange(false);
        owner_=nullptr;
        if(isReady()) {
            fluid_sequencer_remove_events(seq_.get(),scoreSource_,destination_,-1);
            cancelPreview();
            for(int c=0;c<activeChannels_;++c)fluid_synth_all_sounds_off(synth_.get(),c);
            activeChannels_=0;
            discardOutput();
        }
        if(changed)emit stateChanged();
    }
    void Playback::setVolume(int percent) {
        volume_=std::clamp(percent,0,100);
        if(isReady())fluid_synth_set_gain(synth_.get(),0.2*volume_.load()/100.0);
        emit volumeChanged();
    }
    Tick Playback::positionTick() const {
        if(!isPlaying())return startTick_;
        const auto now=audibleTick();
        const auto elapsed=now>=startSeqTick_?static_cast<std::int64_t>(now-startSeqTick_)*1000:0;
        return std::clamp(tickAt(timeline_,timeAt(timeline_,startTick_)+elapsed),startTick_,endTick_);
    }
    QString Playback::diagnostics() const {
        if(!isReady())return errorMessage();
        auto result=configuration_+QString(" · synth CPU %1%").arg(fluid_synth_get_cpu_load(synth_.get()),0,'f',1);
#ifdef MUSIC_PULSE_OUTPUT
        if(pulseOutput_)result+=QString(" · output latency %1 ms").arg(pulseOutput_->latencyMicroseconds()/1000.0,0,'f',1);
#endif
        return result;
    }
    unsigned int Playback::audibleTick() const {
#ifdef MUSIC_PULSE_OUTPUT
        if(pulseOutput_)return static_cast<unsigned int>(pulseOutput_->presentedMicroseconds()/1000);
#endif
        return fluid_sequencer_get_tick(seq_.get());
    }
    void Playback::discardOutput() {
#ifdef MUSIC_PULSE_OUTPUT
        if(pulseOutput_)pulseOutput_->discard();
#endif
    }
}
// namespace music

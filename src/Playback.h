#pragma once
#include "Score.h"
#include <QObject>
#include <QString>
#include <fluidsynth.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
namespace music {
    class PulseOutput;
    struct ScheduledNote {
        std::int64_t microseconds = 0;
        int midi = 0;
        bool on = false;
        int channel = 0;
    };
    struct TempoSegment {
        Tick tick=0;
        std::int64_t microseconds=0;
        TempoMark tempo;
    };
    class Playback : public QObject {
        Q_OBJECT
        public:
        explicit Playback(QObject* parent=nullptr);
        ~Playback() override;
        bool start(const Score& score,Tick fromTick,QString* error=nullptr,Tick until=-1,QObject* owner=nullptr);
        [[nodiscard]] QObject* owner() const { return owner_; }
        void stop();
        void audition(const std::vector<Pitch>& pitches,int program);
        void cancelPreview();
        [[nodiscard]] bool isPlaying() const {
            return playing_.load();
        }
        [[nodiscard]] bool isReady() const {
            return audioState_.load()==1;
        }
        [[nodiscard]] QString errorMessage() const {
            return audioState_.load()==2?audioError_:QString{};
        }
        [[nodiscard]] QString diagnostics() const;
        [[nodiscard]] int volume() const { return volume_.load(); }
        void setVolume(int percent);
        [[nodiscard]] Tick positionTick() const;
        [[nodiscard]] static std::vector<TempoSegment> timeline(const Score& score);
        [[nodiscard]] static std::int64_t timeAt(const std::vector<TempoSegment>& timeline,Tick tick);
        [[nodiscard]] static Tick tickAt(const std::vector<TempoSegment>& timeline,std::int64_t micros);
        [[nodiscard]] static std::vector<ScheduledNote> schedule(const Score& score,Tick fromTick,Tick until=-1);
        signals:
        void finished();
        void readyChanged();
        void stateChanged();
        void volumeChanged();
        private:
        using SettingsPtr=std::unique_ptr<fluid_settings_t,decltype(&delete_fluid_settings)>;
        using SynthPtr=std::unique_ptr<fluid_synth_t,decltype(&delete_fluid_synth)>;
        using DriverPtr=std::unique_ptr<fluid_audio_driver_t,decltype(&delete_fluid_audio_driver)>;
        using SeqPtr=std::unique_ptr<fluid_sequencer_t,decltype(&delete_fluid_sequencer)>;
        void send(int channel,int midi,bool on,unsigned int time,fluid_seq_id_t source);
        [[nodiscard]] unsigned int audibleTick() const;
        void discardOutput();
        SettingsPtr settings_ {
            nullptr,delete_fluid_settings
        };
        SynthPtr synth_ {
            nullptr,delete_fluid_synth
        };
        DriverPtr driver_ {
            nullptr,delete_fluid_audio_driver
        };
#ifdef __linux__
        std::unique_ptr<PulseOutput> pulseOutput_;
#endif
        SeqPtr seq_ {
            nullptr,delete_fluid_sequencer
        };
        fluid_seq_id_t destination_=-1,scoreSource_=-1,previewSource_=-1;
        int soundFont_=0;
        int activeChannels_=0;
        std::atomic<int> volume_{50};
        std::jthread initWorker_,monitor_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::atomic<bool> playing_ {
            false
        };
        std::atomic<int> audioState_ {
            0
        };
        std::atomic<std::uint64_t> generation_ {
            0
        };
        QString audioError_,configuration_;
        std::vector<TempoSegment> timeline_;
        Tick startTick_=0,endTick_=0;
        QObject* owner_=nullptr; // Non-owning transport client; the client stops before destruction.
        unsigned int startSeqTick_=0;
    };
}
// namespace music

#include "Playback.h"
#include "Transformations.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QtEndian>
#include <QtTest>
#include <cmath>
#include <numbers>
#include <optional>

using namespace music;

namespace {
struct Wave {
    int rate=0;
    std::vector<double> mono;
    double peak=0;
    std::size_t clipped=0;
};

std::optional<Wave> readWave(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    const auto bytes=file.readAll();
    if(bytes.left(4)!="RIFF" || bytes.mid(8,4)!="WAVE")return {};
    Wave wave;
    int channels=0,bits=0,format=0;
    QByteArray data;
    for(qsizetype pos=12;pos+8<=bytes.size();) {
        const auto size=qFromLittleEndian<quint32>(bytes.constData()+pos+4);
        if(size>static_cast<quint64>(bytes.size()-pos-8))return {};
        const auto chunk=bytes.mid(pos,4);
        if(chunk=="fmt " && size>=16) {
            format=qFromLittleEndian<quint16>(bytes.constData()+pos+8);
            channels=qFromLittleEndian<quint16>(bytes.constData()+pos+10);
            wave.rate=qFromLittleEndian<quint32>(bytes.constData()+pos+12);
            bits=qFromLittleEndian<quint16>(bytes.constData()+pos+22);
        }
        else if(chunk=="data")data=bytes.mid(pos+8,size);
        pos+=8+size+(size%2);
    }
    if(format!=1 || channels!=2 || bits!=16 || wave.rate<=0 || data.isEmpty())return {};
    for(qsizetype pos=0;pos+2<=data.size();pos+=2) {
        const int sample=qFromLittleEndian<qint16>(data.constData()+pos);
        wave.peak=std::max(wave.peak,std::abs(sample)/32768.0);
        if(sample<=-32767 || sample>=32767)++wave.clipped;
        if(pos%(channels*2)==0)wave.mono.push_back(sample/32768.0);
    }
    return wave;
}

double power(const Wave& wave,double frequency,double from=0.15,double until=0.35)
{
    double re=0,im=0;
    const auto begin=static_cast<std::size_t>(wave.rate*from);
    const auto end=std::min(wave.mono.size(),static_cast<std::size_t>(wave.rate*until));
    for(auto i=begin;i<end;++i) {
        const double angle=2*std::numbers::pi*frequency*i/wave.rate;
        re+=wave.mono[i]*std::cos(angle);
        im+=wave.mono[i]*std::sin(angle);
    }
    return re*re+im*im;
}

double pitch(const Wave& wave)
{
    double largest=-1;
    int best=0;
    for(int frequency=420;frequency<=460;++frequency) {
        const auto energy=power(wave,frequency);
        if(energy>largest) { largest=energy; best=frequency; }
    }
    return best;
}

std::optional<Wave> capture(const QString& name,bool preview,int program,int volume,QString& error)
{
    const QString path=QStringLiteral(MUSIC_AUDIO_ARTIFACT_DIR)+"/"+name+".wav";
    qputenv("MUSIC_AUDIO_DRIVER","file");
    qputenv("MUSIC_AUDIO_FILE",path.toUtf8());
    {
        Playback playback;
        playback.setVolume(volume);
        QElapsedTimer wait;
        wait.start();
        while(!playback.isReady() && playback.errorMessage().isEmpty() && wait.elapsed()<10000)
            QTest::qWait(10);
        if(!playback.isReady()) { error=playback.errorMessage(); return {}; }
        if(preview)playback.audition({{5,4,0}},program);
        else {
            Score score(1);
            if(!score.enterNote(score.firstEventId(0),{5,4,0},4*quarter)
               || !score.setInstrument(score.staves()[0].id,program,"Audio check")
               || !playback.start(score,0,&error))return {};
        }
        QTest::qWait(preview?700:2300);
        playback.stop();
    }
    return readWave(path);
}
}

class AudioTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(QDir().mkpath(QStringLiteral(MUSIC_AUDIO_ARTIFACT_DIR)));
    }
    void scoreAndPreviewHaveCorrectPitch_data() {
        QTest::addColumn<bool>("preview");
        QTest::newRow("score")<<false;
        QTest::newRow("preview")<<true;
    }
    void scoreAndPreviewHaveCorrectPitch() {
        QFETCH(bool,preview);
        QString error;
        const auto wave=capture(preview?"preview":"score",preview,80,50,error);
        QVERIFY2(wave.has_value(),qPrintable(error));
        QCOMPARE(wave->rate,48000);
        QVERIFY(wave->mono.size()>static_cast<std::size_t>(wave->rate*(preview?0.5:2.0)));
        QCOMPARE(wave->clipped,std::size_t{0});
        QVERIFY(wave->peak>0.002 && wave->peak<0.5);
        QVERIFY(std::abs(pitch(*wave)-440)<2);
        if(!preview) {
            // A sustained tone must not drop out between its attack and scheduled note-off.
            for(int block=20;block<180;++block) {
                double energy=0;
                for(int i=block*480;i<(block+1)*480;++i)energy+=wave->mono[i]*wave->mono[i];
                QVERIFY(energy/480>1e-7);
            }
        }
    }
    void instrumentsAndVolumeChangeTheAudio() {
        QString error;
        const auto piano=capture("piano",true,0,50,error);
        const auto violin=capture("violin",true,40,50,error);
        const auto quiet=capture("quiet-piano",true,0,25,error);
        QVERIFY2(piano && violin && quiet,qPrintable(error));
        QCOMPARE(piano->clipped+violin->clipped+quiet->clipped,std::size_t{0});
        QVERIFY(std::abs(pitch(*piano)-440)<2);
        QVERIFY(std::abs(pitch(*violin)-440)<2);
        const auto pianoHarmonic=power(*piano,880)/power(*piano,440);
        const auto violinHarmonic=power(*violin,880)/power(*violin,440);
        QVERIFY(std::abs(pianoHarmonic-violinHarmonic)/(pianoHarmonic+violinHarmonic)>0.25);
        QVERIFY(quiet->peak/piano->peak>0.4 && quiet->peak/piano->peak<0.6);
    }
    void sampleClockAdvancesWhileUiIsBlocked() {
        qputenv("MUSIC_AUDIO_DRIVER","file");
        qputenv("MUSIC_AUDIO_FILE",(QStringLiteral(MUSIC_AUDIO_ARTIFACT_DIR)+"/clock.wav").toUtf8());
        Playback playback;
        QTRY_VERIFY_WITH_TIMEOUT(playback.isReady(),10000);
        Score score(3);
        QVERIFY(score.setTempo(2,TempoMark{quarter,60}));
        const auto from=score.measureStart(2);
        QString error;
        QVERIFY2(playback.start(score,from,&error),qPrintable(error));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto position=playback.positionTick();
        QVERIFY(position>from+quarter/10 && position<from+quarter/4);
        playback.stop();
        QVERIFY(!playback.isPlaying());
        QVERIFY2(playback.start(score,0,&error),qPrintable(error));
        QVERIFY(playback.positionTick()<quarter/10);
        playback.stop();
    }
    void music21TransposeAndRetrogradeSoundCorrect() {
        QString python=qEnvironmentVariable("MUSIC_PYTHON");
        if(python.isEmpty())python="python3";
        QProcess probe;
        probe.start(python,{"-c","import music21"});
        if(!probe.waitForFinished(30000) || probe.exitCode()!=0)QSKIP("Real music21 is unavailable; run this test through Poetry.");
        Score source(1);
        auto id=source.firstEventId(0);
        for(int step:{0,2,4}) {
            QVERIFY(source.enterNote(id,{step,4,0},quarter));
            id=source.nextEventId(id);
        }
        const auto fragment=source.copyRange({0,3*quarter,{source.staves().front().id}});
        QVERIFY(fragment);
        QProcess worker;
        worker.start(python,{"-u",QCoreApplication::applicationDirPath()+"/python/worker.py"});
        QVERIFY(worker.waitForStarted(5000));
        const auto request=transformationRequest(*fragment,{{TransformKind::Transpose,2},{TransformKind::Retrograde}},"audio");
        QCOMPARE(worker.write(request),request.size());
        QTRY_VERIFY_WITH_TIMEOUT(worker.canReadLine(),10000);
        QString error;
        TransformContext context;
        context.program=80;
        const auto result=transformationResponse(worker.readLine(),"audio",context,&error);
        QVERIFY2(result,qPrintable(error));
        worker.closeWriteChannel();
        QVERIFY(worker.waitForFinished(5000));
        const auto path=QStringLiteral(MUSIC_AUDIO_ARTIFACT_DIR)+"/music21-transpose-retrograde.wav";
        qputenv("MUSIC_AUDIO_DRIVER","file");
        qputenv("MUSIC_AUDIO_FILE",path.toUtf8());
        {
            Playback playback;
            QTRY_VERIFY_WITH_TIMEOUT(playback.isReady(),10000);
            QVERIFY2(playback.start(result->score,result->start,&error,result->end),qPrintable(error));
            QTest::qWait(1700);
            playback.stop();
        }
        const auto wave=readWave(path);
        QVERIFY(wave);
        QVERIFY(wave->mono.size()>=static_cast<std::size_t>(wave->rate*1.6));
        QCOMPARE(wave->clipped,std::size_t{0});
        QVERIFY(wave->peak>0.002 && wave->peak<0.5);
        const double frequencies[]{440,369.994,293.665};
        for(int event=0;event<3;++event) {
            double best=0;
            double largest=-1;
            for(int offset=-5;offset<=5;++offset) {
                const double frequency=frequencies[event]+offset;
                const double energy=power(*wave,frequency,event*0.5+0.15,event*0.5+0.35);
                if(energy>largest) { largest=energy; best=frequency; }
            }
            QVERIFY(std::abs(best-frequencies[event])<2);
            for(int block=15;block<35;++block) {
                double energy=0;
                const auto start=event*24000+block*480;
                for(int i=start;i<start+480;++i)energy+=wave->mono[i]*wave->mono[i];
                QVERIFY(energy/480>1e-7);
            }
        }
    }
    void cleanupTestCase() {
        qunsetenv("MUSIC_AUDIO_DRIVER");
        qunsetenv("MUSIC_AUDIO_FILE");
    }
};

QTEST_GUILESS_MAIN(AudioTests)
#include "AudioTests.moc"

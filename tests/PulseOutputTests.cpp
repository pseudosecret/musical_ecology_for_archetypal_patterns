#include "Playback.h"
#include "Transformations.h"
#include "fixtures/PulseDevice.h"
#include <QElapsedTimer>
#include <QtTest>
#include <cmath>

using namespace music;

class PulseOutputTests:public QObject {
    Q_OBJECT
private slots:
    void init() {
        pulse_test::reset();
        qputenv("MUSIC_AUDIO_DRIVER","pulseaudio");
        qputenv("PULSE_SERVER","unix:/mnt/wslg/PulseServer");
        qunsetenv("MUSIC_AUDIO_BUFFER");
    }
    void boundedQueueAndRapidPreviews() {
        Playback playback;
        QTRY_VERIFY_WITH_TIMEOUT(playback.isReady(),10000);
        QTest::qWait(150);
        QElapsedTimer timer;
        for(int i=0;i<12;++i) {
            timer.start();
            playback.audition({{5,4,0},{0,5,0}},0);
            QVERIFY(timer.nsecsElapsed()<50'000'000);
            QTest::qWait(40);
        }
        const auto capture=pulse_test::capture();
        QCOMPARE(capture.format.rate,48000u);
        QCOMPARE(capture.format.format,PA_SAMPLE_FLOAT32NE);
        QCOMPARE(capture.buffering.maxlength,4096u*2*sizeof(float));
        QCOMPARE(capture.buffering.tlength,capture.buffering.maxlength);
        QCOMPARE(capture.buffering.prebuf,capture.buffering.tlength);
        QVERIFY(capture.flags&PA_STREAM_AUTO_TIMING_UPDATE);
        QVERIFY(capture.flags&PA_STREAM_INTERPOLATE_TIMING);
        QVERIFY(capture.largestWrite<=256u*2*sizeof(float));
        double peak=0;
        for(const auto sample:capture.samples) { QVERIFY(std::isfinite(sample)); peak=std::max(peak,std::abs(double(sample))); }
        QVERIFY(peak>0.01 && peak<1);
        QVERIFY(playback.diagnostics().contains("output latency"));
    }
    void delayedTimingDoesNotStarveOutput() {
        pulse_test::reset(0,180'000);
        Playback playback;
        QTRY_VERIFY_WITH_TIMEOUT(playback.isReady(),10000);
        playback.audition({{5,4,0}},80);
        QTest::qWait(450);
        const auto capture=pulse_test::capture();
        QVERIFY(capture.timingQueries>1);
        QVERIFY(capture.missingTiming>0);
        QCOMPARE(capture.underruns,0);
        Score score(1);
        QVERIFY(score.enterNote(score.firstEventId(0),{5,4,0},4*quarter));
        QVERIFY(playback.start(score,0));
        QTest::qWait(450);
        const auto afterFlush=pulse_test::capture();
        QVERIFY(afterFlush.flushes>0);
        QVERIFY(afterFlush.missingTiming>capture.missingTiming);
        QCOMPARE(afterFlush.underruns,0);
        playback.stop();
    }
    void cursorAndCompletionFollowHeardSamples() {
        pulse_test::reset(200'000);
        Playback playback;
        QTRY_VERIFY_WITH_TIMEOUT(playback.isReady(),10000);
        Score score(3);
        const auto from=score.measureStart(1);
        QVERIFY(score.enterNote(score.firstEventId(1),{5,4,0},quarter));
        QString error;
        QVERIFY2(playback.start(score,from,&error,from+quarter),qPrintable(error));
        const auto clockAtStart=static_cast<std::int64_t>(pulse_test::capture().samples.size()/2)*1'000'000/48000+10'000;
        QSignalSpy finished(&playback,&Playback::finished);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        QCOMPARE(playback.positionTick(),from);
        QTest::qWait(250);
        const auto heard=std::max<std::int64_t>(0,pulse_test::playedMicroseconds()-clockAtStart);
        const auto expected=Playback::tickAt(Playback::timeline(score),Playback::timeAt(Playback::timeline(score),from)+heard);
        QVERIFY(std::abs(playback.positionTick()-expected)<quarter/20);
        QCOMPARE(finished.size(),0);
        // Rendering the last note is insufficient: completion waits for the queued output.
        QTest::qWait(140);
        QCOMPARE(finished.size(),0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,1000);
        playback.stop();
        QVERIFY(!playback.isPlaying());
    }
    void stopRestartAndScratchDiscardOldOutput() {
        Playback playback;
        QTRY_VERIFY_WITH_TIMEOUT(playback.isReady(),10000);
        Score main(1);
        QVERIFY(main.enterNote(main.firstEventId(0),{0,4,0},4*quarter));
        QString error;
        QVERIFY(playback.start(main,0,&error));
        QTest::qWait(120);
        const auto before=pulse_test::capture().flushes;
        ScoreFragment transformed;
        transformed.duration=2*quarter;
        transformed.rows={{{quarter,{{5,4,0}},{false}},{quarter,{{1,4,0}},{false}}}};
        const auto scratch=buildTransformationResult(transformed,{},&error);
        QVERIFY2(scratch,qPrintable(error));
        QVERIFY(playback.start(scratch->score,scratch->start,&error,scratch->end));
        QTRY_VERIFY(pulse_test::capture().flushes>before);
        QTest::qWait(200);
        playback.stop();
        const auto stopped=pulse_test::capture().flushes;
        QTRY_VERIFY(pulse_test::capture().flushes>stopped);
        QVERIFY(playback.start(main,0,&error));
        QVERIFY(playback.positionTick()<quarter/10);
        playback.stop();
    }
    void deviceLossDisablesPlayback() {
        Playback playback;
        QTRY_VERIFY_WITH_TIMEOUT(playback.isReady(),10000);
        Score score(1);
        QVERIFY(playback.start(score,0));
        pulse_test::disconnect();
        QTRY_VERIFY(!playback.isReady());
        QVERIFY(!playback.isPlaying());
        QVERIFY(playback.errorMessage().contains("disconnected"));
        QVERIFY(!playback.start(score,0));
    }
};
QTEST_GUILESS_MAIN(PulseOutputTests)
#include "PulseOutputTests.moc"

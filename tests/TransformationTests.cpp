#include "TransformWorkspace.h"
#include "Transformations.h"
#include "ScoreClipboard.h"
#include "MusicXml.h"
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace music;
namespace {
ScoreFragment notes(std::vector<Tick> durations={quarter,quarter/2,quarter/2}) {
    ScoreFragment fragment{0,{{}}};
    int step=0;
    for(auto ticks:durations) {
        fragment.duration+=ticks;
        fragment.rows.front().push_back({ticks,{{step++,4,0}},{false}});
    }
    return fragment;
}
QVariantList identity() { return {QVariantMap{{"kind","transpose"},{"amount",0}}}; }
void configure(TransformWorkspace& workspace,const char* mode) {
    workspace.setWorkerCommand("python3",{QString(MUSIC_TEST_WORKER),QString::fromLatin1(mode)});
}
void enter(Editor& editor) { QVERIFY(editor.handleKey(Qt::Key_C,0)); }
}
class TransformationTests : public QObject {
    Q_OBJECT
private slots:
    void normalizeLongDurations();
    void exactFractionsAndTuplets();
    void sourceContextAndClipboard();
    void protocolValidation();
    void independentResultsAndHistory();
    void workerFailures_data();
    void workerFailures();
    void cancellationAndTimeout();
    void missingWorkerAndDependency();
    void sharedTransportAndBounds();
    void realMusic21Bridge_data();
    void realMusic21Bridge();
    void oneStaffShortcut();
    void orchestralRegion();
};
void TransformationTests::normalizeLongDurations() {
    auto fragment=notes({12*quarter});
    std::string error;
    const auto normalized=normalizeFragment(fragment,&error);
    QVERIFY2(normalized,error.c_str());
    QVERIFY(normalized->valid());
    QVERIFY(normalized->rows.front().size()>1);
    TransformContext context;
    QString reason;
    const auto result=buildTransformationResult(fragment,context,&reason);
    QVERIFY2(result,qPrintable(reason));
    QVERIFY(result->score.valid());
    QCOMPARE(result->end,12*quarter);
    const auto schedule=Playback::schedule(result->score,0,result->end);
    QCOMPARE(schedule.size(),std::size_t{2});
    QCOMPARE(schedule.back().microseconds,std::int64_t{6'000'000});
    fragment.rows.front()[0].ticks=1;
    fragment.duration=1;
    QVERIFY(!normalizeFragment(fragment));
}
void TransformationTests::exactFractionsAndTuplets() {
    for(int count:{3,5,7}) {
        Score source(1);
        const auto id=source.firstEventId(0);
        QVERIFY(source.enterNote(id,{0,4,0},quarter));
        QVERIFY(source.createTuplet(id,count));
        const auto fragment=source.copyRange({0,quarter,{source.staves()[0].id}});
        QVERIFY(fragment);
        const auto result=buildTransformationResult(*fragment,{},nullptr);
        QVERIFY(result);
        QCOMPARE(result->score.measure(0).tuplets.size(),std::size_t{1});
        QCOMPARE(result->score.measure(0).tuplets.front().actual,count);
        QCOMPARE(result->score.measure(0).tuplets.front().span(),quarter);
        const auto again=result->score.copyRange({0,quarter,{result->score.staves()[0].id}});
        QVERIFY(again && again->valid());
    }
    auto fraction=notes({quarter/3,quarter/3,quarter/3});
    const auto result=buildTransformationResult(fraction,{},nullptr);
    QVERIFY(result);
    QVERIFY(result->score.valid());
    QCOMPARE(Playback::schedule(result->score,0,quarter).size(),std::size_t{6});
    QCOMPARE(result->score.measure(0).tuplets.size(),std::size_t{1});
    fraction.rows[0][1].pitches={{9,4,0}};
    QVERIFY(!buildTransformationResult(fraction,{},nullptr));
}
void TransformationTests::sourceContextAndClipboard() {
    TransformContext context{Score::defaultMeter(7,8),{quarter,80},Clef::Bass,42,quarter/2,"Source"};
    const auto result=buildTransformationResult(notes(),context,nullptr);
    QVERIFY(result);
    QCOMPARE(result->start,quarter/2);
    QCOMPARE(result->end,5*quarter/2);
    QCOMPARE(result->score.program(0),42);
    QCOMPARE(result->score.clefAt(0,0),Clef::Bass);
    QCOMPARE(result->score.tempoAt(0).bpm,80);
    QCOMPARE(result->score.bar(0).meter,context.meter);
    Editor main;
    Editor scratch(main.audioEngine(),true);
    scratch.installScratch(result->score,result->start,result->end,"Result");
    QVERIFY(scratch.configureStaff(scratch.score().staves()[0].id,"Result",42));
    QVERIFY(scratch.hasRangeSelection());
    QVERIFY(scratch.copySelection());
    const auto* mime=QGuiApplication::clipboard()->mimeData();
    const auto copied=decodeScoreFragment(mime->data(scoreFragmentMime));
    QVERIFY(copied);
    QCOMPARE(copied->duration,2*quarter);
    QVERIFY(main.addPart(10));
    main.selectMeasure(1,1);
    QVERIFY(main.pasteSelection());
    QCOMPARE(main.score().program(1),10);
    QCOMPARE(main.score().event(main.score().eventAt(1,4*quarter))->pitches[0].step,0);
    main.undo();
    QVERIFY(main.score().event(main.score().firstEventId(1,1))->isRest());
    QTemporaryDir dir;
    QVERIFY(scratch.saveAs(QUrl::fromLocalFile(dir.filePath("result.musicxml"))));
    QTRY_VERIFY(!scratch.fileBusy());
    Score reopened(1);
    QVERIFY(MusicXml::open(dir.filePath("result.musicxml"),&reopened));
    QCOMPARE(reopened.program(0),42);
    QCOMPARE(reopened.totalTicks(),result->score.totalTicks());
}
void TransformationTests::protocolValidation() {
    QString error;
    QVERIFY(!transformSteps({},&error));
    QVERIFY(!transformSteps({QVariantMap{{"kind","scale"},{"numerator",0},{"denominator",1}}},&error));
    const auto steps=transformSteps(identity(),&error);
    QVERIFY(steps);
    const auto bytes=transformationRequest(notes(),*steps,"17");
    auto root=QJsonDocument::fromJson(bytes).object();
    root["ok"]=true;
    auto response=QJsonDocument(root).toJson();
    QVERIFY(transformationResponse(response,"17",{},&error));
    QVERIFY(!transformationResponse(response,"18",{},&error));
    root["version"]=1;
    QVERIFY(!transformationResponse(QJsonDocument(root).toJson(),"17",{},&error));
    QVERIFY(!transformationResponse("{bad","17",{},&error));
}
void TransformationTests::independentResultsAndHistory() {
    Editor main;
    enter(main);
    TransformWorkspace workspace(&main);
    configure(workspace,"identity");
    const auto before=main.score().measure(0);
    QVERIFY(workspace.begin(&main));
    QVERIFY(workspace.submit(identity()));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
    QVERIFY2(workspace.currentEditor(),qPrintable(workspace.status()));
    auto* first=workspace.currentEditor();
    QCOMPARE(main.score().measure(0),before);
    QCOMPARE(first->audioEngine().get(),main.audioEngine().get());
    QVERIFY(!first->dirty());
    QVERIFY(!first->canUndo());
    first->selectEvent(first->score().eventAt(0,0));
    QVERIFY(first->handleKey(Qt::Key_D,0));
    QVERIFY(first->canUndo());
    QCOMPARE(main.score().measure(0),before);
    QVERIFY(workspace.begin(first));
    QVERIFY(workspace.submit(identity()));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
    QCOMPARE(workspace.tabs().size(),2);
    auto* second=workspace.currentEditor();
    QVERIFY(second!=first);
    first->undo();
    QCOMPARE(first->score().event(first->score().eventAt(0,0))->pitches[0].step,0);
    QCOMPARE(second->score().event(second->score().eventAt(0,0))->pitches[0].step,1);
    workspace.setFloating(false);
    workspace.setCurrentIndex(0);
    QCOMPARE(workspace.currentEditor(),first);
    QVERIFY(!workspace.closeResult(0));
    QVERIFY(workspace.closeResult(0,true));
    QCOMPARE(workspace.currentEditor(),second);
    QVERIFY(workspace.closeResult(0));
    QCOMPARE(workspace.currentIndex(),-1);
}
void TransformationTests::workerFailures_data() {
    QTest::addColumn<QString>("mode");
    for(const auto* mode:{"crash","malformed","wrong_id","bad_pitch","bad_tie","bad_key","oversize"})QTest::newRow(mode)<<QString(mode);
}
void TransformationTests::workerFailures() {
    QFETCH(QString,mode);
    Editor main;
    enter(main);
    TransformWorkspace workspace(&main);
    configure(workspace,mode.toLatin1().constData());
    QVERIFY(workspace.begin(&main));
    const auto before=main.score().measure(0);
    QVERIFY(workspace.submit(identity()));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),7000);
    QVERIFY(!workspace.currentEditor());
    QVERIFY(!workspace.status().isEmpty());
    QCOMPARE(main.score().measure(0),before);
}
void TransformationTests::cancellationAndTimeout() {
    Editor main;
    enter(main);
    TransformWorkspace workspace(&main);
    configure(workspace,"delay");
    QVERIFY(workspace.begin(&main));
    QVERIFY(workspace.submit(identity()));
    workspace.cancel();
    QVERIFY(!workspace.busy());
    configure(workspace,"identity");
    QVERIFY(workspace.submit(identity()));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
    QCOMPARE(workspace.tabs().size(),1);
    configure(workspace,"delay");
    workspace.setTimeout(50);
    QVERIFY(workspace.submit(identity()));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),3000);
    QVERIFY(workspace.status().contains("timed out"));
    QCOMPARE(workspace.tabs().size(),1);
}
void TransformationTests::missingWorkerAndDependency() {
    Editor main;
    enter(main);
    TransformWorkspace workspace(&main);
    workspace.setWorkerCommand("/no/such/python",{});
    QVERIFY(workspace.begin(&main));
    QVERIFY(workspace.submit(identity()));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
    QVERIFY(!workspace.currentEditor());
    workspace.setWorkerCommand("python3",{"-S",QString(MUSIC_REAL_WORKER)});
    QVERIFY(workspace.submit(identity()));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
    QVERIFY(!workspace.currentEditor());
    QVERIFY(workspace.status().contains("music21"));
}
void TransformationTests::sharedTransportAndBounds() {
    Editor main;
    QTRY_VERIFY_WITH_TIMEOUT(main.audioReady(),10000);
    auto result=buildTransformationResult(notes({quarter/2}),{Meter{},TempoMark{},Clef::Treble,40,quarter/2,{}},nullptr);
    QVERIFY(result);
    Editor scratch(main.audioEngine(),true);
    scratch.installScratch(std::move(result->score),result->start,result->end,"Result");
    scratch.play(true);
    QVERIFY(scratch.playing());
    QVERIFY(!main.playing());
    QVERIFY(main.playbackBusy());
    const auto before=main.score().measure(0);
    QVERIFY(main.handleKey(Qt::Key_C,0));
    QCOMPARE(main.score().measure(0),before);
    main.play();
    QVERIFY(main.playing());
    QVERIFY(!scratch.playing());
    scratch.stop();
    QVERIFY(!main.playbackBusy());
    scratch.play();
    QTRY_VERIFY_WITH_TIMEOUT(!scratch.playing(),3000);
    const auto events=Playback::schedule(scratch.score(),quarter/2,quarter);
    QCOMPARE(events.size(),std::size_t{2});
    QCOMPARE(events[0].microseconds,std::int64_t{0});
    QCOMPARE(events[1].microseconds,std::int64_t{250000});
}
void TransformationTests::realMusic21Bridge_data() {
    QTest::addColumn<int>("octave");
    QTest::addColumn<QList<int>>("sourceMidi");
    QTest::addColumn<bool>("combined");
    QTest::addColumn<QList<int>>("expectedMidi");
    QTest::newRow("C4-identity")<<4<<QList<int>{60,64,67}<<false<<QList<int>{60,64,67};
    QTest::newRow("C5-identity")<<5<<QList<int>{72,76,79}<<false<<QList<int>{72,76,79};
    QTest::newRow("C4-combined")<<4<<QList<int>{60,64,67}<<true<<QList<int>{58,54,51};
    QTest::newRow("C5-combined")<<5<<QList<int>{72,76,79}<<true<<QList<int>{46,42,39};
}
void TransformationTests::realMusic21Bridge() {
    QFETCH(int,octave);
    QFETCH(QList<int>,sourceMidi);
    QFETCH(bool,combined);
    QFETCH(QList<int>,expectedMidi);
    QString python=qEnvironmentVariable("MUSIC_PYTHON");
    if(python.isEmpty())python="python3";
    QProcess probe;
    probe.start(python,{"-c","import music21"});
    if(!probe.waitForFinished(30000) || probe.exitCode()!=0)QSKIP("Real music21 is unavailable; protocol and exact-math checks run separately.");
    Score score(1);
    const auto id=score.firstEventId(0);
    QVERIFY(score.enterNote(id,{0,octave,0},quarter));
    QVERIFY(score.addChordPitch(id,{2,octave,0}));
    QVERIFY(score.addChordPitch(id,{4,octave,0}));
    Editor main;
    main.setAuditionEnabled(false);
    main.installScratch(std::move(score),0,quarter,"Source");
    QVERIFY(main.configureStaff(main.score().staves()[0].id,"Violin",40));
    const auto* source=main.score().event(main.score().eventAt(0,0));
    QVERIFY(source);
    const auto sourcePitches=source->pitches;
    QCOMPARE(sourcePitches.size(),std::size_t{3});
    for(std::size_t i=0;i<sourcePitches.size();++i)QCOMPARE(sourcePitches[i].midi(),sourceMidi[i]);
    const auto before=main.score().measure(0);
    TransformWorkspace workspace(&main);
    workspace.setWorkerCommand(python,{"-u",QString(MUSIC_REAL_WORKER)});
    QVERIFY(workspace.begin(&main));
    const QVariantList steps=combined?QVariantList{QVariantMap{{"kind","transpose"},{"amount",2}},
                                                 QVariantMap{{"kind","invert"},{"amount",60}},
                                                 QVariantMap{{"kind","scale"},{"numerator",3},{"denominator",2}},
                                                 QVariantMap{{"kind","retrograde"}}}:identity();
    QVERIFY(workspace.submit(steps));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),30000);
    auto* result=workspace.currentEditor();
    QVERIFY2(result,qPrintable(workspace.status()));
    QCOMPARE(result->selectedRange().end-result->selectedRange().start,combined?3*quarter/2:quarter);
    QCOMPARE(result->score().program(0),40);
    const auto* event=result->score().event(result->score().eventAt(0,0));
    QVERIFY(event);
    QCOMPARE(event->pitches.size(),std::size_t{3});
    for(std::size_t i=0;i<event->pitches.size();++i)QCOMPARE(event->pitches[i].midi(),expectedMidi[i]);
    if(!combined)QCOMPARE(event->pitches,sourcePitches);
    QCOMPARE(main.score().measure(0),before);
}

void TransformationTests::oneStaffShortcut() {
    Editor editor;
    QSignalSpy transforms(&editor,&Editor::transformationRequested),tempos(&editor,&Editor::tempoPropertiesRequested);
    QVERIFY(editor.handleKey(Qt::Key_T,0));
    QCOMPARE(transforms.size(),1);
    QVERIFY(editor.handleKey(Qt::Key_T,Qt::ShiftModifier));
    QCOMPARE(tempos.size(),1);
    QVERIFY(editor.addPart(4));
    editor.selectMeasure(0,0);
    ScoreHit hit; hit.kind=HitKind::Measure; hit.staff=1; hit.measure=0;
    editor.activateHit(hit,false,Qt::ControlModifier);
    QVERIFY(editor.selectedRange().staves.size()>1);
    QVERIFY(editor.handleKey(Qt::Key_T,0));
    QCOMPARE(transforms.size(),1);
}
void TransformationTests::orchestralRegion() {
    Score source(400);
    for(int i=1;i<30;++i)QVERIFY(source.addPart("Part",i));
    const auto sourceStaff=source.staves()[15].id;
    QVERIFY(source.enterNote(source.firstEventId(80,15),{0,4,0},quarter));
    std::vector<qint64> times;
    QElapsedTimer timer;
    for(int i=0;i<50;++i) {
        timer.start();
        const auto fragment=source.copyRange({320*quarter,342*quarter,{sourceStaff}});
        QVERIFY(fragment);
        const auto result=buildTransformationResult(*fragment,{},nullptr);
        QVERIFY(result);
        LayoutIndex layout; layout.reset(result->score);
        times.push_back(timer.nsecsElapsed());
    }
    std::sort(times.begin(),times.end());
    qInfo()<<"30 staff / 400 bar selected-region capture + result validation/layout p95 ms"<<times[47]/1e6;
    QVERIFY(times[47]<50'000'000);
}
int main(int argc,char** argv) {
    QTemporaryDir audio;
    qputenv("MUSIC_AUDIO_DRIVER","file");
    qputenv("MUSIC_AUDIO_FILE",audio.filePath("transform.wav").toUtf8());
    QGuiApplication app(argc,argv);
    QFontDatabase::addApplicationFont(":/assets/Bravura.otf");
    TransformationTests test;
    return QTest::qExec(&test,argc,argv);
}
#include "TransformationTests.moc"

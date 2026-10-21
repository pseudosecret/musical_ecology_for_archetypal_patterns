#include "Engraving.h"
#include "Editor.h"
#include "MusicXml.h"
#include "Playback.h"
#include "Score.h"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace music;

class ScoreTests : public QObject {
    Q_OBJECT

private slots:
    void noteChordAndRests();
    void durationNeverOverwritesNotes();
    void undoRedoOneMeasure();
    void musicXmlRoundTrip();
    void unsupportedXmlLeavesScoreIntact();
    void localEditInLongScore();
    void coldVisibleLayoutInLongScore();
    void keyboardEntryAndChords();
    void playbackSchedulesChordsAndRestarts();
    void playbackRejectsEditsAndCanRestart();
    void engravingProducesVisibleScore();
    void overwriteRhythmAndClamp();
    void meterRebarAndClefs();
    void multiStaffMusicXml();
    void instrumentGroups();
    void tiesAndTempoSchedule();
    void engravingBeamsAndReflow();
    void orchestralFixture();
    void backgroundOpenSave();
    void shortRestEntryAfterTempoChange();
    void clefChangesCanBeRemovedAndUndone();
    void selectedGlyphUsesOrangeWithoutChangingLayout();
};

void ScoreTests::shortRestEntryAfterTempoChange()
{
    Editor editor;
    editor.setAuditionEnabled(false);
    const auto first=editor.score().firstEventId(0);
    QVERIFY(editor.handleKey(Qt::Key_C,0));
    QVERIFY(editor.handleKey(Qt::Key_D,0));
    const auto second=editor.score().nextEventId(first);
    editor.selectEvent(first);
    QVERIFY(editor.handleKey(Qt::Key_BracketLeft,0));
    const auto shortRest=editor.score().nextEventId(first);
    QCOMPARE(editor.score().event(shortRest)->ticks,quarter/2);
    editor.selectEvent(second);
    QVERIFY(editor.handleKey(Qt::Key_BracketRight,0));
    editor.selectEvent(shortRest);
    QVERIFY(editor.applyTempo(quarter,90,false));
    QVERIFY(editor.handleKey(Qt::Key_E,0));
    QCOMPARE(editor.score().event(shortRest)->ticks,quarter/2);
    QCOMPARE(editor.score().event(shortRest)->pitches.front().step,2);
    QCOMPARE(editor.score().event(second)->pitches.front().step,1);
    QVERIFY(editor.score().valid());
    editor.undo();
    QVERIFY(editor.score().event(shortRest)->isRest());
}

void ScoreTests::clefChangesCanBeRemovedAndUndone()
{
    Editor editor;
    editor.setAuditionEnabled(false);
    QVERIFY(!editor.canRemoveClef());
    QVERIFY(!editor.removeClef());
    QVERIFY(editor.handleKey(Qt::Key_C,0));
    const auto next=editor.score().nextEventId(editor.score().firstEventId(0));
    editor.selectEvent(next);
    QVERIFY(editor.applyClef(static_cast<int>(Clef::Bass)));
    QCOMPARE(editor.score().clefAt(0,quarter),Clef::Bass);
    QVERIFY(editor.canRemoveClef());
    const ScoreHit hit{HitKind::Clef,0,0,0,0,quarter};
    editor.activateHit(hit,false);
    QCOMPARE(editor.selectedClef(),static_cast<int>(Clef::Bass));
    QVERIFY(editor.canRemoveClef());
    QVERIFY(editor.handleKey(Qt::Key_Delete,0));
    QCOMPARE(editor.score().clefAt(0,quarter),Clef::Treble);
    QCOMPARE(editor.score().staves()[0].clefs.size(),std::size_t{1});
    QVERIFY(!editor.score().event(editor.score().firstEventId(0))->isRest());
    editor.undo();
    QCOMPARE(editor.score().clefAt(0,quarter),Clef::Bass);
    editor.redo();
    QCOMPARE(editor.score().clefAt(0,quarter),Clef::Treble);
    QVERIFY(editor.score().valid());
}

void ScoreTests::selectedGlyphUsesOrangeWithoutChangingLayout()
{
    Score score(1);
    const auto id=score.firstEventId(0);
    QVERIFY(score.enterNote(id,{0,4,0},quarter));
    QVERIFY(score.addChordPitch(id,{1,4,0}));
    const auto snapshot=Engraving::layout(score,0);
    const auto original=Engraving::render(snapshot);
    QRectF bounds;
    const auto selected=Engraving::renderSelection(snapshot,id,1,bounds);
    QVERIFY(!selected.isNull());
    const auto& event=snapshot.events.front();
    QCOMPARE(bounds.center(),QPointF(event.headX[1],event.toneY[1]));
    int orange=0;
    for(int y=0;y<selected.height();++y)for(int x=0;x<selected.width();++x) {
        const auto color=selected.pixelColor(x,y);
        if(color.alpha()>200 && color.red()>180 && color.green()>60 && color.green()<150 && color.blue()<60)++orange;
    }
    QVERIFY(orange>10);
    QCOMPARE(Engraving::render(snapshot),original);
    QVERIFY(Engraving::renderSelection(snapshot,0,0,bounds).isNull());
}

void ScoreTests::noteChordAndRests()
{
    Score score(1);
    const auto id = score.firstEventId(0);
    QVERIFY(score.enterNote(id, Pitch{0, 4, 0}, quarter));
    QCOMPARE(score.measure(0).events.front().ticks, quarter);
    QCOMPARE(score.measure(0).events.back().ticks, Tick{3*quarter});
    std::size_t index = 99;
    QVERIFY(score.addChordPitch(id, Pitch{2, 4, 0}, &index));
    QCOMPARE(index, std::size_t{1});
    QCOMPARE(score.event(id)->pitches.size(), std::size_t{2});
    QVERIFY(!score.addChordPitch(id, Pitch{2, 4, 0}));
    QVERIFY(Score::validMeasure(score.measure(0)));
}

void ScoreTests::durationNeverOverwritesNotes()
{
    Score score(1);
    const auto first = score.firstEventId(0);
    QVERIFY(score.enterNote(first, Pitch{0, 4, 0}, quarter));
    const auto second = score.nextEventId(first);
    QVERIFY(score.enterNote(second, Pitch{2, 4, 0}, quarter));
    QVERIFY(!score.setDuration(first, 2 * quarter));
    QCOMPARE(score.event(second)->pitches.front().step, 2);
    QVERIFY(score.setDuration(second, quarter / 2));
    QCOMPARE(score.nextEventId(second) != EventId{0}, true);
    QVERIFY(score.setDuration(second, quarter));
    QVERIFY(Score::validMeasure(score.measure(0)));
}

void ScoreTests::undoRedoOneMeasure()
{
    Score score(2);
    const auto id = score.firstEventId(1);
    const auto before = score.measure(1);
    QVERIFY(score.enterNote(id, Pitch{6, 4, 0}, quarter));
    const auto after = score.measure(1);
    EditHistory history;
    history.record(EditChange{1, before, after, {}});
    QVERIFY(history.undo(score));
    QCOMPARE(score.measure(1), before);
    QVERIFY(history.redo(score));
    QCOMPARE(score.measure(1), after);
}

void ScoreTests::musicXmlRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Score score(2);
    const auto first = score.firstEventId(0);
    QVERIFY(score.enterNote(first, Pitch{0, 4, 1}, Tick{3*quarter/2}));
    QVERIFY(score.addChordPitch(first, Pitch{4, 4, 0}));
    QString error;
    const auto path = dir.filePath("roundtrip.musicxml");
    QVERIFY2(MusicXml::save(score, path, &error), qPrintable(error));
    Score opened(1);
    QVERIFY2(MusicXml::open(path, &opened, &error), qPrintable(error));
    QCOMPARE(opened.measureCount(), score.measureCount());
    QCOMPARE(opened.measure(0).events.front().ticks, Tick{3*quarter/2});
    QCOMPARE(opened.measure(0).events.front().pitches.size(), std::size_t{2});
    QCOMPARE(opened.measure(0).events.front().pitches.front().alter, 1);
    QVERIFY(Score::validMeasure(opened.measure(0)));
}

void ScoreTests::unsupportedXmlLeavesScoreIntact()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Score score(1);
    const auto originalId = score.firstEventId(0);
    const auto path = dir.filePath("unsupported.musicxml");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<score-partwise><part id='P1'><measure><note><rest/><duration>960</duration>"
               "<voice>2</voice><type>quarter</type></note></measure></part></score-partwise>");
    file.close();
    QString error;
    QVERIFY(!MusicXml::open(path, &score, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(score.firstEventId(0), originalId);
}

void ScoreTests::localEditInLongScore()
{
    Score score(2000);
    const auto id = score.firstEventId(1999);
    QVERIFY(score.enterNote(id, Pitch{0, 4, 0}, quarter));
    QBENCHMARK {
        QVERIFY(score.setPitch(id, 0, Pitch{2, 4, 0}));
        QVERIFY(score.setPitch(id, 0, Pitch{0, 4, 0}));
    }
}

void ScoreTests::coldVisibleLayoutInLongScore()
{
    Score score(2000);
    LayoutIndex index; index.reset(score);
    QBENCHMARK {
        const auto system = Engraving::layout(score, index, 999);
        QCOMPARE(system.events.size(), std::size_t{2});
    }
}

void ScoreTests::keyboardEntryAndChords()
{
    Editor editor;
    QVERIFY(editor.handleKey(Qt::Key_C, 0));
    const auto id = editor.score().firstEventId(0);
    QCOMPARE(editor.score().event(id)->pitches.front(), (Pitch{0, 5, 0}));
    QVERIFY(editor.handleKey(Qt::Key_E, Qt::ShiftModifier));
    QCOMPARE(editor.score().event(id)->pitches.size(), std::size_t{2});
    QVERIFY(editor.handleKey(Qt::Key_D, 0));
    const auto nextId = editor.score().nextEventId(id);
    QCOMPARE(editor.score().event(nextId)->pitches.front().step, 1);
    editor.selectEvent(id, 1);
    QVERIFY(editor.handleKey(Qt::Key_Up, Qt::ControlModifier));
    QCOMPARE(editor.score().event(id)->pitches[1].octave, 6);
    QVERIFY(editor.handleKey(Qt::Key_Up, Qt::ShiftModifier));
    QCOMPARE(editor.score().event(id)->pitches[1].midi(), 89);
    QVERIFY(editor.handleKey(Qt::Key_Plus, Qt::ShiftModifier));
    QCOMPARE(editor.score().event(id)->pitches[1].alter, 1);
    QVERIFY(!editor.handleKey(Qt::Key_BracketRight, Qt::ControlModifier)); // the following note occupies that time
    editor.selectEvent(nextId);
    QVERIFY(editor.handleKey(Qt::Key_BracketRight, 0));
    QCOMPARE(editor.score().event(nextId)->ticks, 2 * quarter);
    QVERIFY(editor.handleKey(Qt::Key_BracketLeft, 0));
    QCOMPARE(editor.score().event(nextId)->ticks, quarter);
    QVERIFY(editor.handleKey(Qt::Key_Delete, 0));
    QVERIFY(editor.score().event(nextId)->isRest());
    editor.undo();
    QVERIFY(!editor.score().event(nextId)->isRest());
}

void ScoreTests::playbackSchedulesChordsAndRestarts()
{
    Score score(2);
    const auto first = score.firstEventId(0);
    QVERIFY(score.enterNote(first, Pitch{0, 4, 0}, quarter));
    QVERIFY(score.addChordPitch(first, Pitch{4, 4, 0}));
    const auto second = score.nextEventId(first);
    QVERIFY(score.enterNote(second, Pitch{0, 4, 0}, quarter));
    const auto all = Playback::schedule(score, 0);
    QCOMPARE(all.size(), std::size_t{6});
    QCOMPARE(all[0].microseconds, std::int64_t{0});
    QCOMPARE(all[1].microseconds, std::int64_t{0});
    QCOMPARE(all[2].microseconds, std::int64_t{500000});
    QVERIFY(!all[2].on);
    QCOMPARE(all[4].microseconds, std::int64_t{500000});
    QVERIFY(all[4].on);
    const auto fromSecondMeasure = Playback::schedule(score, measureTicks);
    QVERIFY(fromSecondMeasure.empty());
}

void ScoreTests::playbackRejectsEditsAndCanRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("MUSIC_AUDIO_DRIVER", "file");
    qputenv("MUSIC_AUDIO_FILE", dir.filePath("playback.wav").toUtf8());
    Editor editor;
    for (int i = 0; i < 100 && !editor.audioReady() &&
                        editor.status().startsWith(QStringLiteral("Use ")); ++i) {
        QTest::qWait(20);
    }
    if (!editor.audioReady()) QSKIP(qPrintable(editor.status()));
    editor.play();
    QVERIFY(editor.playing());
    const auto id = editor.score().firstEventId(0);
    QVERIFY(editor.handleKey(Qt::Key_C, 0));
    QVERIFY(editor.score().event(id)->isRest());
    editor.stop();
    QVERIFY(!editor.playing());
    QVERIFY(editor.handleKey(Qt::Key_C, 0));
    QVERIFY(!editor.score().event(id)->isRest());
    editor.play();
    QVERIFY(editor.playing());
    QTest::qWait(100);
    editor.stop();
    QVERIFY(QFileInfo(dir.filePath("playback.wav")).size() > 44);
    qunsetenv("MUSIC_AUDIO_DRIVER");
    qunsetenv("MUSIC_AUDIO_FILE");
}

void ScoreTests::engravingProducesVisibleScore()
{
    Score score(2);
    const auto id = score.firstEventId(0);
    QVERIFY(score.enterNote(id, Pitch{0, 5, 1}, quarter));
    QVERIFY(score.addChordPitch(id, Pitch{4, 5, 0}));
    const auto layout = Engraving::layout(score, 0);
    QCOMPARE(layout.events.size(), std::size_t{3});
    const auto image = Engraving::render(layout);
    QCOMPARE(image.size(), QSize(Engraving::systemWidth, Engraving::systemHeight));
    QVERIFY(image.pixelColor(300, 80) != image.pixelColor(300, 60));
    if (const auto path = qEnvironmentVariable("MUSIC_SCREENSHOT"); !path.isEmpty()) {
        QVERIFY(image.save(path));
    }
}

void ScoreTests::overwriteRhythmAndClamp()
{
    Score score(1);
    const auto c=score.firstEventId(0);
    QVERIFY(score.enterNote(c,{0,4,0},quarter));
    const auto d=score.nextEventId(c);
    QVERIFY(score.enterNote(d,{1,4,0},2*quarter));
    const auto e=score.nextEventId(d);
    QVERIFY(score.enterNote(e,{2,4,0},quarter));
    QVERIFY(score.setDuration(c,2*quarter,true,true));
    QVERIFY(!score.event(d));
    QVERIFY(score.event(score.nextEventId(c))->isRest());
    QCOMPARE(score.event(score.nextEventId(c))->ticks,quarter);
    QVERIFY(score.event(e));
    QVERIFY(score.setDuration(c,quarter,true,true));
    QVERIFY(score.setDuration(c,3*quarter,true,true));
    QCOMPARE(score.event(c)->ticks,3*quarter);
    QVERIFY(score.valid());
    Editor editor; editor.setAuditionEnabled(false);
    QVERIFY(editor.handleKey(Qt::Key_C,0));
    QVERIFY(editor.handleKey(Qt::Key_D,0));
    const auto second=editor.score().nextEventId(editor.score().firstEventId(0));
    editor.selectEvent(second);
    QVERIFY(editor.handleKey(Qt::Key_BracketRight,0));
    QVERIFY(editor.handleKey(Qt::Key_BracketRight,0));
    QCOMPARE(editor.score().event(second)->ticks,Tick{3*quarter});
    editor.undo();
    QCOMPARE(editor.score().event(second)->ticks,2*quarter);
    editor.redo();
    QCOMPARE(editor.score().event(second)->ticks,3*quarter);
}
void ScoreTests::meterRebarAndClefs()
{
    Score score(1);
    const auto c=score.firstEventId(0);
    QVERIFY(score.enterNote(c,{0,4,0},quarter));
    const auto d=score.nextEventId(c);
    QVERIFY(score.enterNote(d,{1,4,0},2*quarter));
    const auto e=score.nextEventId(d);
    QVERIFY(score.enterNote(e,{2,4,0},quarter));
    QVERIFY(score.setClef(0,quarter,Clef::Bass));
    QVERIFY(score.changeMeter(0,Score::defaultMeter(2,4)));
    QCOMPARE(score.measureCount(),std::size_t{2});
    QCOMPARE(score.event(d)->ticks,quarter);
    QVERIFY(score.event(d)->tieTarget(0));
    QCOMPARE(score.clefAt(0,quarter),Clef::Bass);
    QCOMPARE(score.event(d)->pitches[0].midi(),62);
    QVERIFY(score.valid());
    Score later(3);
    const auto passage=later.firstEventId(2);
    QVERIFY(later.changeMeter(2,Score::defaultMeter(3,4)));
    QVERIFY(later.changeMeter(0,Score::defaultMeter(2,4)));
    QCOMPARE(later.locate(passage)->measure,std::size_t{4});
    QCOMPARE(later.bar(4).meter.numerator,3);
    QVERIFY(later.valid());
    QCOMPARE(Engraving::noteY({0,4,0},Clef::Treble8va)-Engraving::noteY({0,4,0},Clef::Treble),qreal{42});
}
void ScoreTests::multiStaffMusicXml()
{
    Score score(2);
    const auto piano=score.addPart("Piano pair",0,true);
    const auto violin=score.addPart("Violin",40);
    QVERIFY(piano && violin);
    QVERIFY(score.setInstrument(score.staves()[1].id,12,"Upper piano"));
    QVERIFY(score.setClef(1,quarter,Clef::Alto));
    const auto first=score.firstEventId(0,2);
    QVERIFY(score.enterNote(first,{0,3,0},4*quarter));
    QVERIFY(score.toggleTie(first,0));
    QVERIFY(score.setTempo(1,TempoMark{3*quarter/2,72}));
    QVERIFY(score.addGroup("Ensemble",{score.parts()[0].id,piano,violin}));
    QTemporaryDir dir;QString error;
    const auto path=dir.filePath("ensemble.musicxml");
    QVERIFY2(MusicXml::save(score,path,&error),qPrintable(error));
    Score opened(1);
    QVERIFY2(MusicXml::open(path,&opened,&error),qPrintable(error));
    QCOMPARE(opened.staffCount(),std::size_t{4});
    QCOMPARE(opened.parts().size(),std::size_t{3});
    QCOMPARE(opened.groups().size(),std::size_t{1});
    QCOMPARE(opened.staves()[1].program,12);
    QCOMPARE(opened.staves()[2].program,-1);
    QCOMPARE(opened.program(3),40);
    QCOMPARE(opened.staves()[1].name,std::string("Upper piano"));
    QCOMPARE(opened.clefAt(1,quarter),Clef::Alto);
    QCOMPARE(opened.clefAt(2,0),Clef::Bass);
    QCOMPARE(opened.tempoAt(1), (TempoMark{3*quarter/2,72}));
    QVERIFY(opened.event(opened.firstEventId(0,2))->tieTarget(0));
    QVERIFY(opened.valid());
}
void ScoreTests::instrumentGroups()
{
    Score score(1);
    const auto a=score.addPart("Violin",40),b=score.addPart("Viola",41);
    const auto id=score.staves()[1].id;
    QVERIFY(score.addGroup("Strings",{a,b}));
    QVERIFY(score.moveStaff(id,-1));
    QCOMPARE(score.parts().front().id,a);
    QCOMPARE(score.groups().front().parts.front(),a);
    QCOMPARE(score.staves().front().id,id);
    QCOMPARE(score.program(0),40);
    QVERIFY(score.valid());
    QVERIFY(score.setPartInstrument(a,42,"Cello"));
    QCOMPARE(score.program(0),42);
    QVERIFY(score.setInstrument(id,0,"Solo"));
    QCOMPARE(score.program(0),0);
    const auto extra=score.addStaff(a,Clef::Bass);
    QVERIFY(extra);
    QVERIFY(score.removeStaff(extra));
    QVERIFY(score.valid());
}
void ScoreTests::tiesAndTempoSchedule()
{
    Score score(3);
    const auto first=score.firstEventId(0);
    QVERIFY(score.enterNote(first,{0,4,0},4*quarter));
    QVERIFY(score.toggleTie(first,0));
    const auto second=score.firstEventId(1);
    QVERIFY(score.toggleTie(second,0));
    QVERIFY(score.setTempo(1,TempoMark{quarter,60}));
    auto notes=Playback::schedule(score,0);
    QCOMPARE(notes.size(),std::size_t{2});
    QCOMPARE(notes.back().microseconds,std::int64_t{10000000});
    notes=Playback::schedule(score,score.measureStart(1));
    QCOMPARE(notes.front().microseconds,std::int64_t{0});
    QCOMPARE(notes.back().microseconds,std::int64_t{8000000});
    const auto line=Playback::timeline(score);
    QCOMPARE(Playback::tickAt(line,Playback::timeAt(line,5*quarter)),Tick{5*quarter});
    QVERIFY(score.toggleTie(first,0));
    QVERIFY(!score.event(second)->isRest());
    score.addPart("Other",40);
    QVERIFY(score.enterNote(score.firstEventId(0,1),{0,4,0},quarter));
    notes=Playback::schedule(score,0);
    QVERIFY(std::any_of(notes.begin(),notes.end(),[](const ScheduledNote& e){return e.channel==1 && e.midi==60;}));
}
void ScoreTests::engravingBeamsAndReflow()
{
    Score score(4);LayoutIndex index;index.reset(score);
    const auto oldCount=index.systemCount();
    EventId id=score.firstEventId(0);
    for(int n=0;n<16;++n){QVERIFY(score.enterNote(id,{n%7,4,0},quarter/4));id=score.nextEventId(id);}
    index.invalidate(score,0);
    QVERIFY(index.systemCount()>oldCount);
    QCOMPARE(index.system(0).count,std::size_t{1});
    const auto image=Engraving::layout(score,index,0);
    QVERIFY(image.beams.size()>=16);
    QVERIFY(Engraving::hit(image,image.events[0].origin));
    QVERIFY(score.setClef(0,quarter,Clef::Bass));
    index.invalidate(score,0);
    const auto changed=Engraving::layout(score,index,0);
    QVERIFY(std::any_of(changed.clefs.begin(),changed.clefs.end(),[](const GlyphMark& c){return c.small && c.tick==quarter;}));
}
void ScoreTests::orchestralFixture()
{
    Score score(400);
    for(int s=1;s<30;++s)QVERIFY(score.addPart("Instrument",40+s%8));
    for(std::size_t s=0;s<30;++s)for(std::size_t m=0;m<400;++m){EventId id=score.firstEventId(m,s);for(int beat=0;beat<4;++beat){QVERIFY(score.enterNote(id,{beat,4,0},quarter));id=score.nextEventId(id);}}
    QVERIFY(score.valid());
    LayoutIndex index;QElapsedTimer cold;cold.start();index.reset(score);
    qInfo()<<"30 staves / 400 measures cold layout index:"<<cold.nsecsElapsed()/1000000.0<<"ms";
    const auto id=score.firstEventId(200,15);
    QBENCHMARK {
        QVERIFY(score.setPitch(id,0,{2,4,0}));
        index.invalidate(score,200);
        const auto visible=Engraving::layout(score,index,index.systemForMeasure(200));
        QVERIFY(!visible.events.empty());
    }
}
void ScoreTests::backgroundOpenSave()
{
    Editor editor;editor.setAuditionEnabled(false);
    QVERIFY(editor.handleKey(Qt::Key_C,0));
    QVERIFY(editor.addPart(40));
    QTemporaryDir dir;const auto path=dir.filePath("editor.musicxml");
    QVERIFY(editor.saveAs(QUrl::fromLocalFile(path)));
    QTRY_VERIFY_WITH_TIMEOUT(!editor.fileBusy(),3000);
    QVERIFY(!editor.dirty());
    editor.newScore();
    QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));
    QTRY_VERIFY_WITH_TIMEOUT(!editor.fileBusy(),3000);
    QCOMPARE(editor.score().staffCount(),std::size_t{2});
    QCOMPARE(editor.score().measure(0).events.front().pitches.size(),std::size_t{1});
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QFontDatabase::addApplicationFont(QStringLiteral(":/assets/Bravura.otf"));
    ScoreTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "ScoreTests.moc"

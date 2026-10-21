#include "Editor.h"
#include "MusicXml.h"
#include "ScoreClipboard.h"
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMimeData>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace music;
namespace {
void quarters(Score& score,std::size_t staff,std::size_t first,std::size_t count,int step)
{
    for(std::size_t m=first;m<first+count;++m) {
        auto id=score.firstEventId(m,staff);
        for(int beat=0;beat<4;++beat) {
            if(!score.enterNote(id,{step,4,0},quarter))qFatal("Fixture entry failed");
            id=score.nextEventId(id);
        }
    }
}
StaffId staff(const Score& score,std::size_t index) { return score.staves()[index].id; }
}

class CopyPasteTests : public QObject {
    Q_OBJECT
private slots:
    void explicitStaffMapping();
    void boundariesAndOverlap();
    void partialTuplets_data();
    void partialTuplets();
    void rejectUnsupportedSplit();
    void clipsHeldNotesOnOtherStaves();
    void tupletsAcrossBarlines();
    void appendAndUndo();
    void destinationMeterAndTies();
    void layoutReflowsTheWholePaste();
    void clipboardValidation();
    void keyboardSelectionAndPaste();
    void orchestralSelection();
};

void CopyPasteTests::explicitStaffMapping()
{
    Score score(24);
    for(int i=1;i<4;++i)QVERIFY(score.addPart("Part",i));
    for(std::size_t s=0;s<4;++s)quarters(score,s,0,24,int(s));
    const auto before=score.staves();
    const auto fragment=score.copyRange({0,22*quarter,{staff(score,2),staff(score,0)}});
    QVERIFY(fragment && fragment->valid());
    QCOMPARE(fragment->rows.size(),std::size_t{2});
    QCOMPARE(fragment->duration,22*quarter);
    const auto change=score.pasteRange(*fragment,{48*quarter,49*quarter,{staff(score,3),staff(score,1)}});
    QVERIFY(change);
    QVERIFY(score.valid());
    QCOMPARE(score.staves()[0],before[0]);
    QCOMPARE(score.staves()[2],before[2]);
    for(int row=0;row<2;++row)for(int beat=0;beat<22;++beat) {
        const auto s=std::size_t(row*2+1);
        const auto id=score.eventAt(s,(48+beat)*quarter);
        QCOMPARE(score.event(id)->pitches.front().step,row*2);
        QCOMPARE(score.eventStart(id),(48+beat)*quarter);
        QCOMPARE(score.event(id)->ticks,quarter);
    }
    QCOMPARE(score.event(score.eventAt(1,70*quarter))->pitches.front().step,1);
    QCOMPARE(score.event(score.eventAt(3,70*quarter))->pitches.front().step,3);
    const auto snapshot=score.staves();
    QVERIFY(!score.pasteRange(*fragment,{0,quarter,{staff(score,0)}}));
    QCOMPARE(score.staves(),snapshot);
    QVERIFY(!score.copyRange({quarter,quarter,{staff(score,0)}}));
    QVERIFY(!score.copyRange({0,quarter,{staff(score,0),staff(score,0)}}));
}

void CopyPasteTests::boundariesAndOverlap()
{
    Score score(3);
    const auto source=score.firstEventId(0),target=score.firstEventId(2);
    QVERIFY(score.enterNote(source,{0,4,0},4*quarter));
    QVERIFY(score.addChordPitch(source,{2,4,0}));
    QVERIFY(score.toggleTie(source,0));
    QVERIFY(score.enterNote(target,{4,4,0},4*quarter));
    const auto fragment=score.copyRange({quarter,3*quarter,{staff(score,0)}});
    QVERIFY(fragment);
    QCOMPARE(fragment->rows[0].size(),std::size_t{1});
    QVERIFY(!fragment->rows[0][0].ties[0]);
    const auto untouched=score.measure(0);
    QVERIFY(score.pasteRange(*fragment,{9*quarter,10*quarter,{staff(score,0)}}));
    QVERIFY(score.valid());
    QCOMPARE(score.measure(0),untouched);
    const auto& events=score.measure(2).events;
    QCOMPARE(events.size(),std::size_t{3});
    QCOMPARE(events[0].ticks,quarter);
    QCOMPARE(events[0].pitches.front().step,4);
    QCOMPARE(events[1].ticks,2*quarter);
    QCOMPARE(events[1].pitches.size(),std::size_t{2});
    QCOMPARE(events[2].ticks,quarter);
    QCOMPARE(events[2].pitches.front().step,4);
    QVERIFY(!events[0].tieTarget(0) && !events[1].tieTarget(0));
    const auto sustained=score.copyRange({0,8*quarter,{staff(score,0)}});
    QVERIFY(sustained && sustained->rows[0][0].ties[0]);
    QVERIFY(score.pasteRange(*sustained,{quarter,2*quarter,{staff(score,0)}}));
    QVERIFY(score.valid());
    const auto copied=score.eventAt(0,quarter);
    QVERIFY(copied!=source);
    QVERIFY(score.event(copied)->tieTarget(0));
    QCOMPARE(score.event(score.eventAt(0,0))->ticks,quarter);
    QVERIFY(!score.event(score.eventAt(0,0))->tieTarget(0));
}

void CopyPasteTests::partialTuplets_data()
{
    QTest::addColumn<int>("count");
    for(int count:{3,5,7})QTest::newRow(qPrintable(QString::number(count)))<<count;
}

void CopyPasteTests::partialTuplets()
{
    QFETCH(int,count);
    Score score(3);
    const auto id=score.firstEventId(0);
    QVERIFY(score.setDuration(id,quarter));
    QVERIFY(score.createTuplet(id,count));
    auto next=id;
    for(int i=0;i<count;++i) {
        QVERIFY(score.enterNote(next,{i%7,4,0},quarter/count));
        next=score.nextEventId(next);
    }
    const auto fragment=score.copyRange({quarter/count,2*quarter/count,{staff(score,0)}});
    QVERIFY(fragment && fragment->valid());
    const auto destination=score.firstEventId(1);
    QVERIFY(score.setDuration(destination,quarter));
    QVERIFY(score.createTuplet(destination,count));
    std::string error;
    QVERIFY2(score.pasteRange(*fragment,{4*quarter,5*quarter,{staff(score,0)}},&error),error.c_str());
    QVERIFY(score.valid());
    const auto pasted=score.firstEventId(1);
    QCOMPARE(score.event(pasted)->ticks,quarter/count);
    QCOMPARE(score.event(pasted)->pitches.front().step,1);
    QCOMPARE(score.tupletFor(pasted)->actual,count);
    QVERIFY(score.tupletFor(pasted)->length>0);
    QTemporaryDir dir;
    const auto path=dir.filePath("partial.musicxml");
    QString xmlError;
    QVERIFY2(MusicXml::save(score,path,&xmlError),qPrintable(xmlError));
    Score opened;
    QVERIFY2(MusicXml::open(path,&opened,&xmlError),qPrintable(xmlError));
    QVERIFY(opened.valid());
    QCOMPARE(opened.event(opened.firstEventId(1))->ticks,quarter/count);
    QCOMPARE(opened.tupletFor(opened.firstEventId(1))->span(),score.tupletFor(pasted)->span());
    const auto snapshot=Engraving::layout(score,0);
    QVERIFY(!snapshot.tuplets.empty());
    QVERIFY(QDir().mkpath(QStringLiteral(MUSIC_COPY_ARTIFACT_DIR)));
    QVERIFY(Engraving::render(snapshot).save(QStringLiteral(MUSIC_COPY_ARTIFACT_DIR)+QString("/partial-%1.png").arg(count)));
    Score blank(1);
    QVERIFY(blank.pasteRange(*fragment,{0,quarter,{staff(blank,0)}}));
    QVERIFY(blank.valid());
    QCOMPARE(blank.measure(0).tuplets.size(),std::size_t{1});
    QCOMPARE(blank.measure(0).tuplets.front().span(),quarter);
    const auto& tail=blank.measure(0).events.back();
    QVERIFY(tail.isRest() && !tail.tuplet);
    QCOMPARE(tail.ticks,3*quarter);
    QVERIFY(Engraving::render(Engraving::layout(blank,0)).save(QStringLiteral(MUSIC_COPY_ARTIFACT_DIR)+QString("/blank-%1.png").arg(count)));
}

void CopyPasteTests::rejectUnsupportedSplit()
{
    Score score(2);
    const auto source=score.firstEventId(0),dest=score.firstEventId(1);
    QVERIFY(score.setDuration(source,quarter));
    QVERIFY(score.createTuplet(source,3));
    QVERIFY(score.setDuration(dest,quarter));
    QVERIFY(score.createTuplet(dest,7));
    const auto fragment=score.copyRange({0,quarter/3,{staff(score,0)}});
    QVERIFY(fragment);
    const auto staves=score.staves();
    const auto bars=score.bars();
    std::string error;
    QVERIFY(!score.pasteRange(*fragment,{4*quarter,5*quarter,{staff(score,0)}},&error));
    QVERIFY(!error.empty());
    QCOMPARE(score.staves(),staves);
    QCOMPARE(score.bars(),bars);
    QVERIFY(score.valid());
}

void CopyPasteTests::appendAndUndo()
{
    Score score(2);
    QVERIFY(score.addPart("Violin",40));
    quarters(score,0,0,2,2);
    const auto original=score.staves();
    const auto bars=score.bars();
    const auto fragment=score.copyRange({0,8*quarter,{staff(score,0)}});
    QVERIFY(fragment);
    auto edit=score.pasteRange(*fragment,{7*quarter,8*quarter,{staff(score,0)}});
    QVERIFY(edit);
    QCOMPARE(edit->appended.size(),std::size_t{2});
    QCOMPARE(score.measureCount(),std::size_t{4});
    QVERIFY(score.valid());
    const auto after=score.staves();
    EditHistory history;
    history.record(std::move(*edit));
    LayoutIndex layout; layout.reset(score);
    EditImpact impact;
    QVERIFY(history.undo(score,&impact));
    QVERIFY(!impact.reset);
    layout.invalidate(score,impact.measures);
    QCOMPARE(score.staves(),original);
    QCOMPARE(score.bars(),bars);
    QVERIFY(score.valid());
    QVERIFY(history.redo(score,&impact));
    layout.invalidate(score,impact.measures);
    QCOMPARE(score.staves(),after);
    QVERIFY(score.valid());
    LayoutIndex fresh; fresh.reset(score);
    QCOMPARE(layout.systemCount(),fresh.systemCount());
    for(std::size_t m=0;m<score.measureCount();++m)QCOMPARE(layout.measureRect(m,0),fresh.measureRect(m,0));
}

void CopyPasteTests::clipsHeldNotesOnOtherStaves()
{
    Score score(2);
    QVERIFY(score.addPart("Violin",40));
    const auto held=score.firstEventId(0),tuplet=score.firstEventId(0,1);
    QVERIFY(score.enterNote(held,{0,4,0},4*quarter));
    QVERIFY(score.setDuration(tuplet,quarter));
    QVERIFY(score.createTuplet(tuplet,3));
    QVERIFY(score.enterNote(tuplet,{1,4,0},quarter/3));
    const std::vector<StaffId> both{staff(score,0),staff(score,1)};
    const auto fragment=score.copyRange({0,quarter/3,both});
    QVERIFY(fragment);
    QVERIFY(score.pasteRange(*fragment,{4*quarter,5*quarter,both}));
    QVERIFY(score.valid());
    const auto copied=score.firstEventId(1);
    QCOMPARE(score.event(copied)->ticks,quarter/3);
    QCOMPARE(score.event(copied)->pitches.front().midi(),60);
    const auto notes=Playback::schedule(score,4*quarter);
    QCOMPARE(notes.size(),std::size_t{4});
    QCOMPARE(notes.back().microseconds,std::int64_t{500'000}/3);
}

void CopyPasteTests::tupletsAcrossBarlines()
{
    Score score(2);
    const auto source=score.firstEventId(0);
    QVERIFY(score.setDuration(source,quarter));
    QVERIFY(score.createTuplet(source,3));
    auto next=source;
    for(int i=0;i<3;++i) {
        QVERIFY(score.enterNote(next,{i,4,0},quarter/3));
        next=score.nextEventId(next);
    }
    QVERIFY(score.changeMeter(1,Score::defaultMeter(3,8)));
    const auto fragment=score.copyRange({0,quarter,{staff(score,0)}});
    QVERIFY(fragment);
    QVERIFY(score.pasteRange(*fragment,{5*quarter,6*quarter,{staff(score,0)}}));
    QVERIFY(score.valid());
    const auto left=score.measure(1).events.back().id;
    const auto right=score.firstEventId(2);
    QCOMPARE(score.event(left)->pitches.front().step,1);
    QCOMPARE(score.event(left)->ticks,quarter/6);
    QCOMPARE(score.event(left)->tieTarget(0),right);
    QCOMPARE(score.event(right)->ticks,quarter/6);
    QVERIFY(score.tupletFor(left)->length>0 && score.tupletFor(right)->length>0);
    const auto notes=Playback::schedule(score,5*quarter);
    QCOMPARE(notes.size(),std::size_t{6});
    QCOMPARE(notes.back().microseconds,std::int64_t{500'000});
    QTemporaryDir dir;
    QString error;
    const auto path=dir.filePath("barline.musicxml");
    QVERIFY2(MusicXml::save(score,path,&error),qPrintable(error));
    Score opened;
    QVERIFY2(MusicXml::open(path,&opened,&error),qPrintable(error));
    QVERIFY(opened.valid());
    const auto reopened=Playback::schedule(opened,5*quarter);
    QCOMPARE(reopened.size(),notes.size());
    for(std::size_t i=0;i<notes.size();++i) {
        QCOMPARE(reopened[i].microseconds,notes[i].microseconds);
        QCOMPARE(reopened[i].on,notes[i].on);
        QCOMPARE(reopened[i].midi,notes[i].midi);
    }
}

void CopyPasteTests::destinationMeterAndTies()
{
    Score score(2);
    const auto source=score.firstEventId(0);
    QVERIFY(score.enterNote(source,{0,4,1},4*quarter));
    QVERIFY(score.changeMeter(1,Score::defaultMeter(3,4)));
    QVERIFY(score.setClef(0,4*quarter,Clef::Bass));
    QVERIFY(score.setTempo(1,TempoMark{quarter,90}));
    const auto bars=score.bars();
    const auto clefs=score.staves()[0].clefs;
    const auto fragment=score.copyRange({0,4*quarter,{staff(score,0)}});
    QVERIFY(fragment);
    QVERIFY(score.pasteRange(*fragment,{4*quarter,5*quarter,{staff(score,0)}}));
    QVERIFY(score.valid());
    QCOMPARE(score.bars(),bars);
    QCOMPARE(score.staves()[0].clefs,clefs);
    const auto first=score.firstEventId(1),second=score.firstEventId(2);
    QCOMPARE(score.event(first)->ticks,3*quarter);
    QCOMPARE(score.event(second)->ticks,quarter);
    QCOMPARE(score.event(first)->tieTarget(0),second);
    QCOMPARE(score.event(first)->pitches.front().midi(),61);
    const auto notes=Playback::schedule(score,4*quarter);
    QCOMPARE(notes.size(),std::size_t{2});
    QVERIFY(notes.front().on);
    QCOMPARE(notes.front().microseconds,std::int64_t{0});
    QCOMPARE(notes.back().microseconds,std::int64_t{4}*60'000'000/90);
}

void CopyPasteTests::clipboardValidation()
{
    Score score;
    QVERIFY(score.enterNote(score.firstEventId(0),{0,4,0},quarter));
    const auto fragment=score.copyRange({0,quarter,{staff(score,0)}});
    QVERIFY(fragment);
    const auto bytes=encodeScoreFragment(*fragment);
    QVERIFY(!bytes.isEmpty());
    const auto decoded=decodeScoreFragment(bytes);
    QVERIFY(decoded && decoded->valid());
    QCOMPARE(decoded->duration,quarter);
    QCOMPARE(decoded->rows[0][0].pitches.front().midi(),60);
    for(int size:{0,1,5,20,int(bytes.size()-1)})QVERIFY(!decodeScoreFragment(bytes.left(size)));
    auto corrupt=bytes; corrupt[5]=99;
    QVERIFY(!decodeScoreFragment(corrupt));
    corrupt=bytes; for(int i=18;i<22;++i)corrupt[i]=char(255);
    QVERIFY(!decodeScoreFragment(corrupt));
    corrupt=bytes; corrupt[58]=2;
    QVERIFY(!decodeScoreFragment(corrupt));
    QVERIFY(!decodeScoreFragment(bytes+"trailing"));
    QVERIFY(!decodeScoreFragment(QByteArray(32*1024*1024+1,'x')));
    auto invalid=*fragment;
    invalid.rows[0][0].ticks=0;
    QVERIFY(encodeScoreFragment(invalid).isEmpty());
    invalid=*fragment; invalid.rows[0][0].ties[0]=true;
    QVERIFY(!invalid.valid());
}

void CopyPasteTests::layoutReflowsTheWholePaste()
{
    Score score(12);
    for(std::size_t m=0;m<3;++m) {
        auto id=score.firstEventId(m);
        for(int note=0;note<32;++note) {
            QVERIFY(score.enterNote(id,{note%7,4,0},quarter/8));
            id=score.nextEventId(id);
        }
    }
    const auto fragment=score.copyRange({0,12*quarter,{staff(score,0)}});
    QVERIFY(fragment);
    LayoutIndex layout; layout.reset(score);
    auto change=score.pasteRange(*fragment,{24*quarter,25*quarter,{staff(score,0)}});
    QVERIFY(change);
    std::vector<std::size_t> changed;
    for(const auto& patch:change->measures)changed.push_back(patch.measure);
    layout.invalidate(score,changed);
    auto check=[&] {
        LayoutIndex fresh; fresh.reset(score);
        QCOMPARE(layout.width(),fresh.width());
        QCOMPARE(layout.systemCount(),fresh.systemCount());
        for(std::size_t m=0;m<score.measureCount();++m) {
            QCOMPARE(layout.systemForMeasure(m),fresh.systemForMeasure(m));
            QCOMPARE(layout.measureRect(m,0),fresh.measureRect(m,0));
        }
    };
    check();
    EditHistory history; history.record(std::move(*change));
    EditImpact impact;
    QVERIFY(history.undo(score,&impact));
    layout.invalidate(score,impact.measures);
    check();
    QVERIFY(history.redo(score,&impact));
    layout.invalidate(score,impact.measures);
    check();
    QVERIFY(score.valid());
}

void CopyPasteTests::keyboardSelectionAndPaste()
{
    Editor editor;
    editor.setAuditionEnabled(false);
    for(int i=1;i<4;++i)QVERIFY(editor.addPart(40+i));
    for(int key:{Qt::Key_C,Qt::Key_D,Qt::Key_E,Qt::Key_F})QVERIFY(editor.handleKey(key,0));
    const auto first=editor.score().firstEventId(0);
    const auto third=editor.score().nextEventId(editor.score().nextEventId(first));
    editor.selectEvent(third);
    QVERIFY(editor.handleKey(Qt::Key_Left,Qt::ShiftModifier));
    QCOMPARE(editor.selectedRange().start,quarter);
    QCOMPARE(editor.selectedRange().end,3*quarter);
    QVERIFY(editor.handleKey(Qt::Key_Left,Qt::ShiftModifier));
    QCOMPARE(editor.selectedRange().start,Tick{0});
    QVERIFY(editor.handleKey(Qt::Key_Right,Qt::ShiftModifier));
    QCOMPARE(editor.selectedRange().start,quarter);
    editor.selectEvent(first);
    QVERIFY(editor.handleKey(Qt::Key_Right,Qt::ShiftModifier));
    QVERIFY(editor.handleKey(Qt::Key_Right,Qt::ShiftModifier));
    editor.activateHit({HitKind::Note,editor.score().firstEventId(0,2),0,2,0},false,Qt::ControlModifier);
    QCOMPARE(editor.selectedRange().staves.size(),std::size_t{2});
    QCOMPARE(editor.selectedRange().end,3*quarter);
    QVERIFY(editor.handleKey(Qt::Key_C,Qt::ControlModifier));
    editor.selectEvent(editor.score().firstEventId(2,1));
    editor.activateHit({HitKind::Note,editor.score().firstEventId(2,3),0,3,2},false,Qt::ControlModifier);
    const auto original=editor.score().staves();
    QSignalSpy resets(&editor,&Editor::scoreReset);
    QVERIFY(editor.handleKey(Qt::Key_V,Qt::ControlModifier));
    QCOMPARE(resets.size(),0);
    QVERIFY(editor.score().valid());
    QCOMPARE(editor.selectedRange().start,8*quarter);
    QCOMPARE(editor.selectedRange().end,11*quarter);
    QCOMPARE(editor.score().event(editor.score().firstEventId(2,1))->pitches.front().step,0);
    QVERIFY(editor.score().event(editor.score().firstEventId(2,3))->isRest());
    const auto pasted=editor.score().staves();
    editor.undo();
    QCOMPARE(editor.score().staves(),original);
    editor.redo();
    QCOMPARE(editor.score().staves(),pasted);
    QVERIFY(editor.handleKey(Qt::Key_Escape,0));
    QVERIFY(!editor.hasRangeSelection());
    editor.selectEvent(editor.score().firstEventId(1,0));
    const auto unchanged=editor.score().staves();
    QVERIFY(!editor.pasteSelection());
    QCOMPARE(editor.score().staves(),unchanged);
    QGuiApplication::clipboard()->setText("ordinary text");
    QVERIFY(!editor.pasteSelection());
    QCOMPARE(editor.score().staves(),unchanged);
    editor.selectMeasure(0,0);
    editor.activateHit({HitKind::Note,editor.score().firstEventId(0,2),0,2,0},false,Qt::ControlModifier);
    QVERIFY(editor.handleKey(Qt::Key_Right,Qt::ShiftModifier));
    QCOMPARE(editor.selectedRange().start,Tick{0});
    QCOMPARE(editor.selectedRange().end,8*quarter);
    QVERIFY(editor.copySelection());
    editor.newScore();
    QVERIFY(editor.addPart(40));
    editor.selectEvent(editor.score().firstEventId(0));
    editor.activateHit({HitKind::Note,editor.score().firstEventId(0,1),0,1,0},false,Qt::ControlModifier);
    QVERIFY(editor.pasteSelection()); // Clipboard survives replacing the open document.
    QVERIFY(editor.score().valid());
}

void CopyPasteTests::orchestralSelection()
{
    Score score(400);
    for(int s=1;s<30;++s)QVERIFY(score.addPart("Instrument",40+s%8));
    for(std::size_t s=0;s<30;++s)quarters(score,s,0,400,int(s%7));
    std::vector<StaffId> source,destination;
    for(std::size_t s=0;s<20;s+=2) { source.push_back(staff(score,s)); destination.push_back(staff(score,s+1)); }
    const auto untouched=score.measure(399,29);
    QElapsedTimer timer; timer.start();
    const auto fragment=score.copyRange({0,22*quarter,source});
    QVERIFY(fragment);
    qInfo()<<"Ten staves / 5.5 measures copy:"<<timer.nsecsElapsed()/1e6<<"ms";
    LayoutIndex layout; layout.reset(score);
    std::vector<qint64> times;
    for(int i=0;i<50;++i) {
        timer.restart();
        auto change=score.pasteRange(*fragment,{48*quarter,49*quarter,destination});
        QVERIFY(change);
        std::vector<std::size_t> changed;
        for(const auto& patch:change->measures)changed.push_back(patch.measure);
        layout.invalidate(score,changed);
        QVERIFY(!Engraving::layout(score,layout,layout.systemForMeasure(12)).events.empty());
        EditHistory history;
        history.record(std::move(*change));
        EditImpact impact;
        QVERIFY(history.undo(score,&impact));
        layout.invalidate(score,impact.measures);
        times.push_back(timer.nsecsElapsed());
    }
    std::sort(times.begin(),times.end());
    qInfo()<<"Ten-staff paste + visible layout + undo p95:"<<times[47]/1e6<<"ms";
    times.clear();
    const auto small=score.copyRange({800*quarter,801*quarter,{staff(score,0)}});
    QVERIFY(small);
    for(int i=0;i<100;++i) {
        timer.restart();
        auto change=score.pasteRange(*small,{860*quarter,861*quarter,{staff(score,15)}});
        QVERIFY(change);
        std::vector<std::size_t> changed;
        for(const auto& patch:change->measures)changed.push_back(patch.measure);
        layout.invalidate(score,changed);
        QVERIFY(!Engraving::layout(score,layout,layout.systemForMeasure(215)).events.empty());
        EditHistory history;
        history.record(std::move(*change));
        EditImpact impact;
        QVERIFY(history.undo(score,&impact));
        layout.invalidate(score,impact.measures);
        times.push_back(timer.nsecsElapsed());
    }
    std::sort(times.begin(),times.end());
    qInfo()<<"Single-note paste + visible layout + undo p95:"<<times[94]/1e6<<"ms";
    QCOMPARE(score.measure(399,29),untouched);
    QVERIFY(score.valid());
}

int main(int argc,char** argv)
{
    QTemporaryDir audio;
    qputenv("MUSIC_AUDIO_DRIVER","file");
    qputenv("MUSIC_AUDIO_FILE",audio.filePath("copy-paste.wav").toUtf8());
    QGuiApplication app(argc,argv);
    QFontDatabase::addApplicationFont(QStringLiteral(":/assets/Bravura.otf"));
    CopyPasteTests test;
    return QTest::qExec(&test,argc,argv);
}
#include "CopyPasteTests.moc"

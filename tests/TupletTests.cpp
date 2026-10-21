#include "Editor.h"
#include "MusicXml.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace music;

class TupletTests : public QObject {
    Q_OBJECT
private slots:
    void creation_data();
    void creation();
    void supportedSpanLimits();
    void mixedDurationsAndBoundaries();
    void tiesAndMeterChanges();
    void keyboardEntryAndUndo();
    void musicXml_data();
    void musicXml();
    void legacyAndMalformedXml();
    void playbackTiming();
    void engraving();
    void orchestralEdits();
};

void TupletTests::creation_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<bool>("dotted");
    QTest::addColumn<int>("kind");
    for(int count:{3,5,7})for(bool dotted:{false,true})for(int kind:{0,1,2})
        QTest::newRow(qPrintable(QString("%1-%2-%3").arg(count).arg(dotted).arg(kind)))<<count<<dotted<<kind;
}

void TupletTests::creation()
{
    QFETCH(int,count); QFETCH(bool,dotted); QFETCH(int,kind);
    Score score(2);
    const auto id=score.firstEventId(0);
    const Tick span=dotted?3*quarter/2:quarter;
    QVERIFY(score.setDuration(id,span));
    if(kind)QVERIFY(score.enterNote(id,{0,4,0},span));
    if(kind==2)QVERIFY(score.addChordPitch(id,{2,4,0}));
    const auto before=score.measure(0);
    const auto other=score.measure(1);
    QVERIFY(score.createTuplet(id,count));
    QVERIFY(score.valid());
    const auto& after=score.measure(0);
    QCOMPARE(after.events.size(),before.events.size()+count-1);
    QCOMPARE(after.events.front().id,id);
    QCOMPARE(after.events.front().pitches,before.events.front().pitches);
    const bool ordinary=dotted && count==3;
    QCOMPARE(after.tuplets.empty(),ordinary);
    Tick total=0;
    for(int i=0;i<count;++i) {
        QCOMPARE(after.events[i].ticks,span/count);
        if(i)QVERIFY(after.events[i].isRest());
        if(!ordinary)QCOMPARE(after.events[i].tuplet,after.tuplets.front().id);
        total+=after.events[i].ticks;
    }
    QCOMPARE(total,span);
    QVERIFY(std::equal(before.events.begin()+1,before.events.end(),after.events.begin()+count));
    QCOMPARE(score.measure(1),other);
    const auto snapshot=score.measure(0);
    if(!ordinary)QVERIFY(!score.createTuplet(id,3));
    QVERIFY(!score.createTuplet(id,4));
    QCOMPARE(score.measure(0),snapshot);
    Score shortScore(1);
    const auto shortId=shortScore.firstEventId(0);
    QVERIFY(shortScore.setDuration(shortId,quarter/4));
    QCOMPARE(shortScore.createTuplet(shortId,count),count==3);
    QVERIFY(shortScore.valid());
}

void TupletTests::mixedDurationsAndBoundaries()
{
    Score score(1);
    const auto first=score.firstEventId(0);
    QVERIFY(score.enterNote(first,{0,4,0},quarter));
    const auto second=score.nextEventId(first);
    QVERIFY(score.setDuration(second,quarter));
    QVERIFY(score.createTuplet(second,3));
    const auto before=score.measure(0);
    QVERIFY(!score.setDuration(first,2*quarter,true));
    QCOMPARE(score.measure(0),before);
    QVERIFY(score.enterNote(second,{1,4,0},quarter/3));
    const auto middle=score.nextEventId(second);
    QVERIFY(score.enterNote(middle,{2,4,0},quarter/3));
    const auto occupied=score.measure(0);
    QVERIFY(!score.setDuration(second,2*quarter/3,false));
    QCOMPARE(score.measure(0),occupied);
    QVERIFY(score.setDuration(second,2*quarter/3,true));
    QCOMPARE(score.event(second)->writtenDuration(),quarter);
    QVERIFY(!score.setDuration(second,4*quarter/3,true,true));
    QVERIFY(score.setDuration(second,quarter/3));
    QVERIFY(score.setDuration(second,quarter/2)); // Dotted triplet eighth.
    QCOMPARE(score.event(second)->writtenDuration(),3*quarter/4);
    QVERIFY(score.valid());
    auto invalid=score.measure(0);
    invalid.events[1].written+=1;
    QVERIFY(!Score::validMeasure(invalid));
    invalid=score.measure(0);
    invalid.tuplets[0].start+=1;
    QVERIFY(!Score::validMeasure(invalid));

    Score small(1);
    const auto id=small.firstEventId(0);
    QVERIFY(small.setDuration(id,quarter/2));
    QVERIFY(small.createTuplet(id,5));
    const auto snapshot=small.measure(0);
    const auto dotted=small.tupletFor(id)->soundingDuration(3*quarter/16);
    QVERIFY(!small.setDuration(id,dotted)); // Would leave an unwritable 64th rest.
    QCOMPARE(small.measure(0),snapshot);
    QVERIFY(small.valid());
}

void TupletTests::supportedSpanLimits()
{
    for(Tick base:{4*quarter,2*quarter,quarter,quarter/2,quarter/4,quarter/8})for(bool dotted:{false,true})for(int count:{3,5,7}) {
        Score score(1);
        QVERIFY(score.changeMeter(0,Score::defaultMeter(8,4)));
        const auto id=score.firstEventId(0);
        const Tick span=dotted?base*3/2:base;
        if(span==3*quarter/16) {
            // A dotted 32nd needs another half-unit to keep the bar writable.
            const auto before=score.measure(0);
            QVERIFY(!score.setDuration(id,span));
            QCOMPARE(score.measure(0),before);
            Measure paired{{Event{id,span,{},{}},Event{100,span,{},{}},Event{101,4*quarter,{},{}},
                            Event{102,2*quarter,{},{}},Event{103,quarter,{},{}},Event{104,quarter/2,{},{}},Event{105,quarter/8,{},{}}}};
            QVERIFY(score.restoreMeasure(0,paired));
        }
        else QVERIFY(score.setDuration(id,span));
        const auto before=score.measure(0);
        const int normal=dotted?(count==7?6:3):(count==3?2:4);
        const Tick unit=span/normal;
        const bool supported=unit>0 && Score::baseDuration(unit)==unit;
        QCOMPARE(score.createTuplet(id,count),supported);
        if(!supported)QCOMPARE(score.measure(0),before);
        QVERIFY(score.valid());
    }
}

void TupletTests::tiesAndMeterChanges()
{
    Score score(2);
    const auto first=score.firstEventId(0);
    QVERIFY(score.enterNote(first,{0,4,0},quarter));
    QVERIFY(score.toggleTie(first,0));
    const auto second=score.nextEventId(first);
    QVERIFY(score.toggleTie(second,0));
    QVERIFY(score.createTuplet(second,7));
    QCOMPARE(score.event(first)->tieTarget(0),second);
    QCOMPARE(score.event(second)->tieTarget(0),EventId{0});
    QVERIFY(score.toggleTie(second,0));
    const auto next=score.nextEventId(second);
    QCOMPARE(score.event(next)->ticks,quarter/7);
    QCOMPARE(score.event(next)->tuplet,score.event(second)->tuplet);
    QVERIFY(score.valid());
    const auto bars=score.bars();
    const auto staves=score.staves();
    QVERIFY(!score.changeMeter(0,Score::defaultMeter(3,8)));
    QCOMPARE(score.bars(),bars);
    QCOMPARE(score.staves(),staves);
    const auto group=*score.tupletFor(second);
    QVERIFY(score.changeMeter(0,Score::defaultMeter(3,4)));
    QCOMPARE(*score.tupletFor(second),group);
    QVERIFY(score.valid());

    Score moved(1);
    const auto start=moved.firstEventId(0);
    QVERIFY(moved.enterNote(start,{0,4,0},3*quarter));
    const auto last=moved.nextEventId(start);
    QVERIFY(moved.createTuplet(last,5));
    const auto groupId=moved.event(last)->tuplet;
    QVERIFY(moved.changeMeter(0,Score::defaultMeter(3,4)));
    QCOMPARE(moved.locate(last)->measure,std::size_t{1});
    QCOMPARE(moved.tupletFor(last)->start,Tick{0});
    QCOMPARE(moved.event(last)->tuplet,groupId);
    QVERIFY(moved.valid());

    Score exit(1);
    const auto startId=exit.firstEventId(0);
    QVERIFY(exit.enterNote(startId,{0,4,0},4*quarter));
    QVERIFY(exit.createTuplet(startId,3));
    auto endId=exit.nextEventId(exit.nextEventId(startId));
    QVERIFY(exit.enterNote(endId,{0,4,0},4*quarter/3));
    QVERIFY(exit.toggleTie(endId,0));
    QCOMPARE(exit.event(exit.nextEventId(endId))->ticks,2*quarter);
    QVERIFY(exit.valid());
}

void TupletTests::keyboardEntryAndUndo()
{
    for(int count:{3,5,7}) {
        Editor editor;
        editor.setAuditionEnabled(false);
        const auto first=editor.score().firstEventId(0);
        QVERIFY(editor.handleKey(Qt::Key_BracketLeft,0));
        QVERIFY(editor.handleKey(Qt::Key_BracketLeft,0));
        const auto before=editor.score().measure(0);
        QSignalSpy reset(&editor,&Editor::scoreReset);
        QSignalSpy layout(&editor,&Editor::layoutInvalidated);
        QVERIFY(editor.handleKey(Qt::Key_0+count,0));
        QCOMPARE(reset.size(),0);
        QCOMPARE(layout.size(),1);
        const auto divided=editor.score().measure(0);
        editor.undo();
        QCOMPARE(editor.score().measure(0),before);
        editor.redo();
        QCOMPARE(editor.score().measure(0),divided);
        for(int i=0;i<count;++i)QVERIFY(editor.handleKey(Qt::Key_C+i%5,0));
        QVERIFY(editor.handleKey(Qt::Key_F,0));
        QCOMPARE(editor.score().event(editor.selectedEventId())->ticks,quarter);
        QVERIFY(!editor.score().event(editor.selectedEventId())->tuplet);
        editor.selectEvent(first);
        QVERIFY(editor.handleKey(Qt::Key_BracketRight,0));
        QCOMPARE(editor.score().event(first)->writtenDuration(),count==3?quarter:quarter/2);
        QVERIFY(editor.handleKey(Qt::Key_Period,0));
        QVERIFY(editor.score().valid());
        QVERIFY(!editor.handleKey(Qt::Key_3,Qt::ControlModifier));
        QVERIFY(!editor.createTuplet(3));
        editor.selectMeasure(0,0);
        QVERIFY(!editor.createTuplet(5));
    }
    Editor chord;
    chord.setAuditionEnabled(false);
    QVERIFY(chord.handleKey(Qt::Key_C,0));
    QVERIFY(chord.handleKey(Qt::Key_E,Qt::ShiftModifier));
    const auto first=chord.score().firstEventId(0);
    QVERIFY(chord.createTuplet(3));
    QVERIFY(chord.handleKey(Qt::Key_D,0));
    QCOMPARE(chord.score().event(first)->pitches.size(),std::size_t{2});
    QCOMPARE(chord.score().event(chord.score().nextEventId(first))->pitches.front().step,1);
}

void TupletTests::musicXml_data()
{
    QTest::addColumn<int>("count"); QTest::addColumn<bool>("dotted");
    for(int count:{3,5,7})for(bool dotted:{false,true})
        QTest::newRow(qPrintable(QString("%1-%2").arg(count).arg(dotted)))<<count<<dotted;
}

void TupletTests::musicXml()
{
    QFETCH(int,count); QFETCH(bool,dotted);
    Score score(2);
    QVERIFY(score.addStaff(score.parts()[0].id,Clef::Bass));
    const auto first=score.firstEventId(0);
    const Tick span=dotted?3*quarter/2:quarter;
    QVERIFY(score.enterNote(first,{0,4,0},span));
    QVERIFY(score.addChordPitch(first,{2,4,0}));
    QVERIFY(score.createTuplet(first,count));
    if(score.tupletFor(first)) {
        QVERIFY(score.setDuration(first,2*span/count));
        QVERIFY(score.toggleTie(first,0));
    }
    const auto rest=score.firstEventId(0,1);
    QVERIFY(score.setDuration(rest,span));
    QVERIFY(score.createTuplet(rest,count));
    const auto adjacent=score.firstEventId(1);
    QVERIFY(score.setDuration(adjacent,quarter));
    QVERIFY(score.createTuplet(adjacent,5));
    auto next=adjacent;
    for(int i=0;i<5;++i)next=score.nextEventId(next);
    QVERIFY(score.setDuration(next,quarter));
    QVERIFY(score.createTuplet(next,3));
    QVERIFY(score.valid());
    QTemporaryDir dir;
    const auto path=dir.filePath("tuplets.musicxml");
    QString error;
    QVERIFY2(MusicXml::save(score,path,&error),qPrintable(error));
    Score opened;
    QVERIFY2(MusicXml::open(path,&opened,&error),qPrintable(error));
    QVERIFY(opened.valid());
    QCOMPARE(opened.staffCount(),score.staffCount());
    for(std::size_t s=0;s<score.staffCount();++s)for(std::size_t m=0;m<score.measureCount();++m) {
        const auto& a=score.measure(m,s); const auto& b=opened.measure(m,s);
        QCOMPARE(a.events.size(),b.events.size());
        QCOMPARE(a.tuplets.size(),b.tuplets.size());
        for(std::size_t i=0;i<a.tuplets.size();++i) {
            QCOMPARE(a.tuplets[i].start,b.tuplets[i].start);
            QCOMPARE(a.tuplets[i].actual,b.tuplets[i].actual);
            QCOMPARE(a.tuplets[i].normal,b.tuplets[i].normal);
            QCOMPARE(a.tuplets[i].unit,b.tuplets[i].unit);
        }
        for(std::size_t i=0;i<a.events.size();++i) {
            QCOMPARE(a.events[i].ticks,b.events[i].ticks);
            QCOMPARE(a.events[i].writtenDuration(),b.events[i].writtenDuration());
            QCOMPARE(a.events[i].pitches,b.events[i].pitches);
            for(std::size_t p=0;p<a.events[i].pitches.size();++p) {
                const auto x=a.events[i].tieTarget(p),y=b.events[i].tieTarget(p);
                QCOMPARE(bool(x),bool(y));
                if(x)QCOMPARE(score.eventStart(x),opened.eventStart(y));
            }
        }
    }
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes=file.readAll();
    if(!dotted || count!=3)QVERIFY(bytes.contains("<normal-type>"));
    const auto added=opened.addPart("Violin",40);
    QVERIFY(added);
    QVERIFY(opened.valid()); // Imported group IDs must not collide with new objects.
}

void TupletTests::legacyAndMalformedXml()
{
    QTemporaryDir dir;
    const auto path=dir.filePath("input.musicxml");
    auto write=[&](const QByteArray& bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
    };
    QVERIFY(write("<score-partwise><part-list><score-part id='P1'><part-name>Piano</part-name></score-part></part-list>"
                  "<part id='P1'><measure><attributes><divisions>960</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
                  "</attributes><note><rest/><duration>3840</duration><type>whole</type></note></measure></part></score-partwise>"));
    Score score;
    QString error;
    QVERIFY2(MusicXml::open(path,&score,&error),qPrintable(error));
    QCOMPARE(score.measure(0).events[0].ticks,4*quarter);
    const auto id=score.firstEventId(0);
    QVERIFY(score.setDuration(id,quarter));
    QVERIFY(score.createTuplet(id,7));
    QVERIFY(MusicXml::save(score,path,&error));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto valid=file.readAll(); file.close();
    const auto original=score.staves();
    QList<QByteArray> invalid;
    auto missingStop=valid; missingStop.replace("type=\"stop\"","type=\"start\""); invalid<<missingStop;
    auto badRatio=valid; badRatio.replace("<actual-notes>7</actual-notes>","<actual-notes>11</actual-notes>"); invalid<<badRatio;
    auto badTiming=valid; badTiming.replace("<normal-notes>4</normal-notes>","<normal-notes>6</normal-notes>"); invalid<<badTiming;
    auto noStart=valid; noStart.replace("<tuplet type=\"start\" number=\"1\"/>",""); invalid<<noStart;
    auto nested=valid; nested.replace("<tuplet type=\"start\" number=\"1\"/>","<tuplet type=\"start\"/><tuplet type=\"start\" number=\"2\"/>"); invalid<<nested;
    for(const auto& bytes:invalid) {
        QVERIFY(bytes!=valid);
        QVERIFY(write(bytes));
        QVERIFY(!MusicXml::open(path,&score,&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(score.staves(),original);
    }
}

void TupletTests::playbackTiming()
{
    for(int count:{3,5,7}) {
        Score score(2);
        for(std::size_t m=0;m<2;++m) {
            auto id=score.firstEventId(m);
            for(int beat=0;beat<4;++beat) {
                QVERIFY(score.enterNote(id,{0,4,0},quarter));
                QVERIFY(score.createTuplet(id,count));
                for(int slot=0;slot<count;++slot) {
                    if(slot)QVERIFY(score.enterNote(id,{slot%7,4,0},quarter/count));
                    id=score.nextEventId(id);
                }
            }
        }
        const auto notes=Playback::schedule(score,0);
        QCOMPARE(notes.size(),std::size_t(16*count));
        QCOMPARE(notes.back().microseconds,std::int64_t{4'000'000});
        std::size_t ons=0;
        for(const auto& note:notes)if(note.on) {
            QCOMPARE(note.microseconds,std::int64_t(ons/count)*500'000+std::int64_t(ons%count)*500'000/count);
            ++ons;
        }
        QVERIFY(score.setTempo(1,TempoMark{quarter,90}));
        const auto changed=Playback::schedule(score,score.measureStart(1));
        QCOMPARE(changed.front().microseconds,std::int64_t{0});
        QCOMPARE(changed.back().microseconds,std::int64_t(4)*60'000'000/90);
    }
}

void TupletTests::engraving()
{
    QVERIFY(QDir().mkpath(QStringLiteral(MUSIC_TUPLET_ARTIFACT_DIR)));
    for(int count:{3,5,7})for(int kind:{0,1,2}) {
        Score score(1);
        const auto id=score.firstEventId(0);
        QVERIFY(score.setDuration(id,quarter));
        QVERIFY(score.createTuplet(id,count));
        if(kind) {
            auto next=id;
            for(int i=0;i<count;++i) {
                QVERIFY(score.enterNote(next,{i%7,4,0},quarter/count));
                next=score.nextEventId(next);
            }
        }
        if(kind==2)QVERIFY(score.setDuration(id,2*quarter/count,true));
        const auto snapshot=Engraving::layout(score,0);
        QCOMPARE(snapshot.tuplets.size(),std::size_t{1});
        const auto& mark=snapshot.tuplets[0];
        QCOMPARE(mark.text,QString::number(count));
        QCOMPARE(mark.bracket,kind==0 || (kind==2 && count==3));
        for(const auto& e:snapshot.events)if(e.event.tuplet) {
            QVERIFY(!mark.numberBox.intersects(e.hitBox));
            if(kind==1)QVERIFY(e.beamed);
            const auto hit=Engraving::hitTest(snapshot,QPointF(e.origin.x(),e.event.isRest()?e.staffTop+24:e.toneY.front()));
            QVERIFY(hit);
            QCOMPARE(hit->first,e.event.id);
        }
        const auto image=Engraving::render(snapshot);
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QStringLiteral(MUSIC_TUPLET_ARTIFACT_DIR)+QString("/%1-%2.png").arg(count).arg(kind)));
    }
}

void TupletTests::orchestralEdits()
{
    Score score(400);
    for(int s=1;s<30;++s)QVERIFY(score.addPart("Instrument",40+s%8));
    for(std::size_t s=0;s<30;++s)for(std::size_t m=0;m<400;++m) {
        auto id=score.firstEventId(m,s);
        for(int beat=0;beat<4;++beat) {
            QVERIFY(score.enterNote(id,{beat,4,0},quarter));
            id=score.nextEventId(id);
        }
    }
    LayoutIndex layout;
    QElapsedTimer timer; timer.start(); layout.reset(score);
    qInfo()<<"Tuplet fixture cold layout:"<<timer.nsecsElapsed()/1e6<<"ms";
    const auto id=score.firstEventId(200,15);
    const auto original=score.measure(200,15);
    const auto untouched=score.measure(399,29);
    std::vector<qint64> times;
    for(int i=0;i<100;++i) {
        timer.restart();
        QVERIFY(score.createTuplet(id,i%3==0?3:i%3==1?5:7));
        QVERIFY(score.setPitch(id,0,{2,4,0}));
        EditHistory history;
        history.record(LocalEdit{{MeasureEdit{15,200,original,score.measure(200,15)}},{}});
        layout.invalidate(score,200);
        const auto visible=Engraving::layout(score,layout,layout.systemForMeasure(200));
        QVERIFY(!visible.tuplets.empty());
        QVERIFY(history.undo(score));
        layout.invalidate(score,200);
        times.push_back(timer.nsecsElapsed());
        QCOMPARE(score.measure(200,15),original);
    }
    std::sort(times.begin(),times.end());
    qInfo()<<"Tuplet create + edit + local layout + undo p95:"<<times[94]/1e6<<"ms";
    QCOMPARE(score.measure(399,29),untouched);
    QVERIFY(score.valid());
}

int main(int argc,char** argv)
{
    QTemporaryDir audio;
    qputenv("MUSIC_AUDIO_DRIVER","file");
    qputenv("MUSIC_AUDIO_FILE",audio.filePath("tuplets.wav").toUtf8());
    QGuiApplication app(argc,argv);
    QFontDatabase::addApplicationFont(QStringLiteral(":/assets/Bravura.otf"));
    TupletTests tests;
    return QTest::qExec(&tests,argc,argv);
}

#include "TupletTests.moc"

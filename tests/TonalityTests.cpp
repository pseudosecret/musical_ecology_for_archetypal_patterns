#include "Editor.h"
#include "MusicXml.h"
#include "TransformWorkspace.h"
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace music;
namespace {
KeyContext major(int f) { return KeyContext::fromSignature(f,ScaleIdentity::Major); }
ScoreFragment melody() { return {2*quarter,{{{quarter,{{0,4,0}},{false}},{quarter,{{4,4,0}},{false}}}}}; }
bool music21Available() {
    QProcess probe;
    probe.start(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-c","import music21"});
    return probe.waitForFinished(30000) && probe.exitCode()==0;
}
}
class TonalityTests : public QObject {
    Q_OBJECT
private slots:
    void keysAndHistory();
    void entryAndCrossKeyPaste();
    void signaturesAndTiedAccidentals();
    void musicXmlRoundTrip();
    void missingAndMalformedXml();
    void selectedContextsAndValidation();
    void realTonalWorker();
    void analysisRequiresConfirmation();
    void keyEditPerformance();
    void notationArtifactAndKeyDeletion();
};
void TonalityTests::keysAndHistory() {
    for(int f=-7;f<=7;++f)for(int form=0;form<4;++form) {
        auto key=KeyContext::fromSignature(f,form?ScaleIdentity::Minor:ScaleIdentity::Major,form?MinorForm(form-1):MinorForm::Melodic);
        QVERIFY(key.valid());
        int altered=0;
        for(int s=0;s<7;++s)altered+=std::abs(key.alteration(s));
        QCOMPARE(altered,std::abs(f));
    }
    Score score(4);
    const auto id=score.firstEventId(0);
    QVERIFY(score.enterNote(id,{3,4,0},quarter));
    QVERIFY(score.setKey(0,2*measureTicks,major(-2)));
    auto change=score.setKey(0,quarter,major(1));
    QVERIFY(change);
    QCOMPARE(change->measures,(std::vector<std::size_t>{0,1,2}));
    QCOMPARE(score.keyAt(0,quarter-1),major(0));
    QCOMPARE(score.keyAt(0,quarter),major(1));
    QCOMPARE(score.event(id)->pitches.front().midi(),65);
    EditHistory history;
    history.record(*change);
    EditImpact impact;
    QVERIFY(history.undo(score,&impact));
    QVERIFY(!impact.reset);
    QCOMPARE(impact.measures,change->measures);
    QCOMPARE(score.keyAt(0,quarter),major(0));
    QVERIFY(history.redo(score,&impact));
    QCOMPARE(score.keyAt(0,quarter),major(1));
    QVERIFY(!score.removeKey(0,0));
    QVERIFY(score.removeKey(0,quarter));
    QVERIFY(!score.setKey(0,score.totalTicks(),major(1)));
    QVERIFY(!score.setKey(0,1,major(8)));
    QVERIFY(score.changeMeter(0,Score::defaultMeter(3,4)));
    QVERIFY(score.valid());
}
void TonalityTests::entryAndCrossKeyPaste() {
    Editor editor;
    editor.setAuditionEnabled(false);
    QSignalSpy resets(&editor,&Editor::scoreReset);
    QSignalSpy invalidations(&editor,&Editor::layoutInvalidated);
    QVERIFY(editor.applyKey(1,0));
    QCOMPARE(resets.size(),0);
    QCOMPARE(invalidations.size(),1);
    QVERIFY(editor.handleKey(Qt::Key_F,0));
    const auto id=editor.score().firstEventId(0);
    QCOMPARE(editor.score().event(id)->pitches.front().alter,1);
    editor.selectEvent(id);
    QVERIFY(editor.handleKey(Qt::Key_Equal,0));
    const auto next=editor.score().nextEventId(id);
    editor.selectEvent(next);
    QVERIFY(editor.handleKey(Qt::Key_F,0));
    QCOMPARE(editor.score().event(next)->pitches.front().alter,0);
    editor.selectEvent(editor.score().firstEventId(1));
    QVERIFY(editor.handleKey(Qt::Key_F,0));
    QCOMPARE(editor.score().event(editor.score().firstEventId(1))->pitches.front().alter,1);
    editor.selectEvent(id);
    QVERIFY(editor.applyKey(0,1));
    QCOMPARE(editor.selectedKey().value("form").toInt(),1);
    QCOMPARE(editor.score().event(id)->pitches.front().midi(),65);
    editor.undo();
    QCOMPARE(editor.score().keyAt(0,0),major(1));
    Score destination(1);
    QVERIFY(destination.setKey(0,0,major(-1)));
    auto fragment=melody();
    fragment.rows[0][0].pitches={{3,4,1}};
    QVERIFY(destination.pasteRange(fragment,{0,0,{destination.staves()[0].id}}));
    QCOMPARE(destination.keyAt(0,0),major(-1));
    QCOMPARE(destination.event(destination.firstEventId(0))->pitches.front().midi(),66);
    const auto snapshot=Engraving::layout(destination,0);
    QCOMPARE(snapshot.events.front().shownAccidentals.front(),1);
}
void TonalityTests::signaturesAndTiedAccidentals() {
    Score flats(1);
    QVERIFY(flats.setKey(0,0,major(-2)));
    const auto flatView=Engraving::layout(flats,0);
    const auto image=Engraving::render(flatView);
    const auto eFlat=flatView.keys.front().glyphs[1].first;
    int stemPixels=0;
    for(int x=int(eFlat.x())-5;x<int(eFlat.x());++x)for(int y=int(eFlat.y())-17;y<int(eFlat.y())-12;++y)
        if(image.pixelColor(x,y).red()<100)++stemPixels;
    QVERIFY2(stemPixels>0,"Flat glyphs must register on their staff pitch rather than their bounding-box center.");
    for(int c=0;c<6;++c) {
        Score score(4);
        QVERIFY(score.setClef(0,0,Clef(c)));
        QVERIFY(score.setKey(0,0,major(1)));
        const auto first=score.firstEventId(0);
        QVERIFY(score.enterNote(first,{3,4,1},quarter));
        const auto next=score.nextEventId(first);
        QVERIFY(score.enterNote(next,{3,4,0},quarter));
        QVERIFY(score.setKey(0,2*quarter,major(-1)));
        auto system=Engraving::layout(score,0);
        QCOMPARE(system.events[0].shownAccidentals.front(),99);
        QCOMPARE(system.events[1].shownAccidentals.front(),0);
        QCOMPARE(system.keys[0].glyphs.size(),std::size_t{1});
        QCOMPARE(system.keys[1].glyphs.size(),std::size_t{2});
        QCOMPARE(system.keys[1].glyphs[0].second,0);
        QCOMPARE(system.keys[1].glyphs[1].second,-1);
        QVERIFY(system.keys[1].hitBox.right()<system.events[2].origin.x());
        const auto repeated=Engraving::layout(score,1);
        QCOMPARE(repeated.keys.front().glyphs.size(),std::size_t{1});
        for(const auto& k:system.keys)for(const auto& [point,alter]:k.glyphs) {
            Q_UNUSED(alter);
            QVERIFY(point.y()>=68 && point.y()<=128);
        }
    }
    Score tied(2);
    auto first=tied.firstEventId(0);
    QVERIFY(tied.enterNote(first,{3,4,0},measureTicks));
    QVERIFY(tied.toggleTie(first,0));
    QVERIFY(tied.setKey(0,measureTicks,major(1)));
    auto continuation=tied.firstEventId(1);
    QVERIFY(tied.setDuration(continuation,quarter));
    const auto attack=tied.nextEventId(continuation);
    QVERIFY(tied.enterNote(attack,{3,4,0},quarter));
    const auto view=Engraving::layout(tied,0);
    const auto held=std::find_if(view.events.begin(),view.events.end(),[&](const EngravedEvent& e){return e.event.id==continuation;});
    QVERIFY(held!=view.events.end());
    QCOMPARE(held->shownAccidentals.front(),99);
    QCOMPARE(std::next(held)->shownAccidentals.front(),0);
    QCOMPARE(tied.entryAlteration(0,measureTicks+quarter,3,4),1);
}
void TonalityTests::musicXmlRoundTrip() {
    Score score(4);
    const auto second=score.addStaff(score.parts().front().id,Clef::Bass);
    QVERIFY(second);
    QVERIFY(score.setKey(0,0,major(-5)));
    QVERIFY(score.setKey(0,quarter/3,KeyContext::fromSignature(2,ScaleIdentity::Minor,MinorForm::Harmonic)));
    QVERIFY(score.setKey(0,2*measureTicks,KeyContext::fromSignature(0,ScaleIdentity::Minor,MinorForm::Aeolian)));
    QVERIFY(score.setKey(1,0,KeyContext::fromSignature(-7,ScaleIdentity::Minor)));
    QTemporaryDir dir;
    QString error;
    const auto file=dir.filePath("keys.musicxml");
    QVERIFY2(MusicXml::save(score,file,&error),qPrintable(error));
    Score reopened;
    QVERIFY2(MusicXml::open(file,&reopened,&error),qPrintable(error));
    QCOMPARE(reopened.staves()[0].keys,score.staves()[0].keys);
    QCOMPARE(reopened.staves()[1].keys,score.staves()[1].keys);
    QVERIFY(reopened.valid());
}
void TonalityTests::missingAndMalformedXml() {
    QTemporaryDir dir;
    const auto file=dir.filePath("unknown.musicxml");
    Score score(1);
    QVERIFY(MusicXml::save(score,file));
    QFile input(file);
    QVERIFY(input.open(QIODevice::ReadOnly));
    const auto original=input.readAll();
    input.close();
    auto replace=[&](QByteArray text) {
        QFile output(file);
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(text);
        output.close();
    };
    auto missing=original;
    missing.replace("<mode>major</mode>","");
    replace(missing);
    Score unknown;
    QVERIFY(MusicXml::open(file,&unknown));
    QVERIFY(unknown.keyAt(0,0).fifths);
    QVERIFY(!unknown.keyAt(0,0).tonality);
    const auto begin=missing.indexOf("<key "),end=missing.indexOf("</key>",begin);
    QVERIFY(begin>=0 && end>=0);
    missing.remove(begin,end+6-begin);
    replace(missing);
    QVERIFY(MusicXml::open(file,&unknown));
    QVERIFY(!unknown.keyAt(0,0).fifths);
    QVERIFY(!unknown.keyAt(0,0).tonality);
    auto invalid=original;
    invalid.replace("<fifths>0</fifths>","<fifths>8</fifths>");
    replace(invalid);
    QVERIFY(!MusicXml::open(file,&unknown));
}
void TonalityTests::selectedContextsAndValidation() {
    Score source(2);
    QVERIFY(source.setKey(0,0,major(-2)));
    QVERIFY(source.setKey(0,quarter,major(0)));
    QVERIFY(source.setKey(0,2*quarter,major(1)));
    QVERIFY(source.setKey(0,3*quarter,major(2)));
    Editor editor;
    editor.installScratch(source,quarter,3*quarter,"Two keys");
    TransformWorkspace workspace(&editor);
    workspace.setWorkerCommand("python3",{QString(MUSIC_TEST_WORKER),"identity"});
    QVERIFY(workspace.begin(&editor));
    QCOMPARE(workspace.keyRegions().size(),2);
    QVERIFY(workspace.submit({QVariantMap{{"kind","transpose"},{"amount",0}}}));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
    QVERIFY2(workspace.currentEditor(),qPrintable(workspace.status()));
    const auto& result=workspace.currentEditor()->score();
    QCOMPARE(result.keyAt(0,0),major(0));
    QCOMPARE(result.keyAt(0,2*quarter),major(1));
    QCOMPARE(result.staves()[0].keys.size(),std::size_t{2});
    QCOMPARE(editor.score().staves()[0].keys,source.staves()[0].keys);
    auto raw=QJsonDocument::fromJson(transformationRequest(melody(),{{TransformKind::Transpose,0}},"key")).object();
    raw["ok"]=true;
    auto keys=raw["keys"].toArray();
    auto key=keys[0].toObject(); key["tick"]="1"; keys[0]=key;
    raw["keys"]=keys;
    QVERIFY(!transformationResponse(QJsonDocument(raw).toJson(),"key",{},nullptr));
    const auto valid=QJsonDocument::fromJson(transformationRequest(melody(),{{TransformKind::Transpose,0}},"key")).object();
    for(int mutation=0;mutation<6;++mutation) {
        auto bad=valid;
        bad["ok"]=true;
        auto regions=bad["keys"].toArray();
        auto region=regions[0].toObject();
        if(mutation==0)region["tick"]=QString::number(2*quarter);
        if(mutation==1)region["fifths"]=9;
        if(mutation==2)region.remove("tonality");
        if(mutation==3) { auto t=region["tonality"].toObject(); t["step"]=1; region["tonality"]=t; }
        if(mutation==4)region["tick"]="+0";
        regions[0]=region;
        if(mutation==5)regions.append(region);
        bad["keys"]=regions;
        QVERIFY(!transformationResponse(QJsonDocument(bad).toJson(),"key",{},nullptr));
    }
}
void TonalityTests::realTonalWorker() {
    if(!music21Available())QSKIP("Run through Poetry for real music21.");
    Score source(1);
    auto id=source.firstEventId(0);
    QVERIFY(source.enterNote(id,{0,4,0},quarter));
    QVERIFY(source.addChordPitch(id,{2,4,0}));
    QVERIFY(source.addChordPitch(id,{4,4,0}));
    Editor editor;
    editor.installScratch(source,0,quarter,"Triad");
    TransformWorkspace workspace(&editor);
    workspace.setWorkerCommand(qEnvironmentVariable("MUSIC_PYTHON","python3"),{QString(MUSIC_REAL_WORKER)});
    QVERIFY(workspace.begin(&editor));
    QVERIFY(workspace.submit({QVariantMap{{"kind","transpose"},{"mode","degrees"},{"amount",1}}}));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),30000);
    QVERIFY2(workspace.currentEditor(),qPrintable(workspace.status()));
    const auto& pitches=workspace.currentEditor()->score().measure(0).events.front().pitches;
    QCOMPARE(pitches,(std::vector<Pitch>{{1,4,0},{3,4,0},{5,4,0}}));
    Score keyed(2);
    id=keyed.firstEventId(0);
    for(const auto p:std::vector<Pitch>{{0,4,0},{0,4,0},{4,4,0}}) {
        QVERIFY(keyed.enterNote(id,p,quarter));
        id=keyed.nextEventId(id);
    }
    QVERIFY(keyed.setKey(0,2*quarter,major(1)));
    editor.installScratch(keyed,quarter,3*quarter,"Two keys, offset selection");
    QVERIFY(workspace.begin(&editor));
    QVERIFY(workspace.submit({QVariantMap{{"kind","scale"},{"numerator",2},{"denominator",1}},QVariantMap{{"kind","retrograde"}}}));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),30000);
    QCOMPARE(workspace.tabs().size(),2);
    const auto* result=workspace.currentEditor();
    QVERIFY(result);
    QCOMPARE(result->selectedRange().start,quarter);
    QCOMPARE(result->selectedRange().end,5*quarter);
    QCOMPARE(result->score().staves()[0].keys[1].tick,3*quarter);
    QCOMPARE(result->score().keyAt(0,3*quarter),major(1));
    QCOMPARE(result->score().event(result->score().eventAt(0,quarter))->pitches.front().midi(),72);
    QCOMPARE(result->score().event(result->score().eventAt(0,3*quarter))->pitches.front().midi(),55);
    QCOMPARE(editor.score().staves()[0].keys,keyed.staves()[0].keys);
}
void TonalityTests::analysisRequiresConfirmation() {
    if(!music21Available())QSKIP("Run through Poetry for real music21.");
    Score source(2);
    QVERIFY(source.setKey(0,0,KeyContext{std::nullopt,std::nullopt}));
    auto id=source.firstEventId(0);
    for(int step:{0,2,4,0,4,2,0,0}) {
        QVERIFY(source.enterNote(id,{step,4,0},quarter));
        id=source.nextEventId(id);
    }
    Editor editor;
    editor.installScratch(source,0,source.totalTicks(),"Unknown");
    TransformWorkspace workspace(&editor);
    workspace.setWorkerCommand(qEnvironmentVariable("MUSIC_PYTHON","python3"),{QString(MUSIC_REAL_WORKER)});
    QVERIFY(workspace.begin(&editor));
    const QVariantList operation{QVariantMap{{"kind","transpose"},{"mode","degrees"},{"amount",1}}};
    QVERIFY(!workspace.submit(operation));
    QVERIFY(workspace.suggestKeys());
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),30000);
    const auto regions=workspace.keyRegions();
    const auto suggestions=regions[0].toMap().value("suggestions").toList();
    QVERIFY2(!suggestions.empty(),qPrintable(workspace.status()));
    QVERIFY(!workspace.keysReady());
    QVERIFY(!workspace.currentEditor());
    const auto suggested=suggestions.front().toMap();
    QVERIFY(workspace.setRegionKey(0,suggested.value("fifths").toInt(),suggested.value("form").toInt()));
    QVERIFY(workspace.keysReady());
    QVERIFY(!editor.score().keyAt(0,0).tonality);
    QVERIFY(workspace.submit(operation));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),30000);
    QVERIFY2(workspace.currentEditor(),qPrintable(workspace.status()));
    QVERIFY(workspace.currentEditor()->score().keyAt(0,0).tonality);
}
void TonalityTests::keyEditPerformance() {
    Score score(400);
    for(int s=1;s<30;++s)QVERIFY(score.addPart("Instrument",40));
    for(std::size_t s=0;s<30;++s)for(std::size_t m=0;m<400;++m) {
        auto id=score.firstEventId(m,s);
        for(int beat=0;beat<4;++beat) { QVERIFY(score.enterNote(id,{beat,4,0},quarter)); id=score.nextEventId(id); }
        if(m%8==0)QVERIFY(score.setKey(s,score.measureStart(m),major((m/8)%5)) || m==0);
    }
    LayoutIndex layout;
    layout.reset(score);
    std::vector<qint64> timings;
    for(int run=0;run<30;++run) {
        QElapsedTimer timer; timer.start();
        auto change=score.setKey(15,score.measureStart(200)+quarter,major(run%2?1:-1));
        QVERIFY(change);
        QCOMPARE(change->measures.size(),std::size_t{9});
        layout.invalidate(score,change->measures);
        const auto view=Engraving::layout(score,layout,layout.systemForMeasure(200));
        QVERIFY(!view.keys.empty());
        timings.push_back(timer.nsecsElapsed());
    }
    std::sort(timings.begin(),timings.end());
    qInfo()<<"30-staff / 400-measure key edit + visible engraving p95:"<<timings[28]/1e6<<"ms";
    QVERIFY(timings[28]<50'000'000);
    QVERIFY(score.valid());
}
void TonalityTests::notationArtifactAndKeyDeletion() {
    Score score(2);
    for(int c=1;c<6;++c)QVERIFY(score.addStaff(score.parts().front().id,Clef(c)));
    for(std::size_t s=0;s<6;++s) {
        QVERIFY(score.setKey(s,0,major(3)));
        QVERIFY(score.setKey(s,2*quarter,major(-2)));
        QVERIFY(score.setKey(s,measureTicks,major(0)));
        auto id=score.firstEventId(0,s);
        const int octave=s==1?2:s==2 || s==3?3:s==4?5:s==5?1:4;
        for(int step:{3,4,5,6}) {
            QVERIFY(score.enterNote(id,{step,octave,0},quarter));
            id=score.nextEventId(id);
        }
    }
    const auto snapshot=Engraving::layout(score,0);
    QDir directory(QStringLiteral(MUSIC_TONAL_ARTIFACT_DIR));
    QVERIFY(directory.mkpath("."));
    QVERIFY(Engraving::render(snapshot).save(directory.filePath("keys-and-cancellations.png")));
    Score editable(2);
    QVERIFY(editable.setKey(0,quarter,major(1)));
    QVERIFY(editable.setClef(0,quarter,Clef::Bass));
    Editor editor;
    editor.installScratch(editable,0,quarter,"Key and clef");
    editor.activateHit({HitKind::Key,0,0,0,0,quarter},false);
    QVERIFY(editor.handleKey(Qt::Key_Delete,0));
    QCOMPARE(editor.score().keyAt(0,quarter),major(0));
    QCOMPARE(editor.score().clefAt(0,quarter),Clef::Bass);
    editor.undo();
    QCOMPARE(editor.score().keyAt(0,quarter),major(1));
}
int main(int argc,char** argv) {
    QTemporaryDir audio;
    qputenv("MUSIC_AUDIO_DRIVER","file");
    qputenv("MUSIC_AUDIO_FILE",audio.filePath("keys.wav").toUtf8());
    QGuiApplication app(argc,argv);
    QFontDatabase::addApplicationFont(":/assets/Bravura.otf");
    TonalityTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "TonalityTests.moc"

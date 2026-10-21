#include "MelodyAnalysis.h"
#include "TransformWorkspace.h"
#include <QClipboard>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace music;
namespace {
Score example() {
    Score score(2);
    ScoreFragment f{8*quarter,{{{quarter,{{0,4,0}},{false}},{quarter,{{1,4,0}},{false}},
        {quarter,{{4,4,0}},{false}},{quarter,{},{}},{quarter,{{4,4,0}},{false}},
        {quarter,{{5,4,0}},{false}},{quarter,{{1,5,0}},{false}},{quarter,{},{}}}}};
    if(!score.pasteRange(f,{0,0,{score.staves().front().id}}))qFatal("Invalid fixture");
    return score;
}
QByteArray runWorker(const QByteArray& request) {
    QProcess process;
    process.start(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-u",MUSIC_REAL_WORKER});
    if(!process.waitForStarted(10000))return {};
    process.write(request); process.closeWriteChannel();
    if(!process.waitForFinished(30000))return {};
    return process.readAllStandardOutput();
}
bool music21Available() {
    QProcess process;
    process.start(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-c","import music21"});
    return process.waitForFinished(30000) && process.exitCode()==0;
}
}
class MelodyAnalysisTests : public QObject {
    Q_OBJECT
private slots:
    void snapshotAndScratchCopy();
    void realWorkerAndValidation();
    void reviewCopyStalenessAndCancellation();
    void gesturesRetainCategoryWhenReviewed();
    void failureRecovery();
    void selectedSnapshotPerformance();
};

void MelodyAnalysisTests::snapshotAndScratchCopy() {
    Score score(4);
    QVERIFY(score.changeMeter(1,Score::defaultMeter(6,8)));
    QVERIFY(score.changeMeter(2,Score::defaultMeter(5,8)));
    QVERIFY(score.setKey(0,score.measureStart(1),KeyContext::fromSignature(-2,ScaleIdentity::Minor,MinorForm::Harmonic)));
    QVERIFY(score.setClef(0,score.measureStart(1)+quarter,Clef::Alto));
    QVERIFY(score.setTempo(1,TempoMark{quarter,84}));
    const auto first=score.firstEventId(0);
    QVERIFY(score.enterNote(first,{0,4,0},quarter));
    QVERIFY(score.createTuplet(first,3));
    auto start=score.eventStart(first),end=score.measureStart(2)+quarter;
    QString error;
    auto snapshot=captureAnalysis(score,{start,end,{score.staves().front().id}},"Meter fixture",&error);
    QVERIFY2(snapshot,qPrintable(error));
    auto copy=analysisScratch(*snapshot,&error);
    QVERIFY2(copy,qPrintable(error));
    QCOMPARE(copy->score.bar(1).meter,score.bar(1).meter);
    QCOMPARE(copy->score.bar(2).meter,score.bar(2).meter);
    QCOMPARE(copy->score.tempoAt(1),score.tempoAt(1));
    QCOMPARE(copy->score.keyAt(0,score.measureStart(1)),score.keyAt(0,score.measureStart(1)));
    QCOMPARE(copy->score.clefAt(0,score.measureStart(1)+quarter),Clef::Alto);
    const auto copied=copy->score.copyRange({copy->start,copy->end,{copy->score.staves().front().id}});
    QVERIFY(copied);
    QCOMPARE(copied->duration,snapshot->fragment.duration);
    QCOMPARE(copied->rows.front().size(),snapshot->fragment.rows.front().size());
    for(std::size_t i=0;i<copied->rows.front().size();++i) {
        QCOMPARE(copied->rows[0][i].ticks,snapshot->fragment.rows[0][i].ticks);
        QCOMPARE(copied->rows[0][i].pitches,snapshot->fragment.rows[0][i].pitches);
        QCOMPARE(copied->rows[0][i].ties,snapshot->fragment.rows[0][i].ties);
        QCOMPARE(copied->rows[0][i].actual,snapshot->fragment.rows[0][i].actual);
    }
    auto partial=captureAnalysis(score,{quarter,score.measureStart(1)+quarter,{score.staves().front().id}},"Partial",&error);
    QVERIFY(partial); QCOMPARE(partial->offset,quarter);
    auto partialCopy=analysisScratch(*partial,&error);
    QVERIFY2(partialCopy,qPrintable(error));
    QCOMPARE(partialCopy->start,quarter);
    QCOMPARE(partialCopy->score.measure(0).events.front().isRest(),true);
    auto data=QJsonDocument::fromJson(melodyAnalysisRequest(*partial,"partial")).object();
    QCOMPARE(data["offset"].toString(),QString::number(quarter));
    QCOMPARE(data["measures"].toArray().size(),2);
}

void MelodyAnalysisTests::realWorkerAndValidation() {
    if(!music21Available())QSKIP("music21 is optional; run through poetry to test the real analyzer.");
    auto score=example(); QString error;
    auto snapshot=captureAnalysis(score,{0,score.totalTicks(),{score.staves().front().id}},"Known motif",&error);
    QVERIFY(snapshot);
    const auto bytes=runWorker(melodyAnalysisRequest(*snapshot,"42"));
    QVERIFY(!bytes.isEmpty());
    auto result=melodyAnalysisResponse(bytes,"42",*snapshot,&error);
    QVERIFY2(result,qPrintable(error));
    QVERIFY(result->report.contains("+7 semitones"));
    QVERIFY(result->report.contains("Motif candidate"));
    QCOMPARE(result->data["notes"].toArray().size(),6);
    QVERIFY(result->data.contains("source"));
    const auto original=QJsonDocument::fromJson(bytes).object();
    int structuralIndex=0;
    while(original["analysis"].toObject()["findings"].toArray()[structuralIndex].toObject()["kind"]!="motif")++structuralIndex;
    for(int failure=0;failure<27;++failure) {
        auto root=original; auto analysis=root["analysis"].toObject();
        auto notes=analysis["notes"].toArray(); auto note=notes[0].toObject();
        auto findings=analysis["findings"].toArray();
        const int findingIndex=failure>=14?structuralIndex:0;
        auto finding=findings[findingIndex].toObject(); auto evidence=finding["evidence"].toObject();
        switch(failure) {
            case 0:root["id"]="wrong";break;
            case 1:analysis["schema"]=0;break;
            case 2:note["sources"]=QJsonArray{999};break;
            case 3:note["start"]="1";break;
            case 4:note["pitch"]=QJsonArray{0,5,0};break;
            case 5:finding["ranges"]=QJsonArray{QJsonObject{{"start","0"},{"end","999999999"}}};break;
            case 6:finding["score"]=1.5;break;
            case 7:analysis["relations"]=QJsonArray{QJsonObject{{"source","missing"},{"target","f0"},{"kind","contains"},{"similarity",1}}};break;
            case 8:analysis["rests"]=QJsonArray{};break;
            case 9: { auto features=analysis["features"].toObject(); features["attackCount"]=999; analysis["features"]=features; break; }
            case 10:finding["evidence"]=QJsonObject{{"low","missing"}};break;
            case 11:note["clippedStart"]=true;break;
            case 12: { auto features=analysis["features"].toObject(); auto profile=features["boundaryProfile"].toArray(); auto boundary=profile[0].toObject(); boundary["tick"]="1"; profile[0]=boundary; features["boundaryProfile"]=profile; analysis["features"]=features; break; }
            case 13: { auto regions=analysis["tonalRegions"].toArray(); auto region=regions[0].toObject(); auto candidates=region["candidates"].toArray(); auto candidate=candidates[0].toObject(); candidate["intervals"]=QJsonArray{0,2,2,5,7}; candidates[0]=candidate; region["candidates"]=candidates; regions[0]=region; analysis["tonalRegions"]=regions; break; }
            case 14: { auto pattern=evidence["pattern"].toObject(); pattern["returns"]=QJsonArray{true,true,true}; evidence["pattern"]=pattern; break; }
            case 15:finding["salience"]=1.5;break;
            case 16: { auto components=evidence["salienceComponents"].toObject(); components["recurrence"]=0; evidence["salienceComponents"]=components; break; }
            case 17: { auto occurrences=evidence["occurrences"].toArray(); auto o=occurrences[0].toObject(); o["first"]="missing"; occurrences[0]=o; evidence["occurrences"]=occurrences; break; }
            case 18:evidence["signatureNoteCount"]=13;break;
            case 19:evidence["matchedDimensions"]=QJsonArray{"imagined"};break;
            case 20: { auto pattern=evidence["pattern"].toObject(); pattern["rhythmUnitTicks"]="1"; evidence["pattern"]=pattern; break; }
            case 21:analysis["schema"]=1;break;
            case 22:analysis["algorithm"]="melody-v1";break;
            case 23:finding["salienceReasons"]="not an array";break;
            case 24: { auto durations=evidence["durationTicks"].toArray(); durations[0]="1"; evidence["durationTicks"]=durations; break; }
            case 25: { auto intervals=evidence["intervalPattern"].toArray(); intervals[0]=99; evidence["intervalPattern"]=intervals; break; }
            case 26: { auto occurrences=evidence["occurrences"].toArray(); auto o=occurrences[0].toObject(); o["first"]=notes.last().toObject()["id"]; occurrences[0]=o; evidence["occurrences"]=occurrences; break; }
        }
        if(failure>=14)finding["evidence"]=evidence;
        notes[0]=note; analysis["notes"]=notes;
        findings[findingIndex]=finding; analysis["findings"]=findings;
        root["analysis"]=analysis;
        QVERIFY2(!melodyAnalysisResponse(QJsonDocument(root).toJson(),"42",*snapshot,&error),qPrintable(QString("Accepted malformed response %1").arg(failure)));
    }
    Score tied(1);
    const auto first=tied.firstEventId(0);
    QVERIFY(tied.enterNote(first,{0,4,0},quarter));
    const auto second=tied.nextEventId(first);
    QVERIFY(tied.enterNote(second,{0,4,0},quarter));
    QVERIFY(tied.toggleTie(first,0));
    auto clipped=captureAnalysis(tied,{quarter,2*quarter,{tied.staves().front().id}},"Clipped tie",&error);
    QVERIFY(clipped); QVERIFY(clipped->sources.front().clippedStart);
    auto clippedResult=melodyAnalysisResponse(runWorker(melodyAnalysisRequest(*clipped,"tie")),"tie",*clipped,&error);
    QVERIFY2(clippedResult,qPrintable(error));
    QCOMPARE(clippedResult->data["features"].toObject()["attackCount"],0);
}

void MelodyAnalysisTests::reviewCopyStalenessAndCancellation() {
    if(!music21Available())QSKIP("music21 is optional; run through poetry to test the real analyzer.");
    Editor source; source.installScratch(example(),0,8*quarter,"Melody");
    TransformWorkspace workspace(&source);
    workspace.setWorkerCommand(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-u",MUSIC_REAL_WORKER});
    QVERIFY(workspace.begin(&source)); QVERIFY(workspace.analyzeSelection());
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),15000);
    QVERIFY2(workspace.hasAnalysis(),qPrintable(workspace.status()));
    auto* scratch=workspace.currentEditor(); QVERIFY(scratch);
    QCOMPARE(scratch->score().totalTicks(),source.score().totalTicks());
    QVERIFY(!workspace.analysisStale());
    QString motif;
    for(const auto& f:workspace.analysisFindings())if(f.toMap()["kind"]=="motif") { motif=f.toMap()["id"].toString(); break; }
    QVERIFY(!motif.isEmpty());
    QVERIFY(workspace.reviewFinding(motif,"accepted","Seed melody"));
    QString cell;
    for(const auto& f:workspace.analysisFindings())if(f.toMap()["kind"]=="cell") { cell=f.toMap()["id"].toString(); break; }
    QVERIFY(!cell.isEmpty());
    QVERIFY(workspace.reviewFinding(cell,"rejected","Supporting cell"));
    QVERIFY(workspace.analysisReport().contains("Seed melody"));
    workspace.copyAnalysis(); QVERIFY(QGuiApplication::clipboard()->text().contains("Seed melody"));
    workspace.copyAnalysis(true);
    QTRY_VERIFY(QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()).isObject());
    auto json=QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()).object();
    QCOMPARE(json["review"].toArray().size(),2); QCOMPARE(json["stale"],false);
    QVERIFY(workspace.highlightFinding(motif,1));
    QVERIFY(scratch->selectedRange().start>0);
    scratch->selectEvent(scratch->score().firstEventId(0));
    QVERIFY(scratch->handleKey(Qt::Key_Up,0));
    QVERIFY(workspace.analysisStale()); QVERIFY(!workspace.highlightFinding(motif));
    QVERIFY(workspace.analysisReport().startsWith("OUT OF DATE"));
    QCOMPARE(source.score().event(source.score().firstEventId(0))->pitches.front().midi(),60);
    scratch->undo(); QVERIFY(workspace.analysisStale()); // Conservative revision invalidation.
    workspace.setWorkerCommand("python3",{"-u",MUSIC_TEST_WORKER,"delay"});
    QVERIFY(workspace.begin(scratch)); QVERIFY(workspace.analyzeSelection());
    workspace.cancel(); QVERIFY(!workspace.busy());
    QCOMPARE(workspace.tabs().size(),1);
    QVERIFY(workspace.analysisReport().contains("Seed melody"));
}

void MelodyAnalysisTests::gesturesRetainCategoryWhenReviewed() {
    if(!music21Available())QSKIP("music21 is optional; run through poetry to test the real analyzer.");
    Score score(2);
    ScoreFragment fragment{8*quarter,{{{quarter,{{5,4,0}},{false}},{quarter,{{6,4,0}},{false}},
        {quarter,{{5,4,0}},{false}},{quarter,{},{}},{quarter,{{2,4,0}},{false}},
        {quarter,{{5,4,0}},{false}},{quarter,{{2,4,0}},{false}},{quarter,{},{}}}}};
    QVERIFY(score.pasteRange(fragment,{0,0,{score.staves().front().id}}));
    Editor source; source.installScratch(std::move(score),0,8*quarter,"Return gestures");
    TransformWorkspace workspace(&source);
    workspace.setWorkerCommand(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-u",MUSIC_REAL_WORKER});
    QVERIFY(workspace.begin(&source)); QVERIFY(workspace.analyzeSelection());
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),15000);
    QVERIFY2(workspace.hasAnalysis(),qPrintable(workspace.status()));
    QString gesture;
    for(const auto& value:workspace.analysisFindings()) {
        const auto f=value.toMap();
        QVERIFY(f["kind"]!="motif");
        if(f["kind"]=="gesture" && f["occurrences"].toInt()==2)gesture=f["id"].toString();
    }
    QVERIFY(!gesture.isEmpty());
    QVERIFY(workspace.reviewFinding(gesture,"accepted","Return idea"));
    for(const auto& value:workspace.analysisFindings())if(value.toMap()["id"]==gesture) {
        QCOMPARE(value.toMap()["kind"].toString(),QString("gesture"));
        QCOMPARE(value.toMap()["decision"].toString(),QString("accepted"));
        QCOMPARE(value.toMap()["name"].toString(),QString("Return idea"));
    }
    QVERIFY(workspace.highlightFinding(gesture,1));
    QCOMPARE(workspace.currentEditor()->selectedRange().start,4*quarter);
    QCOMPARE(workspace.currentEditor()->selectedRange().end,7*quarter);
}

void MelodyAnalysisTests::failureRecovery() {
    Editor source; source.installScratch(example(),0,8*quarter,"Melody");
    TransformWorkspace workspace(&source);
    for(const auto& mode:{"malformed","wrong_id","crash"}) {
        workspace.setWorkerCommand("python3",{"-u",MUSIC_TEST_WORKER,mode});
        QVERIFY(workspace.begin(&source)); QVERIFY(workspace.analyzeSelection());
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
        QVERIFY(workspace.tabs().isEmpty()); QVERIFY(!workspace.status().isEmpty());
    }
    workspace.setTimeout(30);
    workspace.setWorkerCommand("python3",{"-u",MUSIC_TEST_WORKER,"delay"});
    QVERIFY(workspace.begin(&source)); QVERIFY(workspace.analyzeSelection());
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),3000);
    QVERIFY(workspace.status().contains("timed out"));
    source.selectEvent(source.score().firstEventId(0));
    QVERIFY(source.handleKey(Qt::Key_E,Qt::ShiftModifier));
    QVERIFY(workspace.begin(&source)); QVERIFY(!workspace.analyzeSelection());
    QVERIFY(workspace.status().contains("Stacked notes occur in measure 1"));
}

void MelodyAnalysisTests::selectedSnapshotPerformance() {
    Score score(400);
    for(int i=1;i<30;++i)score.addPart("Instrument",0);
    const auto staff=score.staves().back().id;
    QElapsedTimer timer; timer.start();
    for(int i=0;i<30;++i) {
        auto snapshot=captureAnalysis(score,{score.measureStart(180),score.measureStart(184),{staff}},"Selected passage",nullptr);
        QVERIFY(snapshot); QCOMPARE(snapshot->bars.size(),std::size_t(4));
        QCOMPARE(snapshot->fragment.rows.size(),std::size_t(1));
    }
    qInfo("Selected analysis capture: %.3f ms average, 30 staves / 400 measures",timer.nsecsElapsed()/30e6);
    QVERIFY(timer.elapsed()<1500);
}

int main(int argc,char** argv) {
    QTemporaryDir audio;
    qputenv("MUSIC_AUDIO_DRIVER","file");
    qputenv("MUSIC_AUDIO_FILE",audio.filePath("analysis.wav").toUtf8());
    QGuiApplication app(argc,argv);
    QFontDatabase::addApplicationFont(":/assets/Bravura.otf");
    MelodyAnalysisTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "MelodyAnalysisTests.moc"

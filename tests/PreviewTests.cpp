#include "TransformWorkspace.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace music;
namespace {
QVariantMap transpose(int amount) { return {{"kind","transpose"},{"amount",amount},{"mode","semitones"}}; }
QVariantList chain(int amount) { return {transpose(amount)}; }
void source(Editor& editor,int bars=2) {
    Score score(bars);
    for(int bar=0;bar<bars;++bar) {
        QVERIFY(score.enterNote(score.eventAt(0,bar*4*quarter),{0,4,0},quarter));
        QVERIFY(score.enterNote(score.eventAt(0,(bar*4+1)*quarter),{1,4,0},quarter));
        QVERIFY(score.enterNote(score.eventAt(0,(bar*4+2)*quarter),{0,4,0},quarter));
    }
    editor.setAuditionEnabled(false);
    editor.installScratch(std::move(score),0,bars*4*quarter,"Source");
}
int pitch(Editor* editor,Tick tick=0) { return editor->score().event(editor->score().eventAt(0,tick))->pitches.front().midi(); }
void fixture(TransformWorkspace& workspace,const QString& mode="preview",const QString& log={}) {
    QStringList args{MUSIC_TEST_WORKER,mode}; if(!log.isEmpty())args<<log;
    workspace.setWorkerCommand("python3",args);
}
QStringList requests(const QString& path) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly))return {};
    return QString::fromUtf8(file.readAll()).split('\n',Qt::SkipEmptyParts);
}
}
class PreviewTests:public QObject {
    Q_OBJECT
private slots:
    void baselineChainResetKeepCancel();
    void coalescingSupersessionAndErrors();
    void reportRevisionAndPriority();
    void keptReportCannotFollowEdits();
    void reportFailureKeepsMusic();
    void sourceReviewsAndSelectedOccurrence();
    void workerFailureAndEarlyCancellation();
    void auditionOwnershipStopAndHide();
    void selectedRegionCost();
};
void PreviewTests::baselineChainResetKeepCancel() {
    Editor main; source(main);
    TransformWorkspace workspace(&main); fixture(workspace);
    workspace.setPreviewAudition(false);
    const auto revision=main.revision(); const auto original=main.score().measure(0);
    QSignalSpy resets(&main,&Editor::scoreReset),layout(&main,&Editor::layoutInvalidated),ready(&workspace,&TransformWorkspace::resultReady);
    QVERIFY(workspace.begin(&main));
    QVERIFY(workspace.beginPreview(chain(2)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    auto* preview=workspace.currentEditor(); QVERIFY(preview); QVERIFY(preview->editLocked());
    QCOMPARE(pitch(preview),62);
    const auto previewRevision=preview->revision();
    preview->selectEvent(preview->score().firstEventId(0));
    QVERIFY(!preview->handleKey(Qt::Key_Up,Qt::ShiftModifier));
    QCOMPARE(preview->revision(),previewRevision);
    QVERIFY(preview->handleKey(Qt::Key_Right,Qt::ShiftModifier));
    QVERIFY(preview->copySelection());
    QCOMPARE(preview->revision(),previewRevision);
    QTemporaryDir saved;
    QVERIFY(!preview->saveAs(QUrl::fromLocalFile(saved.filePath("temporary.musicxml"))));
    QVERIFY(!workspace.begin(&main));
    // Editing the source after capture must not change the immutable baseline.
    main.selectEvent(main.score().firstEventId(0)); main.handleKey(Qt::Key_Up,Qt::ShiftModifier);
    QVERIFY(workspace.updatePreview(chain(3),true));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QCOMPARE(workspace.currentEditor(),preview); QCOMPARE(pitch(preview),63);
    const auto beforeKey=preview->revision();
    QVERIFY(workspace.setRegionKey(0,2,0));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QCOMPARE(preview->revision(),beforeKey+1);
    QCOMPARE(preview->score().keyAt(0,0).fifths,std::optional<int>{2});
    QCOMPARE(main.score().keyAt(0,0).fifths,std::optional<int>{0});
    QVariantList ordered{transpose(2),QVariantMap{{"kind","invert"},{"amount",60}},QVariantMap{{"kind","scale"},{"numerator",2},{"denominator",1}},QVariantMap{{"kind","retrograde"}}};
    QVERIFY(workspace.updatePreview(ordered,true));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QCOMPARE(preview->selectedRange().end,16*quarter);
    QCOMPARE(pitch(preview,2*quarter),58); // reverse and scale the baseline's final C, then invert
    workspace.resetPreview();
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),5000);
    QCOMPARE(pitch(preview),60); QCOMPARE(preview->selectedRange().end,8*quarter);
    QCOMPARE(workspace.previewSteps().front().toMap()["amount"].toInt(),0);
    QCOMPARE(workspace.tabs().size(),1); QCOMPARE(ready.size(),1);
    QVERIFY(workspace.keepPreview()); QVERIFY(!preview->editLocked());
    preview->selectEvent(preview->score().firstEventId(0)); preview->handleKey(Qt::Key_Up,Qt::ShiftModifier);
    QVERIFY(preview->canUndo()); QCOMPARE(pitch(preview),61);
    QCOMPARE(main.revision(),revision+1); QCOMPARE(resets.size(),0); QCOMPARE(layout.size(),1);
    QVERIFY(main.score().measure(0)!=original);
    QVERIFY(workspace.begin(preview)); QVERIFY(workspace.beginPreview(chain(4)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QCOMPARE(workspace.tabs().size(),2);
    QPointer<Editor> temporary=workspace.currentEditor();
    workspace.cancelPreview(); QVERIFY(temporary.isNull());
    QCOMPARE(workspace.tabs().size(),1); QCOMPARE(workspace.currentEditor(),preview); QCOMPARE(pitch(preview),61);
}
void PreviewTests::coalescingSupersessionAndErrors() {
    Editor main; source(main);
    TransformWorkspace workspace(&main); QTemporaryDir dir;
    const auto log=dir.filePath("requests"); fixture(workspace,"preview",log);
    workspace.setPreviewAudition(false); QVERIFY(workspace.begin(&main));
    QVERIFY(workspace.beginPreview(chain(0)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    auto* preview=workspace.currentEditor(); const auto revision=preview->revision();
    const auto count=requests(log).count("transform");
    for(int i=1;i<=8;++i)QVERIFY(workspace.updatePreview(chain(i)));
    QVERIFY(!workspace.previewCanKeep());
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QCOMPARE(pitch(preview),68); QCOMPARE(preview->revision(),revision+1);
    QCOMPARE(requests(log).count("transform"),count+1);
    QVERIFY(workspace.updatePreview(chain(8),true)); // release at the same setting must not resubmit
    QVERIFY(!workspace.busy());
    QCOMPARE(preview->revision(),revision+1);
    QVERIFY(workspace.updatePreview(chain(7),true));
    QTest::qWait(60);
    QVERIFY(workspace.updatePreview(chain(5),true));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000); QCOMPARE(pitch(preview),65);
    QVERIFY(workspace.updatePreview(chain(13),true));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),10000);
    QVERIFY(!workspace.previewCanKeep()); QCOMPARE(pitch(preview),65); QVERIFY(!workspace.keepPreview());
    QVERIFY(!workspace.updatePreview({QVariantMap{{"kind","scale"},{"numerator",0}}},true));
    QVERIFY(!workspace.previewCanKeep()); QCOMPARE(pitch(preview),65);
    workspace.resetPreview(); QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),5000); QCOMPARE(pitch(preview),60);
    QVERIFY(workspace.updatePreview(chain(9),true));
    QVERIFY(workspace.closeResult(0)); QTest::qWait(450);
    QVERIFY(!workspace.busy()); QVERIFY(!workspace.previewActive()); QVERIFY(workspace.tabs().isEmpty());
    QCOMPARE(pitch(&main),60);
}
void PreviewTests::reportRevisionAndPriority() {
    Editor main; source(main);
    TransformWorkspace workspace(&main); fixture(workspace,"preview_slow_report");
    workspace.setPreviewAudition(false); QVERIFY(workspace.begin(&main)); QVERIFY(workspace.beginPreview(chain(0)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.hasAnalysis(),10000);
    auto* preview=workspace.currentEditor();
    const auto report=workspace.analysisReport(); const auto revision=preview->revision();
    QVERIFY(!workspace.analysisStale()); QVERIFY(!workspace.analysisFindings().isEmpty());
    const auto finding=workspace.analysisFindings().front().toMap()["id"].toString();
    QVERIFY(workspace.updatePreview(chain(2),true));
    QVERIFY(workspace.analysisStale()); QVERIFY(!workspace.highlightFinding(finding));
    QVERIFY(!workspace.reviewFinding(finding,"accepted","old"));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.busy(),5000); // delayed analysis, not transformation
    QElapsedTimer timer; timer.start();
    QVERIFY(workspace.updatePreview(chain(4),true));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QVERIFY(timer.elapsed()<2000); // report cannot hold up the newer transform
    QCOMPARE(pitch(preview),64);
    QVERIFY(workspace.keepPreview()); // in-flight/queued analysis may attach to this exact revision
    const auto keptRevision=preview->revision();
    QTRY_VERIFY_WITH_TIMEOUT(workspace.hasAnalysis() && !workspace.analysisStale(),10000);
    QCOMPARE(preview->revision(),keptRevision); QCOMPARE(keptRevision,revision+2);
    QVERIFY(workspace.analysisReport()!=report);
    QCOMPARE(workspace.tabs().size(),1);
    for(const auto& f:workspace.analysisFindings())QCOMPARE(f.toMap()["decision"].toString(),QString("undecided"));
}
void PreviewTests::keptReportCannotFollowEdits() {
    Editor main; source(main);
    TransformWorkspace workspace(&main); fixture(workspace,"preview_slow_report");
    workspace.setPreviewAudition(false); QVERIFY(workspace.begin(&main)); QVERIFY(workspace.beginPreview(chain(0)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    auto* preview=workspace.currentEditor();
    QTRY_VERIFY_WITH_TIMEOUT(workspace.busy(),5000);
    QVERIFY(workspace.keepPreview());
    preview->selectEvent(preview->score().firstEventId(0)); preview->handleKey(Qt::Key_Up,Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),10000);
    QVERIFY(!workspace.hasAnalysis()); QVERIFY(workspace.reportStatus().contains("changed")); QCOMPARE(pitch(preview),61);
}
void PreviewTests::reportFailureKeepsMusic() {
    Editor main; source(main);
    TransformWorkspace workspace(&main); fixture(workspace,"preview_report_error");
    workspace.setPreviewAudition(false); QVERIFY(workspace.begin(&main)); QVERIFY(workspace.beginPreview(chain(2)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.reportStatus().contains("Fixture report failure"),10000);
    QVERIFY(workspace.previewCanKeep()); QCOMPARE(pitch(workspace.currentEditor()),62); QVERIFY(workspace.keepPreview());
}
void PreviewTests::sourceReviewsAndSelectedOccurrence() {
    Editor main; source(main);
    TransformWorkspace workspace(&main); workspace.setPreviewAudition(false);
    workspace.setWorkerCommand("python3",{MUSIC_REAL_WORKER});
    QVERIFY(workspace.begin(&main)); QVERIFY(workspace.analyzeSelection());
    QTRY_VERIFY_WITH_TIMEOUT(workspace.hasAnalysis(),10000);
    auto* analyzed=workspace.currentEditor(); QString id;
    for(const auto& value:workspace.analysisFindings())if(value.toMap()["reviewable"].toBool() && value.toMap()["kind"]!="boundary") { id=value.toMap()["id"].toString(); break; }
    QVERIFY(!id.isEmpty()); QVERIFY(workspace.reviewFinding(id,"accepted","Source structure"));
    const auto report=workspace.analysisReport();
    QVERIFY(workspace.highlightFinding(id)); const auto selected=analyzed->selectedRange();
    QVERIFY(selected.end-selected.start<8*quarter);
    const auto revision=analyzed->revision();
    QVERIFY(workspace.begin(analyzed)); QVERIFY(workspace.beginPreview(chain(1)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    auto* preview=workspace.currentEditor();
    QCOMPARE(preview->selectedRange().end-preview->selectedRange().start,selected.end-selected.start);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.hasAnalysis() && !workspace.analysisStale(),10000);
    QVERIFY(!workspace.analysisReport().contains("Source structure"));
    workspace.setCurrentIndex(0); QCOMPARE(workspace.analysisReport(),report); QCOMPARE(analyzed->revision(),revision);
    QVERIFY(!workspace.analysisStale());
    workspace.setCurrentIndex(1); workspace.cancelPreview();
    QCOMPARE(workspace.currentEditor(),analyzed); QCOMPARE(workspace.analysisReport(),report);
}
void PreviewTests::workerFailureAndEarlyCancellation() {
    Editor main; source(main); TransformWorkspace workspace(&main);
    workspace.setPreviewAudition(false); workspace.setWorkerCommand("/missing/preview/python",{});
    QVERIFY(workspace.begin(&main)); QVERIFY(workspace.beginPreview(chain(0)));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000);
    QVERIFY(!workspace.previewCanKeep()); QVERIFY(workspace.tabs().isEmpty()); workspace.cancelPreview();
    fixture(workspace); workspace.setTimeout(30);
    QVERIFY(workspace.begin(&main)); QVERIFY(workspace.beginPreview(chain(0)));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(),5000); QVERIFY(workspace.status().contains("timed out"));
    workspace.cancelPreview(); workspace.setTimeout(60000);
    QVERIFY(workspace.begin(&main)); QVERIFY(workspace.beginPreview(chain(2))); workspace.cancelPreview();
    QTest::qWait(450); QVERIFY(workspace.tabs().isEmpty()); QVERIFY(!workspace.busy());
}
void PreviewTests::auditionOwnershipStopAndHide() {
    Editor main; source(main,8);
    TransformWorkspace workspace(&main); fixture(workspace);
    QTRY_VERIFY_WITH_TIMEOUT(main.audioReady(),10000);
    QVERIFY(workspace.begin(&main)); workspace.setPreviewVisible(true); QVERIFY(workspace.beginPreview(chain(0)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    auto* preview=workspace.currentEditor(); QTRY_VERIFY(preview->playing());
    workspace.setPreviewAudition(false);
    preview->play(true); QVERIFY(preview->playing());
    workspace.setPreviewAudition(true); // a pending restart must not reenter an internal Stop
    QVERIFY(workspace.updatePreview(chain(1),true)); QVERIFY(!preview->playing());
    preview->stop(); // manual Stop cancels the pending restart for this generation
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000); QVERIFY(!preview->playing());
    QVERIFY(workspace.updatePreview(chain(2),true));
    QTRY_VERIFY_WITH_TIMEOUT(preview->playing(),10000); QCOMPARE(pitch(preview),62);
    const auto revision=preview->revision();
    QTRY_VERIFY_WITH_TIMEOUT(workspace.hasAnalysis() && !workspace.analysisStale(),10000);
    QVERIFY(preview->playing()); QCOMPARE(preview->revision(),revision); // report never reinstalls the score
    workspace.setPreviewAudition(false);
    preview->play(true); workspace.setPreviewAudition(true);
    QSignalSpy transport(main.audioEngine().get(),&Playback::stateChanged);
    main.play(); QVERIFY(main.playing());
    QCOMPARE(transport.size(),2); // Stop old / start main, with no reentrant preview restart
    QCoreApplication::processEvents(); QVERIFY(main.playing()); QCOMPARE(transport.size(),2);
    main.stop();
    workspace.setPreviewVisible(false); QVERIFY(!preview->playing());
    QVERIFY(workspace.updatePreview(chain(3),true));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000); QVERIFY(!preview->playing());
    workspace.setPreviewVisible(true); QTest::qWait(100); QVERIFY(!preview->playing());
    QVERIFY(workspace.updatePreview(chain(4),true));
    main.play(); QVERIFY(main.playing());
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    QVERIFY(main.playing()); QVERIFY(!workspace.previewControlsEnabled());
    QVERIFY(!workspace.updatePreview(chain(5),true));
    preview->stop(); QVERIFY(main.playing());
    preview->play(); QVERIFY(main.playing()); // manual preview play cannot steal another owner's transport
    workspace.cancelPreview(); QVERIFY(main.playing()); main.stop();
    QVERIFY(!workspace.previewActive()); QCOMPARE(pitch(&main),60);
    QPointer<Editor> disposed;
    {
        TransformWorkspace closing(&main); fixture(closing);
        closing.setPreviewVisible(true);
        QVERIFY(closing.begin(&main)); QVERIFY(closing.beginPreview(chain(1)));
        QTRY_VERIFY_WITH_TIMEOUT(closing.previewCanKeep(),10000);
        disposed=closing.currentEditor(); QTRY_VERIFY(disposed->playing());
    }
    QVERIFY(disposed.isNull()); QVERIFY(!main.playbackBusy());
}
void PreviewTests::selectedRegionCost() {
    Score orchestral(400);
    for(int staff=1;staff<30;++staff)QVERIFY(orchestral.addPart("Part",staff));
    for(int beat=320;beat<1120;++beat)
        QVERIFY(orchestral.enterNote(orchestral.eventAt(15,beat*quarter),{beat%7,4,0},quarter));
    const auto staff=orchestral.staves()[15].id;
    Editor preview; preview.setAuditionEnabled(false);
    for(const auto length:{Tick(22*quarter),Tick(800*quarter)}) {
        std::vector<qint64> times,baseline;
        for(int i=0;i<30;++i) {
            QElapsedTimer timer; timer.start();
            const auto fragment=orchestral.copyRange({320*quarter,320*quarter+length,{staff}}); QVERIFY(fragment);
            auto result=buildTransformationResult(*fragment,{},nullptr); QVERIFY(result);
            const auto prepared=timer.nsecsElapsed();
            timer.restart();
            LayoutIndex oldLayout; oldLayout.reset(result->score);
            baseline.push_back(prepared+timer.nsecsElapsed());
            timer.restart();
            const auto snapshot=captureAnalysis(result->score,{result->start,result->end,{result->score.staves().front().id}},"Preview",nullptr);
            QVERIFY(snapshot);
            preview.installScratch(std::move(result->score),result->start,result->end,"Preview");
            times.push_back(prepared+timer.nsecsElapsed());
        }
        const auto cold=times.front();
        std::sort(times.begin(),times.end()); std::sort(baseline.begin(),baseline.end());
        qInfo()<<"Preview selected quarters"<<length/quarter<<"cold ms"<<cold/1e6<<"baseline validation/layout p95 ms"<<baseline[28]/1e6<<"with snapshot/editor p95 ms"<<times[28]/1e6;
    }
    // End-to-end worker updates on the larger selected fragment, without reports
    // competing between adjustments. The source editor must receive no resets.
    TransformWorkspace workspace(&preview); workspace.setPreviewAudition(false);
    workspace.setWorkerCommand("python3",{MUSIC_REAL_WORKER});
    QVERIFY(workspace.begin(&preview));
    const auto sourceRevision=preview.revision();
    QSignalSpy resets(&preview,&Editor::scoreReset),invalidations(&preview,&Editor::layoutInvalidated);
    QElapsedTimer timer; timer.start(); QVERIFY(workspace.beginPreview(chain(0)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000);
    const auto cold=timer.elapsed(); auto* target=workspace.currentEditor();
    std::vector<qint64> warm;
    for(int i=1;i<=8;++i) {
        timer.restart(); QVERIFY(workspace.updatePreview(chain(i),true));
        QTRY_VERIFY_WITH_TIMEOUT(workspace.previewCanKeep(),10000); warm.push_back(timer.elapsed());
        QCOMPARE(workspace.currentEditor(),target); QCOMPARE(workspace.tabs().size(),1);
    }
    std::sort(warm.begin(),warm.end());
    qInfo()<<"800 quarter-note actual worker preview cold ms"<<cold<<"warm median / max ms"<<warm[4]<<warm.back();
    QCOMPARE(preview.revision(),sourceRevision); QCOMPARE(resets.size(),0); QCOMPARE(invalidations.size(),0);
    workspace.cancelPreview();

}
int main(int argc,char** argv) {
    QTemporaryDir audio; qputenv("MUSIC_AUDIO_DRIVER","file"); qputenv("MUSIC_AUDIO_FILE",audio.filePath("preview.wav").toUtf8());
    QGuiApplication app(argc,argv); QFontDatabase::addApplicationFont(":/assets/Bravura.otf");
    PreviewTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "PreviewTests.moc"

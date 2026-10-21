#include "Editor.h"
#include "ScoreView.h"
#include "TransformWorkspace.h"

#include <QGuiApplication>
#include <QFontDatabase>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QProcess>
#include <QTemporaryDir>
#include <QClipboard>
#include <QtTest>
#include <algorithm>

namespace {
QQuickItem* findItem(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* item = findItem(child, name)) return item;
    return nullptr;
}
}

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void opensRendersAndAcceptsKeyboard();
    void tempoApplyRestoresNoteEntryAndStaffInstrument();
    void spaceTogglesFromSelectionAndRespectsDialogs();
    void removesClefChangeFromDialog();
    void selectionRendersWithoutDisplayServer();
    void tupletsRespectFocusAndPlayback();
    void copyPasteUsesPointerStaffMapping();
    void rangeOverlayRendersWithoutRelayout();
    void transformedResultsFloatDockEditAndPaste();
    void minimizedResultsRestoreExistingWorkspace();
    void transformationControlsUseRealWorker_data();
    void transformationControlsUseRealWorker();
    void keyDialogAndSuggestions();
    void melodyAnalysisReportAndFindings();
    void interactivePreviewControlsAndPresentation();
};

void UiTests::opensRendersAndAcceptsKeyboard()
{
    music::Editor editor;
    QQmlApplicationEngine engine;
    QSignalSpy qmlWarnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor", &editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    auto* view = window->findChild<music::ScoreView*>("scoreView");
    QVERIFY(view);
    QTest::qWait(150);
    view->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_C);
    const auto* event = editor.score().event(editor.score().firstEventId(0));
    QVERIFY(event);
    QCOMPARE(event->pitches.size(), std::size_t{1});
    QCOMPARE(event->pitches.front().step, 0);
    QTest::keyClick(window, Qt::Key_E, Qt::ShiftModifier);
    QCOMPARE(editor.score().event(editor.score().firstEventId(0))->pitches.size(), std::size_t{2});
    QVERIFY(window->isVisible());
    QSignalSpy invalidations(&editor,&music::Editor::layoutInvalidated);
    const auto cacheCount=view->cachedSystemCount();
    editor.selectEvent(editor.score().firstEventId(0),0);
    QTest::qWait(20);
    QCOMPARE(invalidations.size(),0);
    QCOMPARE(view->cachedSystemCount(),cacheCount);
    auto* staffDialog=window->findChild<QObject*>("staffDialog");
    auto* meterDialog=window->findChild<QObject*>("meterDialog");
    auto* tempoDialog=window->findChild<QObject*>("tempoDialog");
    QVERIFY(staffDialog && meterDialog && tempoDialog);
    QTest::keyClick(window,Qt::Key_S);
    QTRY_VERIFY(staffDialog->property("visible").toBool());
    auto* choice=window->findChild<QObject*>("clefChoice");
    QVERIFY(choice);
    choice->setProperty("currentIndex",1);
    auto* apply=window->findChild<QQuickItem*>("applyClef");
    QVERIFY(apply);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,apply->mapToScene(QPointF(apply->width()/2,apply->height()/2)).toPoint());
    QTRY_VERIFY(!staffDialog->property("visible").toBool());
    QTRY_VERIFY(view->hasActiveFocus());
    QCOMPARE(editor.score().clefAt(0,0),music::Clef::Bass);
    QTest::keyClick(window,Qt::Key_M);
    QTRY_VERIFY(meterDialog->property("visible").toBool());
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(!meterDialog->property("visible").toBool());
    QTRY_VERIFY(view->hasActiveFocus());
    QTest::keyClick(window,Qt::Key_T,Qt::ShiftModifier);
    QTRY_VERIFY(tempoDialog->property("visible").toBool());
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(!tempoDialog->property("visible").toBool());
    QTRY_VERIFY(view->hasActiveFocus());
    editor.selectMeasure(0,1);
    const auto point=view->mapToScene(editor.selectionRect().center()).toPoint();
    QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,point);
    QTRY_VERIFY(staffDialog->property("visible").toBool());
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(!staffDialog->property("visible").toBool());
    QTRY_VERIFY(view->hasActiveFocus());
    QVERIFY(editor.addPart(40));
    auto* instrumentation=window->findChild<QQuickItem*>("instrumentationButton");
    QVERIFY(instrumentation);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,instrumentation->mapToScene(QPointF(instrumentation->width()/2,instrumentation->height()/2)).toPoint());
    auto* instruments=window->findChild<QObject*>("instrumentationDialog");
    QVERIFY(instruments);
    QTRY_VERIFY(instruments->property("visible").toBool());
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(!instruments->property("visible").toBool());
    QCOMPARE(qmlWarnings.size(),0);
}

void UiTests::tempoApplyRestoresNoteEntryAndStaffInstrument()
{
    music::Editor editor;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appEditor", &editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    auto* view = window->findChild<music::ScoreView*>("scoreView");
    QVERIFY(view);
    QTest::qWait(100);
    editor.selectMeasure(0, 1);
    view->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_T, Qt::ShiftModifier);
    auto* tempo = window->findChild<QObject*>("tempoDialog");
    QTRY_VERIFY(tempo->property("visible").toBool());
    window->findChild<QObject*>("tempoBpm")->setProperty("value", 90);
    auto* applyTempo = window->findChild<QQuickItem*>("applyTempo");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                     applyTempo->mapToScene(QPointF(applyTempo->width()/2, applyTempo->height()/2)).toPoint());
    QTRY_VERIFY(!tempo->property("visible").toBool());
    QCOMPARE(editor.score().tempoAt(1).bpm, 90);
    QTRY_VERIFY(view->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_C);
    QVERIFY(!editor.score().event(editor.score().firstEventId(1))->isRest());

    editor.requestStaffProperties();
    QTest::keyClick(window, Qt::Key_Escape);
    auto* instrumentation = window->findChild<QQuickItem*>("instrumentationButton");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                     instrumentation->mapToScene(QPointF(instrumentation->width()/2, instrumentation->height()/2)).toPoint());
    auto* dialog = window->findChild<QObject*>("instrumentationDialog");
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTRY_VERIFY(findItem(window->contentItem(), "staffInstrument"));
    auto* choice = findItem(window->contentItem(), "staffInstrument");
    choice->setProperty("currentIndex", 41); // Violin, following "Use part instrument".
    QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, 41)));
    QCOMPARE(editor.score().program(0), 40);
}

void UiTests::spaceTogglesFromSelectionAndRespectsDialogs()
{
    music::Editor editor;
    editor.setAuditionEnabled(false);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* view=window->findChild<music::ScoreView*>("scoreView");
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(editor.audioReady(),10000);
    editor.selectMeasure(0,2);
    view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_C);
    const auto id=editor.score().firstEventId(2);
    auto* volume=window->findChild<QQuickItem*>("volumeSlider");
    QVERIFY(volume);
    volume->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(editor.playing());
    const auto start=editor.layoutIndex().position(editor.score(),editor.score().measureStart(2));
    QVERIFY(std::abs(editor.playheadX()-start.x())<5);
    QCOMPARE(editor.score().event(id)->pitches.front().step,0);
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(!editor.playing());
    QCOMPARE(editor.score().event(id)->pitches.front().step,0);
    view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_T,Qt::ShiftModifier);
    auto* dialog=window->findChild<QObject*>("tempoDialog");
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(!editor.playing());
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(view->hasActiveFocus());
    editor.newScore();
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(editor.playing());
    QVERIFY(editor.score().event(editor.score().firstEventId(0))->isRest());
    QVERIFY(std::abs(editor.playheadX()-editor.layoutIndex().position(editor.score(),0).x())<5);
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(!editor.playing());
}

void UiTests::removesClefChangeFromDialog()
{
    music::Editor editor;
    editor.setAuditionEnabled(false);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* view=window->findChild<music::ScoreView*>("scoreView");
    QVERIFY(view);
    QTest::qWait(100);
    editor.selectMeasure(0,1);
    QVERIFY(editor.handleKey(Qt::Key_C,0));
    QVERIFY(editor.applyClef(static_cast<int>(music::Clef::Bass)));
    editor.requestStaffProperties();
    auto* dialog=window->findChild<QObject*>("staffDialog");
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto* remove=window->findChild<QQuickItem*>("removeClef");
    QVERIFY(remove && remove->isEnabled());
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
                     remove->mapToScene(QPointF(remove->width()/2,remove->height()/2)).toPoint());
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(editor.score().clefAt(0,editor.score().measureStart(1)),music::Clef::Treble);
    QVERIFY(!editor.score().event(editor.score().firstEventId(1))->isRest());
    QTRY_VERIFY(view->hasActiveFocus());
    editor.undo();
    QCOMPARE(editor.score().clefAt(0,editor.score().measureStart(1)),music::Clef::Bass);
}

void UiTests::tupletsRespectFocusAndPlayback()
{
    music::Editor editor;
    editor.setAuditionEnabled(false);
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* view=window->findChild<music::ScoreView*>("scoreView");
    QVERIFY(view);
    QTest::qWait(100);
    view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_C);
    QTest::keyClick(window,Qt::Key_E,Qt::ShiftModifier);
    const auto first=editor.score().firstEventId(0);
    QTest::keyClick(window,Qt::Key_3);
    QCOMPARE(editor.score().event(first)->ticks,music::quarter/3);
    QCOMPARE(editor.score().event(first)->pitches.size(),std::size_t{2});
    QTest::keyClick(window,Qt::Key_D);
    QCOMPARE(editor.score().event(editor.score().nextEventId(first))->pitches.front().step,1);
    const auto before=editor.score().measure(0);
    QTest::keyClick(window,Qt::Key_T,Qt::ShiftModifier);
    auto* dialog=window->findChild<QObject*>("tempoDialog");
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTest::keyClick(window,Qt::Key_5);
    QCOMPARE(editor.score().measure(0),before);
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(view->hasActiveFocus());
    editor.selectEvent(editor.score().firstEventId(1));
    QTest::keyClick(window,Qt::Key_7);
    QCOMPARE(editor.score().measure(1).tuplets.front().actual,7);
    QTest::keyClick(window,Qt::Key_C);
    const auto playingScore=editor.score().staves();
    QTRY_VERIFY_WITH_TIMEOUT(editor.audioReady(),10000);
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(editor.playing());
    QTest::keyClick(window,Qt::Key_5);
    QVERIFY(!editor.createTuplet(3));
    QCOMPARE(editor.score().staves(),playingScore);
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(!editor.playing());
    QCOMPARE(warnings.size(),0);
}

void UiTests::copyPasteUsesPointerStaffMapping()
{
    using namespace music;
    Editor editor;
    editor.setAuditionEnabled(false);
    for(int i=1;i<4;++i)QVERIFY(editor.addPart(40+i));
    for(int key:{Qt::Key_C,Qt::Key_D,Qt::Key_E,Qt::Key_F})QVERIFY(editor.handleKey(key,0));
    const auto first=editor.score().firstEventId(0);
    editor.selectEvent(first);
    QVERIFY(editor.handleKey(Qt::Key_E,Qt::ShiftModifier));
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    window->setHeight(1100);
    auto* view=window->findChild<ScoreView*>("scoreView");
    QVERIFY(view);
    QTest::qWait(100);
    auto point=[&](EventId id) {
        const auto loc=editor.score().locate(id);
        const auto system=editor.layoutIndex().systemForMeasure(loc->measure);
        const auto snapshot=Engraving::layout(editor.score(),editor.layoutIndex(),system);
        const auto e=std::find_if(snapshot.events.begin(),snapshot.events.end(),[&](const EngravedEvent& event){return event.event.id==id;});
        return view->mapToScene(e->hitBox.center()+QPointF(0,system*editor.layoutIndex().systemHeight())).toPoint();
    };
    view->forceActiveFocus();
    QSignalSpy invalidations(&editor,&Editor::layoutInvalidated);
    QTest::keyClick(window,Qt::Key_Right,Qt::ShiftModifier);
    QCOMPARE(editor.selectedRange().end,2*quarter);
    QTest::mouseClick(window,Qt::LeftButton,Qt::ControlModifier,point(editor.score().firstEventId(0,2)));
    QCOMPARE(editor.selectedRange().staves.size(),std::size_t{2});
    QCOMPARE(editor.selectedRange().end,2*quarter);
    QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(invalidations.size(),0);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point(editor.score().firstEventId(1,1)));
    QVERIFY(!editor.hasRangeSelection());
    QTest::mouseClick(window,Qt::LeftButton,Qt::ControlModifier,point(editor.score().firstEventId(1,3)));
    QCOMPARE(editor.selectedRange().staves.size(),std::size_t{2});
    QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
    QVERIFY(editor.score().valid());
    QCOMPARE(editor.selectedRange().start,4*quarter);
    QCOMPARE(editor.selectedRange().end,6*quarter);
    QCOMPARE(editor.score().event(editor.score().firstEventId(1,1))->pitches.size(),std::size_t{2});
    QVERIFY(editor.score().event(editor.score().firstEventId(1,3))->isRest());
    const auto after=editor.score().staves();
    QTest::keyClick(window,Qt::Key_T,Qt::ShiftModifier);
    QVERIFY(!window->findChild<QObject*>("tempoDialog")->property("visible").toBool());
    QCOMPARE(editor.score().staves(),after);
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(!editor.hasRangeSelection());
    QTest::keyClick(window,Qt::Key_T,Qt::ShiftModifier);
    auto* dialog=window->findChild<QObject*>("tempoDialog");
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
    QCOMPARE(editor.score().staves(),after);
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(view->hasActiveFocus());
    QTRY_VERIFY_WITH_TIMEOUT(editor.audioReady(),10000);
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(editor.playing());
    QVERIFY(!editor.pasteSelection());
    QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
    QCOMPARE(editor.score().staves(),after);
    QTest::keyClick(window,Qt::Key_Space);
    QVERIFY(!editor.playing());
    QCOMPARE(warnings.size(),0);
}

void UiTests::rangeOverlayRendersWithoutRelayout()
{
    using namespace music;
    Editor editor;
    editor.setAuditionEnabled(false);
    for(int i=1;i<4;++i)QVERIFY(editor.addPart(40+i));
    QVERIFY(editor.handleKey(Qt::Key_C,0));
    QVERIFY(editor.handleKey(Qt::Key_D,0));
    editor.selectEvent(editor.score().firstEventId(0));
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setGeometry(0,0,1050,800);
    ScoreView view(window.contentItem());
    view.setWidth(editor.scoreWidth());
    view.setHeight(editor.scoreHeight());
    view.setViewportHeight(800);
    view.setEditor(&editor);
    control.polishItems(); control.sync();
    const auto before=window.grabWindow();
    QVERIFY(!before.isNull());
    const auto cache=view.cachedSystemCount();
    QSignalSpy invalidations(&editor,&Editor::layoutInvalidated);
    QVERIFY(editor.handleKey(Qt::Key_Right,Qt::ShiftModifier));
    editor.activateHit({HitKind::Note,editor.score().firstEventId(0,2),0,2,0},false,Qt::ControlModifier);
    control.polishItems(); control.sync();
    const auto image=window.grabWindow();
    QVERIFY(!image.isNull() && image!=before);
    QCOMPARE(view.cachedSystemCount(),cache);
    QCOMPARE(invalidations.size(),0);
    const auto rectangles=editor.rangeRects(0);
    QCOMPARE(rectangles.size(),std::size_t{2});
    for(const auto& rect:rectangles) {
        int blue=0;
        for(int y=int(rect.top());y<int(rect.bottom());++y)for(int x=int(rect.left());x<int(rect.right());++x) {
            const auto c=image.pixelColor(x,y);
            if(c.red()>180 && c.blue()>c.red()+10 && c.blue()>c.green()+5)++blue;
        }
        QVERIFY(blue>100);
    }
    const auto gap=editor.layoutIndex().measureRect(0,1).center().toPoint();
    QCOMPARE(image.pixelColor(gap),before.pixelColor(gap));
    QVERIFY(QDir().mkpath(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)));
    QVERIFY(image.save(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)+"/range-selection.png"));
    view.setViewportY(editor.layoutIndex().systemHeight());
    QVERIFY(view.cachedSystemCount()<=3);
    QCOMPARE(invalidations.size(),0);
}

void UiTests::selectionRendersWithoutDisplayServer()
{
    music::Editor editor;
    editor.setAuditionEnabled(false);
    QVERIFY(editor.handleKey(Qt::Key_C,0));
    QVERIFY(editor.handleKey(Qt::Key_D,Qt::ShiftModifier));
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setGeometry(0,0,1050,600);
    music::ScoreView view(window.contentItem());
    view.setWidth(editor.scoreWidth());
    view.setHeight(editor.scoreHeight());
    view.setEditor(&editor);
    control.polishItems();
    control.sync();
    const auto image=window.grabWindow();
    QVERIFY(!image.isNull());
    int orange=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
        const auto c=image.pixelColor(x,y);
        if(c.red()>180 && c.green()>60 && c.green()<150 && c.blue()<60)++orange;
    }
    QVERIFY(orange>10);
    QVERIFY(QDir().mkpath(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)));
    QVERIFY(image.save(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)+"/selection.png"));
    QSignalSpy invalidations(&editor,&music::Editor::layoutInvalidated);
    const auto count=view.cachedSystemCount();
    editor.selectEvent(editor.score().firstEventId(0),0);
    control.polishItems();
    control.sync();
    const auto changed=window.grabWindow();
    QVERIFY(!changed.isNull());
    QVERIFY(changed!=image);
    QCOMPARE(invalidations.size(),0);
    QCOMPARE(view.cachedSystemCount(),count);
    view.setEditor(nullptr);
    control.polishItems();
    control.sync();
    const auto empty=window.grabWindow();
    QVERIFY(!empty.isNull());
    orange=0;
    for(int y=0;y<empty.height();++y)for(int x=0;x<empty.width();++x) {
        const auto c=empty.pixelColor(x,y);
        if(c.red()>180 && c.green()>60 && c.green()<150 && c.blue()<60)++orange;
    }
    QCOMPARE(orange,0);
}

void UiTests::transformedResultsFloatDockEditAndPaste()
{
    using namespace music;
    Editor editor;
    editor.setAuditionEnabled(false);
    Score source(12);
    QVERIFY(source.enterNote(source.firstEventId(0),{0,4,0},quarter));
    auto second=source.nextEventId(source.firstEventId(0));
    QVERIFY(source.enterNote(second,{1,4,0},quarter));
    QVERIFY(source.addPart("Strings",40));
    editor.installScratch(std::move(source),0,48*quarter,"Source");
    auto* workspace=qobject_cast<TransformWorkspace*>(editor.transformations());
    QVERIFY(workspace);
    workspace->setWorkerCommand("python3",{QString(MUSIC_TEST_WORKER),"identity"});
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* view=window->findChild<ScoreView*>("scoreView");
    QVERIFY(view);
    QTest::qWait(100);
    view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_T);
    auto* dialog=window->findChild<QObject*>("transformDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto* submit=window->findChild<QQuickItem*>("submitTransform");
    QVERIFY(submit);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,submit->mapToScene({submit->width()/2,submit->height()/2}).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(workspace->currentEditor(),5000);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    auto* result=workspace->currentEditor();
    QCOMPARE(result->score().staffCount(),std::size_t{1});
    QQuickWindow* floating=nullptr;
    for(auto* candidate:QGuiApplication::allWindows())if(candidate->objectName()=="resultWindow")floating=qobject_cast<QQuickWindow*>(candidate);
    QVERIFY(floating);
    QTRY_VERIFY(floating->isVisible());
    auto* preview=qobject_cast<ScoreView*>(findItem(floating->contentItem(),"resultScoreView"));
    QVERIFY(preview);
    QTRY_VERIFY(preview->hasActiveFocus());
    result->selectEvent(result->score().firstEventId(0));
    QTest::keyClick(floating,Qt::Key_Right,Qt::ShiftModifier);
    QCOMPARE(result->selectedRange().end,2*quarter);
    QTest::keyClick(floating,Qt::Key_C,Qt::ControlModifier);
    editor.selectMeasure(1,2);
    window->requestActivate(); view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
    QCOMPARE(editor.score().event(editor.score().eventAt(1,8*quarter))->pitches.front().step,0);
    QCOMPARE(editor.score().event(editor.score().eventAt(1,9*quarter))->pitches.front().step,1);
    QCOMPARE(editor.score().program(1),40);
    result->selectEvent(result->score().firstEventId(0));
    floating->requestActivate(); preview->forceActiveFocus();
    QTest::keyClick(floating,Qt::Key_Up,Qt::ShiftModifier);
    QVERIFY(result->dirty());
    QVERIFY(result->canUndo());
    QCOMPARE(result->score().event(result->score().firstEventId(0))->pitches.front().midi(),61);
    QCOMPARE(editor.score().event(editor.score().firstEventId(0))->pitches.front().midi(),60);
    auto* resultScroll=findItem(floating->contentItem(),"resultScroll");
    QVERIFY(resultScroll);
    resultScroll->setProperty("contentY",350);
    QTest::qWait(30);
    auto* dock=findItem(floating->contentItem(),"toggleDock");
    QVERIFY(dock);
    QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,dock->mapToScene({dock->width()/2,dock->height()/2}).toPoint());
    QTRY_VERIFY(!workspace->floating());
    QTRY_VERIFY(!floating->isVisible());
    QTRY_VERIFY(findItem(window->contentItem(),"resultScoreView"));
    auto* docked=qobject_cast<ScoreView*>(findItem(window->contentItem(),"resultScoreView"));
    QCOMPARE(docked->editor(),result);
    QCOMPARE(workspace->currentEditor(),result);
    auto* dockedScroll=findItem(window->contentItem(),"resultScroll");
    QVERIFY(dockedScroll);
    QTRY_COMPARE(dockedScroll->property("contentY").toDouble(),350.0);
    QVERIFY(result->canUndo());
    result->undo();
    QCOMPARE(result->score().event(result->score().firstEventId(0))->pitches.front().midi(),60);
    QVERIFY(workspace->begin(result));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(workspace->submit({QVariantMap{{"kind","transpose"},{"amount",0}}}));
    QTRY_COMPARE_WITH_TIMEOUT(workspace->tabs().size(),2,5000);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    workspace->setCurrentIndex(0);
    QCOMPARE(workspace->currentEditor(),result);
    QCOMPARE(docked->editor(),result);
    QTRY_COMPARE(findItem(window->contentItem(),"resultScroll")->property("contentY").toDouble(),350.0);
    result->rememberScroll(0,0);
    QTest::qWait(50);
    QQuickRenderControl control;
    QQuickWindow rendered(&control);
    rendered.setGeometry(0,0,1180,560);
    QQmlComponent panelComponent(&engine,QUrl("qrc:/qml/ResultPane.qml"));
    std::unique_ptr<QObject> panel(panelComponent.createWithInitialProperties({{"workspace",QVariant::fromValue(workspace)}}));
    auto* panelItem=qobject_cast<QQuickItem*>(panel.get());
    QVERIFY(panelItem);
    panelItem->setParentItem(rendered.contentItem());
    panelItem->setWidth(1180); panelItem->setHeight(560);
    control.polishItems(); control.sync();
    const auto screenshot=rendered.grabWindow();
    QVERIFY(!screenshot.isNull());
    QVERIFY(screenshot.save(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)+"/transformed-result.png"));
    panel.reset();
    workspace->setFloating(true);
    QTRY_VERIFY(floating->isVisible());
    preview=qobject_cast<ScoreView*>(findItem(floating->contentItem(),"resultScoreView"));
    QVERIFY(preview);
    floating->requestActivate(); preview->forceActiveFocus();
    QTRY_VERIFY(preview->hasActiveFocus());
    QTest::keyClick(floating,Qt::Key_S);
    auto* staffDialog=window->findChild<QObject*>("staffDialog");
    QTRY_VERIFY(staffDialog->property("visible").toBool());
    QTest::keyClick(floating,Qt::Key_Escape);
    QTRY_VERIFY(!staffDialog->property("visible").toBool());
    QVERIFY(!workspace->closeResult(0));
    auto* closeDialog=window->findChild<QObject*>("resultCloseDialog");
    QTRY_VERIFY(closeDialog->property("visible").toBool());
    auto* discard=window->findChild<QQuickItem*>("discardResult");
    QVERIFY(discard);
    QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,discard->mapToScene({discard->width()/2,discard->height()/2}).toPoint());
    QTRY_COMPARE(workspace->tabs().size(),1);
    QTRY_VERIFY(!closeDialog->property("visible").toBool());
    workspace->setWorkerCommand("python3",{QString(MUSIC_TEST_WORKER),"delay"});
    QVERIFY(workspace->begin(workspace->currentEditor()));
    QTRY_VERIFY(dialog->property("visible").toBool());
    submit=window->findChild<QQuickItem*>("submitTransform");
    QVERIFY(submit);
    QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,submit->mapToScene({submit->width()/2,submit->height()/2}).toPoint());
    QVERIFY(workspace->busy());
    QTest::keyClick(floating,Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(!workspace->busy());
    QCOMPARE(workspace->tabs().size(),1);
    QCOMPARE(warnings.size(),0);
}

void UiTests::minimizedResultsRestoreExistingWorkspace()
{
    using namespace music;
    Editor editor;
    editor.setAuditionEnabled(false);
    Score source(12);
    QVERIFY(source.enterNote(source.firstEventId(0),{0,4,0},quarter));
    editor.installScratch(std::move(source),0,48*quarter,"Source");
    auto* workspace=qobject_cast<TransformWorkspace*>(editor.transformations());
    QVERIFY(workspace);
    workspace->setWorkerCommand("python3",{QString(MUSIC_TEST_WORKER),"identity"});
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* restore=window->findChild<QQuickItem*>("restoreResultsTab");
    auto* view=window->findChild<ScoreView*>("scoreView");
    QVERIFY(restore && view);
    QVERIFY(!restore->isVisible());
    auto click=[window](QQuickItem* item) {
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene({item->width()/2,item->height()/2}).toPoint());
    };
    for(int i=0;i<2;++i) {
        QVERIFY(workspace->begin(&editor));
        QVERIFY(workspace->submit({QVariantMap{{"kind","transpose"},{"amount",0}}}));
        QTRY_COMPARE_WITH_TIMEOUT(workspace->tabs().size(),i+1,5000);
    }
    auto* dialog=window->findChild<QObject*>("transformDialog");
    QTRY_VERIFY(!dialog->property("visible").toBool());
    auto* floating=window->findChild<QQuickWindow*>("resultWindow");
    QVERIFY(floating);
    QTRY_VERIFY(floating->isVisible());
    workspace->setCurrentIndex(0);
    auto* result=workspace->currentEditor();
    result->selectEvent(result->score().firstEventId(0));
    QVERIFY(result->handleKey(Qt::Key_Up,Qt::ShiftModifier));
    QVERIFY(result->dirty() && result->canUndo());
    QVERIFY(result->handleKey(Qt::Key_Right,Qt::ShiftModifier));
    const auto selection=result->selectedRange();
    const auto revision=result->revision();
    QTRY_VERIFY(findItem(floating->contentItem(),"resultScroll"));
    QTest::qWait(30);
    findItem(floating->contentItem(),"resultScroll")->setProperty("contentY",350);
    QSignalSpy configurations(workspace,&TransformWorkspace::configurationRequested);
    QSignalSpy results(workspace,&TransformWorkspace::resultReady);
    QSignalSpy invalidations(result,&Editor::layoutInvalidated);
    for(int i=0;i<4;++i) {
        QPointer<QQuickItem> oldView=findItem(floating->contentItem(),"resultScoreView");
        QVERIFY(oldView);
        if(i%2==0) floating->showMinimized();
        else QVERIFY(!floating->close());
        QTRY_VERIFY(restore->isVisible());
        QTRY_VERIFY(!floating->isVisible());
        QTRY_VERIFY(oldView.isNull());
        QTRY_VERIFY(view->hasActiveFocus());
        QTRY_VERIFY(restore->width()>0 && restore->height()>0);
        QTRY_VERIFY(restore->mapToScene({0,0}).y()>window->height()/2);
        QCOMPARE(restore->property("text").toString(),QString("Transform results (2)"));
        QCOMPARE(result->rememberedScroll().y(),350.0);
        if(i%2==0) click(restore);
        else {
            restore->forceActiveFocus();
            QTest::keyClick(window,Qt::Key_Space);
        }
        QTRY_VERIFY(floating->isVisible());
        QTRY_COMPARE(floating->visibility(),QWindow::Windowed);
        QTRY_VERIFY(!restore->isVisible());
        auto* preview=qobject_cast<ScoreView*>(findItem(floating->contentItem(),"resultScoreView"));
        QVERIFY(preview);
        QTRY_VERIFY(preview->hasActiveFocus());
        QCOMPARE(preview->editor(),result);
        QCOMPARE(workspace->currentEditor(),result);
        QCOMPARE(workspace->currentIndex(),0);
        QCOMPARE(workspace->tabs().size(),2);
        QCOMPARE(result->revision(),revision);
        QVERIFY(result->dirty() && result->canUndo());
        QCOMPARE(result->selectedRange().start,selection.start);
        QCOMPARE(result->selectedRange().end,selection.end);
        QCOMPARE(result->selectedRange().staves,selection.staves);
        QTRY_COMPARE(findItem(floating->contentItem(),"resultScroll")->property("contentY").toDouble(),350.0);
        const auto windows=QGuiApplication::allWindows();
        QCOMPARE(std::count_if(windows.begin(),windows.end(),[](QWindow* w){return w->objectName()=="resultWindow";}),1);
        QVERIFY(!workspace->busy());
    }
    QCOMPARE(configurations.size(),0);
    QCOMPARE(results.size(),0);
    QCOMPARE(invalidations.size(),0);
    result->undo();
    QCOMPARE(result->score().event(result->score().firstEventId(0))->pitches.front().midi(),60);

    floating->showMinimized();
    QTRY_VERIFY(restore->isVisible());
    QTRY_VERIFY(!floating->isVisible());
    QTRY_VERIFY(view->hasActiveFocus());
    QTest::keyClick(window,Qt::Key_T);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(configurations.size(),1);
    QCOMPARE(workspace->tabs().size(),2);
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    click(window->findChild<QQuickItem*>("showResults"));
    QTRY_COMPARE(floating->visibility(),QWindow::Windowed);
    QVERIFY(!restore->isVisible());
    QVERIFY(!floating->close());
    QTRY_VERIFY(restore->isVisible());
    QVERIFY(workspace->closeResult(1,true));
    QCOMPARE(restore->property("text").toString(),QString("Transform results (1)"));
    QVERIFY(workspace->closeResult(0,true));
    QTRY_VERIFY(!restore->isVisible());
    QVERIFY2(warnings.isEmpty(),warnings.isEmpty()?"":qPrintable(warnings.front().front().value<QList<QQmlError>>().front().toString()));
}

void UiTests::transformationControlsUseRealWorker_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<bool>("reorder");
    QTest::addColumn<QList<int>>("expectedMidi");
    QTest::addColumn<int>("durationQuarters");
    QTest::addColumn<bool>("chromatic");
    QTest::newRow("scale-step-transpose")<<0<<false<<QList<int>{64,65}<<2<<false;
    QTest::newRow("diatonic-inversion")<<1<<false<<QList<int>{60,59}<<2<<false;
    QTest::newRow("rhythm-scale")<<2<<false<<QList<int>{60,62}<<4<<false;
    QTest::newRow("retrograde")<<3<<false<<QList<int>{62,60}<<2<<false;
    QTest::newRow("edit-second-step-and-reorder")<<1<<true<<QList<int>{64,62}<<2<<false;
    QTest::newRow("chromatic-transpose")<<0<<false<<QList<int>{62,64}<<2<<true;
    QTest::newRow("chromatic-inversion")<<1<<false<<QList<int>{60,58}<<2<<true;
}

void UiTests::transformationControlsUseRealWorker()
{
    using namespace music;
    QFETCH(int,operation);
    QFETCH(bool,reorder);
    QFETCH(QList<int>,expectedMidi);
    QFETCH(int,durationQuarters);
    QFETCH(bool,chromatic);
    QString python=qEnvironmentVariable("MUSIC_PYTHON");
    if(python.isEmpty())python="python3";
    QProcess probe;
    probe.start(python,{"-c","import music21"});
    if(!probe.waitForFinished(30000) || probe.exitCode()!=0)QSKIP("Real music21 is unavailable; run this test through Poetry.");
    Editor editor;
    editor.setAuditionEnabled(false);
    Score source(1);
    const auto first=source.firstEventId(0);
    QVERIFY(source.enterNote(first,{0,4,0},quarter));
    QVERIFY(source.enterNote(source.nextEventId(first),{1,4,0},quarter));
    editor.installScratch(std::move(source),0,2*quarter,"Source");
    const auto before=editor.score().measure(0);
    auto* workspace=qobject_cast<TransformWorkspace*>(editor.transformations());
    workspace->setFloating(false);
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* view=window->findChild<ScoreView*>("scoreView");
    QVERIFY(view);
    QTest::qWait(100);
    view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_T);
    auto* dialog=window->findChild<QObject*>("transformDialog");
    QTRY_VERIFY(dialog && dialog->property("visible").toBool());
    auto click=[window](QQuickItem* item) {
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene({item->width()/2,item->height()/2}).toPoint());
    };
    if(operation==0 || reorder) {
        QTRY_VERIFY(findItem(window->contentItem(),"transformAmount0"));
        auto* amount=findItem(window->contentItem(),"transformAmount0");
        QVERIFY(amount);
        auto* input=qobject_cast<QQuickItem*>(amount->property("contentItem").value<QObject*>());
        QVERIFY(input);
        input->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_2);
        // Clicking Submit must commit typed values without requiring Enter.
    }
    if(reorder) {
        auto* add=window->findChild<QQuickItem*>("addTransformStep");
        QVERIFY(add);
        click(add);
    }
    const auto row=reorder?1:0;
    QTRY_VERIFY(findItem(window->contentItem(),"transformType"+QString::number(row)));
    auto* choice=findItem(window->contentItem(),"transformType"+QString::number(row));
    if(operation>0) {
        choice->forceActiveFocus();
        for(int i=0;i<operation;++i)QTest::keyClick(window,Qt::Key_Down);
        QCOMPARE(choice->property("currentIndex").toInt(),operation);
    }
    if(chromatic) {
        auto* mode=findItem(window->contentItem(),"transformMode0");
        QVERIFY(mode);
        mode->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_Down);
        QCOMPARE(mode->property("currentIndex").toInt(),1);
    }
    if(reorder) {
        auto* up=findItem(window->contentItem(),"moveTransformUp1");
        QVERIFY(up && up->isEnabled());
        click(up);
        QTRY_COMPARE(findItem(window->contentItem(),"transformType0")->property("currentIndex").toInt(),1);
        QTRY_COMPARE(findItem(window->contentItem(),"transformType1")->property("currentIndex").toInt(),0);
    }
    auto* submit=window->findChild<QQuickItem*>("submitTransform");
    QVERIFY(submit);
    click(submit);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(),30000);
    auto* result=workspace->currentEditor();
    QVERIFY2(result,qPrintable(workspace->status()));
    const auto range=result->selectedRange();
    QCOMPARE(range.end-range.start,durationQuarters*quarter);
    QList<int> actual;
    for(const auto& note:Playback::schedule(result->score(),range.start,range.end))if(note.on)actual.append(note.midi);
    QCOMPARE(actual,expectedMidi);
    QCOMPARE(editor.score().measure(0),before);
    QCOMPARE(warnings.size(),0);
}

void UiTests::keyDialogAndSuggestions()
{
    using namespace music;
    QProcess probe;
    probe.start(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-c","import music21"});
    if(!probe.waitForFinished(30000) || probe.exitCode()!=0)QSKIP("Run through Poetry for key analysis.");
    Editor editor;
    editor.setAuditionEnabled(false);
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto click=[window](QQuickItem* item) {
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene({item->width()/2,item->height()/2}).toPoint());
    };
    QTest::qWait(100);
    auto* button=window->findChild<QQuickItem*>("keyButton");
    QVERIFY(button);
    click(button);
    auto* dialog=window->findChild<QObject*>("keyDialog");
    QTRY_VERIFY(dialog && dialog->property("visible").toBool());
    auto* form=dialog->findChild<QQuickItem*>("keyForm");
    QVERIFY(form);
    form->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Down);
    QCOMPARE(form->property("currentIndex").toInt(),1);
    auto* tonic=dialog->findChild<QQuickItem*>("keyTonic");
    QVERIFY(tonic);
    QTRY_COMPARE(tonic->property("currentIndex").toInt(),7);
    tonic->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Down);
    QTest::keyClick(window,Qt::Key_Down);
    QCOMPARE(tonic->property("currentIndex").toInt(),9);
    click(dialog->findChild<QQuickItem*>("applyKey"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(editor.score().keyAt(0,0).tonality->scale,ScaleIdentity::Minor);
    QCOMPARE(editor.score().keyAt(0,0).fifths,std::optional<int>{2});
    auto* view=window->findChild<ScoreView*>("scoreView");
    QVERIFY(view);
    view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_G);
    QCOMPARE(editor.score().measure(0).events.front().pitches.front().alter,0);
    editor.selectEvent(editor.score().firstEventId(0));
    QVERIFY(editor.applyKey(0,0));
    click(button);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTRY_COMPARE(form->property("currentIndex").toInt(),0);
    QTRY_COMPARE(tonic->property("currentIndex").toInt(),7);
    QTest::keyClick(window,Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    Score source(1);
    QVERIFY(source.setKey(0,0,KeyContext{std::nullopt,std::nullopt}));
    auto id=source.firstEventId(0);
    for(int step:{0,2,4,0}) {
        QVERIFY(source.enterNote(id,{step,4,0},quarter));
        id=source.nextEventId(id);
    }
    editor.installScratch(source,0,source.totalTicks(),"Unknown key");
    auto* workspace=qobject_cast<TransformWorkspace*>(editor.transformations());
    workspace->setFloating(false);
    QVERIFY(workspace->begin(&editor));
    auto* transform=window->findChild<QObject*>("transformDialog");
    QTRY_VERIFY(transform->property("visible").toBool());
    click(window->findChild<QQuickItem*>("suggestKeys"));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(),30000);
    QTRY_VERIFY(findItem(window->contentItem(),"keySuggestions0"));
    auto* suggestions=findItem(window->contentItem(),"keySuggestions0");
    QVERIFY(suggestions->isVisible());
    QVERIFY(!workspace->keysReady());
    suggestions->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Down);
    QCOMPARE(suggestions->property("currentIndex").toInt(),0);
    QVERIFY(!workspace->keysReady());
    click(findItem(window->contentItem(),"confirmRegionKey0"));
    QTRY_VERIFY(workspace->keysReady());
    QVERIFY(!editor.score().keyAt(0,0).tonality);
    click(window->findChild<QQuickItem*>("submitTransform"));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace->busy(),30000);
    QVERIFY2(workspace->currentEditor(),qPrintable(workspace->status()));
    QCOMPARE(workspace->currentEditor()->score().keyAt(0,0),KeyContext::fromSignature(0,ScaleIdentity::Major));
    QCOMPARE(warnings.size(),0);
}

void UiTests::melodyAnalysisReportAndFindings() {
    using namespace music;
    QProcess probe;
    probe.start(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-c","import music21"});
    if(!probe.waitForFinished(30000) || probe.exitCode()!=0)QSKIP("music21 is optional; run through poetry for the real analysis UI test.");
    Score score(2);
    ScoreFragment passage{8*quarter,{{{quarter,{{0,4,0}},{false}},{quarter,{{1,4,0}},{false}},
        {quarter,{{4,4,0}},{false}},{quarter,{},{}},{quarter,{{4,4,0}},{false}},
        {quarter,{{5,4,0}},{false}},{quarter,{{1,5,0}},{false}},{quarter,{},{}}}}};
    QVERIFY(score.pasteRange(passage,{0,0,{score.staves().front().id}}));
    Editor editor; editor.installScratch(std::move(score),0,8*quarter,"Analysis UI");
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* workspace=qobject_cast<TransformWorkspace*>(editor.transformations());
    workspace->setWorkerCommand(qEnvironmentVariable("MUSIC_PYTHON","python3"),{"-u",MUSIC_REAL_WORKER});
    auto* view=window->findChild<ScoreView*>("scoreView"); QVERIFY(view);
    view->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_T);
    auto* dialog=window->findChild<QObject*>("transformDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto* analyze=findItem(window->contentItem(),"analyzeSelection"); QVERIFY(analyze);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,analyze->mapToScene({analyze->width()/2,analyze->height()/2}).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(workspace->hasAnalysis(),15000);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    auto* floating=window->findChild<QQuickWindow*>("resultWindow"); QVERIFY(floating);
    QTRY_VERIFY(floating->isVisible());
    QTRY_VERIFY(findItem(floating->contentItem(),"analysisReport"));
    auto* report=findItem(floating->contentItem(),"analysisReport");
    QVERIFY(report->property("text").toString().contains("Motif candidate"));
    QVERIFY(report->width()<floating->width());
    auto* copy=findItem(floating->contentItem(),"copyAnalysisReport"); QVERIFY(copy);
    QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,copy->mapToScene({copy->width()/2,copy->height()/2}).toPoint());
    QVERIFY(QGuiApplication::clipboard()->text().contains("+7 semitones"));
    auto* findingsTab=findItem(floating->contentItem(),"analysisFindingsTab"); QVERIFY(findingsTab);
    QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,findingsTab->mapToScene({findingsTab->width()/2,findingsTab->height()/2}).toPoint());
    auto* findingView=findItem(floating->contentItem(),"analysisFindingView"); QVERIFY(findingView);
    auto* findingList=findItem(floating->contentItem(),"analysisFindingList"); QVERIFY(findingList);
    QCOMPARE(findingView->property("currentIndex").toInt(),0);
    int structures=0,cells=0,registerIndex=0;
    QString cell;
    const auto allFindings=workspace->analysisFindings();
    for(int i=0;i<allFindings.size();++i) {
        const auto f=allFindings[i].toMap();
        if(f["kind"]=="motif" || f["kind"]=="gesture")++structures;
        if(f["kind"]=="cell") { ++cells; if(cell.isEmpty())cell=f["id"].toString(); }
        if(f["id"]=="f0")registerIndex=i;
    }
    QTRY_COMPARE(findingList->property("count").toInt(),structures);
    QVERIFY(structures>0 && cells>0);
    findingView->setProperty("currentIndex",1);
    QTRY_COMPARE(findingList->property("count").toInt(),cells);
    QTRY_VERIFY(findItem(floating->contentItem(),"nameFinding"+cell));
    auto* cellName=findItem(floating->contentItem(),"nameFinding"+cell);
    cellName->setProperty("text","Supporting figure");
    QVERIFY(QMetaObject::invokeMethod(cellName,"accepted"));
    QTRY_VERIFY(workspace->analysisReport().contains("Supporting figure"));
    QTRY_VERIFY(findItem(floating->contentItem(),"reviewFinding"+cell));
    QVERIFY(QMetaObject::invokeMethod(findItem(floating->contentItem(),"reviewFinding"+cell),"activated",Q_ARG(int,2)));
    for(const auto& value:workspace->analysisFindings())if(value.toMap()["id"]==cell) {
        QCOMPARE(value.toMap()["kind"].toString(),QString("cell"));
        QCOMPARE(value.toMap()["decision"].toString(),QString("rejected"));
    }
    findingView->setProperty("currentIndex",2);
    QTRY_COMPARE(findingList->property("count").toInt(),allFindings.size());
    QVERIFY(QMetaObject::invokeMethod(findingList,"positionViewAtIndex",Q_ARG(int,registerIndex),Q_ARG(int,0)));
    QTRY_VERIFY(findItem(floating->contentItem(),"showFindingf0"));
    auto* show=findItem(floating->contentItem(),"showFindingf0");
    QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,show->mapToScene({show->width()/2,show->height()/2}).toPoint());
    QVERIFY(workspace->currentEditor()->hasRangeSelection());
    QCOMPARE(workspace->currentEditor()->selectedRange().start,0);
    QCOMPARE(workspace->currentEditor()->selectedRange().end,quarter);
    QString motif;
    for(const auto& f:workspace->analysisFindings())if(f.toMap()["kind"]=="motif") { motif=f.toMap()["id"].toString(); break; }
    QVERIFY(workspace->reviewFinding(motif,"accepted","Opening cell"));
    workspace->setFloating(false);
    QTRY_VERIFY(findItem(window->contentItem(),"analysisReport"));
    QVERIFY(workspace->analysisReport().contains("Opening cell"));
    workspace->setFloating(true);
    QTRY_VERIFY(findItem(floating->contentItem(),"analysisReport"));
    // Render the actual report/notation split for visual inspection.
    QQuickRenderControl control;
    QQuickWindow rendered(&control); rendered.setGeometry(0,0,1180,680);
    QQmlComponent component(&engine,QUrl("qrc:/qml/ResultPane.qml"));
    std::unique_ptr<QObject> panel(component.createWithInitialProperties({{"workspace",QVariant::fromValue(workspace)}}));
    auto* item=qobject_cast<QQuickItem*>(panel.get()); QVERIFY(item);
    item->setParentItem(rendered.contentItem()); item->setWidth(1180); item->setHeight(680);
    control.polishItems(); control.sync();
    const auto image=rendered.grabWindow(); QVERIFY(!image.isNull());
    QDir().mkpath(MUSIC_UI_ARTIFACT_DIR);
    QVERIFY(image.save(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)+"/melody-analysis.png"));
    auto* renderedFindings=findItem(item,"analysisFindingsTab"); QVERIFY(renderedFindings);
    renderedFindings->setProperty("checked",true);
    QCoreApplication::processEvents();
    control.polishItems(); control.sync();
    QVERIFY(rendered.grabWindow().save(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)+"/melody-structures.png"));
    panel.reset();
    QVERIFY2(warnings.isEmpty(),warnings.isEmpty()?"":qPrintable(warnings.front().front().value<QList<QQmlError>>().front().toString()));
}

void UiTests::interactivePreviewControlsAndPresentation()
{
    using namespace music;
    Editor editor; editor.setAuditionEnabled(false);
    Score source(2);
    for(int bar=0;bar<2;++bar) {
        QVERIFY(source.enterNote(source.eventAt(0,bar*4*quarter),{0,4,0},quarter));
        QVERIFY(source.enterNote(source.eventAt(0,(bar*4+1)*quarter),{1,4,0},quarter));
        QVERIFY(source.enterNote(source.eventAt(0,(bar*4+2)*quarter),{0,4,0},quarter));
    }
    editor.installScratch(std::move(source),0,8*quarter,"Source");
    const auto sourceRevision=editor.revision();
    QSignalSpy sourceLayout(&editor,&Editor::layoutInvalidated),sourceReset(&editor,&Editor::scoreReset);
    auto* workspace=qobject_cast<TransformWorkspace*>(editor.transformations());
    workspace->setPreviewAudition(false);
    workspace->setWorkerCommand("python3",{QString(MUSIC_TEST_WORKER),"preview"});
    QSignalSpy ready(workspace,&TransformWorkspace::resultReady);
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine,&QQmlApplicationEngine::warnings);
    engine.rootContext()->setContextProperty("appEditor",&editor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    auto* mainView=window->findChild<ScoreView*>("scoreView"); QVERIFY(mainView);
    QTest::qWait(100); mainView->forceActiveFocus(); QTest::keyClick(window,Qt::Key_T);
    auto* dialog=window->findChild<QObject*>("transformDialog");
    QTRY_VERIFY(dialog && dialog->property("visible").toBool());
    auto* start=findItem(window->contentItem(),"startPreview"); QVERIFY(start);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,start->mapToScene({start->width()/2,start->height()/2}).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(workspace->previewCanKeep(),10000);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    auto* preview=workspace->currentEditor(); QVERIFY(preview->editLocked());
    QQuickWindow* floating=nullptr;
    for(auto* candidate:QGuiApplication::allWindows())if(candidate->objectName()=="resultWindow")floating=qobject_cast<QQuickWindow*>(candidate);
    QVERIFY(floating); QTRY_VERIFY(floating->isVisible());
    QTRY_VERIFY(findItem(floating->contentItem(),"resultScoreView"));
    auto* openedView=findItem(floating->contentItem(),"resultScoreView");
    QTRY_VERIFY(openedView->hasActiveFocus());
    QTest::qWait(30); // let the initial dialog/window focus restoration settle
    QTRY_VERIFY(findItem(floating->contentItem(),"transformSlider0"));
    auto* slider=findItem(floating->contentItem(),"transformSlider0");
    slider->forceActiveFocus(); QTest::keyClick(floating,Qt::Key_Right);
    QTRY_COMPARE(workspace->previewSteps().front().toMap()["amount"].toInt(),1);
    QTRY_VERIFY_WITH_TIMEOUT(workspace->previewCanKeep(),10000);
    QCOMPARE(preview->score().event(preview->score().firstEventId(0))->pitches.front().midi(),62);
    QCOMPARE(workspace->currentEditor(),preview); QVERIFY(slider->hasActiveFocus());
    QTRY_VERIFY_WITH_TIMEOUT(workspace->hasAnalysis() && !workspace->analysisStale(),10000);
    QVERIFY(findItem(floating->contentItem(),"analysisReport"));
    const auto beforeDrag=preview->revision();
    const auto point=slider->mapToScene({slider->width()*0.60,slider->height()/2}).toPoint();
    QTest::mousePress(floating,Qt::LeftButton,Qt::NoModifier,point);
    QTest::mouseRelease(floating,Qt::LeftButton,Qt::NoModifier,point);
    QVERIFY(workspace->busy()); // release flushes before the 150 ms timer
    QTRY_VERIFY_WITH_TIMEOUT(workspace->previewCanKeep(),10000);
    QCOMPARE(preview->revision(),beforeDrag+1);
    // Typed numeric edits and pointer release both reach the same session.
    auto* amount=findItem(floating->contentItem(),"transformAmount0"); QVERIFY(amount);
    auto* input=qobject_cast<QQuickItem*>(amount->property("contentItem").value<QObject*>()); QVERIFY(input);
    input->forceActiveFocus(); QTest::keyClick(floating,Qt::Key_A,Qt::ControlModifier); QTest::keyClick(floating,Qt::Key_2); QTest::keyClick(floating,Qt::Key_Return);
    QTRY_COMPARE(workspace->previewSteps().front().toMap()["amount"].toInt(),2);
    QTRY_VERIFY_WITH_TIMEOUT(workspace->previewCanKeep(),10000);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace->analysisStale(),10000);
    workspace->setFloating(false);
    QTRY_VERIFY(findItem(window->contentItem(),"transformAmount0"));
    QCOMPARE(findItem(window->contentItem(),"transformAmount0")->property("value").toInt(),2);
    QCOMPARE(workspace->currentEditor(),preview);
    workspace->setFloating(true);
    QTRY_VERIFY(findItem(floating->contentItem(),"transformAmount0"));
    QCOMPARE(findItem(floating->contentItem(),"transformAmount0")->property("value").toInt(),2);
    floating->showMinimized();
    QTRY_VERIFY(!window->property("workspaceVisible").toBool());
    auto parameters=workspace->previewSteps(); auto step=parameters.front().toMap(); step["amount"]=3; parameters[0]=step;
    QVERIFY(workspace->updatePreview(parameters,true));
    QTRY_VERIFY_WITH_TIMEOUT(workspace->previewCanKeep(),10000);
    QVERIFY(!floating->isVisible()); QCOMPARE(ready.size(),1);
    auto* restore=findItem(window->contentItem(),"restoreResultsTab"); QVERIFY(restore);
    restore->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
    QTRY_VERIFY(floating->isVisible());
    QTRY_VERIFY(findItem(floating->contentItem(),"transformAmount0"));
    QCOMPARE(findItem(floating->contentItem(),"transformAmount0")->property("value").toInt(),3);
    QTRY_VERIFY_WITH_TIMEOUT(workspace->hasAnalysis() && !workspace->analysisStale(),10000);
    // Render all three panes for geometry and legibility inspection.
    QQuickRenderControl control; QQuickWindow rendered(&control); rendered.setGeometry(0,0,1400,700);
    QQmlComponent component(&engine,QUrl("qrc:/qml/ResultPane.qml"));
    std::unique_ptr<QObject> panel(component.createWithInitialProperties({{"workspace",QVariant::fromValue(workspace)}}));
    auto* item=qobject_cast<QQuickItem*>(panel.get()); QVERIFY(item);
    item->setParentItem(rendered.contentItem()); item->setWidth(1400); item->setHeight(700);
    control.polishItems(); control.sync(); QDir().mkpath(MUSIC_UI_ARTIFACT_DIR);
    const auto beforeReplacement=rendered.grabWindow();
    QVERIFY(beforeReplacement.save(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)+"/interactive-preview.png"));
    auto* title=findItem(item,"previewTitle"); QVERIFY(title);
    const auto titleRect=title->mapRectToScene(title->boundingRect()).toAlignedRect();
    auto* type=findItem(floating->contentItem(),"transformType0"); QVERIFY(type);
    QVERIFY(QMetaObject::invokeMethod(type,"activated",Q_ARG(int,2)));
    QTRY_VERIFY_WITH_TIMEOUT(workspace->previewCanKeep(),10000);
    QCOMPARE(preview->selectedRange().end-preview->selectedRange().start,16*quarter);
    auto* duration=findItem(floating->contentItem(),"durationSlider0"); QVERIFY(duration);
    duration->forceActiveFocus(); QTest::keyClick(floating,Qt::Key_Left);
    QTRY_VERIFY_WITH_TIMEOUT(workspace->previewCanKeep(),10000);
    QCOMPARE(preview->selectedRange().end-preview->selectedRange().start,8*quarter);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace->analysisStale(),10000);
    control.polishItems(); control.sync();
    const auto afterReplacement=rendered.grabWindow();
    QVERIFY(afterReplacement.save(QStringLiteral(MUSIC_UI_ARTIFACT_DIR)+"/interactive-rhythm-preview.png"));
    QCOMPARE(afterReplacement.copy(titleRect),beforeReplacement.copy(titleRect)); // notation stays inside its viewport
    panel.reset();
    auto* keep=findItem(floating->contentItem(),"keepPreview"); QVERIFY(keep);
    QVERIFY(QMetaObject::invokeMethod(keep,"clicked"));
    QVERIFY(!workspace->previewActive()); QVERIFY(!preview->editLocked());
    preview->selectEvent(preview->score().firstEventId(0)); preview->handleKey(Qt::Key_Up,Qt::ShiftModifier); QVERIFY(preview->canUndo());
    QCOMPARE(editor.revision(),sourceRevision); QCOMPARE(sourceLayout.size(),0); QCOMPARE(sourceReset.size(),0); QCOMPARE(workspace->tabs().size(),1);
    QVERIFY2(warnings.isEmpty(),warnings.isEmpty()?"":qPrintable(warnings.front().front().value<QList<QQmlError>>().front().toString()));
}

int main(int argc, char** argv)
{
    QTemporaryDir audio;
    qputenv("MUSIC_AUDIO_DRIVER", "file");
    qputenv("MUSIC_AUDIO_FILE", audio.filePath("ui.wav").toUtf8());
    QGuiApplication app(argc, argv);
    QFontDatabase::addApplicationFont(QStringLiteral(":/assets/Bravura.otf"));
    qmlRegisterType<music::ScoreView>("Music", 1, 0, "ScoreView");
    UiTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "UiTests.moc"

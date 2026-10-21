#pragma once
#include "Editor.h"
#include "Transformations.h"
#include "MelodyAnalysis.h"
#include <QProcess>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <atomic>

namespace music {
class TransformWorkspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY changed)
    Q_PROPERTY(music::Editor* currentEditor READ currentEditor NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(music::Editor* sourceEditor READ sourceEditor NOTIFY configurationRequested)
    Q_PROPERTY(QString currentOperations READ currentOperations NOTIFY changed)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY configurationRequested)
    Q_PROPERTY(int defaultAxis READ defaultAxis NOTIFY configurationRequested)
    Q_PROPERTY(int defaultTonicOctave READ defaultTonicOctave NOTIFY configurationRequested)
    Q_PROPERTY(QVariantList keyRegions READ keyRegions NOTIFY changed)
    Q_PROPERTY(bool keysReady READ keysReady NOTIFY changed)
    Q_PROPERTY(bool floating READ floating WRITE setFloating NOTIFY changed)
    Q_PROPERTY(bool hasAnalysis READ hasAnalysis NOTIFY analysisChanged)
    Q_PROPERTY(bool analysisStale READ analysisStale NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisReport READ analysisReport NOTIFY analysisChanged)
    Q_PROPERTY(QVariantList analysisFindings READ analysisFindings NOTIFY analysisChanged)
    Q_PROPERTY(bool previewActive READ previewActive NOTIFY changed)
    Q_PROPERTY(bool currentIsPreview READ currentIsPreview NOTIFY changed)
    Q_PROPERTY(bool previewCanKeep READ previewCanKeep NOTIFY changed)
    Q_PROPERTY(QVariantList previewSteps READ previewSteps NOTIFY previewStepsChanged)
    Q_PROPERTY(bool previewAudition READ previewAudition WRITE setPreviewAudition NOTIFY changed)
    Q_PROPERTY(bool previewRestartPending READ previewRestartPending NOTIFY changed)
    Q_PROPERTY(bool previewControlsEnabled READ previewControlsEnabled NOTIFY changed)
    Q_PROPERTY(QString reportStatus READ reportStatus NOTIFY analysisChanged)
public:
    explicit TransformWorkspace(Editor* main,QObject* parent=nullptr);
    ~TransformWorkspace() override;
    QVariantList tabs() const;
    Editor* currentEditor() const;
    int currentIndex() const { return current_; }
    void setCurrentIndex(int index);
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    Editor* sourceEditor() const { return sourceEditor_.data(); }
    QString currentOperations() const;
    QString sourceSummary() const { return context_.label; }
    int defaultAxis() const { return axis_; }
    int defaultTonicOctave() const { return tonicOctave_; }
    QVariantList keyRegions() const;
    bool keysReady() const;
    bool floating() const { return floating_; }
    void setFloating(bool floating);
    bool hasAnalysis() const;
    bool analysisStale() const;
    QString analysisReport() const;
    QVariantList analysisFindings() const;
    Q_INVOKABLE bool analyzeSelection();
    Q_INVOKABLE bool highlightFinding(const QString& id,int occurrence=0);
    Q_INVOKABLE bool reviewFinding(const QString& id,const QString& decision,const QString& name);
    Q_INVOKABLE QString findingEvidence(const QString& id) const;
    Q_INVOKABLE void copyAnalysis(bool structured=false);
    Q_INVOKABLE bool begin(music::Editor* source);
    Q_INVOKABLE bool submit(const QVariantList& steps);
    Q_INVOKABLE bool suggestKeys();
    Q_INVOKABLE bool setRegionKey(int region,int fifths,int form);
    Q_INVOKABLE void cancel();
    bool previewActive() const { return previewActive_; }
    bool currentIsPreview() const { return previewActive_ && previewEditor_ && currentEditor()==previewEditor_; }
    bool previewCanKeep() const { return previewActive_ && previewEditor_ && previewInstalled_==previewVersion_ && previewValid_; }
    QVariantList previewSteps() const { return previewSteps_; }
    bool previewAudition() const { return previewAudition_; }
    bool previewRestartPending() const { return autoAuditionPending_; }
    void setPreviewAudition(bool enabled);
    bool previewControlsEnabled() const;
    QString reportStatus() const;
    Q_INVOKABLE bool beginPreview(const QVariantList& steps);
    Q_INVOKABLE bool updatePreview(const QVariantList& steps,bool flush=false);
    Q_INVOKABLE void flushPreview();
    Q_INVOKABLE void resetPreview();
    Q_INVOKABLE bool keepPreview();
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE void setPreviewVisible(bool visible);
    Q_INVOKABLE void retryPreviewReport();
    Q_INVOKABLE void stopPreviewAudition();
    Q_INVOKABLE bool closeResult(int index,bool discard=false);
    Q_INVOKABLE int firstDirty() const;
    Q_INVOKABLE bool anyFileBusy() const;
    Q_INVOKABLE void rememberPresentation() { emit presentationChanging(); }
    void setWorkerCommand(const QString& program,const QStringList& arguments); // Also permits deterministic bridge tests.
    void setTimeout(int milliseconds) { timeout_.setInterval(milliseconds); }
signals:
    void changed();
    void configurationRequested();
    void resultReady();
    void closeConfirmationRequired(int index);
    void resultClosing(music::Editor* editor);
    void presentationChanging();
    void analysisChanged();
    void previewStepsChanged();
private:
    struct ResultTab {
        std::unique_ptr<Editor> editor; QString label; QString operations;
        std::optional<MelodyAnalysis> analysis;
        std::uint64_t analyzedRevision=0;
        Tick analysisOffset=0;
        bool stale=false;
        bool reportPending=false;
        QString reportError;
    };
    enum class Operation { Transform, Keys, Melody, PreviewTransform, PreviewMelody };
    ResultTab* currentTab();
    const ResultTab* currentTab() const;
    void finishResult(TransformResult result,const QString& label,std::optional<MelodyAnalysis> analysis={});
    void attach(Editor* editor);
    void send(const QByteArray& request,const QString& id);
    void request(Operation operation,const std::vector<TransformStep>& steps={});
    void readResponse();
    void fail(const QString& error);
    void retireProcess();
    void abortRequest();
    bool validateSteps(const QVariantList& values,std::vector<TransformStep>* steps=nullptr);
    void installPreview(TransformResult result,std::shared_ptr<const AnalysisSnapshot> snapshot,const QString& reportError);
    void requestPreviewReport();
    void installPreviewReport(MelodyAnalysis analysis,Editor* editor,std::uint64_t revision,Tick offset);
    void stopPreviewAudio();
    void auditionPreview();
    ResultTab* tabFor(Editor* editor);
    QPointer<Editor> sourceEditor_;
    Editor* main_; // Main editor outlives this workspace and owns the shared audio engine.
    std::vector<ResultTab> results_;
    std::shared_ptr<const ScoreFragment> source_;
    TransformContext context_;
    ScoreRange sourceRange_;
    std::uint64_t sourceRevision_=0;
    std::shared_ptr<const AnalysisSnapshot> analysisSource_;
    Operation operation_=Operation::Transform;
    int axis_=60,tonicOctave_=4,current_=-1;
    bool busy_=false,floating_=true,validating_=false;
    std::vector<QVariantList> suggestions_;
    QString status_,activeId_,program_,pendingOperations_;
    QStringList arguments_;
    std::uint64_t sequence_=0;
    std::unique_ptr<QProcess> process_;
    QByteArray output_,stderr_;
    QTimer timeout_;
    QThread processingThread_;
    QObject processor_;
    std::atomic<std::uint64_t> processingGeneration_{0};
    QPointer<Editor> previewEditor_,reportEditor_;
    QVariantList previewSteps_;
    std::shared_ptr<const AnalysisSnapshot> previewSnapshot_;
    std::uint64_t previewVersion_=0,previewInstalled_=0,reportRevision_=0;
    Tick previewStart_=0,previewEnd_=0;
    bool previewActive_=false,previewValid_=false,previewAudition_=true,previewVisible_=false;
    bool autoAuditionPending_=false,stoppingPreview_=false;
    QTimer previewTimer_,reportTimer_;
};
}

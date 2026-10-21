#include "TransformWorkspace.h"
#include <QMetaObject>

namespace music {
TransformWorkspace::ResultTab* TransformWorkspace::tabFor(Editor* editor) {
    for(auto& tab:results_)if(tab.editor.get()==editor)return &tab;
    return nullptr;
}
bool TransformWorkspace::previewControlsEnabled() const {
    return previewActive_ && (!main_->playbackBusy() || (previewEditor_ && previewEditor_->playing()));
}
QString TransformWorkspace::reportStatus() const {
    const auto* tab=currentTab();
    if(!tab)return {};
    if(tab->reportPending)return "Updating report… Previous findings are unavailable until this revision is analyzed.";
    if(!tab->reportError.isEmpty())return "Report unavailable: "+tab->reportError;
    if(analysisStale())return "Out of date — this report describes an earlier preview or score revision.";
    return tab->analysis?QString("Melody analysis · session only"):QString("Waiting for preview analysis…");
}
void TransformWorkspace::abortRequest() {
    ++processingGeneration_;
    timeout_.stop(); activeId_.clear(); busy_=false; analysisSource_.reset();
    retireProcess();
}
bool TransformWorkspace::beginPreview(const QVariantList& values) {
    if(previewActive_)return updatePreview(values,true);
    if(busy_ || !source_ || main_->playbackBusy())return false;
    if(!validateSteps(values))return false;
    // A kept result's scheduled report must not follow a new session's snapshot.
    reportTimer_.stop();
    if(auto* tab=tabFor(reportEditor_);tab && tab->reportPending) {
        tab->reportPending=false; tab->reportError="Superseded by a new preview session.";
    }
    reportEditor_.clear(); previewSnapshot_.reset();
    previewActive_=true;
    return updatePreview(values,true);
}
bool TransformWorkspace::updatePreview(const QVariantList& values,bool flush) {
    if(!previewControlsEnabled())return false;
    if(values==previewSteps_ && (previewCanKeep() || previewTimer_.isActive() || (busy_ && operation_==Operation::PreviewTransform))) {
        if(flush && previewTimer_.isActive())flushPreview();
        return true;
    }
    stopPreviewAudio();
    if(busy_)abortRequest();
    previewTimer_.stop(); reportTimer_.stop();
    previewSteps_=values; ++previewVersion_; previewValid_=false;
    autoAuditionPending_=previewAudition_ && (previewVisible_ || !previewEditor_);
    if(auto* tab=tabFor(previewEditor_)) { tab->reportPending=true; tab->reportError.clear(); }
    const bool valid=validateSteps(values);
    if(valid)status_="Preview adjustment pending…";
    emit previewStepsChanged(); emit changed(); emit analysisChanged();
    if(!valid) {
        if(auto* tab=tabFor(previewEditor_)) { tab->reportPending=false; tab->reportError="Preview settings are invalid; the previous report is out of date."; }
        emit analysisChanged(); return false;
    }
    if(flush)flushPreview(); else previewTimer_.start();
    return true;
}
void TransformWorkspace::flushPreview() {
    previewTimer_.stop();
    if(!previewActive_ || previewSteps_.isEmpty())return;
    if(busy_ && operation_==Operation::PreviewTransform)return;
    std::vector<TransformStep> steps;
    if(!validateSteps(previewSteps_,&steps))return;
    if(busy_)abortRequest();
    request(Operation::PreviewTransform,steps);
}
void TransformWorkspace::installPreview(TransformResult result,std::shared_ptr<const AnalysisSnapshot> snapshot,const QString& reportError) {
    stopPreviewAudio();
    // Another score can start while a transformation is running. Do not replace
    // or audition a shared transport owner; the locked preview itself is safe.
    timeout_.stop(); busy_=false; activeId_.clear(); analysisSource_.reset();
    previewStart_=result.start; previewEnd_=result.end;
    previewInstalled_=previewVersion_; previewValid_=true; previewSnapshot_=std::move(snapshot);
    if(!previewEditor_)finishResult(std::move(result),"Result — "+context_.label);
    else previewEditor_->installScratch(std::move(result.score),result.start,result.end,previewEditor_->title());
    auto* tab=tabFor(previewEditor_);
    if(!tab)return;
    tab->operations=pendingOperations_;
    tab->reportPending=bool(previewSnapshot_); tab->reportError=reportError;
    reportEditor_=previewEditor_; reportRevision_=previewEditor_->revision();
    status_="Preview ready. Keep result to edit or save it; source music is unchanged.";
    if(previewSnapshot_)reportTimer_.start();
    emit changed(); emit analysisChanged();
    auditionPreview();
}
void TransformWorkspace::stopPreviewAudio() {
    if(!previewEditor_ || !previewEditor_->playing())return;
    stoppingPreview_=true; previewEditor_->stop(); stoppingPreview_=false;
}
void TransformWorkspace::auditionPreview() {
    if(stoppingPreview_)return;
    if(main_->playbackBusy() && (!previewEditor_ || !previewEditor_->playing()))autoAuditionPending_=false;
    if(!autoAuditionPending_ || !previewAudition_ || !previewVisible_ || !currentIsPreview() ||
       !previewCanKeep() || main_->playbackBusy() || !previewEditor_->audioReady())return;
    autoAuditionPending_=false;
    previewEditor_->selectAnalysisRange(previewStart_,previewEnd_);
    previewEditor_->play(true);
}
void TransformWorkspace::stopPreviewAudition() {
    autoAuditionPending_=false; stopPreviewAudio(); emit changed();
}
void TransformWorkspace::setPreviewAudition(bool enabled) {
    if(previewAudition_==enabled)return;
    previewAudition_=enabled; autoAuditionPending_=enabled && previewVisible_;
    if(!enabled)stopPreviewAudio();
    emit changed(); auditionPreview();
}
void TransformWorkspace::setPreviewVisible(bool visible) {
    previewVisible_=visible;
    if(!visible) { autoAuditionPending_=false; stopPreviewAudio(); }
}
void TransformWorkspace::resetPreview() {
    if(!previewControlsEnabled())return;
    stopPreviewAudio(); abortRequest(); previewTimer_.stop(); reportTimer_.stop();
    previewSteps_={QVariantMap{{"kind","transpose"},{"amount",0},{"numerator",1},{"denominator",1},
        {"mode","semitones"},{"accidentals","preserve"},{"axisDegree",1},{"tonicOctave",tonicOctave_},{"reverseKeys",false}}};
    ++previewVersion_; previewValid_=false;
    autoAuditionPending_=previewAudition_ && previewVisible_;
    if(auto* tab=tabFor(previewEditor_))tab->reportPending=true;
    validateSteps(previewSteps_);
    busy_=true; operation_=Operation::PreviewTransform; activeId_=QString::number(++sequence_);
    const auto generation=++processingGeneration_;
    const auto id=activeId_;
    status_="Restoring the captured selection…";
    emit previewStepsChanged(); emit changed(); emit analysisChanged();
    QMetaObject::invokeMethod(&processor_,[this,id,generation,source=source_,context=context_] {
        if(generation!=processingGeneration_.load())return;
        QString error,reportError;
        auto result=buildTransformationResult(*source,context,&error);
        std::shared_ptr<const AnalysisSnapshot> snapshot;
        if(result) {
            auto captured=captureAnalysis(result->score,{result->start,result->end,{result->score.staves().front().id}},"Interactive preview",&reportError);
            if(captured)snapshot=std::make_shared<const AnalysisSnapshot>(std::move(*captured));
        }
        QMetaObject::invokeMethod(this,[this,id,result=std::move(result),snapshot,error,reportError]() mutable {
            if(id!=activeId_)return;
            if(!result) { fail(error); return; }
            installPreview(std::move(*result),snapshot,reportError);
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void TransformWorkspace::requestPreviewReport() {
    auto* tab=tabFor(reportEditor_);
    if(!tab || !previewSnapshot_)return;
    if(tab->editor->revision()!=reportRevision_) {
        tab->reportPending=false; tab->reportError="The score changed before analysis started.";
        emit analysisChanged(); return;
    }
    if(previewActive_ && !previewCanKeep())return;
    if(busy_) { reportTimer_.start(); return; }
    analysisSource_=previewSnapshot_;
    request(Operation::PreviewMelody);
    emit analysisChanged();
}
void TransformWorkspace::retryPreviewReport() {
    if(!currentIsPreview() || !previewCanKeep() || busy_)return;
    if(auto* tab=tabFor(previewEditor_)) { tab->reportPending=true; tab->reportError.clear(); }
    requestPreviewReport();
}
void TransformWorkspace::installPreviewReport(MelodyAnalysis analysis,Editor* editor,std::uint64_t revision,Tick offset) {
    auto* tab=tabFor(editor);
    if(!tab || editor->revision()!=revision) {
        if(tab) { tab->reportPending=false; tab->reportError="The score changed before analysis completed."; }
        status_="Report discarded because the score changed.";
    } else {
        tab->analysis=std::move(analysis); tab->analyzedRevision=revision;
        tab->analysisOffset=offset; tab->stale=false; tab->reportPending=false; tab->reportError.clear();
        status_="Preview report ready.";
    }
    if(!previewActive_) { reportEditor_.clear(); previewSnapshot_.reset(); }
    emit changed(); emit analysisChanged();
}
bool TransformWorkspace::keepPreview() {
    if(!previewCanKeep())return false;
    stopPreviewAudio(); autoAuditionPending_=false;
    auto* tab=tabFor(previewEditor_);
    tab->label="Result — "+context_.label;
    previewEditor_->setEditLocked(false);
    previewActive_=false; previewEditor_.clear();
    status_="Result kept as an editable scratch score. Save as MusicXML to retain it.";
    emit changed(); emit analysisChanged();
    return true;
}
void TransformWorkspace::cancelPreview() {
    if(!previewActive_)return;
    stopPreviewAudio(); autoAuditionPending_=false;
    previewTimer_.stop(); reportTimer_.stop(); abortRequest();
    auto* editor=previewEditor_.data();
    previewActive_=false; previewEditor_.clear(); reportEditor_.clear(); previewSnapshot_.reset();
    for(std::size_t i=0;i<results_.size();++i)if(results_[i].editor.get()==editor) { closeResult(int(i),true); break; }
    status_="Preview cancelled. Source music is unchanged.";
    emit changed(); emit analysisChanged();
}
}

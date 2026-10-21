#include "TransformWorkspace.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QGuiApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>

namespace music {
TransformWorkspace::TransformWorkspace(Editor* main,QObject* parent):QObject(parent),main_(main) {
    attach(main);
    processor_.moveToThread(&processingThread_);
    processingThread_.start();
    timeout_.setSingleShot(true);
    timeout_.setInterval(60'000);
    connect(&timeout_,&QTimer::timeout,this,[this] { fail("Worker operation timed out. You can retry."); });
    program_=qEnvironmentVariable("MUSIC_PYTHON");
    // poetry run supplies its environment through PATH; do not select a local .venv.
    if(program_.isEmpty())program_=QStringLiteral("python3");
    arguments_={"-u",QCoreApplication::applicationDirPath()+"/python/worker.py"};
    previewTimer_.setSingleShot(true); previewTimer_.setInterval(150);
    reportTimer_.setSingleShot(true); reportTimer_.setInterval(500);
    connect(&previewTimer_,&QTimer::timeout,this,&TransformWorkspace::flushPreview);
    connect(&reportTimer_,&QTimer::timeout,this,&TransformWorkspace::requestPreviewReport);
    connect(main_,&Editor::stateChanged,this,&TransformWorkspace::auditionPreview,Qt::QueuedConnection);

}
TransformWorkspace::~TransformWorkspace() {
    main_->disconnect(this);
    for(auto& tab:results_)tab.editor->disconnect(this);
    autoAuditionPending_=false;
    stopPreviewAudio();
    ++processingGeneration_;
    previewTimer_.stop(); reportTimer_.stop();
    timeout_.stop();
    if(process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000); // Shutdown only; cancellation and normal requests never wait.
    }
    processingThread_.quit();
    processingThread_.wait();
}
void TransformWorkspace::attach(Editor* editor) {
    connect(editor,&Editor::transformationRequested,this,[this,editor] { begin(editor); });
    if(!editor->scratch())return;
    auto* pointer=editor;
    connect(pointer,&Editor::stateChanged,this,[this,pointer] {
        // A revision comparison is constant work; never rescan notes on ordinary edits.
        for(auto& tab:results_)if(tab.editor.get()==pointer && tab.analysis && !tab.stale && pointer->revision()!=tab.analyzedRevision) {
            tab.stale=true; emit analysisChanged(); break;
        }
        emit changed();
    });
    connect(editor,&Editor::stopRequested,this,[this,editor] {
        if(editor==previewEditor_ && !stoppingPreview_)autoAuditionPending_=false;
    });
}
QVariantList TransformWorkspace::tabs() const {
    QVariantList list;
    for(const auto& tab:results_)list.append(QVariantMap{{"title",tab.editor->dirty()?QString("● ")+tab.label:tab.label}});
    return list;
}
QString TransformWorkspace::currentOperations() const { return current_>=0 && current_<int(results_.size())?results_[current_].operations:QString{}; }
Editor* TransformWorkspace::currentEditor() const { return current_>=0 && current_<int(results_.size())?results_[current_].editor.get():nullptr; }
void TransformWorkspace::setCurrentIndex(int index) { if(index>=0 && index<int(results_.size()) && current_!=index) { emit presentationChanging(); current_=index; emit changed(); emit analysisChanged(); } }
void TransformWorkspace::setFloating(bool floating) { if(floating_!=floating) { emit presentationChanging(); floating_=floating; emit changed(); } }
bool TransformWorkspace::begin(Editor* source) {
    if(!source || busy_ || previewActive_ || source->editLocked() || source->playbackBusy() || source->fileBusy())return false;
    const auto range=source->selectedRange();
    if(range.staves.size()!=1) { status_="Select music on exactly one staff."; emit changed(); return false; }
    auto fragment=source->score().copyRange(range);
    const auto staff=source->score().staffIndex(range.staves.front());
    if(!fragment || !staff) { status_="The selected passage cannot be transformed."; emit changed(); return false; }
    sourceEditor_=source;
    sourceRange_=range; sourceRevision_=source->revision();
    source_=std::make_shared<const ScoreFragment>(std::move(*fragment));
    const auto measure=source->score().measureAt(range.start);
    context_={source->score().bar(measure).meter,source->score().tempoAt(measure),source->score().clefAt(*staff,range.start),
              source->score().program(*staff),range.start-source->score().measureStart(measure),
              QString("%1 · staff %2 · measures %3–%4").arg(source->title()).arg(*staff+1).arg(measure+1).arg(source->score().measureAt(range.end-1)+1)};
    context_.keys={{0,source->score().keyAt(*staff,range.start)}};
    const auto& keys=source->score().staves()[*staff].keys;
    auto key=std::upper_bound(keys.begin(),keys.end(),range.start,[](Tick t,const KeyChange& k){return t<k.tick;});
    for(;key!=keys.end() && key->tick<range.end;++key)context_.keys.push_back({key->tick-range.start,key->context});
    suggestions_.assign(context_.keys.size(),{});
    axis_=60;
    tonicOctave_=4;
    for(const auto& e:source_->rows.front())if(!e.pitches.empty()) {
        const auto first=std::min_element(e.pitches.begin(),e.pitches.end(),[](const Pitch& a,const Pitch& b){return a.midi()<b.midi();});
        axis_=first->midi(); tonicOctave_=first->octave;
        break;
    }
    status_.clear();
    emit configurationRequested();
    emit changed();
    return true;
}
QVariantList TransformWorkspace::keyRegions() const {
    QVariantList result;
    for(std::size_t i=0;i<context_.keys.size();++i) {
        const auto& k=context_.keys[i];
        result.append(QVariantMap{{"tick",QString::number(k.tick)},{"position",QString("+%1 quarter notes").arg(double(k.tick)/quarter,0,'g',6)},
            {"fifths",k.context.fifths.value_or(0)},
            {"form",!k.context.tonality || k.context.tonality->scale==ScaleIdentity::Major?0:1+int(k.context.tonality->minor)},
            {"known",bool(k.context.tonality)},{"label",QString::fromStdString(k.context.label())},
            {"suggestions",i<suggestions_.size()?suggestions_[i]:QVariantList{}}});
    }
    return result;
}
bool TransformWorkspace::keysReady() const {
    return std::all_of(context_.keys.begin(),context_.keys.end(),[](const KeyChange& k){return bool(k.context.tonality);});
}
bool TransformWorkspace::setRegionKey(int region,int fifths,int form) {
    if((busy_ && !previewActive_) || (previewActive_ && !previewControlsEnabled()) || region<0 || region>=int(context_.keys.size()) || fifths< -7 || fifths>7 || form<0 || form>3)return false;
    context_.keys[region].context=KeyContext::fromSignature(fifths,form?ScaleIdentity::Minor:ScaleIdentity::Major,form?MinorForm(form-1):MinorForm::Melodic);
    if(previewActive_) {
        previewTimer_.stop();
        if(busy_)abortRequest();
        previewValid_=false; // Same step values now have a different tonal context.
        return updatePreview(previewSteps_,true);
    }
    status_="Key confirmed for this transformation. Source music is unchanged.";
    emit changed();
    return true;
}
bool TransformWorkspace::suggestKeys() {
    if(busy_ || previewActive_ || !source_ || main_->playbackBusy())return false;
    request(Operation::Keys);
    return true;
}
void TransformWorkspace::setWorkerCommand(const QString& program,const QStringList& arguments) {
    cancel(); retireProcess(); program_=program; arguments_=arguments;
}
TransformWorkspace::ResultTab* TransformWorkspace::currentTab() { return current_>=0 && current_<int(results_.size())?&results_[current_]:nullptr; }
const TransformWorkspace::ResultTab* TransformWorkspace::currentTab() const { return current_>=0 && current_<int(results_.size())?&results_[current_]:nullptr; }
bool TransformWorkspace::hasAnalysis() const { const auto* tab=currentTab(); return tab && tab->analysis.has_value(); }
bool TransformWorkspace::analysisStale() const { const auto* tab=currentTab(); return tab && tab->analysis && (tab->editor->revision()!=tab->analyzedRevision || tab->reportPending || (currentIsPreview() && !previewCanKeep())); }
QString TransformWorkspace::analysisReport() const {
    const auto* tab=currentTab();
    return tab && tab->analysis?(analysisStale()?QString("OUT OF DATE — this report describes an earlier score revision.\n\n"):QString{})+tab->analysis->report:QString{};
}
QVariantList TransformWorkspace::analysisFindings() const {
    QVariantList result;
    const auto* tab=currentTab(); if(!tab || !tab->analysis)return result;
    std::vector<const AnalysisFinding*> ordered;
    for(const auto& f:tab->analysis->findings)ordered.push_back(&f);
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b) {
        auto rank=[](const auto* f) { return f->kind=="motif"?0:f->kind=="gesture"?1:2; };
        if(rank(a)!=rank(b))return rank(a)<rank(b);
        if(a->salience!=b->salience)return a->salience>b->salience;
        return a->spans.front().start<b->spans.front().start;
    });
    for(const auto* entry:ordered) {
        const auto& f=*entry;
        result.append(QVariantMap{{"id",f.id},{"kind",f.kind},{"title",f.title},
        {"text",f.text},{"strength",f.strength},{"occurrences",int(f.spans.size())},{"decision",f.review},{"name",f.name},
        {"salience",f.salience},{"salienceReasons",f.salienceReasons},
        {"reviewable",f.kind=="motif" || f.kind=="cell" || f.kind=="gesture" || f.kind=="boundary"}});
    }
    return result;
}
bool TransformWorkspace::analyzeSelection() {
    if(busy_ || previewActive_ || !sourceEditor_ || main_->playbackBusy() || sourceEditor_->fileBusy())return false;
    if(sourceEditor_->revision()!=sourceRevision_) { status_="The source changed. Close this dialog and select the passage again."; emit changed(); return false; }
    Tick at=sourceRange_.start;
    for(const auto& e:source_->rows.front()) {
        if(e.pitches.size()>1) {
            status_=QString("Analysis requires one monophonic melody. Stacked notes occur in measure %1; select a passage with one note at a time.").arg(sourceEditor_->score().measureAt(at)+1);
            emit changed(); return false;
        }
        at+=e.ticks;
    }
    auto snapshot=captureAnalysis(sourceEditor_->score(),sourceRange_,context_.label,&status_);
    if(!snapshot) { emit changed(); return false; }
    analysisSource_=std::make_shared<const AnalysisSnapshot>(std::move(*snapshot));
    request(Operation::Melody);
    return true;
}
bool TransformWorkspace::highlightFinding(const QString& id,int occurrence) {
    auto* tab=currentTab(); if(!tab || !tab->analysis || analysisStale())return false;
    for(const auto& f:tab->analysis->findings)if(f.id==id && occurrence>=0 && occurrence<int(f.spans.size()))
        return tab->editor->selectAnalysisRange(tab->analysisOffset+f.spans[occurrence].start,tab->analysisOffset+f.spans[occurrence].end);
    return false;
}
bool TransformWorkspace::reviewFinding(const QString& id,const QString& decision,const QString& name) {
    auto* tab=currentTab();
    if(!tab || !tab->analysis || analysisStale() || tab->editor->playbackBusy() || name.size()>120 ||
       (decision!="undecided" && decision!="accepted" && decision!="rejected"))return false;
    for(auto& f:tab->analysis->findings)if(f.id==id && (f.kind=="motif" || f.kind=="cell" || f.kind=="gesture" || f.kind=="boundary")) {
        f.review=decision; if(f.kind!="boundary")f.name=name.trimmed();
        tab->analysis->updateReport(); emit analysisChanged(); return true;
    }
    return false;
}
QString TransformWorkspace::findingEvidence(const QString& id) const {
    const auto* tab=currentTab(); if(!tab || !tab->analysis)return {};
    for(const auto& value:tab->analysis->data["findings"].toArray()) {
        const auto object=value.toObject();
        if(object["id"]==id)return QString::fromUtf8(QJsonDocument(object["evidence"].toObject()).toJson(QJsonDocument::Indented));
    }
    return {};
}
void TransformWorkspace::copyAnalysis(bool structured) {
    const auto* tab=currentTab(); if(!tab || !tab->analysis)return;
    if(!structured) { QGuiApplication::clipboard()->setText(analysisReport()); return; }
    auto object=tab->analysis->structured(); object["stale"]=analysisStale();
    // A large diagnostic JSON export must not stall the score's UI thread.
    QMetaObject::invokeMethod(&processor_,[this,object=std::move(object)] {
        const auto text=QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
        QMetaObject::invokeMethod(this,[text] { QGuiApplication::clipboard()->setText(text); },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void TransformWorkspace::finishResult(TransformResult result,const QString& label,std::optional<MelodyAnalysis> analysis) {
    timeout_.stop(); busy_=false; activeId_.clear();
    analysisSource_.reset();
    auto editor=std::make_unique<Editor>(main_->audioEngine(),true);
    editor->installScratch(std::move(result.score),result.start,result.end,label);
    attach(editor.get());
    const auto revision=editor->revision();
    emit presentationChanging();
    const auto operations=analysis?QString("Deterministic melody analysis · review findings or copy the report"):pendingOperations_;
    results_.push_back({std::move(editor),label,operations,std::move(analysis),revision,result.start,false,false,{}});
    current_=int(results_.size())-1;
    if(operation_==Operation::PreviewTransform) {
        results_.back().label="Preview — "+context_.label;
        previewEditor_=currentEditor();
        previewEditor_->setEditLocked(true);
    }
    status_="Result ready. Source music is unchanged.";
    emit changed(); emit analysisChanged(); emit resultReady();
}
bool TransformWorkspace::submit(const QVariantList& values) {
    if(busy_ || previewActive_ || !source_ || main_->playbackBusy())return false;
    std::vector<TransformStep> parsed;
    if(!validateSteps(values,&parsed))return false;
    request(Operation::Transform,parsed);
    return true;
}
bool TransformWorkspace::validateSteps(const QVariantList& values,std::vector<TransformStep>* parsed) {
    const auto steps=transformSteps(values,&status_);
    if(!steps) { emit changed(); return false; }
    if(!keysReady() && std::any_of(steps->begin(),steps->end(),[&](const TransformStep& s) {
        return ((s.kind==TransformKind::Transpose || s.kind==TransformKind::Invert) && s.mode==PitchMode::Degrees) ||
            (s.kind==TransformKind::Retrograde && (context_.keys.size()>1 || s.accidentals==AccidentalPolicy::Scale));
    })) { status_="Suggest or choose and confirm a key for each region first."; emit changed(); return false; }
    QStringList descriptions;
    for(const auto& step:*steps) {
        switch(step.kind) {
            case TransformKind::Transpose:descriptions<<QString("Transpose %1 %2").arg(step.amount).arg(step.mode==PitchMode::Degrees?"scale steps":"semitones");break;
            case TransformKind::Invert:descriptions<<(step.mode==PitchMode::Degrees?QString("Diatonic inversion · degree %1, tonic octave %2").arg(step.axisDegree).arg(step.tonicOctave):QString("Invert around MIDI %1").arg(step.amount));break;
            case TransformKind::Scale:descriptions<<QString("Rhythm ×%1/%2").arg(step.numerator).arg(step.denominator);break;
            case TransformKind::Retrograde:descriptions<<(step.reverseKeys?"Retrograde · reverse key regions":"Retrograde · fixed key regions");break;
        }
        if(step.accidentals==AccidentalPolicy::Scale)descriptions<<"Constrain to scale";
    }
    pendingOperations_=descriptions.join(" → ");
    if(parsed)*parsed=*steps;
    return true;
}
void TransformWorkspace::request(Operation operation,const std::vector<TransformStep>& steps) {
    operation_=operation;
    busy_=true;
    activeId_=QString::number(++sequence_);
    const auto generation=++processingGeneration_;
    const auto id=activeId_;
    status_=(operation==Operation::Melody || operation==Operation::PreviewMelody)?"Analyzing melody…":operation==Operation::Keys?"Suggesting keys with Python/music21…":"Transforming with Python/music21…";
    timeout_.start();
    emit changed();
    QMetaObject::invokeMethod(&processor_,[this,id,generation,source=source_,keys=context_.keys,steps,operation,snapshot=analysisSource_] {
        if(generation!=processingGeneration_.load())return;
        auto bytes=(operation==Operation::Melody || operation==Operation::PreviewMelody)?melodyAnalysisRequest(*snapshot,id):operation==Operation::Keys?keyAnalysisRequest(*source,keys,id):transformationRequest(*source,steps,id,keys);
        QMetaObject::invokeMethod(this,[this,id,bytes=std::move(bytes)] {
            if(id!=activeId_)return;
            if(bytes.isEmpty())fail("The selected passage exceeds the worker message limit.");
            else send(bytes,id);
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void TransformWorkspace::send(const QByteArray& request,const QString& id) {
    if(id!=activeId_)return;
    output_.clear(); stderr_.clear(); validating_=false;
    if(!process_) {
        process_=std::make_unique<QProcess>();
        auto* process=process_.get();
        connect(process,&QProcess::readyReadStandardOutput,this,&TransformWorkspace::readResponse);
        connect(process,&QProcess::readyReadStandardError,this,[this] {
            const auto chunk=process_->readAllStandardError();
            if(stderr_.size()<4096)stderr_+=chunk.left(4096-stderr_.size());
        });
        connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError) {
            if(busy_)fail("Could not run the Python worker: "+process_->errorString()+". Set MUSIC_PYTHON to your Python environment.");
        });
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus) {
            if(busy_)fail("The Python worker exited before returning a result. "+QString::fromUtf8(stderr_));
            else retireProcess();
        });
        process->start(program_,arguments_);
    }
    // QProcess buffers input while the process is starting.
    if(process_->write(request)<0)fail("Could not send the selected passage to Python.");
}
void TransformWorkspace::readResponse() {
    const auto chunk=process_->readAllStandardOutput();
    if(!busy_) { if(!chunk.isEmpty())retireProcess(); return; }
    if(validating_ && !chunk.isEmpty()) { fail("The worker returned unexpected extra output."); return; }
    output_+=chunk;
    if(output_.size()>transformMessageLimit) { fail("The worker response exceeded the message limit."); return; }
    const auto newline=output_.indexOf('\n');
    if(newline<0)return;
    if(newline!=output_.size()-1) { fail("The worker returned unexpected extra output."); return; }
    validating_=true;
    auto bytes=std::move(output_);
    const auto id=activeId_;
    const auto context=context_;
    const auto operation=operation_;
    const auto target=reportEditor_;
    const auto revision=reportRevision_;
    const auto generation=processingGeneration_.load();
    QMetaObject::invokeMethod(&processor_,[this,id,generation,context,operation,target,revision,snapshot=analysisSource_,bytes=std::move(bytes)] {
        if(generation!=processingGeneration_.load())return;
        QString error;
        if(operation==Operation::Melody || operation==Operation::PreviewMelody) {
            auto analysis=melodyAnalysisResponse(bytes,id,*snapshot,&error);
            if(operation==Operation::PreviewMelody) {
                QMetaObject::invokeMethod(this,[this,id,target,revision,snapshot,analysis=std::move(analysis),error]() mutable {
                    if(id!=activeId_)return;
                    if(!analysis) { fail(error); return; }
                    timeout_.stop(); busy_=false; activeId_.clear(); analysisSource_.reset();
                    if(target)installPreviewReport(std::move(*analysis),target,revision,snapshot->offset);
                },Qt::QueuedConnection);
                return;
            }
            auto score=analysis?analysisScratch(*snapshot,&error):std::nullopt;
            QMetaObject::invokeMethod(this,[this,id,snapshot,analysis=std::move(analysis),score=std::move(score),error]() mutable {
                if(id!=activeId_)return;
                if(!analysis || !score) { fail(error); return; }
                finishResult(std::move(*score),QString("Analysis %1 — %2").arg(sequence_).arg(snapshot->label),std::move(analysis));
            },Qt::QueuedConnection);
            return;
        }
        if(operation==Operation::Keys) {
            const auto suggestions=keyAnalysisResponse(bytes,id,context.keys,&error);
            QMetaObject::invokeMethod(this,[this,id,suggestions,error] {
                if(id!=activeId_)return;
                if(!suggestions) { fail(error); return; }
                timeout_.stop(); busy_=false; activeId_.clear();
                suggestions_.assign(context_.keys.size(),{});
                for(const auto& v:*suggestions) {
                    const auto region=v.toMap();
                    suggestions_[region.value("region").toInt()]=region.value("candidates").toList();
                }
                status_="Choose a suggestion and click Use key, or choose a key manually. Rest-only regions need a manual choice.";
                emit changed();
            },Qt::QueuedConnection);
            return;
        }
        auto result=transformationResponse(bytes,id,context,&error);
        QString reportError;
        std::shared_ptr<const AnalysisSnapshot> previewSnapshot;
        if(result && operation==Operation::PreviewTransform) {
            auto captured=captureAnalysis(result->score,{result->start,result->end,{result->score.staves().front().id}},"Interactive preview",&reportError);
            if(captured)previewSnapshot=std::make_shared<const AnalysisSnapshot>(std::move(*captured));
        }
        QMetaObject::invokeMethod(this,[this,id,context,operation,previewSnapshot,reportError,result=std::move(result),error]() mutable {
            if(id!=activeId_)return;
            if(!result) { fail(error); return; }
            if(operation==Operation::PreviewTransform) { installPreview(std::move(*result),previewSnapshot,reportError); return; }
            finishResult(std::move(*result),QString("Result %1 — %2").arg(sequence_).arg(context.label));
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void TransformWorkspace::retireProcess() {
    if(!process_)return;
    auto* process=process_.release();
    process->disconnect(this);
    process->kill();
    // Delete after the OS has reaped the child; no synchronous process wait on the UI thread.
    if(process->state()==QProcess::NotRunning)process->deleteLater();
    else connect(process,&QProcess::stateChanged,process,[process](QProcess::ProcessState state) {
        if(state==QProcess::NotRunning)process->deleteLater();
    });
}
void TransformWorkspace::fail(const QString& error) {
    ++processingGeneration_;
    if(operation_==Operation::PreviewMelody) {
        if(auto* tab=tabFor(reportEditor_)) { tab->reportPending=false; tab->reportError=error; }
    }
    if(operation_==Operation::PreviewTransform) {
        previewValid_=false;
        if(auto* tab=tabFor(previewEditor_)) { tab->reportPending=false; tab->reportError="Preview adjustment failed; the previous report is out of date."; }
    }
    timeout_.stop(); busy_=false; activeId_.clear(); status_=error;
    analysisSource_.reset();
    emit analysisChanged();
    retireProcess(); emit changed();
}
void TransformWorkspace::cancel() {
    if(previewActive_) { cancelPreview(); return; }
    if(!busy_)return;
    fail(operation_==Operation::Melody?"Analysis cancelled.":"Transformation cancelled.");
}
bool TransformWorkspace::closeResult(int index,bool discard) {
    if(index<0 || index>=int(results_.size()))return false;
    auto* editor=results_[index].editor.get();
    if(editor->fileBusy())return false;
    if(editor==previewEditor_ && previewActive_) { cancelPreview(); return true; }
    if(editor->dirty() && !discard) { emit closeConfirmationRequired(index); return false; }
    if(editor==reportEditor_) {
        if(busy_ && operation_==Operation::PreviewMelody)abortRequest();
        reportTimer_.stop(); reportEditor_.clear(); previewSnapshot_.reset();
    }
    emit presentationChanging();
    emit resultClosing(editor);
    auto removed=std::move(results_[index].editor);
    results_.erase(results_.begin()+index);
    if(current_>index)--current_;
    current_=std::min(current_,int(results_.size())-1);
    emit changed(); emit analysisChanged();
    return true;
}
bool TransformWorkspace::anyFileBusy() const {
    return std::any_of(results_.begin(),results_.end(),[](const ResultTab& tab){return tab.editor->fileBusy();});
}
int TransformWorkspace::firstDirty() const {
    for(std::size_t i=0;i<results_.size();++i)if(results_[i].editor->dirty())return int(i);
    return -1;
}
}

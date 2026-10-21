#include "Editor.h"
#include "TransformWorkspace.h"
#include "Instruments.h"
#include "MusicXml.h"
#include "ScoreClipboard.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QFileInfo>
#include <QMetaObject>
#include <Qt>
#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
namespace music {
    namespace {
        Pitch pitchFromMidi(int midi,bool sharp) {
            constexpr std::array<int,12> sharpStep {
                0,0,1,1,2,3,3,4,4,5,5,6
            },sharpAlter {
                0,1,0,1,0,0,1,0,1,0,1,0
            },flatStep {
                0,1,1,2,2,3,4,4,5,5,6,6
            },flatAlter {
                0,-1,0,-1,0,0,-1,0,-1,0,-1,0
            };
            return Pitch {
                sharp?sharpStep[midi%12]:flatStep[midi%12],midi/12-1,sharp?sharpAlter[midi%12]:flatAlter[midi%12]
            };
        }
        Pitch clefAnchor(Clef c) {
            switch(c) {
                case Clef::Bass:return {
                    1,3,0
                };
                case Clef::Alto:return {
                    0,4,0
                };
                case Clef::Tenor:return {
                    5,3,0
                };
                case Clef::Treble8va:return {
                    6,5,0
                };
                case Clef::Bass8vb:return {
                    1,2,0
                };
                default:return {
                    6,4,0
                };
            }
        }
    }
    Editor::Editor(QObject* parent):Editor(std::make_shared<Playback>(),false,parent) {}
    Editor::Editor(std::shared_ptr<Playback> playback,bool scratch,QObject* parent)
        :QObject(parent),playback_(std::move(playback)),scratch_(scratch) {
        layout_.reset(score_);
        selected_=score_.firstEventId(0);
        updateSelectionGeometry();
        playheadTimer_.setInterval(33);
        connect(&playheadTimer_,&QTimer::timeout,this,&Editor::updatePlayhead);
        connect(playback_.get(),&Playback::finished,this,[this] { if(playing())stop(); });
        connect(playback_.get(),&Playback::stateChanged,this,[this] {
            if(!playing())playheadTimer_.stop();
            emit stateChanged();
            emit playheadChanged();
        });
        connect(playback_.get(),&Playback::volumeChanged,this,&Editor::volumeChanged);
        connect(playback_.get(),&Playback::readyChanged,this,[this] {
            if(!playback_->isReady())setStatus(playback_->errorMessage());
            else emit stateChanged();
        });
        status_="Use A–G to enter notes; Shift+A–G adds chord tones. S staff · M meter · T transform/analyze · Shift+T tempo.";
    }
    QObject* Editor::transformations() {
        if(!transformations_)transformations_=std::make_unique<TransformWorkspace>(this);
        return transformations_.get();
    }
    Editor::~Editor() {
        if(playback_->owner()==this)playback_->stop();
        if(fileWorker_.joinable())fileWorker_.join();
    }
    void Editor::installScratch(Score score,Tick start,Tick end,const QString& label) {
        score_=std::move(score);
        scratchLabel_=label;
        history_.clear();
        path_.clear();
        dirty_=false;
        ++revision_;
        selectedStaff_=0;
        selected_=score_.eventAt(0,start);
        selectedMeasure_=score_.measureAt(start);
        activeTone_=0;
        measureSelection_=false;
        clefEditTick_.reset(); keySelection_=false;
        range_=ScoreRange{start,end,{score_.staves().front().id}};
        rangeAnchorStart_=start;
        rangeAnchorEnd_=end;
        layout_.reset(score_);
        updateSelectionGeometry();
        emit scoreReset();
        setStatus(editLocked_?"Interactive preview · use controls, then Keep result to edit or save.":"Temporary result · edit, select and copy, or explicitly Save as MusicXML.");
    }
    void Editor::setEditLocked(bool locked) {
        editLocked_=locked;
        setStatus(locked?"Interactive preview · use controls, then Keep result to edit or save.":"Temporary result · edit, select and copy, or explicitly Save as MusicXML.");
    }
    QString Editor::title() const {
        return path_.isEmpty()?(scratch_?scratchLabel_:QString("Untitled")):QFileInfo(path_).fileName();
    }
    void Editor::setStatus(QString message) {
        status_=std::move(message);
        emit stateChanged();
    }
    bool Editor::editable() {
        if(editLocked_) { setStatus("Use the preview controls, or Keep result before editing."); return false; }
        if(playbackBusy() || fileBusy_) {
            setStatus(playbackBusy()?"Stop playback before editing.":"Wait for the file operation to finish.");
            return false;
        }
        return true;
    }
    void Editor::setAuditionEnabled(bool enabled) {
        audition_=enabled;
        if(!enabled)playback_->cancelPreview();
        emit stateChanged();
    }
    void Editor::setVolume(int percent) {
        playback_->setVolume(percent);
    }
    ScoreRange Editor::selectedRange() const {
        if(range_)return *range_;
        if(measureSelection_)return {score_.measureStart(selectedMeasure_),score_.measureStart(selectedMeasure_+1),{score_.staves()[selectedStaff_].id}};
        const auto* e=score_.event(selected_);
        if(!e)return {};
        const auto start=score_.eventStart(selected_);
        return {start,start+e->ticks,{score_.staves()[selectedStaff_].id}};
    }
    bool Editor::selectAnalysisRange(Tick start,Tick end) {
        if(!scratch_ || playbackBusy() || fileBusy_ || start<0 || end<=start || end>score_.totalTicks())return false;
        selectedStaff_=0;
        selected_=score_.eventAt(0,start);
        selectedMeasure_=score_.measureAt(start);
        range_=ScoreRange{start,end,{score_.staves().front().id}};
        rangeAnchorStart_=start; rangeAnchorEnd_=end;
        measureSelection_=false; clefEditTick_.reset(); keySelection_=false; activeTone_=0;
        updateSelectionGeometry();
        emit revealSelection();
        return true;
    }
    std::vector<QRectF> Editor::rangeRects(std::size_t system) const {
        std::vector<QRectF> rects;
        if(!range_ || system>=layout_.systemCount())return rects;
        const auto& placement=layout_.system(system);
        const Tick begin=std::max(range_->start,score_.measureStart(placement.first));
        const Tick end=std::min(range_->end,score_.measureStart(placement.first+placement.count));
        if(begin>=end)return rects;
        const auto first=score_.measureAt(begin),last=score_.measureAt(end-1);
        const qreal left=layout_.eventX(first,begin-score_.measureStart(first));
        const qreal right=end==score_.measureStart(last+1)?layout_.measureRect(last,0).right():layout_.eventX(last,end-score_.measureStart(last));
        for(auto id:range_->staves)if(const auto staff=score_.staffIndex(id)) {
            const auto measure=layout_.measureRect(first,*staff);
            rects.emplace_back(left,system*layout_.systemHeight()+measure.y()+16,std::max(qreal(2),right-left),80);
        }
        return rects;
    }
    bool Editor::copySelection() {
        if(playbackBusy() || fileBusy_)return false;
        if(clefEditTick_) { setStatus("Select notes, rests, or measures to copy."); return false; }
        const auto fragment=score_.copyRange(selectedRange());
        const auto bytes=fragment?encodeScoreFragment(*fragment):QByteArray{};
        if(bytes.isEmpty()) { setStatus("The selection cannot be copied or is too large for the clipboard."); return false; }
        auto mime=std::make_unique<QMimeData>();
        mime->setData(scoreFragmentMime,bytes);
        QGuiApplication::clipboard()->setMimeData(mime.release());
        setStatus(QString("Copied a continuous passage from %1 staff/staves").arg(fragment->rows.size()));
        return true;
    }
    bool Editor::pasteSelection() {
        if(!editable())return false;
        if(clefEditTick_) { setStatus("Select a note, rest, or measure as the paste location."); return false; }
        const auto* mime=QGuiApplication::clipboard()->mimeData();
        const auto fragment=mime && mime->hasFormat(scoreFragmentMime)?decodeScoreFragment(mime->data(scoreFragmentMime)):std::nullopt;
        if(!fragment) { setStatus("The clipboard does not contain a supported musical passage."); return false; }
        auto destination=selectedRange();
        std::string error;
        auto change=score_.pasteRange(*fragment,destination,&error);
        if(!change) { setStatus(QString::fromStdString(error)); return false; }
        finishLocal(std::move(*change));
        destination.end=destination.start+fragment->duration;
        range_=destination;
        std::sort(range_->staves.begin(),range_->staves.end(),[&](StaffId a,StaffId b){return score_.staffIndex(a)<score_.staffIndex(b);});
        selectedStaff_=*score_.staffIndex(range_->staves.front());
        const auto first=score_.eventAt(selectedStaff_,destination.start);
        rangeAnchorStart_=destination.start;
        rangeAnchorEnd_=destination.start+score_.event(first)->ticks;
        selected_=score_.eventAt(selectedStaff_,destination.end-1);
        selectedMeasure_=score_.locate(selected_)->measure;
        activeTone_=0;
        nextEntry_=0;
        measureSelection_=false;
        repairSelection();
        setStatus(QString("Pasted onto %1 staff/staves; existing music was overwritten").arg(fragment->rows.size()));
        return true;
    }
    void Editor::extendSelectionTo(EventId id) {
        const auto loc=score_.locate(id);
        if(!loc)return;
        if(!range_) {
            range_=selectedRange();
            rangeAnchorStart_=range_->start;
            rangeAnchorEnd_=range_->end;
        }
        const auto staff=score_.staves()[loc->staff].id;
        if(std::find(range_->staves.begin(),range_->staves.end(),staff)==range_->staves.end())range_->staves.push_back(staff);
        const auto start=score_.eventStart(id);
        range_->start=std::min(rangeAnchorStart_,start);
        range_->end=std::max(rangeAnchorEnd_,start+score_.event(id)->ticks);
        selected_=id;
        selectedStaff_=loc->staff;
        selectedMeasure_=loc->measure;
        activeTone_=0;
        nextEntry_=0;
        measureSelection_=false;
        clefEditTick_.reset(); keySelection_=false;
        updateSelectionGeometry();
        emit revealSelection();
    }
    bool Editor::extendSelection(bool right) {
        if(playbackBusy() || fileBusy_)return false;
        EventId from=selected_;
        if(!range_ || measureSelection_) {
            const auto selection=selectedRange();
            from=score_.eventAt(selectedStaff_,right?selection.end-1:selection.start);
        }
        const auto next=right?score_.nextEventId(from):score_.previousEventId(from);
        if(next)extendSelectionTo(next);
        return true;
    }
    void Editor::toggleStaff(std::size_t staff) {
        if(playbackBusy() || fileBusy_ || staff>=score_.staffCount())return;
        if(!range_) {
            range_=selectedRange();
            rangeAnchorStart_=range_->start;
            rangeAnchorEnd_=range_->end;
        }
        const auto id=score_.staves()[staff].id;
        const auto found=std::find(range_->staves.begin(),range_->staves.end(),id);
        if(found==range_->staves.end())range_->staves.push_back(id);
        else range_->staves.erase(found);
        if(range_->staves.empty())range_.reset();
        else {
            std::sort(range_->staves.begin(),range_->staves.end(),[&](StaffId a,StaffId b){return score_.staffIndex(a)<score_.staffIndex(b);});
            if(std::find(range_->staves.begin(),range_->staves.end(),score_.staves()[selectedStaff_].id)==range_->staves.end()) {
                selectedStaff_=*score_.staffIndex(range_->staves.front());
                selected_=score_.eventAt(selectedStaff_,range_->start);
                selectedMeasure_=score_.locate(selected_)->measure;
                activeTone_=0;
            }
        }
        nextEntry_=0;
        clefEditTick_.reset(); keySelection_=false;
        updateSelectionGeometry();
    }
    QStringList Editor::instruments() const {
        return instrumentNames();
    }
    QStringList Editor::clefs() const {
        return clefNames();
    }
    int Editor::selectedClef() const {
        return static_cast<int>(score_.clefAt(selectedStaff_,clefEditTick()));
    }
    QVariantMap Editor::selectedKey() const {
        const auto tick=clefEditTick();
        const auto& key=score_.keyAt(selectedStaff_,tick);
        const auto& keys=score_.staves()[selectedStaff_].keys;
        return {{"fifths",key.fifths.value_or(0)},
                {"form",!key.tonality || key.tonality->scale==ScaleIdentity::Major?0:1+int(key.tonality->minor)},
                {"declared",bool(key.tonality)},{"label",QString::fromStdString(key.label())},
                {"removable",tick>0 && std::any_of(keys.begin(),keys.end(),[&](const KeyChange& k){return k.tick==tick;})}};
    }
    QStringList Editor::keyChoices(int form) const {
        QStringList result;
        if(form<0 || form>3)return result;
        for(int f=-7;f<=7;++f)result<<QString::fromStdString(KeyContext::fromSignature(f,form?ScaleIdentity::Minor:ScaleIdentity::Major,form?MinorForm(form-1):MinorForm::Melodic).label());
        return result;
    }
    void Editor::finishKey(KeyEdit change) {
        const auto first=layout_.invalidate(score_,change.measures);
        history_.record(std::move(change));
        dirty_=true; ++revision_; nextEntry_=0;
        repairSelection();
        emit layoutInvalidated(first);
        setStatus("Key context changed; existing sounding pitches retained.");
    }
    bool Editor::applyKey(int fifths,int form) {
        if(!editable() || range_ || fifths< -7 || fifths>7 || form<0 || form>3)return false;
        auto change=score_.setKey(selectedStaff_,clefEditTick(),KeyContext::fromSignature(fifths,form?ScaleIdentity::Minor:ScaleIdentity::Major,form?MinorForm(form-1):MinorForm::Melodic));
        if(change)finishKey(std::move(*change));
        return true;
    }
    bool Editor::removeKey() {
        if(!editable() || range_)return false;
        auto change=score_.removeKey(selectedStaff_,clefEditTick());
        if(!change)return false;
        finishKey(std::move(*change));
        return true;
    }
    void Editor::requestKeyProperties() {
        if(editable() && !range_)emit keyPropertiesRequested();
    }
    Tick Editor::clefEditTick() const {
        return clefEditTick_.value_or(measureSelection_?score_.measureStart(selectedMeasure_):score_.eventStart(selected_));
    }
    bool Editor::canRemoveClef() const {
        const auto tick=clefEditTick();
        const auto& changes=score_.staves()[selectedStaff_].clefs;
        return tick>0 && std::any_of(changes.begin(),changes.end(),[tick](const ClefChange& c) {
            return c.tick==tick;
        });
    }
    QVariantMap Editor::selectedMeter() const {
        const auto& m=score_.bar(selectedMeasure_).meter;
        QStringList groups;
        for(int g:m.groups)groups<<QString::number(g);
        return {
            {
                "numerator",m.numerator
            }, {
                "denominator",m.denominator
            }, {
                "groups",groups.join("+")
            }
        };
    }
    QVariantMap Editor::selectedTempo() const {
        const auto t=score_.tempoAt(selectedMeasure_);
        return {
            {
                "beat",static_cast<int>(Score::baseDuration(t.beat))
            }, {
                "dotted",t.beat!=Score::baseDuration(t.beat)
            }, {
                "bpm",t.bpm
            }, {
                "explicit",bool(score_.bar(selectedMeasure_).tempo)
            }
        };
    }
    QVariantList Editor::staffRows() const {
        QVariantList result;
        for(std::size_t i=0;i<score_.staffCount();++i) {
            const auto& s=score_.staves()[i];
            const auto part=std::find_if(score_.parts().begin(),score_.parts().end(),[&](const Part& p) {
                return p.id==s.part;
            });
            result.push_back(QVariantMap {
                {
                    "id",QVariant::fromValue<qulonglong>(s.id)
                }, {
                    "part",QVariant::fromValue<qulonglong>(s.part)
                }, {
                    "name",QString::fromStdString(s.name)
                }, {
                    "partName",QString::fromStdString(part->name)
                }, {
                    "program",s.program
                }, {
                    "effectiveProgram",score_.program(i)
                }, {
                    "clef",static_cast<int>(score_.clefAt(i,score_.measureStart(selectedMeasure_)))
                }, {
                    "index",static_cast<int>(i)
                }
            });
        }
        return result;
    }
    QVariantList Editor::partRows() const {
        QVariantList result;
        for(const auto& p:score_.parts())result.push_back(QVariantMap {
            {
                "id",QVariant::fromValue<qulonglong>(p.id)
            }, {
                "name",QString::fromStdString(p.name)
            }, {
                "program",p.program
            }
        });
        return result;
    }
    QVariantList Editor::groupRows() const {
        QVariantList result;
        for(const auto& g:score_.groups())result.push_back(QVariantMap {
            {
                "id",QVariant::fromValue<qulonglong>(g.id)
            }, {
                "name",QString::fromStdString(g.name)
            }
        });
        return result;
    }
    void Editor::newScore() {
        if(!editable())return;
        stop();
        score_=Score{};
        history_.clear();
        path_.clear();
        dirty_=false;
        ++revision_;
        selectedStaff_=selectedMeasure_=0;
        clefEditTick_.reset(); keySelection_=false;
        selected_=score_.firstEventId(0);
        nextEntry_=0;
        activeTone_=0;
        anchor_= {
            6,4,0
        };
        entryDuration_=quarter;
        measureSelection_=false;
        range_.reset();
        layout_.reset(score_);
        updateSelectionGeometry();
        setStatus("New score");
        emit scoreReset();
    }
    bool Editor::openFile(const QUrl& url) {
        if(!editable())return false;
        const auto path=url.toLocalFile();
        if(path.isEmpty())return false;
        stop();
        if(fileWorker_.joinable())fileWorker_.join();
        fileBusy_=true;
        setStatus("Opening MusicXML…");
        fileWorker_=std::jthread([this,path] {
            Score loaded(1);
            QString error;
            const bool success=MusicXml::open(path,&loaded,&error);
            QMetaObject::invokeMethod(this,[this,path,success,error,loaded=std::move(loaded)]() mutable {
                fileBusy_=false;
                if(!success) {
                    setStatus(error);
                    return;
                }
                score_=std::move(loaded);
                history_.clear();
                path_=path;
                dirty_=false;
                ++revision_;
                selectedStaff_=selectedMeasure_=0;
                clefEditTick_.reset(); keySelection_=false;
                selected_=score_.firstEventId(0);
                nextEntry_=0;
                activeTone_=0;
                measureSelection_=false;
                range_.reset();
                layout_.reset(score_);
                updateSelectionGeometry();
                setStatus("Opened "+QFileInfo(path).fileName());
                emit scoreReset();
            },Qt::QueuedConnection);
        });
        return true;
    }
    bool Editor::saveTo(const QString& path) {
        if(fileBusy_ || editLocked_)return false;
        if(fileWorker_.joinable())fileWorker_.join();
        Score snapshot=score_;
        const auto savedRevision=revision_;
        fileBusy_=true;
        setStatus("Saving MusicXML…");
        fileWorker_=std::jthread([this,path,savedRevision,snapshot=std::move(snapshot)] {
            QString error;
            const bool success=MusicXml::save(snapshot,path,&error);
            QMetaObject::invokeMethod(this,[this,path,success,error,savedRevision] {
                fileBusy_=false;
                if(!success) {
                    setStatus(error);
                    return;
                }
                path_=path;
                if(savedRevision==revision_)dirty_=false;
                setStatus("Saved "+QFileInfo(path).fileName());
            },Qt::QueuedConnection);
        });
        return true;
    }
    bool Editor::save() {
        if(editLocked_)return false;
        if(path_.isEmpty()) {
            emit saveAsRequested();
            return false;
        }
        return saveTo(path_);
    }
    bool Editor::saveAs(const QUrl& url) {
        QString path=url.toLocalFile();
        if(path.isEmpty())return false;
        if(!path.endsWith(".musicxml",Qt::CaseInsensitive))path+=".musicxml";
        return saveTo(path);
    }
    bool Editor::applyLocal(std::size_t measure,const std::function<bool()>& action,bool preview) {
        if(!editable())return false;
        if(range_) { setStatus("Click one note or rest before editing; use Ctrl+V to replace a range."); return false; }
        const auto count=score_.measureCount();
        LocalEdit change;
        const auto first=measure?measure-1:0,last=std::min(count,measure+2);
        for(std::size_t m=first;m<last;++m)change.measures.push_back(MeasureEdit {
            selectedStaff_,m,score_.measure(m,selectedStaff_),{}
        });
        if(!action()) {
            while(score_.measureCount()>count)score_.removeLastMeasure();
            for(const auto& patch:change.measures)score_.restoreMeasure(patch.measure,patch.before,patch.staff);
            repairSelection();
            setStatus("That edit does not fit the available time, written durations, or matching pitches; tuplet boundaries cannot be crossed.");
            return false;
        }
        for(auto& patch:change.measures)patch.after=score_.measure(patch.measure,patch.staff);
        std::erase_if(change.measures,[](const MeasureEdit& p) {
            return p.before==p.after;
        });
        for(std::size_t m=count;m<score_.measureCount();++m)change.appended.push_back(score_.bundle(m));
        finishLocal(std::move(change));
        repairSelection();
        if(preview)previewSelected();
        return true;
    }
    void Editor::finishLocal(LocalEdit change) {
        if(change.measures.empty() && change.appended.empty())return;
        std::vector<std::size_t> changed;
        for(const auto& patch:change.measures)changed.push_back(patch.measure);
        for(std::size_t i=score_.measureCount()-change.appended.size();i<score_.measureCount();++i)changed.push_back(i);
        const auto firstSystem=layout_.invalidate(score_,changed);
        history_.record(std::move(change));
        dirty_=true;
        ++revision_;
        emit layoutInvalidated(firstSystem);
        emit stateChanged();
    }
    bool Editor::applyStructural(const std::function<bool()>& action,const QString& message,bool allowRange) {
        if(!editable())return false;
        if(range_ && !allowRange) { setStatus("Click one event or measure before changing its properties."); return false; }
        Score before=score_;
        if(!action()) {
            score_=std::move(before);
            repairSelection();
            setStatus("The requested change is invalid. Check the selected staves and values.");
            return false;
        }
        history_.record(std::move(before),score_);
        dirty_=true;
        ++revision_;
        nextEntry_=0;
        layout_.reset(score_);
        repairSelection();
        setStatus(message);
        emit scoreReset();
        return true;
    }
    void Editor::repairSelection() {
        if(range_) {
            std::erase_if(range_->staves,[&](StaffId id){return !score_.staffIndex(id);});
            range_->end=std::min(range_->end,score_.totalTicks());
            if(range_->staves.empty() || range_->start>=range_->end)range_.reset();
            else {
                rangeAnchorStart_=std::clamp(rangeAnchorStart_,range_->start,range_->end-1);
                rangeAnchorEnd_=std::clamp(rangeAnchorEnd_,rangeAnchorStart_+1,range_->end);
            }
        }
        if(const auto l=score_.locate(selected_)) {
            selectedStaff_=l->staff;
            selectedMeasure_=l->measure;
            const auto* e=score_.event(selected_);
            activeTone_=e->isRest()?0:std::min(activeTone_,e->pitches.size()-1);
        }
        else {
            selectedStaff_=std::min(selectedStaff_,score_.staffCount()-1);
            selectedMeasure_=std::min(selectedMeasure_,score_.measureCount()-1);
            selected_=score_.firstEventId(selectedMeasure_,selectedStaff_);
            activeTone_=0;
        }
        updateSelectionGeometry();
    }
    void Editor::previewSelected() {
        if(audition_ && !playbackBusy()) {
            const auto* e=score_.event(selected_);
            if(e && !e->isRest())playback_->audition(e->pitches,score_.program(selectedStaff_));
        }
    }
    bool Editor::enterLetter(int step,bool chord) {
        const EventId target=!chord && nextEntry_ && score_.event(nextEntry_) && score_.event(nextEntry_)->isRest()?nextEntry_:selected_;
        const auto loc=score_.locate(target);
        if(!loc)return false;
        selectedStaff_=loc->staff;
        const auto* current=score_.event(target);
        const bool rest=current->isRest();
        const Pitch reference=!rest && activeTone_<current->pitches.size()?current->pitches[activeTone_]:anchor_;
        Pitch pitch=Score::nearestPitch(step,reference);
        pitch.alter=score_.entryAlteration(loc->staff,score_.eventStart(target),pitch.step,pitch.octave);
        if(chord) {
            if(rest) {
                setStatus("Select a note before adding a chord tone.");
                return false;
            }
            for(const auto& other:current->pitches)if(other.midi()==pitch.midi()) {
                pitch.octave+=pitch.midi()+12<=127?1:-1;
                break;
            }
            return applyLocal(loc->measure,[&] {
                std::size_t added=0;
                if(!score_.addChordPitch(target,pitch,&added))return false;
                selected_=target;
                activeTone_=added;
                anchor_=pitch;
                measureSelection_=false;
                return true;
            },true);
        }
        return applyLocal(loc->measure,[&] {
            if(rest) {
                const Tick remaining=score_.bar(loc->measure).meter.ticks()-(score_.eventStart(target)-score_.measureStart(loc->measure));
                Tick available=0;
                const auto& events=score_.measure(loc->measure,loc->staff).events;
                for(std::size_t i=loc->event;i<events.size() && events[i].isRest() && events[i].tuplet==current->tuplet;++i)available+=events[i].ticks;
                const Tick duration=current->tuplet?current->ticks:Score::largestDuration(std::min({entryDuration_,remaining,available}));
                if(!duration || !score_.enterNote(target,pitch,duration))return false;
                selected_=target;
                const auto next=score_.nextEventId(target);
                nextEntry_=next?next:score_.appendMeasure(selectedStaff_);
                activeTone_=0;
            }
            else {
                if(!score_.setPitch(target,activeTone_,pitch))return false;
                selected_=target;
                const auto next=score_.nextEventId(target);
                nextEntry_=next && score_.event(next)->isRest()?next:0;
            }
            anchor_=pitch;
            measureSelection_=false;
            return true;
        },true);
    }
    bool Editor::changeDuration(bool longer,bool conservative) {
        const auto loc=score_.locate(selected_);
        const auto* e=score_.event(selected_);
        if(!loc || !e)return false;
        const Tick written=e->writtenDuration();
        const Tick base=Score::baseDuration(written);
        const Tick nextBase=longer?base*2:base/2;
        const Tick next=written!=base?nextBase*3/2:nextBase;
        if(!Score::validDuration(next))return false;
        const auto* group=score_.tupletFor(selected_);
        const Tick sounding=group?group->soundingDuration(next):next;
        return applyLocal(loc->measure,[&] {
            if(!score_.setDuration(selected_,sounding,!conservative,true))return false;
            if(!group)entryDuration_=score_.event(selected_)->ticks;
            const auto following=score_.nextEventId(selected_);
            nextEntry_=following && score_.event(following)->isRest()?following:0;
            return true;
        });
    }
    bool Editor::toggleDot(bool conservative) {
        const auto loc=score_.locate(selected_);
        const auto* e=score_.event(selected_);
        if(!loc || !e)return false;
        const Tick written=e->writtenDuration();
        const Tick base=Score::baseDuration(written),next=written==base?base*3/2:base;
        const auto* group=score_.tupletFor(selected_);
        const Tick sounding=group?group->soundingDuration(next):next;
        return applyLocal(loc->measure,[&] {
            if(!score_.setDuration(selected_,sounding,!conservative,true))return false;
            if(!group)entryDuration_=score_.event(selected_)->ticks;
            const auto following=score_.nextEventId(selected_);
            nextEntry_=following && score_.event(following)->isRest()?following:0;
            return true;
        });
    }
    bool Editor::createTuplet(int count) {
        if(!editable())return false;
        const auto loc=score_.locate(selected_);
        if(measureSelection_ || range_ || clefEditTick_ || !loc) {
            setStatus("Select a note or rest to divide into a tuplet.");
            return false;
        }
        if(score_.event(selected_)->tuplet) {
            setStatus("Nested tuplets are not supported. Undo to reverse tuplet creation.");
            return false;
        }
        const bool rest=score_.event(selected_)->isRest();
        if(!applyLocal(loc->measure,[&] {
            if(!score_.createTuplet(selected_,count))return false;
            nextEntry_=rest?0:score_.nextEventId(selected_);
            return true;
        })) {
            setStatus("Use 3, 5, or 7 on a duration that can be divided into written values through 32nd notes.");
            return false;
        }
        setStatus(QString("Divided the selected duration into %1 equal slots").arg(count));
        return true;
    }
    bool Editor::transpose(int diatonic,int chromatic,int octave) {
        const auto loc=score_.locate(selected_);
        const auto* e=score_.event(selected_);
        if(!loc || !e || e->isRest() || activeTone_>=e->pitches.size())return false;
        Pitch p=e->pitches[activeTone_];
        if(diatonic) {
            const int index=p.octave*7+p.step+diatonic;
            if(index<0)return false;
            p.octave=index/7;
            p.step=index%7;
        }
        else if(chromatic) {
            const int midi=p.midi()+chromatic;
            if(midi<0 || midi>127)return false;
            p=pitchFromMidi(midi,chromatic>0);
        }
        else p.octave+=octave;
        return applyLocal(loc->measure,[&] {
            if(!score_.setPitch(selected_,activeTone_,p))return false;
            anchor_=p;
            return true;
        },true);
    }
    bool Editor::setAccidental(int alter) {
        const auto loc=score_.locate(selected_);
        const auto* e=score_.event(selected_);
        if(!loc || !e || e->isRest() || activeTone_>=e->pitches.size())return false;
        Pitch p=e->pitches[activeTone_];
        p.alter=alter;
        return applyLocal(loc->measure,[&] {
            if(!score_.setPitch(selected_,activeTone_,p))return false;
            anchor_=p;
            return true;
        },true);
    }
    void Editor::undo() { applyHistory(false); }
    void Editor::redo() { applyHistory(true); }
    void Editor::applyHistory(bool forward) {
        if(!editable())return;
        EditImpact impact;
        if(!(forward?history_.redo(score_,&impact):history_.undo(score_,&impact)))return;
        dirty_=true;
        ++revision_;
        nextEntry_=0;
        if(impact.reset) {
            layout_.reset(score_);
            repairSelection();
            emit scoreReset();
        }
        else {
            const auto first=layout_.invalidate(score_,impact.measures);
            repairSelection();
            emit layoutInvalidated(first);
        }
        emit stateChanged();
        emit revealSelection();
    }
    void Editor::play(bool fromSelection) {
        if(playing()) {
            stop();
            return;
        }
        if(fileBusy_ || (editLocked_ && playbackBusy()))return;
        QString error;
        const auto range=selectedRange();
        const Tick from=scratch_?range.start:(fromSelection?score_.measureStart(range_?score_.measureAt(range_->start):selectedMeasure_):0);
        if(!playback_->start(score_,from,&error,scratch_?range.end:-1,this)) {
            setStatus(error);
            return;
        }
        updatePlayhead();
        playheadTimer_.start();
        setStatus("Playing · "+playback_->diagnostics());
    }
    void Editor::stop() {
        emit stopRequested();
        if(editLocked_ && playbackBusy() && !playing())return;
        playheadTimer_.stop();
        playback_->stop();
        emit stateChanged();
        emit playheadChanged();
    }
    void Editor::updatePlayhead() {
        const auto position=layout_.position(score_,playback_->positionTick());
        playheadX_=position.x();
        playheadY_=std::floor(position.y()/layout_.systemHeight())*layout_.systemHeight()+70;
        playheadHeight_=layout_.systemHeight()-100;
        emit playheadChanged();
    }
    void Editor::selectEvent(quint64 id,int tone) {
        const auto loc=score_.locate(id);
        const auto* e=score_.event(id);
        if(!loc || !e)return;
        range_.reset();
        clefEditTick_.reset(); keySelection_=false;
        const bool differentStaff=selectedStaff_!=loc->staff;
        selected_=id;
        selectedStaff_=loc->staff;
        selectedMeasure_=loc->measure;
        nextEntry_=0;
        activeTone_=e->isRest()?0:std::clamp(tone,0,static_cast<int>(e->pitches.size())-1);
        measureSelection_=false;
        if(!e->isRest())anchor_=e->pitches[activeTone_];
        else if(differentStaff)anchor_=clefAnchor(score_.clefAt(loc->staff,score_.eventStart(id)));
        updateSelectionGeometry();
    }
    void Editor::selectMeasure(int staff,int measure) {
        if(staff<0 || measure<0 || staff>=static_cast<int>(score_.staffCount()) || measure>=static_cast<int>(score_.measureCount()))return;
        range_.reset();
        clefEditTick_.reset(); keySelection_=false;
        selectedStaff_=staff;
        selectedMeasure_=measure;
        selected_=score_.firstEventId(measure,staff);
        activeTone_=0;
        nextEntry_=0;
        measureSelection_=true;
        anchor_=clefAnchor(score_.clefAt(staff,score_.measureStart(measure)));
        updateSelectionGeometry();
    }
    void Editor::updateSelectionGeometry() {
        if(measureSelection_) {
            selectionRect_=layout_.measureRect(selectedMeasure_,selectedStaff_);
            selectionRect_.translate(0,layout_.systemForMeasure(selectedMeasure_)*layout_.systemHeight());
        }
        else {
            const auto* e=score_.event(selected_);
            if(!e)return;
            const auto point=layout_.position(score_,score_.eventStart(selected_),selectedStaff_);
            qreal y=point.y()+32;
            if(!e->isRest())y=Engraving::noteY(e->pitches[activeTone_],score_.clefAt(selectedStaff_,score_.eventStart(selected_)))+selectedStaff_*180+layout_.systemForMeasure(selectedMeasure_)*layout_.systemHeight();
            selectionRect_=QRectF(point.x()-11,y-9,22,18);
        }
        emit selectionChanged();
    }
    void Editor::requestStaffProperties() {
        if(editable())emit staffPropertiesRequested();
    }
    void Editor::requestMeterProperties() {
        if(editable())emit meterPropertiesRequested();
    }
    void Editor::requestTempoProperties() {
        if(editable())emit tempoPropertiesRequested();
    }
    void Editor::activateHit(const ScoreHit& h,bool doubleClick,int modifiers) {
        if(!doubleClick && (h.kind==HitKind::Note || h.kind==HitKind::Measure)) {
            if(modifiers&Qt::ControlModifier) { toggleStaff(h.staff); return; }
            if(modifiers&Qt::ShiftModifier) {
                if(!editable())return;
                extendSelectionTo(h.kind==HitKind::Note?h.event:score_.firstEventId(h.measure,h.staff));
                if(h.kind==HitKind::Measure) {
                    range_->start=std::min(rangeAnchorStart_,score_.measureStart(h.measure));
                    range_->end=std::max(rangeAnchorEnd_,score_.measureStart(h.measure+1));
                    selected_=score_.eventAt(h.staff,score_.measureStart(h.measure)>=rangeAnchorStart_?score_.measureStart(h.measure+1)-1:score_.measureStart(h.measure));
                    updateSelectionGeometry();
                }
                return;
            }
        }
        if(h.kind==HitKind::Note)selectEvent(h.event,h.tone);
        else selectMeasure(h.staff,h.measure);
        if((h.kind==HitKind::Clef || h.kind==HitKind::Key) && h.tick>=0) {
            clefEditTick_=h.tick;
            keySelection_=h.kind==HitKind::Key;
            emit selectionChanged();
        }
        if(h.kind==HitKind::Key)requestKeyProperties();
        else if(doubleClick || h.kind==HitKind::Clef)requestStaffProperties();
        else if(h.kind==HitKind::Meter)requestMeterProperties();
        else if(h.kind==HitKind::Tempo)requestTempoProperties();
    }
    bool Editor::applyClef(int clef) {
        const auto tick=clefEditTick();
        return applyStructural([&] {
            return score_.setClef(selectedStaff_,tick,static_cast<Clef>(clef));
        },"Clef changed");
    }
    bool Editor::removeClef() {
        const auto tick=clefEditTick();
        return applyStructural([&] {
            return score_.removeClef(selectedStaff_,tick);
        },"Clef change removed");
    }
    bool Editor::applyMeter(int numerator,int denominator,const QString& text) {
        Meter m=Score::defaultMeter(numerator,denominator);
        if(!text.trimmed().isEmpty()) {
            m.groups.clear();
            for(const auto& value:text.split('+')) {
                bool ok=false;
                const int n=value.trimmed().toInt(&ok);
                if(!ok) {
                    setStatus("Beat groups must be positive numbers separated by +.");
                    return false;
                }
                m.groups.push_back(n);
            }
        }
        const bool changed=applyStructural([&] {
            return score_.changeMeter(selectedMeasure_,m);
        },"Meter changed and music re-barred");
        if(!changed && !playbackBusy() && !fileBusy_ && m.valid())setStatus("Meter change cannot split a tuplet across a barline or leave unsupported written durations.");
        return changed;
    }
    bool Editor::applyTempo(int beat,int bpm,bool dotted) {
        return applyStructural([&] {
            return score_.setTempo(selectedMeasure_,TempoMark {
                dotted?beat*3/2:beat,bpm
            });
        },"Tempo changed");
    }
    bool Editor::removeTempo() {
        return applyStructural([&] {
            return score_.setTempo(selectedMeasure_,{});
        },"Tempo mark removed");
    }
    bool Editor::addPart(int program,bool piano) {
        if(scratch_)return false;
        if(program<0 || program>=128)return false;
        return applyStructural([&] {
            const auto id=score_.addPart(instruments()[program].toStdString(),program,piano);
            return id!=0;
        },"Part added");
    }
    bool Editor::addStaff(quint64 part,int clef) {
        if(scratch_)return false;
        return applyStructural([&] {
            return score_.addStaff(part,static_cast<Clef>(clef))!=0;
        },"Staff added");
    }
    bool Editor::removeStaff(quint64 staff) {
        return applyStructural([&] {
            return score_.removeStaff(staff);
        },"Staff removed");
    }
    bool Editor::moveStaff(quint64 staff,int direction) {
        return applyStructural([&] {
            return score_.moveStaff(staff,direction);
        },"Staff order changed");
    }
    bool Editor::configureStaff(quint64 id,const QString& name,int program) {
        return applyStructural([&] {
            return score_.setInstrument(id,program,name.toStdString());
        },"Staff instrument updated",true);
    }
    bool Editor::configurePart(quint64 id,const QString& name,int program) {
        return applyStructural([&] {
            return score_.setPartInstrument(id,program,name.toStdString());
        },"Part instrument updated");
    }
    bool Editor::groupParts(const QString& name,const QVariantList& values) {
        std::vector<PartId> ids;
        for(const auto& part:score_.parts())for(const auto& v:values)if(v.toULongLong()==part.id)ids.push_back(part.id);
        return applyStructural([&] {
            return score_.addGroup(name.toStdString(),ids);
        },"Parts grouped");
    }
    bool Editor::removeGroup(quint64 id) {
        return applyStructural([&] {
            return score_.removeGroup(id);
        },"Group removed");
    }
    void Editor::testInstrument(int program) {
        if(!playbackBusy() && program>=0 && program<128)playback_->audition( {
            Pitch {
                0,4,0
            },Pitch {
                2,4,0
            },Pitch {
                4,4,0
            }
        },program);
    }
    bool Editor::handleKey(int key,int modifiers) {
        const bool control=modifiers&Qt::ControlModifier,shift=modifiers&Qt::ShiftModifier;
        if(key==Qt::Key_Escape) {
            stop();
            if(range_) { range_.reset(); updateSelectionGeometry(); }
            return true;
        }
        if(control && key==Qt::Key_S) {
            save();
            return true;
        }
        if(control && scratch_ && (key==Qt::Key_O || key==Qt::Key_N))return true;
        if(control && key==Qt::Key_O) {
            emit openRequested();
            return true;
        }
        if(control && key==Qt::Key_N) {
            emit newRequested();
            return true;
        }
        if(control && key==Qt::Key_Z) {
            if(shift)redo();
            else undo();
            return true;
        }
        if(control && key==Qt::Key_Y) {
            redo();
            return true;
        }
        if(key==Qt::Key_P && !control) {
            play(shift);
            return true;
        }
        if(key==Qt::Key_Space && !control) {
            play(true);
            return true;
        }
        if(playbackBusy() || fileBusy_)return true;
        if(control && key==Qt::Key_C)return copySelection();
        if(control && key==Qt::Key_V)return pasteSelection();
        if((key==Qt::Key_Left || key==Qt::Key_Right) && !control) {
            if(shift)return extendSelection(key==Qt::Key_Right);
            if(range_) {
                const auto id=score_.eventAt(selectedStaff_,key==Qt::Key_Right?range_->end-1:range_->start);
                if(id)selectEvent(id);
            }
            else {
                const auto next=key==Qt::Key_Right?score_.nextEventId(selected_):score_.previousEventId(selected_);
                if(next)selectEvent(next);
            }
            emit revealSelection();
            return true;
        }
        if(!control && !shift && !(modifiers & (Qt::AltModifier|Qt::MetaModifier)) && key==Qt::Key_T) {
            if(clefEditTick_ || selectedRange().staves.size()!=1)setStatus("Select a passage on exactly one staff to transform or analyze.");
            else emit transformationRequested();
            return true;
        }
        if(range_) {
            setStatus("Click one note or rest before editing; Ctrl+C copies and Ctrl+V overwrites the selected range.");
            return true;
        }
        if(!control && shift && key==Qt::Key_T) { requestTempoProperties(); return true; }
        if(!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier)) &&
           (key==Qt::Key_3 || key==Qt::Key_5 || key==Qt::Key_7))return createTuplet(key-Qt::Key_0);
        if(!control && key==Qt::Key_S) {
            requestStaffProperties();
            return true;
        }
        if(!control && key==Qt::Key_M) {
            requestMeterProperties();
            return true;
        }
        if(key>=Qt::Key_A && key<=Qt::Key_G && !control) {
            constexpr std::array<int,7> map {
                5,6,0,1,2,3,4
            };
            return enterLetter(map[key-Qt::Key_A],shift);
        }
        if(key==Qt::Key_BracketLeft)return changeDuration(false,control);
        if(key==Qt::Key_BracketRight)return changeDuration(true,control);
        if(key==Qt::Key_Period)return toggleDot(control);
        if(key==Qt::Key_Comma) {
            const auto loc=score_.locate(selected_);
            return loc && applyLocal(loc->measure,[&] {
                return score_.toggleTie(selected_,activeTone_);
            });
        }
        if(key==Qt::Key_Plus)return setAccidental(1);
        if(key==Qt::Key_Minus)return setAccidental(-1);
        if(key==Qt::Key_Equal)return setAccidental(0);
        if(key==Qt::Key_Delete || key==Qt::Key_Backspace) {
            if(clefEditTick_)return keySelection_?removeKey():removeClef();
            const auto loc=score_.locate(selected_);
            playback_->cancelPreview();
            return loc && applyLocal(loc->measure,[&] {
                nextEntry_=0;
                return score_.makeRest(selected_);
            });
        }
        if(key==Qt::Key_Up || key==Qt::Key_Down) {
            const int sign=key==Qt::Key_Up?1:-1;
            return transpose(control || shift?0:sign,shift?sign:0,control?sign:0);
        }
        if(key==Qt::Key_Left || key==Qt::Key_Right) {
            const auto next=key==Qt::Key_Right?score_.nextEventId(selected_):score_.previousEventId(selected_);
            if(next)selectEvent(next);
            return true;
        }
        return false;
    }
}
// namespace music

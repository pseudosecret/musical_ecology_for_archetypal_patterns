#pragma once
#include "Engraving.h"
#include "Playback.h"
#include "Score.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <thread>
namespace music {
    class TransformWorkspace;
    class Editor:public QObject {
        Q_OBJECT
        Q_PROPERTY(QObject* transformations READ transformations CONSTANT)
        Q_PROPERTY(QString status READ status NOTIFY stateChanged)
        Q_PROPERTY(QString title READ title NOTIFY stateChanged)
        Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
        Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
        Q_PROPERTY(bool playbackBusy READ playbackBusy NOTIFY stateChanged)
        Q_PROPERTY(bool scratch READ scratch CONSTANT)
        Q_PROPERTY(bool editLocked READ editLocked NOTIFY stateChanged)
        Q_PROPERTY(bool audioReady READ audioReady NOTIFY stateChanged)
        Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
        Q_PROPERTY(bool fileBusy READ fileBusy NOTIFY stateChanged)
        Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
        Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
        Q_PROPERTY(bool auditionEnabled READ auditionEnabled WRITE setAuditionEnabled NOTIFY stateChanged)
        Q_PROPERTY(int systemCount READ systemCount NOTIFY stateChanged)
        Q_PROPERTY(qreal scoreWidth READ scoreWidth NOTIFY stateChanged)
        Q_PROPERTY(qreal scoreHeight READ scoreHeight NOTIFY stateChanged)
        Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY selectionChanged)
        Q_PROPERTY(bool selectionIsNote READ selectionIsNote NOTIFY selectionChanged)
        Q_PROPERTY(bool hasRangeSelection READ hasRangeSelection NOTIFY selectionChanged)
        Q_PROPERTY(qreal selectionX READ selectionX NOTIFY selectionChanged)
        Q_PROPERTY(qreal selectionY READ selectionY NOTIFY selectionChanged)
        Q_PROPERTY(qreal playheadX READ playheadX NOTIFY playheadChanged)
        Q_PROPERTY(qreal playheadY READ playheadY NOTIFY playheadChanged)
        Q_PROPERTY(qreal playheadHeight READ playheadHeight NOTIFY playheadChanged)
        Q_PROPERTY(int selectedClef READ selectedClef NOTIFY selectionChanged)
        Q_PROPERTY(QVariantMap selectedKey READ selectedKey NOTIFY selectionChanged)
        Q_PROPERTY(bool canRemoveClef READ canRemoveClef NOTIFY selectionChanged)
        Q_PROPERTY(int selectedMeasure READ selectedMeasure NOTIFY selectionChanged)
        Q_PROPERTY(QVariantMap selectedMeter READ selectedMeter NOTIFY selectionChanged)
        Q_PROPERTY(QVariantMap selectedTempo READ selectedTempo NOTIFY selectionChanged)
        Q_PROPERTY(QVariantList tempoBeatValues READ tempoBeatValues CONSTANT)
        Q_PROPERTY(QVariantList staffRows READ staffRows NOTIFY stateChanged)
        Q_PROPERTY(QVariantList partRows READ partRows NOTIFY stateChanged)
        Q_PROPERTY(QVariantList groupRows READ groupRows NOTIFY stateChanged)
        Q_PROPERTY(QStringList instruments READ instruments CONSTANT)
        Q_PROPERTY(QStringList clefs READ clefs CONSTANT)
        public:
        explicit Editor(QObject* parent=nullptr);
        QObject* transformations();
        Editor(std::shared_ptr<Playback> playback,bool scratch,QObject* parent=nullptr);
        [[nodiscard]] std::shared_ptr<Playback> audioEngine() const { return playback_; }
        [[nodiscard]] bool scratch() const { return scratch_; }
        bool editLocked() const { return editLocked_; }
        void setEditLocked(bool locked);
        [[nodiscard]] std::uint64_t revision() const { return revision_; }
        bool selectAnalysisRange(Tick start,Tick end);
        [[nodiscard]] bool playbackBusy() const { return playback_->isPlaying(); }
        void installScratch(Score score,Tick start,Tick end,const QString& label);
        ~Editor() override;
        [[nodiscard]] const Score& score() const {
            return score_;
        }
        [[nodiscard]] const LayoutIndex& layoutIndex() const {
            return layout_;
        }
        [[nodiscard]] QString status() const {
            return status_;
        }
        [[nodiscard]] QString title() const;
        [[nodiscard]] bool dirty() const {
            return dirty_;
        }
        [[nodiscard]] bool playing() const {
            return playback_->isPlaying() && playback_->owner()==this;
        }
        [[nodiscard]] bool audioReady() const {
            return playback_->isReady();
        }
        [[nodiscard]] int volume() const { return playback_->volume(); }
        void setVolume(int percent);
        [[nodiscard]] bool fileBusy() const {
            return fileBusy_;
        }
        [[nodiscard]] bool canUndo() const {
            return history_.canUndo();
        }
        [[nodiscard]] bool canRedo() const {
            return history_.canRedo();
        }
        [[nodiscard]] bool auditionEnabled() const {
            return audition_;
        }
        void setAuditionEnabled(bool enabled);
        [[nodiscard]] int systemCount() const {
            return layout_.systemCount();
        }
        [[nodiscard]] qreal scoreWidth() const {
            return layout_.width();
        }
        [[nodiscard]] qreal scoreHeight() const {
            return layout_.height();
        }
        [[nodiscard]] QRectF selectionRect() const {
            return selectionRect_;
        }
        [[nodiscard]] bool selectionIsNote() const {
            return !measureSelection_ && !range_;
        }
        [[nodiscard]] bool hasRangeSelection() const { return range_.has_value(); }
        [[nodiscard]] ScoreRange selectedRange() const;
        [[nodiscard]] std::vector<QRectF> rangeRects(std::size_t system) const;
        [[nodiscard]] qreal selectionX() const {
            return selectionRect_.center().x();
        }
        [[nodiscard]] qreal selectionY() const {
            return selectionRect_.y();
        }
        [[nodiscard]] qreal playheadX() const {
            return playheadX_;
        }
        [[nodiscard]] qreal playheadY() const {
            return playheadY_;
        }
        [[nodiscard]] qreal playheadHeight() const {
            return playheadHeight_;
        }
        [[nodiscard]] int selectedClef() const;
        [[nodiscard]] QVariantMap selectedKey() const;
        Q_INVOKABLE QStringList keyChoices(int form) const;
        Q_INVOKABLE bool applyKey(int fifths,int form);
        Q_INVOKABLE bool removeKey();
        Q_INVOKABLE void requestKeyProperties();
        [[nodiscard]] bool canRemoveClef() const;
        [[nodiscard]] EventId selectedEventId() const {
            return measureSelection_ || range_?0:selected_;
        }
        [[nodiscard]] std::size_t selectedTone() const {
            return activeTone_;
        }
        [[nodiscard]] int selectedMeasure() const {
            return selectedMeasure_;
        }
        [[nodiscard]] QVariantMap selectedMeter() const;
        [[nodiscard]] QVariantMap selectedTempo() const;
        [[nodiscard]] QVariantList tempoBeatValues() const {
            return {int(4*quarter),int(2*quarter),int(quarter),int(quarter/2),int(quarter/4)};
        }
        [[nodiscard]] QVariantList staffRows() const;
        [[nodiscard]] QVariantList partRows() const;
        [[nodiscard]] QVariantList groupRows() const;
        [[nodiscard]] QStringList instruments() const;
        [[nodiscard]] QStringList clefs() const;
        Q_INVOKABLE QPointF rememberedScroll() const { return scroll_; }
        Q_INVOKABLE void rememberScroll(qreal x,qreal y) { scroll_={x,y}; }
        Q_INVOKABLE void newScore();
        Q_INVOKABLE bool openFile(const QUrl& url);
        Q_INVOKABLE bool save();
        Q_INVOKABLE bool saveAs(const QUrl& url);
        Q_INVOKABLE void undo();
        Q_INVOKABLE void redo();
        Q_INVOKABLE void play(bool fromSelection=false);
        Q_INVOKABLE void stop();
        Q_INVOKABLE bool handleKey(int key,int modifiers);
        Q_INVOKABLE bool createTuplet(int count);
        Q_INVOKABLE bool copySelection();
        Q_INVOKABLE bool pasteSelection();
        Q_INVOKABLE void selectEvent(quint64 id,int pitchIndex=0);
        Q_INVOKABLE void selectMeasure(int staff,int measure);
        Q_INVOKABLE bool applyClef(int clef);
        Q_INVOKABLE bool removeClef();
        Q_INVOKABLE bool applyMeter(int numerator,int denominator,const QString& groups);
        Q_INVOKABLE bool applyTempo(int beat,int bpm,bool dotted);
        Q_INVOKABLE bool removeTempo();
        Q_INVOKABLE void requestStaffProperties();
        Q_INVOKABLE void requestMeterProperties();
        Q_INVOKABLE void requestTempoProperties();
        Q_INVOKABLE bool addPart(int program,bool piano=false);
        Q_INVOKABLE bool addStaff(quint64 part,int clef);
        Q_INVOKABLE bool removeStaff(quint64 staff);
        Q_INVOKABLE bool moveStaff(quint64 staff,int direction);
        Q_INVOKABLE bool configureStaff(quint64 staff,const QString& name,int program);
        Q_INVOKABLE bool configurePart(quint64 part,const QString& name,int program);
        Q_INVOKABLE bool groupParts(const QString& name,const QVariantList& parts);
        Q_INVOKABLE bool removeGroup(quint64 group);
        Q_INVOKABLE void testInstrument(int program);
        void activateHit(const ScoreHit& hit,bool doubleClick,int modifiers=0);
        signals:
        void stateChanged();
        void selectionChanged();
        void revealSelection();
        void playheadChanged();
        void volumeChanged();
        void measureChanged(int measureIndex);
        void layoutInvalidated(int firstSystem);
        void scoreReset();
        void newRequested();
        void openRequested();
        void saveAsRequested();
        void staffPropertiesRequested();
        void keyPropertiesRequested();
        void meterPropertiesRequested();
        void tempoPropertiesRequested();
        void transformationRequested();
        void stopRequested();
        private:
        bool editable();
        bool applyLocal(std::size_t measure,const std::function<bool()>& action,bool preview=false);
        bool applyStructural(const std::function<bool()>& action,const QString& status,bool allowRange=false);
        void finishLocal(LocalEdit change);
        void finishKey(KeyEdit change);
        void applyHistory(bool forward);
        bool extendSelection(bool right);
        void extendSelectionTo(EventId id);
        void toggleStaff(std::size_t staff);
        bool enterLetter(int step,bool chord);
        bool changeDuration(bool longer,bool conservative=false);
        bool toggleDot(bool conservative=false);
        bool transpose(int diatonic,int chromatic,int octave);
        bool setAccidental(int alter);
        void updateSelectionGeometry();
        void repairSelection();
        void updatePlayhead();
        void setStatus(QString message);
        bool saveTo(const QString& path);
        void previewSelected();
        [[nodiscard]] Tick clefEditTick() const;
        Score score_;
        LayoutIndex layout_;
        EditHistory history_;
        std::shared_ptr<Playback> playback_;
        QTimer playheadTimer_;
        std::jthread fileWorker_;
        EventId selected_=0,nextEntry_=0;
        std::size_t activeTone_=0,selectedStaff_=0,selectedMeasure_=0;
        Pitch anchor_ {
            6,4,0
        };
        Tick entryDuration_=quarter;
        std::optional<Tick> clefEditTick_;
        std::optional<ScoreRange> range_;
        Tick rangeAnchorStart_=0,rangeAnchorEnd_=0;
        QString path_,status_;
        bool scratch_=false,editLocked_=false;
        QString scratchLabel_;
        bool dirty_=false,measureSelection_=false,audition_=true,fileBusy_=false,keySelection_=false;
        std::uint64_t revision_=0;
        QRectF selectionRect_;
        QPointF scroll_;
        std::unique_ptr<TransformWorkspace> transformations_;
        qreal playheadX_=0,playheadY_=0,playheadHeight_=72;
    };
}
// namespace music

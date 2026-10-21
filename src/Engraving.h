#pragma once
#include "Score.h"
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <optional>
#include <map>
#include <vector>
namespace music {
    struct Column {
        Tick tick=0;
        qreal x=0;
        qreal gap=0;
    };
    struct MeasureSpacing {
        qreal minimum=0;
        std::vector<Column> columns;
    };
    struct SystemPlacement {
        std::size_t first=0,count=0;
        qreal width=1050;
    };
    class LayoutIndex {
        public:
        void reset(const Score& score);
        std::size_t invalidate(const Score& score,std::size_t measure);
        std::size_t invalidate(const Score& score,const std::vector<std::size_t>& measures);
        [[nodiscard]] std::size_t systemCount() const {
            return systems_.size();
        }
        [[nodiscard]] const SystemPlacement& system(std::size_t i) const {
            return systems_.at(i);
        }
        [[nodiscard]] std::size_t systemForMeasure(std::size_t measure) const;
        [[nodiscard]] qreal height() const {
            return systemHeight_*systems_.size();
        }
        [[nodiscard]] qreal width() const {
            return width_;
        }
        [[nodiscard]] qreal systemHeight() const {
            return systemHeight_;
        }
        [[nodiscard]] QRectF measureRect(std::size_t measure,std::size_t staff) const;
        [[nodiscard]] QPointF position(const Score& score,Tick tick,std::size_t staff=0) const;
        [[nodiscard]] qreal eventX(std::size_t measure,Tick within) const;
        private:
        SystemPlacement pack(std::size_t first) const;
        static MeasureSpacing spacing(const Score& score,std::size_t measure);
        std::vector<MeasureSpacing> measures_;
        std::vector<SystemPlacement> systems_;
        std::map<qreal,std::size_t> widthCounts_;
        qreal systemHeight_=220,width_=1050;
    };
    struct EngravedEvent {
        Event event;
        std::size_t measureIndex=0,staff=0;
        Tick start=0;
        QPointF origin;
        std::vector<qreal> toneY,headX;
        std::vector<int> shownAccidentals;
        QRectF hitBox;
        qreal staffTop=80,stemX=0,stemTip=0;
        bool stemUp=true,beamed=false;
    };
    struct GlyphMark {
        QPointF position;
        Clef clef=Clef::Treble;
        QRectF hitBox;
        std::size_t staff=0,measure=0;
        bool small=false;
        Tick tick=0;
    };
    struct MeasureRegion {
        QRectF rect;
        std::size_t staff=0,measure=0;
    };
    struct KeyMark {
        std::vector<std::pair<QPointF,int>> glyphs;
        QRectF hitBox;
        std::size_t staff=0,measure=0;
        Tick tick=0;
    };
    struct TextMark {
        QPointF position;
        QString text;
        QRectF hitBox;
        std::size_t measure=0;
        bool tempo=false;
    };
    struct BeamLine {
        QPointF a,b;
        qreal width=4;
    };
    struct TieCurve {
        QPointF a,b;
        bool above=false;
    };
    struct TupletMark {
        TupletId id=0;
        QPointF a,b;
        QRectF numberBox;
        QString text;
        bool bracket=true,above=true;
    };
    struct SystemSnapshot {
        std::size_t index=0,first=0,count=0;
        qreal width=1050,height=220;
        std::vector<EngravedEvent> events;
        std::vector<MeasureRegion> regions;
        std::vector<GlyphMark> clefs;
        std::vector<KeyMark> keys;
        std::vector<TextMark> marks;
        std::vector<BeamLine> beams;
        std::vector<TieCurve> ties;
        std::vector<TupletMark> tuplets;
        std::vector<std::pair<qreal,qreal>> braces,brackets;
        std::vector<QString> labels;
    };
    enum class HitKind {
        Note,Measure,Clef,Meter,Tempo,Key
    };
    struct ScoreHit {
        HitKind kind=HitKind::Measure;
        EventId event=0;
        std::size_t tone=0,staff=0,measure=0;
        Tick tick=-1;
    };
    class Engraving {
        public:
        static constexpr int measuresPerSystem=2,systemWidth=1050,systemHeight=220;
        [[nodiscard]] static std::size_t systemCount(const Score& score);
        [[nodiscard]] static SystemSnapshot layout(const Score& score,std::size_t system);
        [[nodiscard]] static SystemSnapshot layout(const Score& score,const LayoutIndex& index,std::size_t system);
        [[nodiscard]] static QImage render(const SystemSnapshot& system,qreal scale=1.0);
        [[nodiscard]] static QImage renderSelection(const SystemSnapshot& system,EventId event,std::size_t tone,QRectF& bounds);
        [[nodiscard]] static std::optional<ScoreHit> hit(const SystemSnapshot& system,QPointF point);
        [[nodiscard]] static std::optional<std::pair<EventId,std::size_t>> hitTest(const SystemSnapshot& system,QPointF point);
        [[nodiscard]] static qreal noteY(const Pitch& pitch,Clef clef=Clef::Treble);
        [[nodiscard]] static QPointF tickPosition(Tick tick);
    };
}
// namespace music

#include "Engraving.h"
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
namespace music {
    namespace {
        constexpr qreal left=150,staffStride=180,top=80,lineGap=12;
        std::vector<Pitch> signatureTones(const KeyContext& key,const KeyContext* previous,Clef clef) {
            constexpr int sharpSteps[]{3,0,4,1,5,2,6},flatSteps[]{6,2,5,1,4,0,3};
            int octave=clef==Clef::Bass || clef==Clef::Bass8vb?2:clef==Clef::Alto || clef==Clef::Tenor?3:4;
            if(clef==Clef::Treble8va)++octave;
            if(clef==Clef::Bass8vb)--octave;
            auto tone=[&](int step,int alter,bool sharp) {
                int o=octave;
                if(sharp) { if(step!=5 && step!=6)++o; }
                else if(step==0 || step==1 || step==2)++o;
                if(clef==Clef::Tenor && sharp && step==3)--o;
                return Pitch{step,o,alter};
            };
            std::vector<Pitch> result;
            if(previous && previous->fifths) {
                const bool sharp=*previous->fifths>0;
                for(int i=0;i<std::abs(*previous->fifths);++i) {
                    const int step=(sharp?sharpSteps:flatSteps)[i];
                    if(key.alteration(step)!=previous->alteration(step))result.push_back(tone(step,0,sharp));
                }
            }
            if(key.fifths)for(int i=0;i<std::abs(*key.fifths);++i) {
                const bool sharp=*key.fifths>0;
                result.push_back(tone((sharp?sharpSteps:flatSteps)[i],sharp?1:-1,sharp));
            }
            return result;
        }
        bool keyChangeAt(const Score& score,std::size_t staff,Tick tick) {
            const auto& keys=score.staves()[staff].keys;
            const auto it=std::lower_bound(keys.begin(),keys.end(),tick,[](const KeyChange& k,Tick t){return k.tick<t;});
            return it!=keys.end() && it->tick==tick;
        }
        bool incomingTie(const Score& score,const Event& event,const Pitch& pitch) {
            const auto* previous=score.event(score.previousEventId(event.id));
            if(previous)for(std::size_t i=0;i<previous->pitches.size();++i)
                if(previous->tieTarget(i)==event.id && previous->pitches[i]==pitch)return true;
            return false;
        }
        qreal staffTop(std::size_t staff) {
            return top+staffStride*staff;
        }
        void glyph(QPainter& p,char16_t code,QPointF center,int size) {
            QFont font("Bravura");
            font.setPixelSize(size);
            p.setFont(font);
            const QString text {
                QChar(code)
            };
            static std::map<std::pair<char16_t,int>,QRectF> bounds;
            const auto key=std::pair {
                code,size
            };
            if(!bounds.contains(key))bounds[key]=QFontMetricsF(font).boundingRect(text);
            auto registration=bounds.at(key).center();
            // Accidentals register on the staff pitch; centering a flat's tall stem lowers its bowl.
            if(code>=0xE260 && code<=0xE264)registration.setY(0);
            p.drawText(center-registration,text);
        }
        char16_t clefGlyph(Clef c) {
            switch(c) {
                case Clef::Bass:case Clef::Bass8vb:return 0xE062;
                case Clef::Alto:case Clef::Tenor:return 0xE05C;
                default:return 0xE050;
            }
        }
        QString tempoText(TempoMark t) {
            QString note=Score::baseDuration(t.beat)==(4*quarter)?QString(QChar(0xE1D2)):Score::baseDuration(t.beat)==(2*quarter)?QString(QChar(0xE1D3)):Score::baseDuration(t.beat)==quarter?QString(QChar(0xE1D5)):Score::baseDuration(t.beat)==(quarter/2)?QString(QChar(0xE1D7)):QString(QChar(0xE1D9));
            return note+(t.beat!=Score::baseDuration(t.beat)?QString("·"):QString{})+QString(" = %1").arg(t.bpm);
        }
        int flags(Tick base) {
            return base<=(quarter/8)?3:base<=(quarter/4)?2:base<=(quarter/2)?1:0;
        }
        void ledger(QPainter& p,qreal x,qreal y,qreal t) {
            for(qreal line=t-12;line>=y-1;line-=12)p.drawLine(QPointF(x-12,line),QPointF(x+12,line));
            for(qreal line=t+60;line<=y+1;line+=12)p.drawLine(QPointF(x-12,line),QPointF(x+12,line));
        }
    }
    MeasureSpacing LayoutIndex::spacing(const Score& score,std::size_t m) {
        std::map<Tick,qreal> positions;
        std::map<Tick,qreal> before;
        std::map<Tick,qreal> accidentalBefore;
        qreal keyPrefix=0;
        positions[0]=26;
        for(std::size_t s=0;s<score.staffCount();++s) {
            const Tick begin=score.measureStart(m),end=score.measureStart(m+1);
            const auto& initial=score.keyAt(s,begin);
            const auto* previous=begin && keyChangeAt(score,s,begin)?&score.keyAt(s,begin-1):nullptr;
            keyPrefix=std::max(keyPrefix,qreal(12*signatureTones(initial,previous,score.clefAt(s,begin)).size()));
            const auto& keys=score.staves()[s].keys;
            auto k=std::upper_bound(keys.begin(),keys.end(),begin,[](Tick t,const KeyChange& c){return t<c.tick;});
            for(;k!=keys.end() && k->tick<end;++k) {
                const qreal width=12*signatureTones(k->context,&std::prev(k)->context,score.clefAt(s,k->tick)).size();
                if(width) { before[k->tick-begin]=std::max(before[k->tick-begin],width+64); positions.try_emplace(k->tick-begin,28); }
            }
            Tick tick=0;
            Tick accidentalKeyTick=begin;
            std::map<std::pair<int,int>,int> accidentals;
            for(const auto& e:score.measure(m,s).events) {
                auto active=std::upper_bound(keys.begin(),keys.end(),begin+tick,[](Tick t,const KeyChange& c){return t<c.tick;});
                const auto& key=*std::prev(active);
                if(key.tick>accidentalKeyTick) { accidentals.clear(); accidentalKeyTick=key.tick; }
                qreal accidental=0;
                for(std::size_t tone=0;tone<e.pitches.size();++tone) {
                    const auto& p=e.pitches[tone];
                    const auto spelling=std::pair{p.step,p.octave};
                    const int old=accidentals.contains(spelling)?accidentals[spelling]:key.context.alteration(p.step);
                    if(!incomingTie(score,e,p)) {
                        if(old!=p.alter)accidental=std::max(accidental,qreal(28+14*(tone%3)));
                        accidentals[spelling]=p.alter;
                    }
                }
                accidentalBefore[tick]=std::max(accidentalBefore[tick],accidental);
                positions[tick]=std::max(positions[tick],qreal(28));
                tick+=e.ticks;
            }
            for(const auto& c:score.staves()[s].clefs)if(c.tick>begin && c.tick<end) {
                const auto local=c.tick-begin;
                positions.try_emplace(local,28);
                const auto& key=score.keyAt(s,c.tick);
                const auto width=keyChangeAt(score,s,c.tick)?12*signatureTones(key,&score.keyAt(s,c.tick-1),c.clef).size()+64:0;
                before[local]=std::max(before[local],qreal(width+64));
            }
        }
        MeasureSpacing result;
        const qreal signature=(score.bar(m).meterChange || m==0)?38:0;
        qreal x=50+signature+keyPrefix;
        for(auto it=positions.begin();it!=positions.end();++it) {
            x+=before[it->first]+accidentalBefore[it->first];
            auto next=std::next(it);
            const Tick duration=next==positions.end()?score.bar(m).meter.ticks()-it->first:next->first-it->first;
            const qreal gap=it->second+14*std::sqrt(static_cast<qreal>(duration)/quarter);
            result.columns.push_back(Column {
                it->first,x,gap
            });
            x+=gap;
        }
        result.minimum=std::max(qreal(260),x+20);
        return result;
    }
    SystemPlacement LayoutIndex::pack(std::size_t first) const {
        SystemPlacement p {
            first,1,std::max(qreal(1050),left+measures_[first].minimum+20)
        };
        if(first+1<measures_.size() && measures_[first].minimum+measures_[first+1].minimum<=1050-left-20)p.count=2;
        return p;
    }
    void LayoutIndex::reset(const Score& score) {
        measures_.clear();
        systems_.clear();
        widthCounts_.clear();
        width_=1050;
        systemHeight_=40+staffStride*score.staffCount();
        for(std::size_t m=0;m<score.measureCount();++m)measures_.push_back(spacing(score,m));
        for(std::size_t m=0;m<measures_.size();) {
            auto s=pack(m);
            width_=std::max(width_,s.width);
            systems_.push_back(s);
            ++widthCounts_[s.width];
            m+=s.count;
        }
    }
    std::size_t LayoutIndex::systemForMeasure(std::size_t m) const {
        auto it=std::upper_bound(systems_.begin(),systems_.end(),m,[](std::size_t measure,const SystemPlacement& p) {
            return measure<p.first;
        });
        return it==systems_.begin()?0:std::prev(it)-systems_.begin();
    }
    std::size_t LayoutIndex::invalidate(const Score& score,std::size_t m) {
        return invalidate(score,std::vector<std::size_t>{m});
    }
    std::size_t LayoutIndex::invalidate(const Score& score,const std::vector<std::size_t>& changed) {
        if(systems_.empty() || systemHeight_!=40+staffStride*score.staffCount()) {
            reset(score);
            return 0;
        }
        const auto oldCount=measures_.size(),newCount=score.measureCount();
        std::size_t first=std::min(oldCount,newCount)-1,last=0;
        auto unique=changed;
        std::sort(unique.begin(),unique.end());
        unique.erase(std::unique(unique.begin(),unique.end()),unique.end());
        for(auto m:unique) {
            first=std::min(first,std::min(m,newCount-1));
            if(m<newCount)last=std::max(last,m);
        }
        if(oldCount!=newCount) { first=std::min(first,std::min(oldCount,newCount)-1); last=newCount-1; }
        auto firstSystem=systemForMeasure(first);
        // A narrower first measure may fit back into the preceding system.
        if(firstSystem && systems_[firstSystem].first==first)--firstSystem;
        measures_.resize(newCount);
        for(auto m:unique)if(m<std::min(oldCount,newCount))measures_[m]=spacing(score,m);
        for(std::size_t m=oldCount;m<newCount;++m)measures_[m]=spacing(score,m);
        std::vector<SystemPlacement> replacement;
        std::size_t cursor=systems_[firstSystem].first,endSystem=firstSystem;
        while(cursor<measures_.size()) {
            auto p=pack(cursor);
            replacement.push_back(p);
            cursor+=p.count;
            if(cursor==measures_.size()) {
                endSystem=systems_.size();
                break;
            }
            const auto old=systemForMeasure(cursor);
            if(cursor>last && old>firstSystem && systems_[old].first==cursor) {
                endSystem=old;
                break;
            }
        }
        for(std::size_t i=firstSystem;i<endSystem;++i) {
            auto it=widthCounts_.find(systems_[i].width);
            if(--it->second==0)widthCounts_.erase(it);
        }
        for(const auto& system:replacement)++widthCounts_[system.width];
        if(replacement.size()==endSystem-firstSystem) {
            for(std::size_t i=0;i<replacement.size();++i)systems_[firstSystem+i]=replacement[i];
        }
        else {
            systems_.erase(systems_.begin()+firstSystem,systems_.begin()+endSystem);
            systems_.insert(systems_.begin()+firstSystem,replacement.begin(),replacement.end());
        }
        width_=widthCounts_.empty()?1050:widthCounts_.rbegin()->first;
        return firstSystem;
    }
    QRectF LayoutIndex::measureRect(std::size_t m,std::size_t staff) const {
        const auto& sys=systems_[systemForMeasure(m)];
        qreal minimum=0;
        for(std::size_t i=0;i<sys.count;++i)minimum+=measures_[sys.first+i].minimum;
        const qreal extra=(sys.width-left-20-minimum)/sys.count;
        qreal x=left;
        for(std::size_t i=sys.first;i<m;++i)x+=measures_[i].minimum+extra;
        return QRectF(x,staffTop(staff)-28,measures_[m].minimum+extra,126);
    }
    qreal LayoutIndex::eventX(std::size_t m,Tick within) const {
        const auto rect=measureRect(m,0);
        const auto& spacing=measures_[m];
        const qreal extension=rect.width()-spacing.minimum;
        const auto& columns=spacing.columns;
        auto expanded=[&](std::size_t i) {
            return rect.x()+columns[i].x+extension*static_cast<qreal>(i+1)/(columns.size()+1);
        };
        auto it=std::upper_bound(columns.begin(),columns.end(),within,[](Tick tick,const Column& c) {
            return tick<c.tick;
        });
        if(it==columns.begin())return expanded(0);
        const auto i=static_cast<std::size_t>(std::prev(it)-columns.begin());
        if(columns[i].tick==within)return expanded(i);
        if(it==columns.end())return expanded(i)+static_cast<qreal>(within-columns[i].tick)/(std::max<Tick>(1,within-columns[i].tick+quarter))*std::max(qreal(0),rect.right()-18-expanded(i));
        const qreal fraction=static_cast<qreal>(within-columns[i].tick)/(columns[i+1].tick-columns[i].tick);
        return expanded(i)+(expanded(i+1)-expanded(i))*fraction;
    }
    QPointF LayoutIndex::position(const Score& score,Tick tick,std::size_t staff) const {
        const auto m=score.measureAt(tick);
        return QPointF(eventX(m,tick-score.measureStart(m)),systemForMeasure(m)*systemHeight_+staffTop(staff)-8);
    }
    std::size_t Engraving::systemCount(const Score& score) {
        LayoutIndex index;
        index.reset(score);
        return index.systemCount();
    }
    qreal Engraving::noteY(const Pitch& p,Clef clef) {
        int reference=30;
        switch(clef) {
            case Clef::Bass:reference=18;
            break;
            case Clef::Alto:reference=24;
            break;
            case Clef::Tenor:reference=22;
            break;
            case Clef::Treble8va:reference=37;
            break;
            case Clef::Bass8vb:reference=11;
            break;
            default:break;
        }
        return top+48-(p.octave*7+p.step-reference)*6;
    }
    SystemSnapshot Engraving::layout(const Score& score,std::size_t s) {
        LayoutIndex index;
        index.reset(score);
        return layout(score,index,s);
    }
    SystemSnapshot Engraving::layout(const Score& score,const LayoutIndex& index,std::size_t si) {
        SystemSnapshot out;
        const auto& place=index.system(si);
        out.index=si;
        out.first=place.first;
        out.count=place.count;
        out.width=place.width;
        out.height=index.systemHeight();
        for(std::size_t staff=0;staff<score.staffCount();++staff) {
            const qreal y=staffTop(staff);
            out.labels.push_back(QString::fromStdString(score.staves()[staff].name));
            for(std::size_t m=place.first;m<place.first+place.count;++m) {
                const auto rect=index.measureRect(m,staff);
                out.regions.push_back(MeasureRegion {
                    rect,staff,m
                });
                const Tick begin=score.measureStart(m);
                auto addKey=[&](Tick at,qreal x,const KeyContext* previous) {
                    const auto clef=score.clefAt(staff,at);
                    const auto tones=signatureTones(score.keyAt(staff,at),previous,clef);
                    KeyMark mark{{},QRectF(x-5,y-28,tones.size()*12+8,100),staff,m,at};
                    for(const auto& p:tones) { mark.glyphs.push_back({QPointF(x,noteY(p,clef)+y-top),p.alter}); x+=12; }
                    out.keys.push_back(std::move(mark));
                };
                const bool startChange=keyChangeAt(score,staff,begin);
                const auto* previous=begin && startChange?&score.keyAt(staff,begin-1):nullptr;
                if(m==place.first || startChange)addKey(begin,rect.x()+43,previous);
                const auto& keys=score.staves()[staff].keys;
                auto key=std::upper_bound(keys.begin(),keys.end(),begin,[](Tick t,const KeyChange& c){return t<c.tick;});
                for(;key!=keys.end() && key->tick<score.measureStart(m+1);++key) {
                    const auto count=signatureTones(key->context,&std::prev(key)->context,score.clefAt(staff,key->tick)).size();
                    addKey(key->tick,index.eventX(m,key->tick-begin)-12*count-68,&std::prev(key)->context);
                }
                if(m==place.first)out.clefs.push_back(GlyphMark {
                    QPointF(rect.x()+20,y+24),score.clefAt(staff,begin),QRectF(rect.x(),y-20,38,90),staff,m,false,begin
                });
                for(const auto& c:score.staves()[staff].clefs)if(c.tick>=begin && c.tick<score.measureStart(m+1) && c.tick>0 && !(m==place.first && c.tick==begin)) {
                    const auto keyWidth=keyChangeAt(score,staff,c.tick)?12*signatureTones(score.keyAt(staff,c.tick),&score.keyAt(staff,c.tick-1),c.clef).size():0;
                    const qreal x=c.tick==begin?rect.x()+20:index.eventX(m,c.tick-begin)-80-keyWidth-(keyWidth?14:0);
                    out.clefs.push_back(GlyphMark {
                        QPointF(x,y+24),c.clef,QRectF(x-12,y-15,24,75),staff,m,true,c.tick
                    });
                }
                if(score.bar(m).meterChange || m==0) {
                    const auto meter=score.bar(m).meter;
                    out.marks.push_back(TextMark {
                        QPointF(rect.x()+46+12*signatureTones(score.keyAt(staff,begin),previous,score.clefAt(staff,begin)).size(),y),QString("%1\n%2").arg(meter.numerator).arg(meter.denominator),QRectF(rect.x()+38+12*signatureTones(score.keyAt(staff,begin),previous,score.clefAt(staff,begin)).size(),y-5,28,55),m,false
                    });
                }
                if(staff==0 && score.bar(m).tempo)out.marks.push_back(TextMark {
                    QPointF(rect.x()+42,y-40),tempoText(*score.bar(m).tempo),QRectF(rect.x()+35,y-62,155,28),m,true
                });
                Tick tick=0;
                std::map<std::pair<int,int>,int> accidentals;
                Tick accidentalKeyTick=begin;
                for(const auto& e:score.measure(m,staff).events) {
                    EngravedEvent engr;
                    engr.event=e;
                    engr.staff=staff;
                    engr.measureIndex=m;
                    engr.start=tick;
                    engr.staffTop=y;
                    const qreal x=index.eventX(m,tick);
                    engr.origin=QPointF(x,y+24);
                    const auto clef=score.clefAt(staff,begin+tick);
                    auto active=std::upper_bound(keys.begin(),keys.end(),begin+tick,[](Tick t,const KeyChange& c){return t<c.tick;});
                    const auto& activeKey=*std::prev(active);
                    if(activeKey.tick>accidentalKeyTick) { accidentals.clear(); accidentalKeyTick=activeKey.tick; }
                    for(const auto& p:e.pitches) {
                        engr.toneY.push_back(noteY(p,clef)+y-top);
                        engr.headX.push_back(x);
                        const auto key=std::pair {
                            p.step,p.octave
                        };
                        const int old=accidentals.contains(key)?accidentals[key]:activeKey.context.alteration(p.step);
                        const bool tied=incomingTie(score,e,p);
                        engr.shownAccidentals.push_back(tied || old==p.alter?99:p.alter);
                        if(!tied)accidentals[key]=p.alter;
                    }
                    const qreal mean=engr.toneY.empty()?y+24:std::accumulate(engr.toneY.begin(),engr.toneY.end(),qreal(0))/engr.toneY.size();
                    engr.stemUp=mean>=y+24;
                    for(std::size_t a=0;a<engr.toneY.size();++a)for(std::size_t b=0;b<a;++b)if(std::abs(engr.toneY[a]-engr.toneY[b])==6)engr.headX[a]+=engr.stemUp?10:-10;
                    engr.stemX=x+(engr.stemUp?6:-6);
                    const qreal extreme=engr.toneY.empty()?y+24:engr.stemUp?*std::min_element(engr.toneY.begin(),engr.toneY.end()):*std::max_element(engr.toneY.begin(),engr.toneY.end());
                    engr.stemTip=extreme+(engr.stemUp?-36:36);
                    engr.hitBox=e.isRest()?QRectF(x-14,y+8,28,36):QRectF(x-18,*std::min_element(engr.toneY.begin(),engr.toneY.end())-10,36,*std::max_element(engr.toneY.begin(),engr.toneY.end())-*std::min_element(engr.toneY.begin(),engr.toneY.end())+20);
                    out.events.push_back(std::move(engr));
                    tick+=e.ticks;
                }
            }
        }
        // Tuplets form their own beam groups, even when they span meter beats.
        for(std::size_t staff=0;staff<score.staffCount();++staff)for(std::size_t m=place.first;m<place.first+place.count;++m) {
            std::vector<std::size_t> run;
            auto flush=[&] {
                if(run.size()<2) {
                    run.clear();
                    return;
                }
                bool up=true;
                qreal toneMean=0;
                std::size_t toneCount=0;
                for(auto i:run)for(auto yy:out.events[i].toneY) {
                    toneMean+=yy;
                    toneCount++;
                }
                up=toneCount==0 || toneMean/toneCount>=staffTop(staff)+24;
                const auto& first=out.events[run.front()];
                const auto& last=out.events[run.back()];
                qreal a=up?*std::min_element(first.toneY.begin(),first.toneY.end())-36:*std::max_element(first.toneY.begin(),first.toneY.end())+36;
                qreal b=up?*std::min_element(last.toneY.begin(),last.toneY.end())-36:*std::max_element(last.toneY.begin(),last.toneY.end())+36;
                const qreal slope=std::clamp((b-a)/(last.origin.x()-first.origin.x()),qreal(-0.15),qreal(0.15));
                for(auto i:run) {
                    const auto& e=out.events[i];
                    const qreal predicted=a+slope*(e.origin.x()-first.origin.x());
                    const qreal limit=up?*std::min_element(e.toneY.begin(),e.toneY.end())-30:*std::max_element(e.toneY.begin(),e.toneY.end())+30;
                    if(up && predicted>limit)a-=predicted-limit;
                    if(!up && predicted<limit)a+=limit-predicted;
                }
                for(auto i:run) {
                    auto& e=out.events[i];
                    e.stemUp=up;
                    e.stemX=e.origin.x()+(up?6:-6);
                    e.stemTip=a+slope*(e.origin.x()-first.origin.x());
                    e.beamed=true;
                }
                for(int level=0;level<3;++level)for(std::size_t r=0;r<run.size();++r) {
                    const auto& e=out.events[run[r]];
                    if(flags(Score::baseDuration(e.event.writtenDuration()))<=level)continue;
                    const qreal offset=(up?1:-1)*level*7;
                    if(r+1<run.size() && flags(Score::baseDuration(out.events[run[r+1]].event.writtenDuration()))>level) {
                        const auto& n=out.events[run[r+1]];
                        out.beams.push_back(BeamLine {
                            QPointF(e.stemX,e.stemTip+offset),QPointF(n.stemX,n.stemTip+offset),4
                        });
                    }
                    else if(level>0 && (r==0 || flags(Score::baseDuration(out.events[run[r-1]].event.writtenDuration()))<=level)) {
                        const qreal dx=r+1<run.size()?10:-10;
                        out.beams.push_back(BeamLine {
                            QPointF(e.stemX,e.stemTip+offset),QPointF(e.stemX+dx,e.stemTip+offset+slope*dx),4
                        });
                    }
                }
                run.clear();
            };
            std::size_t beat=0;
            Tick beatEnd=score.bar(m).meter.groups[0]*quarter*4/score.bar(m).meter.denominator;
            std::optional<std::pair<TupletId,std::size_t>> previousGroup;
            for(std::size_t i=0;i<out.events.size();++i) {
                const auto& e=out.events[i];
                if(e.staff!=staff || e.measureIndex!=m)continue;
                while(e.start>=beatEnd && beat+1<score.bar(m).meter.groups.size())
                    beatEnd+=score.bar(m).meter.groups[++beat]*quarter*4/score.bar(m).meter.denominator;
                const auto group=std::pair{e.event.tuplet,e.event.tuplet?std::size_t{0}:beat};
                if(previousGroup && *previousGroup!=group)flush();
                previousGroup=group;
                if(e.event.isRest() || !flags(Score::baseDuration(e.event.writtenDuration())))flush();
                else run.push_back(i);
            }
            flush();
        }
        for(std::size_t staff=0;staff<score.staffCount();++staff)for(std::size_t m=place.first;m<place.first+place.count;++m)for(const auto& t:score.measure(m,staff).tuplets) {
            const EngravedEvent* first=nullptr;
            const EngravedEvent* last=nullptr;
            qreal topEdge=staffTop(staff),bottomEdge=staffTop(staff)+48;
            bool sharedBeam=true;
            std::size_t members=0;
            for(const auto& e:out.events)if(e.staff==staff && e.measureIndex==m && e.event.tuplet==t.id) {
                if(!first)first=&e;
                last=&e;
                ++members;
                sharedBeam=sharedBeam && !e.event.isRest() && e.beamed;
                topEdge=std::min(topEdge,e.hitBox.top());
                bottomEdge=std::max(bottomEdge,e.hitBox.bottom());
                if(!e.event.isRest()) {
                    topEdge=std::min(topEdge,e.stemTip-3);
                    bottomEdge=std::max(bottomEdge,e.stemTip+3);
                }
            }
            if(!first || !last)continue;
            TupletMark mark;
            mark.id=t.id;
            mark.bracket=!sharedBeam || members<2;
            mark.above=first->event.isRest() || first->stemUp;
            qreal y=mark.above?topEdge-18:bottomEdge+18;
            const qreal a=first->origin.x()-14,b=std::max(last->origin.x()+14,a+50);
            if(mark.above) {
                for(const auto& text:out.marks)if(text.tempo && QRectF(QPointF(a,y-10),QPointF(b,y+6)).intersects(text.hitBox))y=std::min(y,text.hitBox.top()-16);
                if(y<14) { mark.above=false; y=bottomEdge+18; }
            }
            mark.a={a,y};
            mark.b={b,y};
            mark.numberBox=QRectF((a+b)/2-12,y-10,24,20);
            mark.text=QString::number(t.actual);
            out.tuplets.push_back(std::move(mark));
        }
        for(const auto& e:out.events)for(std::size_t t=0;t<e.event.pitches.size();++t) {
            const qreal y=e.toneY[t]+(e.stemUp?8:-8);
            const bool above=!e.stemUp;
            if(e.event.tieTarget(t)) {
                const auto target=std::find_if(out.events.begin(),out.events.end(),[&](const EngravedEvent& n) {
                    return n.event.id==e.event.tieTarget(t);
                });
                if(target!=out.events.end()) {
                    const auto pitch=std::find(target->event.pitches.begin(),target->event.pitches.end(),e.event.pitches[t]);
                    const auto n=pitch-target->event.pitches.begin();
                    out.ties.push_back(TieCurve {
                        QPointF(e.headX[t]+8,y),QPointF(target->headX[n]-8,target->toneY[n]+(above?-8:8)),above
                    });
                }
                else out.ties.push_back(TieCurve {
                    QPointF(e.headX[t]+8,y),QPointF(out.width-20,y),above
                });
            }
            const auto* prev=score.event(score.previousEventId(e.event.id));
            if(prev && score.locate(prev->id)->measure<place.first)for(std::size_t p=0;p<prev->pitches.size();++p)if(prev->tieTarget(p)==e.event.id && prev->pitches[p]==e.event.pitches[t])out.ties.push_back(TieCurve {
                QPointF(left+35,y),QPointF(e.headX[t]-8,y),above
            });
        }
        for(const auto& part:score.parts()) {
            std::vector<std::size_t> members;
            for(std::size_t s=0;s<score.staffCount();++s)if(score.staves()[s].part==part.id)members.push_back(s);
            if(members.size()>1)out.braces.emplace_back(staffTop(members.front()),staffTop(members.back())+48);
        }
        for(const auto& group:score.groups()) {
            std::size_t first=score.staffCount(),last=0;
            for(std::size_t s=0;s<score.staffCount();++s)if(std::find(group.parts.begin(),group.parts.end(),score.staves()[s].part)!=group.parts.end()) {
                first=std::min(first,s);
                last=std::max(last,s);
            }
            if(first<score.staffCount())out.brackets.emplace_back(staffTop(first),staffTop(last)+48);
        }
        return out;
    }
    QImage Engraving::render(const SystemSnapshot& sys,qreal scale) {
        QImage image(QSize(std::ceil(sys.width*scale),std::ceil(sys.height*scale)),QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor("#fffefa"));
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale,scale);
        const QColor ink("#172638");
        p.setPen(QPen(ink,1.1));
        for(std::size_t s=0;s<sys.labels.size();++s) {
            const qreal t=staffTop(s);
            for(int line=0;line<5;++line)p.drawLine(QPointF(left,t+line*12),QPointF(sys.width-20,t+line*12));
            p.setFont(QFont("DejaVu Sans",9));
            p.drawText(QRectF(6,t+4,126,40),Qt::AlignRight|Qt::AlignVCenter|Qt::TextWordWrap,sys.labels[s]);
        }
        for(const auto& region:sys.regions) {
            p.drawLine(QPointF(region.rect.right(),region.rect.y()+28),QPointF(region.rect.right(),region.rect.y()+76));
            if(region.staff==0) {
                p.setFont(QFont("DejaVu Sans",9));
                p.drawText(QPointF(region.rect.x()+5,region.rect.y()-4),QString::number(region.measure+1));
            }
        }
        for(const auto& c:sys.clefs) {
            glyph(p,clefGlyph(c.clef),c.position,c.small?43:c.clef==Clef::Treble || c.clef==Clef::Treble8va?60:48);
            if(c.clef==Clef::Treble8va || c.clef==Clef::Bass8vb) {
                p.setFont(QFont("DejaVu Sans",8));
                p.drawText(c.position+QPointF(-4,c.clef==Clef::Treble8va?-44:32),"8");
            }
        }
        for(const auto& k:sys.keys)for(const auto& [point,alter]:k.glyphs)glyph(p,alter>0?0xE262:alter<0?0xE260:0xE261,point,36);
        for(const auto& mark:sys.marks) {
            if(mark.tempo) {
                QFont f("Bravura");
                f.setPixelSize(24);
                p.setFont(f);
                p.drawText(mark.position,mark.text);
            }
            else {
                const auto nums=mark.text.split('\n');
                p.setFont(QFont("DejaVu Sans",19,QFont::Bold));
                p.drawText(mark.position+QPointF(0,23),nums[0]);
                p.drawText(mark.position+QPointF(0,47),nums[1]);
            }
        }
        for(const auto& [a,b]:sys.brackets) {
            p.setPen(QPen(ink,3));
            p.drawLine(QPointF(left-28,a),QPointF(left-28,b));
            p.drawLine(QPointF(left-28,a),QPointF(left-18,a));
            p.drawLine(QPointF(left-28,b),QPointF(left-18,b));
        }
        for(const auto& [a,b]:sys.braces) {
            QPainterPath path;
            const qreal x=left-12,mid=(a+b)/2;
            path.moveTo(x,a);
            path.cubicTo(x-20,a+10,x+8,mid-12,x-10,mid);
            path.cubicTo(x+8,mid+12,x-20,b-10,x,b);
            p.setPen(QPen(ink,2.1));
            p.drawPath(path);
        }
        for(const auto& e:sys.events) {
            p.setPen(QPen(ink,1.4));
            const Tick base=Score::baseDuration(e.event.writtenDuration());
            if(e.event.isRest()) {
                const char16_t code=base==(4*quarter)?0xE4E3:base==(2*quarter)?0xE4E4:base==quarter?0xE4E5:base==(quarter/2)?0xE4E6:base==(quarter/4)?0xE4E7:0xE4E8;
                const qreal restY=e.staffTop+(base>=(2*quarter)?base==(4*quarter)?15:21:24);
                glyph(p,code,QPointF(e.origin.x(),restY),48);
                if(e.event.writtenDuration()!=base) {
                    p.setBrush(ink);
                    p.drawEllipse(QPointF(e.origin.x()+16,e.staffTop+25),2,2);
                }
                continue;
            }
            for(std::size_t tone=0;tone<e.toneY.size();++tone) {
                const qreal x=e.headX[tone],y=e.toneY[tone];
                ledger(p,x,y,e.staffTop);
                glyph(p,base==(4*quarter)?0xE0A2:base>=(2*quarter)?0xE0A3:0xE0A4,QPointF(x,y),43);
                if(e.shownAccidentals[tone]!=99) {
                    const int a=e.shownAccidentals[tone];
                    glyph(p,a==-2?0xE264:a==2?0xE263:a<0?0xE260:a>0?0xE262:0xE261,QPointF(e.origin.x()-20-14*(tone%3),y),34);
                }
                if(e.event.writtenDuration()!=base) {
                    p.setBrush(ink);
                    const bool onLine=std::fmod(std::abs(y-e.staffTop),12)<1;
                    p.drawEllipse(QPointF(e.origin.x()+17,y+(onLine?-6:0)),2.1,2.1);
                }
            }
            if(base<(4*quarter)) {
                const qreal from=e.stemUp?*std::max_element(e.toneY.begin(),e.toneY.end()):*std::min_element(e.toneY.begin(),e.toneY.end());
                p.drawLine(QPointF(e.stemX,from),QPointF(e.stemX,e.stemTip));
                if(!e.beamed)for(int f=0;f<flags(base);++f) {
                    QPainterPath flag;
                    const qreal sign=e.stemUp?1:-1,at=e.stemTip+sign*f*8;
                    flag.moveTo(e.stemX,at);
                    flag.cubicTo(e.stemX+12,at+sign*6,e.stemX+14,at+sign*15,e.stemX+5,at+sign*23);
                    p.drawPath(flag);
                }
            }
        }
        for(const auto& beam:sys.beams) {
            p.setPen(QPen(ink,beam.width,Qt::SolidLine,Qt::FlatCap));
            p.drawLine(beam.a,beam.b);
        }
        for(const auto& mark:sys.tuplets) {
            p.setPen(QPen(ink,1.3));
            if(mark.bracket) {
                const qreal hook=mark.above?6:-6;
                p.drawLine(mark.a+QPointF(0,hook),mark.a);
                p.drawLine(mark.a,QPointF(mark.numberBox.left()-3,mark.a.y()));
                p.drawLine(QPointF(mark.numberBox.right()+3,mark.b.y()),mark.b);
                p.drawLine(mark.b,mark.b+QPointF(0,hook));
            }
            QFont font("DejaVu Sans");
            font.setPixelSize(16);
            font.setItalic(true);
            p.setFont(font);
            p.drawText(mark.numberBox,Qt::AlignCenter,mark.text);
        }
        for(const auto& tie:sys.ties) {
            QPainterPath path;
            const qreal arch=(tie.above?-1:1)*std::min(qreal(16),std::max(qreal(7),(tie.b.x()-tie.a.x())*0.12));
            path.moveTo(tie.a);
            path.cubicTo(tie.a+QPointF((tie.b.x()-tie.a.x())/3,arch),tie.b+QPointF(-(tie.b.x()-tie.a.x())/3,arch),tie.b);
            p.setPen(QPen(ink,1.8));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        return image;
    }
    QImage Engraving::renderSelection(const SystemSnapshot& sys,EventId id,std::size_t tone,QRectF& bounds) {
        bounds={};
        const auto it=std::find_if(sys.events.begin(),sys.events.end(),[id](const EngravedEvent& e) {
            return e.event.id==id;
        });
        if(it==sys.events.end())return {};
        const auto& e=*it;
        const Tick base=Score::baseDuration(e.event.writtenDuration());
        QPointF center;
        char16_t code;
        if(e.event.isRest()) {
            center={e.origin.x(),e.staffTop+(base>=(2*quarter)?base==(4*quarter)?15:21:24)};
            code=base==(4*quarter)?0xE4E3:base==(2*quarter)?0xE4E4:base==quarter?0xE4E5:base==(quarter/2)?0xE4E6:base==(quarter/4)?0xE4E7:0xE4E8;
        }
        else {
            if(tone>=e.toneY.size())return {};
            center={e.headX[tone],e.toneY[tone]};
            code=base==(4*quarter)?0xE0A2:base>=(2*quarter)?0xE0A3:0xE0A4;
        }
        bounds=QRectF(center-QPointF(24,30),QSizeF(48,60));
        QImage image(48,60,QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor("#d96b16"));
        glyph(painter,code,{24,30},e.event.isRest()?48:43);
        return image;
    }
    std::optional<ScoreHit> Engraving::hit(const SystemSnapshot& sys,QPointF point) {
        for(const auto& mark:sys.marks)if(mark.hitBox.contains(point)) {
            std::size_t staff=0;
            for(const auto& r:sys.regions)if(r.measure==mark.measure && r.rect.contains(point))staff=r.staff;
            return ScoreHit {
                mark.tempo?HitKind::Tempo:HitKind::Meter,0,0,staff,mark.measure
            };
        }
        for(const auto& k:sys.keys)if(!k.glyphs.empty() && k.hitBox.contains(point))return ScoreHit{HitKind::Key,0,0,k.staff,k.measure,k.tick};
        for(const auto& c:sys.clefs)if(c.hitBox.contains(point))return ScoreHit {
            HitKind::Clef,0,0,c.staff,c.measure,c.tick
        };
        for(const auto& e:sys.events)if(e.hitBox.contains(point)) {
            std::size_t tone=0;
            qreal distance=1e9;
            for(std::size_t i=0;i<e.toneY.size();++i) {
                const qreal d=std::abs(point.y()-e.toneY[i])+std::abs(point.x()-e.headX[i])*0.2;
                if(d<distance) {
                    distance=d;
                    tone=i;
                }
            }
            return ScoreHit {
                HitKind::Note,e.event.id,tone,e.staff,e.measureIndex
            };
        }
        for(const auto& r:sys.regions)if(r.rect.contains(point))return ScoreHit {
            HitKind::Measure,0,0,r.staff,r.measure
        };
        return {};
    }
    std::optional<std::pair<EventId,std::size_t>> Engraving::hitTest(const SystemSnapshot& sys,QPointF point) {
        const auto h=hit(sys,point);
        if(h && h->kind==HitKind::Note)return std::pair {
            h->event,h->tone
        };
        return {};
    }
    QPointF Engraving::tickPosition(Tick tick) {
        Score score(std::max<Tick>(1,tick/measureTicks+1));
        LayoutIndex index;
        index.reset(score);
        return index.position(score,tick);
    }
}
// namespace music

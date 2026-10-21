#include "Score.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_set>

namespace music {
namespace {
constexpr std::size_t maxEvents=250'000;
constexpr std::array<Tick,12> writtenValues{
    6*quarter,4*quarter,3*quarter,2*quarter,3*quarter/2,quarter,
    3*quarter/4,quarter/2,3*quarter/8,quarter/4,3*quarter/16,quarter/8
};

std::optional<std::vector<std::size_t>> staffIndices(const Score& score,const std::vector<StaffId>& ids)
{
    if(ids.empty() || ids.size()>score.staffCount())return {};
    std::vector<std::size_t> result;
    for(auto id:ids) {
        auto index=score.staffIndex(id);
        if(!index)return {};
        result.push_back(*index);
    }
    std::sort(result.begin(),result.end());
    if(std::adjacent_find(result.begin(),result.end())!=result.end())return {};
    return result;
}

struct Spelling {
    int actual=1,normal=1;
    Tick unit=0;
    std::vector<Tick> values;
};

std::optional<Spelling> spell(Tick ticks,int actual,int normal,Tick unit)
{
    if(ticks<=0 || actual<=0 || normal<=0 || ticks>6*quarter*static_cast<Tick>(maxEvents) ||
       ticks>std::numeric_limits<Tick>::max()/actual || ticks*actual%normal)return {};
    const Tick written=ticks*actual/normal,quantum=quarter/16;
    if(written%quantum)return {};
    Spelling result{actual,normal,unit,{}};
    Tick remainder=written;
    while(remainder>6*quarter) { result.values.push_back(4*quarter); remainder-=4*quarter; }
    const auto count=static_cast<std::size_t>(remainder/quantum);
    std::vector<int> cost(count+1,1000000),choice(count+1,-1);
    cost[0]=0;
    for(std::size_t i=1;i<=count;++i)for(std::size_t v=0;v<writtenValues.size();++v) {
        const auto size=static_cast<std::size_t>(writtenValues[v]/quantum);
        if(size<=i && cost[i-size]+1<cost[i]) {
            cost[i]=cost[i-size]+1;
            choice[i]=static_cast<int>(v);
        }
    }
    if(choice[count]<0)return {};
    for(auto i=count;i;) {
        const auto value=writtenValues[choice[i]];
        result.values.push_back(value);
        i-=value/quantum;
    }
    if(actual!=1 && !unit) {
        result.unit=Score::baseDuration(result.values.front());
        while(result.unit*normal<ticks && result.unit<4*quarter)result.unit*=2;
    }
    return result;
}

std::optional<Spelling> spell(Tick ticks,const Tuplet* group)
{
    if(group)return spell(ticks,group->actual,group->normal,group->unit);
    // Try ordinary spelling first, then one supported ratio, without nesting.
    for(const auto& [actual,normal]:{std::pair{1,1},{3,2},{5,4},{7,4},{5,3},{7,6}})
        if(auto result=spell(ticks,actual,normal,0))return result;
    return {};
}
}

std::optional<ScoreFragment> normalizeFragment(const ScoreFragment& fragment,std::string* error)
{
    auto fail=[&](const char* message)->std::optional<ScoreFragment> { if(error)*error=message; return {}; };
    if(fragment.duration<=0 || fragment.duration>2'000'000*quarter || fragment.rows.size()!=1 ||
       fragment.rows.front().empty() || fragment.rows.front().size()>maxEvents)
        return fail("The proposed passage is empty or exceeds the result limits.");
    ScoreFragment result{fragment.duration,{{}}};
    auto& row=result.rows.front();
    Tick total=0;
    TupletId nextGroup=0,activeOriginal=0,activeGroup=0;
    Tick activeLength=0,activeUnit=0;
    int activeActual=1,activeNormal=1;
    for(const auto& e:fragment.rows.front()) {
        if(e.ticks<=0 || e.ticks>fragment.duration-total || e.pitches.size()>16 || e.ties.size()!=e.pitches.size())
            return fail("The proposed passage has invalid event timing or chord data.");
        const Tuplet hint{1,0,e.actual,e.normal,e.unit};
        if((e.group && !hint.valid()) || (!e.group && (e.actual!=1 || e.normal!=1 || e.unit)))
            return fail("The proposed passage has an unsupported tuplet.");
        auto spelling=spell(e.ticks,e.group?&hint:nullptr);
        if(!spelling)return fail("The requested rhythm cannot be represented exactly with supported note values and tuplets.");
        for(std::size_t i=0;i<spelling->values.size();++i) {
            const Tick ticks=spelling->values[i]*spelling->normal/spelling->actual;
            FragmentEvent piece{ticks,e.pitches,e.ties};
            if(i+1<spelling->values.size())piece.ties.assign(e.pitches.size(),true);
            if(spelling->actual!=1) {
                if(!activeGroup || activeOriginal!=e.group || activeUnit!=spelling->unit ||
                   activeActual!=spelling->actual || activeNormal!=spelling->normal ||
                   activeLength+ticks>activeUnit*activeNormal) {
                    activeGroup=++nextGroup;
                    activeLength=0;
                }
                activeOriginal=e.group;
                activeUnit=spelling->unit;
                activeActual=spelling->actual;
                activeNormal=spelling->normal;
                activeLength+=ticks;
                piece.group=activeGroup;
                piece.unit=activeUnit;
                piece.actual=activeActual;
                piece.normal=activeNormal;
            }
            else { activeGroup=0; activeLength=0; }
            row.push_back(std::move(piece));
            if(row.size()>maxEvents)return fail("The transformed result contains too many notation events.");
        }
        total+=e.ticks;
    }
    if(total!=fragment.duration || !result.valid())return fail("The proposed passage has invalid pitches, ties or duration.");
    return result;
}

bool ScoreFragment::valid() const
{
    if(duration<=0 || rows.empty() || rows.size()>1024)return false;
    std::size_t count=0;
    for(const auto& row:rows) {
        Tick total=0;
        if(row.empty())return false;
        for(std::size_t i=0;i<row.size();++i) {
            const auto& e=row[i];
            if(++count>maxEvents || e.ticks<=0 || e.ticks>6*quarter || e.ticks>duration-total ||
               e.pitches.size()>16 || e.ties.size()!=e.pitches.size())return false;
            if(e.group) {
                if(!Tuplet{1,0,e.actual,e.normal,e.unit}.valid())return false;
            }
            else if(e.actual!=1 || e.normal!=1 || e.unit)return false;
            for(std::size_t p=0;p<e.pitches.size();++p) {
                const auto pitch=e.pitches[p];
                if(pitch.step<0 || pitch.step>6 || pitch.octave< -1 || pitch.octave>9 || pitch.alter< -2 || pitch.alter>2 || pitch.midi()<0 || pitch.midi()>127)return false;
                for(std::size_t q=0;q<p;++q)if(e.pitches[q].midi()==pitch.midi())return false;
                if(e.ties[p] && (i+1==row.size() || std::find(row[i+1].pitches.begin(),row[i+1].pitches.end(),pitch)==row[i+1].pitches.end()))return false;
            }
            total+=e.ticks;
        }
        if(total!=duration)return false;
    }
    return true;
}

EventId Score::eventAt(std::size_t staff,Tick tick) const
{
    if(staff>=staffCount() || tick<0 || tick>=totalTicks())return 0;
    const auto m=measureAt(tick);
    Tick at=measureStart(m);
    for(const auto& e:measure(m,staff).events) {
        if(tick<at+e.ticks)return e.id;
        at+=e.ticks;
    }
    return 0;
}

std::optional<ScoreFragment> Score::copyRange(const ScoreRange& range) const
{
    const auto indices=staffIndices(*this,range.staves);
    if(!indices || range.start<0 || range.end<=range.start || range.end>totalTicks())return {};
    ScoreFragment fragment{range.end-range.start,{}};
    for(auto s:*indices) {
        std::vector<FragmentEvent> row;
        for(std::size_t m=measureAt(range.start);m<measureCount() && measureStart(m)<range.end;++m) {
            Tick at=measureStart(m);
            for(const auto& e:measure(m,s).events) {
                const Tick begin=std::max(at,range.start),end=std::min(at+e.ticks,range.end);
                if(begin<end) {
                    FragmentEvent copied{end-begin,e.pitches,std::vector<bool>(e.pitches.size())};
                    if(const auto* group=tupletFor(e.id)) {
                        copied.actual=group->actual;
                        copied.normal=group->normal;
                        copied.unit=group->unit;
                        copied.group=group->id;
                    }
                    if(end==at+e.ticks && end<range.end)for(std::size_t p=0;p<e.pitches.size();++p)copied.ties[p]=e.tieTarget(p)!=0;
                    row.push_back(std::move(copied));
                }
                at+=e.ticks;
            }
        }
        fragment.rows.push_back(std::move(row));
    }
    return fragment.valid()?std::optional{std::move(fragment)}:std::nullopt;
}

std::optional<LocalEdit> Score::pasteRange(const ScoreFragment& fragment,const ScoreRange& destination,std::string* error)
{
    auto fail=[&](const char* message)->std::optional<LocalEdit> { if(error)*error=message; return {}; };
    const auto indices=staffIndices(*this,destination.staves);
    if(!fragment.valid())return fail("The clipboard does not contain a valid musical passage.");
    if(!indices || indices->size()!=fragment.rows.size())return fail("Select exactly as many destination staves as copied staves.");
    if(destination.start<0 || destination.start>totalTicks() || fragment.duration>std::numeric_limits<Tick>::max()-destination.start)
        return fail("The paste location is outside the score's time range.");
    const Tick start=destination.start,end=start+fragment.duration;
    const auto oldCount=measureCount();
    const Tick finalLength=bars_.back().meter.ticks();
    const Tick extra=end>totalTicks()?(end-totalTicks()-1)/finalLength+1:0;
    if(extra>500'000/static_cast<Tick>(staffCount()))return fail("This paste would require too many new measures.");
    const auto newCount=oldCount+static_cast<std::size_t>(extra);
    auto at=[&](std::size_t m) { return m<=oldCount?measureStart(m):totalTicks()+static_cast<Tick>(m-oldCount)*finalLength; };
    auto length=[&](std::size_t m) { return m<oldCount?bar(m).meter.ticks():finalLength; };
    auto barFor=[&](Tick tick) { return tick<totalTicks()?measureAt(tick):oldCount+static_cast<std::size_t>((tick-totalTicks())/finalLength); };
    const auto first=barFor(start),last=barFor(end-1);
    const auto firstTouched=first?first-1:0,lastTouched=std::min(newCount-1,last+1);
    EventId nextEvent=nextId_;
    auto nextObject=nextObject_;
    LocalEdit change;
    for(std::size_t m=oldCount;m<newCount;++m) {
        BarBundle bundle{Bar{bars_.back().meter,false,{}},{}};
        for(std::size_t s=0;s<staffCount();++s) {
            Measure rests;
            Tick remaining=finalLength;
            while(remaining) {
                const Tick value=largestDuration(remaining);
                if(!value)return fail("The destination meter cannot be filled with supported rests.");
                rests.events.push_back(Event{nextEvent++,value,{},{}});
                remaining-=value;
            }
            bundle.measures.push_back(std::move(rests));
        }
        change.appended.push_back(std::move(bundle));
    }
    auto oldMeasure=[&](std::size_t m,std::size_t s)->const Measure& {
        return m<oldCount?measure(m,s):change.appended[m-oldCount].measures[s];
    };
    for(std::size_t rowIndex=0;rowIndex<indices->size();++rowIndex) {
        const auto s=(*indices)[rowIndex];
        std::map<std::size_t,Measure> prepared;
        struct Piece {
            Event event;
            std::size_t measure=0;
            bool pasted=false;
            std::uint64_t origin=0;
            Tick begin=0,end=0,sourceLength=0;
            std::vector<std::uint64_t> outgoing;
        };
        std::vector<Piece> pieces;
        std::unordered_set<EventId> usedEvents;
        std::unordered_set<TupletId> usedGroups;
        using GroupKey=std::tuple<bool,int,std::uint64_t,int,int,Tick>;
        std::optional<GroupKey> activeGroup;
        std::size_t activeMeasure=std::numeric_limits<std::size_t>::max(),activeIndex=0;
        auto emitSlice=[&](std::size_t m,Tick destinationTick,Tick begin,Tick endOffset,Tick sourceLength,
                           const std::vector<Pitch>& pitches,const std::vector<std::uint64_t>& outgoing,
                           std::uint64_t origin,bool pasted,const Tuplet* group)->bool {
            const auto spelling=spell(endOffset-begin,group);
            if(!spelling)return false;
            Tick offset=begin;
            for(Tick written:spelling->values) {
                const Tick ticks=written*spelling->normal/spelling->actual;
                Event e{!pasted && usedEvents.insert(origin).second?origin:nextEvent++,ticks,pitches,{}};
                if(spelling->actual!=1) {
                    GroupKey key{pasted,group?1:2,group?group->id:origin,spelling->actual,spelling->normal,spelling->unit};
                    auto& cell=prepared[m];
                    if(activeMeasure==m && activeGroup && (!group || std::get<1>(*activeGroup)==2)) {
                        auto& previous=cell.tuplets[activeIndex];
                        const Tick reference=group?group->unit:previous.unit;
                        if(previous.actual==spelling->actual && previous.normal==spelling->normal &&
                           previous.length+(endOffset-offset)<=reference*previous.normal) {
                            previous.unit=reference;
                            key=*activeGroup;
                        }
                    }
                    if(activeMeasure!=m || !activeGroup || *activeGroup!=key) {
                        const auto id=!pasted && group && usedGroups.insert(group->id).second?group->id:nextObject++;
                        cell.tuplets.push_back(Tuplet{id,destinationTick-at(m),spelling->actual,spelling->normal,spelling->unit,0});
                        activeIndex=cell.tuplets.size()-1;
                        activeGroup=key;
                        activeMeasure=m;
                    }
                    auto& t=cell.tuplets[activeIndex];
                    t.length+=ticks;
                    e.tuplet=t.id;
                    e.written=written;
                }
                else activeGroup.reset();
                pieces.push_back(Piece{std::move(e),m,pasted,origin,offset,offset+ticks,sourceLength,outgoing});
                offset+=ticks;
                destinationTick+=ticks;
            }
            return true;
        };
        auto retain=[&](std::size_t m,Tick begin,Tick endOffset)->bool {
            Tick tick=0;
            const auto& old=oldMeasure(m,s);
            for(const auto& e:old.events) {
                const Tick a=std::max(begin,tick),b=std::min(endOffset,tick+e.ticks);
                if(a<b) {
                    const Tuplet* group=nullptr;
                    if(e.tuplet)for(const auto& t:old.tuplets)if(t.id==e.tuplet)group=&t;
                    auto emit=[&](Tick from,Tick to) {
                        return from==to || emitSlice(m,at(m)+from,from-tick,to-tick,e.ticks,e.pitches,e.ties,e.id,false,group);
                    };
                    if(group || spell(b-a,1,1,0)) {
                        if(!emit(a,b))return false;
                    }
                    else {
                        // Confine inferred tuplets to the cut beat, retaining ordinary music beyond it.
                        const auto& meter=m<oldCount?bar(m).meter:bars_.back().meter;
                        std::vector<Tick> boundaries{0};
                        for(int beat:meter.groups)boundaries.push_back(boundaries.back()+beat*quarter*4/meter.denominator);
                        const auto firstBoundary=std::lower_bound(boundaries.begin(),boundaries.end(),a);
                        const auto lastBoundary=std::upper_bound(boundaries.begin(),boundaries.end(),b);
                        if(firstBoundary!=boundaries.end() && lastBoundary!=boundaries.begin() && *firstBoundary<*std::prev(lastBoundary)) {
                            const Tick left=*firstBoundary,right=*std::prev(lastBoundary);
                            if(!emit(a,left) || !emit(left,right) || !emit(right,b))return false;
                        }
                        else if(!emit(a,b))return false;
                    }
                }
                tick+=e.ticks;
            }
            return true;
        };
        const auto& row=fragment.rows[rowIndex];
        std::size_t sourceIndex=0;
        Tick sourceStart=0;
        for(std::size_t m=firstTouched;m<=lastTouched;++m) {
            const Tick barStart=at(m),barEnd=barStart+length(m);
            const Tick a=std::max(start,barStart),b=std::min(end,barEnd);
            if(a>=b) {
                if(!retain(m,0,length(m)))return fail("The paste would require unsupported rhythmic splitting.");
                continue;
            }
            if(!retain(m,0,a-barStart))return fail("The paste would require unsupported rhythmic splitting.");
            Tick position=a-start;
            while(sourceIndex<row.size() && sourceStart+row[sourceIndex].ticks<=position)sourceStart+=row[sourceIndex++].ticks;
            while(position<b-start) {
                const auto& e=row[sourceIndex];
                const Tick stop=std::min(b-start,sourceStart+e.ticks);
                std::vector<std::uint64_t> outgoing(e.pitches.size());
                for(std::size_t p=0;p<e.pitches.size();++p)if(e.ties[p])outgoing[p]=sourceIndex+2;
                const Tuplet context{e.group,0,e.actual,e.normal,e.unit};
                if(!emitSlice(m,start+position,position-sourceStart,stop-sourceStart,e.ticks,e.pitches,outgoing,sourceIndex+1,true,e.group?&context:nullptr))
                    return fail("The paste would require unsupported rhythmic splitting; no music was changed.");
                position=stop;
                if(position==sourceStart+e.ticks) { sourceStart=position; ++sourceIndex; }
            }
            if(!retain(m,b-barStart,length(m)))return fail("The paste would require unsupported rhythmic splitting.");
        }
        for(std::size_t i=0;i<pieces.size();++i) {
            auto& p=pieces[i];
            if(!p.event.isRest()) {
                p.event.ties.assign(p.event.pitches.size(),0);
                if(i+1<pieces.size()) {
                    const auto& next=pieces[i+1];
                    for(std::size_t tone=0;tone<p.event.pitches.size();++tone) {
                        const bool same=p.pasted==next.pasted && p.origin==next.origin && p.end==next.begin;
                        const bool linked=p.pasted==next.pasted && p.end==p.sourceLength && !next.begin &&
                            tone<p.outgoing.size() && p.outgoing[tone]==next.origin;
                        if((same || linked) && std::find(next.event.pitches.begin(),next.event.pitches.end(),p.event.pitches[tone])!=next.event.pitches.end())p.event.ties[tone]=next.event.id;
                    }
                }
                else if(!p.pasted && p.end==p.sourceLength)p.event.ties=p.outgoing;
                if(std::none_of(p.event.ties.begin(),p.event.ties.end(),[](EventId id){return id!=0;}))p.event.ties.clear();
            }
            prepared[p.measure].events.push_back(std::move(p.event));
        }
        for(auto& [m,after]:prepared) {
            for(auto& t:after.tuplets)if(t.length==t.unit*t.normal)t.length=0;
            if(!validMeasure(after,length(m)))return fail("The paste would require unsupported rhythmic splitting.");
            if(m<oldCount) {
                if(after!=measure(m,s))change.measures.push_back(MeasureEdit{s,m,measure(m,s),std::move(after)});
            }
            else change.appended[m-oldCount].measures[s]=std::move(after);
        }
    }
    // Every replacement is validated before any document state is changed.
    for(const auto& patch:change.measures)restoreMeasure(patch.measure,patch.after,patch.staff);
    for(const auto& bundle:change.appended)restoreBundle(bundle);
    nextId_=std::max(nextId_,nextEvent);
    nextObject_=std::max(nextObject_,nextObject);
    return change;
}
}

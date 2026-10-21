#include "Score.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>
namespace music {
    namespace {
        constexpr std::array<Tick, 12> durations {
            6*quarter,4*quarter,3*quarter,2*quarter,3*quarter/2,quarter,
            3*quarter/4,quarter/2,3*quarter/8,quarter/4,3*quarter/16,quarter/8
        };
        constexpr std::array<int,7> semitones {
            0,2,4,5,7,9,11
        };
        bool validPitch(const Pitch& p) {
            return p.step>=0 && p.step<7 && p.octave>=-1 && p.octave<=9 && p.alter>=-2 && p.alter<=2 && p.midi()>=0 && p.midi()<=127;
        }
    }
    int Pitch::midi() const {
        return step<0 || step>6 ? -1 : (octave+1)*12+semitones[step]+alter;
    }
    bool Meter::valid() const {
        return numerator>=1 && numerator<=32 && (denominator==2 || denominator==4 || denominator==8 || denominator==16 || denominator==32) &&
        !groups.empty() && std::all_of(groups.begin(),groups.end(),[](int n) {
            return n>0;
        }) && std::accumulate(groups.begin(),groups.end(),0)==numerator;
    }
    bool TempoMark::valid() const {
        return bpm>=20 && bpm<=400 && Score::baseDuration(beat)>=quarter/4;
    }
    bool Tuplet::valid() const {
        return id && start>=0 && unit>0 && Score::baseDuration(unit)==unit && length>=0 && length<=unit*normal &&
        ((actual==3 && normal==2) || (actual==5 && (normal==3 || normal==4)) ||
         (actual==7 && (normal==4 || normal==6)));
    }
    Meter Score::defaultMeter(int n,int d) {
        Meter m {
            n,d,{}
        };
        if (d>=8 && n>=3) {
            int left=n;
            const bool compound=n>=6 && n%3==0;
            while(left>3) {
                m.groups.push_back(compound?3:2);
                left-=m.groups.back();
            }
            if(left) m.groups.push_back(left);
        }
        else for(int i=0;i<n;++i)m.groups.push_back(1);
        return m;
    }
    Score::Score(std::size_t count) {
        parts_.push_back(Part {
            1,"Piano",0
        });
        staves_.push_back(Staff {
            2,1,"Piano",-1,{}, {
                {
                    0,Clef::Treble
                }
            }
        });
        count=std::max<std::size_t>(1,count);
        bars_.reserve(count);
        staves_[0].measures.reserve(count);
        indexedIds_.resize(1);
        for(std::size_t i=0;i<count;++i)appendMeasure();
        bars_[0].meterChange=true;
        bars_[0].tempo=TempoMark{};
    }
    std::optional<Location> Score::locate(EventId id) const {
        const auto it=index_.find(id);
        return it==index_.end()?std::nullopt:std::optional<Location>(it->second);
    }
    const Event* Score::event(EventId id) const {
        const auto l=locate(id);
        return l?&measure(l->measure,l->staff).events[l->event]:nullptr;
    }
    const Tuplet* Score::tupletFor(EventId id) const {
        const auto l=locate(id);
        const auto* e=event(id);
        if(!l || !e || !e->tuplet)return nullptr;
        const auto& groups=measure(l->measure,l->staff).tuplets;
        const auto it=std::find_if(groups.begin(),groups.end(),[&](const Tuplet& t) { return t.id==e->tuplet; });
        return it==groups.end()?nullptr:&*it;
    }
    Event* Score::mutableEvent(EventId id) {
        const auto l=locate(id);
        return l?&staves_[l->staff].measures[l->measure].events[l->event]:nullptr;
    }
    EventId Score::firstEventId(std::size_t m,std::size_t s) const {
        return measure(m,s).events.front().id;
    }
    EventId Score::nextEventId(EventId id) const {
        const auto l=locate(id);
        if(!l)return 0;
        const auto& e=measure(l->measure,l->staff).events;
        return l->event+1<e.size()?e[l->event+1].id:l->measure+1<measureCount()?firstEventId(l->measure+1,l->staff):0;
    }
    EventId Score::previousEventId(EventId id) const {
        const auto l=locate(id);
        if(!l)return 0;
        return l->event?measure(l->measure,l->staff).events[l->event-1].id:l->measure?measure(l->measure-1,l->staff).events.back().id:0;
    }
    Tick Score::eventStart(EventId id) const {
        const auto l=locate(id);
        if(!l)return 0;
        Tick tick=measureStart(l->measure);
        const auto& es=measure(l->measure,l->staff).events;
        for(std::size_t i=0;i<l->event;++i)tick+=es[i].ticks;
        return tick;
    }
    std::size_t Score::measureAt(Tick tick) const {
        const auto it=std::upper_bound(starts_.begin(),starts_.end(),tick);
        return std::min(measureCount()-1,static_cast<std::size_t>(std::max<std::ptrdiff_t>(0,it-starts_.begin()-1)));
    }
    Clef Score::clefAt(std::size_t s,Tick tick) const {
        const auto& cs=staves_.at(s).clefs;
        const auto it=std::upper_bound(cs.begin(),cs.end(),tick,[](Tick t,const ClefChange& c) {
            return t<c.tick;
        });
        return it==cs.begin()?Clef::Treble:std::prev(it)->clef;
    }
    TempoMark Score::tempoAt(std::size_t m) const {
        for(std::size_t i=m+1;i-->0;)if(bars_[i].tempo)return *bars_[i].tempo;
        return {};
    }
    int Score::program(std::size_t s) const {
        const auto& staff=staves_.at(s);
        if(staff.program>=0)return staff.program;
        const auto it=std::find_if(parts_.begin(),parts_.end(),[&](const Part& p) {
            return p.id==staff.part;
        });
        return it==parts_.end()?0:it->program;
    }
    std::optional<std::size_t> Score::staffIndex(StaffId id) const {
        for(std::size_t i=0;i<staffCount();++i)if(staves_[i].id==id)return i;
        return {};
    }
    bool Score::validDuration(Tick t) {
        return std::find(durations.begin(),durations.end(),t)!=durations.end();
    }
    Tick Score::baseDuration(Tick t) {
        for(Tick b:{4*quarter,2*quarter,quarter,quarter/2,quarter/4,quarter/8})if(t==b || t==b*3/2)return b;
        return 0;
    }
    Tick Score::largestDuration(Tick t) {
        for(Tick d:durations)if(d<=t)return d;
        return 0;
    }
    std::vector<Event> Score::makeRests(Tick ticks,const Tuplet* tuplet) {
        std::vector<Event> result;
        if(ticks<0 || (tuplet && ticks*tuplet->actual%tuplet->normal))return {};
        Tick written=tuplet?ticks*tuplet->actual/tuplet->normal:ticks;
        auto next=nextId_;
        while(written>0) {
            const Tick d=largestDuration(written);
            if(!d)return {};
            result.push_back(Event {
                next++,tuplet?tuplet->soundingDuration(d):d,{},{},tuplet?tuplet->id:0,tuplet?d:0
            });
            written-=d;
        }
        nextId_=next;
        return result;
    }
    bool Score::validMeasure(const Measure& m,Tick length) {
        Tick total=0;
        std::unordered_set<EventId> ids;
        std::unordered_map<TupletId,const Tuplet*> groups;
        std::unordered_map<TupletId,Tick> ends;
        for(const auto& t:m.tuplets) {
            if(!t.valid() || t.start>length || t.span()>length-t.start || !groups.emplace(t.id,&t).second)return false;
            ends[t.id]=t.start;
        }
        if(m.events.empty())return false;
        for(const auto& e:m.events) {
            if(!e.id || !ids.insert(e.id).second || e.ticks<=0 || !validDuration(e.writtenDuration()) || e.pitches.size()>16 || (!e.ties.empty() && e.ties.size()!=e.pitches.size()))return false;
            if(e.tuplet) {
                const auto it=groups.find(e.tuplet);
                if(it==groups.end() || total!=ends[e.tuplet] || it->second->soundingDuration(e.written)!=e.ticks)return false;
                ends[e.tuplet]+=e.ticks;
            }
            else if(e.written)return false;
            for(std::size_t i=0;i<e.pitches.size();++i) {
                if(!validPitch(e.pitches[i]))return false;
                for(std::size_t j=0;j<i;++j)if(e.pitches[i].midi()==e.pitches[j].midi())return false;
            }
            total+=e.ticks;
            if(total>length)return false;
        }
        for(const auto& [id,t]:groups)if(ends[id]!=t->start+t->span())return false;
        return total==length;
    }
    void Score::rebuildStarts() {
        starts_.clear();
        starts_.reserve(bars_.size()+1);
        Tick t=0;
        for(const auto& b:bars_) {
            starts_.push_back(t);
            t+=b.meter.ticks();
        }
        starts_.push_back(t);
    }
    void Score::reindexMeasure(std::size_t s,std::size_t m) {
        auto& ids=indexedIds_[s][m];
        for(auto id:ids)index_.erase(id);
        ids.clear();
        const auto& es=measure(m,s).events;
        for(const auto& t:measure(m,s).tuplets)nextObject_=std::max(nextObject_,t.id+1);
        for(std::size_t i=0;i<es.size();++i) {
            index_[es[i].id]=Location {
                m,i,s
            };
            ids.push_back(es[i].id);
            nextId_=std::max(nextId_,es[i].id+1);
        }
    }
    void Score::rebuildIndex() {
        index_.clear();
        indexedIds_.assign(staffCount(),{});
        for(std::size_t s=0;s<staffCount();++s) {
            indexedIds_[s].resize(measureCount());
            for(std::size_t m=0;m<measureCount();++m)reindexMeasure(s,m);
        }
        rebuildStarts();
    }
    void Score::cleanTiesNear(EventId id) {
        // Ties are adjacent in a staff; only neighboring measures can be affected.
        const auto l=locate(id);
        if(!l)return;
        for(std::size_t m=l->measure?l->measure-1:0;m<std::min(measureCount(),l->measure+2);++m) {
            for(auto& e:staves_[l->staff].measures[m].events) {
                const EventId next=nextEventId(e.id);
                const Event* target=event(next);
                for(std::size_t i=0;i<e.ties.size();++i)if(e.ties[i] && (e.ties[i]!=next || !target || std::find(target->pitches.begin(),target->pitches.end(),e.pitches[i])==target->pitches.end()))e.ties[i]=0;
            }
        }
    }
    bool Score::enterNote(EventId id,Pitch pitch,Tick ticks) {
        const auto l=locate(id);
        if(!l || !validPitch(pitch) || ticks<=0 || ticks>6*quarter)return false;
        auto& es=staves_[l->staff].measures[l->measure].events;
        if(!es[l->event].isRest())return false;
        const auto* group=tupletFor(id);
        if(group && ticks*group->actual%group->normal)return false;
        const Tick written=group?ticks*group->actual/group->normal:ticks;
        if(!validDuration(written))return false;
        const auto groupId=es[l->event].tuplet;
        Tick available=0;
        std::size_t end=l->event;
        while(end<es.size() && es[end].isRest() && es[end].tuplet==groupId && available<ticks)available+=es[end++].ticks;
        if(available<ticks)return false;
        const auto rests=makeRests(available-ticks,group);
        if(available!=ticks && rests.empty())return false;
        auto pos=es.erase(es.begin()+l->event,es.begin()+end);
        pos=es.insert(pos,Event {
            id,ticks, {
                pitch
            },{},groupId,group?written:0
        });
        es.insert(pos+1,rests.begin(),rests.end());
        reindexMeasure(l->staff,l->measure);
        cleanTiesNear(id);
        return true;
    }
    bool Score::addChordPitch(EventId id,Pitch p,std::size_t* added) {
        auto* e=mutableEvent(id);
        if(!e || e->isRest() || e->pitches.size()>=16 || !validPitch(p))return false;
        if(std::any_of(e->pitches.begin(),e->pitches.end(),[&](const Pitch& x) {
            return x.midi()==p.midi();
        }))return false;
        e->pitches.push_back(p);
        if(!e->ties.empty())e->ties.push_back(0);
        if(added)*added=e->pitches.size()-1;
        return true;
    }
    bool Score::setPitch(EventId id,std::size_t tone,Pitch p) {
        auto* e=mutableEvent(id);
        if(!e || tone>=e->pitches.size() || !validPitch(p))return false;
        for(std::size_t i=0;i<e->pitches.size();++i)if(i!=tone && e->pitches[i].midi()==p.midi())return false;
        e->pitches[tone]=p;
        cleanTiesNear(id);
        return true;
    }
    bool Score::setDuration(EventId id,Tick ticks,bool overwrite,bool fit) {
        const auto l=locate(id);
        if(!l || ticks<=0 || ticks>6*quarter)return false;
        auto& es=staves_[l->staff].measures[l->measure].events;
        const auto* group=tupletFor(id);
        if(group && ticks*group->actual%group->normal)return false;
        Tick written=group?ticks*group->actual/group->normal:ticks;
        if(!validDuration(written))return false;
        const auto groupId=es[l->event].tuplet;
        const Tick old=es[l->event].ticks;
        const Tick start=eventStart(id)-measureStart(l->measure);
        const Tick remaining=(group?group->start+group->span():bar(l->measure).meter.ticks())-start;
        if(ticks>remaining) {
            if(!fit || group)return false;
            ticks=largestDuration(remaining);
            if(ticks<=old)return false;
            written=ticks;
        }
        if(ticks==old)return true;
        std::size_t end=l->event+1;
        Tick available=old;
        while(available<ticks && end<es.size() && es[end].tuplet==groupId && (overwrite || es[end].isRest()))available+=es[end++].ticks;
        if(available<ticks)return false;
        const auto rests=makeRests(available-ticks,group);
        if(available!=ticks && rests.empty())return false;
        es[l->event].ticks=ticks;
        es[l->event].written=group?written:0;
        auto pos=es.erase(es.begin()+l->event+1,es.begin()+end);
        es.insert(pos,rests.begin(),rests.end());
        reindexMeasure(l->staff,l->measure);
        cleanTiesNear(id);
        return true;
    }
    bool Score::createTuplet(EventId id,int count) {
        const auto l=locate(id);
        const auto* original=event(id);
        if(!l || !original || original->tuplet || (count!=3 && count!=5 && count!=7))return false;
        const Tick base=baseDuration(original->ticks);
        if(!base)return false;
        const bool dotted=original->ticks!=base;
        const int normal=dotted?(count==7?6:3):(count==3?2:4);
        const Tick unit=original->ticks/normal;
        if(baseDuration(unit)!=unit || original->ticks%count)return false;
        const bool ordinary=count==normal;
        const Tuplet group{nextObject_,eventStart(id)-measureStart(l->measure),count,normal,unit};
        std::vector<Event> replacement;
        replacement.reserve(count);
        const auto first=*original;
        for(int i=0;i<count;++i)replacement.push_back(Event{
            i?nextId_++:id,first.ticks/count,i?std::vector<Pitch>{}:first.pitches,{},
            ordinary?0:group.id,ordinary?0:unit
        });
        auto& m=staves_[l->staff].measures[l->measure];
        auto pos=m.events.erase(m.events.begin()+l->event);
        m.events.insert(pos,replacement.begin(),replacement.end());
        if(!ordinary) {
            m.tuplets.push_back(group);
            ++nextObject_;
        }
        reindexMeasure(l->staff,l->measure);
        cleanTiesNear(id);
        return true;
    }
    bool Score::makeRest(EventId id) {
        auto* e=mutableEvent(id);
        if(!e)return false;
        e->pitches.clear();
        e->ties.clear();
        cleanTiesNear(id);
        return true;
    }
    bool Score::toggleTie(EventId id,std::size_t tone) {
        const auto l=locate(id);
        const auto* e=event(id);
        if(!l || !e || tone>=e->pitches.size())return false;
        if(e->tieTarget(tone)) {
            mutableEvent(id)->ties[tone]=0;
            return true;
        }
        const Pitch p=e->pitches[tone];
        const Tick length=e->writtenDuration();
        EventId next=nextEventId(id);
        if(!next)next=appendMeasure(l->staff);
        const auto* target=event(next);
        if(target->isRest()) {
            const auto nl=locate(next);
            Tick available=0;
            for(std::size_t j=nl->event;j<measure(nl->measure,nl->staff).events.size();++j) {
                const auto& rest=measure(nl->measure,nl->staff).events[j];
                if(!rest.isRest() || rest.tuplet!=target->tuplet)break;
                available+=rest.ticks;
            }
            const Tick duration=target->tuplet?target->ticks:largestDuration(std::min(length,available));
            if(!duration || !enterNote(next,p,duration))return false;
        }
        else if(std::find(target->pitches.begin(),target->pitches.end(),p)==target->pitches.end())return false;
        auto* source=mutableEvent(id);
        source->ties.resize(source->pitches.size());
        source->ties[tone]=next;
        return true;
    }
    bool Score::setClef(std::size_t s,Tick tick,Clef c) {
        if(s>=staffCount() || tick<0 || tick>=totalTicks() || static_cast<int>(c)<0 || static_cast<int>(c)>5)return false;
        auto& changes=staves_[s].clefs;
        auto it=std::lower_bound(changes.begin(),changes.end(),tick,[](const ClefChange& a,Tick t) {
            return a.tick<t;
        });
        if(it!=changes.end() && it->tick==tick)it->clef=c;
        else changes.insert(it,ClefChange {
            tick,c
        });
        return true;
    }
    bool Score::setTempo(std::size_t m,std::optional<TempoMark> t) {
        if(m>=measureCount() || (t && !t->valid()))return false;
        bars_[m].tempo=t;
        if(m==0 && !t)bars_[m].tempo=TempoMark{};
        return true;
    }
    bool Score::removeClef(std::size_t staff,Tick tick) {
        if(staff>=staffCount() || tick<=0)return false;
        auto& changes=staves_[staff].clefs;
        const auto it=std::lower_bound(changes.begin(),changes.end(),tick,[](const ClefChange& c,Tick t) {
            return c.tick<t;
        });
        if(it==changes.end() || it->tick!=tick)return false;
        changes.erase(it);
        return true;
    }
    bool Score::changeMeter(std::size_t first,Meter meter) {
        if(first>=measureCount() || !meter.valid())return false;
        std::size_t end=first+1;
        while(end<measureCount() && !bars_[end].meterChange)++end;
        const Tick oldStart=measureStart(first),oldEnd=measureStart(end),length=oldEnd-oldStart,newLength=meter.ticks();
        for(const auto& staff:staves_)for(std::size_t m=first;m<end;++m)for(const auto& t:staff.measures[m].tuplets) {
            const Tick start=(measureStart(m)-oldStart+t.start)%newLength;
            if(t.span()>newLength-start)return false;
        }
        const std::size_t newCount=static_cast<std::size_t>((length+newLength-1)/newLength);
        const Tick delta=static_cast<Tick>(newCount)*newLength-length;
        std::vector<Bar> replacement(newCount,Bar {
            meter,false,{}
        });
        replacement[0].meterChange=true;
        for(std::size_t m=first;m<end;++m)if(bars_[m].tempo) {
            const auto destination=std::min(newCount-1,static_cast<std::size_t>((measureStart(m)-oldStart)/newLength));
            replacement[destination].tempo=bars_[m].tempo;
        }
        std::vector<std::vector<Measure>> rebuilt;
        rebuilt.reserve(staffCount());
        for(const auto& staff:staves_) {
            std::vector<Measure> result(newCount);
            for(std::size_t m=first;m<end;++m)for(auto t:staff.measures[m].tuplets) {
                const Tick offset=measureStart(m)-oldStart+t.start;
                t.start=offset%newLength;
                result[offset/newLength].tuplets.push_back(t);
            }
            std::size_t destination=0;
            Tick used=0;
            for(std::size_t m=first;m<end;++m)for(const auto& original:staff.measures[m].events) {
                if(used==newLength) {
                    ++destination;
                    used=0;
                }
                if(original.tuplet) {
                    result[destination].events.push_back(original);
                    used+=original.ticks;
                    continue;
                }
                Tick remaining=original.ticks;
                Event* previous=nullptr;
                while(remaining) {
                    if(used==newLength) {
                        ++destination;
                        used=0;
                        previous=nullptr;
                    }
                    const Tick piece=largestDuration(std::min(remaining,newLength-used));
                    if(!piece)return false;
                    const bool firstPiece=remaining==original.ticks;
                    Event eventCopy {
                        firstPiece?original.id:nextId_++,piece,original.pitches,{}
                    };
                    if(!original.isRest()) {
                        if(previous) {
                            previous->ties.assign(previous->pitches.size(),eventCopy.id);
                        }
                        else if(!firstPiece) {
                            auto& prev=result[destination-1].events.back();
                            prev.ties.assign(prev.pitches.size(),eventCopy.id);
                        }
                        if(piece==remaining)eventCopy.ties=original.ties;
                    }
                    result[destination].events.push_back(std::move(eventCopy));
                    previous=&result[destination].events.back();
                    remaining-=piece;
                    used+=piece;
                }
            }
            if(used<newLength) {
                const auto rests=makeRests(newLength-used);
                if(rests.empty())return false;
                result.back().events.insert(result.back().events.end(),rests.begin(),rests.end());
            }
            rebuilt.push_back(std::move(result));
        }
        for(std::size_t s=0;s<staffCount();++s) {
            auto& staff=staves_[s];
            staff.measures.erase(staff.measures.begin()+first,staff.measures.begin()+end);
            staff.measures.insert(staff.measures.begin()+first,rebuilt[s].begin(),rebuilt[s].end());
            for(auto& c:staff.clefs)if(c.tick>=oldEnd)c.tick+=delta;
            for(auto& k:staff.keys)if(k.tick>=oldEnd)k.tick+=delta;
        }
        bars_.erase(bars_.begin()+first,bars_.begin()+end);
        bars_.insert(bars_.begin()+first,replacement.begin(),replacement.end());
        rebuildIndex();
        for(std::size_t s=0;s<staffCount();++s)cleanTiesNear(firstEventId(first+newCount-1,s));
        return valid();
    }
    PartId Score::addPart(std::string name,int program,bool piano) {
        if(program<0 || program>127)return 0;
        const auto id=nextObject_++;
        parts_.push_back(Part {
            id,std::move(name),program
        });
        addStaff(id,Clef::Treble);
        if(piano)addStaff(id,Clef::Bass);
        return id;
    }
    StaffId Score::addStaff(PartId part,Clef clef) {
        const auto p=std::find_if(parts_.begin(),parts_.end(),[&](const Part& x) {
            return x.id==part;
        });
        if(p==parts_.end())return 0;
        Staff staff {
            nextObject_++,part,p->name,-1,{}, {
                {
                    0,clef
                }
            }
        };
        for(const auto& b:bars_)staff.measures.push_back(Measure {
            makeRests(b.meter.ticks())
        });
        auto pos=std::find_if(staves_.begin(),staves_.end(),[&](const Staff& s) {
            return s.part==part;
        });
        if(pos!=staves_.end())while(pos!=staves_.end() && pos->part==part)++pos;
        const auto id=staff.id;
        staves_.insert(pos,std::move(staff));
        rebuildIndex();
        return id;
    }
    bool Score::removeStaff(StaffId id) {
        const auto s=staffIndex(id);
        if(!s || staffCount()==1)return false;
        const auto part=staves_[*s].part;
        staves_.erase(staves_.begin()+*s);
        if(std::none_of(staves_.begin(),staves_.end(),[&](const Staff& x) {
            return x.part==part;
        })) {
            std::erase_if(parts_,[&](const Part& p) {
                return p.id==part;
            });
            for(auto& g:groups_)std::erase(g.parts,part);
            std::erase_if(groups_,[](const StaffGroup& g) {
                return g.parts.size()<2;
            });
        }
        rebuildIndex();
        return true;
    }
    bool Score::moveStaff(StaffId id,int direction) {
        const auto s=staffIndex(id);
        if(!s || !direction)return false;
        const auto target=static_cast<std::ptrdiff_t>(*s)+(direction<0?-1:1);
        if(target<0 || target>=static_cast<std::ptrdiff_t>(staffCount()))return false;
        if(staves_[*s].part==staves_[target].part)std::swap(staves_[*s],staves_[target]);
        else {
            const PartId part=staves_[*s].part,other=staves_[target].part;
            auto rank=[&](PartId id) {
                return std::find_if(parts_.begin(),parts_.end(),[&](const Part& p) {
                    return p.id==id;
                })-parts_.begin();
            };
            auto groupFor=[&](PartId id)->StaffGroup* {
                for(auto& g:groups_)if(std::find(g.parts.begin(),g.parts.end(),id)!=g.parts.end())return &g;
                return nullptr;
            };
            auto* ga=groupFor(part);
            auto* gb=groupFor(other);
            if(ga==gb)std::swap(parts_[rank(part)],parts_[rank(other)]);
            else {
                const auto firstA=rank(ga?ga->parts.front():part),firstB=rank(gb?gb->parts.front():other);
                const auto sizeA=ga?ga->parts.size():1,sizeB=gb?gb->parts.size():1;
                if(firstA<firstB)std::rotate(parts_.begin()+firstA,parts_.begin()+firstB,parts_.begin()+firstB+sizeB);
                else std::rotate(parts_.begin()+firstB,parts_.begin()+firstA,parts_.begin()+firstA+sizeA);
            }
            std::stable_sort(staves_.begin(),staves_.end(),[&](const Staff& x,const Staff& y) {
                return rank(x.part)<rank(y.part);
            });
            for(auto& g:groups_) {
                std::vector<PartId> order;
                for(const auto& p:parts_)if(std::find(g.parts.begin(),g.parts.end(),p.id)!=g.parts.end())order.push_back(p.id);
                g.parts=std::move(order);
            }
        }
        rebuildIndex();
        return true;
    }
    bool Score::setInstrument(StaffId id,int p,std::string name) {
        const auto s=staffIndex(id);
        if(!s || p< -1 || p>127)return false;
        staves_[*s].program=p;
        staves_[*s].name=std::move(name);
        return true;
    }
    bool Score::setPartInstrument(PartId id,int p,std::string name) {
        if(p<0 || p>127)return false;
        auto it=std::find_if(parts_.begin(),parts_.end(),[&](const Part& x) {
            return x.id==id;
        });
        if(it==parts_.end())return false;
        it->program=p;
        it->name=std::move(name);
        return true;
    }
    bool Score::addGroup(std::string name,std::vector<PartId> ids) {
        if(ids.size()<2)return false;
        auto first=std::find_if(parts_.begin(),parts_.end(),[&](const Part& p) {
            return p.id==ids[0];
        });
        if(first==parts_.end() || static_cast<std::size_t>(parts_.end()-first)<ids.size())return false;
        for(std::size_t i=0;i<ids.size();++i)if((first+i)->id!=ids[i])return false;
        for(const auto& g:groups_)for(auto id:ids)if(std::find(g.parts.begin(),g.parts.end(),id)!=g.parts.end())return false;
        groups_.push_back(StaffGroup {
            nextObject_++,std::move(name),std::move(ids)
        });
        return true;
    }
    bool Score::removeGroup(GroupId id) {
        return std::erase_if(groups_,[&](const StaffGroup& g) {
            return g.id==id;
        })>0;
    }
    EventId Score::appendMeasure(std::size_t selected) {
        Bar b=bars_.empty()?Bar{}:Bar {
            bars_.back().meter,false,{}
        };
        bars_.push_back(b);
        if(indexedIds_.size()!=staffCount())indexedIds_.resize(staffCount());
        for(std::size_t s=0;s<staffCount();++s) {
            staves_[s].measures.push_back(Measure {
                makeRests(b.meter.ticks())
            });
            indexedIds_[s].emplace_back();
            reindexMeasure(s,measureCount()-1);
        }
        if(starts_.empty())starts_.push_back(0);
        starts_.push_back(starts_.back()+b.meter.ticks());
        return firstEventId(measureCount()-1,selected);
    }
    bool Score::removeLastMeasure() {
        if(measureCount()<=1)return false;
        const Tick end=measureStart(measureCount()-1);
        for(const auto& s:staves_)if(s.keys.back().tick>=end || s.clefs.back().tick>=end)return false;
        for(std::size_t s=0;s<staffCount();++s) {
            for(const auto& e:staves_[s].measures.back().events)index_.erase(e.id);
            staves_[s].measures.pop_back();
            indexedIds_[s].pop_back();
        }
        bars_.pop_back();
        starts_.pop_back();
        return true;
    }
    bool Score::restoreMeasure(std::size_t m,const Measure& data,std::size_t s) {
        if(s>=staffCount() || m>=measureCount() || !validMeasure(data,bar(m).meter.ticks()))return false;
        staves_[s].measures[m]=data;
        reindexMeasure(s,m);
        return true;
    }
    BarBundle Score::bundle(std::size_t m) const {
        BarBundle b {
            bars_.at(m),{}
        };
        for(const auto& s:staves_)b.measures.push_back(s.measures.at(m));
        return b;
    }
    bool Score::restoreBundle(const BarBundle& b) {
        if(b.measures.size()!=staffCount() || !b.bar.meter.valid())return false;
        for(const auto& m:b.measures)if(!validMeasure(m,b.bar.meter.ticks()))return false;
        bars_.push_back(b.bar);
        for(std::size_t s=0;s<staffCount();++s) {
            staves_[s].measures.push_back(b.measures[s]);
            indexedIds_[s].emplace_back();
            reindexMeasure(s,measureCount()-1);
        }
        starts_.push_back(starts_.back()+b.bar.meter.ticks());
        return true;
    }
    bool Score::restoreAppendedMeasure(const Measure& m) {
        if(staffCount()!=1)return false;
        return restoreBundle(BarBundle {
            Bar {
                bars_.back().meter,false,{}
            }, {
                m
            }
        });
    }
    bool Score::loadMeasures(std::vector<Measure> ms) {
        Staff s {
            2,1,"Piano",-1,std::move(ms), {
                {
                    0,Clef::Treble
                }
            }
        };
        std::vector<Bar> bs(s.measures.size());
        if(!bs.empty()) {
            bs[0].meterChange=true;
            bs[0].tempo=TempoMark{};
        }
        EventId id=1;
        for(auto& m:s.measures)for(auto& e:m.events)e.id=id++;
        return loadDocument(std::move(bs), {
            s
        }, {
            Part {
                1,"Piano",0
            }
        });
    }
    bool Score::loadDocument(std::vector<Bar> bs,std::vector<Staff> ss,std::vector<Part> ps,std::vector<StaffGroup> gs) {
        Score candidate(1);
        candidate.bars_=std::move(bs);
        candidate.staves_=std::move(ss);
        candidate.parts_=std::move(ps);
        candidate.groups_=std::move(gs);
        candidate.nextId_=1;
        candidate.nextObject_=1;
        for(const auto& s:candidate.staves_)candidate.nextObject_=std::max(candidate.nextObject_,s.id+1);
        for(const auto& p:candidate.parts_)candidate.nextObject_=std::max(candidate.nextObject_,p.id+1);
        for(const auto& g:candidate.groups_)candidate.nextObject_=std::max(candidate.nextObject_,g.id+1);
        if(candidate.bars_.empty() || candidate.staves_.empty())return false;
        candidate.rebuildIndex();
        if(!candidate.valid())return false;
        *this=std::move(candidate);
        return true;
    }
    bool Score::valid() const {
        if(bars_.empty() || staves_.empty() || parts_.empty() || !bars_[0].meterChange)return false;
        std::unordered_set<std::uint64_t> objects;
        std::unordered_set<EventId> events;
        for(const auto& p:parts_)if(!p.id || !objects.insert(p.id).second || p.program<0 || p.program>127)return false;
        for(std::size_t m=0;m<bars_.size();++m) {
            if(!bars_[m].meter.valid() || (bars_[m].tempo && !bars_[m].tempo->valid()) || (m && !bars_[m].meterChange && bars_[m].meter!=bars_[m-1].meter))return false;
        }
        for(std::size_t si=0;si<staffCount();++si) {
            const auto& s=staves_[si];
            if(!s.id || !objects.insert(s.id).second || s.measures.size()!=measureCount() || s.program< -1 || s.program>127 || std::none_of(parts_.begin(),parts_.end(),[&](const Part& p) {
                return p.id==s.part;
            }) || s.clefs.empty() || s.clefs.front().tick!=0)return false;
            Tick previous=-1;
            for(const auto& c:s.clefs) {
                if(c.tick<=previous || c.tick>=totalTicks() || static_cast<int>(c.clef)<0 || static_cast<int>(c.clef)>5)return false;
                previous=c.tick;
            }
            if(s.keys.empty() || s.keys.front().tick!=0)return false;
            previous=-1;
            for(const auto& k:s.keys) {
                if(k.tick<=previous || k.tick>=totalTicks() || !k.context.valid())return false;
                previous=k.tick;
            }
            for(std::size_t m=0;m<measureCount();++m) {
                if(!validMeasure(s.measures[m],bar(m).meter.ticks()))return false;
                for(const auto& t:s.measures[m].tuplets)if(!objects.insert(t.id).second)return false;
                for(const auto& e:s.measures[m].events) {
                    if(!events.insert(e.id).second)return false;
                    for(std::size_t i=0;i<e.ties.size();++i)if(e.ties[i]) {
                        const auto* target=event(e.ties[i]);
                        if(e.ties[i]!=nextEventId(e.id) || !target || std::find(target->pitches.begin(),target->pitches.end(),e.pitches[i])==target->pitches.end())return false;
                    }
                }
            }
        }
        for(const auto& p:parts_)if(std::none_of(staves_.begin(),staves_.end(),[&](const Staff& s) {
            return s.part==p.id;
        }))return false;
        std::unordered_set<PartId> grouped;
        for(const auto& g:groups_) {
            if(!g.id || !objects.insert(g.id).second || g.parts.size()<2)return false;
            auto first=std::find_if(parts_.begin(),parts_.end(),[&](const Part& p) {
                return p.id==g.parts[0];
            });
            if(first==parts_.end() || static_cast<std::size_t>(parts_.end()-first)<g.parts.size())return false;
            for(std::size_t i=0;i<g.parts.size();++i)if((first+i)->id!=g.parts[i] || !grouped.insert(g.parts[i]).second)return false;
        }
        return true;
    }
    Pitch Score::nearestPitch(int step,const Pitch& anchor) {
        Pitch best {
            step,anchor.octave,0
        };
        int distance=1000;
        for(int o=std::max(-1,anchor.octave-1);o<=std::min(9,anchor.octave+1);++o) {
            const int d=std::abs(o*7+step-(anchor.octave*7+anchor.step));
            if(d<distance) {
                distance=d;
                best.octave=o;
            }
        }
        return best;
    }
    void EditHistory::record(EditChange c) {
        LocalEdit local;
        local.measures.push_back(MeasureEdit {
            0,c.measureIndex,std::move(c.before),std::move(c.after)
        });
        if(c.appended)local.appended.push_back(BarBundle {
            Bar{}, {
                *c.appended
            }
        });
        record(std::move(local));
    }
    void EditHistory::record(LocalEdit c) {
        undo_.emplace_back(std::move(c));
        redo_.clear();
    }
    void EditHistory::record(KeyEdit c) {
        undo_.emplace_back(std::move(c));
        redo_.clear();
    }
    void EditHistory::record(Score before,const Score& after) {
        undo_.emplace_back(StructuralEdit {
            std::make_unique<Score>(std::move(before)),std::make_unique<Score>(after)
        });
        redo_.clear();
    }
    bool EditHistory::apply(Score& score,const Change& c,bool forward,EditImpact* impact) {
        if(impact)*impact={};
        if(const auto* key=std::get_if<KeyEdit>(&c)) {
            if(!score.restoreKeys(key->staff,forward?key->after:key->before))return false;
            if(impact)impact->measures=key->measures;
            return true;
        }
        if(const auto* structural=std::get_if<StructuralEdit>(&c)) {
            score=forward?*structural->after:*structural->before;
            if(impact)impact->reset=true;
            return true;
        }
        const auto& local=std::get<LocalEdit>(c);
        const auto originalCount=forward?score.measureCount():score.measureCount()-local.appended.size();
        if(!forward)for(std::size_t i=0;i<local.appended.size();++i)if(!score.removeLastMeasure())return false;
        for(const auto& patch:local.measures)if(!score.restoreMeasure(patch.measure,forward?patch.after:patch.before,patch.staff))return false;
        if(forward)for(const auto& bundle:local.appended)if(!score.restoreBundle(bundle))return false;
        if(impact) {
            for(const auto& patch:local.measures)impact->measures.push_back(patch.measure);
            for(std::size_t i=0;i<local.appended.size();++i)impact->measures.push_back(originalCount+i);
            std::sort(impact->measures.begin(),impact->measures.end());
            impact->measures.erase(std::unique(impact->measures.begin(),impact->measures.end()),impact->measures.end());
        }
        return true;
    }
    bool EditHistory::undo(Score& score,EditImpact* impact) {
        if(undo_.empty())return false;
        auto& c=undo_.back();
        if(!apply(score,c,false,impact))return false;
        redo_.push_back(std::move(c));
        undo_.pop_back();
        return true;
    }
    bool EditHistory::redo(Score& score,EditImpact* impact) {
        if(redo_.empty())return false;
        auto& c=redo_.back();
        if(!apply(score,c,true,impact))return false;
        undo_.push_back(std::move(c));
        redo_.pop_back();
        return true;
    }
    void EditHistory::clear() {
        undo_.clear();
        redo_.clear();
    }
}
// namespace music

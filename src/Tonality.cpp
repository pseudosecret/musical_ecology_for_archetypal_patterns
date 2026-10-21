#include "Score.h"
#include <algorithm>
#include <array>

namespace music {
namespace {
int mod(int n,int d) { return (n%d+d)%d; }
}
KeyContext KeyContext::fromSignature(int fifths,ScaleIdentity scale,MinorForm form) {
    if(fifths< -7 || fifths>7)return {fifths,std::nullopt};
    const int step=mod(4*fifths+(scale==ScaleIdentity::Minor?5:0),7);
    const int pc=mod(7*fifths+(scale==ScaleIdentity::Minor?9:0),12);
    const int natural=std::array{0,2,4,5,7,9,11}[step];
    int alter=mod(pc-natural+6,12)-6;
    return {fifths,Tonality{step,alter,scale,scale==ScaleIdentity::Major?MinorForm::Melodic:form}};
}
bool KeyContext::valid() const {
    if(fifths && (*fifths< -7 || *fifths>7))return false;
    if(!tonality)return true;
    const auto& t=*tonality;
    if(t.step<0 || t.step>6 || t.alter< -1 || t.alter>1 || int(t.scale)<0 || int(t.scale)>1 || int(t.minor)<0 || int(t.minor)>2)return false;
    if(!fifths)return false;
    return fromSignature(*fifths,t.scale,t.minor).tonality==tonality;
}
int KeyContext::alteration(int step) const {
    if(!fifths || step<0 || step>6)return 0;
    constexpr std::array sharps{3,0,4,1,5,2,6},flats{6,2,5,1,4,0,3};
    const auto& order=*fifths<0?flats:sharps;
    const auto count=std::abs(*fifths);
    return std::find(order.begin(),order.begin()+count,step)!=order.begin()+count?(*fifths<0?-1:1):0;
}
std::string KeyContext::label() const {
    if(!tonality)return fifths?std::to_string(std::abs(*fifths))+(*fifths<0?" flats":" sharps")+" · key unspecified":"Key unspecified";
    const auto& t=*tonality;
    std::string result(1,"CDEFGAB"[t.step]);
    if(t.alter)result+=t.alter<0?"♭":"♯";
    if(t.scale==ScaleIdentity::Major)return result+" major";
    return result+(t.minor==MinorForm::Melodic?" melodic minor":t.minor==MinorForm::Harmonic?" harmonic minor":" Aeolian (natural minor)");
}
const KeyContext& Score::keyAt(std::size_t staff,Tick tick) const {
    const auto& keys=staves_.at(staff).keys;
    auto it=std::upper_bound(keys.begin(),keys.end(),tick,[](Tick t,const KeyChange& c){return t<c.tick;});
    return (it==keys.begin()?keys.front():*std::prev(it)).context;
}
int Score::entryAlteration(std::size_t staff,Tick tick,int step,int octave) const {
    const auto m=measureAt(tick);
    Tick at=measureStart(m);
    int alter=keyAt(staff,tick).alteration(step);
    const auto& keys=staves_.at(staff).keys;
    const auto next=std::upper_bound(keys.begin(),keys.end(),tick,[](Tick t,const KeyChange& c){return t<c.tick;});
    const Tick reset=std::max(at,(next==keys.begin()?keys.front():*std::prev(next)).tick);
    for(const auto& e:measure(m,staff).events) {
        if(at>=tick)break;
        if(at>=reset)for(const auto& p:e.pitches)if(p.step==step && p.octave==octave) {
            const auto* previous=event(previousEventId(e.id));
            bool tied=false;
            if(previous)for(std::size_t i=0;i<previous->pitches.size();++i)if(previous->tieTarget(i)==e.id && previous->pitches[i]==p)tied=true;
            if(!tied)alter=p.alter;
        }
        at+=e.ticks;
    }
    return alter;
}
bool Score::restoreKeys(std::size_t staff,const std::vector<KeyChange>& keys) {
    if(staff>=staffCount() || keys.empty() || keys.front().tick!=0)return false;
    Tick previous=-1;
    for(const auto& k:keys) {
        if(k.tick<=previous || k.tick>=totalTicks() || !k.context.valid())return false;
        previous=k.tick;
    }
    staves_[staff].keys=keys;
    return true;
}
std::optional<KeyEdit> Score::setKey(std::size_t staff,Tick tick,KeyContext key) {
    if(staff>=staffCount() || tick<0 || tick>=totalTicks() || !key.valid())return {};
    auto keys=staves_[staff].keys;
    auto it=std::lower_bound(keys.begin(),keys.end(),tick,[](const KeyChange& c,Tick t){return c.tick<t;});
    if(it!=keys.end() && it->tick==tick)it->context=key;
    else it=keys.insert(it,{tick,key});
    const Tick end=std::next(it)==keys.end()?totalTicks():std::next(it)->tick;
    KeyEdit patch{staff,staves_[staff].keys,keys,{}};
    if(patch.before==patch.after)return {};
    // The next signature's cancellation also depends on the preceding key.
    for(auto m=measureAt(tick);m<=measureAt(std::min(end,totalTicks()-1));++m)patch.measures.push_back(m);
    staves_[staff].keys=std::move(keys);
    return patch;
}
std::optional<KeyEdit> Score::removeKey(std::size_t staff,Tick tick) {
    if(staff>=staffCount() || tick<=0)return {};
    auto keys=staves_[staff].keys;
    auto it=std::lower_bound(keys.begin(),keys.end(),tick,[](const KeyChange& c,Tick t){return c.tick<t;});
    if(it==keys.end() || it->tick!=tick)return {};
    const Tick end=std::next(it)==keys.end()?totalTicks():std::next(it)->tick;
    keys.erase(it);
    KeyEdit patch{staff,staves_[staff].keys,keys,{}};
    for(auto m=measureAt(tick);m<=measureAt(std::min(end,totalTicks()-1));++m)patch.measures.push_back(m);
    staves_[staff].keys=std::move(keys);
    return patch;
}
}

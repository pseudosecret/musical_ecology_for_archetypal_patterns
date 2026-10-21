#include "Transformations.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace music {
namespace {
bool integer(const QJsonValue& value,int low,int high,int& output) {
    if(!value.isDouble())return false;
    const double n=value.toDouble();
    if(!std::isfinite(n) || std::floor(n)!=n || n<low || n>high)return false;
    output=static_cast<int>(n);
    return true;
}
bool ticks(const QJsonValue& value,Tick& output) {
    if(!value.isString())return false;
    const auto string=value.toString();
    if(string.isEmpty() || std::any_of(string.begin(),string.end(),[](QChar c){return c<'0' || c>'9';}))return false;
    bool ok=false;
    output=value.toString().toLongLong(&ok);
    return ok && output>=0 && output<=2'000'000*quarter;
}
QJsonObject eventJson(const FragmentEvent& e) {
    QJsonArray pitches,ties;
    for(const auto& p:e.pitches)pitches.append(QJsonArray{p.step,p.octave,p.alter});
    for(bool t:e.ties)ties.append(t);
    return {{"ticks",QString::number(e.ticks)},{"pitches",pitches},{"ties",ties},
            {"actual",e.actual},{"normal",e.normal},{"unit",QString::number(e.unit)},{"group",QString::number(e.group)}};
}
bool validKeys(const std::vector<KeyChange>& keys,Tick duration) {
    if(keys.empty() || keys.size()>250'000 || keys.front().tick!=0)return false;
    Tick previous=-1;
    for(const auto& k:keys) {
        if(k.tick<=previous || k.tick>=duration || !k.context.valid())return false;
        previous=k.tick;
    }
    return true;
}
QJsonArray keysJson(const std::vector<KeyChange>& keys) {
    QJsonArray result;
    for(const auto& k:keys) {
        QJsonValue tonality(QJsonValue::Null);
        if(k.context.tonality) {
            const auto& t=*k.context.tonality;
            tonality=QJsonObject{{"step",t.step},{"alter",t.alter},{"scale",t.scale==ScaleIdentity::Major?"major":"minor"},
                                 {"minor",t.minor==MinorForm::Melodic?"melodic":t.minor==MinorForm::Harmonic?"harmonic":"aeolian"}};
        }
        result.append(QJsonObject{{"tick",QString::number(k.tick)},{"fifths",k.context.fifths?QJsonValue(*k.context.fifths):QJsonValue(QJsonValue::Null)},{"tonality",tonality}});
    }
    return result;
}
std::optional<std::vector<KeyChange>> readKeys(const QJsonValue& value,Tick duration) {
    if(!value.isArray() || value.toArray().size()>250'000)return {};
    std::vector<KeyChange> keys;
    for(const auto& v:value.toArray()) {
        if(!v.isObject())return {};
        const auto raw=v.toObject();
        KeyChange k{0,{std::nullopt,std::nullopt}};
        int f=0;
        if(!ticks(raw.value("tick"),k.tick))return {};
        if(!raw.value("fifths").isNull()) {
            if(!integer(raw.value("fifths"),-7,7,f))return {};
            k.context.fifths=f;
        }
        if(!raw.value("tonality").isNull()) {
            if(!raw.value("tonality").isObject())return {};
            const auto t=raw.value("tonality").toObject();
            Tonality identity;
            if(!integer(t.value("step"),0,6,identity.step) || !integer(t.value("alter"),-1,1,identity.alter))return {};
            const auto scale=t.value("scale").toString(),form=t.value("minor").toString();
            if((scale!="major" && scale!="minor") || (form!="melodic" && form!="harmonic" && form!="aeolian"))return {};
            identity.scale=scale=="major"?ScaleIdentity::Major:ScaleIdentity::Minor;
            identity.minor=form=="melodic"?MinorForm::Melodic:form=="harmonic"?MinorForm::Harmonic:MinorForm::Aeolian;
            k.context.tonality=identity;
        }
        keys.push_back(k);
    }
    return validKeys(keys,duration)?std::optional{keys}:std::nullopt;
}
std::optional<QJsonObject> responseRoot(const QByteArray& bytes,const QString& id,QString* error) {
    auto fail=[&](const QString& text)->std::optional<QJsonObject> { if(error)*error=text; return {}; };
    if(bytes.isEmpty() || bytes.size()>transformMessageLimit)return fail("Worker response exceeded the message limit.");
    QJsonParseError parse;
    const auto document=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError || !document.isObject())return fail("The worker returned malformed JSON.");
    const auto root=document.object();
    if(root.value("version")!=2 || root.value("id")!=id || !root.value("ok").isBool())return fail("Worker protocol or request ID mismatch.");
    if(!root.value("ok").toBool())return fail(root.value("error").toObject().value("message").toString("Worker operation failed.").left(4096));
    return root;
}
QByteArray requestBytes(const ScoreFragment& fragment,const std::vector<KeyChange>& keys,const QString& id,const QString& command,const QJsonArray& operations={}) {
    if(!fragment.valid() || fragment.rows.size()!=1 || !validKeys(keys,fragment.duration))return {};
    QJsonArray events;
    for(const auto& e:fragment.rows.front())events.append(eventJson(e));
    auto bytes=QJsonDocument(QJsonObject{{"version",2},{"id",id},{"command",command},{"ppq",int(quarter)},
        {"duration",QString::number(fragment.duration)},{"events",events},{"keys",keysJson(keys)},{"steps",operations}}).toJson(QJsonDocument::Compact);
    return bytes.size()<transformMessageLimit?bytes+'\n':QByteArray{};
}
}
std::optional<std::vector<TransformStep>> transformSteps(const QVariantList& values,QString* error) {
    auto fail=[&]()->std::optional<std::vector<TransformStep>> { if(error)*error="Add valid transformation steps (positive integer rhythm ratios)."; return {}; };
    if(values.empty() || values.size()>64)return fail();
    std::vector<TransformStep> result;
    for(const auto& value:values) {
        const auto map=value.toMap();
        const auto kind=map.value("kind").toString();
        TransformStep step;
        auto number=[&](const char* key,int low,int high,int& n) { return integer(QJsonValue::fromVariant(map.value(key)),low,high,n); };
        const auto mode=map.value("mode","semitones").toString(),policy=map.value("accidentals","preserve").toString();
        if((mode!="semitones" && mode!="degrees") || (policy!="preserve" && policy!="scale"))return fail();
        step.mode=mode=="degrees"?PitchMode::Degrees:PitchMode::Semitones;
        step.accidentals=policy=="scale"?AccidentalPolicy::Scale:AccidentalPolicy::Preserve;
        if(map.contains("axisDegree") && !number("axisDegree",1,7,step.axisDegree))return fail();
        if(map.contains("tonicOctave") && !number("tonicOctave",-1,9,step.tonicOctave))return fail();
        if(map.contains("reverseKeys")) {
            if(map.value("reverseKeys").metaType().id()!=QMetaType::Bool)return fail();
            step.reverseKeys=map.value("reverseKeys").toBool();
        }
        if(kind=="transpose") { step.kind=TransformKind::Transpose; if(!number("amount",-127,127,step.amount))return fail(); }
        else if(kind=="invert") { step.kind=TransformKind::Invert; if(!number("amount",step.mode==PitchMode::Semitones?0:-127,127,step.amount))return fail(); }
        else if(kind=="scale") { step.kind=TransformKind::Scale; if(!number("numerator",1,1'000'000,step.numerator) || !number("denominator",1,1'000'000,step.denominator))return fail(); }
        else if(kind=="retrograde")step.kind=TransformKind::Retrograde;
        else return fail();
        result.push_back(step);
    }
    return result;
}
QByteArray transformationRequest(const ScoreFragment& fragment,const std::vector<TransformStep>& steps,const QString& id,const std::vector<KeyChange>& keys) {
    if(!fragment.valid() || fragment.rows.size()!=1 || steps.empty() || steps.size()>64)return {};
    QJsonArray operations;
    for(const auto& s:steps) {
        const char* kind=s.kind==TransformKind::Transpose?"transpose":s.kind==TransformKind::Invert?"invert":s.kind==TransformKind::Scale?"scale":"retrograde";
        operations.append(QJsonObject{{"kind",kind},{"amount",s.amount},{"mode",s.mode==PitchMode::Degrees?"degrees":"semitones"},
            {"accidentals",s.accidentals==AccidentalPolicy::Scale?"scale":"preserve"},{"axisDegree",s.axisDegree},{"tonicOctave",s.tonicOctave},
            {"reverseKeys",s.reverseKeys},{"numerator",s.numerator},{"denominator",s.denominator}});
    }
    return requestBytes(fragment,keys,id,"transform",operations);
}
QByteArray keyAnalysisRequest(const ScoreFragment& fragment,const std::vector<KeyChange>& keys,const QString& id) {
    return requestBytes(fragment,keys,id,"analyze_keys");
}
std::optional<QVariantList> keyAnalysisResponse(const QByteArray& bytes,const QString& id,const std::vector<KeyChange>& keys,QString* error) {
    const auto root=responseRoot(bytes,id,error);
    if(!root)return {};
    auto fail=[&]()->std::optional<QVariantList> { if(error)*error="Invalid key-analysis response."; return {}; };
    if(!root->value("regions").isArray() || root->value("regions").toArray().size()>qsizetype(keys.size()))return fail();
    QVariantList result;
    std::vector<bool> seen(keys.size());
    for(const auto& v:root->value("regions").toArray()) {
        const auto region=v.toObject();
        int index=0;
        if(!integer(region.value("region"),0,int(keys.size())-1,index) || seen[index] || keys[index].context.tonality || !region.value("candidates").isArray())return fail();
        seen[index]=true;
        const auto candidates=region.value("candidates").toArray();
        if(candidates.size()>3)return fail();
        QVariantList choices;
        for(const auto& c:candidates) {
            const auto candidate=c.toObject();
            int f=0,form=0;
            if(!integer(candidate.value("fifths"),-7,7,f) || !integer(candidate.value("form"),0,1,form) ||
               !candidate.value("score").isDouble() || !std::isfinite(candidate.value("score").toDouble()) ||
               (keys[index].context.fifths && *keys[index].context.fifths!=f))return fail();
            const auto key=KeyContext::fromSignature(f,form?ScaleIdentity::Minor:ScaleIdentity::Major);
            choices.append(QVariantMap{{"fifths",f},{"form",form},{"label",QString::fromStdString(key.label())}});
        }
        result.append(QVariantMap{{"region",index},{"candidates",choices}});
    }
    for(std::size_t i=0;i<keys.size();++i)if(!keys[i].context.tonality && !seen[i])return fail();
    return result;
}
std::optional<TransformResult> buildTransformationResult(const ScoreFragment& proposed,const TransformContext& context,QString* error) {
    auto fail=[&](const QString& text)->std::optional<TransformResult> { if(error)*error=text; return {}; };
    if(!context.meter.valid() || !context.tempo.valid() || context.program<0 || context.program>127 ||
       context.offset<0 || context.offset>=context.meter.ticks() || int(context.clef)<0 || int(context.clef)>5)
        return fail("Invalid source context.");
    std::string reason;
    const auto fragment=normalizeFragment(proposed,&reason);
    if(!fragment)return fail(QString::fromStdString(reason));
    if(!validKeys(context.keys,fragment->duration))return fail("Invalid result key regions.");
    Measure measure;
    Tick remaining=context.meter.ticks();
    EventId id=1;
    while(remaining) {
        const Tick value=Score::largestDuration(remaining);
        if(!value)return fail("The source meter cannot be filled exactly.");
        measure.events.push_back(Event{id++,value,{},{}});
        remaining-=value;
    }
    TransformResult result;
    Staff staff;
    staff.id=2; staff.part=1; staff.name="Result";
    staff.measures.push_back(std::move(measure));
    staff.clefs={{0,context.clef}};
    staff.keys={{0,context.keys.front().context}};
    if(!result.score.loadDocument({Bar{context.meter,true,context.tempo}},{std::move(staff)},{{1,"Result",context.program}}))
        return fail("Could not construct result score.");
    if(!result.score.pasteRange(*fragment,{context.offset,context.offset,{2}},&reason))return fail(QString::fromStdString(reason));
    auto keys=context.keys;
    for(std::size_t i=1;i<keys.size();++i)keys[i].tick+=context.offset;
    if(!result.score.restoreKeys(0,keys))return fail("Could not install result key changes.");
    if(!result.score.valid())return fail("The proposed result violates score invariants.");
    result.start=context.offset;
    result.end=context.offset+fragment->duration;
    return result;
}
std::optional<TransformResult> transformationResponse(const QByteArray& bytes,const QString& id,const TransformContext& context,QString* error) {
    auto fail=[&](const QString& text)->std::optional<TransformResult> { if(error)*error=text; return {}; };
    const auto envelope=responseRoot(bytes,id,error);
    if(!envelope)return {};
    const auto root=*envelope;
    if(root.value("ppq")!=int(quarter) || !root.value("events").isArray())return fail("Invalid worker timing format.");
    ScoreFragment fragment;
    if(!ticks(root.value("duration"),fragment.duration) || !fragment.duration)return fail("Invalid result duration.");
    const auto events=root.value("events").toArray();
    if(events.empty() || events.size()>250'000)return fail("Invalid result event count.");
    fragment.rows.emplace_back();
    for(const auto& entry:events) {
        if(!entry.isObject())return fail("Invalid result event.");
        const auto e=entry.toObject();
        FragmentEvent event;
        bool ok=false;
        event.group=e.value("group").toString().toULongLong(&ok);
        if(!ok || !ticks(e.value("ticks"),event.ticks) || !ticks(e.value("unit"),event.unit) ||
           !integer(e.value("actual"),1,7,event.actual) || !integer(e.value("normal"),1,6,event.normal) ||
           !e.value("pitches").isArray() || !e.value("ties").isArray())return fail("Invalid event rhythm data.");
        const auto pitches=e.value("pitches").toArray(),ties=e.value("ties").toArray();
        if(pitches.size()>16 || pitches.size()!=ties.size())return fail("Invalid chord or tie data.");
        for(qsizetype i=0;i<pitches.size();++i) {
            const auto p=pitches[i].toArray();
            Pitch pitch;
            if(!pitches[i].isArray() || p.size()!=3 || !integer(p[0],0,6,pitch.step) ||
               !integer(p[1],-1,9,pitch.octave) || !integer(p[2],-2,2,pitch.alter) || !ties[i].isBool())return fail("Invalid result pitch.");
            event.pitches.push_back(pitch);
            event.ties.push_back(ties[i].toBool());
        }
        fragment.rows.front().push_back(std::move(event));
    }
    const auto keys=readKeys(root.value("keys"),fragment.duration);
    if(!keys)return fail("Invalid result key regions.");
    auto resultContext=context;
    resultContext.keys=*keys;
    return buildTransformationResult(fragment,resultContext,error);
}
}

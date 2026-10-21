#include "MelodyAnalysis.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace music {
std::optional<AnalysisSnapshot> captureAnalysis(const Score& score,const ScoreRange& range,const QString& label,QString* error) {
    auto fail=[&](const QString& why)->std::optional<AnalysisSnapshot> { if(error)*error=why; return {}; };
    if(range.staves.size()!=1)return fail("Select a melody on exactly one staff.");
    const auto staff=score.staffIndex(range.staves.front());
    const auto fragment=score.copyRange(range);
    if(!staff || !fragment)return fail("The selected passage cannot be analyzed.");
    const auto first=score.measureAt(range.start),last=score.measureAt(range.end-1);
    if(last-first>=20'000 || fragment->rows.front().size()>100'000)return fail("Select a smaller passage (at most 20,000 measures / 100,000 notation events).");
    AnalysisSnapshot result;
    result.fragment=*fragment; result.label=label; result.firstMeasure=int(first);
    const auto origin=score.measureStart(first);
    result.offset=range.start-origin; result.program=score.program(*staff);
    for(auto m=first;m<=last;++m) {
        result.bars.push_back(score.bar(m));
        Tick at=score.measureStart(m);
        for(const auto& e:score.measure(m,*staff).events) {
            if(at<range.end && at+e.ticks>range.start) {
                const auto* previous=score.event(score.previousEventId(e.id));
                const bool incoming=previous && previous->tieTarget(0)==e.id;
                result.sources.push_back({e.id,e.writtenDuration(),at<range.start || (at==range.start && incoming),
                    at+e.ticks>range.end || (at+e.ticks==range.end && e.tieTarget(0)!=0)});
            }
            at+=e.ticks;
        }
    }
    result.bars.front().tempo=score.tempoAt(first);
    result.bars.front().meterChange=true;
    result.keys={{0,score.keyAt(*staff,origin)}};
    result.selectedKeys={{0,score.keyAt(*staff,range.start)}};
    for(const auto& k:score.staves()[*staff].keys) {
        if(k.tick>origin && k.tick<score.measureStart(last+1))result.keys.push_back({k.tick-origin,k.context});
        if(k.tick>range.start && k.tick<range.end)result.selectedKeys.push_back({k.tick-range.start,k.context});
    }
    result.clefs={{0,score.clefAt(*staff,origin)}};
    for(const auto& c:score.staves()[*staff].clefs)
        if(c.tick>origin && c.tick<score.measureStart(last+1))result.clefs.push_back({c.tick-origin,c.clef});
    return result;
}

std::optional<TransformResult> analysisScratch(const AnalysisSnapshot& snapshot,QString* error) {
    auto fail=[&](const QString& why)->std::optional<TransformResult> { if(error)*error=why; return {}; };
    Staff staff; staff.id=2; staff.part=1; staff.name="Analysis";
    staff.keys=snapshot.keys; staff.clefs=snapshot.clefs;
    EventId id=1;
    for(const auto& bar:snapshot.bars) {
        Measure measure;
        for(Tick left=bar.meter.ticks();left;) {
            const auto length=Score::largestDuration(left);
            if(!length)return fail("Could not create the analysis scratchpad meter map.");
            measure.events.push_back(Event{id++,length,{},{}});
            left-=length;
        }
        staff.measures.push_back(std::move(measure));
    }
    TransformResult result;
    if(!result.score.loadDocument(snapshot.bars,{std::move(staff)},{{1,"Analysis",snapshot.program}}))return fail("Could not create the analysis scratchpad.");
    std::string why;
    if(!result.score.pasteRange(snapshot.fragment,{snapshot.offset,snapshot.offset,{2}},&why))return fail(QString::fromStdString(why));
    result.start=snapshot.offset; result.end=snapshot.offset+snapshot.fragment.duration;
    return result;
}

QByteArray melodyAnalysisRequest(const AnalysisSnapshot& snapshot,const QString& id) {
    // Reuse the existing pitch/tie/key wire encoding, independently of transform options.
    auto root=QJsonDocument::fromJson(keyAnalysisRequest(snapshot.fragment,snapshot.selectedKeys,id)).object();
    if(root.isEmpty())return {};
    root["command"]="analyze_melody"; root["analysisSchema"]=2;
    root["offset"]=QString::number(snapshot.offset);
    QJsonArray measures,notation;
    Tick start=0;
    for(std::size_t i=0;i<snapshot.bars.size();++i) {
        const auto& meter=snapshot.bars[i].meter;
        QJsonArray groups; for(int g:meter.groups)groups.append(g);
        measures.append(QJsonObject{{"start",QString::number(start)},{"ticks",QString::number(meter.ticks())},
            {"sourceMeasure",snapshot.firstMeasure+int(i)+1},{"numerator",meter.numerator},{"denominator",meter.denominator},{"groups",groups}});
        start+=meter.ticks();
    }
    for(const auto& source:snapshot.sources)notation.append(QJsonObject{{"sourceId",QString::number(source.id)},
        {"written",QString::number(source.written)},{"clippedStart",source.clippedStart},{"clippedEnd",source.clippedEnd}});
    root["measures"]=measures; root["notation"]=notation;
    const auto bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);
    return bytes.size()<transformMessageLimit?bytes+'\n':QByteArray{};
}

namespace {
bool integer(const QJsonValue& v,int low,int high) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=low && v.toDouble()<=high && std::floor(v.toDouble())==v.toDouble();
}
bool tick(const QJsonValue& value,Tick maximum,Tick& out) {
    if(!value.isString())return false;
    const auto s=value.toString();
    if(s.isEmpty() || s.size()>18 || std::any_of(s.begin(),s.end(),[](QChar c){return c<'0' || c>'9';}))return false;
    bool ok=false; out=s.toLongLong(&ok); return ok && out>=0 && out<=maximum;
}
bool boundedJson(const QJsonValue& v,int depth=0) {
    if(depth>16)return false;
    if(v.isDouble())return std::isfinite(v.toDouble()) && std::abs(v.toDouble())<=1e15;
    if(v.isString())return v.toString().size()<=65536;
    if(v.isArray()) {
        const auto a=v.toArray(); if(a.size()>100'000)return false;
        for(const auto& x:a)if(!boundedJson(x,depth+1))return false;
    }
    if(v.isObject()) {
        const auto o=v.toObject(); if(o.size()>1000)return false;
        for(auto it=o.begin();it!=o.end();++it)if(it.key().size()>128 || !boundedJson(it.value(),depth+1))return false;
    }
    return true;
}
bool strength(const QJsonValue& v) { return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=0 && v.toDouble()<=1; }
bool shortStrings(const QJsonValue& value,int limit=16) {
    if(!value.isArray() || value.toArray().size()>limit)return false;
    for(const auto& item:value.toArray())if(!item.isString() || item.toString().size()>1024)return false;
    return true;
}
const QStringList signatureFields{"exactPitches","chromaticIntervals","diatonicIntervals","contour","pitchEquality","returns","repeatedNotes","rhythm"};
QJsonObject patternSignature(const QJsonArray& notes,int start,int count) {
    QJsonArray pitches,intervals,diatonic,contour,equality,returns,repeated,rhythm;
    QHash<int,int> symbols;
    std::vector<Tick> durations,gaps;
    Tick unit=0;
    int first=0,previousPitch=0,previousWritten=0;
    for(int i=0;i<count;++i) {
        const auto n=notes[start+i].toObject();
        const auto pitch=n["pitch"].toArray();
        const int midi=n["midi"].toInt(),written=pitch[1].toInt()*7+pitch[0].toInt();
        if(!i)first=midi;
        pitches.append(midi);
        if(!symbols.contains(midi))symbols.insert(midi,int(symbols.size()));
        equality.append(symbols[midi]); returns.append(midi==first); repeated.append(i>0 && midi==previousPitch);
        if(i) {
            const int delta=midi-previousPitch;
            intervals.append(delta); diatonic.append(written-previousWritten); contour.append((delta>0)-(delta<0));
        }
        const Tick end=n["end"].toString().toLongLong();
        durations.push_back(end-n["start"].toString().toLongLong());
        gaps.push_back(i+1<count?notes[start+i+1].toObject()["start"].toString().toLongLong()-end:0);
        unit=std::gcd(unit,std::gcd(durations.back(),gaps.back()));
        previousPitch=midi; previousWritten=written;
    }
    for(int i=0;i<count;++i)rhythm.append(QJsonArray{qint64(durations[i]/unit),qint64(gaps[i]/unit)});
    return {{"exactPitches",pitches},{"chromaticIntervals",intervals},{"diatonicIntervals",diatonic},{"contour",contour},
        {"pitchEquality",equality},{"returns",returns},{"repeatedNotes",repeated},{"rhythm",rhythm},{"rhythmUnitTicks",QString::number(unit)}};
}
}

std::optional<MelodyAnalysis> melodyAnalysisResponse(const QByteArray& bytes,const QString& id,const AnalysisSnapshot& snapshot,QString* error) {
    auto fail=[&](const QString& why="Invalid melody-analysis response.")->std::optional<MelodyAnalysis> { if(error)*error=why; return {}; };
    if(bytes.isEmpty() || bytes.size()>transformMessageLimit)return fail("Analysis response exceeded the message limit.");
    QJsonParseError parse;
    const auto document=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError || !document.isObject())return fail("The analysis worker returned malformed JSON.");
    const auto root=document.object();
    if(root["version"]!=2 || root["id"]!=id || !root["ok"].isBool())return fail("Analysis worker protocol or request ID mismatch.");
    if(!root["ok"].toBool())return fail(root["error"].toObject()["message"].toString("Melody analysis failed.").left(4096));
    if(!root["analysis"].isObject())return fail();
    const auto data=root["analysis"].toObject();
    if(!boundedJson(data) || data["schema"]!=2 || data["algorithm"]!="melody-v2" || data["duration"]!=QString::number(snapshot.fragment.duration))return fail();
    for(const char* field:{"notes","rests","findings","relations","overview","notices","tonalRegions","declaredKeys"})if(!data[field].isArray())return fail();
    if(!data["features"].isObject() || !data["parameters"].isObject())return fail();
    for(const char* field:{"overview","notices"}) {
        if(data[field].toArray().size()>1000)return fail();
        for(const auto& value:data[field].toArray())if(!value.isString())return fail();
    }
    const auto& events=snapshot.fragment.rows.front();
    std::vector<Tick> starts{0};
    for(const auto& e:events)starts.push_back(starts.back()+e.ticks);
    std::vector<bool> seen(events.size());
    QSet<QString> noteIds;
    struct NoteReference { Tick start,end; int index,midi; };
    QHash<QString,NoteReference> references;
    const auto notes=data["notes"].toArray();
    if(notes.size()>20'000 || data["features"].toObject()["noteCount"]!=notes.size())return fail();
    Tick previous=-1;
    Tick sounding=0;
    int attacks=0,low=127,high=0;
    double weighted=0;
    for(const auto& value:notes) {
        if(!value.isObject())return fail();
        const auto note=value.toObject();
        const auto noteId=note["id"].toString();
        Tick start=0,end=0;
        if(noteId.isEmpty() || noteId.size()>64 || noteIds.contains(noteId) ||
           !tick(note["start"],snapshot.fragment.duration,start) || !tick(note["end"],snapshot.fragment.duration,end) || start>=end || start<previous ||
           !note["sources"].isArray() || !note["pitch"].isArray() || !strength(note["metricStrength"]) ||
           !note["clippedStart"].isBool() || !note["clippedEnd"].isBool() || !note["name"].isString() || !note["location"].isString())return fail();
        previous=end; noteIds.insert(noteId);
        references.insert(noteId,{start,end,int(references.size()),note["midi"].toInt()});
        const auto sources=note["sources"].toArray();
        if(sources.empty() || !note["writtenTicks"].isArray() || note["writtenTicks"].toArray().size()!=sources.size())return fail();
        int prior=-1;
        for(const auto& s:sources) {
            if(!integer(s,0,int(events.size())-1))return fail();
            const int index=s.toInt(); const auto& e=events[index];
            if(seen[index] || e.pitches.size()!=1 || (prior>=0 && (index!=prior+1 || !events[prior].ties[0])))return fail();
            if(prior<0 && (starts[index]!=start || note["clippedStart"].toBool()!=snapshot.sources[index].clippedStart))return fail();
            const auto& p=e.pitches.front();
            if(note["pitch"].toArray()!=QJsonArray{p.step,p.octave,p.alter} || note["midi"]!=p.midi())return fail();
            seen[index]=true; prior=index;
        }
        if(starts[prior+1]!=end || (events[prior].ties.size() && events[prior].ties[0]) || note["clippedEnd"].toBool()!=snapshot.sources[prior].clippedEnd)return fail();
        for(qsizetype i=0;i<sources.size();++i)if(note["writtenTicks"].toArray()[i]!=QString::number(snapshot.sources[sources[i].toInt()].written))return fail();
        const int noteIndex=int(references.size())-1;
        if(!noteIndex) {
            for(const char* field:{"intervalFromPrevious","diatonicStepsFromPrevious","gapBefore","onsetInterval"})if(!note[field].isNull())return fail();
        }
        else {
            const auto preceding=notes[noteIndex-1].toObject();
            const auto oldPitch=preceding["pitch"].toArray(),newPitch=note["pitch"].toArray();
            const int steps=(newPitch[1].toInt()-oldPitch[1].toInt())*7+newPitch[0].toInt()-oldPitch[0].toInt();
            if(note["intervalFromPrevious"]!=note["midi"].toInt()-preceding["midi"].toInt() || note["diatonicStepsFromPrevious"]!=steps ||
               note["gapBefore"]!=QString::number(start-references.value(preceding["id"].toString()).end) ||
               note["onsetInterval"]!=QString::number(start-references.value(preceding["id"].toString()).start))return fail();
        }
        if(!note["declaredScaleDegree"].isNull() && !integer(note["declaredScaleDegree"],1,7))return fail();
        if(!note["inflectionSemitones"].isNull() && !integer(note["inflectionSemitones"],-12,12))return fail();
        sounding+=end-start; attacks+=!note["clippedStart"].toBool();
        low=std::min(low,note["midi"].toInt()); high=std::max(high,note["midi"].toInt());
        weighted+=double(end-start)*note["midi"].toInt();
    }
    for(std::size_t i=0;i<events.size();++i)if(!events[i].pitches.empty() && !seen[i])return fail();
    for(const auto& value:data["rests"].toArray()) {
        const auto rest=value.toObject(); Tick start=0,end=0;
        if(!tick(rest["start"],snapshot.fragment.duration,start) || !tick(rest["end"],snapshot.fragment.duration,end) || start>=end || !rest["sources"].isArray())return fail();
        int prior=-1;
        for(const auto& s:rest["sources"].toArray()) {
            if(!integer(s,0,int(events.size())-1))return fail();
            int index=s.toInt();
            if(seen[index] || !events[index].pitches.empty() || (prior>=0 && index!=prior+1) || (prior<0 && starts[index]!=start))return fail();
            seen[index]=true; prior=index;
        }
        if(prior<0 || starts[prior+1]!=end)return fail();
    }
    if(std::find(seen.begin(),seen.end(),false)!=seen.end())return fail();
    const auto features=data["features"].toObject();
    if(features["attackCount"]!=attacks || features["restCount"]!=data["rests"].toArray().size() ||
       features["soundingTicks"]!=QString::number(sounding) || features["silenceTicks"]!=QString::number(snapshot.fragment.duration-sounding) ||
       !strength(features["silenceProportion"]) || std::abs(features["silenceProportion"].toDouble()-double(snapshot.fragment.duration-sounding)/snapshot.fragment.duration)>1e-5)return fail();
    if(notes.empty()) {
        for(const char* field:{"lowestMidi","highestMidi","rangeSemitones","weightedRegister"})if(!features[field].isNull())return fail();
    }
    else if(features["lowestMidi"]!=low || features["highestMidi"]!=high || features["rangeSemitones"]!=high-low ||
            !features["weightedRegister"].isDouble() || std::abs(features["weightedRegister"].toDouble()-weighted/sounding)>1e-5)return fail();
    for(const char* field:{"motionCounts","motionProportions","directionCounts","intervalDistribution","durationDistribution","pitchClassTicks"})if(!features[field].isObject())return fail();
    for(const char* field:{"density","boundaryProfile","strongerPositionSustains"})if(!features[field].isArray())return fail();
    if(features["boundaryProfile"].toArray().size()!=std::max(qsizetype(0),notes.size()-1))return fail();
    for(qsizetype i=0;i<features["boundaryProfile"].toArray().size();++i) {
        const auto boundary=features["boundaryProfile"].toArray()[i].toObject();
        if(boundary["tick"]!=notes[i+1].toObject()["start"])return fail();
        for(const char* field:{"strength","pitchChange","timingChange","restChange"})if(!strength(boundary[field]))return fail();
    }
    for(const auto& value:features["strongerPositionSustains"].toArray())if(!noteIds.contains(value.toString()))return fail();
    MelodyAnalysis result; result.data=data;
    QSet<QString> findingIds;
    const auto parameters=data["parameters"].toObject();
    if(parameters["maxShortWindow"]!=12 || parameters["salienceWeights"]!=QJsonArray{0.35,0.35,0.30} || parameters["abstractMotifMinOccurrences"]!=3)return fail();
    const QSet<QString> kinds{"register","rest","contour","boundary","phrase","tonality","motif","cell","gesture"};
    if(data["findings"].toArray().size()>1200 || data["relations"].toArray().size()>20000)return fail();
    for(const auto& value:data["findings"].toArray()) {
        if(!value.isObject())return fail();
        const auto f=value.toObject();
        AnalysisFinding finding;
        finding.id=f["id"].toString(); finding.kind=f["kind"].toString(); finding.title=f["title"].toString(); finding.text=f["text"].toString(); finding.strength=f["score"].toDouble();
        if(finding.id.isEmpty() || finding.id.size()>64 || findingIds.contains(finding.id) || !kinds.contains(finding.kind) ||
           finding.title.isEmpty() || finding.title.size()>1024 || !f["text"].isString() || !strength(f["score"]) ||
           !f["ranges"].isArray() || f["ranges"].toArray().isEmpty() || f["ranges"].toArray().size()>64 || !f["evidence"].isObject())return fail();
        findingIds.insert(finding.id);
        for(const auto& r:f["ranges"].toArray()) {
            AnalysisSpan span;
            if(!tick(r.toObject()["start"],snapshot.fragment.duration,span.start) || !tick(r.toObject()["end"],snapshot.fragment.duration,span.end) || span.start>=span.end)return fail();
            finding.spans.push_back(span);
        }
        // Evidence note references are checked before other systems can consume them.
        const auto evidence=f["evidence"].toObject();
        for(const char* name:{"first","last","low","high","note"})if(evidence.contains(name) && !noteIds.contains(evidence[name].toString()))return fail();
        if(finding.kind=="boundary") {
            Tick at=0; if(!tick(evidence["tick"],snapshot.fragment.duration,at) || at!=finding.spans.front().start)return fail();
        }
        if(finding.kind=="motif" || finding.kind=="cell" || finding.kind=="gesture") {
            if(!evidence["occurrences"].isArray() || evidence["occurrences"].toArray().size()!=int(finding.spans.size()) || !integer(evidence["noteCount"],2,20'000))return fail();
            const int count=evidence["noteCount"].toInt(),prefix=std::min(count,12);
            const QSet<QString> matches{"exact","transposed","rhythmic","interval","diatonic interval","abstract gesture"};
            const QString match=evidence["matchType"].toString();
            if(!matches.contains(match) || (match=="abstract gesture" && count>12) ||
               (finding.kind=="motif" && finding.spans.size()<2) || evidence["signatureNoteCount"]!=prefix ||
               !evidence["signatureComplete"].isBool() || evidence["signatureComplete"].toBool()!=(count==prefix) ||
               !evidence["evidenceClaim"].isString() || evidence["evidenceClaim"].toString().size()>1024 ||
               !shortStrings(evidence["descriptors"]) || !shortStrings(evidence["support"]) || !shortStrings(evidence["limitations"]) ||
               !shortStrings(f["salienceReasons"]) || !strength(f["salience"]) || evidence["salience"]!=f["salience"] ||
               evidence["salienceReasons"]!=f["salienceReasons"] || !integer(evidence["salientOccurrence"],0,int(finding.spans.size())-1))return fail();
            const QSet<QString> descriptors{"departure-return","leap-return","arch","inverted-arch","repeated-note emphasis","rhythmic contrast","distinctive leap"};
            for(const auto& value:evidence["descriptors"].toArray())if(!descriptors.contains(value.toString()))return fail();
            if(!QSet<QString>{"interval cell","rhythmic cell","repeated-note cell"}.contains(evidence["cellType"].toString()))return fail();
            const auto components=evidence["salienceComponents"].toObject();
            for(const char* field:{"grouping","emphasis","recurrence"})if(!strength(components[field]))return fail();
            const double salience=0.35*components["grouping"].toDouble()+0.35*components["emphasis"].toDouble()+0.30*components["recurrence"].toDouble();
            if(std::abs(salience-f["salience"].toDouble())>1e-6 || std::abs(components["recurrence"].toDouble()-std::min(1.0,double(finding.spans.size()-1)/3))>1e-6)return fail();
            finding.salience=f["salience"].toDouble();
            for(const auto& value:f["salienceReasons"].toArray())finding.salienceReasons.append(value.toString());
            if(!shortStrings(evidence["matchedDimensions"],8) || !evidence["intervalPattern"].isArray() || evidence["intervalPattern"].toArray().size()!=count-1 ||
               !evidence["durationTicks"].isArray() || evidence["durationTicks"].toArray().size()!=count)return fail();
            int occurrence=0,basePitch=0,baseIndex=0,previousEnd=-1;
            QStringList shared=signatureFields;
            QJsonObject pattern;
            for(const auto& v:evidence["occurrences"].toArray()) {
                const auto o=v.toObject();
                if(!noteIds.contains(o["first"].toString()) || !noteIds.contains(o["last"].toString()) || !integer(o["initialPitchOffset"],-127,127) ||
                   (!o["transposition"].isNull() && !integer(o["transposition"],-127,127)))return fail();
                const auto a=references.value(o["first"].toString()),b=references.value(o["last"].toString());
                if(!occurrence) { basePitch=a.midi; baseIndex=a.index; }
                if(b.index-a.index+1!=evidence["noteCount"].toInt() || finding.spans[occurrence].start!=a.start ||
                   finding.spans[occurrence].end!=b.end || o["initialPitchOffset"].toInt()!=a.midi-basePitch ||
                   a.index<previousEnd || (!o["transposition"].isNull() && o["transposition"]!=o["initialPitchOffset"]))return fail();
                if(!occurrence)pattern=patternSignature(notes,a.index,prefix);
                const auto other=patternSignature(notes,a.index,prefix);
                for(auto it=shared.begin();it!=shared.end();)if(pattern[*it]!=other[*it])it=shared.erase(it);else ++it;
                for(int i=0;i<count;++i) {
                    const auto n=notes[a.index+i].toObject(),base=notes[baseIndex+i].toObject();
                    const auto length=n["end"].toString().toLongLong()-n["start"].toString().toLongLong();
                    const auto baseLength=base["end"].toString().toLongLong()-base["start"].toString().toLongLong();
                    if(!occurrence && evidence["durationTicks"].toArray()[i]!=QString::number(length))return fail();
                    if(match=="exact" && n["midi"]!=base["midi"])return fail();
                    if(match=="exact" || match=="transposed" || match=="rhythmic") {
                        if(length!=baseLength)return fail();
                        if(i+1<count && notes[a.index+i+1].toObject()["start"].toString().toLongLong()-n["end"].toString().toLongLong()!=
                           notes[baseIndex+i+1].toObject()["start"].toString().toLongLong()-base["end"].toString().toLongLong())return fail();
                    }
                    if(i) {
                        const auto prior=notes[a.index+i-1].toObject(),basePrior=notes[baseIndex+i-1].toObject();
                        const int delta=n["midi"].toInt()-prior["midi"].toInt(),baseDelta=base["midi"].toInt()-basePrior["midi"].toInt();
                        if(!occurrence && evidence["intervalPattern"].toArray()[i-1]!=delta)return fail();
                        if((match=="interval" || match=="transposed") && delta!=baseDelta)return fail();
                        if(match=="diatonic interval") {
                            auto written=[](const QJsonObject& note) { const auto p=note["pitch"].toArray(); return p[1].toInt()*7+p[0].toInt(); };
                            if(written(n)-written(prior)!=written(base)-written(basePrior))return fail();
                        }
                    }
                }
                if(match=="abstract gesture")for(const char* field:{"contour","pitchEquality","rhythm"})if(pattern[field]!=other[field])return fail();
                previousEnd=b.index+1;
                ++occurrence;
            }
            QJsonArray sharedData; for(const auto& field:shared)sharedData.append(field);
            if(evidence["pattern"]!=pattern || evidence["matchedDimensions"]!=sharedData)return fail();
        }
        result.findings.push_back(std::move(finding));
    }
    const QSet<QString> relationships{"contains","approximate variant","interval inversion","retrograde","rhythmic scaling","transposed repetition","contour similarity"};
    for(const auto& value:data["relations"].toArray()) {
        const auto relation=value.toObject();
        if(!findingIds.contains(relation["source"].toString()) || !findingIds.contains(relation["target"].toString()) ||
           relation["source"]==relation["target"] || !relationships.contains(relation["kind"].toString()) || !strength(relation["similarity"]))return fail();
        for(const char* field:{"pitchSimilarity","intervalSimilarity","rhythmSimilarity","contourSimilarity"})if(relation.contains(field) && !strength(relation[field]))return fail();
        if(relation.contains("transposition") && !integer(relation["transposition"],-127,127))return fail();
        if(relation.contains("initialPitchOffset") && !integer(relation["initialPitchOffset"],-127,127))return fail();
        if(relation.contains("axisMidi") && (!relation["axisMidi"].isDouble() || relation["axisMidi"].toDouble()<0 || relation["axisMidi"].toDouble()>127))return fail();
    }
    for(const auto& value:data["tonalRegions"].toArray()) {
        const auto region=value.toObject();
        if(!region["region"].isNull() && !findingIds.contains(region["region"].toString()))return fail();
        if(!region["candidates"].isArray() || region["candidates"].toArray().size()>6)return fail();
        for(const auto& c:region["candidates"].toArray()) {
            const auto candidate=c.toObject();
            if(!integer(candidate["tonic"],0,11) || !candidate["scale"].isString() || !candidate["intervals"].isArray())return fail();
            const auto scale=candidate["intervals"].toArray();
            if((scale.size()!=5 && scale.size()!=7) || scale.first()!=0)return fail();
            int previous=-1;
            for(const auto& interval:scale) {
                if(!integer(interval,0,11) || interval.toInt()<=previous)return fail();
                previous=interval.toInt();
            }
            for(const char* field:{"score","fit","tonicEmphasis","degreeCoverage"})if(!strength(candidate[field]))return fail();
        }
    }
    auto provenance=QJsonDocument::fromJson(melodyAnalysisRequest(snapshot,{})).object();
    provenance.remove("id"); provenance.remove("steps"); provenance["label"]=snapshot.label;
    result.data["source"]=provenance;
    result.updateReport();
    return result;
}

void MelodyAnalysis::updateReport() {
    QStringList lines{"MELODY ANALYSIS",data["source"].toObject()["label"].toString(),"Algorithm: melody-v2 · evidence and salience scores are separate heuristics, not probabilities.",""};
    for(const auto& text:data["overview"].toArray())lines<<text.toString()<<"";
    QHash<QString,QString> titles;
    for(const auto& f:findings) {
        const QString title=f.title+(f.name.isEmpty()?QString{}:" — "+f.name);
        titles.insert(f.id,title);
        const auto state=f.kind=="motif" || f.kind=="cell" || f.kind=="gesture" || f.kind=="boundary"?f.review:f.kind=="phrase" || f.kind=="tonality"?QString("candidate"):QString("measured");
        lines<<QString("%1 [%2]").arg(title,state)<<f.text;
        for(const auto& reason:f.salienceReasons)lines<<"Salience: "+reason;
        lines<<"";
    }
    lines<<"RELATIONSHIPS";
    for(const auto& value:data["relations"].toArray()) {
        const auto r=value.toObject();
        QString line=QString("%1 → %2: %3 (similarity %4)").arg(titles.value(r["source"].toString()),titles.value(r["target"].toString()),r["kind"].toString()).arg(r["similarity"].toDouble(),0,'f',2);
        if(r.contains("transposition"))line+=QString("; offset %1 semitones").arg(r["transposition"].toInt());
        else if(r.contains("initialPitchOffset"))line+=QString("; initial pitch offset %1 semitones").arg(r["initialPitchOffset"].toInt());
        if(r.contains("axisMidi"))line+=QString("; inversion axis MIDI %1").arg(r["axisMidi"].toDouble());
        if(r.contains("rhythmRatio"))line+="; rhythm ×"+r["rhythmRatio"].toString();
        lines<<line;
    }
    lines<<""<<"COVERAGE";
    for(const auto& text:data["notices"].toArray())lines<<text.toString();
    lines<<"Detailed per-note features, exact ticks, declared keys, tonal alternatives, and evidence components are available with Copy structured data.";
    report=lines.join('\n');
}
QJsonObject MelodyAnalysis::structured() const {
    auto result=data;
    QJsonArray review;
    for(const auto& f:findings)if(f.review!="undecided" || !f.name.isEmpty())review.append(QJsonObject{{"id",f.id},{"decision",f.review},{"name",f.name}});
    result["review"]=review;
    return result;
}
}

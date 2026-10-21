#include "MusicXml.h"
#include <QDomDocument>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <QMap>
#include <QStringList>
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
namespace music {
    namespace {
        constexpr std::array<const char*,7> steps {
            "C","D","E","F","G","A","B"
        };
        QString text(const QDomElement& n,const QString& name) {
            return n.firstChildElement(name).text().trimmed();
        }
        void require(bool b,const char* message) {
            if(!b)throw std::runtime_error(message);
        }
        int integer(const QString& s) {
            bool ok=false;
            const int v=s.toInt(&ok);
            require(ok,"Invalid integer in MusicXML.");
            return v;
        }
        void children(const QDomElement& e,const QStringList& allowed) {
            for(auto c=e.firstChildElement();!c.isNull();c=c.nextSiblingElement())require(allowed.contains(c.tagName()),"Unsupported MusicXML content.");
        }
        void writeKey(QXmlStreamWriter& x,const KeyContext& key,int staff) {
            if(!key.fifths)return;
            x.writeStartElement("key");
            x.writeAttribute("number",QString::number(staff));
            x.writeTextElement("fifths",QString::number(*key.fifths));
            if(key.tonality)x.writeTextElement("mode",key.tonality->scale==ScaleIdentity::Major?"major":key.tonality->minor==MinorForm::Aeolian?"aeolian":"minor");
            x.writeEndElement();
        }
        const char* typeFor(Tick ticks) {
            switch(Score::baseDuration(ticks)) {
                case 4*quarter:return "whole";
                case 2*quarter:return "half";
                case quarter:return "quarter";
                case quarter/2:return "eighth";
                case quarter/4:return "16th";
                case quarter/8:return "32nd";
                default:return "";
            }
        }
        Tick valueFor(const QString& type) {
            for(Tick value:{4*quarter,2*quarter,quarter,quarter/2,quarter/4,quarter/8})if(type==typeFor(value))return value;
            return 0;
        }
        void writeClef(QXmlStreamWriter& x,Clef c,int staff) {
            x.writeStartElement("clef");
            x.writeAttribute("number",QString::number(staff));
            const bool bass=c==Clef::Bass || c==Clef::Bass8vb;
            const bool alto=c==Clef::Alto || c==Clef::Tenor;
            x.writeTextElement("sign",bass?"F":alto?"C":"G");
            x.writeTextElement("line",bass?"4":c==Clef::Alto?"3":c==Clef::Tenor?"4":"2");
            if(c==Clef::Treble8va || c==Clef::Bass8vb)x.writeTextElement("clef-octave-change",c==Clef::Treble8va?"1":"-1");
            x.writeEndElement();
        }
        Clef readClef(const QDomElement& c) {
            children(c, {
                "sign","line","clef-octave-change"
            });
            const auto sign=text(c,"sign");
            const int line=integer(text(c,"line"));
            const int octave=text(c,"clef-octave-change").isEmpty()?0:integer(text(c,"clef-octave-change"));
            if(sign=="G" && line==2 && octave==0)return Clef::Treble;
            if(sign=="G" && line==2 && octave==1)return Clef::Treble8va;
            if(sign=="F" && line==4 && octave==0)return Clef::Bass;
            if(sign=="F" && line==4 && octave==-1)return Clef::Bass8vb;
            if(sign=="C" && line==3 && octave==0)return Clef::Alto;
            if(sign=="C" && line==4 && octave==0)return Clef::Tenor;
            throw std::runtime_error("Unsupported clef.");
        }
        void moveCursor(QXmlStreamWriter& x,const char* direction,Tick duration) {
            if(!duration)return;
            x.writeStartElement(direction);
            x.writeTextElement("duration",QString::number(duration));
            x.writeEndElement();
        }
        void writeNote(QXmlStreamWriter& x,const Event& e,std::size_t tone,int staff,const QString& instrument,bool stopTie,
                       const Tuplet* group,bool groupStart,bool groupStop) {
            x.writeStartElement("note");
            if(tone)x.writeEmptyElement("chord");
            if(e.isRest())x.writeEmptyElement("rest");
            else {
                const auto& p=e.pitches[tone];
                x.writeStartElement("pitch");
                x.writeTextElement("step",steps[p.step]);
                if(p.alter)x.writeTextElement("alter",QString::number(p.alter));
                x.writeTextElement("octave",QString::number(p.octave));
                x.writeEndElement();
            }
            x.writeTextElement("duration",QString::number(e.ticks));
            const bool startTie=!e.isRest() && e.tieTarget(tone);
            for(const char* type:{
                "stop","start"
            })if((QString::fromLatin1(type)=="stop" && stopTie) || (QString::fromLatin1(type)=="start" && startTie)) {
                x.writeEmptyElement("tie");
                x.writeAttribute("type",type);
            }
            x.writeEmptyElement("instrument");
            x.writeAttribute("id",instrument);
            x.writeTextElement("voice","1");
            x.writeTextElement("type",typeFor(e.writtenDuration()));
            if(e.writtenDuration()!=Score::baseDuration(e.writtenDuration()))x.writeEmptyElement("dot");
            if(group) {
                x.writeStartElement("time-modification");
                x.writeTextElement("actual-notes",QString::number(group->actual));
                x.writeTextElement("normal-notes",QString::number(group->normal));
                x.writeTextElement("normal-type",typeFor(group->unit));
                x.writeEndElement();
            }
            x.writeTextElement("staff",QString::number(staff));
            if(startTie || stopTie || (!tone && (groupStart || groupStop))) {
                x.writeStartElement("notations");
                if(stopTie) {
                    x.writeEmptyElement("tied");
                    x.writeAttribute("type","stop");
                }
                if(startTie) {
                    x.writeEmptyElement("tied");
                    x.writeAttribute("type","start");
                }
                if(!tone)for(const char* type:{"start","stop"})if((QString::fromLatin1(type)=="start" && groupStart) || (QString::fromLatin1(type)=="stop" && groupStop)) {
                    x.writeEmptyElement("tuplet");
                    x.writeAttribute("type",type);
                    x.writeAttribute("number","1");
                }
                x.writeEndElement();
            }
            x.writeEndElement();
        }
    }
    bool MusicXml::save(const Score& score,const QString& path,QString* error) {
        if(!score.valid()) {
            if(error)*error="The score contains invalid musical data.";
            return false;
        }
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly)) {
            if(error)*error=file.errorString();
            return false;
        }
        QXmlStreamWriter x(&file);
        x.setAutoFormatting(true);
        x.writeStartDocument();
        x.writeStartElement("score-partwise");
        x.writeAttribute("version","4.0");
        x.writeStartElement("part-list");
        for(const auto& part:score.parts()) {
            for(const auto& group:score.groups())if(group.parts.front()==part.id) {
                x.writeStartElement("part-group");
                x.writeAttribute("type","start");
                x.writeAttribute("number",QString::number(group.id));
                x.writeTextElement("group-name",QString::fromStdString(group.name));
                x.writeTextElement("group-symbol","bracket");
                x.writeTextElement("group-barline","yes");
                x.writeEndElement();
            }
            const auto pid=QString("P%1").arg(part.id);
            x.writeStartElement("score-part");
            x.writeAttribute("id",pid);
            x.writeTextElement("part-name",QString::fromStdString(part.name));
            x.writeStartElement("identification");
            x.writeStartElement("miscellaneous");
            int number=0;
            for(const auto& s:score.staves())if(s.part==part.id) {
                ++number;
                x.writeStartElement("miscellaneous-field");
                x.writeAttribute("name",QString("music-as-becoming-program-%1").arg(number));
                x.writeCharacters(QString::number(s.program));
                x.writeEndElement();
                for(const auto& k:s.keys)if(k.context.tonality && k.context.tonality->scale==ScaleIdentity::Minor) {
                    x.writeStartElement("miscellaneous-field");
                    x.writeAttribute("name",QString("music-as-becoming-minor-v1-%1-%2").arg(number).arg(k.tick));
                    x.writeCharacters(QString::number(int(k.context.tonality->minor)));
                    x.writeEndElement();
                }
            }
            x.writeEndElement();
            x.writeEndElement();
            x.writeStartElement("score-instrument");
            x.writeAttribute("id",pid+"-default");
            x.writeTextElement("instrument-name",QString::fromStdString(part.name));
            x.writeEndElement();
            x.writeStartElement("midi-instrument");
            x.writeAttribute("id",pid+"-default");
            x.writeTextElement("midi-program",QString::number(part.program+1));
            x.writeEndElement();
            for(std::size_t si=0;si<score.staffCount();++si)if(score.staves()[si].part==part.id) {
                const auto iid=QString("S%1").arg(score.staves()[si].id);
                x.writeStartElement("score-instrument");
                x.writeAttribute("id",iid);
                x.writeTextElement("instrument-name",QString::fromStdString(score.staves()[si].name));
                x.writeEndElement();
                x.writeStartElement("midi-instrument");
                x.writeAttribute("id",iid);
                x.writeTextElement("midi-program",QString::number(score.program(si)+1));
                x.writeEndElement();
            }
            x.writeEndElement();
            for(const auto& group:score.groups())if(group.parts.back()==part.id) {
                x.writeEmptyElement("part-group");
                x.writeAttribute("type","stop");
                x.writeAttribute("number",QString::number(group.id));
            }
        }
        x.writeEndElement();
        for(std::size_t pi=0;pi<score.parts().size();++pi) {
            const auto& part=score.parts()[pi];
            std::vector<std::size_t> staffIndices;
            for(std::size_t si=0;si<score.staffCount();++si)if(score.staves()[si].part==part.id)staffIndices.push_back(si);
            x.writeStartElement("part");
            x.writeAttribute("id",QString("P%1").arg(part.id));
            for(std::size_t m=0;m<score.measureCount();++m) {
                x.writeStartElement("measure");
                x.writeAttribute("number",QString::number(m+1));
                if(m==0 || score.bar(m).meterChange) {
                    x.writeStartElement("attributes");
                    if(m==0) {
                        x.writeTextElement("divisions",QString::number(quarter));
                        for(std::size_t j=0;j<staffIndices.size();++j)writeKey(x,score.keyAt(staffIndices[j],0),j+1);
                        x.writeTextElement("staves",QString::number(staffIndices.size()));
                    }
                    x.writeStartElement("time");
                    QStringList groups;
                    for(int group:score.bar(m).meter.groups)groups<<QString::number(group);
                    x.writeTextElement("beats",score.bar(m).meter==Score::defaultMeter(score.bar(m).meter.numerator,score.bar(m).meter.denominator)?QString::number(score.bar(m).meter.numerator):groups.join("+"));
                    x.writeTextElement("beat-type",QString::number(score.bar(m).meter.denominator));
                    x.writeEndElement();
                    if(m==0) {
                        for(std::size_t j=0;j<staffIndices.size();++j)writeClef(x,score.clefAt(staffIndices[j],0),j+1);
                        if(staffIndices.size()>1)x.writeTextElement("part-symbol","brace");
                    }
                    x.writeEndElement();
                }
                if(pi==0 && score.bar(m).tempo) {
                    const auto tempo=*score.bar(m).tempo;
                    x.writeStartElement("direction");
                    x.writeAttribute("placement","above");
                    x.writeStartElement("direction-type");
                    x.writeStartElement("metronome");
                    x.writeTextElement("beat-unit",typeFor(tempo.beat));
                    if(tempo.beat!=Score::baseDuration(tempo.beat))x.writeEmptyElement("beat-unit-dot");
                    x.writeTextElement("per-minute",QString::number(tempo.bpm));
                    x.writeEndElement();
                    x.writeEndElement();
                    x.writeEmptyElement("sound");
                    x.writeAttribute("tempo",QString::number(static_cast<double>(tempo.bpm)*tempo.beat/quarter,'g',12));
                    x.writeEndElement();
                }
                for(std::size_t j=0;j<staffIndices.size();++j) {
                    if(j)moveCursor(x,"backup",score.bar(m).meter.ticks());
                    const auto si=staffIndices[j];
                    Tick onset=0;
                    for(const auto& e:score.measure(m,si).events) {
                        const auto* previous=score.event(score.previousEventId(e.id));
                        const auto* group=score.tupletFor(e.id);
                        for(std::size_t tone=0;tone<std::max<std::size_t>(1,e.pitches.size());++tone) {
                            bool stopTie=false;
                            if(previous && !e.isRest())for(std::size_t p=0;p<previous->pitches.size();++p)if(previous->tieTarget(p)==e.id && previous->pitches[p]==e.pitches[tone])stopTie=true;
                            writeNote(x,e,tone,j+1,QString("S%1").arg(score.staves()[si].id),stopTie,group,
                                      group && onset==group->start,group && onset+e.ticks==group->start+group->span());
                        }
                        onset+=e.ticks;
                    }
                }
                // Attribute-only cursor passes preserve clef positions even inside a rest.
                for(std::size_t j=0;j<staffIndices.size();++j) {
                    std::vector<ClefChange> changes;
                    for(const auto& c:score.staves()[staffIndices[j]].clefs)if(c.tick>=score.measureStart(m) && c.tick<score.measureStart(m+1) && c.tick>0)changes.push_back(c);
                    if(changes.empty())continue;
                    moveCursor(x,"backup",score.bar(m).meter.ticks());
                    Tick cursor=0;
                    for(const auto& c:changes) {
                        const Tick local=c.tick-score.measureStart(m);
                        moveCursor(x,"forward",local-cursor);
                        cursor=local;
                        x.writeStartElement("attributes");
                        writeClef(x,c.clef,j+1);
                        x.writeEndElement();
                    }
                    moveCursor(x,"forward",score.bar(m).meter.ticks()-cursor);
                }
                for(std::size_t j=0;j<staffIndices.size();++j) {
                    const auto& keys=score.staves()[staffIndices[j]].keys;
                    auto key=std::lower_bound(keys.begin(),keys.end(),std::max<Tick>(1,score.measureStart(m)),[](const KeyChange& k,Tick t){return k.tick<t;});
                    if(key==keys.end() || key->tick>=score.measureStart(m+1))continue;
                    moveCursor(x,"backup",score.bar(m).meter.ticks());
                    Tick cursor=0;
                    for(;key!=keys.end() && key->tick<score.measureStart(m+1);++key) {
                        const Tick local=key->tick-score.measureStart(m);
                        moveCursor(x,"forward",local-cursor);
                        cursor=local;
                        x.writeStartElement("attributes");
                        writeKey(x,key->context,j+1);
                        x.writeEndElement();
                    }
                    moveCursor(x,"forward",score.bar(m).meter.ticks()-cursor);
                }
                x.writeEndElement();
            }
            x.writeEndElement();
        }
        x.writeEndElement();
        x.writeEndDocument();
        if(x.hasError() || !file.commit()) {
            if(error)*error=file.errorString();
            return false;
        }
        return true;
    }
    bool MusicXml::open(const QString& path,Score* score,QString* error) {
        if(!score)return false;
        try {
            QFile file(path);
            require(file.open(QIODevice::ReadOnly),"Could not open MusicXML file.");
            require(file.size()<=32*1024*1024,"MusicXML file is too large.");
            const auto bytes=file.readAll();
            require(!bytes.contains("<!DOCTYPE") && !bytes.contains("<!ENTITY"),"Unsupported XML document declarations.");
            QDomDocument doc;
            QString parseError;
            int line=0,column=0;
            if(!doc.setContent(bytes,&parseError,&line,&column))throw std::runtime_error(QString("XML error at %1:%2: %3").arg(line).arg(column).arg(parseError).toStdString());
            const auto root=doc.documentElement();
            require(root.tagName()=="score-partwise","Only score-partwise MusicXML is supported.");
            children(root, {
                "part-list","part"
            });
            std::vector<Part> parts;
            std::vector<Staff> staves;
            std::vector<Bar> bars;
            std::vector<StaffGroup> groups;
            QMap<QString,PartId> partIds;
            QMap<QString,int> programs;
            QMap<QString,QString> instrumentNames;
            QMap<PartId,QMap<int,int>> overrides;
            QMap<PartId,QMap<int,QMap<Tick,MinorForm>>> minorForms;
            std::map<QString,StaffGroup> activeGroups;
            std::uint64_t objectId=1;
            EventId eventId=1;
            const auto list=root.firstChildElement("part-list");
            children(list, {
                "score-part","part-group"
            });
            for(auto p=list.firstChildElement();!p.isNull();p=p.nextSiblingElement()) {
                if(p.tagName()=="part-group") {
                    children(p, {
                        "group-name","group-symbol","group-barline"
                    });
                    const QString number=p.attribute("number","1");
                    if(p.attribute("type")=="start") {
                        require(activeGroups.empty(),"Nested or overlapping groups are not supported.");
                        require(text(p,"group-symbol")=="bracket" || text(p,"group-symbol").isEmpty(),"Unsupported group symbol.");
                        activeGroups[number]=StaffGroup {
                            objectId++,text(p,"group-name").toStdString(),{}
                        };
                    }
                    else {
                        require(activeGroups.count(number),"Invalid group boundary.");
                        groups.push_back(activeGroups.at(number));
                        activeGroups.erase(number);
                    }
                    continue;
                }
                children(p, {
                    "part-name","part-abbreviation","identification","score-instrument","midi-instrument"
                });
                const auto xmlId=p.attribute("id");
                require(!xmlId.isEmpty() && !partIds.contains(xmlId),"Missing or duplicate part ID.");
                const auto id=objectId++;
                partIds[xmlId]=id;
                int defaultProgram=0;
                for(auto mi=p.firstChildElement("midi-instrument");!mi.isNull();mi=mi.nextSiblingElement("midi-instrument")) {
                    children(mi, {
                        "midi-program","midi-channel","volume","pan"
                    });
                    const int program=integer(text(mi,"midi-program"))-1;
                    require(program>=0 && program<128,"Invalid MIDI program.");
                    programs[mi.attribute("id")]=program;
                    if(mi==p.firstChildElement("midi-instrument"))defaultProgram=program;
                }
                for(auto inst=p.firstChildElement("score-instrument");!inst.isNull();inst=inst.nextSiblingElement("score-instrument")) {
                    children(inst, {
                        "instrument-name","instrument-abbreviation"
                    });
                    instrumentNames[inst.attribute("id")]=text(inst,"instrument-name");
                }
                const auto identification=p.firstChildElement("identification");
                children(identification, {
                    "miscellaneous"
                });
                const auto miscellaneous=identification.firstChildElement("miscellaneous");
                children(miscellaneous, {
                    "miscellaneous-field"
                });
                for(auto field=miscellaneous.firstChildElement();!field.isNull();field=field.nextSiblingElement()) {
                    const auto name=field.attribute("name");
                    if(name.startsWith("music-as-becoming-program-"))overrides[id][integer(name.mid(QStringLiteral("music-as-becoming-program-").size()))]=integer(field.text());
                    if(name.startsWith("music-as-becoming-minor-v1-")) {
                        const auto position=name.mid(QStringLiteral("music-as-becoming-minor-v1-").size()).split('-');
                        require(position.size()==2,"Invalid minor-form metadata.");
                        bool ok=false;
                        const auto tick=position[1].toLongLong(&ok);
                        const int staff=integer(position[0]),form=integer(field.text());
                        require(ok && tick>=0 && staff>=1 && staff<=128 && form>=0 && form<=2,"Invalid minor-form metadata.");
                        require(!minorForms[id][staff].contains(tick),"Duplicate minor-form metadata.");
                        minorForms[id][staff][tick]=MinorForm(form);
                    }
                }
                parts.push_back(Part {
                    id,text(p,"part-name").toStdString(),defaultProgram
                });
                for(auto& [key,g]:activeGroups) {
                    (void)key;
                    g.parts.push_back(id);
                }
            }
            require(!parts.empty() && activeGroups.empty(),"Invalid part list.");
            std::size_t pi=0;
            for(auto pn=root.firstChildElement("part");!pn.isNull();pn=pn.nextSiblingElement("part"),++pi) {
                require(pi<parts.size() && partIds.value(pn.attribute("id"))==parts[pi].id,"Part order does not match the part list.");
                children(pn, {
                    "measure"
                });
                int staffCount=1,divisions=quarter;
                const auto firstAttributes=pn.firstChildElement("measure").firstChildElement("attributes");
                if(!text(firstAttributes,"staves").isEmpty())staffCount=integer(text(firstAttributes,"staves"));
                require(staffCount>=1 && staffCount<=128,"Unsupported staff count.");
                const auto staffOffset=staves.size();
                for(int s=0;s<staffCount;++s)staves.push_back(Staff {
                    objectId++,parts[pi].id,parts[pi].name,-1,{}, {
                        {
                            0,Clef::Treble
                        }
                    }
                });
                for(int s=0;s<staffCount;++s)staves[staffOffset+s].keys={{0,KeyContext{std::nullopt,std::nullopt}}};
                std::vector<std::map<Pitch,EventId,bool(*)(const Pitch&,const Pitch&)>> pending;
                auto less=[](const Pitch& a,const Pitch& b) {
                    if(a.step!=b.step)return a.step<b.step;
                    if(a.octave!=b.octave)return a.octave<b.octave;
                    return a.alter<b.alter;
                };
                for(int s=0;s<staffCount;++s)pending.emplace_back(less);
                Tick absolute=0;
                Meter meter;
                std::size_t m=0;
                for(auto mn=pn.firstChildElement("measure");!mn.isNull();mn=mn.nextSiblingElement("measure"),++m) {
                    children(mn, {
                        "attributes","direction","note","backup","forward"
                    });
                    for(int s=0;s<staffCount;++s)staves[staffOffset+s].measures.push_back(Measure{});
                    std::vector<std::optional<std::size_t>> activeTuplets(staffCount);
                    std::vector<int> tupletNumbers(staffCount);
                    Tick cursor=0;
                    bool explicitMeter=m==0;
                    std::optional<TempoMark> tempo;
                    for(auto n=mn.firstChildElement();!n.isNull();n=n.nextSiblingElement()) {
                        if(n.tagName()=="attributes") {
                            children(n, {
                                "divisions","key","time","staves","clef","part-symbol"
                            });
                            if(!text(n,"divisions").isEmpty()) {
                                divisions=integer(text(n,"divisions"));
                                require(divisions>0,"Invalid divisions.");
                            }
                            for(auto key=n.firstChildElement("key");!key.isNull();key=key.nextSiblingElement("key")) {
                                children(key,{"fifths","mode","cancel"});
                                const int fifths=integer(text(key,"fifths"));
                                require(fifths>=-7 && fifths<=7,"Key signatures support up to seven sharps or flats.");
                                const auto mode=text(key,"mode");
                                require(mode.isEmpty() || mode=="none" || mode=="major" || mode=="ionian" || mode=="minor" || mode=="aeolian","This key mode is not supported yet.");
                                KeyContext context{fifths,std::nullopt};
                                if(mode=="major" || mode=="ionian")context=KeyContext::fromSignature(fifths,ScaleIdentity::Major);
                                if(mode=="minor" || mode=="aeolian")context=KeyContext::fromSignature(fifths,ScaleIdentity::Minor,mode=="aeolian"?MinorForm::Aeolian:MinorForm::Melodic);
                                const int number=key.hasAttribute("number")?integer(key.attribute("number")):0;
                                require(number>=0 && number<=staffCount,"Invalid key staff.");
                                for(int s=0;s<staffCount;++s)if(!number || number==s+1) {
                                    auto& keys=staves[staffOffset+s].keys;
                                    const Tick tick=absolute+cursor;
                                    auto it=std::lower_bound(keys.begin(),keys.end(),tick,[](const KeyChange& k,Tick t){return k.tick<t;});
                                    if(it!=keys.end() && it->tick==tick)it->context=context;
                                    else keys.insert(it,{tick,context});
                                }
                            }
                            const auto time=n.firstChildElement("time");
                            if(!time.isNull()) {
                                require(cursor==0,"Meter changes must be at barlines.");
                                children(time, {
                                    "beats","beat-type"
                                });
                                require(time.firstChildElement("beats").nextSiblingElement("beats").isNull(),"Mixed-denominator meters are not supported.");
                                meter.groups.clear();
                                meter.numerator=0;
                                meter.denominator=integer(text(time,"beat-type"));
                                for(const auto& group:text(time,"beats").split('+')) {
                                    const int g=integer(group.trimmed());
                                    meter.groups.push_back(g);
                                    meter.numerator+=g;
                                }
                                require(meter.valid(),"Invalid time signature.");
                                if(meter.groups.size()==1)meter=Score::defaultMeter(meter.numerator,meter.denominator);
                                explicitMeter=true;
                            }
                            for(auto c=n.firstChildElement("clef");!c.isNull();c=c.nextSiblingElement("clef")) {
                                const int staff=integer(c.attribute("number","1"))-1;
                                require(staff>=0 && staff<staffCount,"Invalid clef staff.");
                                auto& cs=staves[staffOffset+staff].clefs;
                                const Tick tick=absolute+cursor;
                                auto it=std::lower_bound(cs.begin(),cs.end(),tick,[](const ClefChange& a,Tick t) {
                                    return a.tick<t;
                                });
                                if(it!=cs.end() && it->tick==tick)it->clef=readClef(c);
                                else cs.insert(it,ClefChange {
                                    tick,readClef(c)
                                });
                            }
                            continue;
                        }
                        if(n.tagName()=="backup" || n.tagName()=="forward") {
                            children(n, {
                                "duration","staff","voice"
                            });
                            const Tick raw=integer(text(n,"duration"));
                            require(raw>0 && raw*quarter%divisions==0,"Unsupported rhythmic resolution.");
                            cursor+=(n.tagName()=="backup"?-1:1)*raw*quarter/divisions;
                            require(cursor>=0 && cursor<=meter.ticks(),"Invalid MusicXML cursor movement.");
                            continue;
                        }
                        if(n.tagName()=="direction") {
                            require(cursor==0,"Tempo changes must be at measure starts.");
                            children(n, {
                                "direction-type","sound","staff"
                            });
                            const auto dt=n.firstChildElement("direction-type");
                            children(dt, {
                                "metronome"
                            });
                            const auto met=dt.firstChildElement("metronome");
                            children(met, {
                                "beat-unit","beat-unit-dot","per-minute"
                            });
                            Tick beat=valueFor(text(met,"beat-unit"));
                            if(!met.firstChildElement("beat-unit-dot").isNull())beat=beat*3/2;
                            tempo=TempoMark {
                                beat,integer(text(met,"per-minute"))
                            };
                            require(tempo->valid(),"Invalid tempo mark.");
                            const auto sound=n.firstChildElement("sound");
                            if(!sound.isNull()) {
                                bool ok=false;
                                const double rate=sound.attribute("tempo").toDouble(&ok);
                                require(ok && std::abs(rate-static_cast<double>(tempo->bpm)*beat/quarter)<0.001,"Inconsistent playback tempo.");
                            }
                            continue;
                        }
                        children(n, {
                            "chord","rest","pitch","duration","voice","type","dot","staff","instrument","tie","notations","beam","stem","accidental","time-modification"
                        });
                        require(text(n,"voice").isEmpty() || text(n,"voice")=="1","Only one voice per staff is supported.");
                        const int sn=text(n,"staff").isEmpty()?0:integer(text(n,"staff"))-1;
                        require(sn>=0 && sn<staffCount,"Invalid note staff.");
                        auto& staff=staves[staffOffset+sn];
                        auto& es=staff.measures[m].events;
                        const bool chord=!n.firstChildElement("chord").isNull(),rest=!n.firstChildElement("rest").isNull();
                        const Tick raw=integer(text(n,"duration"));
                        require(raw>0 && raw*quarter%divisions==0,"Unsupported rhythmic resolution.");
                        const Tick duration=raw*quarter/divisions;
                        Tick written=valueFor(text(n,"type"));
                        const auto dot=n.firstChildElement("dot");
                        require(dot.nextSiblingElement("dot").isNull(),"Only single dots are supported.");
                        if(!dot.isNull())written=written*3/2;
                        require(Score::validDuration(written),"Unsupported note duration.");
                        const auto modification=n.firstChildElement("time-modification");
                        children(modification,{"actual-notes","normal-notes","normal-type"});
                        require(modification.nextSiblingElement("time-modification").isNull(),"Duplicate time modification.");
                        const auto notation=n.firstChildElement("notations");
                        require(notation.nextSiblingElement("notations").isNull(),"Duplicate note notations.");
                        children(notation,{"tied","tuplet"});
                        bool groupStart=false,groupStop=false;
                        int startNumber=1,stopNumber=1;
                        for(auto mark=notation.firstChildElement("tuplet");!mark.isNull();mark=mark.nextSiblingElement("tuplet")) {
                            children(mark,{});
                            const int number=mark.hasAttribute("number")?integer(mark.attribute("number")):1;
                            require(number>0,"Invalid tuplet number.");
                            if(mark.attribute("type")=="start") {
                                require(!groupStart,"Duplicate tuplet start.");
                                groupStart=true;
                                startNumber=number;
                            }
                            else {
                                require(mark.attribute("type")=="stop" && !groupStop,"Unsupported tuplet notation.");
                                groupStop=true;
                                stopNumber=number;
                            }
                        }
                        const auto pitch=n.firstChildElement("pitch");
                        require(rest==pitch.isNull(),"A note needs a pitch or rest.");
                        Pitch p;
                        if(!rest) {
                            children(pitch, {
                                "step","alter","octave"
                            });
                            const auto step=text(pitch,"step");
                            require(step.size()==1 && QString("CDEFGAB").contains(step),"Invalid pitch step.");
                            p.step=QString("CDEFGAB").indexOf(step);
                            p.octave=integer(text(pitch,"octave"));
                            p.alter=text(pitch,"alter").isEmpty()?0:integer(text(pitch,"alter"));
                        }
                        Tick elapsed=0;
                        for(const auto& e:es)elapsed+=e.ticks;
                        const Tuplet* group=nullptr;
                        if(groupStart) {
                            require(!chord && !activeTuplets[sn] && !modification.isNull(),"Nested or incomplete tuplets are unsupported.");
                            const Tick unit=text(modification,"normal-type").isEmpty()?Score::baseDuration(written):valueFor(text(modification,"normal-type"));
                            const Tuplet t{objectId++,elapsed,integer(text(modification,"actual-notes")),integer(text(modification,"normal-notes")),unit};
                            require(t.valid(),"Unsupported tuplet ratio or reference note type.");
                            staff.measures[m].tuplets.push_back(t);
                            activeTuplets[sn]=staff.measures[m].tuplets.size()-1;
                            tupletNumbers[sn]=startNumber;
                        }
                        if(chord) {
                            require(!rest && !es.empty() && !es.back().isRest() && es.back().ticks==duration && es.back().writtenDuration()==written && !groupStart && !groupStop,"Invalid chord.");
                            if(es.back().tuplet)for(const auto& t:staff.measures[m].tuplets)if(t.id==es.back().tuplet)group=&t;
                        }
                        else {
                            require(cursor==elapsed,"Unsupported voice or overlapping note placement.");
                            if(activeTuplets[sn])group=&staff.measures[m].tuplets[*activeTuplets[sn]];
                            es.push_back(Event {
                                eventId++,duration,{},{},group?group->id:0,group?written:0
                            });
                            cursor+=duration;
                        }
                        if(group) {
                            require(!modification.isNull() && integer(text(modification,"actual-notes"))==group->actual && integer(text(modification,"normal-notes"))==group->normal &&
                                    (text(modification,"normal-type").isEmpty() || valueFor(text(modification,"normal-type"))==group->unit) && group->soundingDuration(written)==duration,"Inconsistent tuplet timing.");
                            require(chord || elapsed+duration<=group->start+group->span(),"Tuplet exceeds its span.");
                        }
                        else require(modification.isNull() && !groupStart && !groupStop && duration==written,"Unsupported note duration or missing tuplet boundaries.");
                        if(groupStop) {
                            require(!chord && group && stopNumber==tupletNumbers[sn] && elapsed+duration>group->start && elapsed+duration<=group->start+group->span(),"Unmatched or invalid tuplet stop.");
                            const Tick extent=elapsed+duration-group->start;
                            auto& finished=staff.measures[m].tuplets[*activeTuplets[sn]];
                            if(extent!=finished.unit*finished.normal)finished.length=extent;
                            activeTuplets[sn].reset();
                        }
                        auto& e=es.back();
                        if(!rest) {
                            e.pitches.push_back(p);
                            e.ties.resize(e.pitches.size());
                            bool start=false,stop=false;
                            for(auto tie=n.firstChildElement("tie");!tie.isNull();tie=tie.nextSiblingElement("tie")) {
                                require(tie.attribute("type")=="start" || tie.attribute("type")=="stop","Unsupported tie.");
                                start|=tie.attribute("type")=="start";
                                stop|=tie.attribute("type")=="stop";
                            }
                            bool visualStart=false,visualStop=false;
                            for(auto tie=notation.firstChildElement("tied");!tie.isNull();tie=tie.nextSiblingElement("tied")) {
                                require(tie.attribute("type")=="start" || tie.attribute("type")=="stop","Unsupported visual tie.");
                                visualStart|=tie.attribute("type")=="start";
                                visualStop|=tie.attribute("type")=="stop";
                            }
                            if(!notation.isNull())require(start==visualStart && stop==visualStop,"Inconsistent tie notation.");
                            auto& active=pending[sn];
                            if(stop) {
                                require(active.count(p),"Dangling tie stop.");
                                const auto source=active.at(p);
                                for(auto& oldMeasure:staff.measures)for(auto& sourceEvent:oldMeasure.events)if(sourceEvent.id==source)for(std::size_t t=0;t<sourceEvent.pitches.size();++t)if(sourceEvent.pitches[t]==p)sourceEvent.ties[t]=e.id;
                                active.erase(p);
                            }
                            if(start) {
                                require(!active.count(p),"Duplicate tie start.");
                                active[p]=e.id;
                            }
                        }
                        else require(n.firstChildElement("tie").isNull() && notation.firstChildElement("tied").isNull(),"Rests cannot be tied.");
                        const auto instrument=n.firstChildElement("instrument");
                        if(!instrument.isNull()) {
                            const auto iid=instrument.attribute("id");
                            require(programs.contains(iid),"Missing instrument definition.");
                            const int value=programs.value(iid);
                            staff.program=overrides[parts[pi].id].contains(sn+1)?overrides[parts[pi].id].value(sn+1):value==parts[pi].program?-1:value;
                            staff.name=instrumentNames.value(iid,QString::fromStdString(staff.name)).toStdString();
                        }
                    }
                    if(pi==0)bars.push_back(Bar {
                        meter,explicitMeter,tempo
                    });
                    else require(m<bars.size() && bars[m].meter==meter && (!tempo || bars[m].tempo==tempo),"Parts have different time signatures or tempos.");
                    for(int s=0;s<staffCount;++s)require(!activeTuplets[s] && Score::validMeasure(staves[staffOffset+s].measures[m],meter.ticks()),"Incomplete measure or unclosed/cross-measure tuplet.");
                    absolute+=meter.ticks();
                }
                require(m==bars.size(),"Parts have different measure counts.");
                for(auto si=minorForms[parts[pi].id].cbegin();si!=minorForms[parts[pi].id].cend();++si) {
                    require(si.key()>=1 && si.key()<=staffCount,"Invalid minor-form staff.");
                    auto& keys=staves[staffOffset+si.key()-1].keys;
                    for(auto form=si.value().cbegin();form!=si.value().cend();++form) {
                        auto key=std::lower_bound(keys.begin(),keys.end(),form.key(),[](const KeyChange& k,Tick t){return k.tick<t;});
                        require(key!=keys.end() && key->tick==form.key() && key->context.tonality && key->context.tonality->scale==ScaleIdentity::Minor,"Minor-form metadata has no matching key.");
                        key->context.tonality->minor=form.value();
                    }
                }
                for(const auto& active:pending)require(active.empty(),"Dangling tie start.");
            }
            require(pi==parts.size(),"Missing score part.");
            if(!bars.empty() && !bars[0].tempo)bars[0].tempo=TempoMark{};
            require(score->loadDocument(std::move(bars),std::move(staves),std::move(parts),std::move(groups)),"Invalid score relationships.");
            return true;
        }
        catch(const std::exception& e) {
            if(error)*error=QString::fromUtf8(e.what());
            return false;
        }
    }
}
// namespace music

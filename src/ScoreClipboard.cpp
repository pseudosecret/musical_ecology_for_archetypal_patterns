#include "ScoreClipboard.h"
#include <QDataStream>
#include <QIODevice>

namespace music {
namespace {
constexpr quint32 magic=0x4d414246;
constexpr quint16 version=1;
constexpr qsizetype maxBytes=32*1024*1024;
}

QByteArray encodeScoreFragment(const ScoreFragment& fragment)
{
    if(!fragment.valid())return {};
    QByteArray bytes;
    QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream<<magic<<version<<qint64(fragment.duration)<<quint32(fragment.rows.size());
    for(const auto& row:fragment.rows) {
        stream<<quint32(row.size());
        for(const auto& e:row) {
            stream<<qint64(e.ticks)<<quint64(e.group)<<qint32(e.actual)<<qint32(e.normal)<<qint64(e.unit)<<quint8(e.pitches.size());
            for(std::size_t p=0;p<e.pitches.size();++p)stream<<qint8(e.pitches[p].step)<<qint8(e.pitches[p].octave)<<qint8(e.pitches[p].alter)<<quint8(e.ties[p]);
        }
    }
    return stream.status()==QDataStream::Ok && bytes.size()<=maxBytes?bytes:QByteArray{};
}

std::optional<ScoreFragment> decodeScoreFragment(const QByteArray& bytes)
{
    if(bytes.isEmpty() || bytes.size()>maxBytes)return {};
    QDataStream stream(bytes);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 tag=0,rows=0; quint16 schema=0; qint64 duration=0;
    stream>>tag>>schema>>duration>>rows;
    if(stream.status()!=QDataStream::Ok || tag!=magic || schema!=version || !rows || rows>1024 || rows>quint64(stream.device()->bytesAvailable()/4))return {};
    ScoreFragment fragment{duration,{}};
    std::size_t total=0;
    for(quint32 r=0;r<rows;++r) {
        quint32 count=0;
        stream>>count;
        if(!count || count>250'000-total || count>quint64(stream.device()->bytesAvailable()/33))return {};
        total+=count;
        std::vector<FragmentEvent> row;
        row.reserve(count);
        for(quint32 i=0;i<count;++i) {
            qint64 ticks=0,unit=0; quint64 group=0; qint32 actual=0,normal=0; quint8 pitches=0;
            stream>>ticks>>group>>actual>>normal>>unit>>pitches;
            if(pitches>16 || stream.status()!=QDataStream::Ok)return {};
            FragmentEvent e{ticks,{}, {},actual,normal,unit,group};
            for(quint8 p=0;p<pitches;++p) {
                qint8 step=0,octave=0,alter=0; quint8 tie=0;
                stream>>step>>octave>>alter>>tie;
                if(tie>1 || stream.status()!=QDataStream::Ok)return {};
                e.pitches.push_back(Pitch{step,octave,alter});
                e.ties.push_back(tie!=0);
            }
            row.push_back(std::move(e));
        }
        fragment.rows.push_back(std::move(row));
    }
    if(stream.status()!=QDataStream::Ok || !stream.atEnd() || !fragment.valid())return {};
    return fragment;
}
}

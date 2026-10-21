#pragma once
#include "Transformations.h"
#include <QJsonObject>
#include <QStringList>

namespace music {
// Application data, separate from the canonical musical document.
struct AnalysisSourceEvent {
    EventId id=0;
    Tick written=0;
    bool clippedStart=false,clippedEnd=false;
};
struct AnalysisSnapshot {
    ScoreFragment fragment;
    std::vector<Bar> bars;
    std::vector<ClefChange> clefs;
    std::vector<KeyChange> keys,selectedKeys;
    std::vector<AnalysisSourceEvent> sources;
    Tick offset=0;
    int firstMeasure=0,program=0;
    QString label;
};
struct AnalysisSpan { Tick start=0,end=0; };
struct AnalysisFinding {
    QString id,kind,title,text;
    double strength=0;
    double salience=-1;
    QStringList salienceReasons;
    std::vector<AnalysisSpan> spans;
    QString review="undecided",name;
};
struct MelodyAnalysis {
    QJsonObject data;
    std::vector<AnalysisFinding> findings;
    QString report;
    void updateReport();
    [[nodiscard]] QJsonObject structured() const;
};
std::optional<AnalysisSnapshot> captureAnalysis(const Score& score,const ScoreRange& range,const QString& label,QString* error);
std::optional<TransformResult> analysisScratch(const AnalysisSnapshot& snapshot,QString* error);
QByteArray melodyAnalysisRequest(const AnalysisSnapshot& snapshot,const QString& id);
std::optional<MelodyAnalysis> melodyAnalysisResponse(const QByteArray& bytes,const QString& id,const AnalysisSnapshot& snapshot,QString* error);
}

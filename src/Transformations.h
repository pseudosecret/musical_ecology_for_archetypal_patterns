#pragma once
#include "Score.h"
#include <QByteArray>
#include <QString>
#include <QVariantList>

namespace music {
enum class TransformKind { Transpose, Invert, Scale, Retrograde };
enum class PitchMode { Semitones, Degrees };
enum class AccidentalPolicy { Preserve, Scale };
struct TransformStep {
    TransformKind kind=TransformKind::Transpose;
    int amount=0;
    int numerator=1,denominator=1;
    PitchMode mode=PitchMode::Semitones;
    AccidentalPolicy accidentals=AccidentalPolicy::Preserve;
    int axisDegree=1,tonicOctave=4;
    bool reverseKeys=false;
};
struct TransformContext {
    Meter meter;
    TempoMark tempo;
    Clef clef=Clef::Treble;
    int program=0;
    Tick offset=0;
    QString label;
    std::vector<KeyChange> keys{{0,{}}}; // Selection-relative positions.
};
struct TransformResult {
    Score score{1};
    Tick start=0,end=0;
};
constexpr qsizetype transformMessageLimit=32*1024*1024;
std::optional<std::vector<TransformStep>> transformSteps(const QVariantList& values,QString* error);
QByteArray transformationRequest(const ScoreFragment& fragment,const std::vector<TransformStep>& steps,const QString& id,const std::vector<KeyChange>& keys={{0,{}}});
QByteArray keyAnalysisRequest(const ScoreFragment& fragment,const std::vector<KeyChange>& keys,const QString& id);
std::optional<QVariantList> keyAnalysisResponse(const QByteArray& bytes,const QString& id,const std::vector<KeyChange>& keys,QString* error);
std::optional<TransformResult> transformationResponse(const QByteArray& bytes,const QString& id,const TransformContext& context,QString* error);
std::optional<TransformResult> buildTransformationResult(const ScoreFragment& proposed,const TransformContext& context,QString* error);
}

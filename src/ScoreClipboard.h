#pragma once
#include "Score.h"
#include <QByteArray>

namespace music {
inline constexpr auto scoreFragmentMime="application/x-music-as-becoming-score-fragment";
QByteArray encodeScoreFragment(const ScoreFragment& fragment);
std::optional<ScoreFragment> decodeScoreFragment(const QByteArray& bytes);
}

#pragma once

#include "Score.h"

#include <QString>

namespace music {

class MusicXml {
public:
    static bool save(const Score& score, const QString& path, QString* error = nullptr);
    static bool open(const QString& path, Score* score, QString* error = nullptr);
};

} // namespace music

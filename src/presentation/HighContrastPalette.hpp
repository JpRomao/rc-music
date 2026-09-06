#pragma once

#include <QtGui/QColor>

namespace music_player::m0 {

// System colors are validated as fully opaque before the UI contract publishes them.
struct HighContrastPalette final {
    QColor window{Qt::black};
    QColor window_text{Qt::white};
    QColor highlight{Qt::yellow};
    QColor highlighted_text{Qt::black};
    QColor disabled_text{128, 128, 128};

    bool operator==(const HighContrastPalette&) const = default;
};

} // namespace music_player::m0

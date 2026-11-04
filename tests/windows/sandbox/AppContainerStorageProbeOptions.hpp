#pragma once

#include <QString>
#include <QStringList>

namespace music_player::sandbox_probe {
// Pure lexical admission. No file/token/profile/API lookup. This does not
// establish freshness, absence of reparse points or permission to create a
// profile; the explicit native manual mode must check those separately.
struct AppContainerStorageProbeOptions final {
    enum class Mode { Refused, Help, SelfTest, Inventory };
    Mode mode{Mode::Refused};
    QString evidence{};
    static constexpr qsizetype maximum_path_units = 2048;
};
// The workspace is supplied by the compiled trusted composition, never argv or
// the environment. Require its exact spelling after slash normalization so a
// case-sensitive sibling such as PRODUCT cannot be mistaken for out/product.
[[nodiscard]] AppContainerStorageProbeOptions
appContainerStorageProbeOptions(const QStringList& arguments, const QString& compiled_workspace) noexcept;
} // namespace music_player::sandbox_probe

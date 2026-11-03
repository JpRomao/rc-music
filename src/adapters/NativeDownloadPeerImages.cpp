#include "adapters/NativeDownloadPeerImages.hpp"

#include <QStringView>
#include <qt_windows.h>

#include <algorithm>
#include <array>

namespace music_player::m0 {
QStringList nativeDownloadSiblingImages(const QStringList& names) {
    constexpr qsizetype maximum_path_characters = 32'767;
    if (names.isEmpty() || names.size() > 4)
        return {};
    for (const auto& name : names) {
        if (name.size() <= 4 || name.size() > maximum_path_characters ||
            !name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            return {};
        const auto stem = QStringView{name}.first(name.size() - 4);
        if (!std::all_of(stem.cbegin(), stem.cend(), [](const QChar character) {
                const auto unit = character.unicode();
                return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
                       unit == '_' || unit == '-';
            }))
            return {};
    }

    std::array<wchar_t, maximum_path_characters + 1> buffer{};
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size())
        return {};
    // Windows supplies the fully qualified image path, preserving its Unicode
    // and extended-path form. No directory or sibling is opened or resolved.
    const auto image = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(size));
    const auto separator = std::max(image.lastIndexOf(QLatin1Char('\\')), image.lastIndexOf(QLatin1Char('/')));
    if (separator < 0 || separator == image.size() - 1)
        return {};
    const auto directory = image.first(separator + 1);
    QStringList result;
    result.reserve(names.size());
    for (const auto& name : names) {
        if (directory.size() > maximum_path_characters - name.size())
            return {};
        result.append(directory + name);
    }
    return result;
}
} // namespace music_player::m0

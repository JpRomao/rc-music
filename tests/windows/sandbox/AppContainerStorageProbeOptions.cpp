#include "AppContainerStorageProbeOptions.hpp"

namespace music_player::sandbox_probe {
namespace {
using Options = AppContainerStorageProbeOptions;
bool component(const QString& value) {
    if (value.isEmpty() || value.size() > 255 || value == "." || value == ".." || value.endsWith('.') ||
        value.endsWith(' '))
        return false;
    const auto stem = value.section('.', 0, 0).trimmed().toLower();
    const bool serial = stem.size() == 4 && (stem.startsWith("com") || stem.startsWith("lpt")) &&
                        ((stem[3] >= QChar{'1'} && stem[3] <= QChar{'9'}) || stem[3].unicode() == 0x00b9 ||
                         stem[3].unicode() == 0x00b2 || stem[3].unicode() == 0x00b3);
    if (serial || stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" ||
        stem == "conout$")
        return false;
    for (const auto unit : value)
        if (unit.unicode() < 32 || QStringLiteral(":<>\"|?*").contains(unit))
            return false;
    return true;
}
bool evidencePath(const QString& path, const QString& workspace) {
    if (path.size() < 3 || path.size() > Options::maximum_path_units || !path.isValidUtf16() ||
        !workspace.isValidUtf16() || workspace.endsWith('/') ||
        !path.startsWith(workspace + QStringLiteral("/out/product/"), Qt::CaseSensitive) ||
        !((path[0] >= QChar{'A'} && path[0] <= QChar{'Z'}) || (path[0] >= QChar{'a'} && path[0] <= QChar{'z'})) ||
        path[1] != ':' || path[2] != '/')
        return false;
    for (const auto& name : path.mid(3).split('/'))
        if (!component(name))
            return false;
    return true;
}
QString slashOnly(const QString& value) {
    QString result(value.constData(), value.size());
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return result;
}
} // namespace
AppContainerStorageProbeOptions appContainerStorageProbeOptions(const QStringList& arguments,
                                                                const QString& compiled_workspace) noexcept {
    try {
        if (arguments == QStringList{QStringLiteral("--help")})
            return {Options::Mode::Help, {}};
        if (arguments == QStringList{QStringLiteral("--self-test")})
            return {Options::Mode::SelfTest, {}};
        if (arguments.size() != 3 || arguments[0] != QStringLiteral("--allow-profile-changes") ||
            arguments[1] != QStringLiteral("--evidence-dir"))
            return {};
        if (arguments[2].size() > Options::maximum_path_units ||
            compiled_workspace.size() > Options::maximum_path_units)
            return {};
        // QDir::fromNativeSeparators may strip a Windows device prefix. Only
        // replace slash characters; the original namespace must stay visible.
        const auto path = slashOnly(arguments[2]);
        const auto workspace = slashOnly(compiled_workspace);
        if (evidencePath(path, workspace))
            return {Options::Mode::Inventory, QString(path.constData(), path.size())};
    } catch (...) {
        // Admission failure cannot fall through to a native mode.
    }
    return {};
}
} // namespace music_player::sandbox_probe

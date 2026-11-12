#pragma once

#include "adapters/StrictJson.hpp"
#include "adapters/WindowsYoutubeRuntime.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test_support {
// TEST ONLY: external observations for the explicitly invoked offline smoke.
// This is data, never publisher trust or production launch configuration.
struct YoutubeRuntimeReceiptSource final {
    QString name{}, version{}, path{}, provenance{};
    QByteArray sha256{};
};
struct YoutubeRuntimeReceipt final {
    QString artifact_root{};
    std::vector<YoutubeRuntimeReceiptSource> sources{};
    // runtime_parent is deliberately left empty until the driver owns TEMP.
    WindowsYoutubeRuntimeConfig config{};
    std::vector<quint64> declared_sizes{};
    quint64 total_bytes{};
};
enum class YoutubeRuntimeReceiptError {
    None,
    InvalidJson,
    InvalidSchema,
    InvalidSource,
    InvalidArtifact,
    LimitExceeded
};
struct YoutubeRuntimeReceiptResult final {
    YoutubeRuntimeReceiptError error{YoutubeRuntimeReceiptError::InvalidSchema};
    std::optional<YoutubeRuntimeReceipt> receipt{};
};
inline constexpr qsizetype youtube_runtime_receipt_max_bytes = 8 * 1024 * 1024;
namespace youtube_receipt_detail {
inline bool fields(const QJsonObject& object, const std::initializer_list<const char*> names) {
    return object.size() == static_cast<qsizetype>(names.size()) &&
           std::all_of(names.begin(), names.end(),
                       [&object](const char* name) { return object.contains(QLatin1String(name)); });
}
inline bool sha256(const QString& text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](const QChar unit) {
               return (unit >= u'0' && unit <= u'9') || (unit >= u'a' && unit <= u'f');
           });
}
inline bool reserved(const QString& component) {
    const auto base = component.section(u'.', 0, 0).toUpper();
    if (base == QLatin1String("CON") || base == QLatin1String("PRN") || base == QLatin1String("AUX") ||
        base == QLatin1String("NUL"))
        return true;
    return base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))) &&
           base.at(3) >= u'1' && base.at(3) <= u'9';
}
// Same local-path shape as the Windows preparer, without its drive/filesystem IO.
inline std::optional<QString> localPath(QString text) {
    if (text.size() < 4 || text.size() > 30000 || text.at(1) != u':' || (text.at(2) != u'/' && text.at(2) != u'\\') ||
        !((text.at(0) >= u'A' && text.at(0) <= u'Z') || (text.at(0) >= u'a' && text.at(0) <= u'z')))
        return {};
    text.replace(u'\\', u'/');
    if (QString::fromUtf8(text.toUtf8()) != text)
        return {};
    const auto parts = text.mid(3).split(u'/');
    if (parts.size() > 256)
        return {};
    for (const auto& part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..") || part.endsWith(u'.') ||
            part.endsWith(u' ') || reserved(part))
            return {};
        for (const auto unit : part)
            if (unit.category() == QChar::Other_Control || QStringLiteral(":<>\"|?*").contains(unit))
                return {};
    }
    return text;
}
inline YoutubeRuntimeReceiptError relative(const QString& text) {
    if (text.isEmpty())
        return YoutubeRuntimeReceiptError::InvalidArtifact;
    if (text.size() > youtube_runtime_max_relative_units || text.count(u'/') + 1 > youtube_runtime_max_segments)
        return YoutubeRuntimeReceiptError::LimitExceeded;
    for (const auto& part : text.split(u'/')) {
        if (part.isEmpty() || part.size() > 255 || part == QLatin1String(".") || part == QLatin1String("..") ||
            part.endsWith(u'.') || reserved(part))
            return YoutubeRuntimeReceiptError::InvalidArtifact;
        for (const auto unit : part)
            if (!((unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9') ||
                  unit == u'_' || unit == u'-' || unit == u'.'))
                return YoutubeRuntimeReceiptError::InvalidArtifact;
    }
    return YoutubeRuntimeReceiptError::None;
}
inline constexpr std::array<std::pair<const char*, const char*>, 17> required_sources{
    {{"python", "3.13.1"},
     {"node", "24.22.3"},
     {"altgraph", "0.17.5"},
     {"packaging", "26.3"},
     {"pefile", "2024.8.26"},
     {"pyinstaller", "6.22.3"},
     {"pyinstaller-hooks-contrib", "2026.9"},
     {"pywin32-ctypes", "0.2.3"},
     {"setuptools", "84.2.1"},
     {"yt-dlp", "2026.3.17"},
     {"yt-dlp-ejs", "0.8.0"},
     {"entrypoint", "project"},
     {"offline-entry", "project"},
     {"fixture", "project"},
     {"spec", "project"},
     {"hook", "project"},
     {"build-lock", "project"}}};
inline constexpr std::array<const char*, 5> optional_sources{
    {"python-license", "node-license", "yt-dlp-license", "ejs-license", "pyinstaller-license"}};
inline constexpr std::array<std::pair<const char*, const char*>, 2> reviewed_expat_modules{
    {{"pyexpat", "extractor/_internal/pyexpat.pyd"}, {"elementtree", "extractor/_internal/_elementtree.pyd"}}};
inline constexpr std::array<const char*, 4> role_names{{"Extractor", "Node", "EjsCore", "EjsLibrary"}};
inline constexpr std::array<const char*, 4> role_paths{{"extractor/yt-dlp.exe", "javascript/node.exe",
                                                        "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js",
                                                        "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"}};
inline constexpr std::array<WindowsYoutubeRuntimeRole, 4> roles{
    {WindowsYoutubeRuntimeRole::Extractor, WindowsYoutubeRuntimeRole::Node, WindowsYoutubeRuntimeRole::EjsCore,
     WindowsYoutubeRuntimeRole::EjsLibrary}};
inline constexpr std::array<const char*, 5> notice_paths{{"notices/python-LICENSE.txt", "notices/node-LICENSE.txt",
                                                          "notices/yt-dlp-LICENSE.txt", "notices/ejs-LICENSE.txt",
                                                          "notices/pyinstaller-COPYING.txt"}};
inline bool matchesVersion(const QString& name, const QString& observed) {
    if (name == QLatin1String("python") && observed == QLatin1String("3.14.10"))
        return true;
    if (name == QLatin1String("node") && observed == QLatin1String("24.20.0"))
        return true; // Historical offline receipts.
    for (const auto& [known, pinned] : required_sources)
        if (name == QLatin1String(known))
            return observed == QLatin1String(pinned);
    for (const auto known : optional_sources)
        if (name == QLatin1String(known))
            return observed == QLatin1String("project");
    for (const auto& module : reviewed_expat_modules)
        if (name == QLatin1String(module.first))
            return observed == QLatin1String("2.10.0");
    return false;
}
inline bool provenance(const QString& text) {
    return !text.trimmed().isEmpty() && text.size() <= 2048 &&
           std::none_of(text.begin(), text.end(),
                        [](const QChar unit) { return unit.category() == QChar::Other_Control; });
}
} // namespace youtube_receipt_detail

// Pure Qt JSON data decoder: no filesystem, environment, imports or native IO.
[[nodiscard]] inline YoutubeRuntimeReceiptResult decodeYoutubeRuntimeReceipt(const QByteArray& bytes) {
    using Error = YoutubeRuntimeReceiptError;
    using namespace youtube_receipt_detail;
    const auto failed = [](const Error error) { return YoutubeRuntimeReceiptResult{error, {}}; };
    if (bytes.size() > youtube_runtime_receipt_max_bytes)
        return failed(Error::LimitExceeded);
    if (!detail::strictJsonPreflight(bytes, 8))
        return failed(Error::InvalidJson);
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject())
        return failed(Error::InvalidSchema);
    const auto object = document.object();
    if (!fields(object, {"schemaVersion", "purpose", "artifact_root", "sources", "artifacts"}) ||
        !object.value("schemaVersion").isDouble() || object.value("schemaVersion").toDouble() != 1 ||
        object.value("purpose") != QJsonValue(QStringLiteral("offline-synthetic-v1")) ||
        !object.value("artifact_root").isString() || !object.value("sources").isArray() ||
        !object.value("artifacts").isArray())
        return failed(Error::InvalidSchema);
    const auto root = localPath(object.value("artifact_root").toString());
    if (!root)
        return failed(Error::InvalidSchema);
    YoutubeRuntimeReceipt receipt;
    receipt.artifact_root = *root;
    const auto sources = object.value("sources").toArray();
    if (sources.size() > 64)
        return failed(Error::LimitExceeded);
    QSet<QString> source_names;
    for (const auto value : sources) {
        if (!value.isObject())
            return failed(Error::InvalidSource);
        const auto source = value.toObject();
        if (!fields(source, {"name", "version", "path", "sha256", "provenance"}))
            return failed(Error::InvalidSource);
        for (const auto key : {"name", "version", "path", "sha256", "provenance"})
            if (!source.value(QLatin1String(key)).isString())
                return failed(Error::InvalidSource);
        const auto name = source.value("name").toString();
        const auto observed_version = source.value("version").toString();
        const auto path = localPath(source.value("path").toString());
        const auto pin = source.value("sha256").toString();
        const auto observed = source.value("provenance").toString();
        if (!matchesVersion(name, observed_version) || source_names.contains(name) || !path || !sha256(pin) ||
            !provenance(observed))
            return failed(Error::InvalidSource);
        source_names.insert(name);
        receipt.sources.push_back({name, observed_version, *path, observed, pin.toLatin1()});
    }
    for (const auto& [name, pinned] : required_sources) {
        static_cast<void>(pinned);
        if (!source_names.contains(QLatin1String(name)))
            return failed(Error::InvalidSource);
    }
    const auto find_source = [&receipt](const char* name) {
        return std::find_if(receipt.sources.cbegin(), receipt.sources.cend(),
                            [name](const auto& source) { return source.name == QLatin1String(name); });
    };
    const auto expat_sources = std::count_if(
        reviewed_expat_modules.cbegin(), reviewed_expat_modules.cend(),
        [&source_names](const auto& module) { return source_names.contains(QLatin1String(module.first)); });
    if (expat_sources != 0 && (expat_sources != static_cast<qsizetype>(reviewed_expat_modules.size()) ||
                               find_source("python")->version != QLatin1String("3.14.10")))
        return failed(Error::InvalidSource);
    const auto artifacts = object.value("artifacts").toArray();
    if (artifacts.size() > static_cast<qsizetype>(youtube_runtime_max_files))
        return failed(Error::LimitExceeded);
    std::array<bool, 4> unique_roles{};
    QSet<QString> destinations;
    for (const auto value : artifacts) {
        if (!value.isObject())
            return failed(Error::InvalidArtifact);
        const auto artifact = value.toObject();
        if (!fields(artifact, {"role", "path", "bytes", "sha256"}) || !artifact.value("role").isString() ||
            !artifact.value("path").isString() || !artifact.value("sha256").isString() ||
            !artifact.value("bytes").isDouble())
            return failed(Error::InvalidArtifact);
        const auto count = artifact.value("bytes").toInteger(-1);
        if (count <= 0 || artifact.value("bytes").toDouble() != static_cast<double>(count))
            return failed(Error::InvalidArtifact);
        if (count > 128LL * 1024 * 1024 ||
            static_cast<quint64>(count) > youtube_runtime_max_bytes - receipt.total_bytes)
            return failed(Error::LimitExceeded);
        const auto path = artifact.value("path").toString();
        const auto pin = artifact.value("sha256").toString();
        const auto path_error = relative(path);
        if (path_error != Error::None)
            return failed(path_error);
        if (!sha256(pin))
            return failed(Error::InvalidArtifact);
        const auto key = path.toCaseFolded();
        for (const auto& existing : destinations)
            if (existing == key || existing.startsWith(key + '/') || key.startsWith(existing + '/'))
                return failed(Error::InvalidArtifact);
        const auto name = artifact.value("role").toString();
        auto role = WindowsYoutubeRuntimeRole::Support;
        bool matched{};
        for (std::size_t index = 0; index < role_names.size(); ++index) {
            if (name != QLatin1String(role_names[index]))
                continue;
            if (unique_roles[index] || path != QLatin1String(role_paths[index]))
                return failed(Error::InvalidArtifact);
            unique_roles[index] = true;
            matched = true;
            role = roles[index];
            break;
        }
        if (!matched && (name != QLatin1String("Support") || !(path.startsWith(QLatin1String("extractor/_internal/")) ||
                                                               path.startsWith(QLatin1String("notices/")))))
            return failed(Error::InvalidArtifact);
        const auto source_path = localPath(receipt.artifact_root + '/' + path);
        if (!source_path)
            return failed(Error::InvalidArtifact);
        destinations.insert(key);
        receipt.config.artifacts.push_back({role, path, *source_path, pin.toLatin1()});
        receipt.declared_sizes.push_back(static_cast<quint64>(count));
        receipt.total_bytes += static_cast<quint64>(count);
    }
    if (std::any_of(unique_roles.begin(), unique_roles.end(), [](const bool present) { return !present; }))
        return failed(Error::InvalidArtifact);
    for (const auto path : notice_paths)
        if (!destinations.contains(QString::fromLatin1(path).toCaseFolded()))
            return failed(Error::InvalidArtifact);
    if (expat_sources != 0) {
        for (const auto& [name, path] : reviewed_expat_modules) {
            const auto artifact =
                std::find_if(receipt.config.artifacts.cbegin(), receipt.config.artifacts.cend(),
                             [path](const auto& row) { return row.relative_path == QLatin1String(path); });
            if (artifact == receipt.config.artifacts.cend() || artifact->sha256 != find_source(name)->sha256)
                return failed(Error::InvalidArtifact);
        }
    }
    return {Error::None, std::move(receipt)};
}
} // namespace music_player::m0::test_support

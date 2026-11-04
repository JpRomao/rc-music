#include "AppContainerStorageInventoryJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Codec = AppContainerStorageInventoryJson;
using Inventory = Codec::Inventory;
constexpr std::array<const char*, 2> statuses{"incomplete", "observed"};
constexpr std::array<const char*, 11> reasons{"none", "limits",   "source", "provenance", "root",     "metadata",
                                              "name", "identity", "bounds", "changed",    "exception"};
constexpr std::array<const char*, 4> provenances{"unknown", "live-created", "loaded", "revoked"};
constexpr std::array<const char*, 4> roots{"package-root", "ac", "mappings-sid", "storage-sid"};
constexpr std::array<const char*, 3> presences{"unknown", "present", "missing"};
constexpr std::array<const char*, 4> types{"unknown", "directory", "file", "registry-key"};
constexpr std::array<const char*, 4> dacls{"unknown", "absent", "null", "present"};
constexpr std::array<const char*, 3> validations{"unknown", "unchanged", "changed"};
constexpr std::array<const char*, 3> closures{"not-attempted", "closed", "unknown"};

template <typename Enum, std::size_t Size> bool valid(const Enum value, const std::array<const char*, Size>&) {
    return static_cast<std::size_t>(value) < Size;
}
template <typename Enum, std::size_t Size> QString text(const Enum value, const std::array<const char*, Size>& names) {
    return QString::fromLatin1(names[static_cast<std::size_t>(value)]);
}
QString decimal(const std::uint64_t value) { return QString::number(value); }
bool relativeName(const QString& name) {
    // Empty is the root object's unnamed relative component. No ambient path
    // field exists, and nonempty values must stay within one relative name.
    if (name.isEmpty())
        return true;
    if (!name.isValidUtf16() || name == "." || name == "..")
        return false;
    for (const auto unit : name)
        if (unit.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(unit))
            return false;
    return true;
}
Codec::Error preflight(const Inventory::Report& report, const Inventory::ValidationResult post_validation,
                       const Inventory::CloseResult closure, std::uint64_t& payload) {
    if (report.objects.size() > Inventory::maximum_objects || report.total_bytes > Inventory::maximum_total_bytes)
        return Codec::Error::Bounds;
    if (!valid(report.status, statuses) || !valid(report.reason, reasons) || !valid(report.provenance, provenances) ||
        !valid(report.validation.state, validations) || !valid(post_validation.state, validations) ||
        !valid(closure.state, closures))
        return Codec::Error::InvalidEnum;
    for (std::size_t index = 0; index < report.roots.size(); ++index) {
        const auto& root = report.roots[index];
        if (!valid(root.root, roots) || !valid(root.result.presence, presences) || !valid(root.presence, presences))
            return Codec::Error::InvalidEnum;
        if (root.root != static_cast<Inventory::Root>(index) ||
            (root.object_index && *root.object_index >= report.objects.size()))
            return Codec::Error::InvalidReport;
    }
    for (const auto& object : report.objects) {
        const auto& metadata = object.metadata;
        if (!valid(object.root, roots) || !valid(metadata.presence, presences) || !valid(metadata.type, types) ||
            !valid(metadata.dacl, dacls))
            return Codec::Error::InvalidEnum;
        // Bound every field before scanning names or allocating hex/base64/JSON.
        if (object.depth > Inventory::maximum_depth || object.name.size() > Inventory::maximum_name_units ||
            static_cast<std::uint64_t>(metadata.owner_sid.size()) > Codec::maximum_owner_sid_bytes ||
            static_cast<std::uint64_t>(metadata.raw_sd.size()) > Inventory::maximum_sd_bytes)
            return Codec::Error::Bounds;
        if (!relativeName(object.name))
            return Codec::Error::InvalidReport;
        const auto bytes = static_cast<std::uint64_t>(metadata.owner_sid.size() + metadata.raw_sd.size()) +
                           2 * static_cast<std::uint64_t>(object.name.size());
        if (bytes > Inventory::maximum_total_bytes - payload)
            return Codec::Error::Bounds;
        payload += bytes;
    }
    return Codec::Error::None;
}
QJsonObject validation(const Inventory::ValidationResult value) {
    return {{"state", text(value.state, validations)}, {"error", static_cast<qint64>(value.error)}};
}
QJsonObject close(const Inventory::CloseResult value) {
    return {{"state", text(value.state, closures)}, {"error", static_cast<qint64>(value.error)}};
}
QJsonObject metadata(const Inventory::Metadata& value) {
    return {{"ref", decimal(value.ref.value)},
            {"localIdentity",
             QJsonObject{{"domain", decimal(value.identity.domain)}, {"object", decimal(value.identity.object)}}},
            {"presence", text(value.presence, presences)},
            {"type", text(value.type, types)},
            {"error", static_cast<qint64>(value.error)},
            {"linkCount", static_cast<qint64>(value.link_count)},
            {"attributes", static_cast<qint64>(value.attributes)},
            {"reparseTag", static_cast<qint64>(value.reparse_tag)},
            {"reparse", value.reparse},
            {"registryLink", value.registry_link},
            {"ownerSidHex", QString::fromLatin1(value.owner_sid.toHex())},
            {"rawSdBase64", QString::fromLatin1(value.raw_sd.toBase64())},
            {"dacl", text(value.dacl, dacls)},
            {"daclProtected", value.dacl_protected},
            {"saclNotQueried", value.sacl_not_queried}};
}
QJsonObject root(const Inventory::RootObservation& value) {
    return {{"root", text(value.root, roots)},
            {"lookup", QJsonObject{{"ref", decimal(value.result.ref.value)},
                                   {"error", static_cast<qint64>(value.result.error)},
                                   {"presence", text(value.result.presence, presences)}}},
            {"presence", text(value.presence, presences)},
            {"metadataError", static_cast<qint64>(value.metadata_error)},
            {"objectIndex",
             value.object_index ? QJsonValue{static_cast<qint64>(*value.object_index)} : QJsonValue{QJsonValue::Null}}};
}
QJsonObject object(const Inventory::Object& value) {
    return {{"root", text(value.root, roots)},
            {"parentRef", decimal(value.parent.value)},
            {"name", value.name},
            {"depth", static_cast<qint64>(value.depth)},
            {"metadata", metadata(value.metadata)}};
}
QJsonObject hardLimits() {
    return {{"objects", static_cast<qint64>(Inventory::maximum_objects)},
            {"depth", static_cast<qint64>(Inventory::maximum_depth)},
            {"rawSdBytes", decimal(Inventory::maximum_sd_bytes)},
            {"ownerSidBytes", decimal(Codec::maximum_owner_sid_bytes)},
            {"nameUtf16Units", static_cast<qint64>(Inventory::maximum_name_units)},
            {"payloadBytes", decimal(Inventory::maximum_total_bytes)},
            {"jsonBytes", decimal(Codec::maximum_json_bytes)}};
}
QJsonObject qualification() {
    return {{"identityScope", "session-local"}, {"securityPolicyApproved", false},
            {"mandatoryLabelProven", false},    {"ordinaryRegistryKeyTypeProven", false},
            {"atomicSnapshotProven", false},    {"persistentObjectIdentityProven", false},
            {"cleanupAuthorityGranted", false}};
}
} // namespace

AppContainerStorageInventoryJson::Result
AppContainerStorageInventoryJson::encode(const Inventory::Report& report,
                                         const Inventory::ValidationResult post_validation,
                                         const Inventory::CloseResult closure) noexcept {
    try {
        std::uint64_t payload = 0;
        const auto error = preflight(report, post_validation, closure, payload);
        if (error != Error::None)
            return {error};
        QJsonArray root_values, object_values;
        for (const auto& value : report.roots)
            root_values.append(root(value));
        for (const auto& value : report.objects)
            object_values.append(object(value));
        const QJsonObject snapshot{{"status", text(report.status, statuses)},
                                   {"reason", text(report.reason, reasons)},
                                   {"provenance", text(report.provenance, provenances)},
                                   {"beginError", static_cast<qint64>(report.begin_error)},
                                   {"reportedPayloadBytes", decimal(report.total_bytes)},
                                   {"validation", validation(report.validation)},
                                   {"roots", root_values},
                                   {"objects", object_values}};
        const QJsonObject document{{"schema", 1},
                                   {"kind", "appcontainer-storage-inventory"},
                                   {"diagnosticOnly", true},
                                   {"qualification", qualification()},
                                   {"hardLimits", hardLimits()},
                                   {"snapshot", snapshot},
                                   {"postValidation", validation(post_validation)},
                                   {"closure", close(closure)},
                                   {"encodedPayloadBytes", decimal(payload)}};
        auto bytes = QJsonDocument{document}.toJson(QJsonDocument::Compact);
        if (static_cast<std::uint64_t>(bytes.size()) > maximum_json_bytes)
            return {Error::Bounds};
        return {Error::None, std::move(bytes), payload};
    } catch (...) {
        return {Error::Exception};
    }
}
} // namespace music_player::sandbox_probe

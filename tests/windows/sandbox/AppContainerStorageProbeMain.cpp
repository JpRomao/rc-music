// Manual, separately authorized profile inventory. Automatic mode is pure
// argument/receipt self-test only. No worker, network, WFP or elevation launcher.
#include "AppContainerObserverIdentity.hpp"
#include "AppContainerStorageInventoryJson.hpp"
#include "AppContainerStorageInventoryRun.hpp"
#include "AppContainerStorageLiveSource.hpp"
#include "AppContainerStorageNativeSession.hpp"
#include "AppContainerStorageProbeEvidence.hpp"
#include "AppContainerStorageProbeOptions.hpp"
#include "adapters/WindowsDownloadMediaProfileStore.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QUuid>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <optional>
#include <stdexcept>
#include <string>

using namespace music_player::m0;
using namespace music_player::sandbox_probe;
namespace {
using Inventory = AppContainerStorageInventory;
using Run = AppContainerStorageInventoryRun;
using Codec = AppContainerStorageInventoryJson;
using Evidence = AppContainerStorageProbeEvidence;
using Options = AppContainerStorageProbeOptions;
using Identity = AppContainerObserverIdentity;
QString workspace() { return QString::fromUtf8(RC_MUSIC_SANDBOX_WORKSPACE_ROOT); }
void require(const bool condition, const char* failure) {
    if (!condition)
        throw std::runtime_error(failure);
}
QJsonValue raw(const std::optional<qint32> value) {
    return value ? QJsonValue{static_cast<qint64>(*value)} : QJsonValue{QJsonValue::Null};
}
bool matchingRemovedRecord(const DownloadMediaProfileLoadResult& loaded, const std::string& operation,
                           const QByteArray& sid) noexcept {
    return loaded.status == DownloadCheckpointLoadStatus::Loaded && loaded.record &&
           validDownloadMediaProfileRecord(*loaded.record) && loaded.record->job_id == "storage-inventory-probe" &&
           loaded.record->operation_id == operation && loaded.record->stage == DownloadMediaProfileStage::Removed &&
           loaded.record->sid && sid.size() == static_cast<qsizetype>(loaded.record->sid->size()) &&
           std::memcmp(loaded.record->sid->data(), sid.constData(), loaded.record->sid->size()) == 0;
}
QByteArray sidBytes(const Identity::Bytes& sid) {
    return QByteArray(reinterpret_cast<const char*>(sid.data()), static_cast<qsizetype>(sid.size()));
}
QByteArray imageHash() {
    QFile file(QCoreApplication::applicationFilePath());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
bool write(Evidence& evidence, const Evidence::Leaf leaf, QJsonObject object, Evidence::Result& io) {
    object.insert(QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    io = evidence.writeNew(leaf, QByteArrayView(bytes));
    return io.ok;
}
QJsonObject ioReport(const Evidence::Result& io) {
    return {{"ok", io.ok},
            {"phase", static_cast<int>(io.phase)},
            {"error", qint64{io.error}},
            {"close_error", qint64{io.close_error}},
            {"bytes_written", qint64{io.bytes_written}}};
}
struct ThreadContext final {
    bool no_impersonation{false};
    DWORD error{Inventory::not_called}, close_error{Inventory::not_called};
};
ThreadContext threadContext() {
    ThreadContext result;
    HANDLE token = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
        result.error = ERROR_SUCCESS;
    else {
        result.error = GetLastError();
        result.no_impersonation = result.error == ERROR_NO_TOKEN && !token;
    }
    if (token)
        result.close_error = CloseHandle(token) ? ERROR_SUCCESS : GetLastError();
    return result; // A present/unknown thread token always refuses the run.
}
QJsonObject tokenReport(const Identity::Snapshot& token, const ThreadContext& thread) {
    return {{"complete", token.complete},
            {"error", qint64{token.error}},
            {"user_sid_hex", QString::fromLatin1(sidBytes(token.user_sid).toHex())},
            {"session_id", qint64{token.session_id}},
            {"elevated", token.elevated},
            {"elevation_type", static_cast<int>(token.elevation_type)},
            {"integrity_rid", qint64{token.integrity_rid}},
            {"ui_access", token.ui_access},
            {"app_container", token.app_container},
            {"capability_count", qint64{token.capability_count}},
            {"administrators_group_present", token.administrators_group_present},
            {"administrators_group_attributes", qint64{token.administrators_group_attributes}},
            {"thread_no_impersonation", thread.no_impersonation},
            {"thread_token_error", qint64{thread.error}},
            {"thread_token_close_error", qint64{thread.close_error}}};
}
QJsonObject profileReport(const WindowsNativeMediaProfile::Report& profile, const WindowsMediaProfileOwner& owner,
                          const std::optional<WindowsNativeMediaProfile::StorageLocation>& location) {
    QJsonObject result{{"profile_name", profile.name},
                       {"creation_attempted", profile.create_attempted},
                       {"created_by_this_call", profile.created},
                       {"create_hresult", raw(profile.create_hresult)},
                       {"sid_freed", profile.sid_released},
                       {"sid_hex", QString::fromLatin1(profile.sid.toHex())},
                       {"owner_state", static_cast<int>(owner.state())},
                       {"locator_captured", location.has_value()}};
    if (location)
        result.insert(QStringLiteral("captured_locators"),
                      QJsonObject{{"package_root", location->package_root},
                                  {"ac_path", location->ac_path},
                                  {"mappings_key", location->mappings_key},
                                  {"storage_key", location->storage_key},
                                  {"creator_user_sid_hex", QString::fromLatin1(location->creator_user_sid.toHex())}});
    return result;
}
QJsonObject closeReport(const Run::Result& run, const QByteArray& snapshot_hash,
                        const std::optional<Codec::Error> codec_error, const Evidence::Result& snapshot_io) {
    return {{"schema", 1},
            {"kind", "appcontainer-storage-inventory-close"},
            {"snapshot_leaf", "02-inventory.json"},
            {"snapshot_sha256", QString::fromLatin1(snapshot_hash)},
            {"snapshot_codec_error",
             codec_error ? QJsonValue{static_cast<int>(*codec_error)} : QJsonValue{QJsonValue::Null}},
            {"snapshot_io", ioReport(snapshot_io)},
            {"inspection_called", run.inspection_called},
            {"record_called", run.record_called},
            {"record_state", static_cast<int>(run.record_state)},
            {"post_validation_called", run.post_validation_called},
            {"post_validation_state", static_cast<int>(run.post_validation.state)},
            {"post_validation_error", qint64{run.post_validation.error}},
            {"close_called", run.close_called},
            {"close_state", static_cast<int>(run.closure.state)},
            {"close_error", qint64{run.closure.error}},
            {"observed_and_revalidated", run.observed_and_revalidated},
            {"read_resources_closed", run.read_resources_closed},
            {"security_policy_approved", false},
            {"native_identity_or_atomic_snapshot_proven", false}};
}
int host(const Options& options) {
    const auto thread = threadContext();
    const auto driver = Identity::captureCurrentToken();
    require(thread.no_impersonation && Identity::normalDriver(driver),
            "normal interactive Medium primary token without impersonation required");
    Evidence evidence(workspace(), options.evidence);
    Evidence::Result io = evidence.create();
    require(io.ok, "fresh private evidence directory refused");
    io = evidence.createJournal();
    require(io.ok, "fresh private journal directory refused");
    const auto operation = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto name = QString::fromStdString(downloadMediaProfileName(operation));
    auto backend = WindowsNativeMediaProfile::system(); // Binds only.
    WindowsDownloadMediaProfileStore journal(evidence.journalDirectory());
    WindowsMediaProfileOwner owner(journal, *backend, {"storage-inventory-probe", [operation] { return operation; }});
    Run::Result run;
    bool prepared = false, location_ready = false, read_entered = false, snapshot_saved = false;
    QByteArray snapshot_hash;
    std::optional<Codec::Error> snapshot_codec_error;
    Evidence::Result snapshot_io;
    QString failure;
    std::optional<WindowsNativeMediaProfile::StorageLocation> location;
    try {
        const auto hash = imageHash();
        require(hash.size() == 64, "image hash unavailable");
        require(write(evidence, Evidence::Leaf::Before,
                      {{"schema", 1},
                       {"mode", "own-profile-storage-inventory"},
                       {"operation_uuid", QString::fromStdString(operation)},
                       {"planned_profile_name", name},
                       {"image", QCoreApplication::applicationFilePath()},
                       {"image_sha256", QString::fromLatin1(hash)},
                       {"windows_kernel", QSysInfo::kernelVersion()},
                       {"driver_pid", qint64{GetCurrentProcessId()}},
                       {"driver_token", tokenReport(driver, thread)},
                       {"workers_launched", 0},
                       {"network_or_wfp_requested", false},
                       {"uac_requested", false},
                       {"storage_policy_changes_requested", false}},
                      io),
                "before-profile receipt failed");
        prepared = owner.prepare();
        location = backend->capturedStorageLocation();
        location_ready = prepared && location && location->name == name && location->sid == backend->report().sid &&
                         location->creator_user_sid == sidBytes(driver.user_sid);
        auto profile = profileReport(backend->report(), owner, location);
        profile.insert(QStringLiteral("owner_prepared"), prepared);
        profile.insert(QStringLiteral("location_matches_this_creation_and_user"), location_ready);
        require(write(evidence, Evidence::Leaf::Profile, profile, io), "creation receipt failed");
        require(location_ready, "durable live S_OK owner/locator unavailable; preserve profile/history");
        AppContainerStorageLiveSource source(
            owner, *backend, [](const auto& captured) { return appContainerStorageNativeSession(captured); });
        read_entered = true;
        run = Run::execute(source, [&](const Inventory::Report& snapshot) {
            const auto encoded = Codec::encode(snapshot, {}, {});
            snapshot_codec_error = encoded.error;
            if (encoded.error != Codec::Error::None)
                return false;
            snapshot_hash = QCryptographicHash::hash(encoded.bytes, QCryptographicHash::Sha256).toHex();
            snapshot_io = evidence.writeNew(Evidence::Leaf::Inventory, QByteArrayView(encoded.bytes));
            snapshot_saved = snapshot_io.ok;
            return snapshot_saved;
        });
    } catch (const std::exception& error) {
        failure = QString::fromLatin1(error.what());
    } catch (...) {
        failure = QStringLiteral("unexpected exception; preserve uncertain state");
    }
    // The Run closes only its read resources. Removal is requested solely from
    // the same live durable owner; a report/path/SD/Closed0 never creates it.
    Evidence::Result close_io;
    const bool close_receipt = write(evidence, Evidence::Leaf::Close,
                                     closeReport(run, snapshot_hash, snapshot_codec_error, snapshot_io), close_io);
    const bool cleanup_requested = prepared && location_ready && (!read_entered || run.read_resources_closed);
    const bool cleanup = cleanup_requested && owner.close() && owner.state() == WindowsMediaProfileOwnerState::Removed;
    const auto native = backend->report();
    const auto saved = journal.load();
    const bool removed_saved = matchingRemovedRecord(saved, operation, native.sid);
    Evidence::Result cleanup_io;
    const bool cleanup_receipt = write(evidence, Evidence::Leaf::Cleanup,
                                       {{"schema", 1},
                                        {"profile_name", name},
                                        {"cleanup_requested", cleanup_requested},
                                        {"read_entered", read_entered},
                                        {"read_resources_closed", run.read_resources_closed},
                                        {"read_close_receipt_io", ioReport(close_io)},
                                        {"remove_attempted", native.remove_attempted},
                                        {"delete_hresult", raw(native.remove_hresult)},
                                        {"absence", static_cast<int>(native.absence)},
                                        {"owner_removed", cleanup},
                                        {"owner_removed_saved", cleanup && removed_saved},
                                        {"owner_state", static_cast<int>(owner.state())}},
                                       cleanup_io);
    const bool passed = failure.isEmpty() && read_entered && snapshot_saved &&
                        run.record_state == Run::RecordState::Saved && run.observed_and_revalidated &&
                        run.read_resources_closed && close_receipt && cleanup && removed_saved && cleanup_receipt;
    QJsonObject result{{"schema", 1},
                       {"mode", "own-profile-storage-inventory"},
                       {"inventory_observed_and_revalidated", run.observed_and_revalidated},
                       {"read_resources_closed", run.read_resources_closed},
                       {"snapshot_saved", snapshot_saved},
                       {"close_receipt_saved", close_receipt},
                       {"profile_removed_saved", cleanup && removed_saved},
                       {"cleanup_receipt_saved", cleanup_receipt},
                       {"security_policy_approved", false},
                       {"production_approved", false},
                       {"profile_name", name},
                       {"failure", failure},
                       {"before_or_profile_receipt_io", ioReport(io)},
                       {"read_close_receipt_io", ioReport(close_io)},
                       {"cleanup_receipt_io", ioReport(cleanup_io)},
                       {"checks_passed_before_result_receipt", passed}};
    Evidence::Result result_io;
    const bool result_receipt = write(evidence, Evidence::Leaf::Result, result, result_io);
    result.insert(QStringLiteral("result_receipt_saved"), result_receipt);
    result.insert(QStringLiteral("result_receipt_io"), ioReport(result_io));
    result.insert(QStringLiteral("passed"), passed && result_receipt);
    const auto output = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    const bool printed = std::fwrite(output.constData(), 1, static_cast<std::size_t>(output.size()), stdout) ==
                             static_cast<std::size_t>(output.size()) &&
                         std::fflush(stdout) == 0;
    return passed && result_receipt && printed ? 0 : 1;
}
int selfTest() {
    const auto root = workspace();
    const auto path = root + QStringLiteral("/out/product/parser-only-not-created");
    unsigned passed = 0, failed = 0;
    const auto check = [&](const QStringList& args, const Options::Mode expected) {
        if (appContainerStorageProbeOptions(args, root).mode == expected)
            ++passed;
        else
            ++failed;
    };
    check({QStringLiteral("--self-test")}, Options::Mode::SelfTest);
    check({QStringLiteral("--help")}, Options::Mode::Help);
    check({QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), path},
          Options::Mode::Inventory);
    for (const auto& args :
         {QStringList{}, QStringList{QStringLiteral("--allow-profile-changes")},
          QStringList{QStringLiteral("--evidence-dir"), path}, QStringList{QStringLiteral("--allow-wfp-subscription")},
          QStringList{QStringLiteral("--readonly")}, QStringList{QStringLiteral("--self-test"), path},
          QStringList{QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"),
                      root + QStringLiteral("/out/PRODUCT/run")},
          QStringList{QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), path,
                      QStringLiteral("--external-classify-observer")}})
        check(args, Options::Mode::Refused);
    // Pure receipt-correlation regression. Readback is never the owner's Saved
    // acknowledgement; this only checks which operation the readback names.
    const std::string operation = "1701353d-3384-4e32-9341-72cedb405d84";
    const auto sid =
        QByteArray::fromHex("010800000000000f0200000001000000020000000300000004000000050000000600000007000000");
    DownloadMediaProfileSid record_sid{};
    std::memcpy(record_sid.data(), sid.constData(), record_sid.size());
    const DownloadMediaProfileLoadResult exact{DownloadCheckpointLoadStatus::Loaded,
                                               DownloadMediaProfileRecord{"storage-inventory-probe", operation,
                                                                          DownloadMediaProfileStage::Removed,
                                                                          record_sid}};
    const auto check_record = [&](const DownloadMediaProfileLoadResult& record, const QByteArray& expected_sid,
                                  const bool expected) {
        if (matchingRemovedRecord(record, operation, expected_sid) == expected)
            ++passed;
        else
            ++failed;
    };
    check_record(exact, sid, true);
    auto changed = exact;
    changed.record->job_id = "another-job";
    check_record(changed, sid, false);
    changed = exact;
    changed.record->operation_id = "1701353d-3384-4e32-9341-72cedb405d85";
    check_record(changed, sid, false);
    changed = exact;
    (*changed.record->sid)[39] ^= 1;
    check_record(changed, sid, false);
    changed = exact;
    changed.record->sid.reset();
    check_record(changed, sid, false);
    check_record(exact, {}, false);
    changed = exact;
    changed.record->stage = DownloadMediaProfileStage::InUse;
    check_record(changed, sid, false);
    changed = exact;
    changed.status = DownloadCheckpointLoadStatus::Missing;
    check_record(changed, sid, false);
    changed = exact;
    changed.record.reset();
    check_record(changed, sid, false);
    std::printf("{\"schema\":1,\"mode\":\"storage-probe-pure-self-test\",\"nativeProfileCalls\":0,"
                "\"nativeStorageCalls\":0,\"passed\":%u,\"failed\":%u}\n",
                passed, failed);
    return failed ? 1 : 0;
}
} // namespace
int wmain(const int argc, wchar_t** argv) {
    if (argc < 2 || argc > 4 || !argv)
        return 2;
    QStringList arguments;
    for (int index = 1; index < argc; ++index) {
        if (!argv[index] || wcsnlen_s(argv[index], Options::maximum_path_units + 1) > Options::maximum_path_units)
            return 2;
        arguments.push_back(QString::fromWCharArray(argv[index]));
    }
    const auto options = appContainerStorageProbeOptions(arguments, workspace());
    if (options.mode == Options::Mode::SelfTest)
        return selfTest();
    if (options.mode == Options::Mode::Help) {
        std::puts("Storage inventory: --self-test | --help | --allow-profile-changes --evidence-dir <fresh compiled "
                  "workspace/out/product path>. Manual profile mode requires separate authorization.");
        return 0;
    }
    if (options.mode != Options::Mode::Inventory)
        return 2;
    int application_argc = 1;
    char application_name[] = "RCMusicStorageInventoryProbe";
    char* application_argv[]{application_name, nullptr};
    QCoreApplication application(application_argc, application_argv);
    try {
        return host(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Storage inventory refused: %s\n", error.what());
    } catch (...) {
        std::fputs("Storage inventory refused before a confirmed run.\n", stderr);
    }
    return 1;
}

#include "AppContainerObserverActor.hpp"
#include "AppContainerObserverFiles.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QDir>
#include <QScopeGuard>

#include <aclapi.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace music_player::sandbox_probe {
namespace {
using Actor = AppContainerObserverActor;
using Observer = AppContainerNetworkObserver;
using Process = AppContainerObserverProcess;
using Identity = AppContainerObserverIdentity;
using Files = AppContainerObserverFiles;
using Protocol = AppContainerObserverProtocol;
namespace artifacts = music_player::m0::download_artifacts;
using artifacts::Handle;
constexpr std::int64_t window_milliseconds = 15'000;

Identity::Bytes bytes(const QByteArray& value) {
    return {reinterpret_cast<const std::uint8_t*>(value.constData()),
            reinterpret_cast<const std::uint8_t*>(value.constData()) + value.size()};
}
bool samePath(const QString& left, const QString& right) {
    return !left.isEmpty() && !right.isEmpty() && left.compare(right, Qt::CaseInsensitive) == 0;
}
bool completeProcess(const Process::Snapshot& value) {
    return value.complete && value.error == ERROR_SUCCESS && value.pid != 0 && value.creation_filetime != 0 &&
           !value.image_path.empty();
}
bool singleLink(const HANDLE file) {
    BY_HANDLE_FILE_INFORMATION information{};
    return GetFileInformationByHandle(file, &information) && information.nNumberOfLinks == 1;
}
QByteArray sidBytes(PSID value) {
    return value && IsValidSid(value)
               ? QByteArray{static_cast<const char*>(value), static_cast<qsizetype>(GetLengthSid(value))}
               : QByteArray{};
}
bool exactRuntimeSecurity(const HANDLE handle, const QByteArray& driver, const QByteArray& package) {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                        &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    const auto release = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (sidBytes(owner) != driver || !dacl || !IsValidAcl(dacl) || dacl->AceCount != 3 ||
        !GetSecurityDescriptorControl(descriptor, &control, &revision) || !(control & SE_DACL_PROTECTED))
        return false;
    bool found_driver = false, found_system = false, found_package = false;
    constexpr auto offset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(dacl, index, &raw) || !raw)
            return false;
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0 ||
            ace->Header.AceSize < offset + 8)
            return false;
        const auto* sid = reinterpret_cast<const unsigned char*>(&ace->SidStart);
        if (sid[0] != SID_REVISION || sid[1] > SID_MAX_SUB_AUTHORITIES ||
            ace->Header.AceSize != offset + 8 + 4 * std::size_t{sid[1]})
            return false;
        const QByteArray trustee(reinterpret_cast<const char*>(sid),
                                 static_cast<qsizetype>(ace->Header.AceSize - offset));
        if (!IsValidSid(const_cast<char*>(trustee.constData())))
            return false;
        if (trustee == driver) {
            if (std::exchange(found_driver, true) || ace->Mask != FILE_ALL_ACCESS)
                return false;
        } else if (trustee == package) {
            if (std::exchange(found_package, true) || ace->Mask != (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE))
                return false;
        } else if (IsWellKnownSid(const_cast<char*>(trustee.constData()), WinLocalSystemSid)) {
            if (std::exchange(found_system, true) || ace->Mask != FILE_ALL_ACCESS)
                return false;
        } else
            return false;
    }
    return found_driver && found_system && found_package;
}
struct Resources final {
    std::unique_ptr<Files> files{};
    Files::ReadResult request_read{};
    std::vector<Handle> ancestors{};
    artifacts::Artifact probe{}, current_image{}, driver_image{}, helper_source{}, helper{};
    Handle runtime{}, helper_metadata{};
    FILE_ID_INFO runtime_identity{}, helper_identity{};
    QString runtime_path{}, helper_final{};
    quint64 helper_bytes{};
    std::unique_ptr<Observer> observer{};
};
bool pinProductScope(Resources& held, const Actor::Config& config) {
    QString workspace, directory;
    if (!artifacts::validPath(config.workspace_root, workspace) || !artifacts::validPath(config.directory, directory))
        return false;
    const auto trusted_path = workspace + QStringLiteral("/out/product");
    if (!directory.startsWith(trusted_path + '/', Qt::CaseInsensitive))
        return false;
    const auto candidate_path = directory.left(trusted_path.size());
    if (!artifacts::pinAncestors(trusted_path, held.ancestors) ||
        !artifacts::pinAncestors(candidate_path, held.ancestors))
        return false;
    constexpr auto flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
    constexpr auto rights = FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY;
    Handle trusted(CreateFileW(artifacts::nativePath(trusted_path).c_str(), rights, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, flags, nullptr));
    Handle candidate(CreateFileW(artifacts::nativePath(candidate_path).c_str(), rights, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, flags, nullptr));
    FILE_ID_INFO trusted_identity{}, candidate_identity{};
    if (!trusted.valid() || !candidate.valid() || !artifacts::information(trusted.get(), true, trusted_identity) ||
        !artifacts::information(candidate.get(), true, candidate_identity) ||
        !artifacts::sameIdentity(trusted_identity, candidate_identity))
        return false;
    // Casing is a lexical admission check only. Retain both exact native
    // objects so the subsequent request-directory open cannot swap ancestry.
    held.ancestors.push_back(std::move(trusted));
    held.ancestors.push_back(std::move(candidate));
    return true;
}
bool trustedArtifact(artifacts::Artifact& artifact, const QString& path, const QByteArray& expected,
                     std::vector<Handle>& ancestors) {
    QString normalized;
    if (!artifacts::validPath(path, normalized))
        return false;
    artifact.path = std::move(normalized);
    artifact.sha256 = expected.toHex();
    return artifacts::pinAndHash(artifact, ancestors) == artifacts::Error::None && singleLink(artifact.handle.get());
}
bool validImages(Resources& held, const Actor::Config& config, const Protocol::Request& request,
                 const Process::Snapshot& current, const Process::Snapshot& driver) {
    return trustedArtifact(held.probe, config.trusted_probe_path, request.probe_sha256, held.ancestors) &&
           trustedArtifact(held.current_image, QString::fromStdWString(current.image_path), request.probe_sha256,
                           held.ancestors) &&
           trustedArtifact(held.driver_image, QString::fromStdWString(driver.image_path), request.probe_sha256,
                           held.ancestors) &&
           artifacts::sameIdentity(held.probe.identity, held.current_image.identity) &&
           artifacts::sameIdentity(held.probe.identity, held.driver_image.identity);
}
bool runtimeUnchanged(const Resources& held, const Protocol::Request& request) {
    FILE_ID_INFO identity{};
    return held.runtime.valid() && artifacts::information(held.runtime.get(), true, identity) &&
           artifacts::sameIdentity(identity, held.runtime_identity) &&
           samePath(artifacts::finalPath(held.runtime.get()), held.runtime_path) &&
           exactRuntimeSecurity(held.runtime.get(), request.driver_user_sid, request.package_sid);
}
bool helperUnchanged(const Resources& held, const Protocol::Request& request) {
    FILE_ID_INFO identity{};
    quint64 size = 0;
    return runtimeUnchanged(held, request) && held.helper_metadata.valid() &&
           artifacts::information(held.helper_metadata.get(), false, identity, &size) &&
           artifacts::sameIdentity(identity, held.helper_identity) && size == held.helper_bytes &&
           singleLink(held.helper_metadata.get()) &&
           samePath(artifacts::finalPath(held.helper_metadata.get()), held.helper_final) &&
           exactRuntimeSecurity(held.helper_metadata.get(), request.driver_user_sid, request.package_sid);
}
bool validHelper(Resources& held, const Actor::Config& config, const Protocol::Request& request) {
    if (!trustedArtifact(held.helper_source, config.trusted_helper_source, request.helper_sha256, held.ancestors))
        return false;
    QString helper;
    if (!artifacts::validPath(request.helper_path, helper) || !artifacts::pinAncestors(helper, held.ancestors))
        return false;
    const auto separator = helper.lastIndexOf(QLatin1Char{'/'});
    if (separator <= 2)
        return false;
    const auto parent = helper.left(separator);
    held.runtime = Handle(CreateFileW(
        artifacts::nativePath(parent).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | READ_CONTROL,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!held.runtime.valid() || !artifacts::information(held.runtime.get(), true, held.runtime_identity) ||
        !exactRuntimeSecurity(held.runtime.get(), request.driver_user_sid, request.package_sid))
        return false;
    held.runtime_path = artifacts::finalPath(held.runtime.get());
    // Inspect the untrusted leaf's metadata and exact ACL without FILE_READ_DATA.
    // Both pins remain held before the bounded hashing helper opens any bytes.
    held.helper_metadata =
        Handle(CreateFileW(artifacts::nativePath(helper).c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!held.helper_metadata.valid() ||
        !artifacts::information(held.helper_metadata.get(), false, held.helper_identity, &held.helper_bytes) ||
        !singleLink(held.helper_metadata.get()) ||
        !exactRuntimeSecurity(held.helper_metadata.get(), request.driver_user_sid, request.package_sid))
        return false;
    held.helper_final = artifacts::finalPath(held.helper_metadata.get());
    if (!samePath(held.helper_final, held.runtime_path + '\\' + helper.mid(separator + 1)) ||
        !trustedArtifact(held.helper, helper, request.helper_sha256, held.ancestors))
        return false;
    return artifacts::sameIdentity(held.helper_identity, held.helper.identity) &&
           held.helper_bytes == held.helper.bytes && samePath(held.helper_final, held.helper.final_path) &&
           helperUnchanged(held, request);
}
bool validConfiguration(const Actor::Config& config, const Actor::Calls& calls) {
    if (!calls.current_process || !calls.open_driver || !calls.poll_driver || !calls.monotonic_ms || !calls.pump)
        return false;
    QString workspace, directory, probe, source;
    return artifacts::validPath(config.workspace_root, workspace) &&
           artifacts::validPath(config.directory, directory) &&
           artifacts::validPath(config.trusted_probe_path, probe) &&
           artifacts::validPath(config.trusted_helper_source, source) &&
           directory.startsWith(workspace + QStringLiteral("/out/product/"), Qt::CaseInsensitive);
}
Actor::Reason driverHealth(const Actor::Calls& calls) {
    try {
        const auto status = calls.poll_driver();
        if (status.error == ERROR_SUCCESS && status.state == Process::State::Running && !status.exit_code)
            return Actor::Reason::None;
        if (status.error == ERROR_SUCCESS && status.state == Process::State::Stopped && status.exit_code)
            return Actor::Reason::DriverStopped;
    } catch (...) {
    }
    return Actor::Reason::DriverUnknown;
}
bool readClock(const Actor::Calls& calls, std::int64_t& previous, std::int64_t& value) {
    try {
        value = calls.monotonic_ms();
        if (value < 0 || value < previous)
            return false;
        previous = value;
        return true;
    } catch (...) {
        return false;
    }
}
bool cleanStop(const Observer::Report& report) {
    return report.state == Observer::State::Stopped && report.reason == Observer::Reason::None &&
           !report.context_retained && report.unsubscribe_error == ERROR_SUCCESS && report.close_error == ERROR_SUCCESS;
}
} // namespace

struct AppContainerObserverActor::Impl final {
    Config config;
    Calls calls;
    std::atomic_bool attempted{false};
    std::unique_ptr<Resources> resources{};
    Impl(Config configuration, Calls functions) : config(std::move(configuration)), calls(std::move(functions)) {}
};
AppContainerObserverActor::Calls AppContainerObserverActor::systemCalls() {
    struct State final {
        std::unique_ptr<Process> driver{};
    };
    const auto state = std::make_shared<State>();
    Calls result;
    result.current_process = [] {
        HANDLE token = nullptr;
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
            const Handle held(token);
            return Process::Snapshot{.error = ERROR_ACCESS_DENIED};
        }
        const auto error = GetLastError();
        if (error != ERROR_NO_TOKEN)
            return Process::Snapshot{.error = error};
        Process current(GetCurrentProcessId());
        return current.open();
    };
    result.open_driver = [state](const std::uint32_t pid) {
        if (!state->driver)
            state->driver = std::make_unique<Process>(pid);
        if (state->driver->snapshot().pid != pid)
            return Process::Snapshot{.error = ERROR_INVALID_PARAMETER, .pid = pid};
        return state->driver->open();
    };
    result.poll_driver = [state] { return state->driver ? state->driver->poll() : Process::Poll{}; };
    result.network_calls = Observer::systemCalls();
    result.monotonic_ms = [] {
        const auto now = GetTickCount64();
        return now <= static_cast<ULONGLONG>(std::numeric_limits<std::int64_t>::max()) ? static_cast<std::int64_t>(now)
                                                                                       : std::int64_t{-1};
    };
    result.pump = [] { Sleep(1); };
    return result;
}
AppContainerObserverActor::AppContainerObserverActor(Config config, Calls calls)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(calls))) {}
AppContainerObserverActor::~AppContainerObserverActor() {
    if (impl_->resources && impl_->resources->observer) {
        const auto report = impl_->resources->observer->snapshot();
        if (report.context_retained || report.state == Observer::State::Observing ||
            report.state == Observer::State::StopUnconfirmed)
            static_cast<void>(impl_.release());
    }
}
AppContainerObserverActor::RunResult AppContainerObserverActor::run() {
    if (impl_->attempted.exchange(true))
        return {.reason = Reason::AlreadyRun};
    auto& value = *impl_;
    if (!validConfiguration(value.config, value.calls))
        return {};
    RunResult result{.reason = Reason::None};
    value.resources = std::make_unique<Resources>();
    auto& held = *value.resources;
    bool started = false;
    std::int64_t last_clock = -1, now = 0;
    try {
        const auto admit = [&]() -> Reason {
            if (!pinProductScope(held, value.config))
                return Reason::InvalidConfiguration;
            held.files = std::make_unique<Files>(Files::Config{value.config.directory, {}});
            if (held.files->open().status != Files::OpenStatus::Opened)
                return Reason::Files;
            held.request_read = held.files->read(Files::Slot::Request);
            if (held.request_read.status != Files::ReadStatus::Ready || !held.request_read.lease)
                return Reason::Request;
            const auto request = Protocol::decodeRequest(held.request_read.bytes);
            if (!request)
                return Reason::Request;
            const auto current = value.calls.current_process();
            if (!completeProcess(current) || !Identity::elevatedObserver(current.token))
                return Reason::ObserverIdentity;
            result.result = Protocol::Result{.nonce = request->nonce,
                                             .request_sha256 = Protocol::requestSha256(held.request_read.bytes),
                                             .observer_pid = current.pid,
                                             .observer_creation_filetime = current.creation_filetime};
            if (request->driver_user_sid != held.files->driverSid())
                return Reason::Request;
            if (held.files->read(Files::Slot::Ready).status != Files::ReadStatus::Missing ||
                held.files->read(Files::Slot::Result).status != Files::ReadStatus::Missing)
                return Reason::Files;
            const auto driver = value.calls.open_driver(request->driver_pid);
            if (!completeProcess(driver) || driver.pid != request->driver_pid || driver.pid == current.pid ||
                driver.creation_filetime != request->driver_creation_filetime ||
                !Identity::normalDriver(driver.token) ||
                !Identity::matchesCaller(driver.token, bytes(request->driver_user_sid), current.token.session_id))
                return Reason::DriverIdentity;
            if (!validImages(held, value.config, *request, current, driver))
                return Reason::Image;
            if (!validHelper(held, value.config, *request))
                return Reason::Helper;
            if (const auto health = driverHealth(value.calls); health != Reason::None)
                return health;
            if (!readClock(value.calls, last_clock, now))
                return Reason::Clock;
            const auto begin = now;
            Observer::Config network{.executable = held.helper.path.toStdWString(),
                                     .package_sid = bytes(request->package_sid),
                                     .user_sid = bytes(request->driver_user_sid),
                                     .remote_port = request->loopback_tcp_port,
                                     .evidence_kind = Observer::EvidenceKind::ClassifyIsolation,
                                     .window_at_subscribe = true};
            held.observer = std::make_unique<Observer>(std::move(network), value.calls.network_calls);
            const auto report = held.observer->start();
            started = true;
            result.result->status = Protocol::ResultStatus::ObserverResult;
            if (report.state != Observer::State::Observing || !report.available ||
                report.subscribe_error != ERROR_SUCCESS || report.window_begin_filetime == 0 ||
                report.window_end_filetime <= report.window_begin_filetime ||
                report.window_end_filetime - report.window_begin_filetime != Observer::maximum_window_ticks)
                return Reason::NetworkStart;
            if (!readClock(value.calls, last_clock, now))
                return Reason::Clock;
            if (const auto health = driverHealth(value.calls); health != Reason::None)
                return health;
            const Protocol::Ready ready{.nonce = request->nonce,
                                        .request_sha256 = result.result->request_sha256,
                                        .observer_pid = current.pid,
                                        .observer_creation_filetime = current.creation_filetime,
                                        .window_begin_filetime = report.window_begin_filetime,
                                        .window_end_filetime = report.window_end_filetime};
            const auto encoded = Protocol::encodeReady(ready);
            if (encoded.isEmpty() ||
                held.files->writeNew(Files::Slot::Ready, encoded).status != Files::WriteStatus::Written)
                return Reason::ReadyWrite;
            result.ready_written = true;
            result.result->ready = ready;
            for (;;) {
                if (!readClock(value.calls, last_clock, now))
                    return Reason::Clock;
                if (const auto health = driverHealth(value.calls); health != Reason::None)
                    return health;
                if (held.observer->snapshot().first_candidate || now - begin >= window_milliseconds)
                    return Reason::None;
                value.calls.pump();
            }
        };
        result.reason = admit();
    } catch (...) {
        result.reason = Reason::Exception;
    }
    if (started) {
        const auto report = held.observer->stop();
        result.resources_retained = report.context_retained || report.state == Observer::State::StopUnconfirmed ||
                                    report.state == Observer::State::Observing;
        result.result->report = report;
        if (result.reason == Reason::None && (result.resources_retained || !cleanStop(report)))
            result.reason = Reason::NetworkStop;
        if (result.reason == Reason::None && !readClock(value.calls, last_clock, now))
            result.reason = Reason::Clock;
        if (result.reason == Reason::None)
            result.reason = driverHealth(value.calls);
    }
    if (result.result) {
        const auto encoded = Protocol::encodeResult(*result.result);
        if (!encoded.isEmpty())
            result.result_written =
                held.files->writeNew(Files::Slot::Result, encoded).status == Files::WriteStatus::Written;
        if (!result.result_written && result.reason == Reason::None)
            result.reason = Reason::ResultWrite;
    }
    result.exit_code =
        result.reason == Reason::None && result.ready_written && result.result_written && !result.resources_retained
            ? 0
            : 1;
    if (!result.resources_retained)
        value.resources.reset();
    return result;
}
} // namespace music_player::sandbox_probe

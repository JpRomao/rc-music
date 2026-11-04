#include "../windows/sandbox/AppContainerObserverActor.hpp"
#include "../windows/sandbox/AppContainerObserverFiles.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;
using namespace music_player::sandbox_probe;
namespace {
using Actor = AppContainerObserverActor;
using Process = AppContainerObserverProcess;
using Observer = AppContainerNetworkObserver;
using Identity = AppContainerObserverIdentity;
using Protocol = AppContainerObserverProtocol;
using Files = AppContainerObserverFiles;
using music_player::m0::download_artifacts::Handle;
using music_player::m0::download_artifacts::nativePath;
constexpr auto package_hex = "010800000000000f0200000001000000020000000300000004000000050000000600000007000000";
constexpr std::uint64_t filetime = 134'067'564'000'000'000;
constexpr std::uint32_t driver_pid = 101, observer_pid = 202;
Identity::Bytes bytes(const QByteArray& value) {
    return {reinterpret_cast<const std::uint8_t*>(value.constData()),
            reinterpret_cast<const std::uint8_t*>(value.constData()) + value.size()};
}
QByteArray currentSid() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return {};
    const Handle token(raw);
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
    if (!size || size > 64 * 1024)
        return {};
    std::vector<unsigned char> buffer(size);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
        return {};
    const auto sid = reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid;
    return sid && IsValidSid(sid) ? QByteArray{static_cast<const char*>(sid), static_cast<qsizetype>(GetLengthSid(sid))}
                                  : QByteArray{};
}
QString sidText(const QByteArray& sid) {
    LPWSTR text = nullptr;
    if (sid.isEmpty() || !ConvertSidToStringSidW(const_cast<char*>(sid.constData()), &text))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text);
}
struct Security final {
    PSECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    explicit Security(const QString& text) {
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(text.toStdWString().c_str(), SDDL_REVISION_1,
                                                                 &descriptor, nullptr))
            attributes.lpSecurityDescriptor = descriptor;
    }
    ~Security() { LocalFree(descriptor); }
};
QString channelAcl(const QByteArray& user) {
    const auto text = sidText(user);
    return u"O:"_s + text + u"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;"_s + text + ')';
}
QString runtimeAcl(const QByteArray& user, const QByteArray& package) {
    const auto text = sidText(user);
    return u"O:"_s + text + u"D:P(A;;FA;;;SY)(A;;FA;;;"_s + text + u")(A;;0x1200a9;;;"_s + sidText(package) + ')';
}
bool createDirectory(const QString& path, const QString& acl) {
    Security security(acl);
    return security.descriptor && CreateDirectoryW(nativePath(path).c_str(), &security.attributes);
}
bool createFile(const QString& path, const QByteArray& content, const QString& acl = {}) {
    Security security(acl);
    const Handle file(CreateFileW(nativePath(path).c_str(), GENERIC_WRITE, 0,
                                  acl.isEmpty() ? nullptr : &security.attributes, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    DWORD written = 0;
    return file.valid() &&
           WriteFile(file.get(), content.constData(), static_cast<DWORD>(content.size()), &written, nullptr) &&
           written == static_cast<DWORD>(content.size()) && FlushFileBuffers(file.get());
}
Handle mutableSecurity(const QString& path, const bool directory) {
    return Handle(CreateFileW(nativePath(path).c_str(), READ_CONTROL | WRITE_DAC,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
}
DWORD replaceDacl(const HANDLE handle, const QString& acl) {
    Security security(acl);
    PACL native_acl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    if (!security.descriptor || !GetSecurityDescriptorDacl(security.descriptor, &present, &native_acl, &defaulted) ||
        !present || !native_acl)
        return ERROR_INVALID_SECURITY_DESCR;
    return SetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                           nullptr, nullptr, native_acl, nullptr);
}
struct DaclFacts final {
    bool valid{false}, protected_dacl{false};
    DWORD count{};
    std::vector<QByteArray> trustees{};
};
DaclFacts daclFacts(const HANDLE handle) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
                        &descriptor) != ERROR_SUCCESS)
        return {};
    const auto release = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!dacl || !IsValidAcl(dacl) || !GetSecurityDescriptorControl(descriptor, &control, &revision))
        return {};
    DaclFacts result{true, (control & SE_DACL_PROTECTED) != 0, dacl->AceCount, {}};
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(dacl, index, &raw) || !raw)
            return {};
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        constexpr auto offset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceSize < offset + 8)
            return {};
        const auto* sid = reinterpret_cast<const unsigned char*>(&ace->SidStart);
        if (sid[0] != SID_REVISION || sid[1] > SID_MAX_SUB_AUTHORITIES ||
            ace->Header.AceSize != offset + 8 + 4 * std::size_t{sid[1]})
            return {};
        result.trustees.emplace_back(reinterpret_cast<const char*>(sid),
                                     static_cast<qsizetype>(ace->Header.AceSize - offset));
    }
    return result;
}
QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
Identity::Snapshot token(const QByteArray& user, const bool elevated) {
    Identity::Snapshot result;
    result.complete = true;
    result.error = 0;
    result.user_sid = bytes(user);
    result.session_id = 7;
    result.elevated = elevated;
    result.elevation_type = elevated ? Identity::ElevationType::Full : Identity::ElevationType::Limited;
    result.integrity_rid = elevated ? Identity::high_integrity_rid : Identity::medium_integrity_rid;
    result.administrators_group_present = true;
    result.administrators_group_attributes = elevated ? SE_GROUP_ENABLED : SE_GROUP_USE_FOR_DENY_ONLY;
    return result;
}
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + u"/RC Music Actor Fixture-"_s +
                         QUuid::createUuid().toString(QUuid::WithoutBraces) + u"-XXXXXX"_s};
    QByteArray user{currentSid()}, package{QByteArray::fromHex(package_hex)};
    QByteArray probe_content{"trusted probe bytes"}, helper_content{"trusted helper bytes"};
    QString workspace{parent.filePath(u"workspace"_s)};
    QString product{workspace + u"/out/product"_s};
    QString channel{product + u"/observer-0123456789abcdef0123456789abcdef"_s};
    QString runtime{parent.filePath(u"runtime privado"_s)};
    QString probe{product + u"/probe.bin"_s}, source{product + u"/helper-source.bin"_s};
    QString helper{runtime + u"/helper.bin"_s};
    Protocol::Request request;
    bool initialize(const bool write_request = true) {
        if (!parent.isValid() || user.isEmpty() || !QDir{}.mkpath(product) ||
            !createDirectory(channel, channelAcl(user)) || !createDirectory(runtime, runtimeAcl(user, package)) ||
            !createFile(probe, probe_content) || !createFile(source, helper_content) ||
            !createFile(helper, helper_content, runtimeAcl(user, package)))
            return false;
        request = {.nonce = u"0123456789abcdef0123456789abcdef"_s,
                   .driver_user_sid = user,
                   .driver_pid = driver_pid,
                   .driver_creation_filetime = filetime - 100,
                   .probe_sha256 = QCryptographicHash::hash(probe_content, QCryptographicHash::Sha256),
                   .helper_path = helper,
                   .helper_sha256 = QCryptographicHash::hash(helper_content, QCryptographicHash::Sha256),
                   .package_sid = package,
                   .loopback_tcp_port = 54321};
        return !write_request || writeRequest(Protocol::encodeRequest(request));
    }
    Actor::Config config() const { return {workspace, channel, probe, source}; }
    bool writeRequest(const QByteArray& raw) const {
        Files files({channel, user});
        return files.open().status == Files::OpenStatus::Opened &&
               files.writeNew(Files::Slot::Request, raw).status == Files::WriteStatus::Written;
    }
    QString path(const Files::Slot slot) const {
        return channel + (slot == Files::Slot::Request ? u"/wfp-request.json"_s
                          : slot == Files::Slot::Ready ? u"/wfp-ready.json"_s
                                                       : u"/wfp-result.json"_s);
    }
    bool occupy(const Files::Slot slot) const {
        Files files({channel, user});
        return files.open().status == Files::OpenStatus::Opened &&
               files.writeNew(slot, QByteArray{"occupied, preserve"}).status == Files::WriteStatus::Written;
    }
    std::optional<Protocol::Result> result() const {
        return Protocol::decodeResult(readFile(path(Files::Slot::Result)));
    }
};
struct Fake final {
    Process::Snapshot current{}, driver{};
    unsigned current_calls{}, driver_calls{}, poll_calls{}, opens{}, subscribes{}, unsubscribes{}, closes{}, pumps{};
    std::int64_t now{100}, step{16'000};
    bool emit_event{false}, ready_seen{false};
    std::uint32_t open_error{}, unsubscribe_error{}, close_error{};
    Process::Poll driver_poll{Process::State::Running, 0, std::nullopt};
    Observer::Callback callback{};
    void* callback_context{};
    Observer::Bytes application{'h', 0, 'e', 0, 0, 0}, driver_sid{}, package_sid{};
    std::uint16_t port{};
    std::uint64_t window_begin{}, window_end{};
    std::function<void()> during_open{}, during_subscribe{}, during_close{}, during_pump{};
    std::function<Process::Poll()> on_poll{};
    std::function<std::int64_t()> on_clock{};
    std::function<void()> during_current{};
    explicit Fake(const Fixture& f) {
        driver = {true, 0, driver_pid, filetime - 100, f.probe.toStdWString(), token(f.user, false)};
        auto other_user = f.user;
        other_user[other_user.size() - 1] = static_cast<char>(other_user.back() ^ 1);
        current = {true, 0, observer_pid, filetime - 50, f.probe.toStdWString(), token(other_user, true)};
        driver_sid = bytes(f.user);
        package_sid = bytes(f.package);
        port = f.request.loopback_tcp_port;
    }
    void deliver() const {
        if (!callback)
            return;
        const Observer::EventView event{Observer::EventKind::ClassifyDrop,
                                        Observer::required_fields,
                                        window_begin + 1,
                                        4,
                                        Observer::loopback_ipv4,
                                        6,
                                        port,
                                        application,
                                        driver_sid,
                                        package_sid,
                                        Observer::not_called,
                                        123,
                                        1,
                                        48,
                                        Observer::classify_outbound_direction};
        callback(callback_context, event);
    }
};
Actor::Calls calls(const std::shared_ptr<Fake>& fake, const Fixture& fixture) {
    Actor::Calls result;
    result.current_process = [fake] {
        ++fake->current_calls;
        if (fake->during_current)
            fake->during_current();
        return fake->current;
    };
    result.open_driver = [fake](const std::uint32_t pid) {
        ++fake->driver_calls;
        auto value = fake->driver;
        if (pid != driver_pid)
            value.complete = false;
        return value;
    };
    result.poll_driver = [fake] {
        ++fake->poll_calls;
        return fake->on_poll ? fake->on_poll() : fake->driver_poll;
    };
    result.network_calls = {.app_id = [fake](std::wstring_view) { return Observer::AppIdResult{0, fake->application}; },
                            .open =
                                [fake] {
                                    ++fake->opens;
                                    if (fake->during_open)
                                        fake->during_open();
                                    return Observer::OpenResult{fake->open_error, fake->open_error ? 0U : 11U};
                                },
                            .get_collection = [](Observer::Handle) { return Observer::OptionResult{0, true, 1}; },
                            .get_keywords = [](Observer::Handle) { return Observer::OptionResult{0, true, 0}; },
                            .subscribe =
                                [fake](Observer::Handle, const Observer::SubscriptionFilter& filter,
                                       Observer::Callback callback, void* context) {
                                    ++fake->subscribes;
                                    fake->callback = callback;
                                    fake->callback_context = context;
                                    fake->window_begin = filter.begin_filetime;
                                    fake->window_end = filter.end_filetime;
                                    if (fake->during_subscribe)
                                        fake->during_subscribe();
                                    return Observer::SubscribeResult{0, 22};
                                },
                            .unsubscribe =
                                [fake](Observer::Handle, Observer::Handle) {
                                    ++fake->unsubscribes;
                                    if (!fake->unsubscribe_error) {
                                        fake->callback = nullptr;
                                        fake->callback_context = nullptr;
                                    }
                                    return fake->unsubscribe_error;
                                },
                            .close =
                                [fake](Observer::Handle) {
                                    ++fake->closes;
                                    if (fake->during_close)
                                        fake->during_close();
                                    return fake->close_error;
                                },
                            .get_filter =
                                [](Observer::Handle, std::uint64_t id) {
                                    return Observer::FilterResult{0,
                                                                  true,
                                                                  id,
                                                                  false,
                                                                  Observer::FilterAction::Block,
                                                                  Observer::ProviderIdentity::AppIsolation,
                                                                  Observer::SublayerIdentity::AppIsolation,
                                                                  Observer::LayerIdentity::AleAuthConnectV4};
                                },
                            .get_layer =
                                [](Observer::Handle, std::uint16_t id) {
                                    return Observer::LayerResult{0, true, id,
                                                                 Observer::LayerIdentity::AleAuthConnectV4};
                                },
                            .current_filetime = [] { return filetime; }};
    result.monotonic_ms = [fake] { return fake->on_clock ? fake->on_clock() : fake->now; };
    const auto ready_path = fixture.path(Files::Slot::Ready);
    result.pump = [fake, ready_path] {
        ++fake->pumps;
        fake->ready_seen = Protocol::decodeReady(readFile(ready_path)).has_value();
        if (fake->during_pump)
            fake->during_pump();
        if (fake->emit_event)
            fake->deliver();
        fake->now += fake->step;
    };
    return result;
}
bool writeBlocked(const QString& path) {
    const Handle writer(CreateFileW(nativePath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    return !writer.valid() && GetLastError() == ERROR_SHARING_VIOLATION;
}
} // namespace

class AppContainerObserverActorTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructorAndSystemFactoryAreInertAndFactoryBindsAllCalls() {
        const auto bound = Actor::systemCalls();
        QVERIFY(bound.current_process && bound.open_driver && bound.poll_driver && bound.monotonic_ms && bound.pump);
        QVERIFY(bound.network_calls.open && bound.network_calls.subscribe && bound.network_calls.current_filetime);
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        {
            Actor actor(f.config(), calls(fake, f));
        }
        QCOMPARE(fake->current_calls + fake->driver_calls + fake->opens + fake->poll_calls, 0U);
        QVERIFY(!QFile::exists(f.path(Files::Slot::Ready)) && !QFile::exists(f.path(Files::Slot::Result)));
    }
    void healthyTimeoutPublishesReadyThenStoppedWithoutInventingMatch() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QCOMPARE(result.exit_code, 0);
        QCOMPARE(result.reason, Actor::Reason::None);
        QVERIFY(result.ready_written && result.result_written && !result.resources_retained);
        QVERIFY(fake->ready_seen);
        QCOMPARE(fake->subscribes, 1U);
        QCOMPARE(fake->unsubscribes, 1U);
        QCOMPARE(fake->closes, 1U);
        const auto saved = f.result();
        QVERIFY(saved && saved->ready && saved->report);
        QCOMPARE(saved->report->state, Observer::State::Stopped);
        QCOMPARE(saved->report->evidence_kind, Observer::EvidenceKind::ClassifyIsolation);
        QVERIFY(!saved->report->matched && !saved->report->rule_verified && !saved->report->first_candidate);
        QCOMPARE(saved->ready->window_end_filetime - saved->ready->window_begin_filetime,
                 Observer::maximum_window_ticks);
        QVERIFY(Protocol::matchesRequest(f.request, Protocol::requestSha256(readFile(f.path(Files::Slot::Request))),
                                         *saved));
        QCOMPARE(saved->observer_pid, observer_pid);
    }
    void firstCandidateStopsTheWindowAndRetainsEveryTypedField() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        fake->emit_event = true;
        fake->step = 1;
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QCOMPARE(result.exit_code, 0);
        QCOMPARE(fake->pumps, 1U);
        const auto saved = f.result();
        QVERIFY(saved && saved->report && saved->report->first_candidate && saved->report->first_match);
        QVERIFY(saved->report->matched && saved->report->rule_verified);
        QCOMPARE(saved->report->first_candidate->filter_id, std::uint64_t{123});
        QCOMPARE(saved->report->filter_result.provider, Observer::ProviderIdentity::AppIsolation);
        QCOMPARE(saved->report->layer_result.layer_id, std::uint16_t{48});
        QVERIFY(fake->ready_seen);
    }
    void onlyTheFirstRunMayTouchPeersAndReentryCannotStartAnotherSubscription() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        Actor actor(f.config(), calls(fake, f));
        Actor::RunResult reentry;
        fake->during_current = [&] { reentry = actor.run(); };
        QCOMPARE(actor.run().exit_code, 0);
        QCOMPARE(reentry.reason, Actor::Reason::AlreadyRun);
        QCOMPARE(actor.run().reason, Actor::Reason::AlreadyRun);
        QCOMPARE(fake->current_calls, 1U);
        QCOMPARE(fake->driver_calls, 1U);
        QCOMPARE(fake->subscribes, 1U);
    }
    void malformedRequestCannotInventNonceReadyOrResult() {
        Fixture f;
        QVERIFY(f.initialize(false));
        QVERIFY(f.writeRequest(QByteArray{"{\"nonce\":\"invented\"}"}));
        auto fake = std::make_shared<Fake>(f);
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0);
        QCOMPARE(result.reason, Actor::Reason::Request);
        QVERIFY(!result.result && !result.ready_written && !result.result_written);
        QCOMPARE(fake->opens, 0U);
        QVERIFY(!QFile::exists(f.path(Files::Slot::Ready)) && !QFile::exists(f.path(Files::Slot::Result)));
    }
    void requestCannotSubstituteAnotherDriverForThePinnedDirectoryOwner() {
        Fixture f;
        QVERIFY(f.initialize(false));
        f.request.driver_user_sid[f.request.driver_user_sid.size() - 1] ^= 1;
        QVERIFY(f.writeRequest(Protocol::encodeRequest(f.request)));
        auto fake = std::make_shared<Fake>(f);
        fake->driver.token.user_sid = bytes(f.request.driver_user_sid);
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && !result.ready_written);
        QCOMPARE(result.reason, Actor::Reason::Request);
        QCOMPARE(fake->opens, 0U);
    }
    void requestRootMustRemainUnderTheFixedWorkspaceProductDirectory() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        auto config = f.config();
        config.workspace_root = f.parent.path(); // channel is under workspace/out/product, not this root's out/product.
        Actor actor(config, calls(fake, f));
        const auto result = actor.run();
        QCOMPARE(result.reason, Actor::Reason::InvalidConfiguration);
        QVERIFY(result.exit_code != 0 && !result.ready_written && !result.result_written);
        QCOMPARE(fake->opens, 0U);
    }
    void caseFoldedProductNameCannotSubstituteAnotherNativeDirectory() {
        Fixture f;
        QVERIFY(f.parent.isValid() && !f.user.isEmpty());
        const auto out = f.workspace + u"/out"_s;
        QVERIFY(QDir{}.mkpath(out));
        {
            const Handle directory(CreateFileW(nativePath(out).c_str(), GENERIC_READ | GENERIC_WRITE,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                               nullptr));
            if (!directory.valid()) {
                const auto error = GetLastError();
                QSKIP(qPrintable(u"Owned TEMP case-mode handle unavailable; Win32 error="_s + QString::number(error)));
            }
            FILE_CASE_SENSITIVE_INFO requested{FILE_CS_FLAG_CASE_SENSITIVE_DIR};
            if (!SetFileInformationByHandle(directory.get(), FileCaseSensitiveInfo, &requested, sizeof(requested))) {
                const auto error = GetLastError();
                QSKIP(qPrintable(u"Owned TEMP case-mode unavailable; Win32 error="_s + QString::number(error)));
            }
            FILE_CASE_SENSITIVE_INFO actual{};
            QVERIFY2(GetFileInformationByHandleEx(directory.get(), FileCaseSensitiveInfo, &actual, sizeof(actual)),
                     qPrintable(QString::number(GetLastError())));
            QVERIFY((actual.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) != 0);
        }
        const auto other_product = f.workspace + u"/out/PRODUCT"_s;
        QVERIFY(createDirectory(f.product, channelAcl(f.user)));
        QVERIFY(createDirectory(other_product, channelAcl(f.user)));
        {
            const auto flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
            const auto share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
            const Handle trusted(CreateFileW(nativePath(f.product).c_str(), FILE_READ_ATTRIBUTES, share, nullptr,
                                             OPEN_EXISTING, flags, nullptr));
            const Handle other(CreateFileW(nativePath(other_product).c_str(), FILE_READ_ATTRIBUTES, share, nullptr,
                                           OPEN_EXISTING, flags, nullptr));
            QVERIFY(trusted.valid() && other.valid());
            FILE_ID_INFO expected{}, substituted{};
            QVERIFY(GetFileInformationByHandleEx(trusted.get(), FileIdInfo, &expected, sizeof(expected)));
            QVERIFY(GetFileInformationByHandleEx(other.get(), FileIdInfo, &substituted, sizeof(substituted)));
            QVERIFY(!music_player::m0::download_artifacts::sameIdentity(expected, substituted));
        }
        f.channel = other_product + u"/observer-0123456789abcdef0123456789abcdef"_s;
        QVERIFY(f.channel.startsWith(f.product + '/', Qt::CaseInsensitive));
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && !result.ready_written);
        QCOMPARE(fake->opens, 0U);
        QCOMPARE(fake->subscribes, 0U);
        QVERIFY(!QFile::exists(f.path(Files::Slot::Ready)));
    }
    void missingCallsNeverReachNetwork() {
        Fixture f;
        QVERIFY(f.initialize());
        Actor actor(f.config(), {});
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && !result.ready_written && !result.result_written);
        QCOMPARE(result.reason, Actor::Reason::InvalidConfiguration);
    }
    void observerAndDriverAdmission_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 14; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void observerAndDriverAdmission() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        switch (kind) {
        case 0:
            fake->current.complete = false;
            break;
        case 1:
            fake->current.token.elevated = false;
            break;
        case 2:
            fake->driver.complete = false;
            break;
        case 3:
            fake->driver.pid = 999;
            break;
        case 4:
            ++fake->driver.creation_filetime;
            break;
        case 5:
            fake->driver.token.elevated = true;
            break;
        case 6:
            ++fake->driver.token.session_id;
            break;
        case 7:
            fake->driver.token.user_sid.back() ^= 1;
            break;
        case 8:
            fake->driver.pid = observer_pid;
            break;
        case 9:
            fake->driver.token.integrity_rid = 0x1000;
            break;
        case 10:
            fake->current.creation_filetime = 0;
            break;
        case 11:
            fake->driver.error = ERROR_ACCESS_DENIED;
            break;
        case 12:
            fake->current.pid = 0;
            break;
        case 13:
            fake->current.pid = driver_pid;
            break;
        }
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && !result.ready_written);
        QCOMPARE(result.reason, kind <= 1 || kind == 10 || kind == 12 ? Actor::Reason::ObserverIdentity
                                                                      : Actor::Reason::DriverIdentity);
        QCOMPARE(fake->opens, 0U);
        if (kind >= 2 && kind != 10 && kind != 12) {
            const auto saved = f.result();
            QVERIFY(saved && !saved->ready && !saved->report);
            QCOMPARE(saved->status, Protocol::ResultStatus::AdmissionRejected);
        }
    }
    void everyExecutableMustMatchItsTrustedIdentityAndHash_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void everyExecutableMustMatchItsTrustedIdentityAndHash() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize(false));
        auto fake = std::make_shared<Fake>(f);
        const auto clone = f.product + u"/identical-probe-copy.bin"_s;
        QVERIFY(createFile(clone, f.probe_content));
        if (kind == 0)
            fake->current.image_path = clone.toStdWString();
        if (kind == 1)
            fake->driver.image_path = clone.toStdWString();
        if (kind == 2)
            f.request.probe_sha256[0] ^= 1;
        if (kind == 3)
            f.request.helper_sha256[0] ^= 1;
        if (kind == 4) {
            const auto different_source = f.product + u"/other-source.bin"_s;
            QVERIFY(createFile(different_source, QByteArray{"different source"}));
            f.source = different_source;
        }
        QVERIFY(f.writeRequest(Protocol::encodeRequest(f.request)));
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && !result.ready_written && result.result_written);
        QCOMPARE(result.reason, kind < 3 ? Actor::Reason::Image : Actor::Reason::Helper);
        QCOMPARE(fake->opens, 0U);
    }
    void helperMetadataAndPrivateParentAreCheckedBeforeAnySubscription_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void helperMetadataAndPrivateParentAreCheckedBeforeAnySubscription() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        // Retain explicit SD authority before the actor pins the runtime. The
        // physical descriptor below proves that setup changed the intended ACL.
        Handle mutation;
        if (kind == 0 || kind == 1 || kind == 3) {
            mutation = mutableSecurity(kind == 1 ? f.runtime : f.helper, kind == 1);
            QVERIFY2(mutation.valid(), qPrintable(QString::number(GetLastError())));
            const auto acl = kind == 3 ? runtimeAcl(f.user, f.package) + u"(A;;GR;;;WD)"_s : channelAcl(f.user);
            QCOMPARE(replaceDacl(mutation.get(), acl), static_cast<DWORD>(ERROR_SUCCESS));
            const auto facts = daclFacts(mutation.get());
            QVERIFY(facts.valid && facts.protected_dacl);
            if (kind == 3) {
                QVERIFY(facts.count > 3);
                const auto world = QByteArray::fromHex("010100000000000100000000");
                QVERIFY(std::find(facts.trustees.begin(), facts.trustees.end(), world) != facts.trustees.end());
            } else {
                QCOMPARE(facts.count, 3UL);
                QVERIFY(std::find(facts.trustees.begin(), facts.trustees.end(), f.package) == facts.trustees.end());
                const auto administrators = QByteArray::fromHex("01020000000000052000000020020000");
                QVERIFY(std::find(facts.trustees.begin(), facts.trustees.end(), administrators) !=
                        facts.trustees.end());
            }
        }
        if (kind == 2) {
            const auto alias = f.parent.filePath(u"helper-hardlink.bin"_s);
            QVERIFY(CreateHardLinkW(nativePath(alias).c_str(), nativePath(f.helper).c_str(), nullptr));
        }
        if (kind == 4) {
            QVERIFY(QFile::remove(f.helper));
            QVERIFY(createDirectory(f.helper, runtimeAcl(f.user, f.package)));
        }
        auto fake = std::make_shared<Fake>(f);
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && !result.ready_written && result.result_written);
        QCOMPARE(result.reason, Actor::Reason::Helper);
        QCOMPARE(fake->opens, 0U);
    }
    void driverMustBePhysicallyRunningBeforeStartAndThroughoutWindow_data() {
        QTest::addColumn<bool>("after_ready");
        QTest::addColumn<bool>("unknown");
        QTest::newRow("stopped-before") << false << false;
        QTest::newRow("unknown-before") << false << true;
        QTest::newRow("stopped-after") << true << false;
        QTest::newRow("unknown-after") << true << true;
    }
    void driverMustBePhysicallyRunningBeforeStartAndThroughoutWindow() {
        QFETCH(bool, after_ready);
        QFETCH(bool, unknown);
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        const auto failed = unknown ? Process::Poll{Process::State::Unknown, ERROR_ACCESS_DENIED, std::nullopt}
                                    : Process::Poll{Process::State::Stopped, 0, 0};
        const auto ready_path = f.path(Files::Slot::Ready);
        fake->on_poll = [failed, after_ready, ready_path] {
            return !after_ready || QFile::exists(ready_path) ? failed
                                                             : Process::Poll{Process::State::Running, 0, std::nullopt};
        };
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && result.result_written);
        QCOMPARE(result.reason, unknown ? Actor::Reason::DriverUnknown : Actor::Reason::DriverStopped);
        QCOMPARE(result.ready_written, after_ready);
        QCOMPARE(fake->subscribes, after_ready ? 1U : 0U);
        QCOMPARE(fake->unsubscribes, after_ready ? 1U : 0U);
    }
    void clockRegressionAndExceptionsStopWithoutTurningFailureIntoSuccess_data() {
        QTest::addColumn<bool>("throw_clock");
        QTest::newRow("regression") << false;
        QTest::newRow("exception") << true;
    }
    void clockRegressionAndExceptionsStopWithoutTurningFailureIntoSuccess() {
        QFETCH(bool, throw_clock);
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        fake->step = 0;
        fake->during_pump = [fake, throw_clock] {
            fake->now = 99;
            if (throw_clock)
                fake->on_clock = []() -> std::int64_t { throw std::runtime_error("clock failed"); };
        };
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && result.ready_written && result.result_written);
        QCOMPARE(result.reason, Actor::Reason::Clock);
        QCOMPARE(fake->unsubscribes, 1U);
        QCOMPARE(fake->closes, 1U);
        const auto saved = f.result();
        QVERIFY(saved && saved->report);
        QCOMPARE(saved->report->state, Observer::State::Stopped);
    }
    void clockFailureDuringStopRejectsSuccessWithoutRewritingTheNativeReport_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("regression") << 0;
        QTest::newRow("exception") << 1;
        QTest::newRow("negative") << 2;
    }
    void clockFailureDuringStopRejectsSuccessWithoutRewritingTheNativeReport() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        fake->emit_event = true;
        fake->step = 1;
        fake->during_close = [fake, kind] {
            if (kind == 1)
                fake->on_clock = []() -> std::int64_t { throw std::runtime_error("clock failed during stop"); };
            else
                fake->now = kind == 0 ? 99 : -1;
        };
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && result.ready_written && result.result_written && !result.resources_retained);
        QCOMPARE(result.reason, Actor::Reason::Clock);
        QCOMPARE(fake->unsubscribes, 1U);
        QCOMPARE(fake->closes, 1U);
        const auto saved = f.result();
        QVERIFY(saved && saved->ready && saved->report);
        QCOMPARE(saved->report->state, Observer::State::Stopped);
        QCOMPARE(saved->report->reason, Observer::Reason::None);
        // The native observation is preserved. The actor exit failure, rather
        // than a rewritten report, prevents this run from becoming evidence.
        QVERIFY(saved->report->matched && saved->report->rule_verified && saved->report->first_match);
        QVERIFY(!saved->report->context_retained);
    }
    void failedStartWritesOnlyARealFailedReportWithoutReady() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        fake->open_error = ERROR_ACCESS_DENIED;
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QCOMPARE(result.reason, Actor::Reason::NetworkStart);
        QVERIFY(result.exit_code != 0 && !result.ready_written && result.result_written);
        const auto saved = f.result();
        QVERIFY(saved && saved->report && !saved->ready);
        QCOMPARE(saved->report->state, Observer::State::Unavailable);
        QCOMPARE(saved->report->open_error, std::uint32_t{ERROR_ACCESS_DENIED});
        QCOMPARE(saved->report->window_begin_filetime, std::uint64_t{0});
    }
    void readyWriteFailureStopsAndPreservesTheOccupiedLeaf() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        bool occupied = false;
        fake->during_subscribe = [&] { occupied = f.occupy(Files::Slot::Ready); };
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(occupied);
        QCOMPARE(result.reason, Actor::Reason::ReadyWrite);
        QVERIFY(result.exit_code != 0 && !result.ready_written && result.result_written);
        QCOMPARE(fake->unsubscribes, 1U);
        QCOMPARE(readFile(f.path(Files::Slot::Ready)), QByteArray{"occupied, preserve"});
        const auto saved = f.result();
        QVERIFY(saved && saved->report && !saved->ready);
    }
    void resultWriteFailureCannotClaimDeliveryOrReplaceAnExistingLeaf() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        bool occupied = false;
        fake->during_close = [&] { occupied = f.occupy(Files::Slot::Result); };
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(occupied);
        QCOMPARE(result.reason, Actor::Reason::ResultWrite);
        QVERIFY(result.exit_code != 0 && result.ready_written && !result.result_written);
        QCOMPARE(readFile(f.path(Files::Slot::Result)), QByteArray{"occupied, preserve"});
    }
    void driverExitDuringStopCannotReturnAHealthyExit() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        fake->during_close = [fake] { fake->driver_poll = {Process::State::Stopped, 0, 0}; };
        Actor actor(f.config(), calls(fake, f));
        const auto result = actor.run();
        QVERIFY(result.exit_code != 0 && result.ready_written && result.result_written);
        QCOMPARE(result.reason, Actor::Reason::DriverStopped);
        QCOMPARE(fake->unsubscribes, 1U);
    }
    void requestAndExecutablePinsSurviveUntilStopAndThenReleaseNormally() {
        Fixture f;
        QVERIFY(f.initialize());
        auto fake = std::make_shared<Fake>(f);
        bool pinned_at_start = false, pinned_at_stop = false;
        fake->during_open = [&] {
            pinned_at_start = writeBlocked(f.helper) && writeBlocked(f.source) && writeBlocked(f.probe) &&
                              writeBlocked(f.path(Files::Slot::Request));
        };
        fake->during_close = [&] {
            pinned_at_stop = writeBlocked(f.helper) && writeBlocked(f.path(Files::Slot::Request));
        };
        {
            Actor actor(f.config(), calls(fake, f));
            QCOMPARE(actor.run().exit_code, 0);
        }
        QVERIFY(pinned_at_start && pinned_at_stop);
        const Handle released(CreateFileW(nativePath(f.helper).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        QVERIFY(released.valid());
    }
    void stopUnconfirmedRetainsTheWholeOwnerEvenAfterActorDestruction() {
        Fixture f;
        QVERIFY(f.initialize());
        f.parent.setAutoRemove(false);
        qInfo().noquote() << "actor-retained-fixture:" + f.parent.path();
        auto fake = std::make_shared<Fake>(f);
        fake->unsubscribe_error = ERROR_ACCESS_DENIED;
        Actor::RunResult result;
        {
            Actor actor(f.config(), calls(fake, f));
            result = actor.run();
        }
        QCOMPARE(result.reason, Actor::Reason::NetworkStop);
        QVERIFY(result.exit_code != 0 && result.ready_written && result.result_written && result.resources_retained);
        const auto saved = f.result();
        QVERIFY(saved && saved->report && saved->report->context_retained);
        QCOMPARE(saved->report->state, Observer::State::StopUnconfirmed);
        QVERIFY(writeBlocked(f.helper));
        QVERIFY(writeBlocked(f.path(Files::Slot::Request)));
        QCOMPARE(fake->unsubscribes, 1U);
    }
};
QTEST_GUILESS_MAIN(AppContainerObserverActorTests)
#include "AppContainerObserverActorTests.moc"

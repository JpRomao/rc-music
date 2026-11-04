#include "AppContainerStorageLiveSource.hpp"
#include "AppContainerStorageNativeSession.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using namespace music_player::m0;
using namespace music_player::sandbox_probe;
using Inventory = AppContainerStorageInventory;
using Registry = AppContainerStorageRegistry;
using Files = AppContainerStorageFiles;
using Source = AppContainerStorageLiveSource;
namespace {
constexpr auto operation = "1701353d-3384-4e32-9341-72cedb405d84";
const QString prefix = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
const QString sid_text = QStringLiteral("S-1-15-2-1-2-3-4-5-6-7");
QByteArray packageSid() {
    return QByteArray::fromHex("010800000000000f0200000001000000020000000300000004000000050000000600000007000000");
}
// Small self-relative owner + NULL DACL descriptor. It tests transport and
// bounded structural reading, not a policy suitable for the real profile.
QByteArray descriptor() {
    QByteArray raw = QByteArray::fromHex("0100048014000000000000000000000000000000");
    raw += QByteArray::fromHex("010100000000000512000000");
    return raw;
}
struct Store final : DownloadMediaProfileStore {
    struct Lease final : DownloadMediaProfileLease {};
    DownloadMediaProfileLoadResult loaded{};
    std::unique_ptr<DownloadMediaProfileLease> acquireLease() override { return std::make_unique<Lease>(); }
    DownloadMediaProfileLoadResult load() override { return loaded; }
    DownloadCheckpointSaveStatus record(const DownloadMediaProfileRecord& record) override {
        loaded = {DownloadCheckpointLoadStatus::Loaded, record};
        return DownloadCheckpointSaveStatus::Saved;
    }
};
struct FakeRegistry final {
    struct Key final {
        QString name;
        Registry::Handle parent{};
    };
    std::vector<Key> keys{{}, {QStringLiteral("Software"), 0}};
    std::vector<Registry::Handle> opened{}, closed_keys{}, closed_events{};
    int opens{}, infos{}, security_reads{}, watches{}, polls{};
    std::function<void()> on_info{};
    bool fail_close{};
    FakeRegistry() {
        Registry::Handle parent = 1;
        for (const auto& name : {QStringLiteral("Microsoft"), QStringLiteral("Windows"),
                                 QStringLiteral("CurrentVersion"), QStringLiteral("AppContainer")}) {
            keys.push_back({name, parent});
            parent = keys.size() - 1;
        }
        for (const auto& kind : {QStringLiteral("Mappings"), QStringLiteral("Storage")}) {
            keys.push_back({kind, parent});
            keys.push_back({sid_text, keys.size() - 1});
        }
    }
    Registry::OpenResult acquired(const Registry::Handle key) {
        ++opens;
        opened.push_back(key);
        return {Registry::OpenState::Present, 0, key, {key, 0}, 1, 0};
    }
    QString nativeName(const Registry::Handle key) const {
        QString value;
        for (auto i = key; i; i = keys[i].parent)
            value.prepend(QStringLiteral("\\") + keys[i].name);
        return QStringLiteral("\\Registry\\User\\Fixture\\LocalSettings") + value;
    }
    Registry::Calls calls() {
        return {
            .open_software = [this] { return acquired(1); },
            .open_child =
                [this](Registry::Handle parent, const QString& name) {
                    for (Registry::Handle key = 1; key < keys.size(); ++key)
                        if (keys[key].parent == parent && keys[key].name.compare(name, Qt::CaseInsensitive) == 0)
                            return acquired(key);
                    return Registry::OpenResult{Registry::OpenState::Missing, 2};
                },
            .query_info =
                [this](Registry::Handle key, std::uint32_t) {
                    ++infos;
                    if (on_info)
                        on_info();
                    const auto children = std::count_if(keys.begin() + 1, keys.end(),
                                                        [key](const auto& child) { return child.parent == key; });
                    return Registry::KeyInfo{
                        0, nativeName(key), static_cast<std::uint32_t>(children), 1, Registry::ReservedValue::Absent, 0,
                        0};
                },
            .enum_child =
                [this](Registry::Handle parent, std::uint32_t index, std::uint32_t) {
                    for (Registry::Handle key = 1; key < keys.size(); ++key)
                        if (keys[key].parent == parent && index-- == 0)
                            return Registry::EnumResult{Inventory::ChildState::Child, 0, keys[key].name};
                    return Registry::EnumResult{Inventory::ChildState::End, 0, {}};
                },
            .watch_once =
                [this](Registry::Handle key) {
                    ++watches;
                    return Registry::WatchResult{0, key + 1000};
                },
            .poll_event =
                [this](Registry::Handle) {
                    ++polls;
                    return Registry::PollResult{Registry::EventState::Unchanged, 0};
                },
            .security = {.size_query =
                             [](AppContainerStorageSecurity::Object, Registry::Handle) {
                                 return AppContainerStorageSecurity::NativeResult{
                                     122, static_cast<std::uint32_t>(descriptor().size())};
                             },
                         .read =
                             [this](AppContainerStorageSecurity::Object object, Registry::Handle,
                                    std::span<std::uint8_t> bytes) {
                                 ++security_reads;
                                 const auto raw = descriptor();
                                 if (object != AppContainerStorageSecurity::Object::Key ||
                                     bytes.size() < static_cast<std::size_t>(raw.size()))
                                     return AppContainerStorageSecurity::NativeResult{13, 0};
                                 std::memcpy(bytes.data(), raw.constData(), static_cast<std::size_t>(raw.size()));
                                 return AppContainerStorageSecurity::NativeResult{
                                     0, static_cast<std::uint32_t>(raw.size())};
                             }},
            .close_key =
                [this](Registry::Handle key) {
                    closed_keys.push_back(key);
                    return fail_close ? 5U : 0U;
                },
            .close_event =
                [this](Registry::Handle event) {
                    closed_events.push_back(event);
                    return 0U;
                }};
    }
};
struct Fixture final {
    // Auto-removal is confined to this new ordinary test directory. No native
    // profile is created, discovered or removed by this fixture.
    QTemporaryDir temp;
    Store store;
    FakeRegistry registry;
    int creates{}, removes{}, file_opens{};
    WindowsNativeMediaProfile backend{
        {.create =
             [this](const QString&) {
                 ++creates;
                 return WindowsNativeMediaProfile::Result{0, packageSid(), true};
             },
         .remove =
             [this](const QString&) {
                 ++removes;
                 return qint32{0};
             },
         .observe = [](const QString&, const QByteArray&) { return WindowsMediaProfileAbsence::Absent; },
         .location =
             [this](const QString& name, const QByteArray& sid) {
                 WindowsNativeMediaProfile::StorageLocation location;
                 location.name = name;
                 location.sid = sid;
                 location.creator_user_sid = QByteArray::fromHex("010100000000000512000000");
                 location.package_root = temp.path() + QStringLiteral("/Packages/") + name;
                 location.ac_path = location.package_root + QStringLiteral("/AC");
                 location.mappings_key = prefix + QStringLiteral("Mappings\\") + sid_text;
                 location.storage_key = prefix + QStringLiteral("Storage\\") + sid_text;
                 return std::optional{location};
             }}};
    WindowsMediaProfileOwner owner{store, backend, {"native-session-test", [] { return std::string{operation}; }}};
    WindowsNativeMediaProfile::StorageLocation location() const {
        WindowsNativeMediaProfile::StorageLocation value;
        value.package_root = temp.path() + QStringLiteral("/Packages/RCMusic.Media.") + operation;
        value.ac_path = value.package_root + QStringLiteral("/AC");
        value.mappings_key = prefix + QStringLiteral("Mappings\\") + sid_text;
        value.storage_key = prefix + QStringLiteral("Storage\\") + sid_text;
        return value;
    }
    Files::Calls files() {
        auto calls = Files::systemCalls();
        calls.before_open = [this](const QString&) { ++file_opens; };
        return calls;
    }
    Source::Factory factory() {
        return [this](const auto& value) { return appContainerStorageNativeSession(value, files(), registry.calls()); };
    }
    bool prepareFiles() const { return temp.isValid() && QDir{}.mkpath(location().ac_path); }
};
} // namespace

class AppContainerStorageNativeSessionTests final : public QObject {
    Q_OBJECT
  private slots:
    void factoryAndLiveBeginBindWithoutNativeReads() {
        Fixture f;
        QVERIFY(f.prepareFiles());
        auto direct = appContainerStorageNativeSession(f.location(), f.files(), f.registry.calls());
        QVERIFY(direct);
        QCOMPARE(f.file_opens, 0);
        QCOMPARE(f.registry.opens, 0);
        QCOMPARE(direct->close().state, Inventory::CloseState::Closed);
        QVERIFY(f.owner.prepare());
        Source source(f.owner, f.backend, f.factory());
        auto begun = source.begin();
        QCOMPARE(begun.provenance, Inventory::Provenance::LiveCreated);
        QVERIFY(begun.session);
        QCOMPARE(f.file_opens, 0);
        QCOMPARE(f.registry.opens, 0);
        QCOMPARE(begun.session->close().state, Inventory::CloseState::Closed);
        QVERIFY(f.owner.close());
        QCOMPARE(f.creates, 1); // Fake profile calls only.
        QCOMPARE(f.removes, 1);
    }
    void liveInventoryComposesRealFixtureFilesAndFakeRegistry() {
        Fixture f;
        QVERIFY(f.prepareFiles());
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(f.owner, f.backend, f.factory());
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        QCOMPARE(result.report.objects.size(), std::size_t{4});
        QCOMPARE(result.report.validation.state, Inventory::Validation::Unchanged);
        QVERIFY(f.file_opens > 0);
        QVERIFY(f.registry.security_reads >= 2);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenBusy);
        QCOMPARE(f.removes, 0);
        QVERIFY(result.lease);
        QCOMPARE(result.lease->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.registry.closed_keys.size(), f.registry.opened.size());
        QCOMPARE(f.registry.closed_events.size(), static_cast<std::size_t>(f.registry.watches));
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenIdle);
        QVERIFY(f.owner.close());
        QCOMPARE(f.removes, 1);
    }
    void borrowedFilePathsAreCopiedBeforeTheCallerChangesBuffers() {
        Fixture f;
        QVERIFY(f.prepareFiles());
        auto value = f.location();
        QString package = value.package_root, ac = value.ac_path;
        value.package_root = QString::fromRawData(package.constData(), package.size());
        value.ac_path = QString::fromRawData(ac.constData(), ac.size());
        auto session = appContainerStorageNativeSession(value, f.files(), f.registry.calls());
        QVERIFY(session);
        package.fill('x');
        ac.fill('x');
        QCOMPARE(session->root(Inventory::Root::PackageRoot).presence, Inventory::Presence::Present);
        QCOMPARE(session->root(Inventory::Root::AC).presence, Inventory::Presence::Present);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void cancellationDuringARegistryReadRequiresExplicitResourceClosure() {
        Fixture f;
        QVERIFY(f.prepareFiles());
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(f.owner, f.backend, f.factory());
        auto begun = source.begin();
        QVERIFY(begun.session);
        f.registry.on_info = [&] { static_cast<void>(f.owner.close()); };
        QCOMPARE(begun.session->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Unknown);
        QCOMPARE(f.owner.state(), WindowsMediaProfileOwnerState::Draining);
        QCOMPARE(f.removes, 0);
        QCOMPARE(begun.session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.registry.closed_keys.size(), f.registry.opened.size());
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::ClosedQuiescent);
        QVERIFY(f.owner.close());
        QCOMPARE(f.removes, 1);
    }
    void uncertainRegistryCloseCannotAcknowledgeTheLiveFence() {
        Fixture f;
        QVERIFY(f.prepareFiles());
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(f.owner, f.backend, f.factory());
        auto begun = source.begin();
        QVERIFY(begun.session);
        QCOMPARE(begun.session->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        f.registry.fail_close = true;
        QCOMPARE(begun.session->close().state, Inventory::CloseState::Unknown);
        QCOMPARE(f.removes, 0);
        QVERIFY(!f.owner.close());
        QCOMPARE(f.removes, 0);
        const auto count = f.registry.closed_keys.size();
        QCOMPARE(begun.session->close().state, Inventory::CloseState::Unknown);
        QCOMPARE(f.registry.closed_keys.size(), count);
        QVERIFY(f.registry.closed_events.empty());
        QVERIFY(cap->nativeUseFence()->state() != WindowsDownloadNativeUseFence::State::ClosedQuiescent);
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageNativeSessionTests)
#include "AppContainerStorageNativeSessionTests.moc"

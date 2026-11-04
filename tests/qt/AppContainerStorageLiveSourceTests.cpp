#include "AppContainerStorageLiveSource.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"

#include <QTest>

#include <algorithm>
#include <stdexcept>
#include <utility>

using namespace music_player::m0;
using Inventory = music_player::sandbox_probe::AppContainerStorageInventory;
using Source = music_player::sandbox_probe::AppContainerStorageLiveSource;
namespace {
constexpr auto operation = "1701353d-3384-4e32-9341-72cedb405d84";
QByteArray packageSid() {
    return QByteArray::fromHex("010800000000000f0200000001000000020000000300000004000000050000000600000007000000");
}
struct Store final : DownloadMediaProfileStore {
    struct Lease final : DownloadMediaProfileLease {};
    DownloadMediaProfileLoadResult loaded{};
    int saves{};
    std::unique_ptr<DownloadMediaProfileLease> acquireLease() override { return std::make_unique<Lease>(); }
    DownloadMediaProfileLoadResult load() override { return loaded; }
    DownloadCheckpointSaveStatus record(const DownloadMediaProfileRecord& value) override {
        ++saves;
        loaded = {DownloadCheckpointLoadStatus::Loaded, value};
        return DownloadCheckpointSaveStatus::Saved;
    }
};
struct State final {
    int roots{}, reads{}, children{}, validations{}, closes{}, destroyed{};
    Inventory::CloseResult closure{Inventory::CloseState::Closed, 0};
    std::function<void()> on_read{}, on_close{};
    bool throw_read{}, throw_close{};
};
struct Session final : Inventory::ReadSession {
    std::shared_ptr<State> state;
    explicit Session(std::shared_ptr<State> value) : state(std::move(value)) {}
    ~Session() override { ++state->destroyed; }
    Inventory::RootResult root(Inventory::Root) override {
        ++state->roots;
        return {{1}, 0, Inventory::Presence::Present};
    }
    Inventory::Metadata readMetadata(Inventory::NodeRef ref, std::uint64_t) override {
        auto held = state;
        ++held->reads;
        if (held->on_read)
            held->on_read();
        if (held->throw_read)
            throw std::runtime_error{"metadata unavailable"};
        return {.ref = ref, .presence = Inventory::Presence::Present, .error = 0};
    }
    Inventory::ChildResult nextChild(Inventory::NodeRef, std::uint32_t, std::uint32_t) override {
        ++state->children;
        return {Inventory::ChildState::End, 0, {}, {}};
    }
    Inventory::ValidationResult revalidate() override {
        ++state->validations;
        return {Inventory::Validation::Unchanged, 0};
    }
    Inventory::CloseResult close() override {
        auto held = state;
        ++held->closes;
        if (held->on_close)
            held->on_close();
        if (held->throw_close)
            throw std::runtime_error{"native close unknown"};
        return held->closure;
    }
};
struct Fixture final {
    Store store;
    QByteArray sid{packageSid()};
    std::shared_ptr<State> state{std::make_shared<State>()};
    int creates{}, removes{}, locations{}, factories{};
    std::function<void()> on_location{}, on_factory{};
    WindowsNativeMediaProfile backend{
        {.create =
             [&](const QString&) {
                 ++creates;
                 return WindowsNativeMediaProfile::Result{0, sid, true};
             },
         .remove =
             [&](const QString&) {
                 ++removes;
                 return qint32{0};
             },
         .observe = [](const QString&, const QByteArray&) { return WindowsMediaProfileAbsence::Absent; },
         .location =
             [&](const QString& name, const QByteArray& profile_sid) {
                 ++locations;
                 if (on_location)
                     on_location();
                 WindowsNativeMediaProfile::StorageLocation value;
                 value.name = name;
                 value.sid = profile_sid;
                 value.creator_user_sid = QByteArray::fromHex("010100000000000512000000");
                 value.package_root = QStringLiteral("C:/private/Packages/") + name;
                 value.ac_path = value.package_root + QStringLiteral("/AC");
                 const auto key = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
                 const auto sid_text = QStringLiteral("S-1-15-2-1-2-3-4-5-6-7");
                 value.mappings_key = key + QStringLiteral("Mappings\\") + sid_text;
                 value.storage_key = key + QStringLiteral("Storage\\") + sid_text;
                 return std::optional{value};
             }}};
    WindowsMediaProfileOwner owner{store, backend, {"storage-test", [] { return std::string{operation}; }}};
    Source::Factory factory() {
        return [&](const WindowsNativeMediaProfile::StorageLocation& value) {
            ++factories;
            if (on_factory)
                on_factory();
            if (value.sid != sid)
                return std::unique_ptr<Inventory::ReadSession>{};
            return std::unique_ptr<Inventory::ReadSession>{std::make_unique<Session>(state)};
        };
    }
};
} // namespace

class AppContainerStorageLiveSourceTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionIsInertAndAnIdleOwnerCannotCreateAReadScope() {
        Fixture f;
        Source source(f.owner, f.backend, f.factory());
        QCOMPARE(f.creates, 0);
        QCOMPARE(f.locations, 0);
        QVERIFY(f.owner.usesPlatform(f.backend));
        const auto result = source.begin();
        QVERIFY(result.provenance != Inventory::Provenance::LiveCreated && !result.session);
        QCOMPARE(f.factories, 0);
        QCOMPARE(f.creates, 0);
    }
    void aDifferentBackendCannotBorrowAnotherOwnersLiveFenceEvenWithTheSameSid() {
        Fixture f;
        Fixture other;
        QVERIFY(f.owner.prepare());
        QVERIFY(other.owner.prepare());
        QCOMPARE(f.owner.capability()->sidBytes(), other.owner.capability()->sidBytes());
        QVERIFY(!f.owner.usesPlatform(other.backend));
        const auto cap = f.owner.capability();
        Source source(f.owner, other.backend, other.factory());
        const auto result = source.begin();
        QVERIFY(result.provenance != Inventory::Provenance::LiveCreated && !result.session);
        QCOMPARE(other.locations, 0);
        QCOMPARE(other.factories, 0);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenIdle);
        QVERIFY(f.owner.close());
        QVERIFY(other.owner.close());
    }
    void readyOwnerReservesItsExactFenceUntilExplicitReadClosure() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QCOMPARE(result.provenance, Inventory::Provenance::LiveCreated);
        QCOMPARE(result.error, 0U);
        QVERIFY(result.session);
        QCOMPARE(f.creates, 1);
        QCOMPARE(f.factories, 1);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenBusy);
        QVERIFY(!cap->nativeUseFence()->begin());
        QCOMPARE(result.session->root(Inventory::Root::PackageRoot).error, 0U);
        QCOMPARE(result.session->readMetadata({1}, 1024).error, 0U);
        QCOMPARE(result.session->nextChild({1}, 0, 255).state, Inventory::ChildState::End);
        QCOMPARE(result.session->revalidate().state, Inventory::Validation::Unchanged);
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.state->closes, 1);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenIdle);
        const auto reads_before = f.state->reads;
        QCOMPARE(result.session->readMetadata({1}, 1024).presence, Inventory::Presence::Unknown);
        QCOMPARE(f.state->reads, reads_before);
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.state->closes, 1);
        QVERIFY(f.owner.close());
        QCOMPARE(f.removes, 1);
    }
    void ownerCloseDrainsBehindTheReadTicketAndRevokesFurtherReads() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QVERIFY(result.session);
        QVERIFY(!f.owner.close());
        QCOMPARE(f.owner.state(), WindowsMediaProfileOwnerState::Draining);
        QCOMPARE(f.removes, 0);
        QCOMPARE(result.session->root(Inventory::Root::PackageRoot).presence, Inventory::Presence::Unknown);
        QCOMPARE(result.session->revalidate().state, Inventory::Validation::Changed);
        QCOMPARE(f.state->roots, 0);
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::ClosedQuiescent);
        QCOMPARE(f.removes, 0);
        QVERIFY(f.owner.close());
        QCOMPARE(f.removes, 1);
    }
    void cancelDuringMetadataReadCannotReturnAUsableObservation() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QVERIFY(result.session);
        f.state->on_read = [&] { static_cast<void>(f.owner.close()); };
        const auto metadata = result.session->readMetadata({1}, 1024);
        QCOMPARE(metadata.presence, Inventory::Presence::Unknown);
        QVERIFY(metadata.error != 0);
        QCOMPARE(f.removes, 0);
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QVERIFY(f.owner.close());
    }
    void cancellationDuringTheInertFactoryReturnsARevokedLeaseForExplicitClosure() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        f.on_factory = [&] { static_cast<void>(f.owner.close()); };
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QCOMPARE(result.provenance, Inventory::Provenance::Revoked);
        QVERIFY(result.error != 0 && result.session);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::ClosingBusy);
        QCOMPARE(f.removes, 0);
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QVERIFY(f.owner.close());
    }
    void closeDuringAReadAndReentrantCloseNeverDestroyTheActiveSession() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QVERIFY(result.session);
        Inventory::CloseResult during_read, during_close;
        f.state->on_read = [&] {
            during_read = result.session->close();
            QCOMPARE(f.state->destroyed, 0);
        };
        QCOMPARE(result.session->readMetadata({1}, 1024).error, 0U);
        QCOMPARE(during_read.state, Inventory::CloseState::NotAttempted);
        QCOMPARE(during_read.error, 170U);
        QCOMPARE(f.state->closes, 0);
        f.state->on_close = [&] { during_close = result.session->close(); };
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(during_close.state, Inventory::CloseState::NotAttempted);
        QCOMPARE(during_close.error, 170U);
        QCOMPARE(f.state->closes, 1);
        QCOMPARE(f.state->destroyed, 1);
        QVERIFY(f.owner.close());
    }
    void readExceptionCannotReleaseTheReadFence() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QVERIFY(result.session);
        f.state->throw_read = true;
        const auto metadata = result.session->readMetadata({1}, 1024);
        QCOMPARE(metadata.presence, Inventory::Presence::Unknown);
        QVERIFY(metadata.error != 0);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenBusy);
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QVERIFY(f.owner.close());
    }
    void loadedHistoryIsRejectedWithoutCallingTheFactoryOrLocator() {
        Fixture f;
        DownloadMediaProfileSid id{};
        std::copy(f.sid.begin(), f.sid.end(), id.begin());
        const DownloadMediaProfileRecord record{"old", operation, DownloadMediaProfileStage::InUse, id};
        QVERIFY(validDownloadMediaProfileRecord(record));
        f.store.loaded = {DownloadCheckpointLoadStatus::Loaded, record};
        QVERIFY(!f.owner.prepare());
        Source source(f.owner, f.backend, f.factory());
        const auto result = source.begin();
        QVERIFY(result.provenance != Inventory::Provenance::LiveCreated);
        QVERIFY(!result.session);
        QCOMPARE(f.locations, 0);
        QCOMPARE(f.factories, 0);
        QCOMPARE(f.creates, 0);
    }
    void unavailableFenceAndRepeatedBeginNeverMintAnotherSession() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        auto occupied = cap->nativeUseFence()->begin();
        QVERIFY(occupied);
        Source blocked(f.owner, f.backend, f.factory());
        QVERIFY(!blocked.begin().session);
        QCOMPARE(f.locations, 0);
        QVERIFY(occupied->acknowledgeNoChild());
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QVERIFY(result.session);
        QVERIFY(!source.begin().session);
        QCOMPARE(f.factories, 1);
        QCOMPARE(result.session->close().state, Inventory::CloseState::Closed);
        QVERIFY(f.owner.close());
    }
    void closeFromTheLocatorCallbackCannotPublishARevokedScope() {
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        f.on_location = [&] { static_cast<void>(f.owner.close()); };
        Source source(f.owner, f.backend, f.factory());
        const auto result = source.begin();
        QVERIFY(result.provenance != Inventory::Provenance::LiveCreated && !result.session);
        QCOMPARE(f.factories, 0);
        QCOMPARE(f.removes, 0);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::ClosedQuiescent);
        QVERIFY(f.owner.close());
    }
    void failingInertFactoryReturnsNoChildAndDoesNotCreateAnotherProfile_data() {
        QTest::addColumn<bool>("throwing");
        QTest::newRow("empty") << false;
        QTest::newRow("throws") << true;
    }
    void failingInertFactoryReturnsNoChildAndDoesNotCreateAnotherProfile() {
        QFETCH(bool, throwing);
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(
            f.owner, f.backend,
            [&](const WindowsNativeMediaProfile::StorageLocation&) -> std::unique_ptr<Inventory::ReadSession> {
                if (throwing)
                    throw std::runtime_error{"allocation failed before native I/O"};
                return {};
            });
        const auto result = source.begin();
        QVERIFY(!result.session);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenIdle);
        QCOMPARE(f.creates, 1);
        QVERIFY(f.owner.close());
    }
    void unknownCloseOrAbandonedSessionNeverAcknowledgesTheFence_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 4; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void unknownCloseOrAbandonedSessionNeverAcknowledgesTheFence() {
        QFETCH(int, kind);
        Fixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        Source source(f.owner, f.backend, f.factory());
        auto result = source.begin();
        QVERIFY(result.session);
        if (kind != 2) {
            f.state->throw_close = kind == 3;
            f.state->closure = kind == 0 ? Inventory::CloseResult{Inventory::CloseState::Unknown, 5}
                                         : Inventory::CloseResult{Inventory::CloseState::Closed, 5};
            QCOMPARE(result.session->close().state, Inventory::CloseState::Unknown);
            QCOMPARE(result.session->close().state, Inventory::CloseState::Unknown);
            QCOMPARE(f.state->closes, 1);
        }
        result.session.reset();
        QCOMPARE(f.state->destroyed, 0);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::Uncertain);
        QVERIFY(!f.owner.close());
        QCOMPARE(f.removes, 0);
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageLiveSourceTests)
#include "AppContainerStorageLiveSourceTests.moc"

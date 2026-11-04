#include "../windows/sandbox/AppContainerStorageInventoryRun.hpp"
#include "../windows/sandbox/AppContainerStorageLiveSource.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"

#include <QTest>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using Run = music_player::sandbox_probe::AppContainerStorageInventoryRun;
using Inventory = Run::Inventory;
using LiveSource = music_player::sandbox_probe::AppContainerStorageLiveSource;
using namespace music_player::m0;
namespace {
Run::Recorder checkedRecorder(std::function<void(const Inventory::Report&)> check) {
    return [check = std::move(check)](const Inventory::Report& report) {
        check(report); // Qt assertions return from the void check, not the bool recorder.
        return true;
    };
}
QByteArray ownerSid() { return QByteArray::fromHex("010100000000000512000000"); }
Inventory::Metadata metadata(const std::uint64_t ref) {
    return {.ref = {ref},
            .identity = {ref < 3 ? 1ULL : 2ULL, ref},
            .presence = Inventory::Presence::Present,
            .type = ref < 3 ? Inventory::Type::Directory : Inventory::Type::RegistryKey,
            .error = 0,
            .link_count = ref < 3 ? 1U : 0U,
            .owner_sid = ownerSid(),
            .raw_sd = "fake opaque SD", // Trusted-port fixture, not a Win32 security descriptor.
            .dacl = Inventory::Dacl::Present};
}
struct State final {
    int begins{}, roots{}, reads{}, validations{}, closes{}, destroyed{};
    Inventory::Provenance provenance{Inventory::Provenance::LiveCreated};
    std::uint32_t begin_error{};
    bool no_session{};
    QString throw_on{};
    std::map<std::uint64_t, Inventory::Metadata> objects{};
    Inventory::ValidationResult validation{Inventory::Validation::Unchanged, 0};
    Inventory::CloseResult closure{Inventory::CloseState::Closed, 0};
    std::vector<QString> operations{};
    State() {
        for (std::uint64_t ref = 1; ref <= 4; ++ref)
            objects.emplace(ref, metadata(ref));
    }
    void record(const QString& operation) {
        operations.push_back(operation);
        if (throw_on == operation)
            throw std::runtime_error{"trusted read port failed"};
    }
};
class Session final : public Inventory::ReadSession {
  public:
    explicit Session(std::shared_ptr<State> state) : state_(std::move(state)) {}
    ~Session() override { ++state_->destroyed; }
    Inventory::RootResult root(const Inventory::Root root) override {
        ++state_->roots;
        state_->record(QStringLiteral("root"));
        return {{static_cast<std::uint64_t>(root) + 1}, 0, Inventory::Presence::Present};
    }
    Inventory::Metadata readMetadata(const Inventory::NodeRef ref, std::uint64_t) override {
        ++state_->reads;
        state_->record(QStringLiteral("metadata"));
        return state_->objects.at(ref.value);
    }
    Inventory::ChildResult nextChild(const Inventory::NodeRef parent, const std::uint32_t index,
                                     std::uint32_t) override {
        state_->record(QStringLiteral("child"));
        if (parent.value == 1 && index == 0)
            return {Inventory::ChildState::Child, 0, QStringLiteral("AC"), {2}};
        return {Inventory::ChildState::End, 0};
    }
    Inventory::ValidationResult revalidate() override {
        ++state_->validations;
        state_->record(QStringLiteral("validate"));
        return state_->validation;
    }
    Inventory::CloseResult close() override {
        ++state_->closes;
        state_->record(QStringLiteral("close"));
        return state_->closure;
    }

  private:
    std::shared_ptr<State> state_;
};
class Source final : public Inventory::Source {
  public:
    std::shared_ptr<State> state{std::make_shared<State>()};
    Inventory::BeginResult begin() override {
        ++state->begins;
        state->record(QStringLiteral("begin"));
        return {state->provenance, state->begin_error, state->no_session ? nullptr : std::make_unique<Session>(state)};
    }
};
struct Store final : DownloadMediaProfileStore {
    struct Lease final : DownloadMediaProfileLease {};
    DownloadMediaProfileLoadResult loaded{};
    std::unique_ptr<DownloadMediaProfileLease> acquireLease() override { return std::make_unique<Lease>(); }
    DownloadMediaProfileLoadResult load() override { return loaded; }
    DownloadCheckpointSaveStatus record(const DownloadMediaProfileRecord& value) override {
        loaded = {DownloadCheckpointLoadStatus::Loaded, value};
        return DownloadCheckpointSaveStatus::Saved;
    }
};
struct LiveFixture final {
    Store store;
    std::shared_ptr<State> state{std::make_shared<State>()};
    int creates{}, removes{};
    WindowsNativeMediaProfile backend{
        {.create =
             [this](const QString&) {
                 ++creates;
                 return WindowsNativeMediaProfile::Result{
                     0,
                     QByteArray::fromHex(
                         "010800000000000f0200000001000000020000000300000004000000050000000600000007000000"),
                     true};
             },
         .remove =
             [this](const QString&) {
                 ++removes;
                 return qint32{0};
             },
         .observe = [](const QString&, const QByteArray&) { return WindowsMediaProfileAbsence::Absent; },
         .location =
             [](const QString& name, const QByteArray& sid) {
                 WindowsNativeMediaProfile::StorageLocation location;
                 location.name = name;
                 location.sid = sid;
                 location.creator_user_sid = ownerSid();
                 location.package_root = QStringLiteral("C:/private/Packages/") + name;
                 location.ac_path = location.package_root + QStringLiteral("/AC");
                 const auto prefix = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
                 const auto text_sid = QStringLiteral("S-1-15-2-1-2-3-4-5-6-7");
                 location.mappings_key = prefix + QStringLiteral("Mappings\\") + text_sid;
                 location.storage_key = prefix + QStringLiteral("Storage\\") + text_sid;
                 return std::optional{location};
             }}};
    WindowsMediaProfileOwner owner{
        store, backend, {"inventory-run-test", [] { return std::string{"1701353d-3384-4e32-9341-72cedb405d84"}; }}};
    LiveSource::Factory factory() {
        return [this](const WindowsNativeMediaProfile::StorageLocation&) {
            return std::unique_ptr<Inventory::ReadSession>{std::make_unique<Session>(state)};
        };
    }
};
} // namespace

class AppContainerStorageInventoryRunTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingRecorderRefusesBeforeInspectionOrAnyPortCall() {
        Source source;
        const auto result = Run::execute(source, {});
        QCOMPARE(result.record_state, Run::RecordState::Missing);
        QVERIFY(!result.inspection_called && !result.record_called && !result.post_validation_called &&
                !result.close_called);
        QVERIFY(!result.observed_and_revalidated && !result.read_resources_closed && !result.lease);
        QCOMPARE(source.state->begins, 0);
        QVERIFY(source.state->operations.empty());
        for (std::size_t index = 0; index < result.report.roots.size(); ++index)
            QCOMPARE(result.report.roots[index].root, static_cast<Inventory::Root>(index));
    }
    void snapshotIsRecordedWhileTheLeaseIsHeldBeforeRevalidationAndClose() {
        Source source;
        int recordings = 0;
        const auto result = Run::execute(source, checkedRecorder([&](const Inventory::Report& report) {
                                             ++recordings;
                                             source.state->record(QStringLiteral("record"));
                                             QCOMPARE(source.state->validations, 1);
                                             QCOMPARE(source.state->closes, 0);
                                             QCOMPARE(source.state->destroyed, 0);
                                             QCOMPARE(report.status, Inventory::Status::Observed);
                                             QCOMPARE(report.objects.size(), std::size_t{4});
                                         }));
        QCOMPARE(recordings, 1);
        QCOMPARE(result.record_state, Run::RecordState::Saved);
        QVERIFY(result.inspection_called && result.record_called && result.post_validation_called &&
                result.close_called);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        QCOMPARE(result.post_validation.state, Inventory::Validation::Unchanged);
        QCOMPARE(result.post_validation.error, 0U);
        QCOMPARE(result.closure.state, Inventory::CloseState::Closed);
        QCOMPARE(result.closure.error, 0U);
        QVERIFY(result.observed_and_revalidated && result.read_resources_closed && result.lease);
        QCOMPARE(source.state->validations, 2);
        QCOMPARE(source.state->closes, 1);
        QCOMPARE(source.state->destroyed, 1);
        const auto& operations = source.state->operations;
        QCOMPARE(operations[operations.size() - 3], QStringLiteral("record"));
        QCOMPARE(operations[operations.size() - 2], QStringLiteral("validate"));
        QCOMPARE(operations.back(), QStringLiteral("close"));
    }
    void recorderFailureAndExceptionRemainSeparateFromObservationAndClosure_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("save-failed") << false;
        QTest::newRow("save-threw") << true;
    }
    void recorderFailureAndExceptionRemainSeparateFromObservationAndClosure() {
        QFETCH(bool, throws);
        Source source;
        const auto result = Run::execute(source, [&](const Inventory::Report&) -> bool {
            if (throws)
                throw std::runtime_error{"evidence writer failed"};
            return false;
        });
        QCOMPARE(result.record_state, throws ? Run::RecordState::Exception : Run::RecordState::Failed);
        QVERIFY(result.record_called && result.post_validation_called && result.close_called);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        QVERIFY(result.observed_and_revalidated && result.read_resources_closed);
        QCOMPARE(source.state->closes, 1);
    }
    void aChangeAfterRecordingDoesNotRewriteTheEarlierSnapshotOrPreventKnownClosure_data() {
        QTest::addColumn<int>("validation");
        QTest::addColumn<uint>("error");
        QTest::newRow("changed") << int(Inventory::Validation::Changed) << 0U;
        QTest::newRow("unknown") << int(Inventory::Validation::Unknown) << 5U;
        QTest::newRow("contradictory-unchanged") << int(Inventory::Validation::Unchanged) << 5U;
    }
    void aChangeAfterRecordingDoesNotRewriteTheEarlierSnapshotOrPreventKnownClosure() {
        QFETCH(int, validation);
        QFETCH(uint, error);
        Source source;
        const auto result =
            Run::execute(source, checkedRecorder([&](const Inventory::Report& report) {
                             QCOMPARE(report.status, Inventory::Status::Observed);
                             source.state->validation = {static_cast<Inventory::Validation>(validation), error};
                         }));
        QCOMPARE(result.record_state, Run::RecordState::Saved);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        QVERIFY(result.post_validation_called);
        QVERIFY(result.post_validation.state != Inventory::Validation::Unchanged);
        QCOMPARE(result.post_validation.error, error);
        QVERIFY(!result.observed_and_revalidated);
        QVERIFY(result.read_resources_closed);
        QCOMPARE(source.state->closes, 1);
    }
    void revalidationExceptionStillClosesKnownReadResources() {
        Source source;
        const auto result = Run::execute(source, [&](const Inventory::Report&) {
            source.state->throw_on = QStringLiteral("validate");
            return true;
        });
        QVERIFY(result.post_validation_called && result.close_called);
        QCOMPARE(result.post_validation.state, Inventory::Validation::Unknown);
        QVERIFY(result.post_validation.error != 0);
        QVERIFY(!result.observed_and_revalidated && result.read_resources_closed);
        QCOMPARE(source.state->closes, 1);
    }
    void closeErrorsContradictionsBusyAndThrowsNeverBecomeAnAckOrAutomaticRetry_data() {
        QTest::addColumn<int>("closure");
        QTest::addColumn<uint>("error");
        QTest::addColumn<bool>("throws");
        QTest::newRow("unknown-error") << int(Inventory::CloseState::Unknown) << 5U << false;
        QTest::newRow("unknown-zero") << int(Inventory::CloseState::Unknown) << 0U << false;
        QTest::newRow("contradictory-closed") << int(Inventory::CloseState::Closed) << 5U << false;
        QTest::newRow("busy") << int(Inventory::CloseState::NotAttempted) << 170U << false;
        QTest::newRow("throw") << int(Inventory::CloseState::Unknown) << 31U << true;
    }
    void closeErrorsContradictionsBusyAndThrowsNeverBecomeAnAckOrAutomaticRetry() {
        QFETCH(int, closure);
        QFETCH(uint, error);
        QFETCH(bool, throws);
        Source source;
        source.state->closure = {static_cast<Inventory::CloseState>(closure), error};
        if (throws)
            source.state->throw_on = QStringLiteral("close");
        auto result = Run::execute(source, [](const Inventory::Report&) { return true; });
        QVERIFY(result.observed_and_revalidated && !result.read_resources_closed && result.close_called &&
                result.lease);
        QCOMPARE(result.closure.state, Inventory::CloseState::Unknown);
        QCOMPARE(result.closure.error, error);
        QCOMPARE(source.state->closes, 1);
        QCOMPARE(source.state->destroyed, 0);
        QCOMPARE(result.lease->close().state, Inventory::CloseState::Unknown);
        QCOMPARE(source.state->closes, 1);
        result.lease.reset();
        QCOMPARE(source.state->closes, 1);
        QCOMPARE(source.state->destroyed, 0);
    }
    void incompleteObservationCanRecordAndExplicitlyCloseWithoutBecomingObserved() {
        Source source;
        source.state->objects.at(3).error = 5;
        int recordings = 0;
        const auto result = Run::execute(source, checkedRecorder([&](const Inventory::Report& report) {
                                             ++recordings;
                                             QCOMPARE(report.status, Inventory::Status::Incomplete);
                                             QCOMPARE(report.reason, Inventory::Reason::Metadata);
                                             QCOMPARE(report.objects.size(), std::size_t{2});
                                         }));
        QCOMPARE(recordings, 1);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QVERIFY(result.record_called && result.post_validation_called && result.close_called);
        QVERIFY(!result.observed_and_revalidated && result.read_resources_closed);
    }
    void refusedProvenanceOrBeginErrorNeverRevalidatesButStillClosesProvidedResources_data() {
        QTest::addColumn<int>("provenance");
        QTest::addColumn<uint>("error");
        QTest::newRow("loaded") << int(Inventory::Provenance::Loaded) << 0U;
        QTest::newRow("revoked") << int(Inventory::Provenance::Revoked) << 0U;
        QTest::newRow("unknown") << int(Inventory::Provenance::Unknown) << 0U;
        QTest::newRow("begin-error") << int(Inventory::Provenance::LiveCreated) << 5U;
    }
    void refusedProvenanceOrBeginErrorNeverRevalidatesButStillClosesProvidedResources() {
        QFETCH(int, provenance);
        QFETCH(uint, error);
        Source source;
        source.state->provenance = static_cast<Inventory::Provenance>(provenance);
        source.state->begin_error = error;
        const auto result = Run::execute(source, [](const Inventory::Report&) { return true; });
        QCOMPARE(result.report.provenance, static_cast<Inventory::Provenance>(provenance));
        QCOMPARE(result.report.begin_error, error);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QVERIFY(result.record_called && result.close_called && result.lease);
        QVERIFY(!result.post_validation_called && !result.observed_and_revalidated);
        QVERIFY(result.read_resources_closed);
        QCOMPARE(source.state->roots, 0);
        QCOMPARE(source.state->reads, 0);
        QCOMPARE(source.state->validations, 0);
        QCOMPARE(source.state->closes, 1);
    }
    void noSessionAndSourceExceptionProducePartialEvidenceWithoutAClosureAck_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("session-missing") << false;
        QTest::newRow("source-threw") << true;
    }
    void noSessionAndSourceExceptionProducePartialEvidenceWithoutAClosureAck() {
        QFETCH(bool, throws);
        Source source;
        source.state->no_session = true;
        if (throws)
            source.state->throw_on = QStringLiteral("begin");
        const auto result = Run::execute(source, [](const Inventory::Report&) { return true; });
        QVERIFY(result.inspection_called && result.record_called);
        QCOMPARE(result.record_state, Run::RecordState::Saved);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QCOMPARE(result.report.reason, throws ? Inventory::Reason::Exception : Inventory::Reason::Source);
        QVERIFY(!result.post_validation_called && !result.close_called && !result.lease);
        QVERIFY(!result.observed_and_revalidated && !result.read_resources_closed);
        QCOMPARE(source.state->closes, 0);
    }
    void invalidInventoryLimitsArePreservedWithoutBeginningTheSource() {
        Source source;
        Inventory::Limits limits;
        limits.objects = 0;
        const auto result = Run::execute(source, [](const Inventory::Report&) { return true; }, limits);
        QCOMPARE(result.report.reason, Inventory::Reason::Limits);
        QCOMPARE(result.record_state, Run::RecordState::Saved);
        QCOMPARE(source.state->begins, 0);
        QVERIFY(result.inspection_called && result.record_called);
        QVERIFY(!result.lease && !result.post_validation_called && !result.close_called &&
                !result.read_resources_closed);
    }
    void liveSourceKeepsTheFenceBusyUntilReadCloseWithoutRemovingTheProfile() {
        LiveFixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        LiveSource source(f.owner, f.backend, f.factory());
        const auto result =
            Run::execute(source, checkedRecorder([&](const Inventory::Report&) {
                             QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenBusy);
                             QCOMPARE(f.removes, 0);
                         }));
        QVERIFY(result.observed_and_revalidated && result.read_resources_closed);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenIdle);
        QCOMPARE(f.removes, 0);
        QCOMPARE(f.owner.state(), WindowsMediaProfileOwnerState::Ready);
        QVERIFY(f.owner.close()); // The owner, not Run, decides its own removal.
        QCOMPARE(f.removes, 1);
    }
    void cancellationWhileRecordingRevokesTheObservationAndDrainsOnlyAfterExplicitReadClose() {
        LiveFixture f;
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        LiveSource source(f.owner, f.backend, f.factory());
        const auto result = Run::execute(source, checkedRecorder([&](const Inventory::Report& report) {
                                             QCOMPARE(report.status, Inventory::Status::Observed);
                                             QVERIFY(!f.owner.close());
                                             QCOMPARE(f.owner.state(), WindowsMediaProfileOwnerState::Draining);
                                             QCOMPARE(f.removes, 0);
                                         }));
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        // FencedSource reports Changed/revoked; the outer ReadLease preserves
        // error 995 but normalizes any validation carrying an error to Unknown.
        QCOMPARE(result.post_validation.state, Inventory::Validation::Unknown);
        QCOMPARE(result.post_validation.error, 995U);
        QVERIFY(!result.observed_and_revalidated && result.read_resources_closed);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::ClosedQuiescent);
        QCOMPARE(f.removes, 0);
        QVERIFY(f.owner.close());
        QCOMPARE(f.removes, 1);
    }
    void uncertainReadCloseRetainsTheLiveFenceAndNeverCallsTheOwner() {
        LiveFixture f;
        f.state->closure = {Inventory::CloseState::Unknown, 5};
        QVERIFY(f.owner.prepare());
        const auto cap = f.owner.capability();
        LiveSource source(f.owner, f.backend, f.factory());
        auto result = Run::execute(source, [](const Inventory::Report&) { return true; });
        QVERIFY(result.observed_and_revalidated && !result.read_resources_closed && result.lease);
        QCOMPARE(f.removes, 0);
        QCOMPARE(cap->nativeUseFence()->state(), WindowsDownloadNativeUseFence::State::OpenBusy);
        QVERIFY(!f.owner.close());
        QCOMPARE(f.removes, 0);
        result.lease.reset();
        QVERIFY(cap->nativeUseFence()->state() != WindowsDownloadNativeUseFence::State::ClosedQuiescent);
        QCOMPARE(f.state->closes, 1);
        QCOMPARE(f.removes, 0);
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageInventoryRunTests)
#include "AppContainerStorageInventoryRunTests.moc"

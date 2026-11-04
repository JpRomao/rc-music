#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadMediaProfileStore.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"
#include "adapters/WindowsMediaProfileOwner.hpp"

#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <Sddl.h>

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace music_player::m0;
namespace {
using Stage = DownloadMediaProfileStage;
using State = WindowsMediaProfileOwnerState;
const std::string first_id = "12345678-1234-4234-8234-123456789abc";
const std::string second_id = "12345678-1234-4234-9234-123456789abd";
bool createJournalDirectory(const QString& directory) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token(raw);
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
    if (size == 0 || size > 64 * 1024)
        return false;
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
        return false;
    LPWSTR user = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &user))
        return false;
    const auto free_user = qScopeGuard([&] { LocalFree(user); });
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(user) + ')';
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(download_artifacts::nativePath(directory).c_str(), &attributes) != FALSE;
}
QByteArray testSid() {
    QByteArray sid(40, '\0');
    sid[0] = 1;
    sid[1] = 8;
    sid[7] = 15;
    sid[8] = 2;
    sid[12] = 7;
    return sid;
}
DownloadMediaProfileRecord evidence(Stage stage, std::string operation = first_id) {
    DownloadMediaProfileRecord result{"job-1", std::move(operation), stage, {}};
    if (stage != Stage::Planned && stage != Stage::Abandoned) {
        DownloadMediaProfileSid sid{};
        const auto bytes = testSid();
        std::copy(bytes.begin(), bytes.end(), sid.begin());
        result.sid = sid;
    }
    return result;
}
const char* label(Stage stage) {
    switch (stage) {
    case Stage::Planned:
        return "planned";
    case Stage::Created:
        return "created";
    case Stage::InUse:
        return "in-use";
    case Stage::CleanupPending:
        return "cleanup-pending";
    case Stage::Removed:
        return "removed";
    case Stage::Abandoned:
        return "abandoned";
    }
    return "invalid";
}
struct Store final : DownloadMediaProfileStore {
    std::vector<std::string>& events;
    std::optional<DownloadMediaProfileRecord> stored;
    std::optional<DownloadMediaProfileLoadResult> forced_load;
    std::vector<DownloadMediaProfileRecord> writes;
    std::optional<Stage> failing_stage;
    bool after_commit{false};
    bool blocked{false};
    int failures_left{0};
    int loads{0};
    int claims{0};
    bool leased{false};
    std::function<void()> on_load;
    std::function<void(const DownloadMediaProfileRecord&)> on_record;
    explicit Store(std::vector<std::string>& log) : events(log) {}
    struct Lease final : DownloadMediaProfileLease {
        Store& owner;
        explicit Lease(Store& value) : owner(value) {}
        ~Lease() override {
            owner.leased = false;
            owner.events.push_back("release-lease");
        }
    };
    std::unique_ptr<DownloadMediaProfileLease> acquireLease() override {
        ++claims;
        events.push_back("claim");
        if (leased)
            return {};
        leased = true;
        return std::make_unique<Lease>(*this);
    }
    DownloadMediaProfileLoadResult load() override {
        ++loads;
        events.push_back("load");
        auto hook = std::exchange(on_load, {});
        if (hook) {
            hook();
            on_load = std::move(hook);
        }
        if (forced_load)
            return *forced_load;
        return {stored ? DownloadCheckpointLoadStatus::Loaded : DownloadCheckpointLoadStatus::Missing, stored};
    }
    DownloadCheckpointSaveStatus record(const DownloadMediaProfileRecord& value) override {
        events.push_back(label(value.stage));
        writes.push_back(value);
        if (blocked)
            return DownloadCheckpointSaveStatus::BlockedByExistingData;
        if (!validDownloadMediaProfileRecord(value) ||
            (stored && !mayReplaceDownloadMediaProfileRecord(*stored, value)))
            return DownloadCheckpointSaveStatus::BlockedByExistingData;
        const bool fail = failing_stage == value.stage && failures_left > 0;
        if (fail)
            --failures_left;
        if (!fail || after_commit)
            stored = value;
        auto hook = std::exchange(on_record, {});
        if (hook) {
            hook(value);
            on_record = std::move(hook);
        }
        return fail ? DownloadCheckpointSaveStatus::Failed : DownloadCheckpointSaveStatus::Saved;
    }
};
struct Platform final : WindowsMediaProfilePlatform {
    std::vector<std::string>& events;
    WindowsMediaProfileCreateResult creation{WindowsMediaProfileCreateStatus::Created, testSid()};
    WindowsMediaProfileRemovalResult removal{true, WindowsMediaProfileAbsence::Absent};
    std::vector<QString> created_names, removed_names;
    std::vector<QByteArray> removed_sids;
    std::function<void()> on_create, on_remove;
    bool throw_create{false};
    explicit Platform(std::vector<std::string>& log) : events(log) {}
    WindowsMediaProfileCreateResult create(const QString& name) override {
        events.push_back("create-api");
        created_names.push_back(name);
        if (on_create)
            on_create();
        if (throw_create)
            throw std::runtime_error("uncertain creation");
        return creation;
    }
    WindowsMediaProfileRemovalResult remove(const QString& name, const QByteArray& sid) override {
        events.push_back("remove-api");
        removed_names.push_back(name);
        removed_sids.push_back(sid);
        if (on_remove)
            on_remove();
        return removal;
    }
};
struct Harness final {
    std::vector<std::string> events;
    Store store{events};
    Platform platform{events};
    std::string operation{first_id};
    int identities{0};
    std::unique_ptr<WindowsMediaProfileOwner> owner;
    Harness() {
        owner = std::make_unique<WindowsMediaProfileOwner>(store, platform,
                                                           WindowsMediaProfileOwnerConfig{"job-1", [this] {
                                                                                              ++identities;
                                                                                              return operation;
                                                                                          }});
    }
};
} // namespace

class WindowsMediaProfileOwnerTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndDestructionAreInert() {
        Harness h;
        QCOMPARE(h.owner->state(), State::Idle);
        QVERIFY(!h.owner->capability());
        QVERIFY(!h.owner->canRetryPersistence());
        h.owner.reset();
        QVERIFY(h.events.empty());
        QCOMPARE(h.identities, 0);
    }
    void destroyingAnOwnerRevokesAdmissionWithoutInventingCleanup() {
        Harness h;
        QVERIFY(h.owner->prepare());
        const auto cap = h.owner->capability();
        const auto count = h.events.size();
        h.owner.reset();
        QCOMPARE(h.events.size(), count + 1);
        QCOMPARE(h.events.back(), std::string{"release-lease"});
        QVERIFY(!h.store.leased);
        QCOMPARE(h.store.stored->stage, Stage::InUse);
        QVERIFY(h.platform.removed_names.empty());
        QVERIFY(!cap->nativeUseFence()->begin());
    }
    void defaultOperationIdentityIsGeneratedInternallyOnlyWhenPreparing() {
        std::vector<std::string> events;
        Store store(events);
        Platform platform(events);
        WindowsMediaProfileOwner owner(store, platform, {"job-1", {}});
        QVERIFY(events.empty());
        QVERIFY(owner.prepare());
        QVERIFY(store.stored);
        QVERIFY(validDownloadMediaProfileRecord(*store.stored));
        QCOMPARE(platform.created_names.front(),
                 QString::fromStdString(downloadMediaProfileName(store.stored->operation_id)));
        QVERIFY(owner.close());
    }
    void preparationAndRemovalFollowAcknowledgedOrderAndDerivedIdentity() {
        Harness h;
        QVERIFY(h.owner->prepare());
        QCOMPARE(h.owner->state(), State::Ready);
        const auto cap = h.owner->capability();
        QVERIFY(cap && cap->nativeUseFence());
        QCOMPARE(cap->sidBytes(), testSid());
        QVERIFY((h.events == std::vector<std::string>{"claim", "load", "planned", "create-api", "created", "in-use"}));
        QVERIFY(h.owner->prepare());
        QCOMPARE(h.identities, 1);
        QVERIFY(h.owner->close());
        QCOMPARE(h.owner->state(), State::Removed);
        QVERIFY(!h.owner->capability());
        QCOMPARE(h.store.stored->stage, Stage::Removed);
        QCOMPARE(h.platform.created_names.front(), QString::fromStdString("RCMusic.Media." + first_id));
        QCOMPARE(h.platform.removed_names, h.platform.created_names);
        QCOMPARE(h.platform.removed_sids.front(), testSid());
        QVERIFY((h.events == std::vector<std::string>{"claim", "load", "planned", "create-api", "created", "in-use",
                                                      "cleanup-pending", "remove-api", "removed", "release-lease"}));
        QVERIFY(!cap->nativeUseFence()->begin());
        const auto count = h.events.size();
        QVERIFY(h.owner->close());
        QVERIFY(!h.owner->prepare());
        QCOMPARE(h.events.size(), count);
    }
    void everyFailedWriteRequiresExplicitRetryWithoutRepeatingSystemCalls_data() {
        QTest::addColumn<int>("stage");
        QTest::addColumn<bool>("after");
        for (const auto stage : {Stage::Planned, Stage::Created, Stage::InUse, Stage::CleanupPending, Stage::Removed})
            for (const bool after : {false, true})
                QTest::newRow((QByteArray(label(stage)) + (after ? "-after" : "-before")).constData())
                    << static_cast<int>(stage) << after;
    }
    void everyFailedWriteRequiresExplicitRetryWithoutRepeatingSystemCalls() {
        QFETCH(int, stage);
        QFETCH(bool, after);
        Harness h;
        const auto failing = static_cast<Stage>(stage);
        h.store.failing_stage = failing;
        h.store.failures_left = 1;
        h.store.after_commit = after;
        const bool during_close = failing == Stage::CleanupPending || failing == Stage::Removed;
        QCOMPARE(h.owner->prepare(), during_close);
        if (during_close)
            QVERIFY(!h.owner->close());
        QCOMPARE(h.owner->state(), State::PersistenceFailed);
        QVERIFY(h.owner->canRetryPersistence());
        QVERIFY(!h.owner->capability());
        const auto writes = h.store.writes.size();
        const auto creates = h.platform.created_names.size();
        const auto removes = h.platform.removed_names.size();
        QVERIFY(!h.owner->prepare());
        if (during_close)
            QVERIFY(!h.owner->close());
        QCOMPARE(h.store.writes.size(), writes);
        QCOMPARE(h.platform.created_names.size(), creates);
        QCOMPARE(h.platform.removed_names.size(), removes);
        QVERIFY(h.owner->retryPersistence());
        QCOMPARE(h.platform.created_names.size(), std::size_t{1});
        QCOMPARE(h.platform.removed_names.size(), during_close ? std::size_t{1} : std::size_t{0});
        QCOMPARE(h.owner->state(), during_close ? State::Removed : State::Ready);
        QCOMPARE(std::count_if(h.store.writes.begin(), h.store.writes.end(),
                               [&](const auto& record) { return record.stage == failing; }),
                 2);
        if (!during_close)
            QVERIFY(h.owner->close());
    }
    void unknownCreationIsNeverAdoptedRetriedOrRemoved_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("already-exists") << 0;
        QTest::newRow("failed") << 1;
        QTest::newRow("exception") << 2;
        QTest::newRow("invalid-sid") << 3;
    }
    void unknownCreationIsNeverAdoptedRetriedOrRemoved() {
        QFETCH(int, kind);
        Harness h;
        if (kind == 0)
            h.platform.creation.status = WindowsMediaProfileCreateStatus::AlreadyExists;
        if (kind == 1)
            h.platform.creation.status = WindowsMediaProfileCreateStatus::Failed;
        if (kind == 2)
            h.platform.throw_create = true;
        if (kind == 3)
            h.platform.creation.sid = QByteArray{"not a SID"};
        QVERIFY(!h.owner->prepare());
        QCOMPARE(h.owner->state(), State::CreationUnconfirmed);
        QCOMPARE(h.store.stored->stage, Stage::Planned);
        QVERIFY(!h.owner->capability());
        QVERIFY(!h.owner->canRetryPersistence());
        QVERIFY(!h.owner->prepare());
        QVERIFY(!h.owner->retryPersistence());
        QVERIFY(!h.owner->close());
        h.owner.reset();
        QCOMPARE(h.platform.created_names.size(), std::size_t{1});
        QVERIFY(h.platform.removed_names.empty());
        QCOMPARE(h.store.writes.size(), std::size_t{1});
    }
    void cleanupWaitsForExplicitNativeAcknowledgementRatherThanReferences() {
        Harness h;
        QVERIFY(h.owner->prepare());
        const auto cap = h.owner->capability();
        auto first = cap->nativeUseFence()->begin();
        QVERIFY(first);
        QVERIFY(first->acknowledgeNativeStopped());
        auto second = cap->nativeUseFence()->begin();
        QVERIFY(second);
        first.reset();
        QVERIFY(!h.owner->close());
        QCOMPARE(h.owner->state(), State::Draining);
        QCOMPARE(h.store.stored->stage, Stage::InUse);
        QVERIFY(h.platform.removed_names.empty());
        QVERIFY(!cap->nativeUseFence()->begin());
        QVERIFY(second->acknowledgeNoChild());
        QVERIFY(h.owner->close());
        QCOMPARE(h.owner->state(), State::Removed);
        // Keeping the capability alive does not pretend native work is active.
        QCOMPARE(cap->sidBytes(), testSid());
    }
    void abandoningANativeTicketPermanentlyPreservesTheProfile() {
        Harness h;
        QVERIFY(h.owner->prepare());
        const auto cap = h.owner->capability();
        auto use = cap->nativeUseFence()->begin();
        QVERIFY(use);
        use.reset();
        QVERIFY(!h.owner->close());
        QCOMPARE(h.owner->state(), State::NativeUseUnconfirmed);
        QCOMPARE(h.store.stored->stage, Stage::InUse);
        QVERIFY(!h.owner->retryPersistence());
        h.owner.reset();
        QVERIFY(h.platform.removed_names.empty());
    }
    void closeDuringPlannedSaveAbandonsOnlyBeforeAnyCreateAttempt() {
        Harness h;
        h.store.on_record = [&](const auto& record) {
            if (record.stage == Stage::Planned)
                QVERIFY(!h.owner->close());
        };
        QVERIFY(!h.owner->prepare());
        QCOMPARE(h.owner->state(), State::Abandoned);
        QCOMPARE(h.store.stored->stage, Stage::Abandoned);
        QVERIFY(!h.store.stored->sid);
        QVERIFY(h.platform.created_names.empty());
        QVERIFY(h.platform.removed_names.empty());
        QCOMPARE(h.store.writes.size(), std::size_t{2});
    }
    void abandonedWriteFailureRequiresRetryAndNeverCallsTheProfileApi_data() {
        QTest::addColumn<bool>("after");
        QTest::newRow("before") << false;
        QTest::newRow("after") << true;
    }
    void abandonedWriteFailureRequiresRetryAndNeverCallsTheProfileApi() {
        QFETCH(bool, after);
        Harness h;
        h.store.failing_stage = Stage::Abandoned;
        h.store.failures_left = 1;
        h.store.after_commit = after;
        h.store.on_record = [&](const auto& record) {
            if (record.stage == Stage::Planned)
                QVERIFY(!h.owner->close());
        };
        QVERIFY(!h.owner->prepare());
        QCOMPARE(h.owner->state(), State::PersistenceFailed);
        QVERIFY(h.owner->canRetryPersistence());
        QVERIFY(h.owner->retryPersistence());
        QCOMPARE(h.owner->state(), State::Abandoned);
        QCOMPARE(h.store.stored->stage, Stage::Abandoned);
        QVERIFY(h.platform.created_names.empty() && h.platform.removed_names.empty());
    }
    void closeDuringCreatedSavePersistsCreationBeforeCleanupWithoutIssuingUse() {
        Harness h;
        h.store.on_record = [&](const auto& record) {
            if (record.stage == Stage::Created)
                QVERIFY(!h.owner->close());
        };
        QVERIFY(!h.owner->prepare());
        QCOMPARE(h.owner->state(), State::Removed);
        QVERIFY((h.events == std::vector<std::string>{"claim", "load", "planned", "create-api", "created",
                                                      "cleanup-pending", "remove-api", "removed", "release-lease"}));
    }
    void closeAdmissionPrecedesCleanupPersistenceAndTheRemovalPort() {
        Harness h;
        QVERIFY(h.owner->prepare());
        const auto cap = h.owner->capability();
        bool saved_gate = false, remove_gate = false;
        h.store.on_record = [&](const auto& record) {
            if (record.stage == Stage::CleanupPending) {
                saved_gate = true;
                QVERIFY(!cap->nativeUseFence()->begin());
            }
        };
        h.platform.on_remove = [&] {
            remove_gate = true;
            QVERIFY(!cap->nativeUseFence()->begin());
            QVERIFY(!h.owner->capability());
        };
        QVERIFY(h.owner->close());
        QVERIFY(saved_gate && remove_gate);
    }
    void persistedActiveEvidenceNeverReconstructsLiveAuthority_data() {
        QTest::addColumn<int>("stage");
        for (const auto stage : {Stage::Planned, Stage::Created, Stage::InUse, Stage::CleanupPending})
            QTest::newRow(label(stage)) << static_cast<int>(stage);
    }
    void anExclusiveLeaseStopsASecondOwnerBeforeTheSameOperationCanCreate() {
        Harness h;
        WindowsMediaProfileOwner second(h.store, h.platform, {"job-1", [] { return first_id; }});
        h.store.on_record = [&](const auto& record) {
            if (record.stage == Stage::Planned) {
                QVERIFY(!second.prepare());
                QCOMPARE(second.state(), State::BlockedHistory);
                QCOMPARE(h.store.loads, 1);
            }
        };
        QVERIFY(h.owner->prepare());
        QCOMPARE(h.platform.created_names.size(), std::size_t{1});
        QVERIFY(h.owner->close());
        QVERIFY(!h.store.leased);
        WindowsMediaProfileOwner fresh(h.store, h.platform, {"job-2", [] { return second_id; }});
        // The old callback was only testing the first owner's reservation.
        h.store.on_record = {};
        QVERIFY(fresh.prepare());
        QCOMPARE(h.platform.created_names.size(), std::size_t{2});
        QVERIFY(fresh.close());
    }
    void persistedActiveEvidenceNeverReconstructsLiveAuthority() {
        QFETCH(int, stage);
        Harness h;
        h.store.stored = evidence(static_cast<Stage>(stage));
        QVERIFY(!h.owner->prepare());
        QCOMPARE(h.owner->state(), State::BlockedHistory);
        QVERIFY(!h.owner->close());
        QVERIFY(h.store.writes.empty());
        QVERIFY(h.platform.created_names.empty() && h.platform.removed_names.empty());
        QCOMPARE(h.identities, 0);
    }
    void terminalHistoryAllowsOnlyANewOperationIdentity_data() {
        QTest::addColumn<int>("stage");
        QTest::addColumn<bool>("fresh");
        for (const auto stage : {Stage::Removed, Stage::Abandoned})
            for (const bool fresh : {false, true})
                QTest::newRow((QByteArray(label(stage)) + (fresh ? "-fresh" : "-same")).constData())
                    << static_cast<int>(stage) << fresh;
    }
    void terminalHistoryAllowsOnlyANewOperationIdentity() {
        QFETCH(int, stage);
        QFETCH(bool, fresh);
        Harness h;
        h.store.stored = evidence(static_cast<Stage>(stage));
        if (fresh)
            h.operation = second_id;
        QCOMPARE(h.owner->prepare(), fresh);
        QCOMPARE(h.platform.created_names.size(), fresh ? std::size_t{1} : std::size_t{0});
        if (fresh)
            QVERIFY(h.owner->close());
        else
            QVERIFY(h.store.writes.empty());
    }
    void apiSuccessOrAbsenceAloneDoesNotConfirmRemoval_data() {
        QTest::addColumn<bool>("api");
        QTest::addColumn<int>("absence");
        QTest::newRow("unknown") << true << static_cast<int>(WindowsMediaProfileAbsence::Unknown);
        QTest::newRow("still-present") << true << static_cast<int>(WindowsMediaProfileAbsence::Present);
        QTest::newRow("api-failed-but-absent") << false << static_cast<int>(WindowsMediaProfileAbsence::Absent);
    }
    void apiSuccessOrAbsenceAloneDoesNotConfirmRemoval() {
        QFETCH(bool, api);
        QFETCH(int, absence);
        Harness h;
        h.platform.removal = {api, static_cast<WindowsMediaProfileAbsence>(absence)};
        QVERIFY(h.owner->prepare());
        QVERIFY(!h.owner->close());
        QCOMPARE(h.owner->state(), State::RemovalUnconfirmed);
        QCOMPARE(h.store.stored->stage, Stage::CleanupPending);
        QVERIFY(!h.owner->close());
        QVERIFY(!h.owner->retryPersistence());
        QCOMPARE(h.platform.removed_names.size(), std::size_t{1});
    }
    void removalExceptionPreservesUncertaintyWithoutRetryingTheSystemCall() {
        Harness h;
        h.platform.on_remove = [] { throw std::runtime_error("uncertain removal"); };
        QVERIFY(h.owner->prepare());
        QVERIFY(!h.owner->close());
        QCOMPARE(h.owner->state(), State::RemovalUnconfirmed);
        QVERIFY(!h.owner->close());
        QVERIFY(!h.owner->retryPersistence());
        QCOMPARE(h.platform.removed_names.size(), std::size_t{1});
        QCOMPARE(h.store.stored->stage, Stage::CleanupPending);
    }
    void blockedWritesAndReentrantCallsNeverDuplicateOrExposeAuthority() {
        Harness h;
        h.store.on_load = [&] {
            QVERIFY(!h.owner->prepare());
            QVERIFY(!h.owner->retryPersistence());
            QVERIFY(!h.owner->capability());
        };
        h.store.on_record = [&](const auto&) {
            QVERIFY(!h.owner->prepare());
            QVERIFY(!h.owner->retryPersistence());
            QVERIFY(!h.owner->capability());
        };
        h.platform.on_create = [&] {
            QVERIFY(!h.owner->prepare());
            QVERIFY(!h.owner->capability());
        };
        QVERIFY(h.owner->prepare());
        QCOMPARE(h.platform.created_names.size(), std::size_t{1});
        h.store.blocked = true;
        QVERIFY(!h.owner->close());
        QCOMPARE(h.owner->state(), State::BlockedHistory);
        QVERIFY(!h.owner->canRetryPersistence());
        QVERIFY(h.platform.removed_names.empty());
    }
    void invalidStartupAndConfigurationDoNotCreateOrRewrite_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 7; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void invalidStartupAndConfigurationDoNotCreateOrRewrite() {
        QFETCH(int, kind);
        Harness h;
        if (kind == 0)
            h.store.forced_load = DownloadMediaProfileLoadResult{DownloadCheckpointLoadStatus::Invalid, {}};
        if (kind == 1)
            h.store.forced_load = DownloadMediaProfileLoadResult{DownloadCheckpointLoadStatus::FutureVersion, {}};
        if (kind == 2)
            h.store.forced_load = DownloadMediaProfileLoadResult{DownloadCheckpointLoadStatus::Unreadable, {}};
        if (kind == 3)
            h.store.forced_load = DownloadMediaProfileLoadResult{DownloadCheckpointLoadStatus::Loaded, {}};
        if (kind == 4)
            h.store.forced_load =
                DownloadMediaProfileLoadResult{DownloadCheckpointLoadStatus::Missing, evidence(Stage::Planned)};
        if (kind == 5)
            h.operation = "../external-profile";
        if (kind == 6)
            h.owner = std::make_unique<WindowsMediaProfileOwner>(h.store, h.platform,
                                                                 WindowsMediaProfileOwnerConfig{"bad/job", {}});
        QVERIFY(!h.owner->prepare());
        QVERIFY(!h.owner->capability());
        QVERIFY(h.store.writes.empty());
        QVERIFY(h.platform.created_names.empty() && h.platform.removed_names.empty());
    }
    void aRealPostCommitFailureStaysFailedUntilANewWriteAndBlocksRestartedOwnership() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto directory = QDir(temporary.path()).filePath(QStringLiteral("private"));
        // Journals require exact non-inheriting ACEs. The media workspace
        // helper deliberately has inheriting ACEs and is a different policy.
        QVERIFY(createJournalDirectory(directory));
        std::vector<std::string> events;
        Platform platform(events);
        bool faulted = false;
        WindowsDownloadMediaProfileStore store(directory, {[&](const auto point) {
                                                   if (point == WindowsPrivateDownloadFaultPoint::AfterCommit &&
                                                       !faulted && !platform.created_names.empty()) {
                                                       faulted = true;
                                                       return true;
                                                   }
                                                   return false;
                                               }});
        QCOMPARE(store.load().status, DownloadCheckpointLoadStatus::Missing);
        WindowsMediaProfileOwner owner(store, platform, {"job-1", [] { return first_id; }});
        QVERIFY(!owner.prepare());
        QCOMPARE(owner.state(), State::PersistenceFailed);
        QVERIFY(!owner.capability());
        const auto readable = store.load();
        QCOMPARE(readable.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(readable.record);
        QCOMPARE(readable.record->stage, Stage::Created);
        WindowsMediaProfileOwner restarted(store, platform, {"job-2", [] { return second_id; }});
        QVERIFY(!restarted.prepare());
        QCOMPARE(restarted.state(), State::BlockedHistory);
        QCOMPARE(platform.created_names.size(), std::size_t{1});
        QVERIFY(platform.removed_names.empty());
        QVERIFY(owner.canRetryPersistence());
        QVERIFY(owner.retryPersistence());
        QCOMPARE(owner.state(), State::Ready);
        QCOMPARE(platform.created_names.size(), std::size_t{1});
        QVERIFY(owner.close());
        QCOMPARE(store.load().record->stage, Stage::Removed);
        QCOMPARE(platform.removed_names.size(), std::size_t{1});
    }
};

QTEST_GUILESS_MAIN(WindowsMediaProfileOwnerTests)
#include "WindowsMediaProfileOwnerTests.moc"

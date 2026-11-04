#include "../windows/sandbox/AppContainerStorageInventory.hpp"

#include <QScopeGuard>
#include <QTest>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

using Inventory = music_player::sandbox_probe::AppContainerStorageInventory;
namespace {
QByteArray owner() { return QByteArray::fromHex("0102000000000005150000002a000000"); }
Inventory::Metadata metadata(std::uint64_t ref, Inventory::Type type) {
    Inventory::Metadata value;
    value.ref = {ref};
    value.identity = {type == Inventory::Type::RegistryKey ? 2ULL : 1ULL, ref};
    value.presence = Inventory::Presence::Present;
    value.type = type;
    value.error = 0;
    value.link_count = 1;
    value.owner_sid = owner();
    value.raw_sd = "fake"; // Opaque trusted-port fixture, not a Win32 SD or native authorization.
    value.dacl = Inventory::Dacl::Present;
    value.dacl_protected = true;
    return value;
}
struct Data final {
    std::uint32_t begins{}, roots{}, reads{}, children{}, validations{}, closes{}, destroyed{};
    Inventory::Provenance provenance{Inventory::Provenance::LiveCreated};
    std::uint32_t begin_error{};
    std::array<Inventory::RootResult, 4> root_results{};
    std::map<std::uint64_t, Inventory::Metadata> objects{};
    std::map<std::uint64_t, std::vector<Inventory::ChildResult>> child_lists{};
    std::map<std::uint64_t, unsigned> read_counts{};
    std::vector<std::uint64_t> budgets{};
    std::vector<std::uint32_t> name_budgets{};
    Inventory::ValidationResult validation{Inventory::Validation::Unchanged, 0};
    Inventory::CloseResult closure{Inventory::CloseState::Closed, 0};
    std::optional<Inventory::ChildResult> child_override{};
    QString throw_on{};
    int change_second_ac{};
    std::function<void()> before_second_ac{};
    std::function<void()> during_revalidate{};
    bool no_session{false};
};
class Session final : public Inventory::ReadSession {
  public:
    explicit Session(std::shared_ptr<Data> value) : data(std::move(value)) {}
    ~Session() override { ++data->destroyed; }
    Inventory::RootResult root(Inventory::Root root) override {
        ++data->roots;
        throws("root");
        return data->root_results[static_cast<std::size_t>(root)];
    }
    Inventory::Metadata readMetadata(Inventory::NodeRef ref, std::uint64_t cap) override {
        ++data->reads;
        data->budgets.push_back(cap);
        throws("metadata");
        auto value = data->objects.at(ref.value);
        if (++data->read_counts[ref.value] > 1 && ref.value == 2) {
            if (data->before_second_ac)
                data->before_second_ac();
            if (data->change_second_ac == 1)
                ++value.identity.object;
            if (data->change_second_ac == 2)
                value.raw_sd = "else";
            if (data->change_second_ac == 3)
                value.owner_sid.back() ^= 1;
            if (data->change_second_ac == 4)
                value.type = Inventory::Type::File;
        }
        return value;
    }
    Inventory::ChildResult nextChild(Inventory::NodeRef parent, std::uint32_t index, std::uint32_t cap) override {
        ++data->children;
        data->name_budgets.push_back(cap);
        throws("child");
        if (data->child_override)
            return *data->child_override;
        const auto& values = data->child_lists[parent.value];
        return index < values.size() ? values[index] : Inventory::ChildResult{Inventory::ChildState::End, 0, {}, {}};
    }
    Inventory::ValidationResult revalidate() override {
        // A broken lease may destroy this Session from inside the callback.
        // Keep the fake's data locally so the Red never dereferences freed this.
        const auto held = data;
        ++held->validations;
        if (held->throw_on == "revalidate")
            throw std::runtime_error("injected read-only port failure");
        if (held->during_revalidate)
            held->during_revalidate();
        return held->validation;
    }
    Inventory::CloseResult close() override {
        ++data->closes;
        throws("close");
        return data->closure;
    }

  private:
    void throws(const QString& operation) {
        if (data->throw_on == operation)
            throw std::runtime_error("injected read-only port failure");
    }
    std::shared_ptr<Data> data;
};
class Source final : public Inventory::Source {
  public:
    std::shared_ptr<Data> data{std::make_shared<Data>()};
    Source() {
        for (std::uint64_t ref = 1; ref <= 4; ++ref) {
            data->root_results[ref - 1] = {{ref}, 0, Inventory::Presence::Present};
            data->objects[ref] = metadata(ref, ref < 3 ? Inventory::Type::Directory : Inventory::Type::RegistryKey);
        }
        child(1, "AC", 2);
    }
    Inventory::BeginResult begin() override {
        ++data->begins;
        if (data->throw_on == "begin")
            throw std::runtime_error("injected source failure");
        return {data->provenance, data->begin_error, data->no_session ? nullptr : std::make_unique<Session>(data)};
    }
    void child(std::uint64_t parent, QString name, std::uint64_t ref) {
        data->child_lists[parent].push_back({Inventory::ChildState::Child, 0, std::move(name), {ref}});
    }
    void file(std::uint64_t ref, QString name, std::uint64_t parent = 1) {
        data->objects[ref] = metadata(ref, Inventory::Type::File);
        child(parent, std::move(name), ref);
    }
};
void release(Inventory::Result& value) {
    if (value.lease)
        static_cast<void>(value.lease->close());
}
} // namespace

class AppContainerStorageInventoryTests final : public QObject {
    Q_OBJECT
  private slots:
    void fourFixedRootsAndTheExpectedAcOverlapAreCountedOnlyOnce() {
        Source source;
        QCOMPARE(source.data->begins, 0U);
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        QCOMPARE(result.report.provenance, Inventory::Provenance::LiveCreated);
        QCOMPARE(result.report.reason, Inventory::Reason::None);
        QCOMPARE(result.report.objects.size(), std::size_t{4});
        QCOMPARE(result.report.total_bytes, std::uint64_t{84}); // 4*(16 SID+4 SD)+two UTF-16 units "AC".
        for (std::size_t index = 0; index < 4; ++index) {
            const auto& root = result.report.roots[index];
            QCOMPARE(root.root, static_cast<Inventory::Root>(index));
            QCOMPARE(root.presence, Inventory::Presence::Present);
            QVERIFY(root.object_index && *root.object_index < result.report.objects.size());
            const auto& object = result.report.objects[*root.object_index];
            QVERIFY(object.metadata.owner_sid == owner());
            QCOMPARE(object.metadata.raw_sd, QByteArray{"fake"});
            QVERIFY(object.metadata.sacl_not_queried && object.metadata.dacl_protected);
        }
        const auto& ac = result.report.objects[*result.report.roots[1].object_index];
        QCOMPARE(ac.parent.value, std::uint64_t{1});
        QCOMPARE(ac.name, QString{"AC"});
        QCOMPARE(ac.depth, 1U);
        QCOMPARE(result.report.validation.state, Inventory::Validation::Unchanged);
        QCOMPARE(source.data->begins, 1U);
        QCOMPARE(source.data->closes, 0U);
        release(result);
    }
    void acMayUseAnotherReferenceToTheSameCapturedObject() {
        Source source;
        source.data->root_results[1].ref = {20};
        source.data->objects[20] = source.data->objects[2];
        source.data->objects[20].ref = {20};
        source.data->child_lists[1][0].name = "ac";
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        QCOMPARE(result.report.objects.size(), std::size_t{4});
        release(result);
    }
    void loadedRevokedAndUnknownSourcesNeverReadObjects_data() {
        QTest::addColumn<int>("kind");
        for (int kind : {0, 2, 3, 99})
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void loadedRevokedAndUnknownSourcesNeverReadObjects() {
        QFETCH(int, kind);
        Source source;
        source.data->provenance = static_cast<Inventory::Provenance>(kind);
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QCOMPARE(result.report.reason, Inventory::Reason::Provenance);
        QCOMPARE(source.data->begins, 1U);
        QCOMPARE(source.data->roots, 0U);
        QCOMPARE(source.data->reads, 0U);
        release(result);
    }
    void failedOrMissingReadSessionsDoNotFallBackToAnotherSource_data() {
        QTest::addColumn<bool>("missingSession");
        QTest::newRow("raw-failure") << false;
        QTest::newRow("missing-session") << true;
    }
    void failedOrMissingReadSessionsDoNotFallBackToAnotherSource() {
        QFETCH(bool, missingSession);
        Source source;
        source.data->no_session = missingSession;
        source.data->begin_error = missingSession ? 0U : 5U;
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QCOMPARE(result.report.reason, Inventory::Reason::Source);
        QCOMPARE(result.report.begin_error, source.data->begin_error);
        QCOMPARE(source.data->begins, 1U);
        QCOMPARE(source.data->roots, 0U);
        QCOMPARE(source.data->reads, 0U);
        release(result);
    }
    void invalidLimitsAreRejectedBeforeBeginningASource_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 8; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidLimitsAreRejectedBeforeBeginningASource() {
        QFETCH(int, kind);
        Source source;
        Inventory::Limits limits;
        if (kind == 0)
            limits.objects = 0;
        if (kind == 1)
            limits.objects = Inventory::maximum_objects + 1;
        if (kind == 2)
            limits.depth = 0;
        if (kind == 3)
            limits.depth = Inventory::maximum_depth + 1;
        if (kind == 4)
            limits.sd_bytes = 0;
        if (kind == 5)
            limits.sd_bytes = Inventory::maximum_sd_bytes + 1;
        if (kind == 6)
            limits.total_bytes = 0;
        if (kind == 7)
            limits.total_bytes = std::numeric_limits<std::uint64_t>::max();
        const auto result = Inventory::inspect(source, limits);
        QCOMPARE(result.report.reason, Inventory::Reason::Limits);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QVERIFY(!result.lease);
        QCOMPARE(source.data->begins, 0U);
    }
    void missingUnknownOrUnreadableRootsPreserveAPartialManifest_data() {
        QTest::addColumn<int>("root");
        QTest::addColumn<bool>("missing");
        for (int root = 0; root < 4; ++root)
            for (bool missing : {false, true})
                QTest::newRow(qPrintable(QString::number(root) + "-" + QString::number(missing))) << root << missing;
    }
    void missingUnknownOrUnreadableRootsPreserveAPartialManifest() {
        QFETCH(int, root);
        QFETCH(bool, missing);
        Source source;
        auto& slot = source.data->root_results[static_cast<std::size_t>(root)];
        slot.presence = missing ? Inventory::Presence::Missing : Inventory::Presence::Unknown;
        slot.error = missing ? 2U : 5U;
        slot.ref = {};
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        const auto& observed = result.report.roots[static_cast<std::size_t>(root)];
        QCOMPARE(observed.presence, slot.presence);
        QCOMPARE(observed.result.error, slot.error);
        QCOMPARE(source.data->closes, 0U);
        release(result);
    }
    void nullAbsentAndPresentDaclsAreDistinctObservedData_data() {
        QTest::addColumn<int>("dacl");
        for (int i : {1, 2, 3})
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void nullAbsentAndPresentDaclsAreDistinctObservedData() {
        QFETCH(int, dacl);
        Source source;
        source.data->objects[4].dacl = static_cast<Inventory::Dacl>(dacl);
        source.data->objects[4].dacl_protected = false;
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        const auto& observed = result.report.objects[*result.report.roots[3].object_index].metadata;
        QCOMPARE(observed.dacl, static_cast<Inventory::Dacl>(dacl));
        QVERIFY(!observed.dacl_protected);
        release(result);
    }
    void unsafeMetadataNeverClaimsCompleteInventory_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 19; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void unsafeMetadataNeverClaimsCompleteInventory() {
        QFETCH(int, kind);
        Source source;
        auto& node = source.data->objects[4];
        if (kind == 0)
            node.error = 5;
        if (kind == 1)
            node.presence = Inventory::Presence::Unknown;
        if (kind == 2)
            node.presence = Inventory::Presence::Missing;
        if (kind == 3)
            node.type = Inventory::Type::Unknown;
        if (kind == 4)
            node.type = Inventory::Type::Directory;
        if (kind == 5)
            node.ref = {44};
        if (kind == 6)
            node.identity = {};
        if (kind == 7)
            node.registry_link = true;
        if (kind == 8)
            node.owner_sid.clear();
        if (kind == 9)
            node.owner_sid[0] = 2;
        if (kind == 10)
            node.dacl = Inventory::Dacl::Unknown;
        if (kind == 11)
            node.raw_sd.clear();
        if (kind == 12)
            node.sacl_not_queried = false;
        if (kind == 13)
            source.data->objects[1].reparse = true;
        if (kind == 14)
            source.data->objects[1].reparse_tag = 0xa0000003;
        if (kind == 15)
            source.data->objects[1].link_count = 2;
        if (kind == 16)
            node.dacl = static_cast<Inventory::Dacl>(99);
        if (kind == 17)
            node.type = static_cast<Inventory::Type>(99);
        if (kind == 18)
            node.presence = static_cast<Inventory::Presence>(99);
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QVERIFY(result.report.reason != Inventory::Reason::None);
        release(result);
    }
    void unsafeChildNamesAreRejectedBeforeReadingTheirMetadata_data() {
        QTest::addColumn<QString>("name");
        unsigned index = 0;
        for (const auto& name : QStringList{"", ".", "..", "a/b", "a\\b", "C:", "x:y", "*", "?", "NUL", "CON.txt",
                                            "COM1", QString(256, 'x'), QString{"a"} + QChar{0}, QString{QChar{0xd800}}})
            QTest::newRow(qPrintable(QString::number(index++))) << name;
    }
    void unsafeChildNamesAreRejectedBeforeReadingTheirMetadata() {
        QFETCH(QString, name);
        Source source;
        source.file(5, name);
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QCOMPARE(result.report.reason, Inventory::Reason::Name);
        QCOMPARE(source.data->read_counts[5], 0U);
        release(result);
    }
    void duplicateCyclesAliasesAndAcDivergenceAreRefused_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 11; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void duplicateCyclesAliasesAndAcDivergenceAreRefused() {
        QFETCH(int, kind);
        Source source;
        if (kind == 0)
            source.child(1, "AC", 2);
        if (kind == 1)
            source.child(2, "cycle", 1);
        if (kind == 2)
            source.child(1, "alias", 2);
        if (kind == 3)
            source.data->root_results[3].ref = {3};
        if (kind == 4)
            source.data->child_lists[1].clear();
        if (kind == 5)
            source.data->child_lists[1][0].name = "other";
        if (kind >= 6 && kind < 10)
            source.data->change_second_ac = kind - 5;
        if (kind == 10) {
            source.data->objects[5] = source.data->objects[2];
            source.data->objects[5].ref = {5};
            source.child(1, "other", 5);
        }
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QVERIFY(result.report.reason != Inventory::Reason::None);
        release(result);
    }
    void unknownOrContradictoryChildResponsesCannotEndATreeSuccessfully_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void unknownOrContradictoryChildResponsesCannotEndATreeSuccessfully() {
        QFETCH(int, kind);
        Source source;
        Inventory::ChildResult child{Inventory::ChildState::Unknown, 5, {}, {}};
        if (kind == 1)
            child = {Inventory::ChildState::End, 5, {}, {}};
        if (kind == 2)
            child = {Inventory::ChildState::Child, 0, "leaf", {}};
        if (kind == 3)
            child = {Inventory::ChildState::End, 0, "leaf", {5}};
        if (kind == 4)
            child = {static_cast<Inventory::ChildState>(99), 0, {}, {}};
        source.data->child_override = child;
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QVERIFY(result.report.reason != Inventory::Reason::None);
        release(result);
    }
    void allInclusiveBoundsAreEnforced_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("over");
        for (int kind = 0; kind < 4; ++kind)
            for (bool over : {false, true})
                QTest::newRow(qPrintable(QString::number(kind) + "-" + QString::number(over))) << kind << over;
    }
    void allInclusiveBoundsAreEnforced() {
        QFETCH(int, kind);
        QFETCH(bool, over);
        Source source;
        Inventory::Limits limits;
        if (kind == 0) {
            for (std::uint64_t ref = 5; ref <= 512 + std::uint64_t{over}; ++ref)
                source.file(ref, QString::number(ref));
        }
        if (kind == 1) {
            for (std::uint64_t depth = 2; depth <= 16 + std::uint64_t{over}; ++depth) {
                const auto ref = depth + 3;
                source.data->objects[ref] = metadata(
                    ref, depth == 16 + std::uint64_t{over} ? Inventory::Type::File : Inventory::Type::Directory);
                source.child(depth == 2 ? 2 : ref - 1, "child", ref);
            }
        }
        if (kind == 2)
            source.data->objects[4].raw_sd = QByteArray(static_cast<qsizetype>(65536 + int{over}), 'x');
        if (kind == 3) {
            for (std::uint64_t index = 0; index < 64; ++index) {
                const auto ref = index + 5;
                source.file(ref, QString{"n%1"}.arg(index, 3, 10, QLatin1Char{'0'}));
                // Four roots cost84; 64 files cost16-byteSID+8-byteUTF16name each.
                const auto size = index == 63 ? 65536 - 1620 + int{over} : 65536;
                source.data->objects[ref].raw_sd = QByteArray(size, 'x');
            }
        }
        auto result = Inventory::inspect(source, limits);
        QCOMPARE(result.report.status, over ? Inventory::Status::Incomplete : Inventory::Status::Observed);
        if (over)
            QCOMPARE(result.report.reason, Inventory::Reason::Bounds);
        if (!over && kind == 0)
            QCOMPARE(result.report.objects.size(), std::size_t{512});
        if (!over && kind == 3)
            QCOMPARE(result.report.total_bytes, Inventory::maximum_total_bytes);
        QVERIFY(std::all_of(source.data->budgets.begin(), source.data->budgets.end(), [](auto cap) {
            return cap <= Inventory::maximum_sd_bytes + 68 && cap <= Inventory::maximum_total_bytes;
        }));
        QVERIFY(std::all_of(source.data->name_budgets.begin(), source.data->name_budgets.end(),
                            [](auto cap) { return cap <= Inventory::maximum_name_units; }));
        release(result);
    }
    void changedOrUnknownFinalValidationPreservesThePartialReport_data() {
        QTest::addColumn<bool>("changed");
        QTest::newRow("changed") << true;
        QTest::newRow("unknown") << false;
    }
    void changedOrUnknownFinalValidationPreservesThePartialReport() {
        QFETCH(bool, changed);
        Source source;
        source.data->validation = {changed ? Inventory::Validation::Changed : Inventory::Validation::Unknown,
                                   changed ? 0U : 5U};
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QCOMPARE(result.report.reason, Inventory::Reason::Changed);
        QCOMPARE(result.report.validation.state, source.data->validation.state);
        QVERIFY(!result.report.objects.empty());
        QVERIFY(result.lease);
        release(result);
    }
    void portFailuresRemainIncompleteAndNeverTriggerImplicitClose_data() {
        QTest::addColumn<QString>("operation");
        for (const auto& operation : QStringList{"begin", "root", "metadata", "child", "revalidate"})
            QTest::newRow(qPrintable(operation)) << operation;
    }
    void portFailuresRemainIncompleteAndNeverTriggerImplicitClose() {
        QFETCH(QString, operation);
        Source source;
        source.data->throw_on = operation;
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QCOMPARE(result.report.reason, Inventory::Reason::Exception);
        QCOMPARE(source.data->closes, 0U);
        release(result);
    }
    void rejectedOversizedRootMetadataStillRecordsTheReturnedErrorAndPresence() {
        Source source;
        source.data->objects[4].raw_sd = QByteArray(static_cast<qsizetype>(Inventory::maximum_sd_bytes + 1), 'x');
        source.data->objects[4].error = 5;
        source.data->objects[4].presence = Inventory::Presence::Unknown;
        auto result = Inventory::inspect(source);
        const auto cleanup = qScopeGuard([&] { release(result); });
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QCOMPARE(result.report.reason, Inventory::Reason::Bounds);
        const auto& root = result.report.roots[3];
        QCOMPARE(root.result.error, 0U);
        QCOMPARE(root.metadata_error, 5U);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(!root.object_index);
    }
    void reentrantValidationReturnsBusyWithoutCallingThePortAgain() {
        Source source;
        auto result = Inventory::inspect(source);
        QVERIFY(result.lease);
        const auto cleanup = qScopeGuard([&] { release(result); });
        const auto before = source.data->validations;
        bool entered = false;
        Inventory::ValidationResult nested;
        source.data->during_revalidate = [&] {
            if (entered)
                return;
            entered = true;
            nested = result.lease->revalidate();
        };
        const auto outer = result.lease->revalidate();
        source.data->during_revalidate = {};
        QVERIFY(entered);
        QCOMPARE(nested.state, Inventory::Validation::Unknown);
        QCOMPARE(nested.error, 170U); // ERROR_BUSY is a refusal, not a second native read.
        QCOMPARE(outer.state, Inventory::Validation::Unchanged);
        QCOMPARE(outer.error, 0U);
        QCOMPARE(source.data->validations, before + 1);
        QCOMPARE(source.data->closes, 0U);
        QCOMPARE(source.data->destroyed, 0U);
    }
    void closeDuringValidationIsBusyAndCannotDestroyTheActiveSession() {
        Source source;
        auto result = Inventory::inspect(source);
        QVERIFY(result.lease);
        const auto cleanup = qScopeGuard([&] { release(result); });
        Inventory::CloseResult nested;
        unsigned closes_during = 0, destroyed_during = 0;
        source.data->during_revalidate = [&] {
            nested = result.lease->close();
            closes_during = source.data->closes;
            destroyed_during = source.data->destroyed;
        };
        const auto outer = result.lease->revalidate();
        source.data->during_revalidate = {};
        QCOMPARE(nested.state, Inventory::CloseState::NotAttempted);
        QCOMPARE(nested.error, 170U);
        QCOMPARE(closes_during, 0U);
        QCOMPARE(destroyed_during, 0U);
        QCOMPARE(outer.state, Inventory::Validation::Unchanged);
        QCOMPARE(outer.error, 0U);
        QCOMPARE(result.lease->closeResult().state, Inventory::CloseState::NotAttempted);
        const auto closed = result.lease->close();
        QCOMPARE(closed.state, Inventory::CloseState::Closed);
        QCOMPARE(closed.error, 0U);
        QCOMPARE(source.data->closes, 1U);
        QCOMPARE(source.data->destroyed, 1U);
    }
    void successfulCloseIsExplicitOneShotAndReleasesOnlyTheReadSession() {
        Source source;
        auto data = source.data;
        auto result = Inventory::inspect(source);
        QVERIFY(result.lease);
        QCOMPARE(result.lease->closeResult().state, Inventory::CloseState::NotAttempted);
        QCOMPARE(data->closes, 0U);
        QCOMPARE(result.lease->revalidate().state, Inventory::Validation::Unchanged);
        const auto closed = result.lease->close();
        QCOMPARE(closed.state, Inventory::CloseState::Closed);
        QCOMPARE(closed.error, 0U);
        QCOMPARE(data->closes, 1U);
        QCOMPARE(data->destroyed, 1U);
        QCOMPARE(result.lease->close().state, Inventory::CloseState::Closed);
        QCOMPARE(result.lease->revalidate().state, Inventory::Validation::Unknown);
        result.lease.reset();
        QCOMPARE(data->closes, 1U);
        QVERIFY(!result.report.objects.empty());
    }
    void capturedMetadataAndNamesOwnTheirBytesEvenWhenThePortReturnsBorrowedQtViews() {
        Inventory::Result result;
        {
            Source source;
            QByteArray sid_storage = owner(), sd_storage{"fake"};
            QString name_storage{"leaf"};
            source.file(5, QString::fromRawData(name_storage.constData(), name_storage.size()));
            source.data->objects[5].owner_sid = QByteArray::fromRawData(sid_storage.constData(), sid_storage.size());
            source.data->objects[5].raw_sd = QByteArray::fromRawData(sd_storage.constData(), sd_storage.size());
            result = Inventory::inspect(source);
            QCOMPARE(result.report.status, Inventory::Status::Observed);
            const auto found = std::find_if(result.report.objects.begin(), result.report.objects.end(),
                                            [](const auto& object) { return object.name == "leaf"; });
            QVERIFY(found != result.report.objects.end());
            sid_storage.back() = static_cast<char>(sid_storage.back() ^ 1);
            sd_storage[0] = 'X';
            name_storage[0] = QChar{'X'};
            QVERIFY(found->metadata.owner_sid == owner());
            QCOMPARE(found->metadata.raw_sd, QByteArray{"fake"});
            QCOMPARE(found->name, QString{"leaf"});
            release(result);
        }
        const auto found = std::find_if(result.report.objects.begin(), result.report.objects.end(),
                                        [](const auto& object) { return object.name == "leaf"; });
        QVERIFY(found != result.report.objects.end());
        QVERIFY(found->metadata.owner_sid == owner());
        QCOMPARE(found->metadata.raw_sd, QByteArray{"fake"});
    }
    void borrowedAcMetadataCannotRewriteTheEarlierSnapshotBeforeAliasComparison() {
        Source source;
        QByteArray sd_storage{"fake"};
        source.data->objects[2].raw_sd = QByteArray::fromRawData(sd_storage.constData(), sd_storage.size());
        source.data->before_second_ac = [&] { sd_storage[0] = 'X'; };
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Incomplete);
        QVERIFY(result.report.reason != Inventory::Reason::None);
        QVERIFY(source.data->read_counts[2] >= 2);
        release(result);
    }
    void unknownContradictoryOrThrowingCloseRetainsTheSessionWithoutRetry_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 6; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void unknownContradictoryOrThrowingCloseRetainsTheSessionWithoutRetry() {
        QFETCH(int, kind);
        Source source;
        source.data->closure = {kind == 0 ? Inventory::CloseState::Unknown : Inventory::CloseState::Closed, 5};
        if (kind == 2)
            source.data->throw_on = "close";
        if (kind == 3)
            source.data->closure = {Inventory::CloseState::Unknown, 0};
        if (kind == 4)
            source.data->closure = {Inventory::CloseState::NotAttempted, 0};
        if (kind == 5)
            source.data->closure = {static_cast<Inventory::CloseState>(99), 0};
        auto result = Inventory::inspect(source);
        QVERIFY(result.lease);
        const auto closed = result.lease->close();
        QCOMPARE(closed.state, Inventory::CloseState::Unknown);
        if (kind < 3)
            QVERIFY(closed.error != 0);
        else
            QCOMPARE(closed.error, 0U);
        QCOMPARE(result.lease->close().state, Inventory::CloseState::Unknown);
        QCOMPARE(source.data->closes, 1U);
        const auto validation_count = source.data->validations;
        QCOMPARE(result.lease->revalidate().state, Inventory::Validation::Unknown);
        QCOMPARE(source.data->validations, validation_count);
        const std::weak_ptr<Data> lifetime = source.data;
        source.data.reset();
        result.lease.reset();
        const auto retained = lifetime.lock();
        QVERIFY(retained);
        QCOMPARE(retained->closes, 1U);
        QCOMPARE(retained->destroyed, 0U);
    }
    void abandoningALeaseDoesNotInventCloseAcknowledgement() {
        Source source;
        auto result = Inventory::inspect(source);
        QVERIFY(result.lease);
        const std::weak_ptr<Data> lifetime = source.data;
        source.data.reset();
        result.lease.reset();
        const auto retained = lifetime.lock();
        QVERIFY(retained);
        QCOMPARE(retained->closes, 0U);
        QCOMPARE(retained->destroyed, 0U);
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageInventoryTests)
#include "AppContainerStorageInventoryTests.moc"

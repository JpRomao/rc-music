#include "../windows/sandbox/AppContainerStorageSession.hpp"

#include <QTest>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using Session = music_player::sandbox_probe::AppContainerStorageSession;
using Inventory = Session::Inventory;
namespace {
constexpr std::uint32_t invalid_data = 13, busy = 170, more_data = 234;
QByteArray owner() { return QByteArray::fromHex("010100000000000515000000"); }
struct State final {
    bool registry{false};
    unsigned reads{}, children{}, validations{}, closes{}, destroyed{};
    std::vector<Inventory::Root> roots{};
    Inventory::NodeRef last_ref{};
    std::uint64_t last_budget{};
    std::uint32_t last_index{}, last_units{};
    std::array<Inventory::RootResult, 4> root_results{Inventory::RootResult{{1}, 0, Inventory::Presence::Present},
                                                      Inventory::RootResult{{2}, 0, Inventory::Presence::Present},
                                                      Inventory::RootResult{{1}, 0, Inventory::Presence::Present},
                                                      Inventory::RootResult{{2}, 0, Inventory::Presence::Present}};
    Inventory::ValidationResult validation{Inventory::Validation::Unchanged, 0};
    Inventory::CloseResult closure{Inventory::CloseState::Closed, 0};
    std::function<void()> on_root{}, on_read{}, on_child{}, on_validate{}, on_close{};
    std::function<Inventory::Metadata(Inventory::NodeRef)> metadata{};
    std::function<Inventory::ChildResult(Inventory::NodeRef, std::uint32_t)> child{};
    bool throw_root{}, throw_read{}, throw_child{}, throw_validate{}, throw_close{};
};
Inventory::Metadata validMetadata(const State& state, const Inventory::NodeRef ref) {
    return {.ref = ref,
            .identity = {7, ref.value},
            .presence = Inventory::Presence::Present,
            .type = state.registry ? Inventory::Type::RegistryKey : Inventory::Type::Directory,
            .error = 0,
            .link_count = state.registry ? 0U : 1U,
            .owner_sid = owner(),
            .raw_sd = QByteArray{"opaque"},
            .dacl = Inventory::Dacl::Present,
            .dacl_protected = true};
}
struct Port final : Inventory::ReadSession {
    std::shared_ptr<State> state;
    explicit Port(std::shared_ptr<State> value) : state(std::move(value)) {}
    ~Port() override { ++state->destroyed; }
    Inventory::RootResult root(const Inventory::Root value) override {
        const auto held = state;
        held->roots.push_back(value);
        if (held->on_root)
            held->on_root();
        if (held->throw_root)
            throw std::runtime_error{"root"};
        return held->root_results[static_cast<std::size_t>(value)];
    }
    Inventory::Metadata readMetadata(const Inventory::NodeRef ref, const std::uint64_t maximum_bytes) override {
        const auto held = state;
        ++held->reads;
        held->last_ref = ref;
        held->last_budget = maximum_bytes;
        if (held->on_read)
            held->on_read();
        if (held->throw_read)
            throw std::runtime_error{"metadata"};
        return held->metadata ? held->metadata(ref) : validMetadata(*held, ref);
    }
    Inventory::ChildResult nextChild(const Inventory::NodeRef parent, const std::uint32_t index,
                                     const std::uint32_t maximum_units) override {
        const auto held = state;
        ++held->children;
        held->last_ref = parent;
        held->last_index = index;
        held->last_units = maximum_units;
        if (held->on_child)
            held->on_child();
        if (held->throw_child)
            throw std::runtime_error{"children"};
        return held->child ? held->child(parent, index) : Inventory::ChildResult{Inventory::ChildState::End, 0, {}, {}};
    }
    Inventory::ValidationResult revalidate() override {
        const auto held = state;
        ++held->validations;
        if (held->on_validate)
            held->on_validate();
        if (held->throw_validate)
            throw std::runtime_error{"validation"};
        return held->validation;
    }
    Inventory::CloseResult close() override {
        const auto held = state;
        ++held->closes;
        if (held->on_close)
            held->on_close();
        if (held->throw_close)
            throw std::runtime_error{"close"};
        return held->closure;
    }
};
struct Fixture final {
    std::shared_ptr<State> files{std::make_shared<State>()}, registry{std::make_shared<State>()};
    Fixture() { registry->registry = true; }
    std::unique_ptr<Session> make() {
        return std::make_unique<Session>(std::make_unique<Port>(files), std::make_unique<Port>(registry));
    }
};
struct Source final : Inventory::Source {
    std::unique_ptr<Inventory::ReadSession> session;
    explicit Source(std::unique_ptr<Inventory::ReadSession> value) : session(std::move(value)) {}
    Inventory::BeginResult begin() override { return {Inventory::Provenance::LiveCreated, 0, std::move(session)}; }
};
} // namespace

class AppContainerStorageSessionTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndDestructionNeverCallOrAcknowledgeThePorts() {
        Fixture f;
        {
            auto session = f.make();
            QCOMPARE(session->closeResult().state, Inventory::CloseState::NotAttempted);
        }
        QCOMPARE(f.files->roots.size(), std::size_t{});
        QCOMPARE(f.files->reads + f.files->children + f.files->validations + f.files->closes, 0U);
        QCOMPARE(f.registry->reads + f.registry->children + f.registry->validations + f.registry->closes, 0U);
        QCOMPARE(f.files->destroyed, 0U);
        QCOMPARE(f.registry->destroyed, 0U);
    }
    void fourRootsDispatchOnlyToTheirOwnPortAndRefsNeverCollide() {
        Fixture f;
        auto session = f.make();
        std::array<Inventory::NodeRef, 4> refs{};
        for (std::size_t i = 0; i < refs.size(); ++i) {
            const auto root = session->root(static_cast<Inventory::Root>(i));
            QCOMPARE(root.presence, Inventory::Presence::Present);
            QCOMPARE(root.error, 0U);
            QVERIFY(root.ref.value);
            refs[i] = root.ref;
            QCOMPARE(session->root(static_cast<Inventory::Root>(i)).ref, root.ref);
        }
        QCOMPARE(f.files->roots, (std::vector{Inventory::Root::PackageRoot, Inventory::Root::PackageRoot,
                                              Inventory::Root::AC, Inventory::Root::AC}));
        QCOMPARE(f.registry->roots, (std::vector{Inventory::Root::MappingsSID, Inventory::Root::MappingsSID,
                                                 Inventory::Root::StorageSID, Inventory::Root::StorageSID}));
        for (std::size_t i = 0; i < refs.size(); ++i)
            for (std::size_t j = i + 1; j < refs.size(); ++j)
                QVERIFY(refs[i] != refs[j]);
        const auto before_files = f.files->roots.size(), before_registry = f.registry->roots.size();
        QCOMPARE(session->root(static_cast<Inventory::Root>(99)).presence, Inventory::Presence::Unknown);
        QCOMPARE(f.files->roots.size(), before_files);
        QCOMPARE(f.registry->roots.size(), before_registry);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void opaqueNativeRefsCanUseAllSixtyFourBitsWithoutArithmeticOverflow() {
        Fixture f;
        f.files->root_results[0].ref.value = std::numeric_limits<std::uint64_t>::max();
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        QVERIFY(root.ref.value);
        QCOMPARE(session->readMetadata(root.ref, 512).error, 0U);
        QCOMPARE(f.files->last_ref.value, std::numeric_limits<std::uint64_t>::max());
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void namespacePreservesAliasesWithinAPortWithoutCollidingAcrossPorts() {
        Fixture f;
        f.files->metadata = [&](const Inventory::NodeRef ref) {
            auto result = validMetadata(*f.files, ref);
            result.identity = {4, 99};
            return result;
        };
        f.registry->metadata = [&](const Inventory::NodeRef ref) {
            auto result = validMetadata(*f.registry, ref);
            result.identity = {4, 99};
            return result;
        };
        auto session = f.make();
        const auto package = session->root(Inventory::Root::PackageRoot), ac = session->root(Inventory::Root::AC);
        const auto mappings = session->root(Inventory::Root::MappingsSID);
        const auto first = session->readMetadata(package.ref, 512), same = session->readMetadata(ac.ref, 512);
        const auto different = session->readMetadata(mappings.ref, 512);
        QCOMPARE(first.error, 0U);
        QCOMPARE(same.error, 0U);
        QCOMPARE(different.error, 0U);
        QVERIFY(first.identity == same.identity);
        QVERIFY(first.identity != different.identity);
        QCOMPARE(first.ref, package.ref);
        QCOMPARE(same.ref, ac.ref);
        QCOMPARE(different.ref, mappings.ref);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void theSameOuterRefCannotSilentlyChangeItsCapturedIdentity() {
        Fixture f;
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        QCOMPARE(session->readMetadata(root.ref, 512).error, 0U);
        f.files->metadata = [&](const Inventory::NodeRef ref) {
            auto result = validMetadata(*f.files, ref);
            result.identity = {7, 999};
            return result;
        };
        const auto changed = session->readMetadata(root.ref, 512);
        QCOMPARE(changed.presence, Inventory::Presence::Unknown);
        QCOMPARE(changed.error, invalid_data);
        QVERIFY(session->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void metadataAndChildDispatchForwardExactNativeArgumentsAndReturnOuterRefs() {
        Fixture f;
        f.registry->child = [](Inventory::NodeRef, std::uint32_t) {
            return Inventory::ChildResult{Inventory::ChildState::Child, 0, QStringLiteral("child"), {55}};
        };
        auto session = f.make();
        const auto root = session->root(Inventory::Root::MappingsSID);
        const auto metadata = session->readMetadata(root.ref, 257);
        QCOMPARE(metadata.error, 0U);
        QCOMPARE(metadata.type, Inventory::Type::RegistryKey);
        QCOMPARE(f.registry->last_ref, Inventory::NodeRef{1});
        QCOMPARE(f.registry->last_budget, 257U);
        const auto child = session->nextChild(root.ref, 3, 14);
        QCOMPARE(child.state, Inventory::ChildState::Child);
        QCOMPARE(child.name, QStringLiteral("child"));
        QVERIFY(child.ref.value && child.ref != root.ref);
        QCOMPARE(f.registry->last_ref, Inventory::NodeRef{1});
        QCOMPARE(f.registry->last_index, 3U);
        QCOMPARE(f.registry->last_units, 14U);
        QCOMPARE(session->readMetadata(child.ref, 512).error, 0U);
        QCOMPARE(f.registry->last_ref, Inventory::NodeRef{55});
        QCOMPARE(f.files->reads + f.files->children, 0U);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void unknownRefsAndInvalidBoundsNeverReachEitherPort_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 6; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void unknownRefsAndInvalidBoundsNeverReachEitherPort() {
        QFETCH(int, kind);
        Fixture f;
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        if (kind < 3) {
            const auto result = session->readMetadata(kind == 0 ? Inventory::NodeRef{999} : root.ref,
                                                      kind == 1   ? 0
                                                      : kind == 2 ? Inventory::maximum_total_bytes + 1
                                                                  : 512);
            QVERIFY(result.error != 0);
            QCOMPARE(result.presence, Inventory::Presence::Unknown);
        } else {
            const auto result = session->nextChild(kind == 3 ? Inventory::NodeRef{999} : root.ref, 0,
                                                   kind == 4   ? 0
                                                   : kind == 5 ? 256
                                                               : 255);
            QCOMPARE(result.state, Inventory::ChildState::Unknown);
            QVERIFY(result.error != 0);
        }
        QCOMPARE(f.files->reads + f.files->children + f.registry->reads + f.registry->children, 0U);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void missingOrFailedRootsKeepRawPresenceAndErrorsWithoutMintingRefs_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 4; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void missingOrFailedRootsKeepRawPresenceAndErrorsWithoutMintingRefs() {
        QFETCH(int, kind);
        Fixture f;
        const auto expected = kind == 0   ? Inventory::Presence::Missing
                              : kind == 3 ? Inventory::Presence::Present
                                          : Inventory::Presence::Unknown;
        f.registry->root_results[2] = {{}, kind == 0 ? 2U : 5U, expected};
        if (kind >= 2)
            f.registry->root_results[2].ref = {77};
        auto session = f.make();
        const auto result = session->root(Inventory::Root::MappingsSID);
        QCOMPARE(result.presence, expected);
        QCOMPARE(result.error, kind == 0 ? 2U : 5U);
        QVERIFY(!result.ref.value);
        QVERIFY(session->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void successfulRootWithoutAnInnerRefIsRejected() {
        Fixture f;
        f.files->root_results[0].ref = {};
        auto session = f.make();
        const auto result = session->root(Inventory::Root::PackageRoot);
        QCOMPARE(result.presence, Inventory::Presence::Unknown);
        QCOMPARE(result.error, invalid_data);
        QVERIFY(!result.ref.value);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void partialMetadataPreservesRawErrorsFlagsAndOwnsBorrowedQtBytes() {
        Fixture f;
        QByteArray sid = owner(), sd{"partial-descriptor"};
        f.registry->metadata = [&](const Inventory::NodeRef ref) {
            auto result = validMetadata(*f.registry, ref);
            result.error = 5;
            result.registry_link = true;
            result.reparse = true;
            result.reparse_tag = 0x12345678;
            result.owner_sid = QByteArray::fromRawData(sid.constData(), sid.size());
            result.raw_sd = QByteArray::fromRawData(sd.constData(), sd.size());
            return result;
        };
        auto session = f.make();
        const auto root = session->root(Inventory::Root::MappingsSID);
        const auto result = session->readMetadata(root.ref, 512);
        QCOMPARE(result.error, 5U);
        QCOMPARE(result.presence, Inventory::Presence::Present);
        QCOMPARE(result.ref, root.ref);
        QVERIFY(result.registry_link && result.reparse);
        QCOMPARE(result.reparse_tag, 0x12345678U);
        sid.fill('x');
        sd.fill('y');
        QCOMPARE(result.owner_sid, owner());
        QCOMPARE(result.raw_sd, QByteArray{"partial-descriptor"});
        QVERIFY(session->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void metadataRejectsMismatchedRefsTypesIdentitiesAndOversizedPayloads_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 6; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void metadataRejectsMismatchedRefsTypesIdentitiesAndOversizedPayloads() {
        QFETCH(int, kind);
        Fixture f;
        f.files->metadata = [&](const Inventory::NodeRef ref) {
            auto result = validMetadata(*f.files, ref);
            if (kind == 0)
                result.ref = {55};
            if (kind == 1)
                result.type = Inventory::Type::RegistryKey;
            if (kind == 2)
                result.identity = {};
            if (kind == 3)
                result.raw_sd = QByteArray(static_cast<qsizetype>(Inventory::maximum_sd_bytes + 1), 'a');
            if (kind == 4)
                result.owner_sid = QByteArray(69, 'b');
            return result;
        };
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        const auto result = session->readMetadata(root.ref, kind == 5 ? 1 : Inventory::maximum_total_bytes);
        QCOMPARE(result.presence, Inventory::Presence::Unknown);
        QVERIFY(result.error != 0 && result.raw_sd.isEmpty() && result.owner_sid.isEmpty());
        QVERIFY(session->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void childFramesAreValidatedAndNamesOwnBorrowedStorage_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 6; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void childFramesAreValidatedAndNamesOwnBorrowedStorage() {
        QFETCH(int, kind);
        Fixture f;
        QString name{QStringLiteral("node").constData(), 4};
        f.files->child = [&](Inventory::NodeRef, std::uint32_t) {
            Inventory::ChildResult result{
                Inventory::ChildState::Child, 0, QString::fromRawData(name.constData(), name.size()), {2}};
            if (kind == 1)
                result.ref = {};
            if (kind == 2)
                result.name = QStringLiteral("../escape");
            if (kind == 3)
                result.state = Inventory::ChildState::End;
            if (kind == 4)
                result.error = 5;
            if (kind == 5)
                result.name = QString(256, 'n');
            return result;
        };
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        const auto result = session->nextChild(root.ref, 0, 255);
        if (kind == 0) {
            QCOMPARE(result.state, Inventory::ChildState::Child);
            QVERIFY(result.ref.value && result.ref != root.ref);
            name.fill('x');
            QCOMPARE(result.name, QStringLiteral("node"));
        } else {
            QCOMPARE(result.state, Inventory::ChildState::Unknown);
            QCOMPARE(result.error, kind == 4 ? 5U : invalid_data);
            QVERIFY(result.name.isEmpty() && !result.ref.value);
        }
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void refAndIdentityMapsHaveInclusiveFiniteCaps() {
        Fixture f;
        f.files->child = [](Inventory::NodeRef, const std::uint32_t index) {
            return Inventory::ChildResult{Inventory::ChildState::Child,
                                          0,
                                          QStringLiteral("n") + QString::number(index),
                                          {std::uint64_t{index} + 2}};
        };
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        QCOMPARE(session->readMetadata(root.ref, 512).error, 0U);
        for (std::uint32_t index = 0; index < Session::maximum_refs - 1; ++index) {
            const auto child = session->nextChild(root.ref, index, 255);
            QCOMPARE(child.state, Inventory::ChildState::Child);
            const auto metadata = session->readMetadata(child.ref, 512);
            if (index < Session::maximum_identities - 1)
                QCOMPARE(metadata.error, 0U);
            else {
                QCOMPARE(metadata.error, more_data);
                QCOMPARE(metadata.presence, Inventory::Presence::Unknown);
            }
        }
        const auto over = session->nextChild(root.ref, Session::maximum_refs - 1, 255);
        QCOMPARE(over.state, Inventory::ChildState::Unknown);
        QCOMPARE(over.error, more_data);
        QVERIFY(!over.ref.value);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void revalidationCallsBothPortsAndFailureIsSticky_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void revalidationCallsBothPortsAndFailureIsSticky() {
        QFETCH(int, kind);
        Fixture f;
        if (kind == 1)
            f.files->validation = {Inventory::Validation::Changed, 0};
        if (kind == 2)
            f.registry->validation = {Inventory::Validation::Unknown, 5};
        if (kind == 3)
            f.files->validation = {Inventory::Validation::Unchanged, 5};
        if (kind == 4)
            f.registry->throw_validate = true;
        auto session = f.make();
        const auto result = session->revalidate();
        QCOMPARE(result.state, kind == 0   ? Inventory::Validation::Unchanged
                               : kind == 1 ? Inventory::Validation::Changed
                                           : Inventory::Validation::Unknown);
        QCOMPARE(f.files->validations, 1U);
        QCOMPARE(f.registry->validations, 1U);
        if (kind != 0) {
            f.files->validation = f.registry->validation = {Inventory::Validation::Unchanged, 0};
            f.registry->throw_validate = false;
            QCOMPARE(session->revalidate().state, result.state);
            QCOMPARE(f.files->validations, 1U);
            QCOMPARE(f.registry->validations, 1U);
        }
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void closeAttemptsBothInOrderAndAcknowledgesOnlyBothClosedZero() {
        Fixture f;
        std::vector<int> order;
        f.files->on_close = [&] { order.push_back(1); };
        f.registry->on_close = [&] { order.push_back(2); };
        auto session = f.make();
        const auto result = session->close();
        QCOMPARE(result.state, Inventory::CloseState::Closed);
        QCOMPARE(result.error, 0U);
        QCOMPARE(order, (std::vector{1, 2}));
        QCOMPARE(f.files->destroyed, 1U);
        QCOMPARE(f.registry->destroyed, 1U);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.files->closes, 1U);
        QCOMPARE(f.registry->closes, 1U);
        QCOMPARE(session->root(Inventory::Root::PackageRoot).presence, Inventory::Presence::Unknown);
        QCOMPARE(session->revalidate().state, Inventory::Validation::Unknown);
    }
    void unknownContradictoryAndThrowingCloseNeverRetryOrDestroyUncertainPorts_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 4; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void unknownContradictoryAndThrowingCloseNeverRetryOrDestroyUncertainPorts() {
        QFETCH(int, kind);
        Fixture f;
        if (kind == 0)
            f.files->closure = {Inventory::CloseState::Unknown, 5};
        if (kind == 1)
            f.files->closure = {Inventory::CloseState::Closed, 5};
        if (kind == 2)
            f.files->throw_close = true;
        if (kind == 3) {
            f.files->closure = {Inventory::CloseState::Unknown, 5};
            f.registry->closure = {Inventory::CloseState::NotAttempted, busy};
        }
        {
            auto session = f.make();
            QCOMPARE(session->close().state, Inventory::CloseState::Unknown);
            QCOMPARE(session->closeResult().state, Inventory::CloseState::Unknown);
            QCOMPARE(session->close().state, Inventory::CloseState::Unknown);
            QCOMPARE(f.files->closes, 1U);
            QCOMPARE(f.registry->closes, 1U);
            QCOMPARE(f.files->destroyed, 0U);
            QCOMPARE(f.registry->destroyed, kind == 3 ? 0U : 1U);
        }
        QCOMPARE(f.files->destroyed, 0U);
        QCOMPARE(f.registry->destroyed, kind == 3 ? 0U : 1U);
    }
    void busyOnlyCloseCanRetryOnlyTheUnattemptedPort() {
        Fixture f;
        f.registry->closure = {Inventory::CloseState::NotAttempted, busy};
        auto session = f.make();
        QCOMPARE(session->close().state, Inventory::CloseState::NotAttempted);
        QCOMPARE(session->closeResult().error, busy);
        QCOMPARE(f.files->closes, 1U);
        QCOMPARE(f.registry->closes, 1U);
        QCOMPARE(f.files->destroyed, 1U);
        QCOMPARE(f.registry->destroyed, 0U);
        f.registry->closure = {Inventory::CloseState::Closed, 0};
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.files->closes, 1U);
        QCOMPARE(f.registry->closes, 2U);
    }
    void callbacksCannotReenterReadValidationOrCloseAndDestroyAnActivePort() {
        Fixture f;
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        Inventory::CloseResult nested_close;
        Inventory::ValidationResult nested_validation;
        Inventory::Metadata nested_read;
        f.files->on_read = [&] {
            nested_close = session->close();
            nested_validation = session->revalidate();
            nested_read = session->readMetadata(root.ref, 512);
            QCOMPARE(f.files->destroyed, 0U);
        };
        QCOMPARE(session->readMetadata(root.ref, 512).error, 0U);
        QCOMPARE(nested_close.state, Inventory::CloseState::NotAttempted);
        QCOMPARE(nested_close.error, busy);
        QCOMPARE(nested_validation.state, Inventory::Validation::Unknown);
        QCOMPARE(nested_validation.error, busy);
        QCOMPARE(nested_read.error, busy);
        QCOMPARE(f.files->reads, 1U);
        QCOMPARE(f.files->validations + f.registry->validations + f.files->closes + f.registry->closes, 0U);
        f.files->on_close = [&] { nested_close = session->close(); };
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
        QCOMPARE(nested_close.state, Inventory::CloseState::NotAttempted);
        QCOMPARE(nested_close.error, busy);
        QCOMPARE(f.files->closes, 1U);
    }
    void missingPortNeverAllowsACompleteScopeOrAnAcknowledgedAggregate() {
        Fixture f;
        Session session(std::make_unique<Port>(f.files), {});
        QCOMPARE(session.root(Inventory::Root::PackageRoot).presence, Inventory::Presence::Unknown);
        QCOMPARE(session.root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Unknown);
        QCOMPARE(f.files->roots.size(), std::size_t{});
        QCOMPARE(session.revalidate().state, Inventory::Validation::Unknown);
        QCOMPARE(session.close().state, Inventory::CloseState::Unknown);
        QCOMPARE(f.files->closes, 1U);
        QCOMPARE(f.files->destroyed, 1U);
    }
    void exceptionsFromReadPortsNeverBecomeUsableFrames_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 3; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void exceptionsFromReadPortsNeverBecomeUsableFrames() {
        QFETCH(int, kind);
        Fixture f;
        auto session = f.make();
        const auto root = session->root(Inventory::Root::PackageRoot);
        if (kind == 0) {
            f.registry->throw_root = true;
            const auto result = session->root(Inventory::Root::MappingsSID);
            QCOMPARE(result.presence, Inventory::Presence::Unknown);
            QVERIFY(result.error != 0);
        } else if (kind == 1) {
            f.files->throw_read = true;
            const auto result = session->readMetadata(root.ref, 512);
            QCOMPARE(result.presence, Inventory::Presence::Unknown);
            QVERIFY(result.error != 0);
        } else {
            f.files->throw_child = true;
            const auto result = session->nextChild(root.ref, 0, 255);
            QCOMPARE(result.state, Inventory::ChildState::Unknown);
            QVERIFY(result.error != 0);
        }
        QVERIFY(session->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(session->close().state, Inventory::CloseState::Closed);
    }
    void composedFakeSessionFeedsTheExistingFourRootInventoryWithoutDuplicatingAc() {
        Fixture f;
        f.files->child = [](const Inventory::NodeRef parent, const std::uint32_t index) {
            if (parent == Inventory::NodeRef{1} && index == 0)
                return Inventory::ChildResult{Inventory::ChildState::Child, 0, QStringLiteral("AC"), {2}};
            return Inventory::ChildResult{Inventory::ChildState::End, 0, {}, {}};
        };
        Source source(f.make());
        auto result = Inventory::inspect(source);
        QCOMPARE(result.report.status, Inventory::Status::Observed);
        QCOMPARE(result.report.objects.size(), std::size_t{4});
        QCOMPARE(result.report.validation.state, Inventory::Validation::Unchanged);
        QVERIFY(result.lease);
        QCOMPARE(result.lease->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.files->closes, 1U);
        QCOMPARE(f.registry->closes, 1U);
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageSessionTests)
#include "AppContainerStorageSessionTests.moc"

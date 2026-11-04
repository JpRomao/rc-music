#include "../windows/sandbox/AppContainerStorageRegistry.hpp"

#include <QTest>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using Registry = music_player::sandbox_probe::AppContainerStorageRegistry;
using Inventory = Registry::Inventory;
using Security = music_player::sandbox_probe::AppContainerStorageSecurity;
static_assert(std::is_base_of_v<Inventory::ReadSession, Registry>);
static_assert(Registry::maximum_native_name_units == 32767);
namespace {
constexpr std::uint32_t invalid_data = 13, busy = 170, more_data = 234;
const QString sid = QStringLiteral("S-1-15-2-1-2-3-4-5-6-7");
const QString prefix = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
QByteArray owner() { return QByteArray::fromHex("010100000000000515000000"); }
QByteArray descriptor() {
    auto bytes = QByteArray::fromHex("0100049014000000000000000000000000000000");
    bytes.append(owner());
    return bytes;
}
Registry::RootPaths paths() {
    return {prefix + QStringLiteral("Mappings\\") + sid, prefix + QStringLiteral("Storage\\") + sid};
}
struct Key final {
    QString name, native_name;
    std::vector<std::shared_ptr<Key>> children{};
    QByteArray sd{descriptor()};
    std::uint64_t last_write{1};
    Registry::ReservedValue reserved{Registry::ReservedValue::Absent};
    std::uint32_t reserved_type{}, reserved_bytes{}, info_error{}, poll_error{};
    Registry::EventState event_state{Registry::EventState::Unchanged};
    std::optional<std::uint32_t> reported_count{};
};
struct Operation final {
    QString kind{};
    Registry::Handle handle{};
    std::uint32_t index{}, units{};
};
struct State final {
    std::shared_ptr<Key> software{std::make_shared<Key>()}, mappings{}, storage{};
    std::map<Registry::Handle, std::shared_ptr<Key>> keys{}, events{};
    std::vector<Operation> operations{};
    std::vector<std::pair<Registry::Handle, QString>> opens{};
    std::vector<Registry::Handle> opened{}, closed_keys{}, closed_events{};
    Registry::Handle next_key{100}, next_event{10000};
    unsigned software_opens{}, security_queries{}, security_reads{};
    std::function<void(const QString&, Registry::Handle)> hook{};
    std::function<std::optional<Registry::OpenResult>(Registry::Handle, const QString&)> open_override{};
    std::function<std::optional<Registry::EnumResult>(Registry::Handle, std::uint32_t)> enum_override{};
    std::function<std::optional<Registry::KeyInfo>(Registry::Handle)> info_override{};
    std::function<std::uint32_t(Registry::Handle)> key_close_override{}, event_close_override{};
    bool throw_open{}, throw_info{}, throw_enum{}, throw_watch{}, throw_poll{}, throw_security{};

    State() {
        software->name = QStringLiteral("Software");
        software->native_name = QStringLiteral("\\REGISTRY\\USER\\fixture\\Software");
        auto app = software;
        for (const auto& name : {QStringLiteral("Microsoft"), QStringLiteral("Windows"),
                                 QStringLiteral("CurrentVersion"), QStringLiteral("AppContainer")})
            app = add(app, name);
        mappings = add(add(app, QStringLiteral("Mappings")), sid);
        storage = add(add(app, QStringLiteral("Storage")), sid);
    }
    std::shared_ptr<Key> add(const std::shared_ptr<Key>& parent, const QString& name) {
        auto child = std::make_shared<Key>();
        child->name = QString(name.constData(), name.size());
        child->native_name = parent->native_name + QLatin1Char('\\') + name;
        parent->children.push_back(child);
        return child;
    }
    Registry::Handle own(const std::shared_ptr<Key>& key) {
        const auto handle = next_key++;
        keys.emplace(handle, key);
        opened.push_back(handle);
        return handle;
    }
    Registry::OpenResult present(const std::shared_ptr<Key>& key) {
        const auto handle = own(key);
        return {Registry::OpenState::Present, 0, handle, {handle, 0}, 1, 0};
    }
    void record(const QString& kind, const Registry::Handle handle, const std::uint32_t index = 0,
                const std::uint32_t units = 0) {
        operations.push_back({kind, handle, index, units});
        if (hook)
            hook(kind, handle);
    }
    Registry::Handle handleFor(const std::shared_ptr<Key>& wanted) const {
        for (const auto& [handle, key] : keys)
            if (key == wanted)
                return handle;
        return 0;
    }
    std::size_t count(const QString& kind) const {
        return static_cast<std::size_t>(
            std::count_if(operations.begin(), operations.end(), [&](const auto& value) { return value.kind == kind; }));
    }
};
Registry::Calls calls(const std::shared_ptr<State>& state) {
    return {.open_software =
                [state] {
                    ++state->software_opens;
                    state->record(QStringLiteral("software"), 0);
                    if (state->throw_open)
                        throw std::runtime_error{"open"};
                    if (state->open_override)
                        if (auto value = state->open_override(0, QStringLiteral("Software")))
                            return *value;
                    return state->present(state->software);
                },
            .open_child =
                [state](const Registry::Handle parent, const QString& name) {
                    state->opens.emplace_back(parent, QString(name.constData(), name.size()));
                    state->record(QStringLiteral("open"), parent);
                    if (state->throw_open)
                        throw std::runtime_error{"open"};
                    if (state->open_override)
                        if (auto value = state->open_override(parent, name))
                            return *value;
                    const auto at = state->keys.find(parent);
                    if (at == state->keys.end())
                        return Registry::OpenResult{};
                    for (const auto& child : at->second->children)
                        if (child->name.compare(name, Qt::CaseInsensitive) == 0)
                            return state->present(child);
                    return Registry::OpenResult{Registry::OpenState::Missing, 2};
                },
            .query_info =
                [state](const Registry::Handle handle, const std::uint32_t units) {
                    state->record(QStringLiteral("info"), handle, 0, units);
                    if (state->throw_info)
                        throw std::runtime_error{"info"};
                    if (state->info_override)
                        if (auto value = state->info_override(handle))
                            return *value;
                    const auto key = state->keys.at(handle);
                    if (key->native_name.size() > units)
                        return Registry::KeyInfo{more_data};
                    return Registry::KeyInfo{
                        key->info_error,
                        key->native_name,
                        key->reported_count.value_or(static_cast<std::uint32_t>(key->children.size())),
                        key->last_write,
                        key->reserved,
                        key->reserved_type,
                        key->reserved_bytes};
                },
            .enum_child =
                [state](const Registry::Handle handle, const std::uint32_t index, const std::uint32_t units) {
                    state->record(QStringLiteral("enum"), handle, index, units);
                    if (state->throw_enum)
                        throw std::runtime_error{"enum"};
                    if (state->enum_override)
                        if (auto value = state->enum_override(handle, index))
                            return *value;
                    const auto key = state->keys.at(handle);
                    if (index >= key->children.size())
                        return Registry::EnumResult{Inventory::ChildState::End, 0};
                    const auto name = key->children[index]->name;
                    if (name.size() > units)
                        return Registry::EnumResult{Inventory::ChildState::Unknown, more_data};
                    return Registry::EnumResult{Inventory::ChildState::Child, 0, name};
                },
            .watch_once =
                [state](const Registry::Handle handle) {
                    state->record(QStringLiteral("watch"), handle);
                    if (state->throw_watch)
                        throw std::runtime_error{"watch"};
                    const auto event = state->next_event++;
                    state->events.emplace(event, state->keys.at(handle));
                    return Registry::WatchResult{0, event};
                },
            .poll_event =
                [state](const Registry::Handle event) {
                    state->record(QStringLiteral("poll"), event);
                    if (state->throw_poll)
                        throw std::runtime_error{"poll"};
                    const auto key = state->events.at(event);
                    return Registry::PollResult{key->event_state, key->poll_error};
                },
            .security = {.size_query =
                             [state](const Security::Object object, const Security::Handle handle) {
                                 ++state->security_queries;
                                 state->record(QStringLiteral("security_size"), handle);
                                 if (state->throw_security)
                                     throw std::runtime_error{"security"};
                                 if (object != Security::Object::Key)
                                     return Security::NativeResult{invalid_data, 0};
                                 return Security::NativeResult{
                                     122, static_cast<std::uint32_t>(state->keys.at(handle)->sd.size())};
                             },
                         .read =
                             [state](const Security::Object object, const Security::Handle handle,
                                     const std::span<std::uint8_t> output) {
                                 ++state->security_reads;
                                 state->record(QStringLiteral("security_read"), handle);
                                 if (state->throw_security)
                                     throw std::runtime_error{"security"};
                                 const auto data = state->keys.at(handle)->sd;
                                 if (object != Security::Object::Key ||
                                     output.size() < static_cast<std::size_t>(data.size()))
                                     return Security::NativeResult{more_data, static_cast<std::uint32_t>(data.size())};
                                 std::memcpy(output.data(), data.constData(), static_cast<std::size_t>(data.size()));
                                 return Security::NativeResult{0, static_cast<std::uint32_t>(data.size())};
                             }},
            .close_key =
                [state](const Registry::Handle handle) {
                    state->closed_keys.push_back(handle);
                    state->record(QStringLiteral("close_key"), handle);
                    return state->key_close_override ? state->key_close_override(handle) : 0U;
                },
            .close_event =
                [state](const Registry::Handle event) {
                    state->closed_events.push_back(event);
                    state->record(QStringLiteral("close_event"), event);
                    return state->event_close_override ? state->event_close_override(event) : 0U;
                }};
}
struct Fixture final {
    std::shared_ptr<State> state{std::make_shared<State>()};
    std::unique_ptr<Registry> make() { return std::make_unique<Registry>(paths(), calls(state)); }
};
bool noReads(const State& state) { return !state.security_queries && !state.security_reads; }
} // namespace

class AppContainerStorageRegistryTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndDestructionAreInert() {
        Fixture f;
        {
            auto reader = f.make();
            QCOMPARE(reader->closeResult().state, Inventory::CloseState::NotAttempted);
        }
        QVERIFY(f.state->operations.empty());
    }
    void successfulRootBindsOnlyRelativeComponentsAndReusesItsAncestors() {
        Fixture f;
        auto reader = f.make();
        const auto mappings = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(mappings.presence, Inventory::Presence::Present);
        QCOMPARE(mappings.error, 0U);
        QVERIFY(mappings.ref.value);
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).ref, mappings.ref);
        const auto storage = reader->root(Inventory::Root::StorageSID);
        QCOMPARE(storage.presence, Inventory::Presence::Present);
        QVERIFY(storage.ref.value != mappings.ref.value);
        QCOMPARE(f.state->software_opens, 1U);
        QCOMPARE(f.state->opens.size(), std::size_t{8});
        for (const auto& [parent, name] : f.state->opens) {
            QVERIFY(parent);
            QVERIFY(!name.contains(QLatin1Char('\\')));
            QVERIFY(!name.contains(QLatin1Char('/')));
        }
        QCOMPARE(reader->revalidate().state, Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void onlyTheTwoRegistryRootsAreSupported() {
        Fixture f;
        auto reader = f.make();
        for (const auto root : {Inventory::Root::PackageRoot, Inventory::Root::AC, static_cast<Inventory::Root>(99)}) {
            const auto value = reader->root(root);
            QCOMPARE(value.presence, Inventory::Presence::Unknown);
            QVERIFY(value.error != 0);
        }
        QVERIFY(f.state->operations.empty());
    }
    void malformedCapturedRootsAreRefusedBeforeIo_data() {
        QTest::addColumn<QString>("mappings");
        QTest::addColumn<QString>("storage");
        const auto valid = paths();
        QTest::newRow("empty") << QString{} << valid.storage_key;
        QTest::newRow("absolute") << QStringLiteral("HKCU\\") + valid.mappings_key << valid.storage_key;
        QTest::newRow("other-prefix") << QStringLiteral("Software\\Elsewhere\\") + sid << valid.storage_key;
        QTest::newRow("trailing-component") << valid.mappings_key + QStringLiteral("\\child") << valid.storage_key;
        QTest::newRow("slash") << QString(valid.mappings_key).replace(QLatin1Char('\\'), QLatin1Char('/'))
                               << valid.storage_key;
        QTest::newRow("embedded-nul") << valid.mappings_key + QChar{} << valid.storage_key;
        QTest::newRow("different-sid") << valid.mappings_key
                                       << prefix + QStringLiteral("Storage\\S-1-15-2-1-2-3-4-5-6-8");
        for (const auto& bad :
             {QStringLiteral("S-1-15-2-1-2-3-4-5-6"), QStringLiteral("S-1-15-2-1-2-3-4-5-6-7-8"),
              QStringLiteral("S-1-15-2-01-2-3-4-5-6-7"), QStringLiteral("S-1-15-2-4294967296-2-3-4-5-6-7"),
              QStringLiteral("S-1-15-2-+1-2-3-4-5-6-7"), QStringLiteral("S-1-15-2-1-2-3-4-5-6- 7")})
            QTest::newRow(qPrintable(bad))
                << prefix + QStringLiteral("Mappings\\") + bad << prefix + QStringLiteral("Storage\\") + bad;
    }
    void malformedCapturedRootsAreRefusedBeforeIo() {
        QFETCH(QString, mappings);
        QFETCH(QString, storage);
        Fixture f;
        Registry reader{{mappings, storage}, calls(f.state)};
        const auto root = reader.root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(root.error != 0);
        QVERIFY(f.state->operations.empty());
    }
    void canonicalUint32BoundarySidsAreAccepted() {
        Fixture f;
        const auto boundary = QStringLiteral("S-1-15-2-0-4294967295-2-3-4-5-6");
        f.state->mappings->name = boundary;
        f.state->mappings->native_name.replace(sid, boundary);
        f.state->storage->name = boundary;
        f.state->storage->native_name.replace(sid, boundary);
        Registry reader{
            {prefix + QStringLiteral("Mappings\\") + boundary, prefix + QStringLiteral("Storage\\") + boundary},
            calls(f.state)};
        QCOMPARE(reader.root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        QCOMPARE(reader.root(Inventory::Root::StorageSID).presence, Inventory::Presence::Present);
        QCOMPARE(reader.close().state, Inventory::CloseState::Closed);
    }
    void capturedRawDataPathsAreDeepOwned() {
        Fixture f;
        auto first = paths().mappings_key;
        auto second = paths().storage_key;
        Registry reader{{QString::fromRawData(first.constData(), first.size()),
                         QString::fromRawData(second.constData(), second.size())},
                        calls(f.state)};
        std::fill(first.data(), first.data() + first.size(), QChar('x'));
        std::fill(second.data(), second.data() + second.size(), QChar('y'));
        QCOMPARE(reader.root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        QCOMPARE(reader.root(Inventory::Root::StorageSID).presence, Inventory::Presence::Present);
        QCOMPARE(reader.close().state, Inventory::CloseState::Closed);
    }
    void missingCallbacksNeverFallBackToNativeIo() {
        Registry reader{paths(), {}};
        const auto root = reader.root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(root.error != 0);
        QCOMPARE(reader.revalidate().state, Inventory::Validation::Unknown);
    }
    void securityMetadataIsCapturedWithALocalIdentity() {
        Fixture f;
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        const auto metadata = reader->readMetadata(root.ref, Inventory::maximum_sd_bytes);
        QCOMPARE(metadata.error, 0U);
        QCOMPARE(metadata.ref, root.ref);
        QCOMPARE(metadata.presence, Inventory::Presence::Present);
        QCOMPARE(metadata.type, Inventory::Type::RegistryKey);
        QCOMPARE(metadata.identity.domain, std::uint64_t{2});
        QVERIFY(metadata.identity.object);
        QVERIFY(metadata.identity.object != f.state->handleFor(f.state->mappings));
        QCOMPARE(metadata.raw_sd, descriptor());
        QCOMPARE(metadata.owner_sid, owner());
        QCOMPARE(metadata.dacl, Inventory::Dacl::Null);
        QVERIFY(metadata.dacl_protected);
        QVERIFY(metadata.sacl_not_queried);
        QVERIFY(!metadata.registry_link && !metadata.reparse);
        QCOMPARE(metadata.link_count, 0U);
        QCOMPARE(reader->readMetadata(root.ref, Inventory::maximum_sd_bytes).identity, metadata.identity);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void softwareBootstrapIsWatchedAndNameComparedButOnlyWorkKeyGetsSecurityRead() {
        Fixture f;
        Registry::Handle bootstrap{};
        f.state->open_override = [state = f.state, &bootstrap](const auto parent, const auto&) {
            if (parent)
                return std::optional<Registry::OpenResult>{};
            bootstrap = state->own(state->software);
            auto result = state->present(state->software);
            result.owned_keys = {bootstrap, result.key};
            result.owned_count = 2;
            return std::optional{result};
        };
        f.state->info_override = [state = f.state, &bootstrap](const auto key) -> std::optional<Registry::KeyInfo> {
            if (key == bootstrap)
                return Registry::KeyInfo{0, state->software->native_name};
            return {};
        };
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        QVERIFY(bootstrap);
        for (const auto& operation : f.state->operations)
            if (operation.kind.startsWith(QStringLiteral("security")))
                QVERIFY(operation.handle != bootstrap);
        QVERIFY(std::any_of(f.state->operations.begin(), f.state->operations.end(), [&](const auto& operation) {
            return operation.kind == QStringLiteral("info") && operation.handle == bootstrap;
        }));
        QVERIFY(std::any_of(f.state->operations.begin(), f.state->operations.end(), [&](const auto& operation) {
            return operation.kind == QStringLiteral("watch") && operation.handle == bootstrap;
        }));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        QCOMPARE(std::count(f.state->closed_keys.begin(), f.state->closed_keys.end(), bootstrap), 1);
    }
    void watchesPrecedeEveryFirstInfoEnumAndSecurityReadAndNativeBoundsArePassed() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(reader->readMetadata(root.ref, Inventory::maximum_sd_bytes).error, 0U);
        QCOMPARE(reader->nextChild(root.ref, 0, Inventory::maximum_name_units).state, Inventory::ChildState::Child);
        std::set<Registry::Handle> armed;
        for (const auto& op : f.state->operations) {
            if (op.kind == QStringLiteral("watch"))
                QVERIFY(armed.insert(op.handle).second);
            if (op.kind == QStringLiteral("info") || op.kind == QStringLiteral("enum") ||
                op.kind.startsWith(QStringLiteral("security")))
                QVERIFY(armed.contains(op.handle));
            if (op.kind == QStringLiteral("info")) {
                QVERIFY(op.units > 0);
                QVERIFY(op.units <= Registry::maximum_native_name_units);
            }
            if (op.kind == QStringLiteral("enum"))
                QVERIFY(op.units <= Inventory::maximum_name_units);
        }
        QVERIFY(f.state->count(QStringLiteral("poll")) > 0);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void missingIsExplicitlyTypedAndErrorTwoAloneRemainsUnknown_data() {
        QTest::addColumn<int>("state");
        QTest::addColumn<int>("presence");
        QTest::newRow("typed-missing") << int(Registry::OpenState::Missing) << int(Inventory::Presence::Missing);
        QTest::newRow("unqualified-error-two")
            << int(Registry::OpenState::Unknown) << int(Inventory::Presence::Unknown);
        QTest::newRow("rejected-dangling-link")
            << int(Registry::OpenState::RejectedLink) << int(Inventory::Presence::Unknown);
    }
    void missingIsExplicitlyTypedAndErrorTwoAloneRemainsUnknown() {
        QFETCH(int, state);
        QFETCH(int, presence);
        Fixture f;
        f.state->open_override = [=](const auto parent, const auto& name) -> std::optional<Registry::OpenResult> {
            if (parent && name == sid)
                return Registry::OpenResult{static_cast<Registry::OpenState>(state), 2};
            return {};
        };
        auto reader = f.make();
        const auto result = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(result.presence, static_cast<Inventory::Presence>(presence));
        QCOMPARE(result.error, 2U);
        QVERIFY(!result.ref.value);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void rejectedLinkIsNeverQueriedWatchedOrSecurityRead() {
        Fixture f;
        Registry::Handle rejected{};
        f.state->open_override = [state = f.state, &rejected](const auto parent,
                                                              const auto& name) -> std::optional<Registry::OpenResult> {
            if (!parent || name != sid)
                return {};
            rejected = state->own(state->mappings);
            return Registry::OpenResult{Registry::OpenState::RejectedLink, 4395, 0, {rejected, 0}, 1, 0xC000050BU};
        };
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QCOMPARE(root.error, 4395U);
        for (const auto& operation : f.state->operations)
            if (operation.handle == rejected)
                QVERIFY(operation.kind != QStringLiteral("info") && operation.kind != QStringLiteral("watch") &&
                        !operation.kind.startsWith(QStringLiteral("security")));
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        QCOMPARE(std::count(f.state->closed_keys.begin(), f.state->closed_keys.end(), rejected), 1);
    }
    void presentReservedValueOrUnknownObservationIsNeverTreatedAsOrdinaryKey_data() {
        QTest::addColumn<int>("reserved");
        QTest::newRow("present-reg-link") << int(Registry::ReservedValue::Present);
        QTest::newRow("unknown-presence") << int(Registry::ReservedValue::Unknown);
    }
    void presentReservedValueOrUnknownObservationIsNeverTreatedAsOrdinaryKey() {
        QFETCH(int, reserved);
        Fixture f;
        f.state->mappings->reserved = static_cast<Registry::ReservedValue>(reserved);
        f.state->mappings->reserved_type = 6;
        f.state->mappings->reserved_bytes = 42;
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(root.error != 0);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void nativeNameMismatchOrMalformedNameFailsBeforeSecurity_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("other-binding") << QStringLiteral("\\REGISTRY\\USER\\other\\Software\\") + sid;
        QTest::newRow("relative") << QStringLiteral("relative");
        QTest::newRow("empty") << QString{};
        QTest::newRow("nul") << QStringLiteral("\\REGISTRY\\USER\\bad") + QChar{};
        QTest::newRow("over-limit") << QString(Registry::maximum_native_name_units + 1, QChar('x'));
    }
    void nativeNameMismatchOrMalformedNameFailsBeforeSecurity() {
        QFETCH(QString, name);
        Fixture f;
        f.state->mappings->native_name = name;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Unknown);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void nativeNameRawDataIsOwnedBeforeTheNextPortCall() {
        Fixture f;
        QString borrowed = f.state->mappings->native_name;
        bool returned = false;
        f.state->info_override = [state = f.state, &borrowed,
                                  &returned](const auto handle) -> std::optional<Registry::KeyInfo> {
            if (state->keys.at(handle) != state->mappings || returned)
                return {};
            returned = true;
            return Registry::KeyInfo{0, QString::fromRawData(borrowed.constData(), borrowed.size()), 0, 1,
                                     Registry::ReservedValue::Absent};
        };
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Present);
        std::fill(borrowed.data(), borrowed.data() + borrowed.size(), QChar('x'));
        QCOMPARE(reader->revalidate().state, Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void softwareRenameWithoutItsOwnEventIsDetectedBeforeOpeningChildren() {
        Fixture f;
        bool renamed = false;
        f.state->hook = [state = f.state, &renamed](const auto& kind, const auto) {
            if (!renamed && kind == QStringLiteral("open")) {
                renamed = true;
                state->software->native_name += QStringLiteral("Renamed");
            }
        };
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Unknown);
        QVERIFY(f.state->opens.size() <= 1);
        QVERIFY(noReads(*f.state));
        QVERIFY(reader->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void childCursorCachesFramesAndDoesNotSkipIndices() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("first"));
        f.state->add(f.state->mappings, QStringLiteral("second"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        const auto first = reader->nextChild(root.ref, 0, 255);
        QCOMPARE(first.state, Inventory::ChildState::Child);
        QCOMPARE(first.name, QStringLiteral("first"));
        QCOMPARE(reader->nextChild(root.ref, 0, 255).ref, first.ref);
        const auto second = reader->nextChild(root.ref, 1, 255);
        QCOMPARE(second.state, Inventory::ChildState::Child);
        QCOMPARE(second.name, QStringLiteral("second"));
        QVERIFY(first.ref.value && second.ref.value && first.ref.value != second.ref.value);
        QCOMPARE(reader->nextChild(root.ref, 2, 255).state, Inventory::ChildState::End);
        QCOMPARE(reader->revalidate().state, Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void skippedCursorIsRejectedBeforeEnumeration() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        const auto before = f.state->count(QStringLiteral("enum"));
        const auto child = reader->nextChild(root.ref, 1, 255);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QVERIFY(child.error != 0);
        QCOMPARE(f.state->count(QStringLiteral("enum")), before);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void childNameMustBeOneBoundedUnambiguousComponent_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("empty") << QString{};
        QTest::newRow("dot") << QStringLiteral(".");
        QTest::newRow("dot-dot") << QStringLiteral("..");
        QTest::newRow("backslash") << QStringLiteral("a\\b");
        QTest::newRow("slash") << QStringLiteral("a/b");
        QTest::newRow("nul") << QStringLiteral("bad") + QChar{};
        QTest::newRow("too-long") << QString(256, QChar('x'));
        QTest::newRow("unpaired-surrogate") << QString(QChar(0xD800));
    }
    void childNameMustBeOneBoundedUnambiguousComponent() {
        QFETCH(QString, name);
        Fixture f;
        f.state->add(f.state->mappings, name);
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        const auto opened = f.state->opens.size();
        const auto child = reader->nextChild(root.ref, 0, 255);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QVERIFY(child.error != 0);
        QCOMPARE(f.state->opens.size(), opened);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void unicodeAndMaximumLengthComponentsAreAccepted() {
        Fixture f;
        const auto unicode = QString::fromUtf8("música-音楽-😀");
        f.state->add(f.state->mappings, unicode);
        f.state->add(f.state->mappings, QString(255, QChar('x')));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(reader->nextChild(root.ref, 0, 255).name, unicode);
        QCOMPARE(reader->nextChild(root.ref, 1, 255).name, QString(255, QChar('x')));
        QCOMPARE(reader->nextChild(root.ref, 2, 255).state, Inventory::ChildState::End);
        QCOMPARE(reader->revalidate().state, Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void caseInsensitiveDuplicateChildrenAreRejected() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("Track"));
        f.state->add(f.state->mappings, QStringLiteral("track"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(reader->nextChild(root.ref, 0, 255).state, Inventory::ChildState::Child);
        const auto child = reader->nextChild(root.ref, 1, 255);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QVERIFY(child.error != 0);
        QVERIFY(reader->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void enumerationOrderCanChangeWithoutChangingTheSet() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("first"));
        f.state->add(f.state->mappings, QStringLiteral("second"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(reader->nextChild(root.ref, 0, 255).state, Inventory::ChildState::Child);
        QCOMPARE(reader->nextChild(root.ref, 1, 255).state, Inventory::ChildState::Child);
        QCOMPARE(reader->nextChild(root.ref, 2, 255).state, Inventory::ChildState::End);
        std::reverse(f.state->mappings->children.begin(), f.state->mappings->children.end());
        QCOMPARE(reader->revalidate().state, Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void enumerationMoreDataIsNotAcceptedAsATruncatedName() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("lengthy"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        const auto child = reader->nextChild(root.ref, 0, 3);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QCOMPARE(child.error, more_data);
        QVERIFY(child.name.isEmpty());
        QVERIFY(!child.ref.value);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void prematureEndContradictingCapturedSubkeyCountIsRejected() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        f.state->enum_override = [state = f.state](const auto handle,
                                                   const auto) -> std::optional<Registry::EnumResult> {
            if (state->keys.at(handle) == state->mappings)
                return Registry::EnumResult{Inventory::ChildState::End, 0};
            return {};
        };
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        const auto child = reader->nextChild(root.ref, 0, 255);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QVERIFY(child.error != 0);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void oversizedSuccessfulEnumFrameIsBoundsErrorBeforeOpeningTheChild() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        const QString oversized(32768, QChar('x'));
        f.state->enum_override = [held = f.state, oversized](const auto handle,
                                                             const auto) -> std::optional<Registry::EnumResult> {
            if (held->keys.at(handle) == held->mappings)
                return Registry::EnumResult{Inventory::ChildState::Child, 0, oversized};
            return {};
        };
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Present);
        const auto before = f.state->opens.size();
        const auto result = reader->nextChild(root.ref, 0, 255);
        QCOMPARE(result.state, Inventory::ChildState::Unknown);
        QCOMPARE(result.error, more_data);
        QVERIFY(result.name.isEmpty());
        QVERIFY(!result.ref.value);
        QCOMPARE(f.state->opens.size(), before);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void oversizedSuccessfulRevalidationEnumFrameIsBoundsError() {
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(reader->nextChild(root.ref, 0, 255).state, Inventory::ChildState::Child);
        QCOMPARE(reader->nextChild(root.ref, 1, 255).state, Inventory::ChildState::End);
        const QString oversized(32768, QChar('x'));
        f.state->enum_override = [held = f.state, oversized](const auto handle,
                                                             const auto index) -> std::optional<Registry::EnumResult> {
            if (held->keys.at(handle) == held->mappings && index == 0)
                return Registry::EnumResult{Inventory::ChildState::Child, 0, oversized};
            return {};
        };
        const auto before = f.state->opens.size();
        const auto validation = reader->revalidate();
        QCOMPARE(validation.state, Inventory::Validation::Unknown);
        QCOMPARE(validation.error, more_data);
        QCOMPARE(f.state->opens.size(), before);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void reportedSubkeyCountIsBoundedBeforeEnumeration() {
        Fixture f;
        f.state->mappings->reported_count = Inventory::maximum_objects + 1;
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(root.error != 0);
        QCOMPARE(f.state->count(QStringLiteral("enum")), std::size_t{});
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void objectLimitIncludesRootsButNotSharedNativeAncestors() {
        Fixture f;
        for (std::uint32_t i = 0; i < Inventory::maximum_objects; ++i)
            f.state->add(f.state->mappings, QStringLiteral("child%1").arg(i));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Present);
        for (std::uint32_t i = 0; i < Inventory::maximum_objects - 1; ++i)
            QCOMPARE(reader->nextChild(root.ref, i, 255).state, Inventory::ChildState::Child);
        const auto last = reader->nextChild(root.ref, Inventory::maximum_objects - 1, 255);
        QCOMPARE(last.state, Inventory::ChildState::Unknown);
        QVERIFY(last.error != 0);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void depthLimitIsMeasuredFromTheCapturedRoot() {
        Fixture f;
        auto key = f.state->mappings;
        for (std::uint32_t depth = 0; depth < Inventory::maximum_depth + 1; ++depth)
            key = f.state->add(key, QStringLiteral("nested"));
        auto reader = f.make();
        auto current = reader->root(Inventory::Root::MappingsSID).ref;
        QVERIFY(current.value);
        for (std::uint32_t depth = 0; depth < Inventory::maximum_depth; ++depth) {
            const auto child = reader->nextChild(current, 0, 255);
            QCOMPARE(child.state, Inventory::ChildState::Child);
            current = child.ref;
        }
        const auto child = reader->nextChild(current, 0, 255);
        QCOMPARE(child.state, Inventory::ChildState::Unknown);
        QVERIFY(child.error != 0);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void securityBudgetIncludesOwnerCopyAndRejectsBeforeReadingOversizeDescriptor() {
        Fixture f;
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        const auto small = reader->readMetadata(root.ref, static_cast<std::uint64_t>(descriptor().size()));
        QVERIFY(small.error != 0);
        QVERIFY(small.raw_sd.isEmpty());
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        Fixture g;
        g.state->mappings->sd = QByteArray(Inventory::maximum_sd_bytes + 1, '\0');
        auto oversized = g.make();
        const auto next = oversized->root(Inventory::Root::MappingsSID);
        QVERIFY(oversized->readMetadata(next.ref, Inventory::maximum_total_bytes).error != 0);
        QCOMPARE(g.state->security_reads, 0U);
        QCOMPARE(oversized->close().state, Inventory::CloseState::Closed);
    }
    void totalCacheBudgetBoundsNativeNamesAndSecurityTogether() {
        Fixture f;
        f.state->mappings->sd.append(QByteArray(Inventory::maximum_sd_bytes - descriptor().size(), '\0'));
        for (unsigned i = 0; i < 80; ++i) {
            auto key = f.state->add(f.state->mappings, QStringLiteral("child%1").arg(i));
            key->sd.append(QByteArray(Inventory::maximum_sd_bytes - descriptor().size(), '\0'));
        }
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(reader->readMetadata(root.ref, Inventory::maximum_sd_bytes + owner().size()).error, 0U);
        bool stopped = false;
        for (unsigned i = 0; i < 80; ++i) {
            const auto child = reader->nextChild(root.ref, i, 255);
            if (child.state != Inventory::ChildState::Child) {
                stopped = true;
                break;
            }
            const auto metadata = reader->readMetadata(child.ref, Inventory::maximum_sd_bytes + owner().size());
            if (metadata.error != 0) {
                stopped = true;
                break;
            }
        }
        QVERIFY(stopped);
        QVERIFY(f.state->security_reads < 80);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void unknownRefsAndInvalidCallerBoundsDoNotInvokeThePort() {
        Fixture f;
        auto reader = f.make();
        const auto before = f.state->operations.size();
        QVERIFY(reader->readMetadata({999}, 100).error != 0);
        QVERIFY(reader->nextChild({999}, 0, 255).error != 0);
        QCOMPARE(f.state->operations.size(), before);
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Present);
        const auto after = f.state->operations.size();
        QVERIFY(reader->readMetadata(root.ref, 0).error != 0);
        QVERIFY(reader->nextChild(root.ref, 0, 0).error != 0);
        QVERIFY(reader->nextChild(root.ref, 0, 256).error != 0);
        QCOMPARE(f.state->operations.size(), after);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void signaledAndUnknownWatchesAreStickyAndNeverRearmed_data() {
        QTest::addColumn<int>("eventState");
        QTest::addColumn<int>("validation");
        QTest::newRow("changed") << int(Registry::EventState::Changed) << int(Inventory::Validation::Changed);
        QTest::newRow("unknown") << int(Registry::EventState::Unknown) << int(Inventory::Validation::Unknown);
    }
    void signaledAndUnknownWatchesAreStickyAndNeverRearmed() {
        QFETCH(int, eventState);
        QFETCH(int, validation);
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        const auto watches = f.state->count(QStringLiteral("watch"));
        f.state->mappings->event_state = static_cast<Registry::EventState>(eventState);
        QCOMPARE(reader->revalidate().state, static_cast<Inventory::Validation>(validation));
        f.state->mappings->event_state = Registry::EventState::Unchanged;
        QCOMPARE(reader->revalidate().state, static_cast<Inventory::Validation>(validation));
        QCOMPARE(f.state->count(QStringLiteral("watch")), watches);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void parentWatchAndNativeNameAreCheckedAroundChildReads_data() {
        QTest::addColumn<QString>("mutation");
        for (const auto* value : {"watch", "name", "last-write", "count", "reserved", "security", "enum-name"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void parentWatchAndNativeNameAreCheckedAroundChildReads() {
        QFETCH(QString, mutation);
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(reader->readMetadata(root.ref, Inventory::maximum_sd_bytes).error, 0U);
        QCOMPARE(reader->nextChild(root.ref, 0, 255).state, Inventory::ChildState::Child);
        QCOMPARE(reader->nextChild(root.ref, 1, 255).state, Inventory::ChildState::End);
        if (mutation == QStringLiteral("watch"))
            f.state->software->event_state = Registry::EventState::Changed;
        else if (mutation == QStringLiteral("name"))
            f.state->software->native_name += QStringLiteral("Renamed");
        else if (mutation == QStringLiteral("last-write"))
            ++f.state->mappings->last_write;
        else if (mutation == QStringLiteral("count"))
            f.state->mappings->reported_count = 2;
        else if (mutation == QStringLiteral("reserved"))
            f.state->mappings->reserved = Registry::ReservedValue::Present;
        else if (mutation == QStringLiteral("security"))
            f.state->mappings->sd[3] = static_cast<char>(static_cast<unsigned char>(f.state->mappings->sd[3]) & ~0x10U);
        else
            f.state->mappings->children.front()->name = QStringLiteral("replacement");
        QVERIFY(reader->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void mutationsDuringInfoEnumAndSecurityAreDetectedBeforeReturningSuccess_data() {
        QTest::addColumn<QString>("operation");
        for (const auto* value : {"info", "enum", "security_read"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void mutationsDuringInfoEnumAndSecurityAreDetectedBeforeReturningSuccess() {
        QFETCH(QString, operation);
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Present);
        bool fired = false;
        f.state->hook = [state = f.state, operation, &fired](const auto& kind, const auto handle) {
            if (!fired && kind == operation && state->keys.contains(handle) &&
                state->keys.at(handle) == state->mappings) {
                fired = true;
                state->mappings->event_state = Registry::EventState::Changed;
            }
        };
        if (operation == QStringLiteral("enum"))
            QCOMPARE(reader->nextChild(root.ref, 0, 255).state, Inventory::ChildState::Unknown);
        else
            QVERIFY(reader->readMetadata(root.ref, Inventory::maximum_sd_bytes).error != 0);
        QVERIFY(fired);
        QCOMPARE(reader->revalidate().state, Inventory::Validation::Changed);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void rawPollErrorsRemainUnknown() {
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        f.state->mappings->poll_error = 5;
        const auto validation = reader->revalidate();
        QCOMPARE(validation.state, Inventory::Validation::Unknown);
        QCOMPARE(validation.error, 5U);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void callbacksThrowIntoUnknownAndNeverEscapeNoexcept_data() {
        QTest::addColumn<QString>("operation");
        for (const auto* value : {"open", "info", "enum", "watch", "poll", "security"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void callbacksThrowIntoUnknownAndNeverEscapeNoexcept() {
        QFETCH(QString, operation);
        Fixture f;
        f.state->add(f.state->mappings, QStringLiteral("child"));
        auto reader = f.make();
        if (operation == QStringLiteral("open"))
            f.state->throw_open = true;
        else if (operation == QStringLiteral("info"))
            f.state->throw_info = true;
        else if (operation == QStringLiteral("watch"))
            f.state->throw_watch = true;
        if (f.state->throw_open || f.state->throw_info || f.state->throw_watch) {
            QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Unknown);
        } else {
            const auto root = reader->root(Inventory::Root::MappingsSID);
            QCOMPARE(root.presence, Inventory::Presence::Present);
            if (operation == QStringLiteral("enum")) {
                f.state->throw_enum = true;
                QCOMPARE(reader->nextChild(root.ref, 0, 255).state, Inventory::ChildState::Unknown);
            } else if (operation == QStringLiteral("poll")) {
                f.state->throw_poll = true;
                QCOMPARE(reader->revalidate().state, Inventory::Validation::Unknown);
            } else {
                f.state->throw_security = true;
                QVERIFY(reader->readMetadata(root.ref, Inventory::maximum_sd_bytes).error != 0);
            }
        }
        QVERIFY(reader->revalidate().state != Inventory::Validation::Unchanged);
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void everyOwnedKeyReturnedOnErrorIsStillClosedExactlyOnce() {
        Fixture f;
        std::array<Registry::Handle, 2> transferred{};
        f.state->open_override = [state = f.state, &transferred](const auto parent,
                                                                 const auto&) -> std::optional<Registry::OpenResult> {
            if (parent)
                return {};
            transferred = {state->own(state->software), state->own(state->software)};
            return Registry::OpenResult{Registry::OpenState::Unknown, 5, 0, transferred, 2};
        };
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).error, 5U);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.state->closed_keys, (std::vector{transferred[1], transferred[0]}));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.state->closed_keys.size(), std::size_t{2});
    }
    void eventReturnedWithWatchErrorIsRetainedForExplicitClose() {
        Fixture f;
        auto binding = calls(f.state);
        const auto watch = binding.watch_once;
        Registry::Handle returned{};
        binding.watch_once = [watch, &returned](const auto key) {
            auto result = watch(key);
            returned = result.event;
            result.error = 5;
            return result;
        };
        Registry reader{paths(), std::move(binding)};
        QCOMPARE(reader.root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Unknown);
        QVERIFY(returned);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader.close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.state->closed_events, (std::vector{returned}));
    }
    void infoRawErrorIsPreservedAndNoSecurityDataIsRead() {
        Fixture f;
        f.state->mappings->info_error = 5;
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QCOMPARE(root.error, 5U);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void invalidOwnershipFramesNeverProduceAWorkKey_data() {
        QTest::addColumn<QString>("mode");
        for (const auto* value : {"missing-work", "duplicate-owned", "too-many", "zero-key", "invalid-handle"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void invalidOwnershipFramesNeverProduceAWorkKey() {
        QFETCH(QString, mode);
        Fixture f;
        f.state->open_override = [state = f.state, mode](const auto parent,
                                                         const auto&) -> std::optional<Registry::OpenResult> {
            if (parent)
                return {};
            auto result = state->present(state->software);
            if (mode == QStringLiteral("missing-work"))
                result.key = 456;
            else if (mode == QStringLiteral("duplicate-owned")) {
                result.owned_keys[1] = result.key;
                result.owned_count = 2;
            } else if (mode == QStringLiteral("too-many")) {
                result.owned_keys[1] = state->own(state->software);
                result.owned_count = 3;
            } else if (mode == QStringLiteral("zero-key"))
                result.key = 0;
            else
                result.key = std::numeric_limits<Registry::Handle>::max();
            return result;
        };
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(root.error != 0);
        QCOMPARE(f.state->count(QStringLiteral("watch")), std::size_t{});
        QCOMPARE(f.state->count(QStringLiteral("info")), std::size_t{});
        QVERIFY(noReads(*f.state));
        const auto closure = reader->close();
        QVERIFY(closure.state != Inventory::CloseState::NotAttempted);
        QCOMPARE(std::set(f.state->closed_keys.begin(), f.state->closed_keys.end()).size(),
                 f.state->closed_keys.size());
        if (mode == QStringLiteral("too-many"))
            QCOMPARE(closure.state, Inventory::CloseState::Unknown);
        if (mode == QStringLiteral("missing-work"))
            QCOMPARE(closure.state, Inventory::CloseState::Unknown);
        QCOMPARE(f.state->closed_keys.size(), f.state->opened.size());
    }
    void unlistedWorkKeyCannotAcknowledgeClosureOrCloseAnUnownedHandle_data() {
        QTest::addColumn<int>("state");
        QTest::addColumn<unsigned>("error");
        QTest::newRow("present-work-not-transferred") << int(Registry::OpenState::Present) << 0U;
        QTest::newRow("unknown-error-work-not-transferred") << int(Registry::OpenState::Unknown) << 5U;
    }
    void unlistedWorkKeyCannotAcknowledgeClosureOrCloseAnUnownedHandle() {
        QFETCH(int, state);
        QFETCH(unsigned, error);
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        constexpr Registry::Handle unlisted = 777777;
        f.state->open_override = [held = f.state, state,
                                  error](const auto parent, const auto& name) -> std::optional<Registry::OpenResult> {
            if (!parent || name != sid || held->keys.at(parent)->name != QStringLiteral("Storage"))
                return {};
            const auto known = held->own(held->storage);
            return Registry::OpenResult{static_cast<Registry::OpenState>(state), error, unlisted, {known, 0}, 1};
        };
        const auto root = reader->root(Inventory::Root::StorageSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(root.error != 0);
        const auto closure = reader->close();
        QCOMPARE(closure.state, Inventory::CloseState::Unknown);
        QVERIFY(closure.error != 0);
        QCOMPARE(f.state->closed_keys.size(), f.state->opened.size());
        QVERIFY(std::find(f.state->closed_keys.begin(), f.state->closed_keys.end(), unlisted) ==
                f.state->closed_keys.end());
        QVERIFY(f.state->closed_events.empty());
        const auto count = f.state->operations.size();
        QCOMPARE(reader->close().state, Inventory::CloseState::Unknown);
        reader.reset();
        QCOMPARE(f.state->operations.size(), count);
    }
    void missingFramesRequireNoTransferredHandlesAndAnAbsenceError_data() {
        QTest::addColumn<bool>("transferred");
        QTest::addColumn<unsigned>("error");
        QTest::newRow("presence-handle-is-not-absence") << true << 2U;
        QTest::newRow("access-denied-is-not-absence") << false << 5U;
    }
    void missingFramesRequireNoTransferredHandlesAndAnAbsenceError() {
        QFETCH(bool, transferred);
        QFETCH(unsigned, error);
        Fixture f;
        f.state->open_override = [held = f.state, transferred,
                                  error](const auto parent, const auto& name) -> std::optional<Registry::OpenResult> {
            if (!parent || name != sid)
                return {};
            Registry::OpenResult result{Registry::OpenState::Missing, error};
            if (transferred) {
                result.owned_keys[0] = held->own(held->mappings);
                result.owned_count = 1;
            }
            return result;
        };
        auto reader = f.make();
        const auto root = reader->root(Inventory::Root::MappingsSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QVERIFY(root.error != 0);
        QVERIFY(!root.ref.value);
        QVERIFY(noReads(*f.state));
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.state->closed_keys.size(), f.state->opened.size());
    }
    void handlesOutsideDeclaredOwnedCountCannotAcknowledgeClosure_data() {
        QTest::addColumn<unsigned>("count");
        QTest::newRow("zero-declared-with-slot-zero") << 0U;
        QTest::newRow("one-declared-with-slot-one") << 1U;
    }
    void handlesOutsideDeclaredOwnedCountCannotAcknowledgeClosure() {
        QFETCH(unsigned, count);
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        constexpr Registry::Handle undeclared = 888888;
        f.state->open_override = [held = f.state, count](const auto parent,
                                                         const auto& name) -> std::optional<Registry::OpenResult> {
            if (!parent || name != sid || held->keys.at(parent)->name != QStringLiteral("Storage"))
                return {};
            Registry::OpenResult result{Registry::OpenState::Unknown, 5};
            result.owned_count = count;
            if (count)
                result.owned_keys[0] = held->own(held->storage);
            result.owned_keys[count] = undeclared;
            return result;
        };
        const auto root = reader->root(Inventory::Root::StorageSID);
        QCOMPARE(root.presence, Inventory::Presence::Unknown);
        QCOMPARE(root.error, 5U);
        QCOMPARE(reader->close().state, Inventory::CloseState::Unknown);
        QCOMPARE(f.state->closed_keys.size(), f.state->opened.size());
        QVERIFY(std::find(f.state->closed_keys.begin(), f.state->closed_keys.end(), undeclared) ==
                f.state->closed_keys.end());
        QVERIFY(f.state->closed_events.empty());
        const auto before = f.state->operations.size();
        QCOMPARE(reader->close().state, Inventory::CloseState::Unknown);
        reader.reset();
        QCOMPARE(f.state->operations.size(), before);
    }
    void explicitCloseClosesKeysInReverseAcquisitionOrderBeforeEvents() {
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        QCOMPARE(reader->root(Inventory::Root::StorageSID).presence, Inventory::Presence::Present);
        auto expected = f.state->opened;
        std::reverse(expected.begin(), expected.end());
        const auto result = reader->close();
        QCOMPARE(result.state, Inventory::CloseState::Closed);
        QCOMPARE(result.error, 0U);
        QCOMPARE(f.state->closed_keys, expected);
        QCOMPARE(f.state->closed_events.size(), f.state->events.size());
        bool events_started = false;
        for (const auto& op : f.state->operations) {
            if (op.kind == QStringLiteral("close_event"))
                events_started = true;
            if (op.kind == QStringLiteral("close_key"))
                QVERIFY(!events_started);
        }
        const auto count = f.state->operations.size();
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        QCOMPARE(f.state->operations.size(), count);
        QVERIFY(reader->readMetadata({1}, 100).error != 0);
        QVERIFY(reader->root(Inventory::Root::MappingsSID).error != 0);
        QCOMPARE(reader->revalidate().state, Inventory::Validation::Unknown);
        QCOMPARE(f.state->operations.size(), count);
    }
    void anyKeyCloseFailureRetainsAllEventsAndNeverRetries_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("raw-error") << false;
        QTest::newRow("exception") << true;
    }
    void anyKeyCloseFailureRetainsAllEventsAndNeverRetries() {
        QFETCH(bool, throws);
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        const auto failed = f.state->opened.back();
        f.state->key_close_override = [=](const auto key) {
            if (key == failed) {
                if (throws)
                    throw std::runtime_error{"close"};
                return 5U;
            }
            return 0U;
        };
        const auto closure = reader->close();
        QCOMPARE(closure.state, Inventory::CloseState::Unknown);
        QVERIFY(closure.error != 0);
        QCOMPARE(f.state->closed_keys.size(), f.state->opened.size());
        QVERIFY(f.state->closed_events.empty());
        const auto count = f.state->operations.size();
        QCOMPARE(reader->close().state, Inventory::CloseState::Unknown);
        reader.reset();
        QCOMPARE(f.state->operations.size(), count);
    }
    void eventCloseFailureIsUnknownAndNeverRetried() {
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        f.state->event_close_override = [](const auto) { return 6U; };
        const auto closure = reader->close();
        QCOMPARE(closure.state, Inventory::CloseState::Unknown);
        QCOMPARE(closure.error, 6U);
        QCOMPARE(f.state->closed_keys.size(), f.state->opened.size());
        const auto count = f.state->operations.size();
        QCOMPARE(reader->close().state, Inventory::CloseState::Unknown);
        reader.reset();
        QCOMPARE(f.state->operations.size(), count);
    }
    void destructionWithoutExplicitCloseDoesNotCallThePortOrAcknowledge() {
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        const auto count = f.state->operations.size();
        reader.reset();
        QCOMPARE(f.state->operations.size(), count);
        QVERIFY(f.state->closed_keys.empty());
        QVERIFY(f.state->closed_events.empty());
    }
    void reentrantOperationsAreBusyWithoutCallingThePort() {
        Fixture f;
        auto reader = f.make();
        Inventory::NodeRef current{};
        bool active = false, exercised = false;
        f.state->hook = [&](const auto&, const auto) {
            if (active)
                return;
            active = true;
            exercised = true;
            const auto before = f.state->operations.size();
            QCOMPARE(reader->root(Inventory::Root::StorageSID).error, busy);
            QCOMPARE(reader->readMetadata(current, 100).error, busy);
            QCOMPARE(reader->nextChild(current, 0, 255).error, busy);
            QCOMPARE(reader->revalidate().error, busy);
            const auto closure = reader->close();
            QCOMPARE(closure.state, Inventory::CloseState::NotAttempted);
            QCOMPARE(closure.error, busy);
            QCOMPARE(f.state->operations.size(), before);
            active = false;
        };
        const auto root = reader->root(Inventory::Root::MappingsSID);
        current = root.ref;
        QCOMPARE(root.presence, Inventory::Presence::Present);
        QVERIFY(exercised);
        QCOMPARE(reader->readMetadata(root.ref, Inventory::maximum_sd_bytes).error, 0U);
        f.state->hook = {};
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
    }
    void reentrantCloseWhileClosingIsBusyAndDoesNotDuplicateCloses() {
        Fixture f;
        auto reader = f.make();
        QCOMPARE(reader->root(Inventory::Root::MappingsSID).presence, Inventory::Presence::Present);
        unsigned nested = 0;
        f.state->hook = [&](const auto& kind, const auto) {
            if (!kind.startsWith(QStringLiteral("close")))
                return;
            ++nested;
            const auto before = f.state->operations.size();
            QCOMPARE(reader->close().state, Inventory::CloseState::NotAttempted);
            QCOMPARE(reader->root(Inventory::Root::MappingsSID).error, busy);
            QCOMPARE(f.state->operations.size(), before);
        };
        QCOMPARE(reader->close().state, Inventory::CloseState::Closed);
        QVERIFY(nested);
        QCOMPARE(f.state->closed_keys.size(), f.state->opened.size());
    }
};

QTEST_APPLESS_MAIN(AppContainerStorageRegistryTests)
#include "AppContainerStorageRegistryTests.moc"

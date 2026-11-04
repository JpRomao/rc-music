#include "AppContainerStorageRegistry.hpp"

#include <QStringList>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace music_player::sandbox_probe {
namespace {
using Registry = AppContainerStorageRegistry;
using Inventory = Registry::Inventory;
using Security = AppContainerStorageSecurity;
constexpr std::uint32_t invalid_data = 13, busy = 170, more_data = 234;
constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();
constexpr std::size_t maximum_bindings = Registry::maximum_anchors + Inventory::maximum_objects;
constexpr std::size_t maximum_keys = 2 * maximum_bindings;
struct Busy final {
    bool& value;
    explicit Busy(bool& held) : value(held) { value = true; }
    ~Busy() { value = false; }
};
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
bool validText(const QString& value) {
    if (value.isEmpty() || value.contains(QChar{}))
        return false;
    for (qsizetype i = 0; i < value.size(); ++i) {
        if (value[i].isHighSurrogate()) {
            if (++i >= value.size() || !value[i].isLowSurrogate())
                return false;
        } else if (value[i].isLowSurrogate())
            return false;
    }
    return true;
}
bool component(const QString& value) {
    return value.size() <= Inventory::maximum_name_units && validText(value) && value != QStringLiteral(".") &&
           value != QStringLiteral("..") && !value.contains(QLatin1Char('\\')) && !value.contains(QLatin1Char('/'));
}
bool same(const QString& first, const QString& second) { return first.compare(second, Qt::CaseInsensitive) == 0; }
bool canonicalSid(const QString& value) {
    const auto parts = value.split(QLatin1Char('-'));
    if (parts.size() != 11 || parts[0] != QStringLiteral("S") || parts[1] != QStringLiteral("1") ||
        parts[2] != QStringLiteral("15") || parts[3] != QStringLiteral("2"))
        return false;
    for (qsizetype i = 4; i < parts.size(); ++i) {
        const auto& part = parts[i];
        if (part.isEmpty() || part.size() > 10 || (part.size() > 1 && part.front() == QLatin1Char('0')))
            return false;
        std::uint64_t number = 0;
        for (const auto digit : part) {
            if (digit < QLatin1Char('0') || digit > QLatin1Char('9'))
                return false;
            number = number * 10 + digit.unicode() - '0';
            if (number > std::numeric_limits<std::uint32_t>::max())
                return false;
        }
    }
    return true;
}
bool validRoots(const Registry::RootPaths& paths) {
    const auto prefix = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\");
    const auto mappings = prefix + QStringLiteral("Mappings\\"), storage = prefix + QStringLiteral("Storage\\");
    if (!paths.mappings_key.startsWith(mappings) || !paths.storage_key.startsWith(storage))
        return false;
    const auto sid = paths.mappings_key.mid(mappings.size());
    return canonicalSid(sid) && paths.storage_key.mid(storage.size()) == sid;
}
bool validHandle(const Registry::Handle value) {
    return value && value != std::numeric_limits<Registry::Handle>::max();
}
Inventory::Metadata failedMetadata(const Inventory::NodeRef ref, const std::uint32_t error) {
    Inventory::Metadata result;
    result.ref = ref;
    result.error = error;
    return result;
}
} // namespace

struct AppContainerStorageRegistry::Impl final {
    struct Binding final {
        Handle key{}, event{};
        std::size_t parent{absent};
        QString component{};
        KeyInfo info{};
        bool witness{false};
    };
    struct Node final {
        std::size_t binding{absent};
        std::uint32_t depth{};
        Inventory::Metadata metadata{};
        bool captured{false}, ended{false};
        std::vector<Inventory::ChildResult> children{};
    };
    RootPaths roots;
    Calls calls;
    Security security;
    std::array<Handle, maximum_keys> keys{};
    std::array<Handle, maximum_bindings> events{};
    std::array<Binding, maximum_bindings> bindings{};
    std::array<Node, Inventory::maximum_objects> nodes{};
    std::size_t key_count{}, event_count{}, binding_count{}, node_count{}, software{absent};
    std::array<Inventory::RootResult, 2> root_results{};
    std::array<bool, 2> root_attempted{};
    std::uint64_t cached_bytes{};
    bool valid_roots{}, working{}, ownership_uncertain{}, observed{};
    Inventory::ValidationResult validation{Inventory::Validation::Unchanged, 0};
    Inventory::CloseResult closure{};

    Impl(RootPaths value, Calls bindings_calls)
        : roots{owned(value.mappings_key), owned(value.storage_key)}, calls(std::move(bindings_calls)),
          security(calls.security), valid_roots(validRoots(roots)) {
        cached_bytes = 2 * static_cast<std::uint64_t>(roots.mappings_key.size() + roots.storage_key.size());
    }
    void unknown(const std::uint32_t code = invalid_data) {
        if (validation.state == Inventory::Validation::Unchanged)
            validation = {Inventory::Validation::Unknown, code ? code : invalid_data};
    }
    void changed() {
        if (validation.state == Inventory::Validation::Unchanged)
            validation = {Inventory::Validation::Changed, 0};
    }
    std::uint32_t error() const { return validation.error ? validation.error : invalid_data; }
    bool reserve(const std::uint64_t bytes) {
        if (cached_bytes > Inventory::maximum_total_bytes || bytes > Inventory::maximum_total_bytes - cached_bytes) {
            unknown(more_data);
            return false;
        }
        cached_bytes += bytes;
        return true;
    }
    bool available() const { return closure.state == Inventory::CloseState::NotAttempted; }
    bool takeKey(const Handle key) {
        if (!validHandle(key) || key_count == keys.size() ||
            std::find(keys.begin(), keys.begin() + key_count, key) != keys.begin() + key_count) {
            ownership_uncertain = true;
            return false;
        }
        keys[key_count++] = key;
        return true;
    }
    bool takeEvent(const Handle event) {
        if (!validHandle(event) || event_count == events.size() ||
            std::find(events.begin(), events.begin() + event_count, event) != events.begin() + event_count) {
            ownership_uncertain = true;
            return false;
        }
        events[event_count++] = event;
        return true;
    }
    bool takeOpen(const OpenResult& result) {
        const auto count = std::min<std::size_t>(result.owned_count, result.owned_keys.size());
        bool valid = result.owned_count <= result.owned_keys.size();
        for (std::size_t i = 0; i < count; ++i)
            valid = takeKey(result.owned_keys[i]) && valid;
        for (std::size_t i = count; i < result.owned_keys.size(); ++i)
            if (result.owned_keys[i]) {
                ownership_uncertain = true;
                valid = false;
            }
        if (result.owned_count > result.owned_keys.size())
            ownership_uncertain = true;
        const bool listed = std::find(result.owned_keys.begin(), result.owned_keys.begin() + count, result.key) !=
                            result.owned_keys.begin() + count;
        // A work handle outside the transfer list is not ours to close. Its
        // unexplained presence also prevents acknowledging complete closure,
        // including when the open returned an error or an Unknown state.
        if (result.key && (!validHandle(result.key) || !listed)) {
            ownership_uncertain = true;
            valid = false;
        }
        if (result.state == OpenState::Present && (!validHandle(result.key) || !listed))
            valid = false;
        if (!valid)
            unknown(result.error);
        return valid;
    }
    bool poll() {
        if (validation.state != Inventory::Validation::Unchanged)
            return false;
        if (!calls.poll_event) {
            unknown();
            return false;
        }
        for (std::size_t i = 0; i < event_count; ++i) {
            const auto result = calls.poll_event(events[i]);
            if (result.error || result.state == EventState::Unknown) {
                unknown(result.error);
                return false;
            }
            if (result.state == EventState::Changed) {
                changed();
                return false;
            }
            if (result.state != EventState::Unchanged) {
                unknown();
                return false;
            }
        }
        return true;
    }
    bool normalInfo(const KeyInfo& info, const bool witness) {
        if (info.error) {
            unknown(info.error);
            return false;
        }
        if (info.native_name.size() > maximum_native_name_units || !validText(info.native_name) ||
            !info.native_name.startsWith(QStringLiteral("\\REGISTRY\\"), Qt::CaseInsensitive)) {
            unknown(info.native_name.size() > maximum_native_name_units ? more_data : invalid_data);
            return false;
        }
        if (!witness && (info.reserved != ReservedValue::Absent || info.subkeys > Inventory::maximum_objects)) {
            unknown(info.subkeys > Inventory::maximum_objects ? more_data : invalid_data);
            return false;
        }
        return true;
    }
    bool equalInfo(const KeyInfo& first, const KeyInfo& second, const bool witness) {
        return same(first.native_name, second.native_name) &&
               (witness || (first.subkeys == second.subkeys && first.last_write == second.last_write &&
                            first.reserved == second.reserved && first.reserved_type == second.reserved_type &&
                            first.reserved_bytes == second.reserved_bytes));
    }
    bool guard() {
        if (!poll())
            return false;
        if (!calls.query_info) {
            unknown();
            return false;
        }
        for (std::size_t i = 0; i < binding_count; ++i) {
            const auto& binding = bindings[i];
            const auto current = calls.query_info(binding.key, maximum_native_name_units);
            if (!normalInfo(current, binding.witness))
                return false;
            if (!equalInfo(binding.info, current, binding.witness)) {
                changed();
                return false;
            }
            if (binding.parent != absent && !same(current.native_name, bindings[binding.parent].info.native_name +
                                                                           QLatin1Char('\\') + binding.component)) {
                changed();
                return false;
            }
        }
        return poll();
    }
    std::size_t bind(const Handle key, const std::size_t parent, const QString& name, const bool witness,
                     const QString& expected = {}) {
        if (binding_count == bindings.size() || !calls.watch_once || !calls.query_info) {
            unknown();
            return absent;
        }
        const auto watch = calls.watch_once(key);
        bool event_valid = true;
        if (watch.event)
            event_valid = takeEvent(watch.event);
        if (watch.error || !watch.event || !event_valid) {
            unknown(watch.error);
            return absent;
        }
        if (!poll())
            return absent;
        auto info = calls.query_info(key, maximum_native_name_units);
        if (!normalInfo(info, witness))
            return absent;
        // Copy before the next callback, including a fromRawData result.
        info.native_name = owned(info.native_name);
        const auto wanted = parent != absent ? bindings[parent].info.native_name + QLatin1Char('\\') + name : expected;
        if ((!wanted.isEmpty() && !same(info.native_name, wanted)) ||
            (wanted.isEmpty() && !info.native_name.endsWith(QStringLiteral("\\Software"), Qt::CaseInsensitive))) {
            unknown();
            return absent;
        }
        if (!reserve(2 * static_cast<std::uint64_t>(info.native_name.size() + name.size())))
            return absent;
        const auto index = binding_count++;
        bindings[index] = {key, watch.event, parent, owned(name), std::move(info), witness};
        return guard() ? index : absent;
    }
    std::size_t softwareAnchor() {
        if (software != absent)
            return guard() ? software : absent;
        if (!calls.open_software) {
            unknown();
            return absent;
        }
        const auto opened = calls.open_software();
        if (!takeOpen(opened))
            return absent;
        if (opened.error || opened.state != OpenState::Present) {
            unknown(opened.error);
            return absent;
        }
        QString expected;
        if (opened.owned_count == 2) {
            if (opened.key != opened.owned_keys[1]) {
                unknown();
                return absent;
            }
            const auto witness = bind(opened.owned_keys[0], absent, {}, true);
            if (witness == absent)
                return absent;
            expected = owned(bindings[witness].info.native_name);
        }
        software = bind(opened.key, absent, {}, false, expected);
        return software;
    }
    struct Opened final {
        std::size_t binding{absent};
        OpenState state{OpenState::Unknown};
        std::uint32_t error{invalid_data};
    };
    Opened child(const std::size_t parent, const QString& name) {
        if (!component(name) || !guard() || !calls.open_child) {
            unknown();
            return {absent, OpenState::Unknown, error()};
        }
        for (std::size_t i = 0; i < binding_count; ++i)
            if (!bindings[i].witness && bindings[i].parent == parent && same(bindings[i].component, name))
                return {i, OpenState::Present, 0};
        const auto opened = calls.open_child(bindings[parent].key, name);
        if (!takeOpen(opened))
            return {absent, OpenState::Unknown, error()};
        // A parent's own rename may not notify that key. Recheck its captured
        // native name before a newly opened child supplies any metadata.
        if (!guard())
            return {absent, OpenState::Unknown, error()};
        if (opened.state == OpenState::Missing && (opened.error == 2 || opened.error == 3) && !opened.key &&
            !opened.owned_count)
            return {absent, OpenState::Missing, opened.error};
        if (opened.error || opened.state != OpenState::Present) {
            unknown(opened.error);
            return {absent, OpenState::Unknown, opened.error ? opened.error : invalid_data};
        }
        const auto result = bind(opened.key, parent, name, false);
        return {result, result == absent ? OpenState::Unknown : OpenState::Present, result == absent ? error() : 0};
    }
    Inventory::NodeRef node(const std::size_t binding, const std::uint32_t depth) {
        for (std::size_t i = 0; i < node_count; ++i)
            if (nodes[i].binding == binding)
                return {i + 1};
        if (node_count == nodes.size() || depth > Inventory::maximum_depth) {
            unknown(more_data);
            return {};
        }
        const auto index = node_count++;
        nodes[index].binding = binding;
        nodes[index].depth = depth;
        return {index + 1};
    }
    Node* find(const Inventory::NodeRef ref) {
        return ref.value && ref.value <= node_count ? &nodes[static_cast<std::size_t>(ref.value - 1)] : nullptr;
    }
    Inventory::RootResult readRoot(const Inventory::Root root) {
        const auto slot = root == Inventory::Root::MappingsSID ? 0U : 1U;
        if (root_attempted[slot]) {
            if (!guard())
                return {{}, error(), Inventory::Presence::Unknown};
            return root_results[slot];
        }
        observed = true;
        auto current = softwareAnchor();
        if (current == absent)
            return {{}, error(), Inventory::Presence::Unknown};
        const auto components = (slot ? roots.storage_key : roots.mappings_key).split(QLatin1Char('\\'));
        std::uint32_t ancestors = 1;
        for (qsizetype i = 1; i < components.size(); ++i) {
            if (++ancestors > maximum_anchors) {
                unknown(more_data);
                return {{}, error(), Inventory::Presence::Unknown};
            }
            const auto opened = child(current, components[i]);
            if (opened.state == OpenState::Missing) {
                root_attempted[slot] = true;
                return root_results[slot] = {{}, opened.error, Inventory::Presence::Missing};
            }
            if (opened.binding == absent)
                return {{}, opened.error, Inventory::Presence::Unknown};
            current = opened.binding;
        }
        const auto ref = node(current, 0);
        if (!ref.value)
            return {{}, error(), Inventory::Presence::Unknown};
        root_attempted[slot] = true;
        return root_results[slot] = {ref, 0, Inventory::Presence::Present};
    }
    Inventory::Metadata metadata(const Inventory::NodeRef ref, Node& node, const std::uint64_t maximum_bytes) {
        if (!guard())
            return failedMetadata(ref, error());
        if (node.captured) {
            if (static_cast<std::uint64_t>(node.metadata.raw_sd.size() + node.metadata.owner_sid.size()) >
                maximum_bytes)
                return failedMetadata(ref, more_data);
            return node.metadata;
        }
        const auto available_bytes = Inventory::maximum_total_bytes - cached_bytes;
        const auto captured =
            security.read(Security::Object::Key, bindings[node.binding].key, std::min(maximum_bytes, available_bytes));
        if (captured.error != Security::Error::None) {
            const auto code = captured.read_error != Inventory::not_called && captured.read_error ? captured.read_error
                              : captured.query_error != Inventory::not_called && captured.query_error != 122
                                  ? captured.query_error
                                  : invalid_data;
            unknown(code);
            return failedMetadata(ref, code);
        }
        if (!guard())
            return failedMetadata(ref, error());
        if (!reserve(static_cast<std::uint64_t>(captured.raw_sd.size() + captured.owner_sid.size())))
            return failedMetadata(ref, error());
        node.metadata = {.ref = ref,
                         .identity = {2, ref.value},
                         .presence = Inventory::Presence::Present,
                         .type = Inventory::Type::RegistryKey,
                         .error = 0,
                         .owner_sid = captured.owner_sid,
                         .raw_sd = captured.raw_sd,
                         .dacl = static_cast<Inventory::Dacl>(captured.dacl),
                         .dacl_protected = captured.dacl_protected,
                         .sacl_not_queried = captured.sacl_not_queried};
        node.captured = true;
        return node.metadata;
    }
    Inventory::ChildResult next(Node& node, const std::uint32_t index, const std::uint32_t units) {
        if (index > node.children.size())
            return {Inventory::ChildState::Unknown, invalid_data};
        if (!guard())
            return {Inventory::ChildState::Unknown, error()};
        if (index < node.children.size()) {
            if (node.children[index].name.size() > units)
                return {Inventory::ChildState::Unknown, more_data};
            return node.children[index];
        }
        if (node.ended)
            return {Inventory::ChildState::End, 0};
        if (!calls.enum_child) {
            unknown();
            return {Inventory::ChildState::Unknown, error()};
        }
        auto result = calls.enum_child(bindings[node.binding].key, index, units);
        if (result.error) {
            unknown(result.error);
            return {Inventory::ChildState::Unknown, result.error};
        }
        if (result.name.size() > units) {
            unknown(more_data);
            return {Inventory::ChildState::Unknown, more_data};
        }
        result.name = owned(result.name);
        if (!guard())
            return {Inventory::ChildState::Unknown, error()};
        const auto count = bindings[node.binding].info.subkeys;
        if (result.state == Inventory::ChildState::Unknown) {
            unknown();
            return {Inventory::ChildState::Unknown, invalid_data};
        }
        if (result.state == Inventory::ChildState::End) {
            if (!result.name.isEmpty() || index != count) {
                unknown();
                return {Inventory::ChildState::Unknown, error()};
            }
            node.ended = true;
            return {Inventory::ChildState::End, 0};
        }
        if (result.state != Inventory::ChildState::Child || index >= count || !component(result.name) ||
            result.name.size() > units) {
            unknown();
            return {Inventory::ChildState::Unknown, error()};
        }
        for (const auto& previous : node.children)
            if (same(previous.name, result.name)) {
                unknown();
                return {Inventory::ChildState::Unknown, error()};
            }
        if (node.depth == Inventory::maximum_depth || node_count == nodes.size()) {
            unknown(more_data);
            return {Inventory::ChildState::Unknown, error()};
        }
        const auto opened = child(node.binding, result.name);
        if (opened.binding == absent) {
            if (opened.state == OpenState::Missing)
                changed();
            return {Inventory::ChildState::Unknown, opened.error};
        }
        const auto child_ref = this->node(opened.binding, node.depth + 1);
        if (!child_ref.value || !reserve(2 * static_cast<std::uint64_t>(result.name.size())))
            return {Inventory::ChildState::Unknown, error()};
        Inventory::ChildResult frame{Inventory::ChildState::Child, 0, std::move(result.name), child_ref};
        node.children.push_back(frame);
        return frame;
    }
    bool checkEnumeration(const Node& node) {
        if (node.children.empty() && !node.ended)
            return true;
        if (!calls.enum_child) {
            unknown();
            return false;
        }
        std::vector<QString> names;
        const auto count = bindings[node.binding].info.subkeys;
        names.reserve(count);
        for (std::uint32_t i = 0; i <= count; ++i) {
            if (!guard())
                return false;
            auto result = calls.enum_child(bindings[node.binding].key, i, Inventory::maximum_name_units);
            if (result.error) {
                unknown(result.error);
                return false;
            }
            if (result.name.size() > Inventory::maximum_name_units) {
                unknown(more_data);
                return false;
            }
            result.name = owned(result.name);
            if (!guard())
                return false;
            if (i == count) {
                if (result.state != Inventory::ChildState::End || !result.name.isEmpty()) {
                    changed();
                    return false;
                }
                break;
            }
            if (result.state != Inventory::ChildState::Child || !component(result.name)) {
                unknown();
                return false;
            }
            if (std::any_of(names.begin(), names.end(), [&](const auto& name) { return same(name, result.name); })) {
                unknown();
                return false;
            }
            names.push_back(std::move(result.name));
        }
        // Registry enumeration order is unspecified. Partial cursors require
        // every captured component still in the current set; completed cursors
        // require exactly the same case-insensitive set.
        if ((node.ended && names.size() != node.children.size()) ||
            std::any_of(node.children.begin(), node.children.end(), [&](const auto& frame) {
                return std::none_of(names.begin(), names.end(),
                                    [&](const auto& name) { return same(name, frame.name); });
            })) {
            changed();
            return false;
        }
        return true;
    }
    Inventory::ValidationResult recheck() {
        if (!observed || !available())
            return {Inventory::Validation::Unknown, invalid_data};
        if (!guard())
            return validation;
        for (std::size_t i = 0; i < node_count; ++i) {
            const auto& node = nodes[i];
            if (node.captured) {
                const auto captured = security.read(
                    Security::Object::Key, bindings[node.binding].key,
                    static_cast<std::uint64_t>(node.metadata.raw_sd.size() + node.metadata.owner_sid.size()));
                if (captured.error != Security::Error::None) {
                    unknown();
                    return validation;
                }
                if (!guard())
                    return validation;
                if (captured.raw_sd != node.metadata.raw_sd || captured.owner_sid != node.metadata.owner_sid ||
                    static_cast<Inventory::Dacl>(captured.dacl) != node.metadata.dacl ||
                    captured.dacl_protected != node.metadata.dacl_protected) {
                    changed();
                    return validation;
                }
            }
            if (!checkEnumeration(node))
                return validation;
        }
        (void)guard();
        return validation;
    }
    Inventory::CloseResult finish() {
        if (!available())
            return closure;
        closure = {Inventory::CloseState::Unknown, invalid_data};
        std::uint32_t first_error = ownership_uncertain ? invalid_data : 0;
        bool keys_ok = !ownership_uncertain;
        for (std::size_t i = key_count; i > 0; --i) {
            std::uint32_t code = invalid_data;
            try {
                if (calls.close_key)
                    code = calls.close_key(keys[i - 1]);
            } catch (...) {
            }
            if (code) {
                keys_ok = false;
                if (!first_error)
                    first_error = code;
            }
        }
        if (keys_ok)
            for (std::size_t i = event_count; i > 0; --i) {
                std::uint32_t code = invalid_data;
                try {
                    if (calls.close_event)
                        code = calls.close_event(events[i - 1]);
                } catch (...) {
                }
                if (code && !first_error)
                    first_error = code;
            }
        closure = first_error ? Inventory::CloseResult{Inventory::CloseState::Unknown, first_error}
                              : Inventory::CloseResult{Inventory::CloseState::Closed, 0};
        return closure;
    }
};

AppContainerStorageRegistry::AppContainerStorageRegistry(RootPaths roots, Calls calls)
    : impl_(std::make_unique<Impl>(std::move(roots), std::move(calls))) {}
AppContainerStorageRegistry::~AppContainerStorageRegistry() {
    // No close/ACK from a destructor, including abandonment during a callback.
    // Preserve active state/captures rather than destroy them under a callback.
    if (impl_ && (impl_->working || impl_->closure.state != Inventory::CloseState::Closed))
        (void)impl_.release();
}
AppContainerStorageRegistry::Inventory::RootResult
AppContainerStorageRegistry::root(const Inventory::Root root) noexcept {
    auto* const held = impl_.get();
    if (!held)
        return {{}, invalid_data};
    if (held->working)
        return {{}, busy};
    if (root != Inventory::Root::MappingsSID && root != Inventory::Root::StorageSID)
        return {{}, invalid_data};
    if (!held->available() || !held->valid_roots)
        return {{}, invalid_data};
    Busy guard(held->working);
    try {
        if (held->validation.state != Inventory::Validation::Unchanged)
            return {{}, held->error()};
        return held->readRoot(root);
    } catch (...) {
        held->unknown();
        return {{}, held->error()};
    }
}
AppContainerStorageRegistry::Inventory::Metadata
AppContainerStorageRegistry::readMetadata(const Inventory::NodeRef ref, const std::uint64_t maximum_bytes) noexcept {
    auto* const held = impl_.get();
    if (!held)
        return failedMetadata(ref, invalid_data);
    if (held->working)
        return failedMetadata(ref, busy);
    if (!held->available() || maximum_bytes < Security::minimum_sd_bytes)
        return failedMetadata(ref, invalid_data);
    auto* const node = held->find(ref);
    if (!node)
        return failedMetadata(ref, invalid_data);
    Busy guard(held->working);
    try {
        return held->metadata(ref, *node, maximum_bytes);
    } catch (...) {
        held->unknown();
        return failedMetadata(ref, held->error());
    }
}
AppContainerStorageRegistry::Inventory::ChildResult
AppContainerStorageRegistry::nextChild(const Inventory::NodeRef parent, const std::uint32_t index,
                                       const std::uint32_t maximum_units) noexcept {
    auto* const held = impl_.get();
    if (!held)
        return {Inventory::ChildState::Unknown, invalid_data};
    if (held->working)
        return {Inventory::ChildState::Unknown, busy};
    if (!held->available() || !maximum_units || maximum_units > Inventory::maximum_name_units)
        return {Inventory::ChildState::Unknown, invalid_data};
    auto* const node = held->find(parent);
    if (!node)
        return {Inventory::ChildState::Unknown, invalid_data};
    Busy guard(held->working);
    try {
        return held->next(*node, index, maximum_units);
    } catch (...) {
        held->unknown();
        return {Inventory::ChildState::Unknown, held->error()};
    }
}
AppContainerStorageRegistry::Inventory::ValidationResult AppContainerStorageRegistry::revalidate() noexcept {
    auto* const held = impl_.get();
    if (!held)
        return {Inventory::Validation::Unknown, invalid_data};
    if (held->working)
        return {Inventory::Validation::Unknown, busy};
    Busy guard(held->working);
    try {
        return held->recheck();
    } catch (...) {
        held->unknown();
        return held->validation;
    }
}
AppContainerStorageRegistry::Inventory::CloseResult AppContainerStorageRegistry::close() noexcept {
    auto* const held = impl_.get();
    if (!held)
        return {Inventory::CloseState::Unknown, invalid_data};
    if (held->working)
        return {Inventory::CloseState::NotAttempted, busy};
    Busy guard(held->working);
    return held->finish();
}
AppContainerStorageRegistry::Inventory::CloseResult AppContainerStorageRegistry::closeResult() const noexcept {
    return impl_ ? impl_->closure : Inventory::CloseResult{Inventory::CloseState::Unknown, invalid_data};
}
} // namespace music_player::sandbox_probe

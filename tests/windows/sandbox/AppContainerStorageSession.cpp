#include "AppContainerStorageSession.hpp"

#include <array>
#include <optional>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Inventory = AppContainerStorageInventory;
constexpr std::uint32_t invalid_handle = 6, invalid_data = 13, busy_error = 170, more_data = 234,
                        exception_error = 1359;
constexpr std::uint64_t maximum_sid_bytes = 68;
enum class Part { Files, Registry };
struct Operation final {
    bool& busy;
    const bool acquired;
    explicit Operation(bool& value) : busy(value), acquired(!value) {
        if (acquired)
            busy = true;
    }
    ~Operation() {
        if (acquired)
            busy = false;
    }
};
std::optional<Part> rootPart(const Inventory::Root root) {
    switch (root) {
    case Inventory::Root::PackageRoot:
    case Inventory::Root::AC:
        return Part::Files;
    case Inventory::Root::MappingsSID:
    case Inventory::Root::StorageSID:
        return Part::Registry;
    }
    return {};
}
bool validName(const QString& name, const std::uint32_t maximum_units) {
    if (name.isEmpty() || name.size() > maximum_units || !name.isValidUtf16() || name == "." || name == "..")
        return false;
    for (const auto unit : name)
        if (unit.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(unit))
            return false;
    return true;
}
bool validIdentity(const Inventory::Identity identity) { return identity.domain && identity.object; }
bool validType(const Part part, const Inventory::Type type) {
    return part == Part::Files ? type == Inventory::Type::Directory || type == Inventory::Type::File
                               : type == Inventory::Type::RegistryKey;
}
} // namespace

struct AppContainerStorageSession::Impl final {
    struct Port final {
        std::unique_ptr<Inventory::ReadSession> session{};
        Inventory::CloseResult closure{};
        bool attempted{false};
        ~Port() {
            // Only explicit Closed+0 releases through destruction. Unknown or
            // unattempted native ownership stays retained until process exit.
            if (session)
                static_cast<void>(session.release());
        }
    };
    struct Reference final {
        Part part{Part::Files};
        Inventory::NodeRef inner{};
        std::optional<Inventory::Identity> captured_identity{};
    };
    struct Identity final {
        Part part{Part::Files};
        Inventory::Identity inner{}, outer{};
    };
    Port files{}, registry{};
    // Bounded opaque maps, with no native-handle bits in outward identifiers.
    std::array<Reference, maximum_refs> references{};
    std::array<Identity, maximum_identities> identities{};
    std::array<std::optional<Inventory::NodeRef>, 4> roots{};
    std::size_t reference_count{}, identity_count{};
    Inventory::ValidationResult validation{};
    Inventory::CloseResult closure{};
    bool configured{false}, busy{false}, validation_failed{false}, close_started{false};

    Port& port(const Part part) { return part == Part::Files ? files : registry; }
    void fail(const std::uint32_t error) {
        if (!validation_failed) {
            validation_failed = true;
            validation = {Inventory::Validation::Unknown, error};
        }
    }
    bool readable() const { return configured && !close_started; }
    Reference* reference(const Inventory::NodeRef ref) {
        if (!ref.value || ref.value > reference_count)
            return nullptr;
        return &references[static_cast<std::size_t>(ref.value - 1)];
    }
    Inventory::NodeRef remember(const Part part, const Inventory::NodeRef inner) {
        for (std::size_t index = 0; index < reference_count; ++index)
            if (references[index].part == part && references[index].inner == inner)
                return {static_cast<std::uint64_t>(index + 1)};
        if (reference_count == references.size())
            return {};
        references[reference_count] = {part, inner, {}};
        return {static_cast<std::uint64_t>(++reference_count)};
    }
    std::optional<Inventory::Identity> remember(const Part part, const Inventory::Identity inner) {
        for (std::size_t index = 0; index < identity_count; ++index)
            if (identities[index].part == part && identities[index].inner == inner)
                return identities[index].outer;
        if (identity_count == identities.size())
            return {};
        // Identity is only a session-local equality token, never a native grant.
        const Inventory::Identity outer{part == Part::Files ? 1U : 2U, static_cast<std::uint64_t>(identity_count + 1)};
        identities[identity_count++] = {part, inner, outer};
        return outer;
    }
    Inventory::Metadata reject(Inventory::Metadata result, const Inventory::NodeRef outer, const std::uint32_t error) {
        result.ref = outer;
        result.identity = {};
        result.presence = Inventory::Presence::Unknown;
        result.error = error;
        result.owner_sid.clear();
        result.raw_sd.clear();
        fail(error);
        return result;
    }
    void attemptClose(Port& value) {
        if (value.attempted)
            return;
        value.closure = {Inventory::CloseState::Unknown, invalid_handle};
        if (!value.session) {
            value.attempted = true;
            return;
        }
        try {
            const auto result = value.session->close();
            if (result.state == Inventory::CloseState::NotAttempted && result.error == busy_error) {
                value.closure = result;
                return;
            }
            value.closure = {result.state == Inventory::CloseState::Closed && result.error == 0
                                 ? Inventory::CloseState::Closed
                                 : Inventory::CloseState::Unknown,
                             result.error};
        } catch (...) {
            value.closure = {Inventory::CloseState::Unknown, exception_error};
        }
        value.attempted = true;
        // The port call has returned before any acknowledged object is freed.
        if (value.closure.state == Inventory::CloseState::Closed)
            value.session.reset();
    }
    Inventory::ValidationResult validate(Port& value) {
        try {
            auto result = value.session->revalidate();
            if (result.state != Inventory::Validation::Unknown && result.state != Inventory::Validation::Changed &&
                result.state != Inventory::Validation::Unchanged)
                return {Inventory::Validation::Unknown, result.error ? result.error : invalid_data};
            if (result.state == Inventory::Validation::Unchanged && result.error)
                result.state = Inventory::Validation::Unknown;
            return result;
        } catch (...) {
            return {Inventory::Validation::Unknown, exception_error};
        }
    }
};

AppContainerStorageSession::AppContainerStorageSession(std::unique_ptr<Inventory::ReadSession> files,
                                                       std::unique_ptr<Inventory::ReadSession> registry) {
    try {
        // Allocate before transferring either uncertain port into the owner.
        impl_ = std::make_unique<Impl>();
    } catch (...) {
        static_cast<void>(files.release());
        static_cast<void>(registry.release());
        throw;
    }
    impl_->files.session = std::move(files);
    impl_->registry.session = std::move(registry);
    impl_->configured = impl_->files.session && impl_->registry.session;
}
AppContainerStorageSession::~AppContainerStorageSession() {
    // Retain the guard and maps as well as the uncertain ports. In particular,
    // destroying this owner from a port callback cannot free its active state.
    if (impl_->busy || impl_->files.session || impl_->registry.session)
        static_cast<void>(impl_.release());
}

AppContainerStorageSession::Inventory::RootResult
AppContainerStorageSession::root(const Inventory::Root root) noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return {{}, busy_error};
    const auto part = rootPart(root);
    if (!part || !value.readable()) {
        const auto error = part ? invalid_handle : invalid_data;
        value.fail(error);
        return {{}, error};
    }
    try {
        auto result = value.port(*part).session->root(root);
        if (result.presence != Inventory::Presence::Present || result.error) {
            result.ref = {};
            value.fail(result.error);
            return result;
        }
        const auto index = static_cast<std::size_t>(root);
        if (!result.ref.value || (value.roots[index] && *value.roots[index] != result.ref)) {
            value.fail(invalid_data);
            return {{}, invalid_data};
        }
        const auto outer = value.remember(*part, result.ref);
        if (!outer.value) {
            value.fail(more_data);
            return {{}, more_data};
        }
        value.roots[index] = result.ref;
        result.ref = outer;
        return result;
    } catch (...) {
        value.fail(exception_error);
        return {{}, exception_error};
    }
}

AppContainerStorageSession::Inventory::Metadata
AppContainerStorageSession::readMetadata(const Inventory::NodeRef ref, const std::uint64_t maximum_bytes) noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return {.ref = ref, .error = busy_error};
    if (!value.readable()) {
        value.fail(invalid_handle);
        return {.ref = ref, .error = invalid_handle};
    }
    auto* const binding = value.reference(ref);
    if (!binding || !maximum_bytes || maximum_bytes > Inventory::maximum_total_bytes) {
        value.fail(invalid_data);
        return {.ref = ref, .error = invalid_data};
    }
    const auto held = *binding;
    try {
        auto result = value.port(held.part).session->readMetadata(held.inner, maximum_bytes);
        const auto failure = result.error ? result.error : invalid_data;
        const bool successful = result.presence == Inventory::Presence::Present && result.error == 0;
        const auto owner_bytes = static_cast<std::uint64_t>(result.owner_sid.size());
        const auto sd_bytes = static_cast<std::uint64_t>(result.raw_sd.size());
        if (owner_bytes > maximum_sid_bytes || sd_bytes > Inventory::maximum_sd_bytes || owner_bytes > maximum_bytes ||
            sd_bytes > maximum_bytes - owner_bytes)
            return value.reject(std::move(result), ref, failure);
        if (successful &&
            (result.ref != held.inner || !validType(held.part, result.type) || !validIdentity(result.identity) ||
             (held.captured_identity && *held.captured_identity != result.identity)))
            return value.reject(std::move(result), ref, failure);
        const auto inner_identity = result.identity;
        result.identity = {};
        if (validIdentity(inner_identity)) {
            const auto outer = value.remember(held.part, inner_identity);
            if (!outer) {
                const auto error = result.error ? result.error : more_data;
                return value.reject(std::move(result), ref, error);
            }
            result.identity = *outer;
        }
        result.ref = ref;
        // A Qt value copy can still borrow a port's fromRawData() allocation.
        result.owner_sid = QByteArray(result.owner_sid.constData(), result.owner_sid.size());
        result.raw_sd = QByteArray(result.raw_sd.constData(), result.raw_sd.size());
        if (successful)
            binding->captured_identity = inner_identity;
        else
            value.fail(result.error);
        return result;
    } catch (...) {
        value.fail(exception_error);
        return {.ref = ref, .error = exception_error};
    }
}

AppContainerStorageSession::Inventory::ChildResult
AppContainerStorageSession::nextChild(const Inventory::NodeRef parent, const std::uint32_t index,
                                      const std::uint32_t maximum_units) noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return {Inventory::ChildState::Unknown, busy_error};
    if (!value.readable()) {
        value.fail(invalid_handle);
        return {Inventory::ChildState::Unknown, invalid_handle};
    }
    const auto* const binding = value.reference(parent);
    if (!binding || !maximum_units || maximum_units > Inventory::maximum_name_units ||
        index > Inventory::maximum_objects) {
        value.fail(invalid_data);
        return {Inventory::ChildState::Unknown, invalid_data};
    }
    const auto held = *binding;
    try {
        auto result = value.port(held.part).session->nextChild(held.inner, index, maximum_units);
        if (result.state == Inventory::ChildState::Unknown) {
            value.fail(result.error);
            return {Inventory::ChildState::Unknown, result.error};
        }
        if (result.state == Inventory::ChildState::End && result.error == 0 && result.name.isEmpty() &&
            !result.ref.value)
            return result;
        if (result.state != Inventory::ChildState::Child || result.error || !result.ref.value ||
            !validName(result.name, maximum_units)) {
            const auto error = result.error ? result.error : invalid_data;
            value.fail(error);
            return {Inventory::ChildState::Unknown, error};
        }
        const auto outer = value.remember(held.part, result.ref);
        if (!outer.value) {
            value.fail(more_data);
            return {Inventory::ChildState::Unknown, more_data};
        }
        result.ref = outer;
        result.name = QString(result.name.constData(), result.name.size());
        return result;
    } catch (...) {
        value.fail(exception_error);
        return {Inventory::ChildState::Unknown, exception_error};
    }
}

AppContainerStorageSession::Inventory::ValidationResult AppContainerStorageSession::revalidate() noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return {Inventory::Validation::Unknown, busy_error};
    if (!value.readable()) {
        value.fail(invalid_handle);
        return {Inventory::Validation::Unknown, invalid_handle};
    }
    if (value.validation_failed)
        return value.validation;
    const auto files = value.validate(value.files);
    const auto registry = value.validate(value.registry);
    for (const auto result : {files, registry})
        if (result.state != Inventory::Validation::Unchanged || result.error) {
            value.validation_failed = true;
            value.validation = result;
            return result;
        }
    value.validation = {Inventory::Validation::Unchanged, 0};
    return value.validation;
}

AppContainerStorageSession::Inventory::CloseResult AppContainerStorageSession::close() noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return {Inventory::CloseState::NotAttempted, busy_error};
    if (value.closure.state != Inventory::CloseState::NotAttempted)
        return value.closure;
    value.close_started = true;
    value.attemptClose(value.files);
    value.attemptClose(value.registry);
    const auto files = value.files.closure, registry = value.registry.closure;
    if (files.state == Inventory::CloseState::Closed && registry.state == Inventory::CloseState::Closed)
        value.closure = {Inventory::CloseState::Closed, 0};
    else if (files.state == Inventory::CloseState::Unknown || registry.state == Inventory::CloseState::Unknown)
        value.closure = {Inventory::CloseState::Unknown,
                         files.state == Inventory::CloseState::Unknown ? files.error : registry.error};
    else
        value.closure = {Inventory::CloseState::NotAttempted, busy_error};
    return value.closure;
}
AppContainerStorageSession::Inventory::CloseResult AppContainerStorageSession::closeResult() const noexcept {
    if (impl_->busy)
        return {Inventory::CloseState::NotAttempted, busy_error};
    return impl_->closure;
}
} // namespace music_player::sandbox_probe

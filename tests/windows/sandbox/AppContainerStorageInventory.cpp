#include "AppContainerStorageInventory.hpp"

#include <algorithm>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Inventory = AppContainerStorageInventory;
constexpr std::uint32_t failed_call = 31;
constexpr std::uint32_t invalid_handle = 6;
constexpr std::uint32_t busy_call = 170;
constexpr std::uint64_t maximum_sid_bytes = 68;
struct Operation final {
    bool& busy;
    bool acquired;
    explicit Operation(bool& value) : busy(value), acquired(!value) {
        if (acquired)
            busy = true;
    }
    ~Operation() {
        if (acquired)
            busy = false;
    }
};
bool validLimits(const Inventory::Limits& value) {
    return value.objects && value.objects <= Inventory::maximum_objects && value.depth &&
           value.depth <= Inventory::maximum_depth && value.sd_bytes && value.sd_bytes <= Inventory::maximum_sd_bytes &&
           value.total_bytes && value.total_bytes <= Inventory::maximum_total_bytes;
}
bool validSid(const QByteArray& value) {
    if (value.size() < 8 || value.size() > static_cast<qsizetype>(maximum_sid_bytes) || value[0] != 1)
        return false;
    const auto count = static_cast<unsigned char>(value[1]);
    return count <= 15 && value.size() == 8 + 4 * count;
}
bool validName(const QString& name) {
    if (name.isEmpty() || name.size() > Inventory::maximum_name_units || !name.isValidUtf16() || name == "." ||
        name == ".." || name.endsWith(' ') || name.endsWith('.'))
        return false;
    for (const auto unit : name)
        if (unit.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(unit))
            return false;
    const auto base = name.section('.', 0, 0).toUpper();
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CLOCK$")
        return false;
    return !(base.size() == 4 && (base.startsWith("COM") || base.startsWith("LPT")) && base[3] >= QChar{'1'} &&
             base[3] <= QChar{'9'});
}
bool sameMetadata(const Inventory::Metadata& left, const Inventory::Metadata& right) {
    // NodeRef may differ only for the explicitly correlated AC overlap.
    return left.identity == right.identity && left.presence == right.presence && left.type == right.type &&
           left.error == right.error && left.link_count == right.link_count && left.attributes == right.attributes &&
           left.reparse_tag == right.reparse_tag && left.reparse == right.reparse &&
           left.registry_link == right.registry_link && left.owner_sid == right.owner_sid &&
           left.raw_sd == right.raw_sd && left.dacl == right.dacl && left.dacl_protected == right.dacl_protected &&
           left.sacl_not_queried == right.sacl_not_queried;
}
Inventory::Metadata owned(Inventory::Metadata value) {
    // Qt value copies may still borrow Source buffers through fromRawData().
    value.owner_sid = QByteArray(value.owner_sid.constData(), value.owner_sid.size());
    value.raw_sd = QByteArray(value.raw_sd.constData(), value.raw_sd.size());
    return value;
}
class Walker final {
  public:
    Walker(Inventory::ReadSession& session, Inventory::Report& report, Inventory::Limits limits,
           std::vector<Inventory::NodeRef>& references)
        : session_(session), report_(report), limits_(limits), references_(references) {}
    bool run() {
        for (std::uint32_t index = 0; index < 4; ++index) {
            auto& root = report_.roots[index];
            root.result = session_.root(root.root);
            root.presence = root.result.presence;
            if (root.result.presence != Inventory::Presence::Present || root.result.error || !root.result.ref.value)
                return fail(Inventory::Reason::Root);
            if (knownRef(root.result.ref))
                return fail(Inventory::Reason::Identity);
            if (report_.objects.size() >= limits_.objects)
                return fail(Inventory::Reason::Bounds);
            auto value = read(root.result.ref, 0, false, &root);
            if (!value)
                return false;
            const auto expected = index < 2 ? Inventory::Type::Directory : Inventory::Type::RegistryKey;
            if (!validMetadata(*value, root.result.ref) || value->type != expected)
                return failIfUnset(Inventory::Reason::Metadata);
            if (knownIdentity(value->identity))
                return fail(Inventory::Reason::Identity);
            root.object_index = append(root.root, {}, {}, 0, std::move(*value));
        }
        if (!walk(*report_.roots[0].object_index) || !ac_linked_)
            return failIfUnset(Inventory::Reason::Identity);
        return walk(*report_.roots[2].object_index) && walk(*report_.roots[3].object_index);
    }

  private:
    bool fail(Inventory::Reason reason) {
        report_.reason = reason;
        return false;
    }
    bool failIfUnset(Inventory::Reason reason) {
        return report_.reason == Inventory::Reason::None ? fail(reason) : false;
    }
    std::optional<std::uint32_t> knownRef(Inventory::NodeRef ref) const {
        for (const auto& [known, index] : known_refs_)
            if (known == ref)
                return index;
        return {};
    }
    std::optional<std::uint32_t> knownIdentity(Inventory::Identity id) const {
        for (std::uint32_t index = 0; index < report_.objects.size(); ++index)
            if (report_.objects[index].metadata.identity == id)
                return index;
        return {};
    }
    std::optional<Inventory::Metadata> read(Inventory::NodeRef ref, std::uint64_t name_bytes, bool overlap,
                                            Inventory::RootObservation* observation = nullptr) {
        const auto remaining = limits_.total_bytes - report_.total_bytes;
        if (name_bytes > remaining) {
            fail(Inventory::Reason::Bounds);
            return {};
        }
        const auto cap =
            std::min(limits_.sd_bytes + maximum_sid_bytes, overlap ? limits_.total_bytes : remaining - name_bytes);
        if (std::find(references_.begin(), references_.end(), ref) == references_.end())
            references_.push_back(ref);
        auto value = session_.readMetadata(ref, cap);
        if (observation) {
            observation->metadata_error = value.error;
            observation->presence = value.presence;
        }
        const auto owner_bytes = static_cast<std::uint64_t>(value.owner_sid.size());
        const auto sd_bytes = static_cast<std::uint64_t>(value.raw_sd.size());
        if (sd_bytes > limits_.sd_bytes || owner_bytes > maximum_sid_bytes || owner_bytes > cap ||
            sd_bytes > cap - owner_bytes) {
            fail(Inventory::Reason::Bounds);
            return {};
        }
        return value;
    }
    bool validMetadata(const Inventory::Metadata& value, Inventory::NodeRef ref) {
        if (value.ref != ref || !ref.value || value.presence != Inventory::Presence::Present || value.error ||
            (value.type != Inventory::Type::Directory && value.type != Inventory::Type::File &&
             value.type != Inventory::Type::RegistryKey) ||
            value.reparse || value.reparse_tag || (value.attributes & 0x400U) || value.registry_link ||
            (value.type != Inventory::Type::RegistryKey && value.link_count != 1) || !validSid(value.owner_sid) ||
            value.raw_sd.isEmpty() ||
            (value.dacl != Inventory::Dacl::Absent && value.dacl != Inventory::Dacl::Null &&
             value.dacl != Inventory::Dacl::Present) ||
            !value.sacl_not_queried)
            return fail(Inventory::Reason::Metadata);
        if (!value.identity.domain || !value.identity.object)
            return fail(Inventory::Reason::Identity);
        return true;
    }
    std::uint32_t append(Inventory::Root root, Inventory::NodeRef parent, QString name, std::uint32_t depth,
                         Inventory::Metadata value) {
        const auto index = static_cast<std::uint32_t>(report_.objects.size());
        report_.total_bytes += static_cast<std::uint64_t>(value.owner_sid.size()) +
                               static_cast<std::uint64_t>(value.raw_sd.size()) +
                               2 * static_cast<std::uint64_t>(name.size());
        known_refs_.push_back({value.ref, index});
        report_.objects.push_back({root, parent, std::move(name), depth, owned(std::move(value))});
        return index;
    }
    bool walk(std::uint32_t index) {
        const auto parent_ref = report_.objects[index].metadata.ref;
        const auto parent_type = report_.objects[index].metadata.type;
        const auto parent_root = report_.objects[index].root;
        const auto depth = report_.objects[index].depth;
        if (parent_type == Inventory::Type::File)
            return true;
        std::vector<QString> names;
        for (std::uint32_t child_index = 0; child_index <= limits_.objects; ++child_index) {
            auto child = session_.nextChild(parent_ref, child_index, Inventory::maximum_name_units);
            if (child.state == Inventory::ChildState::End)
                return child.error == 0 && child.name.isEmpty() && !child.ref.value ? true
                                                                                    : fail(Inventory::Reason::Metadata);
            if (child.state != Inventory::ChildState::Child || child.error || !child.ref.value)
                return fail(Inventory::Reason::Metadata);
            if (!validName(child.name) || std::any_of(names.begin(), names.end(), [&](const auto& previous) {
                    return previous.compare(child.name, Qt::CaseInsensitive) == 0;
                }))
                return fail(Inventory::Reason::Name);
            const QString name(child.name.constData(), child.name.size());
            names.push_back(name);
            if (depth >= limits_.depth)
                return fail(Inventory::Reason::Bounds);
            const bool overlap =
                index == *report_.roots[0].object_index && name.compare("AC", Qt::CaseInsensitive) == 0;
            if ((!overlap && knownRef(child.ref)) || (overlap && ac_linked_))
                return fail(Inventory::Reason::Identity);
            if (!overlap && report_.objects.size() >= limits_.objects)
                return fail(Inventory::Reason::Bounds);
            const auto name_bytes = 2 * static_cast<std::uint64_t>(name.size());
            auto value = read(child.ref, name_bytes, overlap);
            if (!value || !validMetadata(*value, child.ref))
                return false;
            if ((parent_type == Inventory::Type::RegistryKey) != (value->type == Inventory::Type::RegistryKey))
                return fail(Inventory::Reason::Metadata);
            std::uint32_t next{};
            if (overlap) {
                next = *report_.roots[1].object_index;
                auto& captured = report_.objects[next];
                if (!sameMetadata(captured.metadata, *value))
                    return fail(Inventory::Reason::Changed);
                captured.root = parent_root;
                captured.parent = parent_ref;
                captured.name = name;
                captured.depth = depth + 1;
                // The retained AC pin remains the canonical reference even if
                // the Package enumeration names a second reference to it.
                if (!knownRef(child.ref))
                    known_refs_.push_back({child.ref, next});
                report_.total_bytes += name_bytes;
                ac_linked_ = true;
            } else {
                if (knownIdentity(value->identity))
                    return fail(Inventory::Reason::Identity);
                next = append(parent_root, parent_ref, name, depth + 1, std::move(*value));
            }
            if (!walk(next))
                return false;
        }
        return fail(Inventory::Reason::Bounds);
    }
    Inventory::ReadSession& session_;
    Inventory::Report& report_;
    Inventory::Limits limits_;
    std::vector<Inventory::NodeRef>& references_;
    std::vector<std::pair<Inventory::NodeRef, std::uint32_t>> known_refs_{};
    bool ac_linked_{false};
};
} // namespace

struct AppContainerStorageInventory::ReadLease::Impl final {
    std::unique_ptr<ReadSession> session{};
    std::vector<NodeRef> references{};
    ValidationResult validation{};
    CloseResult closure{};
    bool validation_failed{false}, validation_exception{false}, busy{false};
    ~Impl() {
        // No implicit port close or invented ACK if construction/ownership fails.
        if (session)
            static_cast<void>(session.release());
    }
};
AppContainerStorageInventory::ReadLease::ReadLease(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AppContainerStorageInventory::ReadLease::~ReadLease() {
    if (impl_->closure.state != CloseState::Closed || impl_->closure.error)
        static_cast<void>(impl_.release());
}
AppContainerStorageInventory::ValidationResult AppContainerStorageInventory::ReadLease::revalidate() noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return {Validation::Unknown, busy_call};
    if (!value.session || value.closure.state != CloseState::NotAttempted)
        return {Validation::Unknown, invalid_handle};
    if (value.validation_failed)
        return value.validation;
    try {
        value.validation = value.session->revalidate();
    } catch (...) {
        value.validation = {Validation::Unknown, failed_call};
        value.validation_exception = true;
    }
    if (value.validation.state != Validation::Unchanged || value.validation.error) {
        value.validation_failed = true;
        if (value.validation.state != Validation::Changed || value.validation.error)
            value.validation.state = Validation::Unknown;
    }
    return value.validation;
}
AppContainerStorageInventory::CloseResult AppContainerStorageInventory::ReadLease::close() noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return {CloseState::NotAttempted, busy_call};
    if (value.closure.state != CloseState::NotAttempted)
        return value.closure;
    value.closure = {CloseState::Unknown, failed_call};
    try {
        const auto result = value.session->close();
        value.closure = {result.state == CloseState::Closed && result.error == 0 ? CloseState::Closed
                                                                                 : CloseState::Unknown,
                         result.error};
    } catch (...) {
    }
    if (value.closure.state == CloseState::Closed)
        value.session.reset();
    return value.closure;
}
AppContainerStorageInventory::CloseResult AppContainerStorageInventory::ReadLease::closeResult() const noexcept {
    return impl_->closure;
}
AppContainerStorageInventory::Result AppContainerStorageInventory::inspect(Source& source, Limits limits) {
    Result result;
    for (std::size_t index = 0; index < result.report.roots.size(); ++index)
        result.report.roots[index].root = static_cast<Root>(index);
    if (!validLimits(limits)) {
        result.report.reason = Reason::Limits;
        return result;
    }
    std::unique_ptr<ReadSession> pending;
    try {
        auto begun = source.begin();
        result.report.provenance = begun.provenance;
        result.report.begin_error = begun.error;
        pending = std::move(begun.session);
        if (pending) {
            auto held = std::make_unique<ReadLease::Impl>();
            held->session = std::move(pending);
            result.lease = std::shared_ptr<ReadLease>(new ReadLease(std::move(held)));
        }
        if (begun.error || !result.lease) {
            result.report.reason = Reason::Source;
            return result;
        }
        if (begun.provenance != Provenance::LiveCreated) {
            result.report.reason = Reason::Provenance;
            return result;
        }
        auto& held = *result.lease->impl_;
        Walker walker(*held.session, result.report, limits, held.references);
        if (!walker.run())
            return result;
        result.report.validation = result.lease->revalidate();
        if (held.validation_exception)
            result.report.reason = Reason::Exception;
        else if (result.report.validation.state != Validation::Unchanged || result.report.validation.error)
            result.report.reason = Reason::Changed;
        else
            result.report.status = Status::Observed;
    } catch (...) {
        if (pending)
            static_cast<void>(pending.release());
        result.report.reason = Reason::Exception;
    }
    return result;
}
AppContainerStorageInventory::Result AppContainerStorageInventory::inspect(Source& source) {
    return inspect(source, Limits{});
}
} // namespace music_player::sandbox_probe

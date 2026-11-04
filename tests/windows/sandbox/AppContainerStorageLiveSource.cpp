#include "AppContainerStorageLiveSource.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"

#include <optional>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Inventory = AppContainerStorageInventory;
using Owner = music_player::m0::WindowsMediaProfileOwner;
using Backend = music_player::m0::WindowsNativeMediaProfile;
using Capability = music_player::m0::WindowsDownloadAppContainer;
using Fence = music_player::m0::WindowsDownloadNativeUseFence;
using State = music_player::m0::WindowsMediaProfileOwnerState;
constexpr std::uint32_t denied = 5, busy_error = 170, revoked = 995, exception_error = 1359;
struct Busy final {
    bool& value;
    explicit Busy(bool& flag) : value(flag) { value = true; }
    ~Busy() { value = false; }
};
bool matches(const Backend& backend, const Capability& capability, const QString& name) {
    const auto report = backend.report();
    return report.create_attempted && report.created && report.create_hresult == 0 && report.sid_released &&
           !report.remove_attempted && report.sid == capability.sidBytes() && report.name == name;
}
class FencedSession final : public Inventory::ReadSession {
  public:
    FencedSession(Owner& owner, Backend& backend, std::shared_ptr<const Capability> capability, QString name,
                  Fence::Ticket&& ticket, std::unique_ptr<Inventory::ReadSession>&& inner)
        : owner_(owner), backend_(backend), capability_(std::move(capability)), name_(std::move(name)),
          ticket_(std::move(ticket)), inner_(std::move(inner)) {}
    ~FencedSession() override {
        // No destructor close or native ACK. Keep unknown handles alive until
        // process exit; destruction of a pending ticket marks it Uncertain.
        if (closure_.state != Inventory::CloseState::Closed || closure_.error != 0)
            static_cast<void>(inner_.release());
    }
    [[nodiscard]] bool live() const {
        return inner_ && !close_attempted_ && owner_.usesPlatform(backend_) && owner_.state() == State::Ready &&
               owner_.capability() == capability_ && capability_->nativeUseFence() &&
               capability_->nativeUseFence()->state() == Fence::State::OpenBusy &&
               matches(backend_, *capability_, name_);
    }
    Inventory::RootResult root(const Inventory::Root root) override {
        return read<Inventory::RootResult>([&] { return inner_->root(root); },
                                           [](const std::uint32_t code) { return Inventory::RootResult{{}, code}; });
    }
    Inventory::Metadata readMetadata(const Inventory::NodeRef ref, const std::uint64_t maximum_bytes) override {
        return read<Inventory::Metadata>(
            [&] { return inner_->readMetadata(ref, maximum_bytes); },
            [&](const std::uint32_t code) { return Inventory::Metadata{.ref = ref, .error = code}; });
    }
    Inventory::ChildResult nextChild(const Inventory::NodeRef parent, const std::uint32_t index,
                                     const std::uint32_t maximum_units) override {
        return read<Inventory::ChildResult>(
            [&] { return inner_->nextChild(parent, index, maximum_units); },
            [](const std::uint32_t code) { return Inventory::ChildResult{Inventory::ChildState::Unknown, code}; });
    }
    Inventory::ValidationResult revalidate() override {
        return read<Inventory::ValidationResult>(
            [&] { return inner_->revalidate(); },
            [](const std::uint32_t code) {
                return Inventory::ValidationResult{
                    code == revoked ? Inventory::Validation::Changed : Inventory::Validation::Unknown, code};
            });
    }
    Inventory::CloseResult close() override {
        if (busy_)
            return {Inventory::CloseState::NotAttempted, busy_error};
        if (close_attempted_)
            return closure_;
        const Busy guard(busy_);
        close_attempted_ = true;
        closure_ = {Inventory::CloseState::Unknown, exception_error};
        try {
            const auto result = inner_->close();
            if (result.state == Inventory::CloseState::NotAttempted && result.error == busy_error) {
                close_attempted_ = false;
                closure_ = result;
                return result;
            }
            closure_.error = result.error;
            if (result.state == Inventory::CloseState::Closed && result.error == 0) {
                inner_.reset();
                // No process was launched by a read-only session. This ACK
                // covers only its explicitly closed native read resources.
                if (ticket_.acknowledgeNoChild())
                    closure_ = {Inventory::CloseState::Closed, 0};
                else
                    closure_ = {Inventory::CloseState::Unknown, revoked};
            }
        } catch (...) {
            // Unknown close retains the inner session and pending ticket.
        }
        return closure_;
    }

  private:
    template <typename Result, typename Call, typename Failure> Result read(Call call, Failure failure) {
        if (busy_)
            return failure(busy_error);
        const Busy guard(busy_);
        try {
            if (!live())
                return failure(revoked);
            auto result = call();
            return live() ? result : failure(revoked);
        } catch (...) {
            return failure(exception_error);
        }
    }
    Owner& owner_;
    Backend& backend_;
    const std::shared_ptr<const Capability> capability_;
    const QString name_;
    Fence::Ticket ticket_;
    std::unique_ptr<Inventory::ReadSession> inner_;
    bool busy_{false}, close_attempted_{false};
    Inventory::CloseResult closure_{};
};
} // namespace
struct AppContainerStorageLiveSource::Impl final {
    music_player::m0::WindowsMediaProfileOwner& owner;
    music_player::m0::WindowsNativeMediaProfile& backend;
    Factory factory;
    bool attempted{false};
};
AppContainerStorageLiveSource::AppContainerStorageLiveSource(music_player::m0::WindowsMediaProfileOwner& owner,
                                                             music_player::m0::WindowsNativeMediaProfile& backend,
                                                             Factory factory)
    : impl_(std::make_unique<Impl>(Impl{owner, backend, std::move(factory)})) {}
AppContainerStorageLiveSource::~AppContainerStorageLiveSource() = default;
AppContainerStorageInventory::BeginResult AppContainerStorageLiveSource::begin() {
    auto& value = *impl_;
    Inventory::BeginResult result;
    result.error = denied;
    if (std::exchange(value.attempted, true))
        return result;
    if (!value.owner.usesPlatform(value.backend) || value.owner.state() != State::Ready || !value.factory)
        return result;
    const auto capability = value.owner.capability();
    if (!capability || !capability->nativeUseFence())
        return result;
    auto ticket = capability->nativeUseFence()->begin();
    if (!ticket) {
        result.error = busy_error;
        return result;
    }
    std::unique_ptr<Inventory::ReadSession> inner;
    bool factory_returned{false};
    try {
        const auto location = value.backend.capturedStorageLocation();
        const auto live = [&] {
            return location && location->sid == capability->sidBytes() && value.owner.state() == State::Ready &&
                   value.owner.capability() == capability &&
                   capability->nativeUseFence()->state() == Fence::State::OpenBusy &&
                   matches(value.backend, *capability, location->name);
        };
        if (!live()) {
            static_cast<void>(ticket->acknowledgeNoChild());
            result.provenance = Inventory::Provenance::Revoked;
            result.error = revoked;
            return result;
        }
        // Trusted factory contract: allocation/binding only, no native I/O.
        inner = value.factory(*location);
        factory_returned = true;
        if (!inner) {
            static_cast<void>(ticket->acknowledgeNoChild());
            return result;
        }
        auto session = std::make_unique<FencedSession>(value.owner, value.backend, capability, location->name,
                                                       std::move(*ticket), std::move(inner));
        const auto usable = session->live();
        result.provenance = usable ? Inventory::Provenance::LiveCreated : Inventory::Provenance::Revoked;
        result.error = usable ? 0 : revoked;
        result.session = std::move(session);
    } catch (...) {
        result.error = exception_error;
        if (!factory_returned)
            static_cast<void>(ticket->acknowledgeNoChild());
        else
            static_cast<void>(inner.release()); // Uncertain; ticket destruction must not ACK.
    }
    return result;
}
} // namespace music_player::sandbox_probe

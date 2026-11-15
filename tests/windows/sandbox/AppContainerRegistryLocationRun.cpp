#include "AppContainerRegistryLocationRun.hpp"

namespace music_player::sandbox_probe {
struct AppContainerRegistryLocationRun::Impl final {
    Result result{};
};
AppContainerRegistryLocationRun::AppContainerRegistryLocationRun(Config, std::shared_ptr<Port>)
    : impl_(std::make_unique<Impl>()) {}
AppContainerRegistryLocationRun::~AppContainerRegistryLocationRun() = default;
const AppContainerRegistryLocationRun::Result& AppContainerRegistryLocationRun::start(qint64) noexcept {
    return impl_->result;
}
const AppContainerRegistryLocationRun::Result& AppContainerRegistryLocationRun::poll(qint64) noexcept {
    return impl_->result;
}
const AppContainerRegistryLocationRun::Result& AppContainerRegistryLocationRun::cancel(qint64) noexcept {
    return impl_->result;
}
const AppContainerRegistryLocationRun::Result& AppContainerRegistryLocationRun::result() const noexcept {
    return impl_->result;
}
} // namespace music_player::sandbox_probe

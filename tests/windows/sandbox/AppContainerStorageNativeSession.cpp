#include "AppContainerStorageNativeSession.hpp"

#include "AppContainerRegistryNative.hpp"
#include "AppContainerStorageSession.hpp"

#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Inventory = AppContainerStorageInventory;
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
class FileSession final : public Inventory::ReadSession {
  public:
    FileSession(AppContainerStorageFiles::RootPaths roots, AppContainerStorageFiles::Calls calls)
        : files_(std::move(roots), std::move(calls)) {}
    Inventory::RootResult root(const Inventory::Root root) override { return files_.root(root); }
    Inventory::Metadata readMetadata(const Inventory::NodeRef ref, const std::uint64_t maximum_bytes) override {
        return files_.readMetadata(ref, maximum_bytes);
    }
    Inventory::ChildResult nextChild(const Inventory::NodeRef parent, const std::uint32_t index,
                                     const std::uint32_t maximum_units) override {
        return files_.nextChild(parent, index, maximum_units);
    }
    Inventory::ValidationResult revalidate() override { return files_.revalidate(); }
    Inventory::CloseResult close() override { return files_.close(); }

  private:
    AppContainerStorageFiles files_;
};
} // namespace
std::unique_ptr<AppContainerStorageInventory::ReadSession>
appContainerStorageNativeSession(const music_player::m0::WindowsNativeMediaProfile::StorageLocation& location,
                                 AppContainerStorageFiles::Calls files, AppContainerStorageRegistry::Calls registry) {
    return std::make_unique<AppContainerStorageSession>(
        std::make_unique<FileSession>(
            AppContainerStorageFiles::RootPaths{owned(location.package_root), owned(location.ac_path)},
            std::move(files)),
        std::make_unique<AppContainerStorageRegistry>(
            AppContainerStorageRegistry::RootPaths{owned(location.mappings_key), owned(location.storage_key)},
            std::move(registry)));
}
std::unique_ptr<AppContainerStorageInventory::ReadSession>
appContainerStorageNativeSession(const music_player::m0::WindowsNativeMediaProfile::StorageLocation& location) {
    return appContainerStorageNativeSession(location, AppContainerStorageFiles::systemCalls(),
                                            appContainerRegistryNativeCalls());
}
} // namespace music_player::sandbox_probe

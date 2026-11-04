#include "WindowsDownloadReadInput.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstring>
#include <utility>

namespace music_player::m0 {
namespace {
bool regularFile(HANDLE file, FILE_ID_INFO& identity) {
    if (!file || file == INVALID_HANDLE_VALUE || ::GetFileType(file) != FILE_TYPE_DISK)
        return false;
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    LARGE_INTEGER size{};
    return ::GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes, sizeof(attributes)) &&
           !(attributes.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
           ::GetFileInformationByHandleEx(file, FileIdInfo, &identity, sizeof(identity)) &&
           ::GetFileSizeEx(file, &size) && size.QuadPart >= 0;
}
} // namespace

struct WindowsDownloadReadInput::Impl final {
    HANDLE file{INVALID_HANDLE_VALUE};
    std::atomic<bool> used{false};
    ~Impl() {
        if (file && file != INVALID_HANDLE_VALUE)
            ::CloseHandle(file);
    }
};
WindowsDownloadReadInput::WindowsDownloadReadInput(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WindowsDownloadReadInput::~WindowsDownloadReadInput() = default;
std::shared_ptr<const WindowsDownloadReadInput> WindowsDownloadReadInput::fromHandle(const std::uintptr_t borrowed) {
    try {
        const auto original = reinterpret_cast<HANDLE>(borrowed);
        FILE_ID_INFO before{};
        if (!regularFile(original, before))
            return {};
        auto impl = std::make_unique<Impl>();
        // A new file object gives the codec its own cursor. Neither the original
        // access mask nor its inherit flag is copied to this reduced capability.
        impl->file = ::ReOpenFile(original, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN);
        FILE_ID_INFO after{};
        if (!regularFile(impl->file, after) || before.VolumeSerialNumber != after.VolumeSerialNumber ||
            std::memcmp(&before.FileId, &after.FileId, sizeof(before.FileId)) != 0 ||
            !::SetHandleInformation(impl->file, HANDLE_FLAG_INHERIT, 0))
            return {};
        return std::shared_ptr<const WindowsDownloadReadInput>(new WindowsDownloadReadInput(std::move(impl)));
    } catch (...) {
        return {};
    }
}
std::uint64_t WindowsDownloadReadInput::handleValue() const noexcept {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(impl_->file));
}
bool WindowsDownloadReadInput::acquireUse() const noexcept {
    bool expected = false;
    if (!impl_->used.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    const LARGE_INTEGER start{};
    if (!::SetFilePointerEx(impl_->file, start, nullptr, FILE_BEGIN)) {
        impl_->used.store(false, std::memory_order_release);
        return false;
    }
    return true;
}
bool WindowsDownloadReadInput::beginInheritance() const noexcept {
    return impl_->used.load(std::memory_order_acquire) &&
           ::SetHandleInformation(impl_->file, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}
bool WindowsDownloadReadInput::endInheritance() const noexcept {
    return ::SetHandleInformation(impl_->file, HANDLE_FLAG_INHERIT, 0) != FALSE;
}
void WindowsDownloadReadInput::releaseUse() const noexcept {
    (void)endInheritance();
    impl_->used.store(false, std::memory_order_release);
}
} // namespace music_player::m0

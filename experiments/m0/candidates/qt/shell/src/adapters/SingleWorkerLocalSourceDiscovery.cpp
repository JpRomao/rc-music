#include "m0/adapters/SingleWorkerLocalSourceDiscovery.hpp"

#include "m0/domain/LocalAudioFilePolicy.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace music_player::m0 {
namespace {

constexpr auto shutdown_interrupt_retry_interval = std::chrono::milliseconds{5};

[[nodiscard]] LocalSourceDiscoveryIssueCode issueCodeFor(const std::error_code& error) noexcept {
    if (error == std::errc::no_such_file_or_directory)
        return LocalSourceDiscoveryIssueCode::SourceNotFound;
    if (error == std::errc::permission_denied)
        return LocalSourceDiscoveryIssueCode::AccessDenied;
    return LocalSourceDiscoveryIssueCode::FilesystemError;
}

#ifdef _WIN32
class UniqueWindowsHandle final {
  public:
    UniqueWindowsHandle() = default;
    explicit UniqueWindowsHandle(HANDLE handle) noexcept : handle_{handle} {}
    ~UniqueWindowsHandle() {
        if (valid())
            CloseHandle(handle_);
    }

    UniqueWindowsHandle(const UniqueWindowsHandle&) = delete;
    UniqueWindowsHandle& operator=(const UniqueWindowsHandle&) = delete;
    UniqueWindowsHandle(UniqueWindowsHandle&& other) noexcept : handle_{std::exchange(other.handle_, nullptr)} {}
    UniqueWindowsHandle& operator=(UniqueWindowsHandle&& other) noexcept {
        if (this == &other)
            return *this;
        if (valid())
            CloseHandle(handle_);
        handle_ = std::exchange(other.handle_, nullptr);
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

  private:
    HANDLE handle_{nullptr};
};

struct WindowsOpenedPath final {
    UniqueWindowsHandle handle{};
    BY_HANDLE_FILE_INFORMATION information{};
};

[[nodiscard]] bool sameFileIdentity(const BY_HANDLE_FILE_INFORMATION& left,
                                    const BY_HANDLE_FILE_INFORMATION& right) noexcept {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber && left.nFileIndexHigh == right.nFileIndexHigh &&
           left.nFileIndexLow == right.nFileIndexLow;
}

[[nodiscard]] std::optional<WindowsOpenedPath>
openWindowsPath(const std::filesystem::path& source_path, const bool directory_hint, std::error_code& error) noexcept {
    error.clear();
    const DWORD access = FILE_READ_ATTRIBUTES | (directory_hint ? FILE_LIST_DIRECTORY : 0);
    UniqueWindowsHandle handle{
        CreateFileW(source_path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle.valid()) {
        error = std::error_code{static_cast<int>(GetLastError()), std::system_category()};
        return std::nullopt;
    }

    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(handle.get(), &information) == FALSE) {
        error = std::error_code{static_cast<int>(GetLastError()), std::system_category()};
        return std::nullopt;
    }

    const bool directory = (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool reparse = (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (!directory_hint && directory && !reparse) {
        UniqueWindowsHandle directory_handle{
            CreateFileW(source_path.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (!directory_handle.valid()) {
            error = std::error_code{static_cast<int>(GetLastError()), std::system_category()};
            return std::nullopt;
        }
        BY_HANDLE_FILE_INFORMATION directory_information{};
        if (GetFileInformationByHandle(directory_handle.get(), &directory_information) == FALSE) {
            error = std::error_code{static_cast<int>(GetLastError()), std::system_category()};
            return std::nullopt;
        }
        if (!sameFileIdentity(information, directory_information) ||
            (directory_information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            error = std::make_error_code(std::errc::state_not_recoverable);
            return std::nullopt;
        }
        handle = std::move(directory_handle);
        information = directory_information;
    }
    return WindowsOpenedPath{std::move(handle), information};
}

[[nodiscard]] std::optional<LocalSourceDiscoveryIssueCode>
windowsBindingIssue(const std::filesystem::path& source_path,
                    const BY_HANDLE_FILE_INFORMATION& retained_information) noexcept {
    std::error_code open_error;
    auto current = openWindowsPath(source_path, false, open_error);
    if (!current.has_value())
        return issueCodeFor(open_error);
    if ((current->information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return LocalSourceDiscoveryIssueCode::ReparsePoint;
    if (!sameFileIdentity(current->information, retained_information))
        return LocalSourceDiscoveryIssueCode::FilesystemError;
    return std::nullopt;
}

using WindowsDirectoryEntryVisitor = std::function<bool(std::wstring, DWORD)>;

struct alignas(FILE_ID_BOTH_DIR_INFO) WindowsDirectoryBuffer final {
    std::array<std::byte, 64U * 1024U> bytes{};
};

[[nodiscard]] bool enumerateWindowsDirectory(const HANDLE directory, const WindowsDirectoryEntryVisitor& visitor,
                                             std::error_code& error) {
    error.clear();
    auto buffer = std::make_unique<WindowsDirectoryBuffer>();
    bool restart = true;
    for (;;) {
        const auto information_class = restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
        if (GetFileInformationByHandleEx(directory, information_class, buffer->bytes.data(),
                                         static_cast<DWORD>(buffer->bytes.size())) == FALSE) {
            const DWORD windows_error = GetLastError();
            if (windows_error == ERROR_NO_MORE_FILES)
                return true;
            error = std::error_code{static_cast<int>(windows_error), std::system_category()};
            return false;
        }
        restart = false;

        std::size_t offset = 0;
        for (;;) {
            constexpr std::size_t name_offset = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
            if (offset > buffer->bytes.size() - name_offset) {
                error = std::make_error_code(std::errc::illegal_byte_sequence);
                return false;
            }
            const auto* const entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer->bytes.data() + offset);
            const std::size_t remaining = buffer->bytes.size() - offset - name_offset;
            if (entry->FileNameLength % sizeof(wchar_t) != 0 || entry->FileNameLength > remaining) {
                error = std::make_error_code(std::errc::illegal_byte_sequence);
                return false;
            }
            std::wstring name{entry->FileName, entry->FileNameLength / sizeof(wchar_t)};
            if (name != L"." && name != L".." && !visitor(std::move(name), entry->FileAttributes))
                return false;
            if (entry->NextEntryOffset == 0)
                break;
            if (entry->NextEntryOffset < name_offset || entry->NextEntryOffset > buffer->bytes.size() - offset) {
                error = std::make_error_code(std::errc::illegal_byte_sequence);
                return false;
            }
            offset += entry->NextEntryOffset;
        }
    }
}
#endif

} // namespace

SingleWorkerLocalSourceDiscovery::SingleWorkerLocalSourceDiscovery(SingleWorkerLocalSourceDiscoveryOptions options)
    : options_(std::move(options)) {
    if (options_.batch_size == 0)
        throw std::invalid_argument{"discovery batch size must be positive"};
    if (options_.progress_interval == 0)
        throw std::invalid_argument{"discovery progress interval must be positive"};
    if (options_.maximum_depth == 0)
        throw std::invalid_argument{"discovery maximum depth must be positive"};

    worker_ = std::jthread{[this] {
        runWorker();
        {
            std::lock_guard lock{mutex_};
            worker_exited_ = true;
        }
        worker_exit_changed_.notify_all();
    }};
}

SingleWorkerLocalSourceDiscovery::~SingleWorkerLocalSourceDiscovery() {
    try {
        static_cast<void>(shutdown());
    } catch (...) {
    }
}

DiscoveryCommandResult SingleWorkerLocalSourceDiscovery::start(LocalSourceDiscoveryRequest request) {
    std::lock_guard shutdown_lock{shutdown_mutex_};
    std::lock_guard lock{mutex_};
    if (stopping_)
        return DiscoveryCommandResult::Shutdown;
    if (request.generation.value == 0 || request.roots.empty() ||
        std::ranges::any_of(request.roots, [](const std::filesystem::path& root) { return root.empty(); })) {
        return DiscoveryCommandResult::InvalidArgument;
    }
    if (request.generation.value <= highest_generation_.value)
        return DiscoveryCommandResult::StaleGeneration;
    if (current_state_ == CurrentState::Pending || current_state_ == CurrentState::Active ||
        current_state_ == CurrentState::Cancelling || current_state_ == CurrentState::Completed) {
        return DiscoveryCommandResult::Busy;
    }

    highest_generation_ = request.generation;
    current_state_ = CurrentState::Pending;
    mailbox_.reset();
    completion_.reset();
    pending_.emplace(std::move(request));
    changed_.notify_all();
    return DiscoveryCommandResult::Accepted;
}

DiscoveryCommandResult SingleWorkerLocalSourceDiscovery::cancel(const DiscoveryGeneration generation) {
    std::lock_guard shutdown_lock{shutdown_mutex_};
    bool interrupt_io = false;
    {
        std::lock_guard lock{mutex_};
        if (stopping_)
            return DiscoveryCommandResult::Shutdown;
        if (generation.value == 0)
            return DiscoveryCommandResult::InvalidArgument;
        if (generation != highest_generation_)
            return DiscoveryCommandResult::StaleGeneration;

        interrupt_io = current_state_ == CurrentState::Active || current_state_ == CurrentState::Cancelling;
        current_state_ = interrupt_io ? CurrentState::Cancelling : CurrentState::Cancelled;
        if (pending_.has_value() && pending_->generation == generation)
            pending_.reset();
        if (mailbox_.has_value() && mailbox_->generation == generation)
            mailbox_.reset();
        completion_.reset();
    }
    changed_.notify_all();
    if (interrupt_io)
        interruptWorkerIo();
    return DiscoveryCommandResult::Accepted;
}

DiscoveryPollResult SingleWorkerLocalSourceDiscovery::poll(const DiscoveryGeneration generation) {
    std::lock_guard shutdown_lock{shutdown_mutex_};
    std::unique_lock lock{mutex_};
    if (stopping_)
        return {.status = DiscoveryPollStatus::Shutdown};
    if (generation.value == 0 || generation.value > highest_generation_.value)
        return {.status = DiscoveryPollStatus::InvalidArgument};
    if (generation.value < highest_generation_.value)
        return {.status = DiscoveryPollStatus::StaleGeneration};
    if (mailbox_.has_value()) {
        auto batch = std::move(*mailbox_);
        mailbox_.reset();
        changed_.notify_all();
        return {.status = DiscoveryPollStatus::BatchReady, .batch = std::move(batch)};
    }
    if (current_state_ == CurrentState::Completed && completion_.has_value()) {
        auto summary = std::move(*completion_);
        completion_.reset();
        current_state_ = CurrentState::Idle;
        return {.status = DiscoveryPollStatus::Completed, .summary = std::move(summary)};
    }
    if (current_state_ == CurrentState::Cancelled)
        return {.status = DiscoveryPollStatus::Cancelled};
    if (current_state_ == CurrentState::Cancelling) {
        lock.unlock();
        interruptWorkerIo();
        return {.status = DiscoveryPollStatus::Pending};
    }
    if (current_state_ == CurrentState::Pending || current_state_ == CurrentState::Active)
        return {.status = DiscoveryPollStatus::Pending};
    return {.status = DiscoveryPollStatus::Idle};
}

DiscoveryCommandResult SingleWorkerLocalSourceDiscovery::shutdown() {
    std::lock_guard shutdown_lock{shutdown_mutex_};
    {
        std::lock_guard lock{mutex_};
        if (!stopping_) {
            stopping_ = true;
            current_state_ = CurrentState::Idle;
            pending_.reset();
            mailbox_.reset();
            completion_.reset();
        }
    }
    changed_.notify_all();
    if (worker_.joinable()) {
        std::unique_lock lock{mutex_};
        // CancelSynchronousIo can observe no pending request immediately before the worker enters its next one.
        // Retry until the worker explicitly confirms exit, and only then join it.
        while (!worker_exited_) {
            lock.unlock();
            interruptWorkerIo();
            lock.lock();
            worker_exit_changed_.wait_for(lock, shutdown_interrupt_retry_interval, [this] { return worker_exited_; });
        }
        lock.unlock();
        worker_.join();
    }
    return DiscoveryCommandResult::Accepted;
}

void SingleWorkerLocalSourceDiscovery::interruptWorkerIo() noexcept {
    if (!worker_.joinable())
        return;
    if (options_.cancel_synchronous_io) {
        try {
            options_.cancel_synchronous_io(worker_.native_handle());
        } catch (...) {
        }
        return;
    }
#ifdef _WIN32
    if (CancelSynchronousIo(worker_.native_handle()) == FALSE) {
        const DWORD error = GetLastError();
        // ERROR_NOT_FOUND means that the worker had no cancellable synchronous request at this instant.
        if (error == ERROR_NOT_FOUND)
            return;
    }
#endif
}

void SingleWorkerLocalSourceDiscovery::runWorker() {
    for (;;) {
        LocalSourceDiscoveryRequest request;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_)
                return;
            request = std::move(*pending_);
            pending_.reset();
            if (request.generation == highest_generation_ && current_state_ == CurrentState::Pending)
                current_state_ = CurrentState::Active;
        }

        LocalSourceDiscoverySummary summary;
        bool completed = false;
        try {
            completed = runDiscovery(request, summary);
        } catch (...) {
            ++summary.issue_count;
            completed = isCurrent(request.generation);
        }

        std::lock_guard lock{mutex_};
        if (!stopping_ && request.generation == highest_generation_ && current_state_ == CurrentState::Cancelling) {
            current_state_ = CurrentState::Cancelled;
            changed_.notify_all();
            continue;
        }
        if (!completed || stopping_ || request.generation != highest_generation_ ||
            current_state_ != CurrentState::Active) {
            continue;
        }
        completion_ = summary;
        current_state_ = CurrentState::Completed;
    }
}

bool SingleWorkerLocalSourceDiscovery::runDiscovery(const LocalSourceDiscoveryRequest& request,
                                                    LocalSourceDiscoverySummary& summary) {
    LocalSourceDiscoveryBatch batch{.generation = request.generation};
    std::uint64_t sequence = 0;

    const auto flush = [&](const bool force) -> bool {
        const auto item_count = batch.accepted_paths.size() + batch.issues.size();
        if (!force && item_count < options_.batch_size && batch.inspected_count < options_.progress_interval)
            return true;
        if (batch.inspected_count == 0 && item_count == 0)
            return true;
        batch.sequence = ++sequence;
        if (!publishBatch(std::move(batch)))
            return false;
        batch = LocalSourceDiscoveryBatch{.generation = request.generation};
        return true;
    };

    const auto recordIssue = [&](const LocalSourceDiscoveryIssueCode code,
                                 const std::filesystem::path& source_path) -> bool {
        ++summary.issue_count;
        batch.issues.push_back({code, source_path});
        return flush(false);
    };

    const auto beginInspection = [&](const std::filesystem::path& source_path) -> bool {
        if (!isCurrent(request.generation))
            return false;
        ++summary.inspected_count;
        ++batch.inspected_count;
        if (options_.before_inspect) {
            try {
                options_.before_inspect();
            } catch (...) {
                return recordIssue(LocalSourceDiscoveryIssueCode::FilesystemError, source_path) && false;
            }
        }
        return isCurrent(request.generation) && flush(false);
    };

    const auto recordAccepted = [&](const std::filesystem::path& source_path) -> bool {
        ++summary.accepted_count;
        batch.accepted_paths.push_back(source_path);
        return flush(false);
    };

#ifdef _WIN32
    std::function<bool(const std::filesystem::path&, std::size_t, WindowsOpenedPath)> scanOpenedDirectory;
    scanOpenedDirectory = [&](const std::filesystem::path& directory, const std::size_t depth,
                              WindowsOpenedPath opened) -> bool {
        if (options_.after_directory_handle_validated) {
            try {
                options_.after_directory_handle_validated(directory);
            } catch (...) {
                return recordIssue(LocalSourceDiscoveryIssueCode::FilesystemError, directory);
            }
        }
        if (const auto binding_issue = windowsBindingIssue(directory, opened.information); binding_issue.has_value()) {
            return recordIssue(*binding_issue, directory);
        }

        bool directory_isolated = false;
        std::error_code enumeration_error;
        const bool enumerated = enumerateWindowsDirectory(
            opened.handle.get(),
            [&](std::wstring name, const DWORD enumerated_attributes) {
                const auto source_path = directory / std::filesystem::path{std::move(name)};
                if (!beginInspection(source_path))
                    return false;

                if (const auto binding_issue = windowsBindingIssue(directory, opened.information);
                    binding_issue.has_value()) {
                    if (!recordIssue(*binding_issue, directory))
                        return false;
                    directory_isolated = true;
                    return false;
                }

                const bool directory_hint = (enumerated_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                std::error_code open_error;
                auto child = openWindowsPath(source_path, directory_hint, open_error);
                if (!child.has_value())
                    return recordIssue(issueCodeFor(open_error), source_path);
                if (const auto binding_issue = windowsBindingIssue(directory, opened.information);
                    binding_issue.has_value()) {
                    if (!recordIssue(*binding_issue, directory))
                        return false;
                    directory_isolated = true;
                    return false;
                }

                const DWORD attributes = child->information.dwFileAttributes;
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    return recordIssue(LocalSourceDiscoveryIssueCode::ReparsePoint, source_path);
                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    if (depth >= options_.maximum_depth)
                        return recordIssue(LocalSourceDiscoveryIssueCode::DepthLimitExceeded, source_path);
                    return scanOpenedDirectory(source_path, depth + 1, std::move(*child));
                }
                if ((attributes & FILE_ATTRIBUTE_DEVICE) != 0)
                    return recordIssue(LocalSourceDiscoveryIssueCode::UnsupportedEntry, source_path);
                if (LocalAudioFilePolicy::isCandidate(source_path))
                    return recordAccepted(source_path);
                return recordIssue(LocalSourceDiscoveryIssueCode::UnsupportedFile, source_path);
            },
            enumeration_error);
        if (!enumerated) {
            if (directory_isolated)
                return true;
            if (enumeration_error)
                return recordIssue(issueCodeFor(enumeration_error), directory);
            return false;
        }
        if (const auto binding_issue = windowsBindingIssue(directory, opened.information); binding_issue.has_value()) {
            return recordIssue(*binding_issue, directory);
        }
        return true;
    };
#else
    std::function<bool(const std::filesystem::path&, std::size_t)> scanDirectory;
    scanDirectory = [&](const std::filesystem::path& directory, const std::size_t depth) -> bool {
        std::error_code iterator_error;
        std::filesystem::directory_iterator iterator{directory, std::filesystem::directory_options::none,
                                                     iterator_error};
        if (iterator_error)
            return recordIssue(issueCodeFor(iterator_error), directory);

        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            const auto source_path = iterator->path();
            if (!beginInspection(source_path))
                return false;

            std::error_code status_error;
            const auto status = std::filesystem::symlink_status(source_path, status_error);
            if (status_error) {
                if (!recordIssue(issueCodeFor(status_error), source_path))
                    return false;
            } else if (status.type() == std::filesystem::file_type::symlink) {
                if (!recordIssue(LocalSourceDiscoveryIssueCode::ReparsePoint, source_path))
                    return false;
            } else if (std::filesystem::is_directory(status)) {
                if (depth >= options_.maximum_depth) {
                    if (!recordIssue(LocalSourceDiscoveryIssueCode::DepthLimitExceeded, source_path))
                        return false;
                } else if (!scanDirectory(source_path, depth + 1)) {
                    return false;
                }
            } else if (std::filesystem::is_regular_file(status)) {
                if (LocalAudioFilePolicy::isCandidate(source_path)) {
                    if (!recordAccepted(source_path))
                        return false;
                } else if (!recordIssue(LocalSourceDiscoveryIssueCode::UnsupportedFile, source_path)) {
                    return false;
                }
            } else if (!recordIssue(LocalSourceDiscoveryIssueCode::UnsupportedEntry, source_path)) {
                return false;
            }

            std::error_code increment_error;
            iterator.increment(increment_error);
            if (increment_error)
                return recordIssue(issueCodeFor(increment_error), directory);
        }
        return true;
    };
#endif

    for (const auto& root : request.roots) {
        if (!beginInspection(root))
            return false;

#ifdef _WIN32
        const DWORD path_attributes = GetFileAttributesW(root.c_str());
        const bool directory_hint =
            path_attributes != INVALID_FILE_ATTRIBUTES && (path_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::error_code open_error;
        auto opened = openWindowsPath(root, directory_hint, open_error);
        if (!opened.has_value()) {
            if (!recordIssue(issueCodeFor(open_error), root))
                return false;
            continue;
        }
        const DWORD attributes = opened->information.dwFileAttributes;
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            if (!recordIssue(LocalSourceDiscoveryIssueCode::ReparsePoint, root))
                return false;
        } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!scanOpenedDirectory(root, 0, std::move(*opened)))
                return false;
        } else if ((attributes & FILE_ATTRIBUTE_DEVICE) != 0) {
            if (!recordIssue(LocalSourceDiscoveryIssueCode::UnsupportedEntry, root))
                return false;
        } else if (!recordAccepted(root)) {
            return false;
        }
#else
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(root, status_error);
        if (status_error) {
            if (!recordIssue(issueCodeFor(status_error), root))
                return false;
            continue;
        }
        if (!std::filesystem::exists(status)) {
            if (!recordIssue(LocalSourceDiscoveryIssueCode::SourceNotFound, root))
                return false;
            continue;
        }
        if (status.type() == std::filesystem::file_type::symlink) {
            if (!recordIssue(LocalSourceDiscoveryIssueCode::ReparsePoint, root))
                return false;
        } else if (std::filesystem::is_regular_file(status)) {
            if (!recordAccepted(root))
                return false;
        } else if (std::filesystem::is_directory(status)) {
            if (!scanDirectory(root, 0))
                return false;
        } else if (!recordIssue(LocalSourceDiscoveryIssueCode::UnsupportedEntry, root)) {
            return false;
        }
#endif
    }

    return flush(true);
}

bool SingleWorkerLocalSourceDiscovery::publishBatch(LocalSourceDiscoveryBatch batch) {
    std::unique_lock lock{mutex_};
    changed_.wait(lock, [this, generation = batch.generation] {
        return stopping_ || generation != highest_generation_ || current_state_ != CurrentState::Active ||
               !mailbox_.has_value();
    });
    if (stopping_ || batch.generation != highest_generation_ || current_state_ != CurrentState::Active)
        return false;
    mailbox_ = std::move(batch);
    changed_.notify_all();
    return true;
}

bool SingleWorkerLocalSourceDiscovery::isCurrent(const DiscoveryGeneration generation) const {
    std::lock_guard lock{mutex_};
    return !stopping_ && generation == highest_generation_ && current_state_ == CurrentState::Active;
}

} // namespace music_player::m0

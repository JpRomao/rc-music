#include "PlayerDownloadComponentProbe.hpp"

#include <QDir>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {
using State = PlayerDownloadComponentState;
constexpr std::string_view marker_bytes{"RCMusicDownloadTools-v1\n"};
static_assert(marker_bytes.size() == 24);

class Handle final {
  public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    bool valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return handle_; }

  private:
    void reset() noexcept {
        if (valid())
            CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    HANDLE handle_;
};

std::wstring native(const QString& path) {
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}

bool reserved(QString component) {
    component = component.section(QLatin1Char('.'), 0, 0);
    while (component.endsWith(QLatin1Char(' ')))
        component.chop(1);
    for (const auto* name : {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}) {
        if (component.compare(QLatin1StringView{name}, Qt::CaseInsensitive) == 0)
            return true;
    }
    if (component.size() != 4 || (component.first(3).compare(QLatin1StringView{"COM"}, Qt::CaseInsensitive) != 0 &&
                                  component.first(3).compare(QLatin1StringView{"LPT"}, Qt::CaseInsensitive) != 0))
        return false;
    const auto suffix = component.at(3).unicode();
    return (suffix >= '1' && suffix <= '9') || suffix == 0x00b9 || suffix == 0x00b2 || suffix == 0x00b3;
}

std::optional<QString> applicationPath(const QString& original) {
    // Namespace/drive syntax and all DOS components are checked before drive
    // queries or file opens. No argv, environment, CWD or discovery is used.
    if (original.size() < 3 || original.size() > 32'700 || original.at(1) != QLatin1Char(':') ||
        (original.at(2) != QLatin1Char('/') && original.at(2) != QLatin1Char('\\')) ||
        !((original.at(0) >= QLatin1Char('A') && original.at(0) <= QLatin1Char('Z')) ||
          (original.at(0) >= QLatin1Char('a') && original.at(0) <= QLatin1Char('z'))))
        return {};
    auto path = QDir::fromNativeSeparators(original);
    if (QString::fromUtf8(path.toUtf8()) != path)
        return {};
    if (path.size() > 3 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (path.size() > 3) {
        const auto components = path.mid(3).split(QLatin1Char('/'));
        if (components.size() > 256)
            return {};
        for (const auto& component : components) {
            if (component.isEmpty() || component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')) ||
                reserved(component))
                return {};
            for (const auto unit : component) {
                if (unit.category() == QChar::Other_Control || QStringLiteral(":\"<>|?*").contains(unit))
                    return {};
            }
        }
    }
    const auto drive = QDir::toNativeSeparators(path.first(3)).toStdWString();
    if (GetDriveTypeW(drive.c_str()) != DRIVE_FIXED)
        return {};
    return path;
}

State missingOrFailed(const DWORD error, const bool absent_allowed) {
    return absent_allowed && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? State::Absent
                                                                                              : State::Failed;
}

State pinDirectory(const QString& path, const bool absent_allowed, std::vector<Handle>& pins) {
    Handle handle{CreateFileW(native(path).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr)};
    if (!handle.valid())
        return missingOrFailed(GetLastError(), absent_allowed);
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle.get(), &information))
        return State::Failed;
    if (GetFileType(handle.get()) != FILE_TYPE_DISK || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return State::Invalid;
    pins.push_back(std::move(handle));
    return State::Enabled;
}

State inspectMarker(const QString& path) {
    Handle file{CreateFileW(native(path).c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file.valid())
        return missingOrFailed(GetLastError(), true);
    BY_HANDLE_FILE_INFORMATION information{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file.get(), &information))
        return State::Failed;
    if (GetFileType(file.get()) != FILE_TYPE_DISK ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1)
        return State::Invalid;
    if (!GetFileSizeEx(file.get(), &size))
        return State::Failed;
    if (size.QuadPart != static_cast<LONGLONG>(marker_bytes.size()))
        return State::Invalid;
    // The only content read is this fixed 24-byte marker, after size/type/link
    // checks. Sharing refuses ordinary concurrent writes/deletes/renames.
    std::array<char, marker_bytes.size()> bytes{};
    DWORD read = 0;
    if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())
        return State::Failed;
    if (std::memcmp(bytes.data(), marker_bytes.data(), bytes.size()) != 0)
        return State::Invalid;
    if (!GetFileInformationByHandle(file.get(), &information) || !GetFileSizeEx(file.get(), &size))
        return State::Failed;
    if (information.nNumberOfLinks != 1 ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        size.QuadPart != static_cast<LONGLONG>(marker_bytes.size()))
        return State::Invalid;
    return State::Enabled;
}

State checked(const State value) {
    switch (value) {
    case State::Enabled:
    case State::Absent:
    case State::Invalid:
    case State::Failed:
        return value;
    }
    return State::Failed;
}
} // namespace

PlayerDownloadComponentState readPlayerDownloadComponentMarker(const QString& application_directory) {
    try {
        const auto application = applicationPath(application_directory);
        if (!application)
            return State::Invalid;
        std::vector<Handle> pins;
        auto prefix = application->first(3);
        auto state = pinDirectory(prefix, false, pins);
        if (state != State::Enabled)
            return state;
        if (application->size() > 3) {
            for (const auto& component : application->mid(3).split(QLatin1Char('/'))) {
                if (!prefix.endsWith(QLatin1Char('/')))
                    prefix += QLatin1Char('/');
                prefix += component;
                state = pinDirectory(prefix, false, pins);
                if (state != State::Enabled)
                    return state;
            }
        }
        const auto tools = *application + (application->endsWith(QLatin1Char('/')) ? QString{} : QStringLiteral("/")) +
                           QStringLiteral("download-tools");
        state = pinDirectory(tools, true, pins);
        if (state != State::Enabled)
            return state;
        return inspectMarker(tools + QStringLiteral("/installed-v1"));
    } catch (...) {
        return State::Failed;
    }
}

PlayerDownloadComponentState readInstalledPlayerDownloadComponentMarker() {
    try {
        std::array<wchar_t, 32768> image{};
        const auto count = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
        if (count == 0 || count >= image.size())
            return State::Failed;
        const auto path = QString::fromWCharArray(image.data(), static_cast<qsizetype>(count));
        const auto separator = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
        if (separator < 2)
            return State::Failed;
        return readPlayerDownloadComponentMarker(path.first(separator == 2 ? 3 : separator));
    } catch (...) {
        return State::Failed;
    }
}

struct PlayerDownloadComponentProbe::Impl final {
    PlayerDownloadComponentReader reader;
    std::thread worker;
    std::mutex mutex;
    std::optional<State> result;
    bool attempted{false}, sealed{false}, delivered{false};

    explicit Impl(PlayerDownloadComponentReader value) : reader(std::move(value)) {}
    ~Impl() {
        cancel();
        if (worker.joinable())
            worker.join();
    }
    bool start() {
        const std::scoped_lock lock(mutex);
        if (attempted || sealed)
            return false;
        attempted = true;
        if (!reader)
            return false;
        try {
            worker = std::thread([this, callback = std::move(reader)]() mutable {
                auto value = State::Failed;
                try {
                    value = checked(callback());
                } catch (...) {
                    value = State::Failed;
                }
                // Release callable captures on this worker, before publishing
                // even the private result. Native thread exit is checked below.
                callback = {};
                const std::scoped_lock guard(mutex);
                if (!sealed)
                    result = value;
            });
            return true;
        } catch (...) {
            sealed = true;
            return false;
        }
    }
    void cancel() {
        const std::scoped_lock lock(mutex);
        sealed = true;
        result.reset();
    }
    bool stopped() {
        // A completion flag alone can precede lambda/TLS teardown. The real
        // std::thread native handle signals only after that teardown finishes.
        return !worker.joinable() ||
               WaitForSingleObject(static_cast<HANDLE>(worker.native_handle()), 0) == WAIT_OBJECT_0;
    }
    std::optional<State> poll() {
        if (!stopped())
            return {};
        const std::scoped_lock lock(mutex);
        if (sealed || delivered || !result)
            return {};
        delivered = true;
        return std::exchange(result, std::nullopt);
    }
};
PlayerDownloadComponentProbe::PlayerDownloadComponentProbe(PlayerDownloadComponentReader reader)
    : impl_(std::make_unique<Impl>(std::move(reader))) {}
PlayerDownloadComponentProbe::~PlayerDownloadComponentProbe() = default;
bool PlayerDownloadComponentProbe::start() { return impl_->start(); }
std::optional<PlayerDownloadComponentState> PlayerDownloadComponentProbe::poll() { return impl_->poll(); }
void PlayerDownloadComponentProbe::cancel() { impl_->cancel(); }
bool PlayerDownloadComponentProbe::stopped() const { return impl_->stopped(); }
} // namespace music_player::m0

#include "adapters/QtWindowsDiagnosticLogStore.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QDir>

#include <array>
#include <charconv>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace music_player::m0 {
namespace {

constexpr std::string_view headerPrefix = "music-player-diagnostics-v1 created=";
constexpr std::string_view sequencePrefix = " sequence=";
constexpr qsizetype decimalWidth = 20;
constexpr qsizetype headerBytes =
    static_cast<qsizetype>(headerPrefix.size() + sequencePrefix.size()) + 2 * decimalWidth + 1;
constexpr std::size_t slotCount = 3;

class Handle final {
  public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            close();
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { close(); }
    [[nodiscard]] bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    void close() noexcept {
        if (valid()) {
            CloseHandle(std::exchange(value_, INVALID_HANDLE_VALUE));
        }
    }

  private:
    HANDLE value_;
};

struct Slot final {
    Handle handle{};
    std::uint64_t bytes{0};
    std::int64_t created_ms{0};
    std::uint64_t sequence{0};
    bool obsolete{false};
};

std::wstring nativePath(const QString& path) {
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}

bool safeRootSyntax(const QString& path) {
    if (path.size() < 4 || path.at(1) != QLatin1Char(':') || path.at(2) != QLatin1Char('/') ||
        !((path.at(0) >= QLatin1Char('A') && path.at(0) <= QLatin1Char('Z')) ||
          (path.at(0) >= QLatin1Char('a') && path.at(0) <= QLatin1Char('z')))) {
        return false;
    }
    for (const auto& component : path.mid(3).split(QLatin1Char('/'))) {
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..") ||
            component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' '))) {
            return false;
        }
        for (const auto character : component) {
            if (character.unicode() < 32 || QStringLiteral(":<>\"|?*").contains(character)) {
                return false;
            }
        }
    }
    const auto drive = QDir::toNativeSeparators(path.left(3)).toStdWString();
    const auto type = GetDriveTypeW(drive.c_str());
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK;
}

Handle pinDirectory(const QString& path, DWORD access = FILE_READ_ATTRIBUTES) {
    Handle handle(CreateFileW(nativePath(path).c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!handle.valid() || !GetFileInformationByHandle(handle.get(), &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return Handle{};
    }
    return handle;
}

bool pinRoot(const QString& root, std::vector<Handle>& pins) {
    if (!safeRootSyntax(root)) {
        return false;
    }
    QString prefix = root.left(3);
    auto drive = pinDirectory(prefix);
    if (!drive.valid()) {
        return false;
    }
    pins.push_back(std::move(drive));
    for (const auto& component : root.mid(3).split(QLatin1Char('/'))) {
        if (!prefix.endsWith(QLatin1Char('/'))) {
            prefix += QLatin1Char('/');
        }
        prefix += component;
        auto directory = pinDirectory(prefix);
        if (!directory.valid()) {
            return false;
        }
        pins.push_back(std::move(directory));
    }
    return true;
}

bool safeFile(const Handle& handle, std::uint64_t& bytes) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!handle.valid() || GetFileType(handle.get()) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(handle.get(), &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1 || !GetFileSizeEx(handle.get(), &size) || size.QuadPart < 0) {
        return false;
    }
    bytes = static_cast<std::uint64_t>(size.QuadPart);
    return true;
}

QString slotPath(const QString& directory, std::size_t index) {
    return directory + QStringLiteral("/diagnostic-%1.log").arg(index);
}

Handle openSlot(const QString& path, DWORD disposition, DWORD* openError = nullptr) {
    const auto native = nativePath(path);
    const auto value = CreateFileW(native.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
                                   disposition, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const auto error = value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (openError) {
        *openError = error;
    }
    return Handle(value);
}

QByteArray makeHeader(std::int64_t created_ms, std::uint64_t sequence) {
    return QByteArray::fromStdString(std::string(headerPrefix)) +
           QByteArray::number(static_cast<qlonglong>(created_ms)).rightJustified(decimalWidth, '0') +
           QByteArray::fromStdString(std::string(sequencePrefix)) +
           QByteArray::number(static_cast<qulonglong>(sequence)).rightJustified(decimalWidth, '0') + '\n';
}

template <typename Integer> bool parseDecimal(std::string_view text, Integer& value) noexcept {
    for (const auto character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool readSlot(Slot& slot) noexcept {
    if (!safeFile(slot.handle, slot.bytes) || slot.bytes < static_cast<std::uint64_t>(headerBytes)) {
        return false;
    }
    std::array<char, static_cast<std::size_t>(headerBytes)> buffer{};
    DWORD readBytes = 0;
    if (!ReadFile(slot.handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, nullptr) ||
        readBytes != buffer.size()) {
        return false;
    }
    const std::string_view header(buffer.data(), buffer.size());
    const auto separator = headerPrefix.size() + decimalWidth;
    return header.starts_with(headerPrefix) && header.substr(separator).starts_with(sequencePrefix) &&
           header.back() == '\n' && parseDecimal(header.substr(headerPrefix.size(), decimalWidth), slot.created_ms) &&
           parseDecimal(header.substr(separator + sequencePrefix.size(), decimalWidth), slot.sequence) &&
           slot.sequence > 0;
}

bool eraseSlot(Slot& slot) noexcept {
    std::uint64_t unused = 0;
    if (!safeFile(slot.handle, unused)) {
        return false;
    }
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(slot.handle.get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
        return false;
    }
    slot.handle.close();
    return true;
}

bool older(const Slot& left, const Slot& right) noexcept {
    return left.created_ms < right.created_ms ||
           (left.created_ms == right.created_ms && left.sequence < right.sequence);
}

bool writeRecord(Slot& slot, const QByteArray& bytes, bool replace) noexcept {
    std::uint64_t originalSize = 0;
    if (!safeFile(slot.handle, originalSize)) {
        return false;
    }
    LARGE_INTEGER position{};
    if (!SetFilePointerEx(slot.handle.get(), position, nullptr, replace ? FILE_BEGIN : FILE_END) ||
        (replace && !SetEndOfFile(slot.handle.get()))) {
        return false;
    }
    DWORD written = 0;
    if (WriteFile(slot.handle.get(), bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(slot.handle.get())) {
        return true;
    }
    // Best-effort rollback of an append. Replacement failure can lose only this
    // already validated owned slot; it never authorizes another target.
    if (!replace) {
        position.QuadPart = static_cast<LONGLONG>(originalSize);
        if (SetFilePointerEx(slot.handle.get(), position, nullptr, FILE_BEGIN)) {
            SetEndOfFile(slot.handle.get());
        }
    }
    return false;
}

bool appendRecord(const QString& root, const QtWindowsDiagnosticLogStoreOptions& options,
                  const DiagnosticRecord& record) {
    if (!validDiagnosticLogLimits(options.limits)) {
        return false;
    }
    const auto now = options.clock ? options.clock() : QDateTime::currentMSecsSinceEpoch();
    const auto line = QByteArray::fromStdString(formatDiagnosticRecord(record));
    if (now < 0 || line.isEmpty() || static_cast<std::size_t>(line.size()) > maxDiagnosticRecordBytes ||
        !line.endsWith('\n') ||
        !canAppendDiagnosticRecord(headerBytes, static_cast<std::uint64_t>(line.size()),
                                   options.limits.max_file_bytes)) {
        return false;
    }

    // Ancestor handles deny rename/delete and write opens. The child also
    // requests write access, serializing writers without a separate lock file.
    std::vector<Handle> pins;
    if (!pinRoot(root, pins)) {
        return false;
    }
    const auto directory = root + QStringLiteral("/diagnostics");
    if (!CreateDirectoryW(nativePath(directory).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    auto directoryLock = pinDirectory(directory, GENERIC_READ | GENERIC_WRITE);
    if (!directoryLock.valid()) {
        return false;
    }

    std::array<Slot, slotCount> managed_slots;
    std::uint64_t sequence = 0;
    std::size_t newest = slotCount;
    // Preflight all reserved names, including slots outside a reduced limit,
    // before modifying any owned file. Unknown headers are never overwritten.
    for (std::size_t i = 0; i < managed_slots.size(); ++i) {
        auto& slot = managed_slots[i];
        DWORD openError = ERROR_SUCCESS;
        slot.handle = openSlot(slotPath(directory, i), OPEN_EXISTING, &openError);
        if (!slot.handle.valid()) {
            if (openError != ERROR_FILE_NOT_FOUND) {
                return false;
            }
            continue;
        }
        if (!readSlot(slot)) {
            return false;
        }
        sequence = qMax(sequence, slot.sequence);
        slot.obsolete = i >= options.limits.max_files || slot.bytes > options.limits.max_file_bytes ||
                        diagnosticLogExpired(slot.created_ms, now, options.limits.max_age_ms);
        if (!slot.obsolete && (newest == slotCount || older(managed_slots[newest], slot))) {
            newest = i;
        }
    }

    const bool replace = newest == slotCount || !canAppendDiagnosticRecord(managed_slots[newest].bytes,
                                                                           static_cast<std::uint64_t>(line.size()),
                                                                           options.limits.max_file_bytes);
    auto target = newest;
    QByteArray bytes = line;
    if (replace) {
        if (sequence == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        target = 0;
        for (std::size_t i = 0; i < options.limits.max_files; ++i) {
            if (!managed_slots[i].handle.valid() || managed_slots[i].obsolete) {
                target = i;
                break;
            }
            if (older(managed_slots[i], managed_slots[target])) {
                target = i;
            }
        }
        bytes.prepend(makeHeader(now, sequence + 1));
    }
    for (std::size_t i = 0; i < managed_slots.size(); ++i) {
        if (i != target && managed_slots[i].handle.valid() && managed_slots[i].obsolete &&
            !eraseSlot(managed_slots[i])) {
            return false;
        }
    }
    if (!managed_slots[target].handle.valid()) {
        managed_slots[target].handle = openSlot(slotPath(directory, target), CREATE_NEW);
        if (!managed_slots[target].handle.valid()) {
            return false;
        }
    }
    return writeRecord(managed_slots[target], bytes, replace);
}

} // namespace

QtWindowsDiagnosticLogStore::QtWindowsDiagnosticLogStore(QString config_root,
                                                         QtWindowsDiagnosticLogStoreOptions options)
    : config_root_(std::move(config_root)), options_(std::move(options)) {}

bool QtWindowsDiagnosticLogStore::append(const DiagnosticRecord& record) noexcept {
    try {
        return appendRecord(QDir::fromNativeSeparators(config_root_), options_, record);
    } catch (...) {
        return false;
    }
}

} // namespace music_player::m0

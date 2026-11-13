#include "adapters/WindowsDownloadWorkspace.hpp"
#include "adapters/DownloadOwnershipJson.hpp"
#include "adapters/WindowsDownloadReadInput.hpp"
#include "m0/domain/DownloadDirectory.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QScopedValueRollback>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
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

constexpr std::uint64_t maximumWorkspaceBytes = 512ULL * 1024 * 1024;
constexpr std::uint64_t writeBlockBytes = 1024 * 1024;

class Handle final {
  public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            close();
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    ~Handle() { close(); }
    [[nodiscard]] bool valid() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    void close() noexcept {
        if (valid()) {
            CloseHandle(std::exchange(value_, INVALID_HANDLE_VALUE));
        }
    }

  private:
    HANDLE value_;
};

std::wstring nativePath(const QString& path) {
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}

void trimFilenameEdges(QString& stem) {
    const auto boundary = [](const QChar unit) { return unit.isSpace() || unit == QLatin1Char('.'); };
    qsizetype begin = 0;
    auto end = stem.size();
    while (begin < end && boundary(stem[begin]))
        ++begin;
    while (end > begin && boundary(stem[end - 1]))
        --end;
    stem = stem.mid(begin, end - begin);
}

QString safePublicationStem(QString suggested) {
    if (suggested.endsWith(QLatin1String(".mp3"), Qt::CaseInsensitive))
        suggested.chop(4);
    for (qsizetype index = 0; index < suggested.size(); ++index) {
        const auto unit = suggested.at(index);
        const auto value = unit.unicode();
        if (value <= 0x1f || (value >= 0x7f && value <= 0x9f) || (value >= 0x2028 && value <= 0x202e) ||
            (value >= 0x2066 && value <= 0x2069) || QStringLiteral("/\\:<>\"|?*").contains(unit))
            suggested[index] = QLatin1Char('_');
    }
    constexpr auto maximum_stem =
        maximum_download_publication_leaf_units - 4 - 6 - 1; // Extension, suffix and device prefix.
    if (suggested.size() > maximum_stem) {
        suggested.truncate(maximum_stem);
        if (suggested.back().isHighSurrogate())
            suggested.chop(1);
    }
    trimFilenameEdges(suggested);
    if (suggested.isEmpty())
        suggested = QStringLiteral("RC Music");
    // A device basename (possibly before another extension) needs a prefix.
    if (!validDownloadPublicationLeaf(suggested + QStringLiteral(".mp3")))
        suggested.prepend(QLatin1Char('_'));
    return suggested;
}

QString availablePublicationLeaf(const QString& parent, const QString& suggested, const QStringList& reserved) {
    const auto stem = safePublicationStem(suggested);
    for (int index = 1; index <= 129; ++index) {
        auto name = stem;
        if (index > 1)
            name += QStringLiteral(" (%1)").arg(index);
        name += QStringLiteral(".mp3");
        if (!validDownloadPublicationLeaf(name))
            break;
        if (reserved.contains(name, Qt::CaseInsensitive))
            continue;
        const auto path = nativePath(parent + QLatin1Char('/') + name);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                return name;
            break;
        }
    }
    return {};
}

bool safeParentSyntax(const QString& path) {
    if (path.size() < 4 || path.size() > 30'000 || path.at(1) != QLatin1Char(':') || path.at(2) != QLatin1Char('/') ||
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
    DWORD flags = 0;
    return GetDriveTypeW(drive.c_str()) == DRIVE_FIXED &&
           GetVolumeInformationW(drive.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) &&
           (flags & FILE_PERSISTENT_ACLS) != 0;
}

bool sameIdentity(const BY_HANDLE_FILE_INFORMATION& left, const BY_HANDLE_FILE_INFORMATION& right) noexcept {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber && left.nFileIndexHigh == right.nFileIndexHigh &&
           left.nFileIndexLow == right.nFileIndexLow;
}

bool validJobId(const std::string& value) noexcept {
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::optional<DownloadFileIdentity> physicalIdentity(const HANDLE handle, const bool directory) noexcept {
    BY_HANDLE_FILE_INFORMATION basic{};
    FILE_ID_INFO information{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &basic) ||
        (basic.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((basic.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        (!directory && basic.nNumberOfLinks != 1) ||
        !GetFileInformationByHandleEx(handle, FileIdInfo, &information, sizeof(information)))
        return std::nullopt;
    DownloadFileIdentity result{.volume_serial = information.VolumeSerialNumber};
    std::memcpy(result.file_id.data(), information.FileId.Identifier, result.file_id.size());
    return result;
}

QString finalPath(HANDLE handle) {
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    if (required == 0 || required > 32'768) {
        return {};
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1);
    const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (written == 0 || written >= buffer.size()) {
        return {};
    }
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written));
}

Handle pinDirectory(const QString& path, BY_HANDLE_FILE_INFORMATION* identity = nullptr, DWORD extra_access = 0,
                    DWORD sharing = FILE_SHARE_READ) {
    Handle handle(CreateFileW(nativePath(path).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | extra_access,
                              sharing, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!handle.valid() || GetFileType(handle.get()) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(handle.get(), &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return Handle{};
    }
    if (identity) {
        *identity = info;
    }
    return handle;
}

bool pinParent(const QString& parent, std::vector<Handle>& pins, BY_HANDLE_FILE_INFORMATION& identity,
               DWORD leaf_sharing = FILE_SHARE_READ, DWORD leaf_access = 0) {
    QString prefix = parent.left(3);
    auto drive = pinDirectory(prefix);
    if (!drive.valid()) {
        return false;
    }
    pins.push_back(std::move(drive));
    for (const auto& component : parent.mid(3).split(QLatin1Char('/'))) {
        if (!prefix.endsWith(QLatin1Char('/'))) {
            prefix += QLatin1Char('/');
        }
        prefix += component;
        auto directory = pinDirectory(prefix, &identity, prefix == parent ? leaf_access : 0,
                                      prefix == parent ? leaf_sharing : FILE_SHARE_READ);
        if (!directory.valid()) {
            return false;
        }
        pins.push_back(std::move(directory));
    }
    return true;
}

bool samePublicationVolume(const QString& staging, const QString& publication, std::vector<Handle>& staging_pins,
                           std::vector<Handle>& publication_pins, BY_HANDLE_FILE_INFORMATION& publication_id) {
    BY_HANDLE_FILE_INFORMATION staging_id{};
    return safeParentSyntax(staging) && safeParentSyntax(publication) && pinParent(staging, staging_pins, staging_id) &&
           pinParent(publication, publication_pins, publication_id, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     FILE_ADD_FILE | SYNCHRONIZE) &&
           staging_id.dwVolumeSerialNumber == publication_id.dwVolumeSerialNumber;
}

bool outsideStaging(const HANDLE staging, const HANDLE publication) {
    const auto staging_path = finalPath(staging);
    const auto publication_path = finalPath(publication);
    return !staging_path.isEmpty() && !publication_path.isEmpty() &&
           publication_path.compare(staging_path, Qt::CaseInsensitive) != 0 &&
           !publication_path.startsWith(staging_path + QLatin1Char('\\'), Qt::CaseInsensitive);
}

class PrivateSecurity final {
  public:
    bool initialize() {
        HANDLE raw_token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
            return false;
        }
        Handle token(raw_token);
        DWORD required = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
        if (required == 0) {
            return false;
        }
        user_.resize(required);
        if (!GetTokenInformation(token.get(), TokenUser, user_.data(), required, &required)) {
            return false;
        }
        const auto* user = reinterpret_cast<const TOKEN_USER*>(user_.data());
        DWORD system_size = static_cast<DWORD>(system_.size());
        if (!IsValidSid(user->User.Sid) ||
            !CreateWellKnownSid(WinLocalSystemSid, nullptr, system_.data(), &system_size)) {
            return false;
        }
        const DWORD ace_flags = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
        const auto acl_size = sizeof(ACL) + 2 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) +
                              GetLengthSid(user->User.Sid) + GetLengthSid(system_.data());
        acl_.resize(acl_size);
        auto* acl = reinterpret_cast<PACL>(acl_.data());
        if (!InitializeAcl(acl, static_cast<DWORD>(acl_.size()), ACL_REVISION) ||
            !AddAccessAllowedAceEx(acl, ACL_REVISION, ace_flags, FILE_ALL_ACCESS, user->User.Sid) ||
            !AddAccessAllowedAceEx(acl, ACL_REVISION, ace_flags, FILE_ALL_ACCESS, system_.data()) ||
            !InitializeSecurityDescriptor(&descriptor_, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorOwner(&descriptor_, user->User.Sid, FALSE) ||
            !SetSecurityDescriptorDacl(&descriptor_, TRUE, acl, FALSE) ||
            !SetSecurityDescriptorControl(&descriptor_, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
            return false;
        }
        attributes_ = {sizeof(SECURITY_ATTRIBUTES), &descriptor_, FALSE};
        return true;
    }
    SECURITY_ATTRIBUTES* attributes() noexcept { return &attributes_; }

  private:
    std::vector<std::byte> user_{};
    std::array<std::byte, SECURITY_MAX_SID_SIZE> system_{};
    std::vector<std::byte> acl_{};
    SECURITY_DESCRIPTOR descriptor_{};
    SECURITY_ATTRIBUTES attributes_{};
};

struct OwnedFile final {
    QString path{};
    Handle handle{};
    BY_HANDLE_FILE_INFORMATION identity{};
    std::uint64_t bytes{0};
    bool sealed{false};
    bool published{false};
    bool delete_access{false};
};

bool inspectFile(const OwnedFile& file) noexcept {
    BY_HANDLE_FILE_INFORMATION information{};
    LARGE_INTEGER size{};
    return file.handle.valid() && GetFileType(file.handle.get()) == FILE_TYPE_DISK &&
           GetFileInformationByHandle(file.handle.get(), &information) &&
           !(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
           information.nNumberOfLinks == 1 && sameIdentity(information, file.identity) &&
           GetFileSizeEx(file.handle.get(), &size) && size.QuadPart >= 0 &&
           static_cast<std::uint64_t>(size.QuadPart) == file.bytes;
}

bool createFile(OwnedFile& file, PrivateSecurity& security) {
    // QFile/CRT readers do not generally share DELETE. The writer handle denies
    // external writers and rename/delete. Later DELETE reacquisition proves ID.
    file.handle = Handle(CreateFileW(nativePath(file.path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                                     security.attributes(), CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    return file.handle.valid() && GetFileInformationByHandle(file.handle.get(), &file.identity) && inspectFile(file);
}

bool acquireDeleteHandle(OwnedFile& file) {
    if (file.handle.valid() && !inspectFile(file)) {
        return false;
    }
    if (file.handle.valid() && file.delete_access) {
        return true;
    }
    file.handle.close();
    file.handle = Handle(CreateFileW(nativePath(file.path).c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                                     FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    file.delete_access = file.handle.valid();
    return inspectFile(file);
}

std::string sealedHash(const OwnedFile& file) {
    if (!file.sealed || !inspectFile(file) || file.bytes == 0 || file.bytes > maximumWorkspaceBytes)
        return {};
    LARGE_INTEGER begin{};
    if (!SetFilePointerEx(file.handle.get(), begin, nullptr, FILE_BEGIN))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> block{};
    std::uint64_t consumed = 0;
    while (consumed < file.bytes) {
        DWORD count = 0;
        const auto wanted = static_cast<DWORD>(std::min<std::uint64_t>(block.size(), file.bytes - consumed));
        if (!ReadFile(file.handle.get(), block.data(), wanted, &count, nullptr) || count == 0 || count > wanted)
            return {};
        hash.addData(QByteArrayView{block.data(), static_cast<qsizetype>(count)});
        consumed += count;
    }
    return inspectFile(file) ? hash.result().toHex().toStdString() : std::string{};
}

class FileSink final : public QIODevice {
  public:
    FileSink(OwnedFile& file, std::uint64_t& total, const std::uint64_t limit, DownloadFailure& error)
        : file_(file), total_(total), limit_(limit), error_(error) {
        open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    }
    bool isSequential() const override { return false; }
    qint64 size() const override { return static_cast<qint64>(file_.bytes); }
    bool seek(qint64 position) override { return isOpen() && position == size() && QIODevice::seek(position); }

  protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* bytes, qint64 length) override {
        if (error_ != DownloadFailure::None || file_.sealed || length < 0 || !inspectFile(file_)) {
            if (error_ == DownloadFailure::None) {
                error_ = DownloadFailure::InvalidResult;
            }
            return -1;
        }
        const auto requested = static_cast<std::uint64_t>(length);
        if (total_ > limit_ || requested > limit_ - total_) {
            error_ = DownloadFailure::LimitExceeded;
            return -1;
        }
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(file_.bytes);
        if (!SetFilePointerEx(file_.handle.get(), position, nullptr, FILE_BEGIN)) {
            error_ = DownloadFailure::Publication;
            return -1;
        }
        std::uint64_t written_total = 0;
        while (written_total < requested) {
            const auto block = static_cast<DWORD>(std::min(writeBlockBytes, requested - written_total));
            DWORD written = 0;
            const bool success =
                WriteFile(file_.handle.get(), bytes + written_total, block, &written, nullptr) != FALSE;
            file_.bytes += written;
            total_ += written;
            written_total += written;
            if (!success || written != block) {
                error_ = DownloadFailure::Publication;
                return -1;
            }
        }
        return length;
    }

  private:
    OwnedFile& file_;
    std::uint64_t& total_;
    std::uint64_t limit_;
    DownloadFailure& error_;
};

} // namespace

struct WindowsDownloadWorkspace::Impl final {
    explicit Impl(WindowsDownloadWorkspaceConfig settings) : config(std::move(settings)) {}

    WindowsDownloadWorkspaceConfig config;
    std::vector<Handle> staging_pins{};
    std::vector<Handle> publication_pins{};
    Handle job_directory{};
    BY_HANDLE_FILE_INFORMATION job_identity{};
    BY_HANDLE_FILE_INFORMATION publication_identity{};
    QString job_path{};
    QString planned_publication{};
    QString publication_leaf{};
    QString publication_canonical{};
    OwnedFile input{};
    OwnedFile output{};
    std::unique_ptr<FileSink> input_sink{};
    std::unique_ptr<FileSink> output_sink{};
    std::uint64_t total_bytes{0};
    DownloadFailure failure{DownloadFailure::None};
    bool created{false};
    bool cleanup_requested{false};
    bool operating{false};
    DownloadOwnershipStore* ownership{nullptr};
    std::string job_id{};
    std::optional<DownloadOwnershipRecord> last_confirmed{};
    QString retained_publication{};
    bool recording{false};
    bool physically_cleaned{false};
    bool cleanup_record_attempted{false};
    bool cleanup_record_saved{false};

    bool fail(DownloadFailure value) noexcept {
        if (failure == DownloadFailure::None) {
            failure = value;
        }
        return false;
    }

    bool persist(DownloadOwnershipRecord record) noexcept {
        if (!ownership)
            return true;
        if (recording || !validDownloadOwnershipRecord(record))
            return fail(DownloadFailure::Publication);
        const QScopedValueRollback guard(recording, true);
        try {
            if (ownership->record(record) != DownloadCheckpointSaveStatus::Saved)
                return fail(DownloadFailure::Publication);
            // No readback and no upgrade of a Failed-after-commit result.
            last_confirmed = std::move(record);
            return true;
        } catch (...) {
            return fail(DownloadFailure::Publication);
        }
    }

    bool confirmCleanup() noexcept {
        if (!ownership || !last_confirmed)
            return true;
        if (cleanup_record_attempted)
            return cleanup_record_saved;
        if (recording)
            return false; // Defer the store call until its current callback returns.
        cleanup_record_attempted = true;
        try {
            auto record = *last_confirmed;
            record.cleanup_complete = true;
            cleanup_record_saved = persist(std::move(record));
            return cleanup_record_saved;
        } catch (...) {
            return fail(DownloadFailure::Publication);
        }
    }

    bool publicationDirectoryValid() const {
        BY_HANDLE_FILE_INFORMATION information{};
        return !publication_pins.empty() && GetFileInformationByHandle(publication_pins.back().get(), &information) &&
               (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
               !(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
               sameIdentity(information, publication_identity) &&
               finalPath(publication_pins.back().get()) == publication_canonical;
    }

    bool seal(OwnedFile& file, FileSink* sink) {
        if (!created || cleanup_requested || failure != DownloadFailure::None || !sink || !inspectFile(file) ||
            file.bytes == 0) {
            return fail(DownloadFailure::InvalidResult);
        }
        if (file.sealed) {
            return true;
        }
        if (!FlushFileBuffers(file.handle.get())) {
            return fail(DownloadFailure::Publication);
        }
        file.sealed = true;
        sink->close();
        return true;
    }

    std::shared_ptr<const WindowsDownloadReadInput> read(const OwnedFile& file) const {
        if (!created || cleanup_requested || failure != DownloadFailure::None || !file.sealed || file.published ||
            !inspectFile(file))
            return {};
        const auto capability =
            WindowsDownloadReadInput::fromHandle(reinterpret_cast<std::uintptr_t>(file.handle.get()));
        return capability && inspectFile(file) ? capability : std::shared_ptr<const WindowsDownloadReadInput>{};
    }

    bool erase(OwnedFile& file) {
        if (file.path.isEmpty()) {
            return true;
        }
        if (file.published) {
            file.handle.close();
            file.path.clear();
            return true;
        }
        if (!acquireDeleteHandle(file)) {
            file.handle.close();
            return false;
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(file.handle.get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
            return false;
        }
        file.handle.close();
        file.path.clear();
        return true;
    }
};

WindowsDownloadWorkspace::WindowsDownloadWorkspace(WindowsDownloadWorkspaceConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
WindowsDownloadWorkspace::WindowsDownloadWorkspace(WindowsDownloadWorkspaceConfig config, std::string job_id,
                                                   DownloadOwnershipStore& ownership)
    : WindowsDownloadWorkspace(std::move(config)) {
    impl_->ownership = &ownership;
    impl_->job_id = std::move(job_id);
}
WindowsDownloadWorkspace::~WindowsDownloadWorkspace() { static_cast<void>(cleanup()); }

bool validateWindowsDownloadDirectory(const QString& staging, const QString& publication) {
    try {
        const auto directory = QDir::fromNativeSeparators(publication);
        const auto bytes = directory.toStdString();
        if (!validDownloadDirectory(bytes) || QString::fromStdString(bytes) != directory)
            return false;
        std::vector<Handle> staging_pins, publication_pins;
        BY_HANDLE_FILE_INFORMATION publication_id{};
        return samePublicationVolume(QDir::fromNativeSeparators(staging), directory, staging_pins, publication_pins,
                                     publication_id) &&
               outsideStaging(staging_pins.back().get(), publication_pins.back().get());
    } catch (...) {
        return false;
    }
}

bool WindowsDownloadWorkspace::create(const std::function<bool()>& continue_allowed) {
    if (impl_->created || impl_->cleanup_requested || impl_->operating || impl_->failure != DownloadFailure::None) {
        return false;
    }
    const QScopedValueRollback operation(impl_->operating, true);
    try {
        impl_->config.staging_parent = QDir::fromNativeSeparators(impl_->config.staging_parent);
        impl_->config.publish_parent = QDir::fromNativeSeparators(impl_->config.publish_parent);
        const auto custom_parent = QDir::fromNativeSeparators(impl_->config.custom_publish_parent);
        const auto custom_bytes = custom_parent.toStdString();
        if (!custom_parent.isEmpty()) {
            if (!validDownloadDirectory(custom_bytes) || QString::fromStdString(custom_bytes) != custom_parent)
                return impl_->fail(DownloadFailure::InvalidResult);
            impl_->config.publish_parent = custom_parent;
        }
        if ((impl_->ownership && !validJobId(impl_->job_id)) || impl_->config.max_workspace_bytes == 0 ||
            impl_->config.max_workspace_bytes > maximumWorkspaceBytes) {
            return impl_->fail(DownloadFailure::InvalidResult);
        }
        if (!samePublicationVolume(impl_->config.staging_parent, impl_->config.publish_parent, impl_->staging_pins,
                                   impl_->publication_pins, impl_->publication_identity) ||
            (!custom_parent.isEmpty() &&
             !outsideStaging(impl_->staging_pins.back().get(), impl_->publication_pins.back().get()))) {
            impl_->fail(DownloadFailure::InvalidResult);
            static_cast<void>(cleanup());
            return false;
        }
        PrivateSecurity security;
        impl_->publication_canonical = finalPath(impl_->publication_pins.back().get());
        if (impl_->publication_canonical.isEmpty()) {
            impl_->fail(DownloadFailure::InvalidResult);
            static_cast<void>(cleanup());
            return false;
        }
        if (!security.initialize()) {
            impl_->fail(DownloadFailure::Publication);
            static_cast<void>(cleanup());
            return false;
        }
        std::optional<DownloadOwnershipRecord> ready_record;
        if (impl_->ownership) {
            const auto staging = physicalIdentity(impl_->staging_pins.back().get(), true);
            const auto publication = physicalIdentity(impl_->publication_pins.back().get(), true);
            if (!staging || !publication) {
                impl_->fail(DownloadFailure::Publication);
                static_cast<void>(cleanup());
                return false;
            }
            ready_record = DownloadOwnershipRecord{.job_id = impl_->job_id,
                                                   .staging_parent = *staging,
                                                   .publish_parent = *publication,
                                                   .maximum_workspace_bytes = impl_->config.max_workspace_bytes,
                                                   .publication_directory = custom_bytes};
        }
        QString selected_leaf;
        if (!impl_->config.publication_name.isEmpty()) {
            QStringList reserved;
            if (ready_record) {
                const auto journal = impl_->ownership->load();
                if (journal.status == DownloadCheckpointLoadStatus::Loaded && journal.journal &&
                    validDownloadOwnershipJournal(*journal.journal)) {
                    for (const auto& record : journal.journal->records) {
                        if (record.publish_parent != ready_record->publish_parent)
                            continue;
                        reserved.push_back(
                            record.publication_leaf.empty()
                                ? QStringLiteral("rc-music-%1.mp3").arg(QString::fromStdString(record.workspace_id))
                                : QString::fromStdString(record.publication_leaf));
                    }
                } else if (journal.status != DownloadCheckpointLoadStatus::Missing) {
                    impl_->fail(DownloadFailure::Publication);
                    static_cast<void>(cleanup());
                    return false;
                }
            }
            selected_leaf =
                availablePublicationLeaf(impl_->config.publish_parent, impl_->config.publication_name, reserved);
            if (selected_leaf.isEmpty()) {
                impl_->fail(DownloadFailure::Publication);
                static_cast<void>(cleanup());
                return false;
            }
        }
        for (int attempt = 0; attempt < 16; ++attempt) {
            const auto uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const auto path = impl_->config.staging_parent + QStringLiteral("/rc-music-job-") + uuid;
            impl_->publication_leaf =
                selected_leaf.isEmpty() ? QStringLiteral("rc-music-") + uuid + QStringLiteral(".mp3") : selected_leaf;
            impl_->planned_publication = impl_->config.publish_parent + QLatin1Char('/') + impl_->publication_leaf;
            if (ready_record) {
                ready_record->workspace_id = uuid.toStdString();
                ready_record->publication_leaf = selected_leaf.toStdString();
                if (!impl_->persist(*ready_record) || impl_->cleanup_requested) {
                    impl_->fail(DownloadFailure::Publication);
                    static_cast<void>(cleanup());
                    return false;
                }
            }
            if ((continue_allowed && !continue_allowed()) || impl_->cleanup_requested || impl_->physically_cleaned ||
                impl_->failure != DownloadFailure::None) {
                static_cast<void>(cleanup());
                return false;
            }
            if (CreateDirectoryW(nativePath(path).c_str(), security.attributes())) {
                impl_->job_path = path;
                break;
            }
            if (ready_record || GetLastError() != ERROR_ALREADY_EXISTS) {
                break;
            }
        }
        if (impl_->job_path.isEmpty()) {
            impl_->fail(DownloadFailure::Publication);
            static_cast<void>(cleanup());
            return false;
        }
        // NTFS requires delete sharing on the source parent for child rename.
        // This fresh child remains protected from its own rename by the pinned
        // staging parent. Write sharing stays denied, so reparse mutation fails.
        impl_->job_directory = pinDirectory(impl_->job_path, &impl_->job_identity, DELETE | READ_CONTROL,
                                            FILE_SHARE_READ | FILE_SHARE_DELETE);
        if (!impl_->job_directory.valid()) {
            impl_->fail(DownloadFailure::Publication);
            static_cast<void>(cleanup());
            return false;
        }
        impl_->input.path = impl_->job_path + QStringLiteral("/input.bin");
        impl_->output.path = impl_->job_path + QStringLiteral("/converted.part");
        if (!createFile(impl_->input, security) || !createFile(impl_->output, security)) {
            impl_->fail(DownloadFailure::Publication);
            static_cast<void>(cleanup());
            return false;
        }
        if (ready_record) {
            ready_record->stage = DownloadOwnershipStage::WorkspaceReady;
            ready_record->job_directory = physicalIdentity(impl_->job_directory.get(), true);
            ready_record->input = physicalIdentity(impl_->input.handle.get(), false);
            ready_record->output = physicalIdentity(impl_->output.handle.get(), false);
            if (!impl_->persist(std::move(*ready_record)) || impl_->cleanup_requested) {
                impl_->fail(DownloadFailure::Publication);
                static_cast<void>(cleanup());
                return false;
            }
        }
        if ((continue_allowed && !continue_allowed()) || impl_->cleanup_requested || impl_->physically_cleaned ||
            impl_->failure != DownloadFailure::None) {
            static_cast<void>(cleanup());
            return false;
        }
        impl_->input_sink = std::make_unique<FileSink>(impl_->input, impl_->total_bytes,
                                                       impl_->config.max_workspace_bytes, impl_->failure);
        impl_->output_sink = std::make_unique<FileSink>(impl_->output, impl_->total_bytes,
                                                        impl_->config.max_workspace_bytes, impl_->failure);
        impl_->created = true;
        return true;
    } catch (...) {
        impl_->fail(DownloadFailure::Publication);
        static_cast<void>(cleanup());
        return false;
    }
}

QIODevice* WindowsDownloadWorkspace::inputSink() noexcept {
    return impl_->created && !impl_->cleanup_requested ? impl_->input_sink.get() : nullptr;
}
bool WindowsDownloadWorkspace::sealInput() { return impl_->seal(impl_->input, impl_->input_sink.get()); }
QString WindowsDownloadWorkspace::inputPath() const {
    return impl_->input.sealed && !impl_->cleanup_requested ? impl_->input.path : QString{};
}
std::shared_ptr<const WindowsDownloadReadInput> WindowsDownloadWorkspace::readInput() const {
    return impl_->read(impl_->input);
}
QIODevice* WindowsDownloadWorkspace::outputSink() noexcept {
    return impl_->created && !impl_->cleanup_requested ? impl_->output_sink.get() : nullptr;
}
bool WindowsDownloadWorkspace::sealOutput() { return impl_->seal(impl_->output, impl_->output_sink.get()); }
QString WindowsDownloadWorkspace::outputPath() const {
    return impl_->output.sealed && !impl_->cleanup_requested && !impl_->output.published ? impl_->output.path
                                                                                         : QString{};
}
std::shared_ptr<const WindowsDownloadReadInput> WindowsDownloadWorkspace::readOutput() const {
    return impl_->read(impl_->output);
}
QString WindowsDownloadWorkspace::plannedPublicationPath() const { return impl_->planned_publication; }

QString WindowsDownloadWorkspace::validateAndPublish(const std::function<bool(const QString&)>& validate_mp3,
                                                     const std::function<bool()>& commit_allowed) {
    if (impl_->operating) {
        return {};
    }
    const QScopedValueRollback operation(impl_->operating, true);
    if (!impl_->created || impl_->cleanup_requested || impl_->failure != DownloadFailure::None ||
        !impl_->input.sealed || !impl_->output.sealed || impl_->output.published || !validate_mp3 ||
        !inspectFile(impl_->input) || !inspectFile(impl_->output) || !impl_->publicationDirectoryValid()) {
        impl_->fail(DownloadFailure::InvalidResult);
        return {};
    }
    try {
        if (!validate_mp3(impl_->output.path)) {
            impl_->fail(DownloadFailure::UnsupportedMedia);
            return {};
        }
        if (impl_->cleanup_requested || !inspectFile(impl_->input) || !inspectFile(impl_->output) ||
            !impl_->publicationDirectoryValid() || !acquireDeleteHandle(impl_->output)) {
            impl_->fail(DownloadFailure::InvalidResult);
            return {};
        }
        if (!FlushFileBuffers(impl_->output.handle.get())) {
            impl_->fail(DownloadFailure::Publication);
            return {};
        }
        std::optional<DownloadOwnershipRecord> published_record;
        if (impl_->ownership) {
            if (!impl_->last_confirmed) {
                impl_->fail(DownloadFailure::Publication);
                return {};
            }
            auto prepared = *impl_->last_confirmed;
            prepared.stage = DownloadOwnershipStage::PublicationPrepared;
            prepared.input_bytes = impl_->input.bytes;
            prepared.output_bytes = impl_->output.bytes;
            prepared.output_sha256 = sealedHash(impl_->output);
            if (prepared.input != physicalIdentity(impl_->input.handle.get(), false) ||
                prepared.output != physicalIdentity(impl_->output.handle.get(), false)) {
                impl_->fail(DownloadFailure::Publication);
                return {};
            }
            if (!impl_->persist(std::move(prepared)))
                return {};
            // Reserve/copy the next DTO before the physical commit point.
            published_record = *impl_->last_confirmed;
            published_record->stage = DownloadOwnershipStage::Published;
        }
        if (impl_->cleanup_requested || (commit_allowed && !commit_allowed()))
            return {};
        if (!inspectFile(impl_->input) || !inspectFile(impl_->output) || !impl_->publicationDirectoryValid()) {
            impl_->fail(DownloadFailure::Publication);
            return {};
        }
        // The Win32 wrapper rejects relative RootDirectory here. Ancestors and
        // directory identity stay pinned; prove the actual normalized path
        // from the renamed file handle before acknowledging its public path.
        const auto name = nativePath(impl_->planned_publication);
        const auto name_bytes = name.size() * sizeof(wchar_t);
        std::vector<std::byte> storage(sizeof(FILE_RENAME_INFO) + name_bytes + sizeof(wchar_t));
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        rename->ReplaceIfExists = FALSE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(name_bytes);
        std::memcpy(rename->FileName, name.data(), name_bytes);
        if (!SetFileInformationByHandle(impl_->output.handle.get(), FileRenameInfo, rename,
                                        static_cast<DWORD>(storage.size()))) {
            impl_->fail(DownloadFailure::Publication);
            return {};
        }
        if (impl_->ownership) {
            // From this syscall onward the artifact is preserved, even if its
            // destination proof or journal acknowledgement is uncertain.
            impl_->output.published = true;
            impl_->output.path = impl_->planned_publication;
        }
        const auto expected_path = impl_->publication_canonical + QLatin1Char('\\') + impl_->publication_leaf;
        if (!impl_->publicationDirectoryValid() || !inspectFile(impl_->output) ||
            finalPath(impl_->output.handle.get()) != expected_path ||
            (published_record && published_record->output != physicalIdentity(impl_->output.handle.get(), false))) {
            // The handle owns the renamed file even when directory metadata
            // changes during the syscall. Do not acknowledge an uncertain final.
            if (!impl_->ownership)
                static_cast<void>(impl_->erase(impl_->output));
            else
                impl_->output.handle.close();
            impl_->fail(DownloadFailure::Publication);
            return {};
        }
        impl_->output.path = impl_->planned_publication;
        impl_->output.published = true;
        impl_->retained_publication = impl_->planned_publication;
        if (published_record && !impl_->persist(std::move(*published_record))) {
            impl_->output.handle.close();
            return {};
        }
        if (impl_->cleanup_requested) {
            impl_->output.handle.close();
            impl_->fail(DownloadFailure::Publication);
            return {};
        }
        // Release DELETE access before the local importer reads by path.
        impl_->output.handle.close();
        return impl_->planned_publication;
    } catch (...) {
        impl_->fail(impl_->ownership ? DownloadFailure::Publication : DownloadFailure::InvalidResult);
        return {};
    }
}

QString WindowsDownloadWorkspace::retainedPublicationPath() const { return impl_->retained_publication; }

bool WindowsDownloadWorkspace::cleanup() noexcept {
    impl_->cleanup_requested = true;
    impl_->created = false;
    if (impl_->input_sink) {
        impl_->input_sink->close();
    }
    if (impl_->output_sink) {
        impl_->output_sink->close();
    }
    if (impl_->physically_cleaned)
        return impl_->confirmCleanup();
    try {
        const bool input_erased = impl_->erase(impl_->input);
        const bool output_erased = impl_->erase(impl_->output);
        bool directory_erased = impl_->job_path.isEmpty();
        if (input_erased && output_erased && impl_->job_directory.valid()) {
            BY_HANDLE_FILE_INFORMATION information{};
            FILE_DISPOSITION_INFO disposition{TRUE};
            directory_erased = GetFileInformationByHandle(impl_->job_directory.get(), &information) &&
                               (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                               !(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                               sameIdentity(information, impl_->job_identity) &&
                               SetFileInformationByHandle(impl_->job_directory.get(), FileDispositionInfo, &disposition,
                                                          sizeof(disposition));
            if (directory_erased) {
                impl_->job_directory.close();
                impl_->job_path.clear();
            }
        }
        if (input_erased && output_erased && directory_erased) {
            impl_->staging_pins.clear();
            impl_->publication_pins.clear();
            impl_->physically_cleaned = true;
            return impl_->confirmCleanup();
        }
    } catch (...) {
    }
    impl_->fail(DownloadFailure::Publication);
    return false;
}

DownloadFailure WindowsDownloadWorkspace::error() const noexcept { return impl_->failure; }

} // namespace music_player::m0

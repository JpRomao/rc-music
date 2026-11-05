#include "adapters/WindowsPrivateDownloadDocument.hpp"
#include "adapters/DownloadMediaProfile.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QDir>
#include <QScopeGuard>
#include <QUuid>

#include <AclAPI.h>
#include <Sddl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {

struct Policy final {
    const char* journal_leaf;
    const char* lock_leaf;
    const char* temporary_prefix;
    qsizetype maximum_file_size;
};

std::optional<Policy> documentPolicy(const WindowsPrivateDownloadDocumentKind kind) {
    switch (kind) {
    case WindowsPrivateDownloadDocumentKind::Checkpoint:
        return Policy{"download-checkpoint.json", "download-checkpoint.lock", "download-checkpoint-", 2 * 1024 * 1024};
    case WindowsPrivateDownloadDocumentKind::Ownership:
        return Policy{"download-ownership.json", "download-ownership.lock", "download-ownership-", 256 * 1024};
    case WindowsPrivateDownloadDocumentKind::MediaProfile:
        return Policy{"download-media-profile.json", "download-media-profile.lock", "download-media-profile-",
                      8 * 1024};
    }
    return std::nullopt;
}

using download_artifacts::finalPath;
using download_artifacts::Handle;
using download_artifacts::nativePath;
using download_artifacts::sameIdentity;

class PrivateSecurity final {
  public:
    PrivateSecurity() {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return;
        const Handle token(raw);
        DWORD bytes = 0;
        static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes));
        if (bytes == 0 || bytes > 64 * 1024)
            return;
        user_.resize(bytes);
        if (!GetTokenInformation(token.get(), TokenUser, user_.data(), bytes, &bytes) || !IsValidSid(userSid()))
            return;
        DWORD system_bytes = static_cast<DWORD>(system_.size());
        if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system_.data(), &system_bytes))
            return;
        LPWSTR user_text = nullptr;
        if (!ConvertSidToStringSidW(userSid(), &user_text))
            return;
        const auto free_text = qScopeGuard([&] { LocalFree(user_text); });
        const auto sddl =
            QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(user_text) + QLatin1Char(')');
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1,
                                                                  &descriptor_, nullptr))
            return;
        attributes_ = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
    }
    ~PrivateSecurity() { LocalFree(descriptor_); }
    PrivateSecurity(const PrivateSecurity&) = delete;
    PrivateSecurity& operator=(const PrivateSecurity&) = delete;
    bool valid() const noexcept { return descriptor_ != nullptr; }
    SECURITY_ATTRIBUTES* attributes() noexcept { return valid() ? &attributes_ : nullptr; }

    bool privateObject(HANDLE handle) const {
        if (!valid())
            return false;
        PSID owner = nullptr;
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;
        if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                            nullptr, &dacl, nullptr, &security) != ERROR_SUCCESS)
            return false;
        const auto release = qScopeGuard([&] { LocalFree(security); });
        SECURITY_DESCRIPTOR_CONTROL control{};
        DWORD revision = 0;
        if (!owner || !EqualSid(owner, userSid()) || !dacl || dacl->AceCount != 2 ||
            !GetSecurityDescriptorControl(security, &control, &revision) || !(control & SE_DACL_PROTECTED))
            return false;
        bool current_user = false, system = false;
        for (DWORD index = 0; index < dacl->AceCount; ++index) {
            void* raw = nullptr;
            if (!GetAce(dacl, index, &raw))
                return false;
            const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
            // This adapter consumes a dedicated protected directory, not an
            // arbitrary user profile directory with a broader inherited ACL.
            if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0 ||
                ace->Mask != FILE_ALL_ACCESS)
                return false;
            auto* sid = const_cast<DWORD*>(&ace->SidStart);
            if (!IsValidSid(sid))
                return false;
            const bool is_user = EqualSid(sid, userSid()) != FALSE;
            const bool is_system = EqualSid(sid, const_cast<unsigned char*>(system_.data())) != FALSE;
            if (!is_user && !is_system)
                return false;
            current_user |= is_user;
            system |= is_system;
        }
        return current_user && system;
    }

  private:
    PSID userSid() const noexcept {
        return user_.empty() ? nullptr : reinterpret_cast<const TOKEN_USER*>(user_.data())->User.Sid;
    }
    std::vector<unsigned char> user_{};
    std::array<unsigned char, SECURITY_MAX_SID_SIZE> system_{};
    PSECURITY_DESCRIPTOR descriptor_{nullptr};
    SECURITY_ATTRIBUTES attributes_{};
};

struct PrivateDirectory final {
    QString path{};
    QString canonical{};
    std::vector<Handle> pins{};
    FILE_ID_INFO identity{};

    bool unchanged(const PrivateSecurity& security) const {
        FILE_ID_INFO current{};
        return !pins.empty() && download_artifacts::information(pins.back().get(), true, current) &&
               sameIdentity(current, identity) && finalPath(pins.back().get()) == canonical &&
               security.privateObject(pins.back().get());
    }
    QString child(const char* leaf) const { return path + QLatin1Char('/') + QString::fromLatin1(leaf); }
    QString canonicalChild(const QString& leaf) const { return canonical + QLatin1Char('\\') + leaf; }
};

bool pinDirectory(const QString& configured, const PrivateSecurity& security, PrivateDirectory& directory) {
    if (!security.valid() || !download_artifacts::validPath(configured, directory.path) ||
        directory.path.size() > 29'900 || !download_artifacts::pinAncestors(directory.path, directory.pins) ||
        directory.pins.empty())
        return false;
    // Pin ancestors separately: NTFS requires DELETE sharing on the source
    // parent for a child rename; the same directory is also its destination,
    // whose Win32 wrapper needs WRITE sharing. The ancestor prevents this
    // directory's own rename, and we revalidate identity/reparse/DACL before
    // and after commit. This does not stop a malicious same-user reparse race.
    Handle security_pin(CreateFileW(nativePath(directory.path).c_str(),
                                    FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | READ_CONTROL | DELETE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!security_pin.valid() || !download_artifacts::information(security_pin.get(), true, directory.identity))
        return false;
    directory.pins.push_back(std::move(security_pin));
    directory.canonical = finalPath(directory.pins.back().get());
    return !directory.canonical.isEmpty() && directory.unchanged(security);
}

bool storageParentSyntax(const QString& original) {
    // Run this before validPath's drive query or any handle open. The existing
    // document parser remains unchanged; storage creation additionally refuses
    // DOS device components, including extension and superscript aliases.
    if (original.size() < 4 || original.size() > 30'000 || original.at(1) != QLatin1Char(':') ||
        (original.at(2) != QLatin1Char('/') && original.at(2) != QLatin1Char('\\')) ||
        !((original.at(0) >= QLatin1Char('A') && original.at(0) <= QLatin1Char('Z')) ||
          (original.at(0) >= QLatin1Char('a') && original.at(0) <= QLatin1Char('z'))))
        return false;
    const auto normalized = QDir::fromNativeSeparators(original);
    for (const auto& component : normalized.mid(3).split(QLatin1Char('/'))) {
        auto stem = component.section(QLatin1Char('.'), 0, 0);
        while (stem.endsWith(QLatin1Char(' ')))
            stem.chop(1);
        for (const auto* reserved : {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}) {
            if (stem.compare(QLatin1StringView{reserved}, Qt::CaseInsensitive) == 0)
                return false;
        }
        if (stem.size() == 4 && (stem.first(3).compare(QLatin1StringView{"COM"}, Qt::CaseInsensitive) == 0 ||
                                 stem.first(3).compare(QLatin1StringView{"LPT"}, Qt::CaseInsensitive) == 0)) {
            const auto suffix = stem.at(3).unicode();
            if ((suffix >= '1' && suffix <= '9') || suffix == 0x00b9 || suffix == 0x00b2 || suffix == 0x00b3)
                return false;
        }
    }
    return true;
}

bool prepareStorageDirectory(const QString& path, PrivateSecurity& security, PrivateDirectory& directory) {
    directory.path = path;
    if (!security.valid() || !download_artifacts::pinAncestors(path, directory.pins))
        return false;
    if (!CreateDirectoryW(nativePath(path).c_str(), security.attributes())) {
        const auto error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            return false;
    }
    // Unlike a store's rename-capable leaf pin, these pins have no DELETE
    // access and deny write/delete sharing. Keeping root pinned is compatible
    // with pinAncestors opening it again while preparing the fixed children.
    Handle pin{CreateFileW(nativePath(path).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | READ_CONTROL,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!pin.valid() || !download_artifacts::information(pin.get(), true, directory.identity))
        return false;
    directory.pins.push_back(std::move(pin));
    directory.canonical = finalPath(directory.pins.back().get());
    return !directory.canonical.isEmpty() && directory.unchanged(security);
}

bool regularPrivateFile(HANDLE handle, const PrivateSecurity& security, FILE_ID_INFO& identity, quint64& size) {
    BY_HANDLE_FILE_INFORMATION basic{};
    LARGE_INTEGER bytes{};
    if (!download_artifacts::information(handle, false, identity) || !GetFileInformationByHandle(handle, &basic) ||
        basic.nNumberOfLinks != 1 || !GetFileSizeEx(handle, &bytes) || bytes.QuadPart < 0 ||
        !security.privateObject(handle))
        return false;
    size = static_cast<quint64>(bytes.QuadPart);
    return true;
}

struct Journal final {
    Handle pin{};
    FILE_ID_INFO identity{};
    quint64 bytes{0};
    WindowsPrivateDownloadDocumentResult result{.status = DownloadCheckpointLoadStatus::Unreadable};
};

Journal inspect(const PrivateDirectory& directory, const PrivateSecurity& security, const Policy& policy,
                const WindowsPrivateDownloadDocument::Validator& validator) {
    Journal journal;
    journal.pin = Handle(CreateFileW(nativePath(directory.child(policy.journal_leaf)).c_str(),
                                     GENERIC_READ | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!journal.pin.valid()) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            journal.result.status = DownloadCheckpointLoadStatus::Missing;
        return journal;
    }
    if (!regularPrivateFile(journal.pin.get(), security, journal.identity, journal.bytes) ||
        finalPath(journal.pin.get()) != directory.canonicalChild(QString::fromLatin1(policy.journal_leaf)))
        return journal;
    if (journal.bytes > static_cast<quint64>(policy.maximum_file_size)) {
        journal.result.status = DownloadCheckpointLoadStatus::Invalid;
        return journal;
    }
    QByteArray encoded(static_cast<qsizetype>(journal.bytes), Qt::Uninitialized);
    quint64 consumed = 0;
    while (consumed < journal.bytes) {
        DWORD count = 0;
        const auto wanted = static_cast<DWORD>(std::min<quint64>(64 * 1024, journal.bytes - consumed));
        if (!ReadFile(journal.pin.get(), encoded.data() + static_cast<qsizetype>(consumed), wanted, &count, nullptr) ||
            count == 0 || count > wanted)
            return journal;
        consumed += count;
    }
    FILE_ID_INFO after{};
    quint64 size = 0;
    if (!regularPrivateFile(journal.pin.get(), security, after, size) || !sameIdentity(after, journal.identity) ||
        size != journal.bytes || !directory.unchanged(security))
        return journal;
    const auto status = validator(encoded);
    if (status != DownloadCheckpointLoadStatus::Loaded && status != DownloadCheckpointLoadStatus::Invalid &&
        status != DownloadCheckpointLoadStatus::FutureVersion)
        return journal;
    journal.result.status = status;
    if (status == DownloadCheckpointLoadStatus::Loaded)
        journal.result.bytes = std::move(encoded);
    return journal;
}

bool writableStatus(const DownloadCheckpointLoadStatus status) {
    return status == DownloadCheckpointLoadStatus::Missing || status == DownloadCheckpointLoadStatus::Loaded;
}

Handle writerLock(const PrivateDirectory& directory, PrivateSecurity& security, const Policy& policy) {
    const auto path = nativePath(directory.child(policy.lock_leaf));
    Handle handle(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, security.attributes(),
                              CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle.valid()) {
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return Handle{};
        handle = Handle(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    }
    FILE_ID_INFO identity{};
    quint64 bytes = 0;
    if (!handle.valid() || !regularPrivateFile(handle.get(), security, identity, bytes) || bytes != 0 ||
        finalPath(handle.get()) != directory.canonicalChild(QString::fromLatin1(policy.lock_leaf)))
        return Handle{};
    // The zero-byte file remains. Kernel handle sharing is the exclusive lock;
    // crash releases it, without trusting a PID or unlinking a stale lock path.
    return handle;
}

class MediaProfileOperationLease final : public DownloadMediaProfileLease {
  public:
    MediaProfileOperationLease(PrivateDirectory directory, Handle lock)
        : directory_(std::move(directory)), lock_(std::move(lock)) {}

  private:
    // The exclusive handle closes before the pins. Releasing the lease never
    // unlinks a name or grants authority to remove an AppContainer profile.
    PrivateDirectory directory_;
    Handle lock_;
};

class FreshTemporary final {
  public:
    Handle pin{};
    FILE_ID_INFO identity{};
    QString leaf{};
    bool committed{false};
    ~FreshTemporary() {
        if (committed || !pin.valid())
            return;
        BY_HANDLE_FILE_INFORMATION basic{};
        FILE_ID_INFO current{};
        if (GetFileInformationByHandle(pin.get(), &basic) && basic.nNumberOfLinks == 1 &&
            !(basic.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            GetFileInformationByHandleEx(pin.get(), FileIdInfo, &current, sizeof(current)) &&
            sameIdentity(current, identity)) {
            FILE_DISPOSITION_INFO remove{TRUE};
            // Only the exact freshly created file handle is eligible. Never
            // unlink a name parsed from JSON, an old journal or an unknown file.
            static_cast<void>(SetFileInformationByHandle(pin.get(), FileDispositionInfo, &remove, sizeof(remove)));
        }
    }
};

bool createTemporary(const PrivateDirectory& directory, PrivateSecurity& security, const Policy& policy,
                     FreshTemporary& temporary) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        temporary.leaf = QString::fromLatin1(policy.temporary_prefix) +
                         QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".tmp");
        const auto path = directory.path + QLatin1Char('/') + temporary.leaf;
        temporary.pin = Handle(CreateFileW(
            nativePath(path).c_str(), GENERIC_READ | GENERIC_WRITE | DELETE | READ_CONTROL, FILE_SHARE_READ,
            security.attributes(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (temporary.pin.valid()) {
            quint64 bytes = 0;
            return regularPrivateFile(temporary.pin.get(), security, temporary.identity, bytes) && bytes == 0 &&
                   finalPath(temporary.pin.get()) == directory.canonicalChild(temporary.leaf);
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return false;
}

bool writeAll(HANDLE file, const QByteArray& bytes) {
    qsizetype consumed = 0;
    while (consumed < bytes.size()) {
        DWORD count = 0;
        const auto wanted = static_cast<DWORD>(std::min<qsizetype>(64 * 1024, bytes.size() - consumed));
        if (!WriteFile(file, bytes.constData() + consumed, wanted, &count, nullptr) || count != wanted)
            return false;
        consumed += static_cast<qsizetype>(count);
    }
    return FlushFileBuffers(file) != FALSE;
}

bool injected(const WindowsPrivateDownloadDocumentOptions& options, const WindowsPrivateDownloadFaultPoint point) {
    if (!options.inject_fault)
        return false;
    try {
        return options.inject_fault(point);
    } catch (...) {
        return true;
    }
}

bool renameAtomically(HANDLE file, const PrivateDirectory& directory, const Policy& policy, bool replace_existing) {
    // The Win32 wrapper used by the workspace also requires an absolute name.
    // Ancestry and the private parent stay pinned for this entire transaction.
    const auto name = nativePath(directory.child(policy.journal_leaf));
    const auto name_bytes = name.size() * sizeof(wchar_t);
    std::vector<std::byte> storage(sizeof(FILE_RENAME_INFO) + name_bytes + sizeof(wchar_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = static_cast<BOOLEAN>(replace_existing);
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(name_bytes);
    std::memcpy(rename->FileName, name.data(), name_bytes);
    return SetFileInformationByHandle(file, FileRenameInfo, rename, static_cast<DWORD>(storage.size())) != FALSE;
}

} // namespace

WindowsDownloadStoragePaths windowsDownloadStoragePaths(const QString& config_root) {
    const auto root = config_root + QStringLiteral("/downloads");
    return {root,
            root + QStringLiteral("/state"),
            root + QStringLiteral("/network"),
            root + QStringLiteral("/media"),
            root + QStringLiteral("/youtube"),
            root + QStringLiteral("/staging"),
            root + QStringLiteral("/published")};
}
bool prepareWindowsDownloadStorage(const QString& config_root) {
    try {
        QString normalized;
        if (!storageParentSyntax(config_root) || !download_artifacts::validPath(config_root, normalized))
            return false;
        const auto layout = windowsDownloadStoragePaths(normalized);
        const std::array paths{layout.root,    layout.state,   layout.network,  layout.media,
                               layout.youtube, layout.staging, layout.published};
        for (const auto& path : paths) {
            if (path.size() > 29'900)
                return false;
        }
        // The configured parent is pre-existing and trusted, but need not have
        // the dedicated store DACL. Do not create it or change its permissions.
        std::vector<Handle> parent_pins;
        if (!download_artifacts::pinAncestors(layout.root, parent_pins) || parent_pins.empty())
            return false;
        FILE_ID_INFO parent_identity{};
        if (!download_artifacts::information(parent_pins.back().get(), true, parent_identity))
            return false;
        const auto parent_final = finalPath(parent_pins.back().get());
        if (parent_final.isEmpty())
            return false;

        PrivateSecurity security;
        std::array<PrivateDirectory, 7> directories;
        for (std::size_t index = 0; index < paths.size(); ++index) {
            if (!prepareStorageDirectory(paths[index], security, directories[index]))
                return false;
        }
        FILE_ID_INFO after{};
        if (!download_artifacts::information(parent_pins.back().get(), true, after) ||
            !sameIdentity(parent_identity, after) || finalPath(parent_pins.back().get()) != parent_final)
            return false;
        return std::all_of(directories.begin(), directories.end(),
                           [&security](const auto& directory) { return directory.unchanged(security); });
    } catch (...) {
        // No rollback or cleanup: existing and partially created data remain.
        return false;
    }
}

WindowsPrivateDownloadDocument::WindowsPrivateDownloadDocument(QString directory,
                                                               WindowsPrivateDownloadDocumentKind kind,
                                                               Validator validator,
                                                               WindowsPrivateDownloadDocumentOptions options)
    : directory_(std::move(directory)), kind_(kind), validator_(std::move(validator)), options_(std::move(options)) {}

std::unique_ptr<DownloadMediaProfileLease>
WindowsPrivateDownloadDocument::acquireMediaProfileLease(QString configured) {
    try {
        PrivateSecurity security;
        PrivateDirectory directory;
        if (!pinDirectory(configured, security, directory))
            return {};
        auto policy = documentPolicy(WindowsPrivateDownloadDocumentKind::MediaProfile);
        if (!policy)
            return {};
        policy->lock_leaf = "download-media-profile-owner.lock";
        auto lock = writerLock(directory, security, *policy);
        if (!lock.valid() || !directory.unchanged(security))
            return {};
        return std::make_unique<MediaProfileOperationLease>(std::move(directory), std::move(lock));
    } catch (...) {
        return {};
    }
}

WindowsPrivateDownloadDocumentResult WindowsPrivateDownloadDocument::load() {
    try {
        const auto policy = documentPolicy(kind_);
        if (!policy || !validator_)
            return {};
        PrivateSecurity security;
        PrivateDirectory directory;
        if (!pinDirectory(directory_, security, directory))
            return {};
        auto journal = inspect(directory, security, *policy, validator_);
        return std::move(journal.result);
    } catch (...) {
        return {};
    }
}

DownloadCheckpointSaveStatus WindowsPrivateDownloadDocument::update(const Mutation& mutation) {
    try {
        const auto policy = documentPolicy(kind_);
        if (!policy || !validator_ || !mutation)
            return DownloadCheckpointSaveStatus::Failed;
        PrivateSecurity security;
        PrivateDirectory directory;
        if (!pinDirectory(directory_, security, directory))
            return DownloadCheckpointSaveStatus::Failed;

        // Preserve unknown documents before creating even a writer lock.
        if (!writableStatus(inspect(directory, security, *policy, validator_).result.status))
            return DownloadCheckpointSaveStatus::BlockedByExistingData;
        const auto lock = writerLock(directory, security, *policy);
        if (!lock.valid())
            return DownloadCheckpointSaveStatus::Failed;
        auto previous = inspect(directory, security, *policy, validator_);
        if (!writableStatus(previous.result.status))
            return DownloadCheckpointSaveStatus::BlockedByExistingData;

        // Both the read/modify decision and commit share this exclusive lock.
        const auto candidate = mutation(previous.result);
        if (candidate.status != DownloadCheckpointSaveStatus::Saved)
            return candidate.status == DownloadCheckpointSaveStatus::BlockedByExistingData
                       ? DownloadCheckpointSaveStatus::BlockedByExistingData
                       : DownloadCheckpointSaveStatus::Failed;
        const auto& encoded = candidate.bytes;
        if (encoded.isEmpty() || encoded.size() > policy->maximum_file_size ||
            validator_(encoded) != DownloadCheckpointLoadStatus::Loaded)
            return DownloadCheckpointSaveStatus::Failed;

        FreshTemporary temporary;
        if (!createTemporary(directory, security, *policy, temporary) || !writeAll(temporary.pin.get(), encoded) ||
            injected(options_, WindowsPrivateDownloadFaultPoint::BeforeCommit) || !directory.unchanged(security))
            return DownloadCheckpointSaveStatus::Failed;

        auto current = inspect(directory, security, *policy, validator_);
        if (!writableStatus(current.result.status))
            return DownloadCheckpointSaveStatus::BlockedByExistingData;
        if (current.result.status != previous.result.status ||
            (previous.result.status == DownloadCheckpointLoadStatus::Loaded &&
             !sameIdentity(previous.identity, current.identity)))
            return DownloadCheckpointSaveStatus::BlockedByExistingData;
        FILE_ID_INFO written_identity{};
        quint64 written_bytes = 0;
        if (!regularPrivateFile(temporary.pin.get(), security, written_identity, written_bytes) ||
            !sameIdentity(written_identity, temporary.identity) ||
            written_bytes != static_cast<quint64>(encoded.size()) ||
            finalPath(temporary.pin.get()) != directory.canonicalChild(temporary.leaf))
            return DownloadCheckpointSaveStatus::Failed;

        // Windows normal replacement refuses our own open destination pins.
        // Release only those two immediately before commit under lock+dir pins.
        // This is not CAS against noncooperative or malicious same-user writers.
        previous.pin = Handle{};
        current.pin = Handle{};
        if (!renameAtomically(temporary.pin.get(), directory, *policy,
                              previous.result.status == DownloadCheckpointLoadStatus::Loaded))
            return DownloadCheckpointSaveStatus::Failed;

        // No cleanup may erase the committed replacement, even after a failure.
        temporary.committed = true;
        FILE_ID_INFO published_identity{};
        quint64 published_bytes = 0;
        if (!directory.unchanged(security) ||
            !regularPrivateFile(temporary.pin.get(), security, published_identity, published_bytes) ||
            !sameIdentity(published_identity, temporary.identity) ||
            published_bytes != static_cast<quint64>(encoded.size()) ||
            finalPath(temporary.pin.get()) != directory.canonicalChild(QString::fromLatin1(policy->journal_leaf)) ||
            injected(options_, WindowsPrivateDownloadFaultPoint::AfterCommit))
            return DownloadCheckpointSaveStatus::Failed;
        return DownloadCheckpointSaveStatus::Saved;
    } catch (...) {
        return DownloadCheckpointSaveStatus::Failed;
    }
}

} // namespace music_player::m0

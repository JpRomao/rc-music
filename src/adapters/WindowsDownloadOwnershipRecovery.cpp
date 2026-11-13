#include "adapters/WindowsDownloadOwnershipRecovery.hpp"

#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsDownloadReadInput.hpp"
#include "m0/ports/DownloadExecution.hpp"

#include <QCryptographicHash>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#include <AclAPI.h>

namespace music_player::m0 {
namespace {
using download_artifacts::Handle;
using Assessment = DownloadOwnershipAssessment;
using DiscardOutcome = DownloadOwnershipDiscardOutcome;
constexpr std::uint64_t maximum_workspace_bytes = 512ULL * 1024 * 1024;

enum class OpenState { Present, Missing, Conflict, Unreadable };

struct Object final {
    OpenState state{OpenState::Missing};
    Handle handle{};
    FILE_ID_INFO identity{};
    QString canonical{};
    std::uint64_t bytes{0};
};

bool matches(const FILE_ID_INFO& actual, const DownloadFileIdentity& expected) noexcept {
    return actual.VolumeSerialNumber == expected.volume_serial &&
           std::memcmp(actual.FileId.Identifier, expected.file_id.data(), expected.file_id.size()) == 0;
}

Object observe(const QString& path, const bool directory, const bool delete_access = false) {
    const DWORD access =
        (directory ? FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | READ_CONTROL : GENERIC_READ | READ_CONTROL) |
        (delete_access ? DELETE : 0);
    // NTFS directory enumeration/child deletion needs the owned job pin to
    // share DELETE. File pins remain share-READ-only, excluding writers and
    // replacement/deletion while the entire workspace is verified.
    const DWORD sharing = FILE_SHARE_READ | (directory && delete_access ? FILE_SHARE_DELETE : 0);
    const DWORD flags =
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN);
    Object object;
    object.handle = Handle(CreateFileW(download_artifacts::nativePath(path).c_str(), access, sharing, nullptr,
                                       OPEN_EXISTING, flags, nullptr));
    if (!object.handle.valid()) {
        const auto error = GetLastError();
        object.state =
            error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? OpenState::Missing : OpenState::Unreadable;
        return object;
    }
    BY_HANDLE_FILE_INFORMATION basic{};
    if (GetFileType(object.handle.get()) != FILE_TYPE_DISK) {
        object.state = OpenState::Conflict;
        return object;
    }
    if (!GetFileInformationByHandle(object.handle.get(), &basic)) {
        object.state = OpenState::Unreadable;
        return object;
    }
    if ((basic.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (((basic.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) ||
        (!directory && basic.nNumberOfLinks != 1)) {
        object.state = OpenState::Conflict;
        return object;
    }
    if (!download_artifacts::information(object.handle.get(), directory, object.identity)) {
        object.state = OpenState::Unreadable;
        return object;
    }
    if (!directory) {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(object.handle.get(), &size) || size.QuadPart < 0) {
            object.state = OpenState::Unreadable;
            return object;
        }
        object.bytes = static_cast<std::uint64_t>(size.QuadPart);
    }
    object.canonical = download_artifacts::finalPath(object.handle.get());
    object.state = object.canonical.isEmpty() ? OpenState::Unreadable : OpenState::Present;
    return object;
}

Assessment failure(const OpenState state) noexcept {
    return state == OpenState::Conflict ? Assessment::Conflict : Assessment::Unreadable;
}

struct Parent final {
    std::vector<Handle> ancestors{};
    Object leaf{};
};

// Pin each configured ancestor without following reparse points. The leaf is
// retained separately for identity/canonical checks, not reopened by JSON data.
OpenState pinParent(const QString& path, Parent& parent) {
    QString prefix = path.left(3);
    const auto components = path.mid(3).split(QLatin1Char('/'));
    for (qsizetype index = 0; index < components.size(); ++index) {
        if (index != 0) {
            if (!prefix.endsWith(QLatin1Char('/')))
                prefix += QLatin1Char('/');
            prefix += components[index - 1];
        }
        auto ancestor = observe(prefix, true);
        if (ancestor.state != OpenState::Present)
            return ancestor.state;
        parent.ancestors.push_back(std::move(ancestor.handle));
    }
    parent.leaf = observe(path, true);
    return parent.leaf.state;
}

class PrivateSecurity final {
  public:
    bool initialize() {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return false;
        Handle token(raw);
        DWORD required = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
        if (!required)
            return false;
        user_.resize(required);
        if (!GetTokenInformation(token.get(), TokenUser, user_.data(), required, &required))
            return false;
        user_sid_ = reinterpret_cast<const TOKEN_USER*>(user_.data())->User.Sid;
        DWORD system_bytes = static_cast<DWORD>(system_.size());
        return IsValidSid(user_sid_) && CreateWellKnownSid(WinLocalSystemSid, nullptr, system_.data(), &system_bytes);
    }

    OpenState check(HANDLE handle) const {
        PSID owner = nullptr;
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR raw = nullptr;
        if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                            nullptr, &dacl, nullptr, &raw) != ERROR_SUCCESS) {
            LocalFree(raw);
            return OpenState::Unreadable;
        }
        struct FreeDescriptor final {
            PSECURITY_DESCRIPTOR value;
            ~FreeDescriptor() { LocalFree(value); }
        } descriptor{raw};
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (!raw || !owner || !IsValidSid(owner) || !EqualSid(owner, user_sid_) || !dacl || dacl->AceCount != 2 ||
            !GetSecurityDescriptorControl(raw, &control, &revision) || !(control & SE_DACL_PROTECTED))
            return OpenState::Conflict;
        bool found_user = false;
        bool found_system = false;
        for (DWORD index = 0; index != 2; ++index) {
            void* raw_ace = nullptr;
            if (!GetAce(dacl, index, &raw_ace))
                return OpenState::Conflict;
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
            constexpr auto flags = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
            auto* sid = const_cast<DWORD*>(&ace->SidStart);
            if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != flags ||
                ace->Mask != FILE_ALL_ACCESS || !IsValidSid(sid) ||
                ace->Header.AceSize != sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(sid))
                return OpenState::Conflict;
            if (EqualSid(sid, user_sid_) && !found_user)
                found_user = true;
            else if (EqualSid(sid, const_cast<std::byte*>(system_.data())) && !found_system)
                found_system = true;
            else
                return OpenState::Conflict;
        }
        return found_user && found_system ? OpenState::Present : OpenState::Conflict;
    }

  private:
    std::vector<std::byte> user_{};
    std::array<std::byte, SECURITY_MAX_SID_SIZE> system_{};
    PSID user_sid_{nullptr};
};

OpenState knownChildrenOnly(const QString& job_path, const bool require_empty = false) {
    WIN32_FIND_DATAW entry{};
    const auto pattern = download_artifacts::nativePath(job_path + QStringLiteral("/*"));
    const auto raw = FindFirstFileW(pattern.c_str(), &entry);
    if (raw == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? OpenState::Present : OpenState::Unreadable;
    struct CloseSearch final {
        HANDLE value;
        ~CloseSearch() { FindClose(value); }
    } search{raw};
    for (;;) {
        const auto name = QString::fromWCharArray(entry.cFileName);
        if (name != QStringLiteral(".") && name != QStringLiteral("..") &&
            (require_empty || (name != QStringLiteral("input.bin") && name != QStringLiteral("converted.part"))))
            return OpenState::Conflict;
        if (!FindNextFileW(raw, &entry))
            return GetLastError() == ERROR_NO_MORE_FILES ? OpenState::Present : OpenState::Unreadable;
    }
}

OpenState verifyObject(const Object& object, const DownloadFileIdentity& identity, const QString& expected_path,
                       const PrivateSecurity& security) {
    if (object.state != OpenState::Present)
        return object.state;
    if (!matches(object.identity, identity) || object.canonical != expected_path)
        return OpenState::Conflict;
    return security.check(object.handle.get());
}

OpenState unchanged(const Object& object, const bool directory) {
    FILE_ID_INFO identity{};
    BY_HANDLE_FILE_INFORMATION basic{};
    if (!GetFileInformationByHandle(object.handle.get(), &basic) ||
        !download_artifacts::information(object.handle.get(), directory, identity))
        return OpenState::Unreadable;
    if (!download_artifacts::sameIdentity(identity, object.identity) || (!directory && basic.nNumberOfLinks != 1) ||
        download_artifacts::finalPath(object.handle.get()) != object.canonical)
        return OpenState::Conflict;
    if (!directory) {
        LARGE_INTEGER bytes{};
        if (!GetFileSizeEx(object.handle.get(), &bytes) || bytes.QuadPart < 0)
            return OpenState::Unreadable;
        if (static_cast<std::uint64_t>(bytes.QuadPart) != object.bytes)
            return OpenState::Conflict;
    }
    return OpenState::Present;
}

OpenState hashMatches(const Object& object, const DownloadOwnershipRecord& record) {
    if (object.bytes != record.output_bytes)
        return OpenState::Conflict;
    // This is a workspace budget, not the artifact pin helper's 128 MiB cap.
    // Read only the already-pinned regular single-link file with a fixed block.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> bytes{};
    std::uint64_t consumed = 0;
    while (consumed < object.bytes) {
        const auto wanted = static_cast<DWORD>(std::min<std::uint64_t>(bytes.size(), object.bytes - consumed));
        DWORD received = 0;
        if (!ReadFile(object.handle.get(), bytes.data(), wanted, &received, nullptr) || !received || received > wanted)
            return OpenState::Unreadable;
        hash.addData(QByteArrayView(bytes.data(), static_cast<qsizetype>(received)));
        consumed += received;
    }
    const auto state = unchanged(object, false);
    if (state != OpenState::Present)
        return state;
    return hash.result().toHex() == QByteArray::fromStdString(record.output_sha256) ? OpenState::Present
                                                                                    : OpenState::Conflict;
}

struct Workspace final {
    Parent staging{}, publication{};
    PrivateSecurity security{};
    Object job{}, input{}, output{}, final{};
    QString job_path{}, input_path{}, output_path{}, final_path{};
    QString job_canonical{}, final_canonical{};
};

std::optional<Assessment> pinWorkspaceRoots(const WindowsDownloadWorkspaceConfig& config,
                                            const DownloadOwnershipRecord& record, Workspace& workspace) {
    QString staging_path;
    QString publication_path;
    if (!validDownloadOwnershipRecord(record) || !config.max_workspace_bytes ||
        config.max_workspace_bytes > maximum_workspace_bytes)
        return Assessment::Unverified;
    const auto configured_publication = record.publication_directory.empty()
                                            ? config.publish_parent
                                            : QString::fromStdString(record.publication_directory);
    if (!download_artifacts::validPath(config.staging_parent, staging_path) ||
        !download_artifacts::validPath(configured_publication, publication_path))
        return Assessment::Unverified;

    auto& staging = workspace.staging;
    auto& publication = workspace.publication;
    auto state = pinParent(staging_path, staging);
    if (state != OpenState::Present)
        return failure(state);
    state = pinParent(publication_path, publication);
    if (state != OpenState::Present)
        return failure(state);
    if (!matches(staging.leaf.identity, record.staging_parent) ||
        !matches(publication.leaf.identity, record.publish_parent) ||
        staging.leaf.identity.VolumeSerialNumber != publication.leaf.identity.VolumeSerialNumber)
        return Assessment::Conflict;

    const auto uuid =
        QString::fromLatin1(record.workspace_id.data(), static_cast<qsizetype>(record.workspace_id.size()));
    const auto job_leaf = QStringLiteral("rc-music-job-") + uuid;
    const auto final_leaf = QStringLiteral("rc-music-") + uuid + QStringLiteral(".mp3");
    workspace.job_path = staging_path + QLatin1Char('/') + job_leaf;
    workspace.input_path = workspace.job_path + QStringLiteral("/input.bin");
    workspace.output_path = workspace.job_path + QStringLiteral("/converted.part");
    workspace.final_path = publication_path + QLatin1Char('/') + final_leaf;
    workspace.job_canonical = staging.leaf.canonical + QLatin1Char('\\') + job_leaf;
    workspace.final_canonical = publication.leaf.canonical + QLatin1Char('\\') + final_leaf;
    return {};
}

// Shared fresh proof, with all handles kept alive in the caller's workspace.
// A writable proof grants DELETE only to the owned job and temporary files;
// assessment and the publication candidate always remain read-only.
Assessment assessExisting(const WindowsDownloadWorkspaceConfig& config, const DownloadOwnershipRecord& record,
                          Workspace& workspace, const bool delete_access = false) {
    if (record.stage == DownloadOwnershipStage::Planned)
        return Assessment::Unverified;
    if (const auto failed = pinWorkspaceRoots(config, record, workspace))
        return *failed;
    auto& security = workspace.security;
    if (!security.initialize())
        return Assessment::Unreadable;
    const auto& job_path = workspace.job_path;
    const auto& job_canonical = workspace.job_canonical;
    auto& job = workspace.job;
    job = observe(job_path, true, delete_access);
    auto state = verifyObject(job, *record.job_directory, job_canonical, security);
    if (state != OpenState::Present && state != OpenState::Missing)
        return failure(state);
    if (state == OpenState::Present) {
        state = knownChildrenOnly(job_path);
        if (state != OpenState::Present)
            return failure(state);
    }

    auto& input = workspace.input;
    auto& output = workspace.output;
    if (job.state == OpenState::Present) {
        input = observe(workspace.input_path, false, delete_access);
        output = observe(workspace.output_path, false, delete_access);
    }
    auto& final = workspace.final;
    final = observe(workspace.final_path, false);
    for (const auto item : {std::pair{&input, &*record.input}, std::pair{&output, &*record.output}}) {
        const auto leaf = item.first == &input ? QStringLiteral("input.bin") : QStringLiteral("converted.part");
        state = verifyObject(*item.first, *item.second, job_canonical + QLatin1Char('\\') + leaf, security);
        if (state != OpenState::Present && state != OpenState::Missing)
            return failure(state);
    }
    if (final.state != OpenState::Present && final.state != OpenState::Missing)
        return failure(final.state);
    if (record.stage == DownloadOwnershipStage::WorkspaceReady && final.state == OpenState::Present)
        return Assessment::Conflict;
    if (record.cleanup_complete &&
        (job.state == OpenState::Present || input.state == OpenState::Present || output.state == OpenState::Present))
        return Assessment::Conflict;
    if (final.state == OpenState::Present && output.state == OpenState::Present)
        return Assessment::Conflict;
    if (record.stage == DownloadOwnershipStage::Published && output.state == OpenState::Present)
        return Assessment::Conflict;

    const auto limit = std::min(config.max_workspace_bytes, record.maximum_workspace_bytes);
    std::uint64_t total = 0;
    for (const auto* file : {&input, &output, &final}) {
        if (file->state == OpenState::Present) {
            if (file->bytes > limit - total)
                return Assessment::Conflict;
            total += file->bytes;
        }
    }
    if (record.stage != DownloadOwnershipStage::WorkspaceReady) {
        if (input.state == OpenState::Present && input.bytes != record.input_bytes)
            return Assessment::Conflict;
        if (final.state == OpenState::Present) {
            state = verifyObject(final, *record.output, workspace.final_canonical, security);
            if (state != OpenState::Present)
                return failure(state);
            state = hashMatches(final, record);
            if (state != OpenState::Present)
                return failure(state);
        } else if (output.state == OpenState::Present) {
            state = hashMatches(output, record);
            if (state != OpenState::Present)
                return failure(state);
        }
    }

    for (const auto* directory : {&workspace.staging.leaf, &workspace.publication.leaf, &job}) {
        if (directory->state == OpenState::Present) {
            state = unchanged(*directory, true);
            if (state != OpenState::Present)
                return failure(state);
        }
    }
    for (const auto* file : {&input, &output, &final}) {
        if (file->state == OpenState::Present) {
            state = unchanged(*file, false);
            if (state != OpenState::Present)
                return failure(state);
        }
    }
    if (final.state == OpenState::Present)
        return Assessment::OwnedFinalCandidate;
    return job.state == OpenState::Present || input.state == OpenState::Present || output.state == OpenState::Present
               ? Assessment::OwnedPartial
               : Assessment::Missing;
}

DiscardOutcome refusedDiscard(const Assessment assessment) noexcept {
    switch (assessment) {
    case Assessment::Unverified:
        return DiscardOutcome::Unverified;
    case Assessment::OwnedFinalCandidate:
        return DiscardOutcome::OwnedFinalCandidate;
    case Assessment::Missing:
        return DiscardOutcome::Missing;
    case Assessment::Conflict:
        return DiscardOutcome::Conflict;
    case Assessment::Unreadable:
        return DiscardOutcome::Unreadable;
    case Assessment::OwnedPartial:
        break;
    }
    return DiscardOutcome::Unverified;
}

bool eraseOwned(Object& object, const QString& path, const bool directory) {
    if (object.state == OpenState::Missing)
        return true;
    if (unchanged(object, directory) != OpenState::Present)
        return false;
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(object.handle.get(), FileDispositionInfo, &disposition, sizeof(disposition)))
        return false;
    // Delete-on-close alone is not success: an external delete-sharing reader
    // can keep the object pending. Require observable absence after closing.
    object.handle = Handle{};
    if (observe(path, directory).state != OpenState::Missing)
        return false;
    object.state = OpenState::Missing;
    return true;
}

bool temporaryFilesAbsent(const Workspace& workspace) {
    return unchanged(workspace.staging.leaf, true) == OpenState::Present &&
           unchanged(workspace.publication.leaf, true) == OpenState::Present &&
           observe(workspace.job_path, true).state == OpenState::Missing &&
           observe(workspace.input_path, false).state == OpenState::Missing &&
           observe(workspace.output_path, false).state == OpenState::Missing;
}

bool removedUnderPinnedParents(const Workspace& workspace) {
    return temporaryFilesAbsent(workspace) && observe(workspace.final_path, false).state == OpenState::Missing;
}

bool pinCleanedAbsence(const WindowsDownloadWorkspaceConfig& config, const DownloadOwnershipRecord& record,
                       Workspace& workspace) {
    return record.cleanup_complete && !pinWorkspaceRoots(config, record, workspace) &&
           removedUnderPinnedParents(workspace);
}

std::optional<Assessment> pinPlannedAbsence(const WindowsDownloadWorkspaceConfig& config,
                                            const DownloadOwnershipRecord& record, Workspace& workspace) {
    if (record.stage != DownloadOwnershipStage::Planned)
        return Assessment::Unverified;
    if (const auto failed = pinWorkspaceRoots(config, record, workspace))
        return failed;
    if (!removedUnderPinnedParents(workspace))
        return Assessment::Unverified;
    return {};
}

bool pinReclaimable(const WindowsDownloadWorkspaceConfig& config, const DownloadOwnershipRecord& record,
                    const bool import_retired, Workspace& workspace) {
    if (pinWorkspaceRoots(config, record, workspace) || !temporaryFilesAbsent(workspace))
        return false;
    workspace.final = observe(workspace.final_path, false);
    if (workspace.final.state == OpenState::Missing)
        return true;
    // Retiring the journal row leaves the publication intact. It requires no media read or reimport.
    return record.stage == DownloadOwnershipStage::Published && import_retired && record.output &&
           workspace.final.state == OpenState::Present && workspace.final.bytes == record.output_bytes &&
           workspace.final.bytes <= config.max_workspace_bytes && workspace.security.initialize() &&
           verifyObject(workspace.final, *record.output, workspace.final_canonical, workspace.security) ==
               OpenState::Present;
}

bool reclaimProofStillValid(const Workspace& workspace, const DownloadOwnershipRecord& record) {
    if (!temporaryFilesAbsent(workspace))
        return false;
    if (workspace.final.state == OpenState::Missing)
        return observe(workspace.final_path, false).state == OpenState::Missing;
    return record.output && unchanged(workspace.final, false) == OpenState::Present &&
           verifyObject(workspace.final, *record.output, workspace.final_canonical, workspace.security) ==
               OpenState::Present;
}
} // namespace

struct WindowsPinnedDownloadFinal::Impl {
    WindowsDownloadWorkspaceConfig config;
    DownloadOwnershipRecord record;
    Workspace workspace{};
    Impl(const WindowsDownloadWorkspaceConfig& settings, const DownloadOwnershipRecord& evidence)
        : config(settings), record(evidence) {}
};
DownloadOwnershipCapacity
WindowsDownloadOwnershipRecovery::ensureCapacity(const WindowsDownloadWorkspaceConfig& config,
                                                 WindowsDownloadOwnershipStore& store,
                                                 const ArchivedDownloadJobEvidence& evidence) noexcept {
    try {
        const auto loaded = store.load();
        if (loaded.status == DownloadCheckpointLoadStatus::Missing && !loaded.journal)
            return DownloadOwnershipCapacity::Available;
        if (loaded.status != DownloadCheckpointLoadStatus::Loaded || !loaded.journal)
            return DownloadOwnershipCapacity::HistoryUnavailable;
        const auto& records = loaded.journal->records;
        if (records.size() < DownloadOwnershipJournal::maximum_records)
            return DownloadOwnershipCapacity::Available;
        for (const auto& record : records) {
            if (!record.cleanup_complete ||
                std::ranges::find(evidence.archived_job_ids, record.job_id) == evidence.archived_job_ids.end())
                continue;
            const auto import_retired = std::ranges::find(evidence.retired_import_job_ids, record.job_id) !=
                                        evidence.retired_import_job_ids.end();
            Workspace workspace;
            if (!pinReclaimable(config, record, import_retired, workspace))
                continue;
            const auto saved =
                store.reclaimAtCapacity(record, [&] { return reclaimProofStillValid(workspace, record); });
            return saved == DownloadCheckpointSaveStatus::Saved ? DownloadOwnershipCapacity::Available
                                                                : DownloadOwnershipCapacity::ReclaimUnconfirmed;
        }
        return DownloadOwnershipCapacity::Full;
    } catch (...) {
        return DownloadOwnershipCapacity::HistoryUnavailable;
    }
}
WindowsPinnedDownloadFinal::WindowsPinnedDownloadFinal(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WindowsPinnedDownloadFinal::~WindowsPinnedDownloadFinal() = default;
QString WindowsPinnedDownloadFinal::pathForValidation() const { return impl_->workspace.final_path; }
std::shared_ptr<const WindowsDownloadReadInput> WindowsPinnedDownloadFinal::readInput() const {
    const auto& workspace = impl_->workspace;
    const auto consistent = [&] {
        if (!impl_->record.output || workspace.final.state != OpenState::Present ||
            !matches(workspace.final.identity, *impl_->record.output) ||
            workspace.final.bytes != impl_->record.output_bytes)
            return false;
        for (const auto* parent : {&workspace.staging.leaf, &workspace.publication.leaf}) {
            if (parent->state != OpenState::Present || unchanged(*parent, true) != OpenState::Present)
                return false;
        }
        for (const auto [object, directory] :
             {std::pair{&workspace.job, true}, std::pair{&workspace.input, false}, std::pair{&workspace.output, false},
              std::pair{&workspace.final, false}}) {
            if (object->state == OpenState::Missing)
                continue;
            if (object->state != OpenState::Present || unchanged(*object, directory) != OpenState::Present ||
                workspace.security.check(object->handle.get()) != OpenState::Present)
                return false;
        }
        return true;
    };
    if (!consistent())
        return {};
    const auto input =
        WindowsDownloadReadInput::fromHandle(reinterpret_cast<std::uintptr_t>(workspace.final.handle.get()));
    return input && consistent() ? input : std::shared_ptr<const WindowsDownloadReadInput>{};
}
const DownloadOwnershipRecord& WindowsPinnedDownloadFinal::record() const { return impl_->record; }
DownloadOwnershipAssessment WindowsPinnedDownloadFinal::revalidate() const noexcept {
    return WindowsDownloadOwnershipRecovery::assess(impl_->config, impl_->record);
}
DownloadFinalPinResult WindowsDownloadOwnershipRecovery::pinFinal(const WindowsDownloadWorkspaceConfig& config,
                                                                  const DownloadOwnershipRecord& record) noexcept {
    try {
        auto impl = std::make_unique<WindowsPinnedDownloadFinal::Impl>(config, record);
        const auto assessment = assessExisting(config, record, impl->workspace);
        if (assessment != Assessment::OwnedFinalCandidate)
            return {.assessment = assessment};
        return {.assessment = assessment,
                .candidate =
                    std::unique_ptr<WindowsPinnedDownloadFinal>(new WindowsPinnedDownloadFinal(std::move(impl)))};
    } catch (...) {
        return {.assessment = Assessment::Unreadable};
    }
}

DownloadOwnershipAssessment WindowsDownloadOwnershipRecovery::assess(const WindowsDownloadWorkspaceConfig& config,
                                                                     const DownloadOwnershipRecord& record) noexcept {
    try {
        Workspace workspace;
        return assessExisting(config, record, workspace);
    } catch (...) {
        return DownloadOwnershipAssessment::Unreadable;
    }
}

bool WindowsDownloadOwnershipRecovery::cleanedWithoutFiles(const WindowsDownloadWorkspaceConfig& config,
                                                           const DownloadOwnershipRecord& record) noexcept {
    try {
        Workspace workspace;
        return pinCleanedAbsence(config, record, workspace);
    } catch (...) {
        return false;
    }
}

bool WindowsDownloadOwnershipRecovery::plannedWithoutFiles(const WindowsDownloadWorkspaceConfig& config,
                                                           const DownloadOwnershipRecord& record) noexcept {
    try {
        Workspace workspace;
        return !pinPlannedAbsence(config, record, workspace);
    } catch (...) {
        return false;
    }
}

DownloadOwnershipDiscardOutcome
WindowsDownloadOwnershipRecovery::confirmPlannedWithoutFiles(const WindowsDownloadWorkspaceConfig& config,
                                                             const DownloadOwnershipRecord& record,
                                                             DownloadOwnershipStore& store) noexcept {
    bool absence_proven = false;
    try {
        Workspace workspace;
        if (const auto failed = pinPlannedAbsence(config, record, workspace))
            return refusedDiscard(*failed);
        absence_proven = true;
        const auto still_absent = [&workspace] { return removedUnderPinnedParents(workspace); };
        const auto saved = record.cleanup_complete ? store.acknowledgeCleanedRecord(record, still_absent)
                                                   : store.markPlannedCleaned(record, still_absent);
        return saved == DownloadCheckpointSaveStatus::Saved ? DiscardOutcome::Discarded : DiscardOutcome::Unconfirmed;
    } catch (...) {
        return absence_proven ? DiscardOutcome::Unconfirmed : DiscardOutcome::Unreadable;
    }
}

DownloadOwnershipDiscardOutcome
WindowsDownloadOwnershipRecovery::discardPartials(const WindowsDownloadWorkspaceConfig& config,
                                                  const DownloadOwnershipRecord& record,
                                                  DownloadOwnershipStore& store) noexcept {
    bool mutation_started = false;
    bool cleanup_proven = false;
    try {
        if (record.cleanup_complete) {
            Workspace pinned;
            if (pinCleanedAbsence(config, record, pinned)) {
                cleanup_proven = true;
                const auto saved =
                    store.acknowledgeCleanedRecord(record, [&] { return removedUnderPinnedParents(pinned); });
                return saved == DownloadCheckpointSaveStatus::Saved ? DiscardOutcome::Discarded
                                                                    : DiscardOutcome::Unconfirmed;
            }
        }
        Workspace workspace;
        const auto assessment = assessExisting(config, record, workspace, true);
        if (assessment != Assessment::OwnedPartial)
            return refusedDiscard(assessment);
        auto cleaned = record;
        cleaned.cleanup_complete = true;
        mutation_started = true;
        if (!eraseOwned(workspace.input, workspace.input_path, false) ||
            !eraseOwned(workspace.output, workspace.output_path, false))
            return DiscardOutcome::Incomplete;
        if (workspace.job.state == OpenState::Present &&
            (workspace.security.check(workspace.job.handle.get()) != OpenState::Present ||
             knownChildrenOnly(workspace.job_path, true) != OpenState::Present))
            return DiscardOutcome::Incomplete;
        if (!eraseOwned(workspace.job, workspace.job_path, true) || !removedUnderPinnedParents(workspace))
            return DiscardOutcome::Incomplete;
        cleanup_proven = true;
        // This action requires its own durable acknowledgement. Pins outlive the save.
        return store.record(cleaned) == DownloadCheckpointSaveStatus::Saved ? DiscardOutcome::Discarded
                                                                            : DiscardOutcome::Unconfirmed;
    } catch (...) {
        return cleanup_proven     ? DiscardOutcome::Unconfirmed
               : mutation_started ? DiscardOutcome::Incomplete
                                  : DiscardOutcome::Unreadable;
    }
}
} // namespace music_player::m0

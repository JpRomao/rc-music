#include "presentation/PlayerDownloadImport.hpp"

#include "presentation/PlayerPresenter.hpp"

#include <QPointer>
#include <QScopedValueRollback>
#include <QString>
#include <QThread>

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr std::size_t max_id_bytes = 64;
constexpr std::size_t max_native_path_units = 30'000;

bool validId(const std::string_view id) {
    return !id.empty() && id.size() <= max_id_bytes && std::ranges::all_of(id, [](const char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
               value == '_' || value == '-';
    });
}

template <typename Character> Character asciiLower(const Character value) {
    return value >= 'A' && value <= 'Z' ? static_cast<Character>(value + ('a' - 'A')) : value;
}

#ifdef Q_OS_WIN
bool windowsSeparator(const wchar_t value) { return value == L'/' || value == L'\\'; }

bool reservedWindowsComponent(const std::wstring_view component) {
    const auto stem = component.substr(0, component.find(L'.'));
    const auto equals = [stem](const std::wstring_view name) {
        return stem.size() == name.size() &&
               std::ranges::equal(stem, name,
                                  [](const wchar_t left, const wchar_t right) { return asciiLower(left) == right; });
    };
    if (equals(L"con") || equals(L"prn") || equals(L"aux") || equals(L"nul"))
        return true;
    if (stem.size() != 4)
        return false;
    const auto prefix = stem.substr(0, 3);
    const bool serial = asciiLower(prefix[0]) == L'c' && asciiLower(prefix[1]) == L'o' && asciiLower(prefix[2]) == L'm';
    const bool printer =
        asciiLower(prefix[0]) == L'l' && asciiLower(prefix[1]) == L'p' && asciiLower(prefix[2]) == L't';
    const wchar_t suffix = stem[3];
    return (serial || printer) &&
           ((suffix >= L'1' && suffix <= L'9') || suffix == L'\u00b9' || suffix == L'\u00b2' || suffix == L'\u00b3');
}

bool validWindowsPath(const std::wstring& native) {
    const QString text = QString::fromStdWString(native);
    if (!text.isValidUtf16() || text.toStdWString() != native || native.size() < 3 ||
        !((native[0] >= L'a' && native[0] <= L'z') || (native[0] >= L'A' && native[0] <= L'Z')) || native[1] != L':' ||
        !windowsSeparator(native[2]))
        return false;
    for (std::size_t index = 3; index < native.size(); ++index) {
        const wchar_t value = native[index];
        if (value < L' ' || value == L':' || value == L'<' || value == L'>' || value == L'"' || value == L'|' ||
            value == L'?' || value == L'*')
            return false;
    }
    std::size_t begin = 3;
    while (begin < native.size()) {
        auto end = begin;
        while (end < native.size() && !windowsSeparator(native[end]))
            ++end;
        if (end == begin || native[end - 1] == L'.' || native[end - 1] == L' ' ||
            reservedWindowsComponent(std::wstring_view{native}.substr(begin, end - begin)))
            return false;
        begin = end + 1;
    }
    return true;
}
#endif

bool validPublishedPath(const std::filesystem::path& path) {
    const auto& native = path.native();
    if (native.empty() || native.size() > max_native_path_units || !path.is_absolute() ||
        path != path.lexically_normal() || std::ranges::find(native, 0) != native.end())
        return false;
    auto extension = path.extension().native();
    std::ranges::transform(extension, extension.begin(), [](const auto value) { return asciiLower(value); });
    if (extension != std::filesystem::path{".mp3"}.native())
        return false;
#ifdef Q_OS_WIN
    return validWindowsPath(native);
#else
    const auto text = QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
    return text.toUtf8().toStdString() == native;
#endif
}

} // namespace

struct PlayerDownloadImport::Impl final {
    struct Receipt final {
        std::filesystem::path path;
        bool committed{false};
    };
    QPointer<PlayerPresenter> presenter;
    QThread* const owner_thread;
    const PlayerDownloadImportLimits limits;
    std::unordered_map<std::string, Receipt> receipts{};
    std::size_t retained_bytes{0};
    bool calling_presenter{false};
    PlayerDownloadImportPersistence persistence{PlayerDownloadImportPersistence::Volatile};

    Impl(PlayerPresenter& owner, const PlayerDownloadImportLimits caps)
        : presenter(&owner), owner_thread(owner.thread()), limits(caps) {}
};

PlayerDownloadImport::PlayerDownloadImport(PlayerPresenter& presenter, const PlayerDownloadImportLimits limits,
                                           const PlayerDownloadImportPersistence persistence)
    : impl_(std::make_unique<Impl>(presenter, limits)) {
    impl_->persistence = persistence;
}
PlayerDownloadImport::~PlayerDownloadImport() = default;

DownloadImportState PlayerDownloadImport::begin(const std::string& id, const std::filesystem::path& path) {
    auto& state = *impl_;
    if (state.persistence == PlayerDownloadImportPersistence::Volatile)
        return accept(id, path) ? DownloadImportState::Committed : DownloadImportState::Failed;
    if (state.persistence != PlayerDownloadImportPersistence::Session || QThread::currentThread() != state.owner_thread)
        return DownloadImportState::Failed;
    try {
        if (!state.presenter || state.presenter->thread() != state.owner_thread || !validId(id) ||
            !validPublishedPath(path))
            return DownloadImportState::Failed;
        auto& owner = *state.presenter;
        const bool reentrant = state.calling_presenter || owner.download_import_call_in_progress_;
        const QScopedValueRollback call_guard(state.calling_presenter, true);
        const QScopedValueRollback owner_guard(owner.download_import_call_in_progress_, true);
        auto find = [&] {
            return std::ranges::find(owner.download_import_receipts_, id, &DownloadImportReceipt::job_id);
        };
        auto receipt = find();
        if (receipt != owner.download_import_receipts_.end()) {
            if (receipt->source_path != path)
                return DownloadImportState::Failed;
            const auto status = pollImport(id);
            if (status == DownloadImportState::Committed)
                return status;
            // A replay sees the queue/ledger commit before Qt row signals, but
            // cannot stage or acknowledge persistence inside that transaction.
            if (reentrant || owner.download_append_in_progress_)
                return DownloadImportState::Pending;
        } else {
            if (reentrant || owner.download_append_in_progress_ || !owner.download_import_persistence_ready_ ||
                owner.session_autosave_ == nullptr)
                return DownloadImportState::Failed;
            if (owner.download_import_receipts_.size() >= state.limits.max_receipts)
                return DownloadImportState::Failed;
            std::size_t remaining = state.limits.max_retained_bytes;
            const auto count = [&](const std::string& job, const std::filesystem::path& source) {
                if (job.size() > remaining)
                    return false;
                remaining -= job.size();
                if (source.native().size() > remaining / sizeof(std::filesystem::path::value_type))
                    return false;
                remaining -= source.native().size() * sizeof(std::filesystem::path::value_type);
                return true;
            };
            for (const auto& existing : owner.download_import_receipts_)
                if (!count(existing.job_id, existing.source_path))
                    return DownloadImportState::Failed;
            if (!count(id, path))
                return DownloadImportState::Failed;
            bool committed = false;
            static_cast<void>(owner.appendPublishedAudioImpl(path, committed, &id));
            if (!committed)
                return DownloadImportState::Failed;
            receipt = find();
        }
        if (!owner.download_import_persistence_ready_ || owner.session_autosave_ == nullptr || owner.exiting_)
            return DownloadImportState::Failed;
        const auto index = static_cast<std::size_t>(receipt - owner.download_import_receipts_.begin());
        if (owner.download_import_revisions_[index] == std::numeric_limits<SessionRevision>::max()) {
            static_cast<void>(owner.session_autosave_->scheduleStructural(owner.currentSessionSnapshot()));
            // No vector element reference is held across trusted clock callbacks.
            owner.download_import_revisions_[index] = owner.session_autosave_->latestRevision();
        }
        // Only begin (an explicit admission/retry) may rearm a failed write.
        // pollImport is read-only and never retries or promotes readback.
        if (!owner.session_autosave_->writeActive() && owner.session_autosave_->notice() != SessionAutosaveNotice::None)
            static_cast<void>(owner.session_autosave_->retry());
        owner.session_autosave_->requestSaveNow();
        owner.reconcileTimer();
        return pollImport(id);
    } catch (...) {
        // A queue+receipt commit is retained for idempotent explicit retry even
        // when subsequent snapshot/clock/writer preparation failed.
        return DownloadImportState::Failed;
    }
}
DownloadImportState PlayerDownloadImport::pollImport(const std::string_view id) {
    auto& state = *impl_;
    if (QThread::currentThread() != state.owner_thread || !state.presenter || !validId(id))
        return DownloadImportState::Failed;
    if (state.persistence == PlayerDownloadImportPersistence::Volatile) {
        const auto receipt = state.receipts.find(std::string{id});
        return receipt != state.receipts.end() && receipt->second.committed ? DownloadImportState::Committed
                                                                            : DownloadImportState::Failed;
    }
    if (state.persistence != PlayerDownloadImportPersistence::Session)
        return DownloadImportState::Failed;
    auto& owner = *state.presenter;
    const auto receipt = std::ranges::find(owner.download_import_receipts_, id, &DownloadImportReceipt::job_id);
    if (receipt == owner.download_import_receipts_.end() || owner.session_autosave_ == nullptr)
        return DownloadImportState::Failed;
    const auto index = static_cast<std::size_t>(receipt - owner.download_import_receipts_.begin());
    const auto revision = owner.download_import_revisions_[index];
    if (revision == 0 || (revision != std::numeric_limits<SessionRevision>::max() &&
                          owner.session_autosave_->savedRevision() >= revision))
        return DownloadImportState::Committed;
    if (!owner.session_autosave_->writeActive() && owner.session_autosave_->notice() != SessionAutosaveNotice::None)
        return DownloadImportState::Failed;
    return DownloadImportState::Pending;
}

bool PlayerDownloadImport::accept(const std::string& job_id, const std::filesystem::path& mp3_path) {
    auto& state = *impl_;
    if (state.persistence == PlayerDownloadImportPersistence::Session)
        return begin(job_id, mp3_path) == DownloadImportState::Committed;
    if (state.persistence != PlayerDownloadImportPersistence::Volatile)
        return false;
    // Check the immutable owner identity before touching QPointer or receipt state.
    // This port is application-thread-owned, not a cross-thread command queue.
    if (QThread::currentThread() != state.owner_thread)
        return false;
    auto receipt = state.receipts.end();
    const auto release_uncommitted = [&] {
        if (receipt == state.receipts.end() || receipt->second.committed)
            return;
        state.retained_bytes -=
            receipt->first.size() + receipt->second.path.native().size() * sizeof(std::filesystem::path::value_type);
        state.receipts.erase(receipt);
        receipt = state.receipts.end();
    };
    try {
        if (!state.presenter || state.presenter->thread() != state.owner_thread || !validId(job_id) ||
            !validPublishedPath(mp3_path))
            return false;
        receipt = state.receipts.find(job_id);
        if (receipt != state.receipts.end()) {
            if (receipt->second.path != mp3_path)
                return false;
            // The presenter records commitment before its first observable signal.
            // Reentrant replay may acknowledge it without recursing into the queue.
            if (receipt->second.committed)
                return true;
        }
        if (state.calling_presenter)
            return false;
        if (receipt == state.receipts.end()) {
            const auto path_bytes = mp3_path.native().size() * sizeof(std::filesystem::path::value_type);
            if (state.receipts.size() >= state.limits.max_receipts || job_id.size() > state.limits.max_retained_bytes ||
                path_bytes > state.limits.max_retained_bytes - job_id.size())
                return false;
            const auto bytes = job_id.size() + path_bytes;
            if (state.retained_bytes > state.limits.max_retained_bytes - bytes)
                return false;
            receipt = state.receipts.emplace(job_id, Impl::Receipt{mp3_path}).first;
            state.retained_bytes += bytes;
        }
        const QScopedValueRollback guard(state.calling_presenter, true);
        const bool accepted = state.presenter->appendPublishedAudio(receipt->second.path, receipt->second.committed);
        if (!receipt->second.committed) {
            release_uncommitted();
            return false;
        }
        return accepted;
    } catch (...) {
        // Preparation/ordinary refusal cannot reserve capacity forever. The
        // presenter promises no queue mutation unless it set commitment; retain
        // only committed receipts, even if a later observer threw.
        release_uncommitted();
        return false;
    }
}
} // namespace music_player::m0

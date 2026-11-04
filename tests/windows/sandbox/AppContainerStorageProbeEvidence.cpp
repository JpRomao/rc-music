#include "AppContainerStorageProbeEvidence.hpp"

#include "AppContainerStorageProbeOptions.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QScopeGuard>

#include <Sddl.h>

#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace music_player::sandbox_probe {
namespace {
using Evidence = AppContainerStorageProbeEvidence;
namespace artifacts = music_player::m0::download_artifacts;
constexpr std::uint32_t exception_error = ERROR_GEN_FAILURE;
struct Busy final {
    bool& value;
    explicit Busy(bool& held) : value(held) { value = true; }
    ~Busy() { value = false; }
};
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
QString leafName(const AppContainerStorageProbeEvidence::Leaf leaf) {
    using Leaf = AppContainerStorageProbeEvidence::Leaf;
    switch (leaf) {
    case Leaf::Before:
        return QStringLiteral("00-before-profile.json");
    case Leaf::Profile:
        return QStringLiteral("01-profile-result.json");
    case Leaf::Inventory:
        return QStringLiteral("02-inventory.json");
    case Leaf::Close:
        return QStringLiteral("03-read-close.json");
    case Leaf::Cleanup:
        return QStringLiteral("04-profile-cleanup.json");
    case Leaf::Result:
        return QStringLiteral("05-result.json");
    }
    return {};
}
std::uint32_t lastError(const std::uint32_t fallback = ERROR_GEN_FAILURE) {
    const auto value = static_cast<std::uint32_t>(GetLastError());
    return value ? value : fallback;
}
Evidence::Result failure(const Evidence::Phase phase, const std::uint32_t error) { return {false, error, phase}; }
Evidence::Result complete() { return {true, ERROR_SUCCESS, Evidence::Phase::Complete}; }
bool validFile(const HANDLE handle) { return handle && handle != INVALID_HANDLE_VALUE; }
// The store requires flags 0, not the inheritable OICI ACEs on the outer
// evidence directory. Only a new leaf is created; no existing ACL is repaired.
std::uint32_t createJournalDirectory(const QString& directory) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return lastError();
    const artifacts::Handle token(raw);
    DWORD required = 0;
    SetLastError(ERROR_SUCCESS);
    const bool query = GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required) != FALSE;
    const auto query_error = static_cast<std::uint32_t>(GetLastError());
    if (query || query_error != ERROR_INSUFFICIENT_BUFFER || required < sizeof(TOKEN_USER) || required > 64 * 1024)
        return query_error ? query_error : ERROR_INVALID_DATA;
    std::vector<unsigned char> bytes(required);
    DWORD used = 0;
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), required, &used))
        return lastError();
    if (used < sizeof(TOKEN_USER) || used > bytes.size())
        return ERROR_INVALID_DATA;
    TOKEN_USER user{};
    std::memcpy(&user, bytes.data(), sizeof(user));
    const auto base = reinterpret_cast<std::uintptr_t>(bytes.data());
    const auto address = reinterpret_cast<std::uintptr_t>(user.User.Sid);
    if (!user.User.Sid || address < base || address - base > used || used - (address - base) < 8)
        return ERROR_INVALID_DATA;
    const auto offset = address - base;
    const auto count = bytes[offset + 1];
    const auto length = 8 + 4 * std::size_t{count};
    if (bytes[offset] != SID_REVISION || count > SID_MAX_SUB_AUTHORITIES || length > used - offset)
        return ERROR_INVALID_DATA;
    LPWSTR user_text = nullptr;
    if (!ConvertSidToStringSidW(user.User.Sid, &user_text))
        return lastError();
    const auto free_text = qScopeGuard([&] { LocalFree(user_text); });
    const auto sddl =
        QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(user_text) + QLatin1Char(')');
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return lastError();
    const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(artifacts::nativePath(directory).c_str(), &attributes) ? ERROR_SUCCESS : lastError();
}
} // namespace
struct AppContainerStorageProbeEvidence::Impl final {
    struct Slot final {
        HANDLE handle{}; // Failed close is retained, never retried/destructor-closed.
        bool attempted{false};
        Result result{};
    };
    QString workspace, directory, journal;
    Calls calls;
    std::vector<artifacts::Handle> pins{};
    std::array<Slot, 6> receipt_slots{};
    bool create_attempted{}, journal_attempted{}, ready{}, working{};
    Impl(QString root, QString evidence, Calls bindings)
        : workspace(owned(root)), directory(owned(evidence)), journal(directory + QStringLiteral("/profile-journal")),
          calls(std::move(bindings)) {}
};
AppContainerStorageProbeEvidence::Calls AppContainerStorageProbeEvidence::systemCalls() {
    return {.write =
                [](const Handle raw, const QByteArrayView bytes) {
                    if (!validFile(reinterpret_cast<HANDLE>(raw)))
                        return WriteResult{ERROR_INVALID_HANDLE, 0};
                    if (bytes.isEmpty() || bytes.size() > maximum_receipt_bytes)
                        return WriteResult{ERROR_INVALID_PARAMETER, 0};
                    DWORD used = 0;
                    const auto ok = WriteFile(reinterpret_cast<HANDLE>(raw), bytes.data(),
                                              static_cast<DWORD>(bytes.size()), &used, nullptr);
                    return WriteResult{ok ? ERROR_SUCCESS : lastError(), static_cast<std::uint32_t>(used)};
                },
            .flush =
                [](const Handle raw) {
                    if (!validFile(reinterpret_cast<HANDLE>(raw)))
                        return std::uint32_t{ERROR_INVALID_HANDLE};
                    return FlushFileBuffers(reinterpret_cast<HANDLE>(raw)) ? std::uint32_t{ERROR_SUCCESS} : lastError();
                },
            .close =
                [](const Handle raw) {
                    if (!validFile(reinterpret_cast<HANDLE>(raw)))
                        return std::uint32_t{ERROR_INVALID_HANDLE};
                    return CloseHandle(reinterpret_cast<HANDLE>(raw)) ? std::uint32_t{ERROR_SUCCESS} : lastError();
                }};
}
AppContainerStorageProbeEvidence::AppContainerStorageProbeEvidence(QString workspace, QString evidence)
    : AppContainerStorageProbeEvidence(std::move(workspace), std::move(evidence), systemCalls()) {}
AppContainerStorageProbeEvidence::AppContainerStorageProbeEvidence(QString workspace, QString evidence, Calls calls)
    : impl_(std::make_unique<Impl>(std::move(workspace), std::move(evidence), std::move(calls))) {}
AppContainerStorageProbeEvidence::~AppContainerStorageProbeEvidence() {
    // Pins are ordinary evidence resources, not a profile/fence ACK. Never
    // retry an uncertain writer handle. Preserve active callback state rather
    // than destroy it beneath a reentrant trusted fault-injection callback.
    if (impl_ && impl_->working)
        static_cast<void>(impl_.release());
}
const QString& AppContainerStorageProbeEvidence::directory() const noexcept { return impl_->directory; }
const QString& AppContainerStorageProbeEvidence::journalDirectory() const noexcept { return impl_->journal; }
QString AppContainerStorageProbeEvidence::path(const Leaf leaf) const {
    const auto name = leafName(leaf);
    return name.isEmpty() ? QString{} : impl_->directory + QLatin1Char('/') + name;
}
AppContainerStorageProbeEvidence::Result AppContainerStorageProbeEvidence::create() noexcept {
    auto* const held = impl_.get();
    if (!held)
        return failure(Phase::Input, ERROR_INVALID_HANDLE);
    if (held->working)
        return failure(Phase::Input, ERROR_BUSY);
    if (std::exchange(held->create_attempted, true))
        return failure(Phase::EvidenceDirectory, ERROR_ALREADY_EXISTS);
    Busy guard(held->working);
    Phase phase = Phase::Input;
    try {
        const auto options = appContainerStorageProbeOptions(
            {QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), held->directory},
            held->workspace);
        if (options.mode != AppContainerStorageProbeOptions::Mode::Inventory)
            return failure(phase, ERROR_INVALID_NAME);
        QString normalized;
        phase = Phase::Drive;
        if (!artifacts::validPath(held->directory, normalized))
            return failure(phase, ERROR_NOT_SUPPORTED);
        phase = Phase::Ancestors;
        SetLastError(ERROR_SUCCESS);
        if (!artifacts::pinAncestors(normalized, held->pins))
            return failure(phase, lastError(ERROR_INVALID_DATA));
        phase = Phase::EvidenceDirectory;
        SetLastError(ERROR_SUCCESS);
        if (!artifacts::createPrivateDirectory(normalized))
            return failure(phase, lastError());
        phase = Phase::EvidencePin;
        SetLastError(ERROR_SUCCESS);
        if (!artifacts::pinAncestors(normalized + QStringLiteral("/fixed-receipt-slot"), held->pins))
            return failure(phase, lastError(ERROR_INVALID_DATA));
        held->ready = true;
        return complete();
    } catch (...) {
        return failure(phase, exception_error);
    }
}
AppContainerStorageProbeEvidence::Result AppContainerStorageProbeEvidence::createJournal() noexcept {
    auto* const held = impl_.get();
    if (!held || !held->ready)
        return failure(Phase::Input, ERROR_INVALID_HANDLE);
    if (held->working)
        return failure(Phase::Input, ERROR_BUSY);
    if (std::exchange(held->journal_attempted, true))
        return failure(Phase::JournalDirectory, ERROR_ALREADY_EXISTS);
    Busy guard(held->working);
    try {
        const auto error = createJournalDirectory(held->journal);
        return error ? failure(Phase::JournalDirectory, error) : complete();
    } catch (...) {
        return failure(Phase::JournalDirectory, exception_error);
    }
}
AppContainerStorageProbeEvidence::Result
AppContainerStorageProbeEvidence::writeNew(const Leaf leaf, const QByteArrayView bytes) noexcept {
    auto* const held = impl_.get();
    const auto ordinal = static_cast<unsigned>(leaf);
    if (!held || !held->ready)
        return failure(Phase::Input, ERROR_INVALID_HANDLE);
    if (held->working)
        return failure(Phase::Input, ERROR_BUSY);
    if (ordinal >= held->receipt_slots.size() || bytes.isEmpty() || bytes.size() > maximum_receipt_bytes ||
        !held->calls.write || !held->calls.flush || !held->calls.close)
        return failure(Phase::Input, ERROR_INVALID_PARAMETER);
    auto& slot = held->receipt_slots[ordinal];
    if (slot.attempted)
        return slot.result.ok ? failure(Phase::Open, ERROR_FILE_EXISTS) : slot.result;
    Busy guard(held->working);
    slot.attempted = true;
    slot.result = failure(Phase::Open, not_called);
    bool write_complete = false, flush_complete = false;
    try {
        const auto native = artifacts::nativePath(held->directory + QLatin1Char('/') + leafName(leaf));
        const auto handle = CreateFileW(native.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (!validFile(handle)) {
            slot.result.error = lastError();
            return slot.result;
        }
        slot.handle = handle; // Retain ownership before any callback can throw.
        slot.result.phase = Phase::Write;
        const auto written = held->calls.write(reinterpret_cast<Handle>(slot.handle), bytes);
        slot.result.error = written.error;
        slot.result.bytes_written = written.bytes;
        write_complete = written.error == ERROR_SUCCESS && written.bytes == static_cast<std::uint32_t>(bytes.size());
        if (write_complete) {
            slot.result.phase = Phase::Flush;
            slot.result.error = held->calls.flush(reinterpret_cast<Handle>(slot.handle));
            flush_complete = slot.result.error == ERROR_SUCCESS;
        }
    } catch (...) {
        slot.result.error = exception_error;
    }
    // Write/flush failure, short write and callback exception still receive one
    // explicit close attempt. A failed close is a separate raw observation.
    if (validFile(slot.handle)) {
        std::uint32_t close_error = exception_error;
        try {
            close_error = held->calls.close(reinterpret_cast<Handle>(slot.handle));
        } catch (...) {
        }
        slot.result.close_error = close_error;
        if (close_error == ERROR_SUCCESS)
            slot.handle = nullptr;
        if (write_complete && flush_complete) {
            slot.result.error = close_error;
            slot.result.phase = close_error ? Phase::Close : Phase::Complete;
            slot.result.ok = close_error == ERROR_SUCCESS;
        }
    }
    return slot.result;
}
} // namespace music_player::sandbox_probe

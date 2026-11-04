#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <QByteArray>
#include <QString>

#include <string>
#include <utility>
#include <vector>

namespace music_player::m0::download_artifacts {

inline constexpr quint64 max_artifact_bytes = 128ULL * 1024 * 1024;

enum class Error { None, FileAccess, HashMismatch };

class Handle final {
  public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    ~Handle() { close(); }
    bool valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return handle_; }

  private:
    HANDLE handle_;
    void close() noexcept {
        if (valid())
            CloseHandle(std::exchange(handle_, INVALID_HANDLE_VALUE));
    }
};

struct Artifact final {
    QString path{};
    QByteArray sha256{};
    Handle handle{};
    FILE_ID_INFO identity{};
    QString final_path{};
    quint64 bytes{0};
};

std::wstring nativePath(const QString& path);
bool validPath(const QString& original, QString& normalized);
bool validPin(const QByteArray& pin);
QString finalPath(HANDLE handle);
bool information(HANDLE handle, bool directory, FILE_ID_INFO& identity, quint64* size = nullptr);
bool sameIdentity(const FILE_ID_INFO& left, const FILE_ID_INFO& right) noexcept;
bool pinAncestors(const QString& path, std::vector<Handle>& pins);
bool unchanged(const Artifact& artifact);
Error pinAndHash(Artifact& artifact, std::vector<Handle>& ancestors);
bool moduleMatches(const void* address, const Artifact& expected);
// Private child creation/copy/removal used by the reviewed download runtimes.
// Copy reads only the held source capability; removal checks the recorded ID.
bool createPrivateDirectory(const QString& path);
bool copyPinned(const Artifact& source, Artifact& destination);
bool removeOwned(const QString& path, bool directory, const FILE_ID_INFO& expected, const QString& final_path);

} // namespace music_player::m0::download_artifacts

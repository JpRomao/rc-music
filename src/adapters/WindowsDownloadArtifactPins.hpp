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
Error hashPinned(const Artifact& artifact);
// The caller holds an ordinary directory pin for each lexical ancestor.
Error pinAndHashUnderPinnedAncestors(Artifact& artifact);
Error pinAndHash(Artifact& artifact, std::vector<Handle>& ancestors);
bool moduleMatches(const void* address, const Artifact& expected);
// Private child creation/copy/removal used by the reviewed download runtimes.
// Copy reads only the held source capability; removal checks the recorded ID.
bool createPrivateDirectory(const QString& path);
bool copyPinned(const Artifact& source, Artifact& destination);
// For a per-job copy verified before use and never recovered after a restart.
bool copyPinnedTemporary(const Artifact& source, Artifact& destination);
// Opt-in runtime only: create the new objects with the final protected DACL,
// granting this canonical AppContainer SID exactly read/execute. No parent or
// source ACL is changed; existing destinations are never opened for mutation.
bool createPrivateDirectory(const QString& path, const QByteArray& app_container_sid);
bool copyPinned(const Artifact& source, Artifact& destination, const QByteArray& app_container_sid);
bool runtimeDaclMatches(HANDLE handle, const QByteArray& app_container_sid);
bool removeOwned(const QString& path, bool directory, const FILE_ID_INFO& expected, const QString& final_path);

} // namespace music_player::m0::download_artifacts

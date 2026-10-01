#include "adapters/WindowsDownloadTlsRuntime.hpp"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLibrary>
#include <QPluginLoader>
#include <QSslSocket>

#include <algorithm>
#include <array>
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
constexpr auto backendName = "rcmusic-schannel-offline";
constexpr auto requiredQtVersion = "6.11.2";

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

std::wstring nativePath(const QString& path) {
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}

bool validPath(const QString& original, QString& normalized) {
    // Qt strips Windows extended-path prefixes during separator normalization.
    // Reject namespaces/UNC/root-relative inputs BEFORE any such rewrite.
    if (original.size() < 4 || original.at(1) != QLatin1Char(':') ||
        (original.at(2) != QLatin1Char('/') && original.at(2) != QLatin1Char('\\')) ||
        !((original.at(0) >= QLatin1Char('A') && original.at(0) <= QLatin1Char('Z')) ||
          (original.at(0) >= QLatin1Char('a') && original.at(0) <= QLatin1Char('z'))))
        return false;
    normalized = QDir::fromNativeSeparators(original);
    if (normalized.size() < 4 || normalized.size() > 30000 || normalized.at(1) != QLatin1Char(':') ||
        normalized.at(2) != QLatin1Char('/') ||
        !((normalized.at(0) >= QLatin1Char('A') && normalized.at(0) <= QLatin1Char('Z')) ||
          (normalized.at(0) >= QLatin1Char('a') && normalized.at(0) <= QLatin1Char('z'))) ||
        QString::fromUtf8(normalized.toUtf8()) != normalized)
        return false;
    const auto components = normalized.mid(3).split(QLatin1Char('/'));
    if (components.size() > 256)
        return false;
    for (const auto& component : components) {
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..") ||
            component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')))
            return false;
        for (const auto character : component) {
            if (character.category() == QChar::Other_Control || QStringLiteral(":<>\"|?*").contains(character))
                return false;
        }
    }
    const auto drive = QDir::toNativeSeparators(normalized.left(3)).toStdWString();
    return GetDriveTypeW(drive.c_str()) == DRIVE_FIXED;
}

bool validPin(const QByteArray& pin) {
    return pin.size() == 64 && std::all_of(pin.cbegin(), pin.cend(), [](const char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

QString finalPath(HANDLE handle) {
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    if (required == 0 || required > 32768)
        return {};
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1);
    const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (written == 0 || written >= buffer.size())
        return {};
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written));
}

bool information(HANDLE handle, bool directory, FILE_ID_INFO& identity, quint64* size = nullptr) {
    BY_HANDLE_FILE_INFORMATION basic{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &basic) ||
        (basic.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((basic.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        !GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)))
        return false;
    if (size) {
        LARGE_INTEGER bytes{};
        if (!GetFileSizeEx(handle, &bytes) || bytes.QuadPart <= 0 ||
            static_cast<quint64>(bytes.QuadPart) > download_tls_max_artifact_bytes)
            return false;
        *size = static_cast<quint64>(bytes.QuadPart);
    }
    return true;
}

bool sameIdentity(const FILE_ID_INFO& left, const FILE_ID_INFO& right) noexcept {
    return left.VolumeSerialNumber == right.VolumeSerialNumber &&
           std::memcmp(left.FileId.Identifier, right.FileId.Identifier, sizeof(left.FileId.Identifier)) == 0;
}

bool pinAncestors(const QString& path, std::vector<Handle>& pins) {
    QString prefix = path.left(3);
    const auto components = path.mid(3).split(QLatin1Char('/'));
    for (qsizetype index = 0; index < components.size(); ++index) {
        // index 0 pins the drive; subsequent iterations pin ancestors only.
        if (index != 0) {
            if (!prefix.endsWith(QLatin1Char('/')))
                prefix += QLatin1Char('/');
            prefix += components.at(index - 1);
        }
        Handle handle(CreateFileW(nativePath(prefix).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FILE_ID_INFO identity{};
        if (!handle.valid() || !information(handle.get(), true, identity))
            return false;
        pins.push_back(std::move(handle));
    }
    return true;
}

struct Artifact final {
    QString path{};
    QByteArray sha256{};
    Handle handle{};
    FILE_ID_INFO identity{};
    QString final_path{};
    quint64 bytes{0};
};

bool unchanged(const Artifact& artifact) {
    FILE_ID_INFO identity{};
    quint64 bytes = 0;
    return artifact.handle.valid() && information(artifact.handle.get(), false, identity, &bytes) &&
           sameIdentity(identity, artifact.identity) && bytes == artifact.bytes &&
           finalPath(artifact.handle.get()) == artifact.final_path;
}

WindowsDownloadTlsRuntimeError pinAndHash(Artifact& artifact, std::vector<Handle>& ancestors) {
    if (!pinAncestors(artifact.path, ancestors))
        return WindowsDownloadTlsRuntimeError::FileAccess;
    artifact.handle =
        Handle(CreateFileW(nativePath(artifact.path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!artifact.handle.valid() || !information(artifact.handle.get(), false, artifact.identity, &artifact.bytes))
        return WindowsDownloadTlsRuntimeError::FileAccess;
    artifact.final_path = finalPath(artifact.handle.get());
    if (artifact.final_path.isEmpty())
        return WindowsDownloadTlsRuntimeError::FileAccess;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> block{};
    quint64 consumed = 0;
    while (consumed < artifact.bytes) {
        DWORD count = 0;
        const auto wanted = static_cast<DWORD>(std::min<quint64>(block.size(), artifact.bytes - consumed));
        if (!ReadFile(artifact.handle.get(), block.data(), wanted, &count, nullptr) || count == 0 || count > wanted)
            return WindowsDownloadTlsRuntimeError::FileAccess;
        hash.addData(QByteArrayView{block.data(), static_cast<qsizetype>(count)});
        consumed += count;
    }
    if (!unchanged(artifact))
        return WindowsDownloadTlsRuntimeError::FileAccess;
    return hash.result().toHex() == artifact.sha256 ? WindowsDownloadTlsRuntimeError::None
                                                    : WindowsDownloadTlsRuntimeError::HashMismatch;
}

bool moduleMatches(const void* address, const Artifact& expected) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module))
        return false;
    // UNCHANGED_REFCOUNT: never FreeLibrary this handle. Core/Network are linked
    // dependencies and the plugin's QPluginLoader retains its own lifetime.
    std::array<wchar_t, 32768> buffer{};
    const DWORD written = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size())
        return false;
    auto path = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written));
    if (path.startsWith(QStringLiteral("\\\\?\\")))
        path.remove(0, 4);
    QString normalized;
    if (!validPath(path, normalized))
        return false;
    Handle actual(CreateFileW(nativePath(normalized).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    FILE_ID_INFO identity{};
    quint64 bytes = 0;
    return actual.valid() && information(actual.get(), false, identity, &bytes) &&
           sameIdentity(identity, expected.identity) && bytes == expected.bytes &&
           finalPath(actual.get()) == expected.final_path && unchanged(expected);
}
} // namespace

struct WindowsDownloadTlsRuntime::Impl final {
    WindowsDownloadTlsRuntimeError error{WindowsDownloadTlsRuntimeError::None};
    bool attempted{false};
    std::vector<Handle> ancestors{};
    std::array<Artifact, 3> artifacts{};
    QPluginLoader loader{};
};

WindowsDownloadTlsRuntime::WindowsDownloadTlsRuntime() : impl_(std::make_unique<Impl>()) {}
WindowsDownloadTlsRuntime::~WindowsDownloadTlsRuntime() = default;

bool WindowsDownloadTlsRuntime::activate(const WindowsDownloadTlsRuntimeConfig& config) {
    if (impl_->attempted) {
        impl_->error = WindowsDownloadTlsRuntimeError::AlreadyAttempted;
        return false;
    }
    impl_->attempted = true;
    if (!QCoreApplication::instance()) {
        impl_->error = WindowsDownloadTlsRuntimeError::MissingApplication;
        return false;
    }
    QCoreApplication::setLibraryPaths({});
    const auto fail = [this](WindowsDownloadTlsRuntimeError error) {
        impl_->error = error;
        return false;
    };
    try {
        auto& artifacts = impl_->artifacts;
        if (!validPath(config.core_path, artifacts[0].path) || !validPin(config.core_sha256) ||
            !validPath(config.network_path, artifacts[1].path) || !validPin(config.network_sha256) ||
            !validPath(config.plugin_path, artifacts[2].path) || !validPin(config.plugin_sha256))
            return fail(WindowsDownloadTlsRuntimeError::InvalidConfiguration);
        artifacts[0].sha256 = config.core_sha256;
        artifacts[1].sha256 = config.network_sha256;
        artifacts[2].sha256 = config.plugin_sha256;
        for (auto& artifact : artifacts) {
            const auto error = pinAndHash(artifact, impl_->ancestors);
            if (error != WindowsDownloadTlsRuntimeError::None)
                return fail(error);
        }
        if (std::strcmp(QT_VERSION_STR, requiredQtVersion) != 0 || std::strcmp(qVersion(), requiredQtVersion) != 0 ||
            !moduleMatches(reinterpret_cast<const void*>(&qVersion), artifacts[0]) ||
            !moduleMatches(&QAbstractSocket::staticMetaObject, artifacts[1]))
            return fail(WindowsDownloadTlsRuntimeError::RuntimeMismatch);
        if (!QCoreApplication::libraryPaths().isEmpty())
            return fail(WindowsDownloadTlsRuntimeError::Activation);
        // Clearing search paths does not unload registered Qt backend instances.
        // Check BEFORE manual load, even if the existing name is our own name.
        if (!QSslSocket::availableBackends().isEmpty())
            return fail(WindowsDownloadTlsRuntimeError::PreexistingBackend);
        impl_->loader.setLoadHints(QLibrary::PreventUnloadHint);
        impl_->loader.setFileName(artifacts[2].path);
        const auto* instance = impl_->loader.instance();
        if (!instance)
            return fail(WindowsDownloadTlsRuntimeError::PluginLoad);
        if (!moduleMatches(instance->metaObject(), artifacts[2]))
            return fail(WindowsDownloadTlsRuntimeError::RuntimeMismatch);
        const auto expected_backend = QString::fromLatin1(backendName);
        if (QSslSocket::availableBackends() != QStringList{expected_backend})
            return fail(WindowsDownloadTlsRuntimeError::UnexpectedBackend);
        if (!QCoreApplication::libraryPaths().isEmpty() || !QSslSocket::setActiveBackend(expected_backend) ||
            QSslSocket::activeBackend() != expected_backend || !QSslSocket::supportsSsl())
            return fail(WindowsDownloadTlsRuntimeError::Activation);
        impl_->error = WindowsDownloadTlsRuntimeError::None;
        return true;
    } catch (...) {
        // No free diagnostics or fallback. The caller must stop this worker.
        return fail(WindowsDownloadTlsRuntimeError::FileAccess);
    }
}

WindowsDownloadTlsRuntimeError WindowsDownloadTlsRuntime::error() const noexcept { return impl_->error; }

} // namespace music_player::m0

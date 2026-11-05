#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

class QNetworkAccessManager;

namespace music_player::m0 {
// Optional API client for an explicitly configured Lidarr instance. Credentials
// and lookup results stay in memory; this adapter never receives audio bytes,
// imports files or changes playback. Mutations require requestAlbum explicitly.
class QtLidarrService final : public QObject {
    Q_OBJECT
  public:
    enum class Kind { Album, Artist };
    enum class Status { Disconnected, Connecting, Ready, Searching, Requesting, Requested, Error };
    enum class Error {
        None,
        Configuration,
        NotConnected,
        Busy,
        Authentication,
        Network,
        Http,
        Timeout,
        Response,
        Limit,
        Selection,
        Unavailable,
        Cancelled
    };
    struct Profile final {
        int id{};
        QString name;
        friend bool operator==(const Profile&, const Profile&) = default;
    };
    struct ResultItem final {
        // A service-generated handle valid only for the current lookup. The
        // original API object is private and is never accepted back from QML.
        QString key;
        Kind kind{Kind::Album};
        QString title;
        QString artist;
        QString year;
        int existing_id{};
        friend bool operator==(const ResultItem&, const ResultItem&) = default;
    };
    static constexpr qint64 maximum_response_bytes = 4 * 1024 * 1024;
    static constexpr qsizetype maximum_results = 500;

    explicit QtLidarrService(QNetworkAccessManager* borrowed_network = nullptr, QObject* parent = nullptr,
                             int request_timeout_ms = 10'000);
    ~QtLidarrService() override;

    // Validates the explicit base address and checks status plus configured
    // roots/profiles. HTTP is supported for self-hosted instances; callers must
    // inform the user that it sends the key without TLS. No redirect following.
    [[nodiscard]] bool connectServer(QString address, QString api_key);
    void disconnectServer();
    [[nodiscard]] bool search(QString term, Kind kind);
    // Requests a full album through Lidarr, not individual tracks. An existing
    // album uses AlbumSearch; a new one is added with explicit server choices.
    [[nodiscard]] bool requestAlbum(const QString& result_key, const QString& root_folder, int quality_profile_id,
                                    int metadata_profile_id);
    void cancel();
    void beginShutdown();
    [[nodiscard]] bool drained() const noexcept;
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] Error error() const noexcept;
    [[nodiscard]] QString serverAddress() const;
    [[nodiscard]] const QVector<ResultItem>& results() const noexcept;
    [[nodiscard]] const QStringList& rootFolders() const noexcept;
    [[nodiscard]] const QVector<Profile>& qualityProfiles() const noexcept;
    [[nodiscard]] const QVector<Profile>& metadataProfiles() const noexcept;

  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0

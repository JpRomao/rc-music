#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace music_player::m0 {
class QtLidarrService;
// Presentation of an optional, explicitly configured Lidarr connection. This
// binding exposes neither API credentials nor arbitrary server JSON to QML.
class LidarrBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool interactionAllowed READ interactionAllowed NOTIFY changed)
    Q_PROPERTY(QString serverAddress READ serverAddress NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QStringList rootFolders READ rootFolders NOTIFY changed)
    Q_PROPERTY(QVariantList qualityProfiles READ qualityProfiles NOTIFY changed)
    Q_PROPERTY(QVariantList metadataProfiles READ metadataProfiles NOTIFY changed)
  public:
    explicit LidarrBinding(QtLidarrService& service, QObject* parent = nullptr);
    [[nodiscard]] bool connected() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool interactionAllowed() const;
    [[nodiscard]] QString serverAddress() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QVariantList results() const;
    [[nodiscard]] QStringList rootFolders() const;
    [[nodiscard]] QVariantList qualityProfiles() const;
    [[nodiscard]] QVariantList metadataProfiles() const;
    Q_INVOKABLE bool connectServer(const QString& address, const QString& api_key);
    Q_INVOKABLE void disconnectServer();
    Q_INVOKABLE bool search(const QString& term, const QString& kind);
    Q_INVOKABLE bool requestDownload(const QString& key, const QString& root, int quality_id, int metadata_id);
    Q_INVOKABLE void cancel();
    [[nodiscard]] bool prepareExit();
    void resume();
    void beginShutdown();
  signals:
    void changed();

  private:
    QtLidarrService& service_;
    bool exit_preparing_{};
    bool shutting_down_{};
};
} // namespace music_player::m0

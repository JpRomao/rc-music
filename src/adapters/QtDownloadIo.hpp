#pragma once

#include "DirectDownloadSession.hpp"

#include <QObject>

namespace music_player::m0 {

// Construct/use/destroy in one worker event-loop thread. Aborts only its own DNS lookup/socket.
class QtDownloadIo final : public QObject, public DirectDownloadIo {
  public:
    explicit QtDownloadIo(QObject* parent = nullptr);
    ~QtDownloadIo() override;
    bool resolve(const QString& host) override;
    DownloadResolution resolution() const override;
    bool open(const PinnedDownloadEndpoint& endpoint) override;
    PinnedDownloadStreamUpdate poll() const override;
    QByteArray read(qint64 maximum) override;
    void cancel() override;

  private:
    PinnedDownloadStream stream_{};
    DownloadResolution resolution_{};
    int lookup_id_{-1};
    quint64 generation_{0};
};

} // namespace music_player::m0

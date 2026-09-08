#include "QtDownloadIo.hpp"

#include <QHostInfo>

namespace music_player::m0 {
QtDownloadIo::QtDownloadIo(QObject* parent) : QObject(parent) {}
QtDownloadIo::~QtDownloadIo() { cancel(); }
bool QtDownloadIo::resolve(const QString& host) {
    cancel();
    if (!parseDownloadNetworkTarget(QStringLiteral("https://") + host + QLatin1Char('/'))) {
        resolution_.state = DownloadResolutionState::Failed;
        return false;
    }
    resolution_ = {};
    const auto generation = generation_;
    lookup_id_ = QHostInfo::lookupHost(host, this, [this, generation](const QHostInfo& info) {
        if (generation != generation_)
            return;
        lookup_id_ = -1;
        if (info.error() != QHostInfo::NoError || info.addresses().size() > 32) {
            resolution_.state = DownloadResolutionState::Failed;
            return;
        }
        resolution_ = {DownloadResolutionState::Resolved, info.addresses()};
    });
    if (lookup_id_ < 0) {
        resolution_.state = DownloadResolutionState::Failed;
        return false;
    }
    return true;
}
DownloadResolution QtDownloadIo::resolution() const { return resolution_; }
bool QtDownloadIo::open(const PinnedDownloadEndpoint& endpoint) { return stream_.open(endpoint); }
PinnedDownloadStreamUpdate QtDownloadIo::poll() const { return stream_.poll(); }
QByteArray QtDownloadIo::read(qint64 maximum) { return stream_.read(maximum); }
void QtDownloadIo::cancel() {
    ++generation_;
    if (lookup_id_ >= 0) {
        QHostInfo::abortHostLookup(lookup_id_);
        lookup_id_ = -1;
    }
    resolution_ = {DownloadResolutionState::Failed, {}};
    stream_.cancel();
}
} // namespace music_player::m0

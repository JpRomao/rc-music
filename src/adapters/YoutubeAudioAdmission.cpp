#include "YoutubeAudioAdmission.hpp"
#include "YoutubeAudioCodec.hpp"

#include <QRegularExpression>
#include <QScopedValueRollback>

namespace music_player::m0 {
YoutubeAudioAdmission::YoutubeAudioAdmission(AsyncDownloadSubmission& destination) : destination_(destination) {}
DownloadAdmissionResult YoutubeAudioAdmission::begin(const YoutubeAudioRequest& request) {
    if (operating_)
        return {.result = {.error = DownloadSubmissionError::Busy}};
    const QScopedValueRollback guard(operating_, true);
    const auto payload = encodeYoutubeAudioRequest(request);
    if (payload.isEmpty())
        return {.result = {.error = DownloadSubmissionError::InvalidRequest}};
    DownloadRequest mapped{.request_id = request.request_id};
    mapped.items.reserve(request.items.size());
    for (const auto& selected : request.items)
        mapped.items.push_back({.url = canonicalYoutubeWatchUrl(selected.video_id).toStdString(),
                                .title = selected.title,
                                .kind = DownloadMediaKind::Audio,
                                .youtube = selected});
    // Exceptions may follow admission. Let the observing session close without
    // fabricating an acknowledgement or retrying this owned request.
    return destination_.begin(mapped);
}
std::optional<DownloadSubmissionResult> YoutubeAudioAdmission::poll(std::string_view request_id) {
    if (operating_ || request_id.size() != 36)
        return {};
    static const QRegularExpression uuid(
        QStringLiteral("\\A[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-4[0-9a-fA-F]{3}-[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}\\z"));
    if (!uuid.match(QString::fromLatin1(request_id.data(), static_cast<qsizetype>(request_id.size()))).hasMatch())
        return {};
    const QScopedValueRollback guard(operating_, true);
    return destination_.poll(request_id);
}
} // namespace music_player::m0

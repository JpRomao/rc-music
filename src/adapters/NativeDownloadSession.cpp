#include "NativeDownloadSession.hpp"
#include "NativeDownloadCodec.hpp"

#include <QRegularExpression>

namespace music_player::m0 {

NativeDownloadSession::NativeDownloadSession(QString caller_origin, QString allowed_origin,
                                             DownloadSubmission& submission, qint64 start_ms)
    : submission_(submission), frame_(start_ms) {
    static const QRegularExpression origin_pattern(QStringLiteral("\\Achrome-extension://[a-p]{32}/\\z"));
    authorized_ = origin_pattern.match(allowed_origin).hasMatch() && caller_origin == allowed_origin;
}

NativeSessionResult NativeDownloadSession::receive(QByteArrayView bytes, qint64 now_ms) {
    if (!authorized_ || terminal_) {
        terminal_ = true;
        return {NativeSessionStatus::Closed};
    }
    const auto frame = frame_.receive(bytes, now_ms);
    if (frame.status == NativeFrameStatus::Pending)
        return {};
    terminal_ = true;
    if (frame.status == NativeFrameStatus::Expired)
        return {NativeSessionStatus::Expired};
    if (frame.status != NativeFrameStatus::Complete)
        return {NativeSessionStatus::Closed};

    const auto decoded = decodeNativeDownloadRequest(frame.payload);
    DownloadSubmissionResult result{.error = decoded.error};
    if (decoded.error == DownloadSubmissionError::None) {
        try {
            result = submission_.submit(decoded.request);
        } catch (...) {
            // Admission may have happened: never fabricate an ACK or retry the batch here.
            return {NativeSessionStatus::Closed};
        }
        if (result.error == DownloadSubmissionError::None && result.job_ids.size() != decoded.request.items.size())
            return {NativeSessionStatus::Closed};
    }
    const auto payload = encodeNativeDownloadResponse(decoded.request.request_id, result);
    const auto response = encodeNativeMessageFrame(payload);
    if (response.isEmpty())
        return {NativeSessionStatus::Closed};
    return {NativeSessionStatus::Replied, response};
}

NativeSessionResult NativeDownloadSession::poll(qint64 now_ms) {
    if (!authorized_ || terminal_) {
        terminal_ = true;
        return {NativeSessionStatus::Closed};
    }
    const auto frame = frame_.poll(now_ms);
    if (frame.status == NativeFrameStatus::Pending)
        return {};
    terminal_ = true;
    return {frame.status == NativeFrameStatus::Expired ? NativeSessionStatus::Expired : NativeSessionStatus::Closed};
}

NativeSessionResult NativeDownloadSession::finishInput(qint64 now_ms) {
    const auto result = poll(now_ms);
    terminal_ = true;
    return {result.status == NativeSessionStatus::Expired ? NativeSessionStatus::Expired : NativeSessionStatus::Closed};
}

} // namespace music_player::m0

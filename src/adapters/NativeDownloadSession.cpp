#include "NativeDownloadSession.hpp"
#include "NativeDownloadCodec.hpp"

#include <QRegularExpression>

#include <utility>

namespace music_player::m0 {
namespace {

constexpr quint64 admission_wait_ms = 5000;

NativeSessionResult reply(const std::string& request_id, const DownloadSubmissionResult& result,
                          const std::size_t expected_item_count) {
    if (result.error == DownloadSubmissionError::None && result.job_ids.size() != expected_item_count)
        return {NativeSessionStatus::Closed};
    const auto payload = encodeNativeDownloadResponse(request_id, result);
    const auto response = encodeNativeMessageFrame(payload);
    if (response.isEmpty())
        return {NativeSessionStatus::Closed};
    return {NativeSessionStatus::Replied, response};
}

} // namespace

NativeDownloadSession::NativeDownloadSession(QString caller_origin, QString allowed_origin,
                                             DownloadSubmission& submission, qint64 start_ms)
    : submission_(&submission), frame_(start_ms) {
    static const QRegularExpression origin_pattern(QStringLiteral("\\Achrome-extension://[a-p]{32}/\\z"));
    authorized_ = origin_pattern.match(allowed_origin).hasMatch() && caller_origin == allowed_origin;
}

NativeDownloadSession::NativeDownloadSession(QString caller_origin, QString allowed_origin,
                                             AsyncDownloadSubmission& submission, qint64 start_ms)
    : async_submission_(&submission), frame_(start_ms) {
    static const QRegularExpression origin_pattern(QStringLiteral("\\Achrome-extension://[a-p]{32}/\\z"));
    authorized_ = origin_pattern.match(allowed_origin).hasMatch() && caller_origin == allowed_origin;
}

NativeSessionResult NativeDownloadSession::receive(QByteArrayView bytes, qint64 now_ms) {
    if (!authorized_ || terminal_) {
        terminal_ = true;
        return {NativeSessionStatus::Closed};
    }
    if (waiting_) {
        // Frame assembly is over. Only polling may observe the admission; new
        // input cannot dispatch a second request or keep this observer alive.
        terminal_ = true;
        waiting_ = false;
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
            if (async_submission_ != nullptr) {
                request_id_ = decoded.request.request_id;
                expected_item_count_ = decoded.request.items.size();
                auto admission = async_submission_->begin(decoded.request);
                switch (admission.state) {
                case DownloadAdmissionState::Pending:
                    if (admission.result.error != DownloadSubmissionError::NotAvailable ||
                        !admission.result.job_ids.empty())
                        return {NativeSessionStatus::Closed};
                    wait_started_ms_ = now_ms;
                    wait_last_ms_ = now_ms;
                    waiting_ = true;
                    terminal_ = false;
                    return {};
                case DownloadAdmissionState::Completed:
                    result = std::move(admission.result);
                    break;
                default:
                    return {NativeSessionStatus::Closed};
                }
            } else {
                result = submission_->submit(decoded.request);
            }
        } catch (...) {
            // Admission may have happened: never fabricate an ACK or retry the batch here.
            return {NativeSessionStatus::Closed};
        }
    }
    return reply(decoded.request.request_id, result, decoded.request.items.size());
}

NativeSessionResult NativeDownloadSession::poll(qint64 now_ms) {
    if (!authorized_ || terminal_) {
        terminal_ = true;
        return {NativeSessionStatus::Closed};
    }
    if (waiting_) {
        // The temporary terminal guard also protects the outward poll against
        // reentrancy. A legitimate Pending completion reopens only this wait.
        terminal_ = true;
        const auto timing = waitingTiming(now_ms);
        if (timing != NativeSessionStatus::Awaiting) {
            waiting_ = false;
            return {timing};
        }
        try {
            const auto completion = async_submission_->poll(request_id_);
            if (!completion) {
                terminal_ = false;
                return {};
            }
            waiting_ = false;
            return reply(request_id_, *completion, expected_item_count_);
        } catch (...) {
            // The owner remains independent; neither uncertainty nor a closed
            // observer authorizes cancellation, retransmission or a wire ACK.
            waiting_ = false;
            return {NativeSessionStatus::Closed};
        }
    }
    const auto frame = frame_.poll(now_ms);
    if (frame.status == NativeFrameStatus::Pending)
        return {};
    terminal_ = true;
    return {frame.status == NativeFrameStatus::Expired ? NativeSessionStatus::Expired : NativeSessionStatus::Closed};
}

NativeSessionResult NativeDownloadSession::finishInput(qint64 now_ms) {
    if (!authorized_ || terminal_) {
        terminal_ = true;
        return {NativeSessionStatus::Closed};
    }
    if (waiting_) {
        terminal_ = true;
        waiting_ = false;
        const auto timing = waitingTiming(now_ms);
        // EOF closes the observer without consuming the owner's completion.
        return {timing == NativeSessionStatus::Expired ? NativeSessionStatus::Expired : NativeSessionStatus::Closed};
    }
    const auto result = poll(now_ms);
    terminal_ = true;
    return {result.status == NativeSessionStatus::Expired ? NativeSessionStatus::Expired : NativeSessionStatus::Closed};
}

NativeSessionStatus NativeDownloadSession::waitingTiming(const qint64 now_ms) {
    if (now_ms < wait_last_ms_)
        return NativeSessionStatus::Closed;
    wait_last_ms_ = now_ms;
    // Monotonic ordering makes unsigned subtraction exact across signed limits.
    const auto elapsed = static_cast<quint64>(now_ms) - static_cast<quint64>(wait_started_ms_);
    return elapsed >= admission_wait_ms ? NativeSessionStatus::Expired : NativeSessionStatus::Awaiting;
}

} // namespace music_player::m0

#include "NativeYoutubeAudioSession.hpp"
#include "YoutubeAudioCodec.hpp"

#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>

namespace music_player::m0 {
namespace {
constexpr quint64 admission_wait_ms = 5000;

bool authorizedOrigin(const QString& caller, const QString& allowed) {
    static const QRegularExpression pattern(QStringLiteral("\\Achrome-extension://[a-p]{32}/\\z"));
    return pattern.match(allowed).hasMatch() && caller == allowed;
}
} // namespace

struct NativeYoutubeAudioSession::Impl final {
    enum class Phase { Assembling, Admitting, Waiting, Polling, Terminal };

    AsyncYoutubeAudioSubmission& submission;
    NativeMessageFrame frame;
    const bool authorized;
    Phase phase{Phase::Assembling};
    std::array<char, 4> prefix{};
    qsizetype prefix_size{};
    qsizetype received_bytes{};
    std::string request_id;
    std::size_t expected_item_count{};
    qint64 wait_started_ms{};
    qint64 wait_last_ms{};

    Impl(const QString& caller, const QString& allowed, AsyncYoutubeAudioSubmission& source, const qint64 start_ms)
        : submission(source), frame(start_ms), authorized(authorizedOrigin(caller, allowed)) {}

    NativeSessionResult close(const NativeSessionStatus status = NativeSessionStatus::Closed) {
        phase = Phase::Terminal;
        return {status};
    }

    NativeSessionStatus waitingTiming(const qint64 now_ms) {
        if (now_ms < wait_last_ms)
            return NativeSessionStatus::Closed;
        wait_last_ms = now_ms;
        // Ordering makes unsigned subtraction exact across the signed clock limits.
        const auto elapsed = static_cast<quint64>(now_ms) - static_cast<quint64>(wait_started_ms);
        return elapsed >= admission_wait_ms ? NativeSessionStatus::Expired : NativeSessionStatus::Awaiting;
    }

    NativeSessionResult reply(const DownloadSubmissionResult& result) {
        phase = Phase::Terminal;
        const auto payload = encodeYoutubeAudioResponse(request_id, result);
        if (payload.isEmpty() || !decodeYoutubeAudioResponse(payload, request_id, expected_item_count))
            return close();
        const auto response = encodeNativeMessageFrame(payload);
        if (response.isEmpty())
            return close();
        return {NativeSessionStatus::Replied, response};
    }

    bool boundedInput(const QByteArrayView bytes) {
        constexpr auto maximum_frame_bytes = static_cast<qsizetype>(youtube_audio_max_payload_bytes) + 4;
        if (bytes.size() > maximum_frame_bytes - received_bytes)
            return false;
        if (prefix_size < 4) {
            const auto count = std::min(qsizetype{4} - prefix_size, bytes.size());
            if (count > 0)
                std::memcpy(prefix.data() + prefix_size, bytes.data(), static_cast<std::size_t>(count));
            prefix_size += count;
            if (prefix_size == 4) {
                const auto length = qFromLittleEndian<quint32>(prefix.data());
                if (length == 0 || length > youtube_audio_max_payload_bytes)
                    return false;
            }
        }
        received_bytes += bytes.size();
        return true;
    }

    NativeSessionResult receive(const QByteArrayView bytes, const qint64 now_ms) {
        if (!authorized || phase != Phase::Assembling)
            return close();
        const auto timing = frame.poll(now_ms);
        if (timing.status != NativeFrameStatus::Pending)
            return close(timing.status == NativeFrameStatus::Expired ? NativeSessionStatus::Expired
                                                                     : NativeSessionStatus::Closed);
        // Inspect the small prefix before the generic framer can reserve a body
        // larger than the YouTube protocol's own 64KiB limit.
        if (!boundedInput(bytes))
            return close();
        const auto incoming = frame.receive(bytes, now_ms);
        if (incoming.status == NativeFrameStatus::Pending)
            return {};
        if (incoming.status != NativeFrameStatus::Complete)
            return close(incoming.status == NativeFrameStatus::Expired ? NativeSessionStatus::Expired
                                                                       : NativeSessionStatus::Closed);
        const auto decoded = decodeYoutubeAudioRequest(incoming.payload);
        if (decoded.error != DownloadSubmissionError::None)
            return close();
        request_id = decoded.request.request_id;
        expected_item_count = decoded.request.items.size();
        phase = Phase::Admitting;
        try {
            const auto admission = submission.begin(decoded.request);
            // A nested receive/poll/EOF irreversibly closes this observer. The
            // external owner's result cannot reopen it or cause another begin.
            if (phase != Phase::Admitting)
                return close();
            switch (admission.state) {
            case DownloadAdmissionState::Pending:
                if (admission.result.error != DownloadSubmissionError::NotAvailable ||
                    !admission.result.job_ids.empty())
                    return close();
                wait_started_ms = now_ms;
                wait_last_ms = now_ms;
                phase = Phase::Waiting;
                return {};
            case DownloadAdmissionState::Completed:
                return reply(admission.result);
            default:
                return close();
            }
        } catch (...) {
            // Admission can already be owned. No retry, cancellation or ACK is
            // inferred from an exception or its potentially private message.
            return close();
        }
    }

    NativeSessionResult poll(const qint64 now_ms) {
        if (!authorized)
            return close();
        if (phase == Phase::Assembling) {
            const auto timing = frame.poll(now_ms);
            if (timing.status == NativeFrameStatus::Pending)
                return {};
            return close(timing.status == NativeFrameStatus::Expired ? NativeSessionStatus::Expired
                                                                     : NativeSessionStatus::Closed);
        }
        if (phase != Phase::Waiting)
            return close();
        const auto timing = waitingTiming(now_ms);
        if (timing != NativeSessionStatus::Awaiting)
            return close(timing);
        phase = Phase::Polling;
        try {
            const auto completion = submission.poll(request_id);
            if (phase != Phase::Polling)
                return close();
            if (!completion) {
                phase = Phase::Waiting;
                return {};
            }
            return reply(*completion);
        } catch (...) {
            return close();
        }
    }

    NativeSessionResult finishInput(const qint64 now_ms) {
        if (!authorized)
            return close();
        if (phase == Phase::Waiting) {
            const auto timing = waitingTiming(now_ms);
            return close(timing == NativeSessionStatus::Expired ? timing : NativeSessionStatus::Closed);
        }
        if (phase == Phase::Assembling) {
            const auto timing = frame.poll(now_ms);
            return close(timing.status == NativeFrameStatus::Expired ? NativeSessionStatus::Expired
                                                                     : NativeSessionStatus::Closed);
        }
        return close();
    }
};

NativeYoutubeAudioSession::NativeYoutubeAudioSession(QString caller_origin, QString allowed_origin,
                                                     AsyncYoutubeAudioSubmission& submission, const qint64 start_ms)
    : impl_(std::make_unique<Impl>(caller_origin, allowed_origin, submission, start_ms)) {}
NativeYoutubeAudioSession::~NativeYoutubeAudioSession() = default;
NativeSessionResult NativeYoutubeAudioSession::receive(const QByteArrayView bytes, const qint64 now_ms) {
    return impl_->receive(bytes, now_ms);
}
NativeSessionResult NativeYoutubeAudioSession::poll(const qint64 now_ms) { return impl_->poll(now_ms); }
NativeSessionResult NativeYoutubeAudioSession::finishInput(const qint64 now_ms) { return impl_->finishInput(now_ms); }
} // namespace music_player::m0

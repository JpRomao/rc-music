#pragma once

#include "AppContainerNetworkObserver.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <cstdint>
#include <optional>

namespace music_player::sandbox_probe {
// Pure transport for the one-shot, read-only observer. Files, pins, tokens and
// authority remain the responsibility of the native probe, never this codec.
class AppContainerObserverProtocol final {
  public:
    static constexpr qsizetype maximum_frame_size = 16 * 1024;
    struct Request final {
        QString nonce{};
        QByteArray driver_user_sid{};
        std::uint32_t driver_pid{};
        std::uint64_t driver_creation_filetime{};
        QByteArray probe_sha256{};
        QString helper_path{};
        QByteArray helper_sha256{}, package_sid{};
        std::uint16_t loopback_tcp_port{};
    };
    struct Ready final {
        QString nonce{};
        QByteArray request_sha256{};
        std::uint32_t observer_pid{};
        std::uint64_t observer_creation_filetime{};
        std::uint64_t window_begin_filetime{}, window_end_filetime{};
    };
    enum class ResultStatus { AdmissionRejected, ObserverResult };
    struct Result final {
        QString nonce{};
        QByteArray request_sha256{};
        std::uint32_t observer_pid{};
        std::uint64_t observer_creation_filetime{};
        ResultStatus status{ResultStatus::AdmissionRejected};
        std::optional<Ready> ready{};
        std::optional<AppContainerNetworkObserver::Report> report{};
    };

    [[nodiscard]] static QByteArray encodeRequest(const Request& value);
    [[nodiscard]] static std::optional<Request> decodeRequest(QByteArrayView bytes);
    [[nodiscard]] static QByteArray encodeReady(const Ready& value);
    [[nodiscard]] static std::optional<Ready> decodeReady(QByteArrayView bytes);
    [[nodiscard]] static QByteArray encodeResult(const Result& value);
    [[nodiscard]] static std::optional<Result> decodeResult(QByteArrayView bytes);
    // SHA-256 of the exact request bytes, including otherwise legal whitespace.
    [[nodiscard]] static QByteArray requestSha256(QByteArrayView bytes);
    [[nodiscard]] static bool matchesRequest(const Request& request, QByteArrayView request_sha256, const Ready& ready);
    [[nodiscard]] static bool matchesRequest(const Request& request, QByteArrayView request_sha256,
                                             const Result& result);
};
} // namespace music_player::sandbox_probe

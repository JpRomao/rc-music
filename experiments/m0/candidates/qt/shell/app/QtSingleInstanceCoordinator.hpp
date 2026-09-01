#pragma once

#include "OpenSourcesCommand.hpp"

#include <QObject>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>

namespace music_player::m0 {

struct QtSingleInstanceOptions final {
    QString lock_file_path{};
    QString server_name_prefix{};
    qsizetype max_bootstrap_commands{16};
    qsizetype max_bootstrap_payload_bytes{4 * open_sources_max_payload_bytes};
    std::chrono::milliseconds secondary_retry_timeout{1'500};
    std::chrono::milliseconds secondary_retry_interval{25};
    std::chrono::milliseconds frame_timeout{2'000};
};

enum class QtSingleInstanceOutcome {
    Primary,
    Forwarded,
    Rejected,
    TimedOut,
    Failed,
};

struct QtSingleInstanceStartResult final {
    QtSingleInstanceOutcome outcome{QtSingleInstanceOutcome::Failed};
    QString detail{};
};

class QtSingleInstanceCoordinator final : public QObject {
    Q_OBJECT

  public:
    explicit QtSingleInstanceCoordinator(QtSingleInstanceOptions options, QObject* parent = nullptr);
    ~QtSingleInstanceCoordinator() override;

    QtSingleInstanceCoordinator(const QtSingleInstanceCoordinator&) = delete;
    QtSingleInstanceCoordinator& operator=(const QtSingleInstanceCoordinator&) = delete;

    [[nodiscard]] QtSingleInstanceStartResult start(const OpenSourcesCommand& initial_command);
    [[nodiscard]] bool isPrimary() const noexcept;
    [[nodiscard]] bool isAccepting() const noexcept;
    [[nodiscard]] QString serverName() const;
    [[nodiscard]] qsizetype pendingCommandCount() const noexcept;
    [[nodiscard]] qsizetype pendingPayloadBytes() const noexcept;
    [[nodiscard]] std::optional<OpenSourcesCommand> takeNextCommand();

    void beginShutdown();

  signals:
    void commandAvailable();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0

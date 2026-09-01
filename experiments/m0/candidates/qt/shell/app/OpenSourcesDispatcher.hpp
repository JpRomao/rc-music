#pragma once

#include "OpenSourcesCommand.hpp"

#include <QStringList>

#include <cstddef>
#include <deque>
#include <functional>

namespace music_player::m0 {

class OpenSourcesDispatcher final {
  public:
    struct Limits final {
        std::size_t max_pending_commands{32};
        std::size_t max_pending_paths{4'000};
        qsizetype max_pending_payload_bytes{4 * open_sources_max_payload_bytes};
    };

    using Activate = std::function<bool()>;
    using ImporterBusy = std::function<bool()>;
    using StartImport = std::function<bool(const QStringList&)>;

    OpenSourcesDispatcher(Activate activate, ImporterBusy importer_busy, StartImport start_import, Limits limits = {});

    [[nodiscard]] bool enqueue(OpenSourcesCommand command);
    void importerStateChanged();
    void stopAccepting() noexcept;

    [[nodiscard]] bool accepting() const noexcept;
    [[nodiscard]] std::size_t pendingCommandCount() const noexcept;

  private:
    struct PendingCommand final {
        OpenSourcesCommand command{};
        qsizetype payload_bytes{0};
    };

    void drain();
    [[nodiscard]] bool hasCapacityFor(const OpenSourcesCommand& command, qsizetype payload_bytes) const noexcept;

    Activate activate_;
    ImporterBusy importer_busy_;
    StartImport start_import_;
    Limits limits_;
    bool accepting_{true};
    std::size_t pending_path_count_{0};
    qsizetype pending_payload_bytes_{0};
    std::deque<PendingCommand> pending_{};
};

} // namespace music_player::m0

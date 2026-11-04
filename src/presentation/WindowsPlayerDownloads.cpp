#include "presentation/WindowsPlayerDownloads.hpp"

#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/WindowsDownloadExecution.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "presentation/PlayerDownloadImport.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QThread>
#include <QUuid>

#include <stdexcept>
#include <utility>

namespace music_player::m0 {
struct WindowsPlayerDownloads::Impl final {
    std::unique_ptr<WindowsDownloadExecution> execution;
    QtDownloadRequestIdentity identity;
    PlayerDownloadImport importer;
    SingleWorkerDownloadCheckpointIo checkpoint;
    DownloadService service;
    DownloadAdmissionController controller;
    DownloadApplicationBinding view;

    Impl(PlayerPresenter& presenter, const SessionLoadResult& session,
         std::unique_ptr<WindowsDownloadExecution> owned_execution,
         SingleWorkerDownloadCheckpointIo::Factory checkpoint_store, WindowsPlayerDownloadsOptions options)
        : execution(std::move(owned_execution)), importer(presenter, {}, PlayerDownloadImportPersistence::Session),
          checkpoint(std::move(checkpoint_store)),
          service(*execution, importer,
                  options.job_namespace.empty() ? QUuid::createUuid().toString(QUuid::Id128).toStdString()
                                                : std::move(options.job_namespace),
                  {}, &identity),
          controller(service, checkpoint, session, DownloadFinalSavePolicy::HoldForDecision),
          view(presenter, controller, service,
               {.shutdown = [this] { execution->shutdown(); }, .stopped = [this] { return execution->stopped(); }},
               std::move(options.binding)) {}
};

WindowsPlayerDownloads::WindowsPlayerDownloads(PlayerPresenter& presenter, const SessionLoadResult& session,
                                               std::unique_ptr<WindowsDownloadExecution> execution,
                                               SingleWorkerDownloadCheckpointIo::Factory checkpoint_store,
                                               WindowsPlayerDownloadsOptions options) {
    if (!execution || !checkpoint_store || QThread::currentThread() != presenter.thread())
        throw std::invalid_argument{
            "player downloads requires same-thread presenter, execution and checkpoint factory"};
    impl_ = std::make_unique<Impl>(presenter, session, std::move(execution), std::move(checkpoint_store),
                                   std::move(options));
}
WindowsPlayerDownloads::~WindowsPlayerDownloads() = default;
bool WindowsPlayerDownloads::start() { return impl_->view.start(); }
DownloadApplicationBinding* WindowsPlayerDownloads::binding() const { return &impl_->view; }
AsyncDownloadSubmission& WindowsPlayerDownloads::submission() { return impl_->controller; }
void WindowsPlayerDownloads::poll() { impl_->view.poll(); }
bool WindowsPlayerDownloads::stopped() const {
    return impl_->controller.stopped() && impl_->checkpoint.stopped() && impl_->execution->stopped();
}
} // namespace music_player::m0

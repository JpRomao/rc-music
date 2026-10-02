#include "DownloadsShellSmoke.hpp"

#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"
#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "m0/presentation/LocalQueueListModel.hpp"
#include "presentation/DownloadApplicationBinding.hpp"
#include "presentation/PlayerDownloadImport.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTimer>
#include <QUuid>

#include <cstdio>
#include <stdexcept>

namespace music_player::m0 {
namespace {
void marker(const char* text) {
    std::fputs(text, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

class FixturePublication final : public DownloadExecution {
  public:
    explicit FixturePublication(const QString& file) : path_(file.toStdWString()) {}
    bool start(const std::string& id, const DownloadItem&) override {
        if (closed_)
            return false;
        ++starts;
        result_ = {.job_id = id,
                   .state = DownloadExecutionState::Published,
                   .phase = DownloadPhase::Publishing,
                   .completed_units = 1,
                   .total_units = 1,
                   .published_mp3 = path_};
        return true;
    }
    void cancel(std::string_view id) override {
        if (result_.job_id == id)
            result_ = {.job_id = std::string{id}, .state = DownloadExecutionState::Cancelled};
    }
    DownloadExecutionUpdate poll(std::string_view) override { return result_; }
    void shutdown() { closed_ = true; }
    bool stopped() const { return closed_; }
    int starts{0};

  private:
    std::filesystem::path path_;
    DownloadExecutionUpdate result_{};
    bool closed_{false};
};
} // namespace

struct DownloadsShellSmoke::Impl final {
    PlayerPresenter& presenter;
    PlaybackBackend& backend;
    bool restart;
    FixturePublication execution;
    QtDownloadRequestIdentity identity;
    PlayerDownloadImport importer;
    SingleWorkerDownloadCheckpointIo checkpoint_io;
    DownloadService service;
    DownloadAdmissionController controller;
    DownloadApplicationBinding view;
    QTimer timer;
    QElapsedTimer watchdog;
    QObject* root{nullptr};
    enum class Phase { Startup, Admission, Import, Playback, Exit };
    Phase phase{Phase::Startup};
    bool exit_requested{false};

    Impl(PlayerPresenter& owner, PlaybackBackend& audio, const SessionLoadResult& session, const QString& config)
        : presenter(owner), backend(audio), restart(!session.snapshot.download_import_receipts.empty()),
          execution(QDir{config}.filePath(QStringLiteral("synthetic-input.mp3"))),
          importer(owner, {}, PlayerDownloadImportPersistence::Session),
          checkpoint_io([directory = QDir{config}.filePath(QStringLiteral("downloads"))] {
              return std::make_unique<WindowsDownloadCheckpointStore>(directory);
          }),
          service(execution, importer, QUuid::createUuid().toString(QUuid::Id128).toStdString(), {}, &identity),
          controller(service, checkpoint_io, session, DownloadFinalSavePolicy::HoldForDecision),
          view(owner, controller, service,
               {.shutdown = [this] { execution.shutdown(); }, .stopped = [this] { return execution.stopped(); }}) {
        // Both runs use disabled restore: a persisted receipt must not become
        // playback or hydration authority in the restarted app.
        if ((presenter.restoreLastSession() && !presenter.setRestoreLastSession(false)) || !view.start())
            throw std::runtime_error{"isolated download composition could not start"};
        timer.setInterval(25);
        QObject::connect(&timer, &QTimer::timeout, &timer, [this] { tick(); });
    }

    void fail() {
        switch (phase) {
        case Phase::Startup:
            marker("M0_DOWNLOAD_COMPOSITION_FAILED startup");
            break;
        case Phase::Admission:
            marker("M0_DOWNLOAD_COMPOSITION_FAILED admission");
            break;
        case Phase::Import:
            marker("M0_DOWNLOAD_COMPOSITION_FAILED import");
            break;
        case Phase::Playback:
            marker("M0_DOWNLOAD_COMPOSITION_FAILED playback");
            break;
        case Phase::Exit:
            marker("M0_DOWNLOAD_COMPOSITION_FAILED exit");
            break;
        }
        timer.stop();
        controller.shutdown();
        execution.shutdown();
        QCoreApplication::exit(2);
    }
    bool noPlayback() const { return !presenter.hasCurrentTrack() && backend.snapshot().generation == 0; }
    void requestExit() {
        phase = Phase::Exit;
        exit_requested = presenter.requestExit();
        if (!exit_requested)
            fail();
    }
    void tick() {
        if (watchdog.elapsed() > 10'000) {
            fail();
            return;
        }
        if (phase == Phase::Startup) {
            if (controller.state() == DownloadAdmissionControllerState::Blocked) {
                fail();
                return;
            }
            if (controller.state() != DownloadAdmissionControllerState::Ready)
                return;
            if (!root || root->property("downloads").value<QObject*>() != &view || !noPlayback() ||
                presenter.queueModelObject()->rowCount() != 0) {
                fail();
                return;
            }
            if (restart) {
                const auto jobs = service.snapshot();
                if (jobs.size() != 1 || jobs.front().state != DownloadJobState::Imported || execution.starts != 0 ||
                    view.jobsModel()->rowCount() != 1) {
                    fail();
                    return;
                }
                marker("M0_DOWNLOAD_RESTART_NO_DUPLICATES");
                requestExit();
                return;
            }
            const DownloadRequest request{
                .request_id = "11111111-1111-4111-8111-111111111111",
                .items = {{"https://fixture.invalid/synthetic.mp3", "Áudio sintético", DownloadMediaKind::Audio}}};
            if (controller.begin(request).state != DownloadAdmissionState::Pending || execution.starts != 0) {
                fail();
                return;
            }
            phase = Phase::Admission;
        } else if (phase == Phase::Admission) {
            const auto acknowledgement = controller.poll("11111111-1111-4111-8111-111111111111");
            if (!acknowledgement)
                return;
            if (acknowledgement->error != DownloadSubmissionError::None || acknowledgement->job_ids.size() != 1) {
                fail();
                return;
            }
            phase = Phase::Import;
        } else if (phase == Phase::Import) {
            const auto jobs = service.snapshot();
            if (jobs.size() != 1 || jobs.front().state == DownloadJobState::ImportFailed ||
                jobs.front().state == DownloadJobState::Failed) {
                fail();
                return;
            }
            if (jobs.front().state != DownloadJobState::Completed)
                return;
            if (!noPlayback() || presenter.queueModelObject()->rowCount() != 1 || execution.starts != 1 ||
                view.jobsModel()->rowCount() != 1) {
                fail();
                return;
            }
            marker("M0_DOWNLOAD_IMPORTED_WITHOUT_AUTOPLAY");
            const auto occurrence =
                presenter.queueModelObject()
                    ->data(presenter.queueModelObject()->index(0, 0), LocalQueueListModel::OccurrenceIdRole)
                    .toString();
            if (!presenter.playQueueOccurrence(occurrence)) {
                fail();
                return;
            }
            phase = Phase::Playback;
        } else if (phase == Phase::Playback) {
            if (presenter.playbackState() == PlayerPresenter::PlaybackState::Error) {
                fail();
                return;
            }
            if (presenter.playbackState() != PlayerPresenter::PlaybackState::Playing)
                return;
            if (backend.snapshot().generation == 0 || !presenter.pause()) {
                fail();
                return;
            }
            marker("M0_DOWNLOAD_EXPLICIT_PLAYBACK");
            requestExit();
        }
    }
};

DownloadsShellSmoke::DownloadsShellSmoke(PlayerPresenter& presenter, PlaybackBackend& backend,
                                         const SessionLoadResult& session, const QString& config_root)
    : impl_(std::make_unique<Impl>(presenter, backend, session, config_root)) {}
DownloadsShellSmoke::~DownloadsShellSmoke() = default;
DownloadApplicationBinding* DownloadsShellSmoke::binding() const { return &impl_->view; }
void DownloadsShellSmoke::bindWindow(QObject& root) {
    impl_->root = &root;
    impl_->watchdog.start();
    impl_->timer.start();
}
bool DownloadsShellSmoke::completed() const {
    const bool ready = impl_->exit_requested && impl_->presenter.exitCommitted() && impl_->controller.stopped() &&
                       impl_->checkpoint_io.stopped() && impl_->execution.stopped() &&
                       impl_->controller.saveStatus() == DownloadCheckpointSaveStatus::Saved;
    if (ready)
        marker("M0_DOWNLOAD_SAFE_EXIT");
    return ready;
}
} // namespace music_player::m0

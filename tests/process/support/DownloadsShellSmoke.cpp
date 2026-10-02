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
#ifdef RC_MUSIC_NATIVE_DOWNLOAD_SMOKE_AVAILABLE
#include "NativeHostBuildConfiguration.hpp"
#include "adapters/QtNativeDownloadServer.hpp"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QTimer>
#include <QUuid>

#include <cstdio>
#include <functional>
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
    explicit FixturePublication(const QString& file, std::function<bool()> admitted)
        : path_(file.toStdWString()), admitted_(std::move(admitted)) {}
    bool start(const std::string& id, const DownloadItem&) override {
        if (closed_ || !admitted_())
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
    std::function<bool()> admitted_;
    DownloadExecutionUpdate result_{};
    bool closed_{false};
};
} // namespace

struct DownloadsShellSmoke::Impl final {
    PlayerPresenter& presenter;
    PlaybackBackend& backend;
    bool restart;
    bool native_ingress;
    QString exit_marker;
    FixturePublication execution;
    QtDownloadRequestIdentity identity;
    PlayerDownloadImport importer;
    SingleWorkerDownloadCheckpointIo checkpoint_io;
    DownloadService service;
    DownloadAdmissionController controller;
#ifdef RC_MUSIC_NATIVE_DOWNLOAD_SMOKE_AVAILABLE
    std::unique_ptr<QtNativeDownloadServer> server;
#endif
    DownloadApplicationBinding view;
    QTimer timer;
    QElapsedTimer watchdog;
    QObject* root{nullptr};
    enum class Phase { Startup, Admission, Import, Playback, AwaitDriverExit, Exit };
    Phase phase{Phase::Startup};
    bool exit_requested{false};

    Impl(PlayerPresenter& owner, PlaybackBackend& audio, const SessionLoadResult& session, const QString& config,
         const bool native)
        : presenter(owner), backend(audio), restart(!session.snapshot.download_import_receipts.empty()),
          native_ingress(native), exit_marker(QDir{config}.filePath(QStringLiteral("m0-native-downloads-exit"))),
          execution(QDir{config}.filePath(QStringLiteral("synthetic-input.mp3")),
                    [this] {
                        if (controller.saveStatus() != DownloadCheckpointSaveStatus::Saved)
                            return false;
                        if (native_ingress)
                            marker("M0_NATIVE_DURABLE_BEFORE_EXECUTION");
                        return true;
                    }),
          importer(owner, {}, PlayerDownloadImportPersistence::Session),
          checkpoint_io([directory = QDir{config}.filePath(QStringLiteral("downloads"))] {
              return std::make_unique<WindowsDownloadCheckpointStore>(directory);
          }),
          service(execution, importer, QUuid::createUuid().toString(QUuid::Id128).toStdString(), {}, &identity),
          controller(service, checkpoint_io, session, DownloadFinalSavePolicy::HoldForDecision),
          view(owner, controller, service,
               {.shutdown =
                    [this] {
#ifdef RC_MUSIC_NATIVE_DOWNLOAD_SMOKE_AVAILABLE
                        if (server)
                            server->beginShutdown();
#endif
                        execution.shutdown();
                    },
                .stopped = [this] { return ingressStopped() && execution.stopped(); }}) {
#ifdef RC_MUSIC_NATIVE_DOWNLOAD_SMOKE_AVAILABLE
        if (native_ingress) {
            const QPointer<PlayerPresenter> guard{&owner};
            server = std::make_unique<QtNativeDownloadServer>(
                nativeHostBuildConfiguration(), controller,
                QtNativeDownloadServerOptions{.available = [guard] { return guard && !guard->exiting(); }});
            if (!server->start())
                throw std::runtime_error{"isolated native download ingress could not start"};
        }
#else
        if (native_ingress)
            throw std::runtime_error{"native ingress unavailable in this instrumented build"};
#endif
        // Both runs use disabled restore: a persisted receipt must not become
        // playback or hydration authority in the restarted app.
        if ((presenter.restoreLastSession() && !presenter.setRestoreLastSession(false)) || !view.start())
            throw std::runtime_error{"isolated download composition could not start"};
        timer.setInterval(25);
        QObject::connect(&timer, &QTimer::timeout, &timer, [this] { tick(); });
    }

    bool ingressStopped() const {
#ifdef RC_MUSIC_NATIVE_DOWNLOAD_SMOKE_AVAILABLE
        return !server || server->stopped();
#else
        return true;
#endif
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
        case Phase::AwaitDriverExit:
            marker("M0_DOWNLOAD_COMPOSITION_FAILED exit");
            break;
        }
        timer.stop();
        controller.shutdown();
#ifdef RC_MUSIC_NATIVE_DOWNLOAD_SMOKE_AVAILABLE
        if (server)
            server->beginShutdown();
#endif
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
        if (watchdog.elapsed() > (native_ingress ? 45'000 : 10'000)) {
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
                if (native_ingress) {
                    marker("M0_NATIVE_DOWNLOAD_READY");
                    phase = Phase::AwaitDriverExit;
                } else
                    requestExit();
                return;
            }
            if (native_ingress) {
                phase = Phase::Admission;
                marker("M0_NATIVE_DOWNLOAD_READY");
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
            if (native_ingress)
                phase = Phase::AwaitDriverExit;
            else
                requestExit();
        } else if (phase == Phase::AwaitDriverExit && QFile::exists(exit_marker)) {
            // The process driver creates only this closed test lifecycle file
            // after strict host replies. Nothing extends the production wire.
            requestExit();
        }
    }
};

DownloadsShellSmoke::DownloadsShellSmoke(PlayerPresenter& presenter, PlaybackBackend& backend,
                                         const SessionLoadResult& session, const QString& config_root,
                                         const bool native_ingress)
    : impl_(std::make_unique<Impl>(presenter, backend, session, config_root, native_ingress)) {}
DownloadsShellSmoke::~DownloadsShellSmoke() = default;
DownloadApplicationBinding* DownloadsShellSmoke::binding() const { return &impl_->view; }
void DownloadsShellSmoke::bindWindow(QObject& root) {
    impl_->root = &root;
    impl_->watchdog.start();
    impl_->timer.start();
}
bool DownloadsShellSmoke::completed() const {
    const bool ready = impl_->exit_requested && impl_->presenter.exitCommitted() && impl_->controller.stopped() &&
                       impl_->checkpoint_io.stopped() && impl_->ingressStopped() && impl_->execution.stopped() &&
                       impl_->controller.saveStatus() == DownloadCheckpointSaveStatus::Saved;
    if (ready)
        marker("M0_DOWNLOAD_SAFE_EXIT");
    return ready;
}
} // namespace music_player::m0

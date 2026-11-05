#include "presentation/WindowsPlayerDownloads.hpp"

#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/DownloadLyricsLookup.hpp"
#include "adapters/QtLyricsService.hpp"
#include "adapters/WindowsDownloadExecution.hpp"
#include "adapters/YoutubeAudioAdmission.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "presentation/DownloadLyricsBinding.hpp"
#include "presentation/LyricsBinding.hpp"
#include "presentation/PlayerDownloadImport.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QAbstractItemModel>
#include <QNetworkAccessManager>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace music_player::m0 {
struct WindowsPlayerDownloads::Impl final {
    std::unique_ptr<WindowsDownloadExecution> execution;
    QtDownloadRequestIdentity identity;
    PlayerDownloadImport importer;
    SingleWorkerDownloadCheckpointIo checkpoint;
    DownloadService service;
    DownloadAdmissionController controller;
    YoutubeAudioAdmission youtube;
    LyricsBinding* lyrics_writer{};
    std::unique_ptr<QtLyricsService> lyrics_service;
    std::unique_ptr<DownloadLyricsLookup> lyrics_lookup;
    std::unique_ptr<DownloadLyricsBinding> lyrics_view;
    std::unordered_set<std::string> offered_lyrics;
    bool offering_lyrics{};
    bool lyrics_wake_queued{};
    DownloadApplicationBinding view;
    bool lyricsDrained() const {
        return !lyrics_view ||
               (lyrics_view->drained() && !lyrics_writer->variantsBusy() && !lyrics_writer->variantsHasPendingChange());
    }
    void offerLyrics() {
        if (!lyrics_view || offering_lyrics || lyrics_view->hasRequest())
            return;
        const auto state = controller.state();
        if (state != DownloadAdmissionControllerState::Ready &&
            state != DownloadAdmissionControllerState::AdmissionPending &&
            state != DownloadAdmissionControllerState::AdmissionFailed)
            return;
        const QScopedValueRollback guard(offering_lyrics, true);
        for (const auto& job : service.snapshot()) {
            if (offered_lyrics.contains(job.job_id))
                continue;
            const auto publication = service.publishedYoutubeAudio(job.job_id);
            if (!publication)
                continue;
            // Bound by the common service's 128-job ledger. Reserve before any
            // Qt callback: dismissal/reentry must not silently repeat a lookup.
            offered_lyrics.insert(job.job_id);
            if (!publication->source.fetch_lyrics)
                continue;
            PublishedLyricsRequest request;
            request.job_id = publication->job_id;
            request.final_mp3 = QString::fromStdWString(publication->published_mp3.native());
            request.title = QString::fromStdString(publication->source.title);
            request.artist = QString::fromStdString(publication->source.artist);
            request.lyrics_page = QString::fromStdString(publication->source.lyrics_page);
            static_cast<void>(lyrics_view->offerPublished(std::move(request)));
            break;
        }
    }

    Impl(PlayerPresenter& presenter, const SessionLoadResult& session,
         std::unique_ptr<WindowsDownloadExecution> owned_execution,
         SingleWorkerDownloadCheckpointIo::Factory checkpoint_store, WindowsPlayerDownloadsOptions options)
        : execution(std::move(owned_execution)), importer(presenter, {}, PlayerDownloadImportPersistence::Session),
          checkpoint(std::move(checkpoint_store)),
          service(*execution, importer,
                  options.job_namespace.empty() ? QUuid::createUuid().toString(QUuid::Id128).toStdString()
                                                : std::move(options.job_namespace),
                  {}, &identity),
          controller(service, checkpoint, session, DownloadFinalSavePolicy::HoldForDecision), youtube(controller),
          lyrics_writer(options.lyrics_writer),
          lyrics_service(lyrics_writer ? std::make_unique<QtLyricsService>(options.lyrics_network) : nullptr),
          lyrics_lookup(lyrics_service ? std::make_unique<DownloadLyricsLookup>(*lyrics_service) : nullptr),
          lyrics_view(lyrics_lookup ? std::make_unique<DownloadLyricsBinding>(*lyrics_lookup, *lyrics_writer)
                                    : nullptr),
          view(presenter, controller, service,
               {.shutdown =
                    [this] {
                        if (lyrics_view)
                            lyrics_view->beginShutdown();
                        execution->shutdown();
                    },
                .stopped = [this] { return execution->stopped() && lyricsDrained(); }},
               std::move(options.binding)) {
        view.setLyrics(lyrics_view.get());
        QObject::connect(&view, &DownloadApplicationBinding::stateChanged, &view, [this] { offerLyrics(); });
        QObject::connect(view.jobsModel(), &QAbstractItemModel::dataChanged, &view, [this] { offerLyrics(); });
        QObject::connect(view.jobsModel(), &QAbstractItemModel::rowsInserted, &view, [this] { offerLyrics(); });
        if (lyrics_view)
            QObject::connect(lyrics_view.get(), &DownloadLyricsBinding::changed, &view, [this] {
                if (lyrics_wake_queued)
                    return;
                lyrics_wake_queued = true;
                QTimer::singleShot(0, &view, [this] {
                    lyrics_wake_queued = false;
                    offerLyrics();
                });
            });
    }
};

WindowsPlayerDownloads::WindowsPlayerDownloads(PlayerPresenter& presenter, const SessionLoadResult& session,
                                               std::unique_ptr<WindowsDownloadExecution> execution,
                                               SingleWorkerDownloadCheckpointIo::Factory checkpoint_store,
                                               WindowsPlayerDownloadsOptions options) {
    if (!execution || !checkpoint_store || QThread::currentThread() != presenter.thread() ||
        (options.lyrics_writer && options.lyrics_writer->thread() != presenter.thread()) ||
        (options.lyrics_network && (!options.lyrics_writer || options.lyrics_network->thread() != presenter.thread())))
        throw std::invalid_argument{
            "player downloads requires same-thread presenter, execution and checkpoint factory"};
    impl_ = std::make_unique<Impl>(presenter, session, std::move(execution), std::move(checkpoint_store),
                                   std::move(options));
}
WindowsPlayerDownloads::~WindowsPlayerDownloads() = default;
bool WindowsPlayerDownloads::start() { return impl_->view.start(); }
DownloadApplicationBinding* WindowsPlayerDownloads::binding() const { return &impl_->view; }
AsyncDownloadSubmission& WindowsPlayerDownloads::submission() { return impl_->controller; }
AsyncYoutubeAudioSubmission& WindowsPlayerDownloads::youtubeSubmission() { return impl_->youtube; }
void WindowsPlayerDownloads::poll() {
    impl_->view.poll();
    impl_->offerLyrics();
}
bool WindowsPlayerDownloads::stopped() const {
    return impl_->controller.stopped() && impl_->checkpoint.stopped() && impl_->execution->stopped() &&
           impl_->lyricsDrained();
}
} // namespace music_player::m0

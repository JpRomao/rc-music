#include "presentation/WindowsPlayerDownloads.hpp"

#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/DownloadLyricsLookup.hpp"
#include "adapters/QtLyricsService.hpp"
#include "adapters/WindowsConfiguredDownloadExecution.hpp"
#include "adapters/WindowsDownloadExecution.hpp"
#include "adapters/WindowsDownloadRecoveryReview.hpp"
#include "adapters/WindowsDownloadRecoveryValidation.hpp"
#include "adapters/YoutubeAudioAdmission.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "presentation/DownloadLyricsBinding.hpp"
#include "presentation/LyricsBinding.hpp"
#include "presentation/PlayerDownloadImport.hpp"
#include "presentation/PlayerPresenter.hpp"

#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
#include "adapters/QtNativeDownloadServer.hpp"
#include "adapters/QtNativeYoutubeAudioServer.hpp"
#endif

#include <QAbstractItemModel>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace music_player::m0 {
namespace {
struct PublicationDirectory final {
    QString current() const {
        const std::lock_guard lock{mutex};
        return selected;
    }
    void select(QString directory) {
        const std::lock_guard lock{mutex};
        selected = std::move(directory);
    }

    mutable std::mutex mutex;
    QString selected;
};

bool nativeProfileActive(const NativeHostConfiguration& profile) {
    return !profile.allowed_origin.isEmpty() || !profile.endpoint_name.isEmpty();
}
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
void ownProfileStrings(NativeHostConfiguration& profile) {
    const auto own = [](const QString& value) { return QString{value.constData(), value.size()}; };
    profile.allowed_origin = own(profile.allowed_origin);
    profile.endpoint_name = own(profile.endpoint_name);
    for (auto& image : profile.expected_owner_images)
        image = own(image);
    for (auto& image : profile.expected_host_images)
        image = own(image);
}
#endif
void validateAndOwnIngress(std::optional<WindowsPlayerNativeIngressOptions>& ingress) {
    if (!ingress)
        return;
    const bool principal = nativeProfileActive(ingress->principal);
    const bool youtube = nativeProfileActive(ingress->youtube);
    if (!principal && !youtube)
        return;
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
    for (auto* profile : {&ingress->principal, &ingress->youtube}) {
        if (nativeProfileActive(*profile) &&
            (!isValidNativeHostConfiguration(*profile) || !validNativePeerImages(profile->expected_owner_images) ||
             !validNativePeerImages(profile->expected_host_images)))
            throw std::invalid_argument{"player downloads requires a complete trusted native profile"};
        ownProfileStrings(*profile);
    }
    if (principal && youtube &&
        (ingress->principal.allowed_origin == ingress->youtube.allowed_origin ||
         ingress->principal.endpoint_name.compare(ingress->youtube.endpoint_name, Qt::CaseInsensitive) == 0))
        throw std::invalid_argument{"player download native profiles must use distinct origins and endpoints"};
#else
    throw std::invalid_argument{"player native ingress is not available in this build"};
#endif
}
} // namespace
struct WindowsPlayerDownloads::Impl final : AsyncDownloadSubmission {
    QPointer<PlayerPresenter> presenter;
    bool ingress_ready{}, ingress_closing{};
    const bool embedded_browser_enabled;
    bool owner_started{};
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
    // Reservations outlive every worker's destructor fallback. The destructor
    // closes observers before their borrowed controller/ports start to die.
    std::unique_ptr<QtNativeDownloadServer> principal_ingress;
    std::unique_ptr<QtNativeYoutubeAudioServer> youtube_ingress;
#endif
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
    std::unique_ptr<DownloadRecoveryReview> recovery_review;
    std::unique_ptr<DownloadRecoveryValidation> recovery_validation;
    DownloadApplicationBinding view;
    bool recoveryStopped() const {
        return (!recovery_review || recovery_review->stopped()) &&
               (!recovery_validation || recovery_validation->stopped());
    }
    void closeIngress() {
        ingress_ready = false;
        ingress_closing = true;
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
        if (principal_ingress)
            principal_ingress->beginShutdown();
        if (youtube_ingress)
            youtube_ingress->beginShutdown();
#endif
    }
    bool ingressStopped() const {
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
        return (!principal_ingress || principal_ingress->stopped()) && (!youtube_ingress || youtube_ingress->stopped());
#else
        return true;
#endif
    }
    bool ingressAvailable() const { return ingress_ready && !ingress_closing && presenter && !presenter->exiting(); }
    bool embeddedBrowserAvailable() const {
        return QThread::currentThread() == view.thread() && embedded_browser_enabled && owner_started &&
               !ingress_closing && presenter && !presenter->exiting();
    }
    DownloadAdmissionResult begin(const DownloadRequest& request) override {
        if (!embeddedBrowserAvailable())
            return {};
        if (std::ranges::any_of(request.items, [](const auto& item) { return item.youtube.has_value(); }))
            return {.result = {.error = DownloadSubmissionError::UnsupportedSource}};
        return controller.begin(request);
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view request_id) override {
        if (!embeddedBrowserAvailable())
            return DownloadSubmissionResult{};
        return controller.poll(request_id);
    }
    bool start() {
        if (!view.start())
            return false;
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
        if (principal_ingress || youtube_ingress) {
            if (ingress_closing || !presenter || presenter->exiting() ||
                (principal_ingress && !principal_ingress->start()) || (youtube_ingress && !youtube_ingress->start()) ||
                ingress_closing || !presenter || presenter->exiting()) {
                closeIngress();
                return false;
            }
            // No request may enter a partially started pair. The controller
            // retains its own startup/busy/replay policy after this latch opens.
            ingress_ready = true;
        }
#endif
        owner_started = true;
        return true;
    }
    void pollIngress() {
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
        if (principal_ingress)
            principal_ingress->poll();
        if (youtube_ingress)
            youtube_ingress->poll();
#endif
    }
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
            if (!publication || !publication->source.fetch_lyrics)
                continue;
            // Live jobs with requested lyrics stay in the service ledger.
            // Reserve before Qt callbacks so reentry cannot repeat a lookup.
            offered_lyrics.insert(job.job_id);
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
         SingleWorkerDownloadCheckpointIo::Factory checkpoint_store, WindowsPlayerDownloadsOptions options,
         std::unique_ptr<DownloadRecoveryReview> review, std::unique_ptr<DownloadRecoveryValidation> validation)
        : presenter(&presenter), embedded_browser_enabled(options.embedded_browser_ingress),
          execution(std::move(owned_execution)), importer(presenter, {}, PlayerDownloadImportPersistence::Session),
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
          recovery_review(std::move(review)), recovery_validation(std::move(validation)),
          view(presenter, controller, service,
               {.shutdown =
                    [this] {
                        if (lyrics_view)
                            lyrics_view->beginShutdown();
                        execution->shutdown();
                    },
                .stopped = [this] { return execution->stopped() && lyricsDrained() && ingressStopped(); },
                .before_shutdown = [this] { closeIngress(); }},
               std::move(options.binding)) {
#ifdef RC_MUSIC_PLAYER_NATIVE_INGRESS_AVAILABLE
        if (options.native_ingress) {
            auto& native = *options.native_ingress;
            if (nativeProfileActive(native.principal))
                principal_ingress = std::make_unique<QtNativeDownloadServer>(
                    std::move(native.principal), controller,
                    QtNativeDownloadServerOptions{.available = [this] { return ingressAvailable(); },
                                                  .peer_check_factory =
                                                      std::move(native.principal_peer_check_factory)});
            if (nativeProfileActive(native.youtube))
                youtube_ingress = std::make_unique<QtNativeYoutubeAudioServer>(
                    std::move(native.youtube), youtube,
                    QtNativeYoutubeAudioServerOptions{.available = [this] { return ingressAvailable(); },
                                                      .peer_check_factory =
                                                          std::move(native.youtube_peer_check_factory)});
        }
#endif
        if (lyrics_service)
            lyrics_service->configureLyricFind(std::move(options.lyricfind));
        view.setLyrics(lyrics_view.get());
        QObject::connect(&view, &DownloadApplicationBinding::stateChanged, &view, [this] { offerLyrics(); });
        QObject::connect(view.jobsModel(), &QAbstractItemModel::dataChanged, &view, [this] { offerLyrics(); });
        QObject::connect(view.jobsModel(), &QAbstractItemModel::rowsInserted, &view, [this] { offerLyrics(); });
        if (lyrics_view) {
            QObject::connect(lyrics_view.get(), &DownloadLyricsBinding::reviewFinished, &view,
                             &DownloadApplicationBinding::finishPublishedLyricsReview, Qt::QueuedConnection);
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
    }
    ~Impl() { closeIngress(); }
};

WindowsPlayerDownloads::WindowsPlayerDownloads(PlayerPresenter& presenter, const SessionLoadResult& session,
                                               std::unique_ptr<WindowsDownloadExecution> execution,
                                               SingleWorkerDownloadCheckpointIo::Factory checkpoint_store,
                                               WindowsPlayerDownloadsOptions options)
    : WindowsPlayerDownloads(presenter, session, std::move(execution), std::move(checkpoint_store), std::move(options),
                             {}, {}) {}
WindowsPlayerDownloads::WindowsPlayerDownloads(PlayerPresenter& presenter, const SessionLoadResult& session,
                                               std::unique_ptr<WindowsDownloadExecution> execution,
                                               SingleWorkerDownloadCheckpointIo::Factory checkpoint_store,
                                               WindowsPlayerDownloadsOptions options,
                                               std::unique_ptr<DownloadRecoveryReview> review,
                                               std::unique_ptr<DownloadRecoveryValidation> validation) {
    if (!execution || !checkpoint_store || QThread::currentThread() != presenter.thread() ||
        (options.lyrics_writer && options.lyrics_writer->thread() != presenter.thread()) ||
        (options.lyrics_network && (!options.lyrics_writer || options.lyrics_network->thread() != presenter.thread())))
        throw std::invalid_argument{
            "player downloads requires same-thread presenter, execution and checkpoint factory"};
    validateAndOwnIngress(options.native_ingress);
    if (review)
        options.binding.recovery_review = review.get();
    if (validation)
        options.binding.recovery_validation = validation.get();
    impl_ = std::make_unique<Impl>(presenter, session, std::move(execution), std::move(checkpoint_store),
                                   std::move(options), std::move(review), std::move(validation));
}
WindowsPlayerDownloads::~WindowsPlayerDownloads() = default;
std::unique_ptr<WindowsPlayerDownloads> WindowsPlayerDownloads::createConfigured(
    PlayerPresenter& presenter, const SessionLoadResult& session, WindowsDownloadExecutionProfile profile,
    SingleWorkerDownloadCheckpointIo::Factory checkpoint_store, WindowsPlayerDownloadsOptions options) {
    const auto own = [](const QString& text) { return QString{text.constData(), text.size()}; };
    const auto journal = own(profile.journal_directory);
    auto workspace = profile.job.workspace;
    workspace.staging_parent = own(workspace.staging_parent);
    workspace.publish_parent = own(workspace.publish_parent);
    const auto media = profile.media;
    const auto memory = profile.job.converter.max_process_memory_bytes;
    const auto timeout = profile.job.converter.total_timeout_ms;
    const bool validation_available =
        !profile.provision_media_profile && !media.require_app_container && !media.app_container;
    const auto publication_directory =
        options.configurable_download_directory ? std::make_shared<PublicationDirectory>() : nullptr;
    if (publication_directory)
        profile.publication_directory = [publication_directory] { return publication_directory->current(); };
    auto execution = makeWindowsConfiguredDownloadExecution(std::move(profile));
    if (!execution)
        return {};
    const bool use_owned_recovery = !options.binding.recovery_review && !options.binding.recovery_validation;
    auto review = use_owned_recovery ? std::make_unique<WindowsDownloadRecoveryReview>(journal, workspace) : nullptr;
    auto validation =
        use_owned_recovery && validation_available
            ? std::make_unique<WindowsDownloadRecoveryValidation>(journal, workspace, media, memory, timeout)
            : nullptr;
    auto owner = std::unique_ptr<WindowsPlayerDownloads>{
        new WindowsPlayerDownloads(presenter, session, std::move(execution), std::move(checkpoint_store),
                                   std::move(options), std::move(review), std::move(validation))};
    if (publication_directory) {
        owner->binding()->configureDownloadDirectory(
            workspace.publish_parent,
            [staging = workspace.staging_parent](const QString& directory) {
                return validateWindowsDownloadDirectory(staging, directory);
            },
            [publication_directory](QString directory) { publication_directory->select(std::move(directory)); });
    }
    return owner;
}
bool WindowsPlayerDownloads::start() { return impl_->start(); }
DownloadApplicationBinding* WindowsPlayerDownloads::binding() const { return &impl_->view; }
AsyncDownloadSubmission& WindowsPlayerDownloads::submission() { return impl_->controller; }
AsyncYoutubeAudioSubmission& WindowsPlayerDownloads::youtubeSubmission() { return impl_->youtube; }
AsyncDownloadSubmission* WindowsPlayerDownloads::embeddedBrowserSubmission() const {
    return impl_->embeddedBrowserAvailable() ? impl_.get() : nullptr;
}
void WindowsPlayerDownloads::poll() {
    impl_->view.poll();
    impl_->pollIngress();
    impl_->offerLyrics();
}
bool WindowsPlayerDownloads::stopped() const {
    return impl_->controller.stopped() && impl_->checkpoint.stopped() && impl_->execution->stopped() &&
           impl_->lyricsDrained() && impl_->ingressStopped() && impl_->recoveryStopped();
}
} // namespace music_player::m0

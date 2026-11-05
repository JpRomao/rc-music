#include "adapters/DownloadJobsModel.hpp"
#include "adapters/QtNativeDownloadClient.hpp"
#include "app/PlayerDownloadsBuildProfile.hpp"
#include "presentation/DownloadApplicationBinding.hpp"
#include "presentation/PlayerPresenter.hpp"

#if RC_MUSIC_CONFIGURED_DOWNLOADS_EXPECT_BINDING
#include RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>

#include <cstdio>
#include <memory>

using namespace music_player::m0;
namespace {
int profile_calls{};
constexpr std::string_view request_id = "12345678-1234-4234-8234-123456789012";

void report(const QJsonObject& object) {
    const auto bytes = QJsonDocument{object}.toJson(QJsonDocument::Compact);
    std::fputs("CONFIGURED_DOWNLOADS ", stdout);
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

struct Observation final {
    QString root;
    QPointer<QWindow> window;
    QPointer<PlayerPresenter> presenter;
    QPointer<DownloadApplicationBinding> binding;
    std::unique_ptr<QtNativeDownloadClient> client;
    std::vector<std::string> jobs;
    int acknowledgements{};
    bool inspected{}, finished{}, exit_requested{}, failed{};

    void tick() {
        if (!inspected) {
            for (auto* candidate : QGuiApplication::allWindows()) {
                auto* found = qobject_cast<PlayerPresenter*>(candidate->property("presenter").value<QObject*>());
                if (!found || !candidate->property("startupReady").toBool())
                    continue;
                window = candidate;
                presenter = found;
                binding = qobject_cast<DownloadApplicationBinding*>(candidate->property("downloads").value<QObject*>());
                inspected = true;
                report({{"stage", "ready"},
                        {"profileCalls", profile_calls},
                        {"binding", !binding.isNull()},
                        {"lyrics", binding && binding->lyrics() != nullptr}});
#if RC_MUSIC_CONFIGURED_DOWNLOADS_EXPECT_BINDING
                if (!binding)
#endif
                {
                    finished = true;
                    report({{"stage", "finished"},
                            {"profileCalls", profile_calls},
                            {"acks", 0},
                            {"queue", presenter->queueModelObject()->rowCount()},
                            {"playing", presenter->playbackState() == PlayerPresenter::PlaybackState::Playing}});
                }
                break;
            }
        }
        if (!inspected || !presenter)
            return;
#if RC_MUSIC_CONFIGURED_DOWNLOADS_EXPECT_BINDING
        if (binding && !finished && !failed) {
            try {
                if (!client && acknowledgements < 2 &&
                    binding->statusText() ==
                        QCoreApplication::translate("DownloadApplicationBinding", "Histórico de downloads carregado")) {
                    client = std::make_unique<QtNativeDownloadClient>(configuredDownloadsFixtureNativeProfile());
                    const DownloadRequest request{
                        std::string{request_id},
                        {{"https://media.example.test/synthetic-mp4", "Synthetic AAC", DownloadMediaKind::Video}}};
                    const auto begun = client->begin(request);
                    if (begun.state != DownloadAdmissionState::Pending) {
                        failed = true;
                        report({{"stage", "failure"}, {"cause", "admission-not-pending"}});
                    }
                }
                if (client) {
                    if (const auto result = client->poll(request_id)) {
                        if (result->error != DownloadSubmissionError::None || result->job_ids.size() != 1 ||
                            (!jobs.empty() && jobs != result->job_ids)) {
                            failed = true;
                            report({{"stage", "failure"}, {"cause", "admission-result"}});
                        } else {
                            jobs = result->job_ids;
                            ++acknowledgements;
                            report({{"stage", "ack"}, {"count", acknowledgements}});
                        }
                        client->shutdown();
                    }
                    if (client->stopped())
                        client.reset();
                }
                const bool restarted =
                    QFile::exists(QDir{root}.filePath(QStringLiteral("configured-downloads-restart")));
                const int expected_queue = restarted ? 0 : 1;
                auto* model = binding->jobsModel();
                const auto row = model->index(0, 0);
                const auto job_state = model->data(row, DownloadJobsModel::StateRole).toString();
                const bool committed =
                    model->rowCount() == 1 && jobs.size() == 1 &&
                    model->data(row, DownloadJobsModel::JobIdRole).toString() == QString::fromStdString(jobs.front()) &&
                    job_state == (restarted ? QStringLiteral("imported") : QStringLiteral("completed"));
                if (!client && acknowledgements == 2 && committed &&
                    presenter->queueModelObject()->rowCount() == expected_queue) {
                    finished = true;
                    report({{"stage", "finished"},
                            {"profileCalls", profile_calls},
                            {"acks", acknowledgements},
                            {"queue", presenter->queueModelObject()->rowCount()},
                            {"jobState", job_state},
                            {"playing", presenter->playbackState() == PlayerPresenter::PlaybackState::Playing}});
                }
            } catch (...) {
                failed = true;
                report({{"stage", "failure"}, {"cause", "uncertain-admission"}});
            }
        }
#endif
        if (!exit_requested && QFile::exists(QDir{root}.filePath(QStringLiteral("configured-downloads-exit")))) {
            exit_requested = true;
            if (client)
                client->shutdown();
            static_cast<void>(presenter->requestExit());
        }
    }
};

void observeConfiguredDownloads() {
    auto observation = std::make_shared<Observation>();
    const auto prefix = QStringLiteral("--m0-test-config-root=");
    for (const auto& argument : QCoreApplication::arguments())
        if (argument.startsWith(prefix))
            observation->root = argument.sliced(prefix.size());
    auto* timer = new QTimer{QCoreApplication::instance()};
    timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, timer, [observation] { observation->tick(); });
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, timer, [observation] {
        report({{"stage", "stopped"},
                {"clientStopped", !observation->client || observation->client->stopped()},
                {"downloadStatus", observation->binding ? observation->binding->statusText() : QString{}},
                {"failed", observation->failed}});
    });
    timer->start();
}
} // namespace

void configuredDownloadsProfileObserved() { ++profile_calls; }
Q_COREAPP_STARTUP_FUNCTION(observeConfiguredDownloads)

#include "support/YoutubeRuntimeReceipt.hpp"

#include "adapters/WindowsYoutubeBrokerProcess.hpp"
#include "adapters/YoutubeResolverSession.hpp"
#ifdef RC_MUSIC_YOUTUBE_WORKER_SMOKE
#include "adapters/WindowsYoutubeExecution.hpp"
#endif

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

using namespace music_player::m0;
using namespace music_player::m0::test_support;
namespace {
constexpr qint64 fixed_utc_seconds = 1'900'000'000;
constexpr qint64 native_run_limit_ms = 60'000;
// Reserve the broker destructor's existing one-second conservative drain.
constexpr qint64 native_owner_limit_ms = 74'000;
const QString video_id{QStringLiteral("testVideo01")};
const std::string job_id{"cc51a360-474b-4e41-b97d-88d0632ac6ad"};

struct Options final {
    QString manifest{}, receipt{};
    QByteArray manifest_sha256{};
};
struct Report final {
    bool success{}, process_acknowledged{}, runtime_lease_released{}, temporary_preserved{};
    QString state{QStringLiteral("failed")}, stage{QStringLiteral("arguments")},
        error{QStringLiteral("invalid_arguments")};
    QByteArray manifest_sha256{};
    qsizetype file_count{};
    QJsonObject validated_audit{};
#ifdef RC_MUSIC_YOUTUBE_WORKER_SMOKE
    int preparations{}, handoffs{}, direct_starts{};
    qsizetype lease_path_units{};
    bool prepare_on_worker{}, consumer_on_worker{}, worker_stopped{}, lease_released_at_handoff{};
    QString consumer_terminal{}, consumer_error{};
#endif
    QJsonObject json() const {
        return {{"schemaVersion", 1},
#ifdef RC_MUSIC_YOUTUBE_WORKER_SMOKE
                {"purpose", QStringLiteral("youtube-worker-offline-smoke-v1")},
#else
                {"purpose", QStringLiteral("youtube-offline-smoke-v1")},
#endif
                {"success", success},
                {"state", state},
                {"stage", stage},
                {"error", error},
                {"manifest_sha256", QString::fromLatin1(manifest_sha256)},
                {"file_count", file_count},
                {"validated_audit", validated_audit},
                {"process_acknowledged", process_acknowledged},
                {"runtime_lease_released", runtime_lease_released},
                {"temporary_preserved", temporary_preserved}
#ifdef RC_MUSIC_YOUTUBE_WORKER_SMOKE
                ,
                {"preparations", preparations},
                {"handoffs", handoffs},
                {"direct_starts", direct_starts},
                {"lease_path_units", lease_path_units},
                {"prepare_on_worker", prepare_on_worker},
                {"consumer_on_worker", consumer_on_worker},
                {"worker_stopped", worker_stopped},
                {"lease_released_at_handoff", lease_released_at_handoff},
                {"consumer_terminal", consumer_terminal},
                {"consumer_error", consumer_error}
#endif
        };
    }
};
std::optional<Options> options(const QStringList& arguments) {
    if (arguments.size() != 7)
        return {};
    Options result;
    QSet<QString> seen;
    for (qsizetype index = 1; index < arguments.size(); index += 2) {
        const auto key = arguments.at(index);
        const auto value = arguments.at(index + 1);
        if (seen.contains(key))
            return {};
        seen.insert(key);
        if (key == QLatin1String("--manifest-sha256")) {
            if (!youtube_receipt_detail::sha256(value))
                return {};
            result.manifest_sha256 = value.toLatin1();
        } else if (key == QLatin1String("--manifest") || key == QLatin1String("--receipt")) {
            const auto normalized = youtube_receipt_detail::localPath(value);
            if (!normalized)
                return {};
            if (key == QLatin1String("--manifest"))
                result.manifest = *normalized;
            else
                result.receipt = *normalized;
        } else {
            return {};
        }
    }
    if (result.manifest.isEmpty() || result.receipt.isEmpty() || result.manifest_sha256.isEmpty() ||
        result.manifest.compare(result.receipt, Qt::CaseInsensitive) == 0)
        return {};
    return result;
}
bool inside(const QString& path, const QString& directory) {
    return path.compare(directory, Qt::CaseInsensitive) == 0 || path.startsWith(directory + '/', Qt::CaseInsensitive);
}
std::optional<QByteArray> readManifest(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > youtube_runtime_receipt_max_bytes)
        return {};
    auto bytes = file.read(youtube_runtime_receipt_max_bytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.isEmpty() || bytes.size() > youtube_runtime_receipt_max_bytes ||
        !file.atEnd())
        return {};
    return bytes;
}
bool writeReport(QFile& file, const Report& report) {
    const auto bytes = QJsonDocument(report.json()).toJson(QJsonDocument::Compact) + '\n';
    return file.write(bytes) == bytes.size() && file.flush();
}
int failureReceipt(const Options& input, Report report) {
    QFile receipt(input.receipt);
    if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return 2;
    report.manifest_sha256 = input.manifest_sha256;
    return writeReport(receipt, report) ? 1 : 2;
}
bool observedSizes(const YoutubeRuntimeReceipt& receipt, const QString& private_root = {}) {
    if (receipt.declared_sizes.size() != receipt.config.artifacts.size())
        return false;
    quint64 total{};
    for (std::size_t index = 0; index < receipt.config.artifacts.size(); ++index) {
        const auto& source = receipt.config.artifacts[index];
        const QFileInfo file(private_root.isEmpty() ? source.source_path : private_root + '/' + source.relative_path);
        const auto bytes = file.size();
        if (!file.isFile() || file.isSymLink() || bytes <= 0 || bytes > 128LL * 1024 * 1024 ||
            static_cast<quint64>(bytes) != receipt.declared_sizes[index] ||
            static_cast<quint64>(bytes) > youtube_runtime_max_bytes - total)
            return false;
        total += static_cast<quint64>(bytes);
    }
    return total == receipt.total_bytes;
}
bool originalArtifactsUnchanged(const YoutubeRuntimeReceipt& receipt) {
    for (std::size_t index = 0; index < receipt.config.artifacts.size(); ++index) {
        const auto& source = receipt.config.artifacts[index];
        QFile file(source.source_path);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
            static_cast<quint64>(file.size()) != receipt.declared_sizes[index])
            return false;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        quint64 count{};
        while (!file.atEnd()) {
            const auto chunk = file.read(64 * 1024);
            if (file.error() != QFileDevice::NoError || chunk.isEmpty() ||
                static_cast<quint64>(chunk.size()) > receipt.declared_sizes[index] - count)
                return false;
            count += static_cast<quint64>(chunk.size());
            hash.addData(chunk);
        }
        if (count != receipt.declared_sizes[index] || hash.result().toHex() != source.sha256)
            return false;
    }
    return true;
}
std::optional<QProcessEnvironment> environment() {
    std::array<wchar_t, 32768> buffer{};
    const auto count = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (count == 0 || count >= static_cast<UINT>(buffer.size()))
        return {};
    const auto system_root = youtube_receipt_detail::localPath(QString::fromWCharArray(buffer.data(), count));
    if (!system_root)
        return {};
    QProcessEnvironment result; // Deliberately fresh; no systemEnvironment().
    result.insert(QStringLiteral("PATH"), QString{});
    result.insert(QStringLiteral("SystemRoot"), QDir::toNativeSeparators(*system_root));
    return result;
}
bool terminal(const YoutubeResolverState state) {
    return state == YoutubeResolverState::Ready || state == YoutubeResolverState::Failed ||
           state == YoutubeResolverState::Cancelled;
}
QString stateName(const YoutubeResolverState state) {
    switch (state) {
    case YoutubeResolverState::Idle:
        return QStringLiteral("idle");
    case YoutubeResolverState::Resolving:
        return QStringLiteral("resolving");
    case YoutubeResolverState::Stopping:
        return QStringLiteral("stopping");
    case YoutubeResolverState::Ready:
        return QStringLiteral("ready");
    case YoutubeResolverState::Failed:
        return QStringLiteral("failed");
    case YoutubeResolverState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}
QString failureName(const DownloadFailure error) {
    switch (error) {
    case DownloadFailure::None:
        return QStringLiteral("none");
    case DownloadFailure::Expired:
        return QStringLiteral("expired");
    case DownloadFailure::Restricted:
        return QStringLiteral("restricted");
    case DownloadFailure::LimitExceeded:
        return QStringLiteral("limit_exceeded");
    case DownloadFailure::Timeout:
        return QStringLiteral("timeout");
    case DownloadFailure::InvalidResult:
        return QStringLiteral("invalid_result");
    default:
        return QStringLiteral("unavailable");
    }
}
std::optional<QJsonObject> audit(const YoutubeResolverSnapshot& snapshot) {
    if (snapshot.state != YoutubeResolverState::Ready || snapshot.error != DownloadFailure::None || !snapshot.audio ||
        snapshot.audio->jobId() != job_id || snapshot.audio->videoId() != video_id ||
        snapshot.audio->data().video_id != video_id ||
        snapshot.audio->data().artist != QLatin1String("RC Music synthetic fixture"))
        return {};
    const auto& title = snapshot.audio->data().title;
    const QString prefix{QStringLiteral("RC Music offline v1 ")};
    if (!title.startsWith(prefix))
        return {};
    const auto payload = title.mid(prefix.size()).toUtf8();
    if (payload.size() > 2048 || !detail::strictJsonPreflight(payload, 4))
        return {};
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return {};
    const auto object = document.object();
    if (!youtube_receipt_detail::fields(
            object, {"yt_dlp", "ejs", "n", "sig", "http", "socket", "init", "players", "providers"}) ||
        object.value("yt_dlp") != QJsonValue(QStringLiteral("2026.03.17")) ||
        object.value("ejs") != QJsonValue(QStringLiteral("0.8.0")))
        return {};
    constexpr std::array<std::pair<const char*, int>, 7> counts{
        {{"n", 2}, {"sig", 3}, {"http", 0}, {"socket", 0}, {"init", 0}, {"players", 1}, {"providers", 1}}};
    for (const auto& [key, count] : counts)
        if (!object.value(QLatin1String(key)).isDouble() || object.value(QLatin1String(key)).toDouble() != count)
            return {};
    return object;
}
[[maybe_unused]] void run(const Options& input, YoutubeRuntimeReceipt receipt, Report& report) {
    report.file_count = static_cast<qsizetype>(receipt.config.artifacts.size());
    report.stage = QStringLiteral("metadata");
    report.error = QStringLiteral("size_mismatch");
    if (!observedSizes(receipt))
        return;
    const auto explicit_environment = environment();
    if (!explicit_environment) {
        report.stage = QStringLiteral("environment");
        report.error = QStringLiteral("unavailable");
        return;
    }
    QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/RCMusic-real-youtube-offline-XXXXXX"));
    if (!temporary.isValid()) {
        report.stage = QStringLiteral("temporary");
        report.error = QStringLiteral("unavailable");
        return;
    }
    // The outer TEMP owner must never bypass the inventory's conservative
    // cleanup by recursively deleting files preserved after an uncertain ACK.
    temporary.setAutoRemove(false);
    report.stage = QStringLiteral("runtime_prepare");
    report.error = QStringLiteral("unavailable");
    receipt.config.runtime_parent = temporary.path();
    std::weak_ptr<const WindowsYoutubeRuntime> lease;
    std::unique_ptr<WindowsYoutubeBrokerProcess> broker;
    std::unique_ptr<YoutubeResolverSession> session;
    YoutubeResolverSnapshot final;
    QElapsedTimer elapsed;
    elapsed.start();
    bool attempted{}, acknowledged{};
    try {
        WindowsYoutubeRuntimeError prepare_error{};
        auto files = WindowsYoutubeRuntime::prepare(receipt.config, &prepare_error);
        if (!files) {
            report.error = prepare_error == WindowsYoutubeRuntimeError::HashMismatch ? QStringLiteral("hash_mismatch")
                           : prepare_error == WindowsYoutubeRuntimeError::LimitExceeded
                               ? QStringLiteral("limit_exceeded")
                               : QStringLiteral("unavailable");
        } else if (!observedSizes(receipt, files->directory()) || !files->unchanged()) {
            report.error = QStringLiteral("size_mismatch");
        } else {
            lease = files;
            broker = std::make_unique<WindowsYoutubeBrokerProcess>(
                [files = std::move(files),
                 prepared_environment =
                     *explicit_environment]() mutable -> std::optional<WindowsYoutubeBrokerPreparedRuntime> {
                    if (!files)
                        return {};
                    return WindowsYoutubeBrokerPreparedRuntime{std::exchange(files, {}), prepared_environment};
                });
            session = std::make_unique<YoutubeResolverSession>(*broker);
            report.stage = QStringLiteral("resolver");
            report.error = QStringLiteral("unavailable");
            elapsed.restart();
            attempted = true;
            if (!session->start(job_id, video_id, 0, fixed_utc_seconds))
                session->cancel();
            do {
                QCoreApplication::processEvents();
                const auto now = elapsed.elapsed();
                if (now >= native_run_limit_ms)
                    session->cancel();
                final = session->poll(now, fixed_utc_seconds + now / 1000);
                if (terminal(final.state)) {
                    acknowledged = true;
                    break;
                }
                QThread::msleep(1);
            } while (elapsed.elapsed() < native_owner_limit_ms);
        }
    } catch (...) {
        report.error = QStringLiteral("exception");
    }
    if (attempted && !acknowledged && session && broker) {
        report.stage = QStringLiteral("drain");
        report.error = QStringLiteral("uncertain_stop");
        try {
            session->cancel();
            broker->cancel();
            while (elapsed.elapsed() < native_owner_limit_ms) {
                QCoreApplication::processEvents();
                const auto now = elapsed.elapsed();
                final = session->poll(now, fixed_utc_seconds + now / 1000);
                if (terminal(final.state)) {
                    acknowledged = true;
                    break;
                }
                QThread::msleep(1);
            }
        } catch (...) {
            // No exception or RAII path may be recorded as an exit/pipe ACK.
        }
    }
    report.process_acknowledged = !attempted || acknowledged;
    if (attempted)
        report.state = stateName(final.state);
    session.reset();
    broker.reset(); // Existing supervisor retains raw state/lease if not ACKed.
    report.runtime_lease_released = lease.expired();
    const bool empty = QDir(temporary.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
    const bool clean = report.process_acknowledged && report.runtime_lease_released && empty;
    temporary.setAutoRemove(clean);
    report.temporary_preserved = !clean;
    if (!attempted)
        return;
    if (!acknowledged) {
        report.stage = QStringLiteral("drain");
        report.error = QStringLiteral("uncertain_stop");
        return;
    }
    if (!clean) {
        report.stage = QStringLiteral("release");
        report.error = QStringLiteral("runtime_preserved");
        return;
    }
    if (final.state != YoutubeResolverState::Ready) {
        report.stage = QStringLiteral("resolver");
        report.error = failureName(final.error);
        return;
    }
    report.stage = QStringLiteral("audit");
    report.error = QStringLiteral("unexpected_audit");
    const auto validated = audit(final);
    if (!validated)
        return;
    report.validated_audit = *validated;
    report.stage = QStringLiteral("source_verify");
    report.error = QStringLiteral("source_changed");
    const auto manifest = readManifest(input.manifest);
    if (!manifest || QCryptographicHash::hash(*manifest, QCryptographicHash::Sha256).toHex() != input.manifest_sha256 ||
        !originalArtifactsUnchanged(receipt))
        return;
    report.success = true;
    report.state = QStringLiteral("ready");
    report.stage = QStringLiteral("complete");
    report.error = QStringLiteral("none");
}
#ifdef RC_MUSIC_YOUTUBE_WORKER_SMOKE
// At 241 UTF-16 units both child executable paths exceed the legacy MAX_PATH.
constexpr qsizetype minimum_worker_lease_path_units = 241;
constexpr qsizetype worker_lease_suffix_units = 53; // Slash, lease prefix and UUID.

struct WorkerObservations final {
    std::atomic_int preparations{}, handoffs{}, direct_starts{};
    std::atomic_bool prepare_on_worker{true}, consumer_on_worker{true}, prepared_sizes{true},
        lease_released_at_handoff{true};
    std::mutex mutex;
    std::weak_ptr<const WindowsYoutubeRuntime> lease{};
    QJsonObject validated_audit{};
    qsizetype lease_path_units{};
    void observe(const std::shared_ptr<const WindowsYoutubeRuntime>& files) {
        std::lock_guard lock(mutex);
        lease = files;
        if (files)
            lease_path_units = files->directory().size();
    }
    bool expired() {
        std::lock_guard lock(mutex);
        return lease.expired();
    }
    void observeConsumerThread() {
        const auto* application = QCoreApplication::instance();
        consumer_on_worker = consumer_on_worker && application && QThread::currentThread() != application->thread();
    }
};
class WorkerAuditConsumer final : public WindowsDownloadTask {
    std::shared_ptr<WorkerObservations> observations_;
    std::string id_{};
    bool cancelled_{};

  public:
    explicit WorkerAuditConsumer(std::shared_ptr<WorkerObservations> observations)
        : observations_(std::move(observations)) {
        observations_->observeConsumerThread();
    }
    ~WorkerAuditConsumer() override { observations_->observeConsumerThread(); }
    bool start(const std::string& id, const DownloadItem&, qint64) override {
        id_ = id;
        ++observations_->direct_starts;
        observations_->observeConsumerThread();
        return true;
    }
    bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> capability, qint64,
                              const qint64 epoch) override {
        observations_->observeConsumerThread();
        if (!capability || capability->expiresAtSeconds() <= epoch)
            return false;
        id_ = capability->jobId();
        // The real session minted this capability only after the broker's
        // terminal native/pipe ACK. Reuse the same audit as the direct smoke.
        const auto validated = audit({YoutubeResolverState::Ready, DownloadFailure::None, capability});
        observations_->lease_released_at_handoff = observations_->lease_released_at_handoff && observations_->expired();
        {
            std::lock_guard lock(observations_->mutex);
            observations_->validated_audit = validated.value_or(QJsonObject{});
        }
        ++observations_->handoffs;
        return validated.has_value();
    }
    void cancel() override {
        observations_->observeConsumerThread();
        cancelled_ = true;
    }
    DownloadExecutionUpdate poll(qint64) override {
        observations_->observeConsumerThread();
        // Deliberate acknowledged terminal: never touch transfer, converters,
        // publication, importer or playback, and never manufacture an MP3.
        return {.job_id = id_,
                .state = cancelled_ ? DownloadExecutionState::Cancelled : DownloadExecutionState::Failed,
                .error = cancelled_ ? DownloadFailure::None : DownloadFailure::UnsupportedMedia};
    }
};
bool workerTerminal(const DownloadExecutionState state) {
    return state == DownloadExecutionState::Published || state == DownloadExecutionState::Failed ||
           state == DownloadExecutionState::Cancelled;
}
QString workerStateName(const DownloadExecutionState state) {
    switch (state) {
    case DownloadExecutionState::Running:
        return QStringLiteral("running");
    case DownloadExecutionState::Published:
        return QStringLiteral("published");
    case DownloadExecutionState::Failed:
        return QStringLiteral("failed");
    case DownloadExecutionState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}
void runWorker(const Options& input, YoutubeRuntimeReceipt receipt, Report& report) {
    report.file_count = static_cast<qsizetype>(receipt.config.artifacts.size());
    report.stage = QStringLiteral("metadata");
    report.error = QStringLiteral("size_mismatch");
    if (!observedSizes(receipt))
        return;
    const auto explicit_environment = environment();
    if (!explicit_environment) {
        report.stage = QStringLiteral("environment");
        report.error = QStringLiteral("unavailable");
        return;
    }
    auto temporary_template = QDir::tempPath() + QStringLiteral("/RCMusic-real-youtube-worker-offline-");
    const auto suffix = QStringLiteral("XXXXXX");
    const auto padding =
        minimum_worker_lease_path_units - worker_lease_suffix_units - temporary_template.size() - suffix.size();
    if (padding > 0)
        temporary_template += QString(padding, u'x');
    QTemporaryDir temporary(temporary_template + suffix);
    if (!temporary.isValid()) {
        report.stage = QStringLiteral("temporary");
        report.error = QStringLiteral("unavailable");
        return;
    }
    temporary.setAutoRemove(false);
    receipt.config.runtime_parent = temporary.path();
    const auto observations = std::make_shared<WorkerObservations>();
    std::unique_ptr<WindowsDownloadExecution> execution;
    DownloadExecutionUpdate final;
    bool admitted{}, observed_terminal{}, stopped{};
    QElapsedTimer elapsed;
    elapsed.start();
    report.stage = QStringLiteral("worker");
    report.error = QStringLiteral("unavailable");
    try {
        auto source = makeWindowsYoutubeBrokerResolverFactory(
            receipt.config, *explicit_environment,
            [observations, expected = receipt](const WindowsYoutubeRuntimeConfig& config) {
                ++observations->preparations;
                const auto* application = QCoreApplication::instance();
                observations->prepare_on_worker =
                    observations->prepare_on_worker && application && QThread::currentThread() != application->thread();
                auto files = WindowsYoutubeRuntime::prepare(config);
                observations->observe(files);
                if (files && (!observedSizes(expected, files->directory()) || !files->unchanged())) {
                    observations->prepared_sizes = false;
                    return std::shared_ptr<const WindowsYoutubeRuntime>{};
                }
                return files;
            });
        execution = makeWindowsMediaAndYoutubeExecution(
            std::move(source), [observations] { return std::make_unique<WorkerAuditConsumer>(observations); },
            [] { return fixed_utc_seconds; });
        if (execution) {
            DownloadItem item{"https://www.youtube.com/watch?v=testVideo01", "RC Music synthetic fixture",
                              DownloadMediaKind::Audio};
            item.youtube = YoutubeAudioItem{"testVideo01", item.title, "RC Music synthetic fixture", "", false};
            admitted = execution->start(job_id, item);
            if (!admitted)
                execution->shutdown();
            do {
                QCoreApplication::processEvents();
                if (elapsed.elapsed() >= native_run_limit_ms) {
                    execution->cancel(job_id);
                    execution->shutdown();
                }
                final = execution->poll(job_id);
                if (workerTerminal(final.state)) {
                    observed_terminal = true;
                    execution->shutdown();
                }
                stopped = execution->stopped();
                if (stopped)
                    break;
                QThread::msleep(1);
            } while (elapsed.elapsed() < native_owner_limit_ms);
        }
    } catch (...) {
        report.error = QStringLiteral("exception");
    }
    if (execution && !stopped) {
        report.stage = QStringLiteral("drain");
        report.error = QStringLiteral("uncertain_stop");
        try {
            execution->cancel(job_id);
            execution->shutdown();
            while (elapsed.elapsed() < native_owner_limit_ms) {
                QCoreApplication::processEvents();
                final = execution->poll(job_id);
                observed_terminal = observed_terminal || workerTerminal(final.state);
                stopped = execution->stopped();
                if (stopped)
                    break;
                QThread::msleep(1);
            }
        } catch (...) {
        }
    }
    if (execution && !stopped) {
        // Its destructor joins. Preserve the owner, captured callback data and
        // TEMP after an uncertain stop instead of blocking or claiming ACK.
        static_cast<void>(execution.release());
    } else {
        execution.reset();
    }
    report.preparations = observations->preparations.load();
    report.handoffs = observations->handoffs.load();
    report.direct_starts = observations->direct_starts.load();
    report.prepare_on_worker = observations->prepare_on_worker.load();
    report.consumer_on_worker = observations->consumer_on_worker.load();
    report.lease_released_at_handoff = observations->lease_released_at_handoff.load();
    report.worker_stopped = stopped;
    report.process_acknowledged = observed_terminal && stopped;
    report.runtime_lease_released = observations->expired();
    {
        std::lock_guard lock(observations->mutex);
        report.validated_audit = observations->validated_audit;
        report.lease_path_units = observations->lease_path_units;
    }
    report.state = workerStateName(final.state);
    report.consumer_terminal = report.state;
    report.consumer_error = final.error == DownloadFailure::UnsupportedMedia ? QStringLiteral("unsupported_media")
                                                                             : failureName(final.error);
    const bool empty = QDir(temporary.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
    const bool clean = stopped && report.runtime_lease_released && empty;
    temporary.setAutoRemove(clean);
    report.temporary_preserved = !clean;
    if (!stopped) {
        report.stage = QStringLiteral("drain");
        report.error = QStringLiteral("uncertain_stop");
        return;
    }
    if (!clean) {
        report.stage = QStringLiteral("release");
        report.error = QStringLiteral("runtime_preserved");
        return;
    }
    report.stage = QStringLiteral("handoff");
    report.error = QStringLiteral("unexpected_handoff");
    if (!admitted || !observed_terminal || final.job_id != job_id || final.state != DownloadExecutionState::Failed ||
        final.error != DownloadFailure::UnsupportedMedia || !final.published_mp3.empty() || report.preparations != 1 ||
        report.handoffs != 1 || report.direct_starts != 0 ||
        report.lease_path_units < minimum_worker_lease_path_units || !report.prepare_on_worker ||
        !report.consumer_on_worker || !report.lease_released_at_handoff || !observations->prepared_sizes ||
        report.validated_audit.isEmpty())
        return;
    report.stage = QStringLiteral("source_verify");
    report.error = QStringLiteral("source_changed");
    const auto manifest = readManifest(input.manifest);
    if (!manifest || QCryptographicHash::hash(*manifest, QCryptographicHash::Sha256).toHex() != input.manifest_sha256 ||
        !originalArtifactsUnchanged(receipt))
        return;
    report.success = true;
    report.stage = QStringLiteral("complete");
    report.error = QStringLiteral("none");
}
#endif
} // namespace

// TEST ONLY: EXCLUDE_FROM_ALL, never a product mode or installed host.
int main(int argc, char** argv) {
    const QCoreApplication application(argc, argv);
    const auto input = options(application.arguments());
    if (!input || QFileInfo::exists(input->receipt))
        return 2;
    Report report;
    report.manifest_sha256 = input->manifest_sha256;
    report.stage = QStringLiteral("manifest_read");
    report.error = QStringLiteral("unavailable");
    const auto bytes = readManifest(input->manifest);
    if (!bytes)
        return failureReceipt(*input, report);
    report.stage = QStringLiteral("manifest_hash");
    report.error = QStringLiteral("hash_mismatch");
    if (QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256).toHex() != input->manifest_sha256)
        return failureReceipt(*input, report);
    report.stage = QStringLiteral("manifest_decode");
    report.error = QStringLiteral("invalid_manifest");
    const auto decoded = decodeYoutubeRuntimeReceipt(*bytes);
    if (!decoded.receipt)
        return failureReceipt(*input, report);
    if (inside(input->receipt, decoded.receipt->artifact_root))
        return 2;
    for (const auto& source : decoded.receipt->sources)
        if (source.path.compare(input->receipt, Qt::CaseInsensitive) == 0)
            return 2;
    QFile receipt(input->receipt);
    if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return 2;
    try {
#ifdef RC_MUSIC_YOUTUBE_WORKER_SMOKE
        runWorker(*input, *decoded.receipt, report);
#else
        run(*input, *decoded.receipt, report);
#endif
    } catch (...) {
        report.success = false;
        report.stage = QStringLiteral("driver");
        report.error = QStringLiteral("exception");
    }
    if (!writeReport(receipt, report))
        return 2;
    return report.success ? 0 : 1;
}

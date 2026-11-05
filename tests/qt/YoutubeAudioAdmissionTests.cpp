#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/YoutubeAudioAdmission.hpp"
#include "m0/application/DownloadAdmissionController.hpp"

#include <QTest>
#include <functional>
#include <stdexcept>

using namespace music_player::m0;
namespace {
const YoutubeAudioRequest request{
    "01234567-89ab-4cde-8fab-0123456789ab",
    {{"aB0-_Cd1EF2", "Canção 日本", "Artist", "https://www.letras.mus.br/naruto/silhouette/", true}}};
class Destination final : public AsyncDownloadSubmission {
  public:
    int begins{}, polls{};
    DownloadRequest received;
    std::function<void()> callback;
    DownloadAdmissionResult result{.state = DownloadAdmissionState::Pending};
    bool throws{};
    DownloadAdmissionResult begin(const DownloadRequest& value) override {
        ++begins;
        received = value;
        if (callback)
            callback();
        if (throws)
            throw std::runtime_error("uncertain admission");
        return result;
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view id) override {
        ++polls;
        if (id != request.request_id)
            return {};
        return DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"youtube-1"}};
    }
};
class Execution final : public DownloadExecution {
  public:
    int starts{}, cancels{};
    DownloadItem received;
    DownloadExecutionUpdate update;
    bool start(const std::string& id, const DownloadItem& item) override {
        ++starts;
        received = item;
        update = {.job_id = id};
        return true;
    }
    void cancel(std::string_view) override {
        ++cancels;
        update.state = DownloadExecutionState::Cancelled;
    }
    DownloadExecutionUpdate poll(std::string_view) override { return update; }
};
class Import final : public DownloadImport {
  public:
    int calls{};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
};
class Io final : public DownloadCheckpointIo {
  public:
    std::optional<DownloadCheckpointIoResult> operation;
    std::optional<DownloadCheckpoint> initial;
    DownloadCheckpoint saved;
    bool acknowledge{}, closed{};
    DownloadCheckpointIoCommand startLoad(std::uint64_t id) override {
        operation = DownloadCheckpointIoResult{
            .operation_id = id,
            .operation = DownloadCheckpointOperation::Load,
            .load = {.checkpoint = initial,
                     .status = initial ? DownloadCheckpointLoadStatus::Loaded : DownloadCheckpointLoadStatus::Missing}};
        return DownloadCheckpointIoCommand::Accepted;
    }
    DownloadCheckpointIoCommand startSave(std::uint64_t id, const DownloadCheckpoint& value) override {
        saved = value;
        operation = DownloadCheckpointIoResult{.operation_id = id, .operation = DownloadCheckpointOperation::Save};
        return DownloadCheckpointIoCommand::Accepted;
    }
    std::optional<DownloadCheckpointIoResult> poll(std::uint64_t id) override {
        if (!operation || operation->operation_id != id ||
            (operation->operation == DownloadCheckpointOperation::Save && !acknowledge))
            return {};
        auto result = operation;
        if (result->operation == DownloadCheckpointOperation::Save)
            result->save_status = DownloadCheckpointSaveStatus::Saved;
        operation.reset();
        return result;
    }
    void shutdown() override { closed = true; }
    bool stopped() const override { return closed && !operation; }
};
struct Harness final {
    Execution execution;
    Import importer;
    QtDownloadRequestIdentity identity;
    Io io;
    DownloadService service;
    DownloadAdmissionController controller{service, io};
    YoutubeAudioAdmission admission{controller};
    explicit Harness(std::string prefix = "yt-owner")
        : service(execution, importer, std::move(prefix), {}, &identity) {}
    void start() {
        if (!controller.start())
            throw std::runtime_error("startup refused");
        controller.tick(0);
    }
};
} // namespace
class YoutubeAudioAdmissionTests final : public QObject {
    Q_OBJECT
  private slots:
    void mapsOnlyTheCanonicalAudioAndPreservesTheWholeSelectedContext() {
        Destination destination;
        YoutubeAudioAdmission bridge(destination);
        auto selected = request;
        const auto pending = bridge.begin(selected);
        QCOMPARE(pending.state, DownloadAdmissionState::Pending);
        QCOMPARE(pending.result.error, DownloadSubmissionError::NotAvailable);
        QCOMPARE(destination.begins, 1);
        QCOMPARE(destination.received.request_id, request.request_id);
        QCOMPARE(destination.received.items.size(), std::size_t{1});
        const auto& item = destination.received.items.front();
        QCOMPARE(item.url, std::string("https://www.youtube.com/watch?v=aB0-_Cd1EF2"));
        QCOMPARE(item.title, request.items.front().title);
        QCOMPARE(item.kind, DownloadMediaKind::Audio);
        QVERIFY(item.youtube);
        QCOMPARE(*item.youtube, request.items.front());
        selected.items.front().artist = "Changed after begin";
        QCOMPARE(item.youtube->artist, request.items.front().artist);
        const auto ack = bridge.poll(request.request_id);
        QVERIFY(ack);
        QCOMPARE(ack->job_ids, std::vector<std::string>{"youtube-1"});
    }
    void invalidTypedBatchNeverReachesTheDurableOwner() {
        Destination destination;
        YoutubeAudioAdmission bridge(destination);
        for (int kind = 0; kind < 6; ++kind) {
            auto invalid = request;
            switch (kind) {
            case 0:
                invalid.request_id = "invalid";
                break;
            case 1:
                invalid.items.front().video_id = "https://youtube.com/";
                break;
            case 2:
                invalid.items.front().lyrics_page = "https://attacker.example/";
                break;
            case 3:
                invalid.items.front().title = std::string(513, 'a');
                break;
            case 4:
                invalid.items.push_back(invalid.items.front());
                break;
            default:
                invalid.items.front().artist = std::string("\xc0\xaf", 2);
                break;
            }
            const auto result = bridge.begin(invalid);
            QCOMPARE(result.state, DownloadAdmissionState::Completed);
            QVERIFY(result.result.error != DownloadSubmissionError::None);
        }
        QCOMPARE(destination.begins, 0);
    }
    void reentrantAdmissionIsRefusedAndAnUncertainExceptionIsNotAnAcknowledgement() {
        Destination destination;
        YoutubeAudioAdmission bridge(destination);
        DownloadAdmissionResult nested;
        destination.callback = [&] { nested = bridge.begin(request); };
        destination.throws = true;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, static_cast<void>(bridge.begin(request)));
        QCOMPARE(nested.result.error, DownloadSubmissionError::Busy);
        QCOMPARE(destination.begins, 1);
        destination.callback = {};
        destination.throws = false;
        QCOMPARE(bridge.begin(request).state, DownloadAdmissionState::Pending);
        QCOMPARE(destination.begins, 2);
    }
    void commonControllerDoesNotAckOrExecuteBeforeSavedAndRejectsChangedConsent() {
        Harness h;
        h.start();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Ready);
        QCOMPARE(h.admission.begin(request).state, DownloadAdmissionState::Pending);
        QVERIFY(!h.admission.poll(request.request_id));
        h.controller.tick(1);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.service.snapshot().size(), std::size_t{1});
        auto changed = request;
        changed.items.front().fetch_lyrics = false;
        QCOMPARE(h.admission.begin(changed).result.error, DownloadSubmissionError::InvalidRequest);
        h.io.acknowledge = true;
        h.controller.tick(2);
        const auto ack = h.admission.poll(request.request_id);
        QVERIFY(ack);
        QCOMPARE(ack->error, DownloadSubmissionError::None);
        QCOMPARE(h.execution.starts, 1);
        QVERIFY(h.execution.received.youtube);
        QCOMPARE(*h.execution.received.youtube, request.items.front());
        changed = request;
        changed.items.front().artist = "Another artist";
        QCOMPARE(h.admission.begin(changed).result.error, DownloadSubmissionError::InvalidRequest);
        QCOMPARE(h.service.snapshot().size(), std::size_t{1});
        QCOMPARE(h.importer.calls, 0);
    }
    void restartReplaysTheCompleteFingerprintWithoutAStreamOrRestartingAudio() {
        Harness first;
        first.start();
        QCOMPARE(first.admission.begin(request).state, DownloadAdmissionState::Pending);
        first.io.acknowledge = true;
        first.controller.tick(1);
        const auto original = first.admission.poll(request.request_id);
        QVERIFY(original);
        auto checkpoint = first.service.checkpoint();
        QVERIFY(checkpoint);
        const auto encoded = DownloadCheckpointJson::encode(*checkpoint);
        QVERIFY(!encoded.isEmpty());
        QVERIFY(!encoded.contains("youtube.com"));
        QVERIFY(!encoded.contains("googlevideo"));
        QVERIFY(!encoded.contains("lyricsPage"));
        QVERIFY(!encoded.contains("fetchLyrics"));
        Harness second("yt-restarted");
        second.io.initial = checkpoint;
        second.start();
        QCOMPARE(second.controller.state(), DownloadAdmissionControllerState::Ready);
        const auto replay = second.admission.begin(request);
        QCOMPARE(replay.state, DownloadAdmissionState::Completed);
        QCOMPARE(replay.result.job_ids, original->job_ids);
        QCOMPARE(second.execution.starts, 0);
        for (int field = 0; field < 3; ++field) {
            auto changed = request;
            if (field == 0)
                changed.items.front().artist = "Other";
            else if (field == 1)
                changed.items.front().lyrics_page = "";
            else
                changed.items.front().fetch_lyrics = false;
            QCOMPARE(second.admission.begin(changed).result.error, DownloadSubmissionError::InvalidRequest);
        }
        QCOMPARE(second.execution.starts, 0);
        QCOMPARE(second.importer.calls, 0);
    }
};
QTEST_GUILESS_MAIN(YoutubeAudioAdmissionTests)
#include "YoutubeAudioAdmissionTests.moc"

#include "adapters/NativeDownloadSession.hpp"
#include "adapters/NativeMessageFrame.hpp"
#include "m0/application/DownloadService.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace music_player::m0;

namespace {
const QString origin = QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/");
const QByteArray request =
    R"({"schemaVersion":1,"type":"download.request","requestId":"01234567-89ab-4cde-8fab-0123456789ab","items":[{"url":"https://media.example.test/a.mp4?token=private","title":"Selected video","kind":"video"}]})";

class Execution final : public DownloadExecution {
  public:
    int starts{0};
    int cancels{0};
    DownloadItem item{};
    DownloadExecutionUpdate update{};
    bool start(const std::string& id, const DownloadItem& selected) override {
        ++starts;
        item = selected;
        update = {.job_id = id};
        return true;
    }
    void cancel(std::string_view) override { ++cancels; }
    DownloadExecutionUpdate poll(std::string_view) override { return update; }
};

class Import final : public DownloadImport {
  public:
    int calls{0};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
};

QJsonObject submitThroughConnection(DownloadService& service, const QByteArray& payload = request,
                                    const QString& caller = origin) {
    NativeDownloadSession channel(caller, origin, service);
    const auto reply = channel.receive(encodeNativeMessageFrame(payload), 1);
    return QJsonDocument::fromJson(reply.response_frame.mid(4)).object();
}
} // namespace

class NativeDownloadServiceTests final : public QObject {
    Q_OBJECT
  private slots:
    void acceptedBatchSurvivesBrowserConnectionAndIsNotStartedByAcknowledgement() {
        Execution execution;
        Import importer;
        DownloadService service(execution, importer, "isolated-session");
        const auto ack = submitThroughConnection(service);
        QCOMPARE(ack.value("status").toString(), QStringLiteral("accepted"));
        QCOMPARE(ack.value("jobIds").toArray().size(), qsizetype{1});
        QCOMPARE(service.snapshot().size(), std::size_t{1});
        QCOMPARE(execution.starts, 0);
        service.tick(0);
        QCOMPARE(execution.starts, 1);
        QCOMPARE(execution.item.url, std::string("https://media.example.test/a.mp4?token=private"));
        QCOMPARE(execution.item.kind, DownloadMediaKind::Video);
        QCOMPARE(importer.calls, 0);
    }

    void anotherConnectionReusesTheSameReceiptButCannotChangeTheSelection() {
        Execution execution;
        Import importer;
        DownloadService service(execution, importer, "isolated-session");
        const auto first = submitThroughConnection(service);
        const auto repeated = submitThroughConnection(service);
        QCOMPARE(repeated, first);
        QCOMPARE(service.snapshot().size(), std::size_t{1});
        const auto changed = submitThroughConnection(service, QByteArray(request).replace("a.mp4", "b.mp4"));
        QCOMPARE(changed.value("status").toString(), QStringLiteral("rejected"));
        QCOMPARE(changed.value("code").toString(), QStringLiteral("invalid_request"));
        QCOMPARE(service.snapshot().size(), std::size_t{1});
        service.tick(0);
        QCOMPARE(execution.starts, 1);
    }

    void invalidOrUnauthorizedInputCannotCreateJobs() {
        Execution execution;
        Import importer;
        DownloadService service(execution, importer, "isolated-session");
        QVERIFY(submitThroughConnection(service, request, QStringLiteral("https://page.example.test/")).isEmpty());
        for (const auto& payload :
             {QByteArray("{}"), QByteArray(request).replace("media.example.test", "youtube.com"),
              QByteArray(request).replace("\"kind\":\"video\"", "\"kind\":\"video\",\"headers\":{}")}) {
            const auto result = submitThroughConnection(service, payload);
            QVERIFY(result.isEmpty() || result.value("status").toString() == "rejected");
        }
        QVERIFY(service.snapshot().empty());
        service.tick(0);
        QCOMPARE(execution.starts, 0);
    }

    void cancelledJobReceiptDoesNotQueueTheBatchAgain() {
        Execution execution;
        Import importer;
        DownloadService service(execution, importer, "isolated-session");
        const auto first = submitThroughConnection(service);
        QVERIFY(!first.value("jobIds").toArray().isEmpty());
        QVERIFY(service.cancel(first.value("jobIds").toArray().first().toString().toStdString()));
        QCOMPARE(submitThroughConnection(service), first);
        QCOMPARE(service.snapshot().size(), std::size_t{1});
        QCOMPARE(service.snapshot().front().state, DownloadJobState::Cancelled);
        service.tick(0);
        QCOMPARE(execution.starts, 0);
    }

    void capacityAndShutdownProduceTypedRejectionsWithoutFalseAcknowledgement() {
        Execution execution;
        Import importer;
        DownloadService service(execution, importer, "isolated-session", {.max_jobs = 1});
        QCOMPARE(submitThroughConnection(service).value("status").toString(), QStringLiteral("accepted"));
        const auto another = QByteArray(request).replace("0123456789ab\"", "0123456789ac\"");
        const auto busy = submitThroughConnection(service, another);
        QCOMPARE(busy.value("status").toString(), QStringLiteral("rejected"));
        QVERIFY(!busy.contains("jobIds"));
        service.shutdown();
        QCOMPARE(submitThroughConnection(service, another).value("status").toString(), QStringLiteral("rejected"));
        QCOMPARE(execution.starts, 0);
    }
};

QTEST_APPLESS_MAIN(NativeDownloadServiceTests)
#include "NativeDownloadServiceTests.moc"

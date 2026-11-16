#include "adapters/WindowsYoutubeBrokerProcess.hpp"
#include "adapters/YoutubeAudioResolution.hpp"
#include "adapters/YoutubeSearch.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace music_player::m0;
namespace {
const QString video_id{QStringLiteral("testVideo01")};
using State = YoutubeResolverProcessState;
using Failure = DownloadFailure;
bool terminal(const State state) { return state != State::Running; }
QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
QByteArray fileHash(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
struct NativeHandle final {
    HANDLE value{};
    ~NativeHandle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + QStringLiteral("/RC Music broker 日本-XXXXXX")};
    QString runtime_parent{parent.filePath(QStringLiteral("private runtimes"))};
    QString markers{parent.filePath(QStringLiteral("markers"))};
    WindowsYoutubeRuntimeConfig runtime{};
    QProcessEnvironment environment{};
    std::array<NativeHandle, 9> node_gates{};
    NativeHandle extractor_gate{}, extractor_final_gate{};
    QString node_gate_prefix{}, extractor_gate_name{}, extractor_final_gate_name{}, runtime_directory{};
    std::weak_ptr<const WindowsYoutubeRuntime> lease{};
    std::optional<WindowsYoutubeBrokerPreparedRuntime> pending{};
    int preparations{};
    bool initialize(const QString& test_case = QStringLiteral("single"), const bool gate_nodes = false,
                    const bool gate_extractor = false, const bool gate_final = false) {
        if (!parent.isValid() || !QDir().mkdir(runtime_parent) || !QDir().mkdir(markers))
            return false;
        const auto core = parent.filePath(QStringLiteral("source core.js"));
        const auto library = parent.filePath(QStringLiteral("source library.js"));
        if (!writeFile(core, QByteArrayLiteral("/* SYNTHETIC_CORE */ function jsc(data) { return data; }")) ||
            !writeFile(library, QByteArrayLiteral("/* SYNTHETIC_LIBRARY */ var lib = {};")))
            return false;
        const auto helper = QString::fromUtf8(RC_MUSIC_YOUTUBE_BROKER_HELPER_PATH);
        const auto helper_hash = fileHash(helper);
        if (helper_hash.isEmpty())
            return false;
        runtime = {
            runtime_parent,
            {{WindowsYoutubeRuntimeRole::Extractor, QStringLiteral("extractor/yt-dlp.exe"), helper, helper_hash},
             {WindowsYoutubeRuntimeRole::Node, QStringLiteral("javascript/node.exe"), helper, helper_hash},
             {WindowsYoutubeRuntimeRole::EjsCore,
              QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js"), core, fileHash(core)},
             {WindowsYoutubeRuntimeRole::EjsLibrary,
              QStringLiteral("extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"), library, fileHash(library)}}};
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        node_gate_prefix = QStringLiteral("Local\\RCMusic-broker-node-") + token + '-';
        extractor_gate_name = QStringLiteral("Local\\RCMusic-broker-extractor-") + token;
        extractor_final_gate_name = QStringLiteral("Local\\RCMusic-broker-extractor-final-") + token;
        for (std::size_t index = 0; index < node_gates.size(); ++index) {
            const auto name = node_gate_prefix + QString::number(index + 1);
            node_gates[index].value = CreateEventW(nullptr, TRUE, !gate_nodes, name.toStdWString().c_str());
            if (!node_gates[index].value || GetLastError() == ERROR_ALREADY_EXISTS)
                return false;
        }
        extractor_gate.value = CreateEventW(nullptr, TRUE, !gate_extractor, extractor_gate_name.toStdWString().c_str());
        if (!extractor_gate.value || GetLastError() == ERROR_ALREADY_EXISTS)
            return false;
        extractor_final_gate.value =
            CreateEventW(nullptr, TRUE, !gate_final, extractor_final_gate_name.toStdWString().c_str());
        if (!extractor_final_gate.value || GetLastError() == ERROR_ALREADY_EXISTS)
            return false;
        environment.insert(QStringLiteral("PATH"), QLibraryInfo::path(QLibraryInfo::BinariesPath));
        environment.insert(QStringLiteral("SystemRoot"), qEnvironmentVariable("SystemRoot"));
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_CASE"), test_case);
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_MARKERS"), markers);
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_NODE_GATE_PREFIX"), node_gate_prefix);
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_EXTRACTOR_GATE"), extractor_gate_name);
        environment.insert(QStringLiteral("RC_MUSIC_BROKER_TEST_EXTRACTOR_FINAL_GATE"), extractor_final_gate_name);
        return true;
    }
    std::optional<WindowsYoutubeBrokerPreparedRuntime> prepare() {
        ++preparations;
        if (pending)
            return std::exchange(pending, {});
        auto files = WindowsYoutubeRuntime::prepare(runtime);
        if (!files)
            return {};
        runtime_directory = files->directory();
        lease = files;
        return WindowsYoutubeBrokerPreparedRuntime{std::move(files), environment};
    }
    QString marker(const QString& leaf) const { return markers + '/' + leaf; }
    QJsonObject report(const QString& leaf) const { return QJsonDocument::fromJson(readFile(marker(leaf))).object(); }
    QStringList reports() const { return QDir(markers).entryList(QDir::Files | QDir::NoDotAndDotDot); }
    void verifySources() const {
        for (const auto& source : runtime.artifacts)
            QCOMPARE(fileHash(source.source_path), source.sha256);
    }
};
struct Collected final {
    YoutubeResolverProcessUpdate update{};
    QByteArray output{}, errors{};
};
void sample(YoutubeResolverProcess& process, Collected& result, const qint64 now_ms) {
    QCoreApplication::processEvents();
    result.update = process.poll(now_ms);
    result.output += result.update.stdout_chunk;
    result.errors += result.update.stderr_chunk;
}
bool collect(YoutubeResolverProcess& process, Collected& result, const qint64 now_ms = 0,
             const qint64 timeout_ms = 10'000, const bool yield_between_polls = false) {
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        sample(process, result, now_ms);
        if (terminal(result.update.state))
            return true;
        if (yield_between_polls)
            QThread::yieldCurrentThread();
        else
            QThread::msleep(1);
    } while (elapsed.elapsed() < timeout_ms);
    return false;
}
bool waitMarker(YoutubeResolverProcess& process, Collected& result, const QString& path) {
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        sample(process, result, 0);
        if (!readFile(path).isEmpty())
            return true;
        if (terminal(result.update.state))
            return false;
        QThread::msleep(1);
    } while (elapsed.elapsed() < 10'000);
    return false;
}
void pump(YoutubeResolverProcess& process, Collected& result) {
    for (int index = 0; index < 40; ++index) {
        sample(process, result, 0);
        QThread::msleep(1);
    }
}
struct DrainGuard final {
    YoutubeResolverProcess* process{};
    Fixture& fixture;
    ~DrainGuard() {
        if (!process)
            return;
        process->cancel();
        Collected ignored;
        if (!collect(*process, ignored))
            fixture.parent.setAutoRemove(false);
    }
};
HANDLE processHandle(const QJsonObject& report) {
    return OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(report.value("pid").toInteger()));
}
void verifyReport(const Fixture& fixture, const QString& leaf, const QString& role, const QStringList& arguments) {
    const auto report = fixture.report(leaf);
    QCOMPARE(report.value("role").toString(), role);
    QCOMPARE(report.value("argv").toArray(), QJsonArray::fromStringList(arguments));
    const auto directory = role == QLatin1String("node") ? QStringLiteral("javascript") : QStringLiteral("extractor");
    QCOMPARE(report.value("cwd").toString(), fixture.runtime_directory + '/' + directory);
    QVERIFY(report.value("executable").toString().startsWith(fixture.runtime_directory + '/' + directory + '/'));
    QVERIFY(report.value("spawnBlocked").toBool());
}
} // namespace

class WindowsYoutubeBrokerProcessTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionIsPassiveAndCancelBeforeStartNeverPrepares() {
        Fixture h;
        QVERIFY(h.initialize());
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QCOMPARE(h.preparations, 0);
        QVERIFY(h.reports().isEmpty());
        QVERIFY(process.poll(0).stdout_chunk.isEmpty());
        process.cancel();
        QVERIFY(!process.start(video_id, 0));
        const auto result = process.poll(0);
        QCOMPARE(result.state, State::Cancelled);
        QVERIFY(result.stdout_chunk.isEmpty());
        QCOMPARE(h.preparations, 0);
        QVERIFY(h.reports().isEmpty());
    }
    void invalidIdentifiersAndNegativeClockNeverInvokePreparation_data() {
        QTest::addColumn<QString>("id");
        QTest::addColumn<qint64>("now");
        QTest::newRow("empty") << QString{} << qint64{0};
        QTest::newRow("URL") << QStringLiteral("https://youtube.com/watch?v=testVideo01") << qint64{0};
        QTest::newRow("option") << QStringLiteral("--exec=evil") << qint64{0};
        QTest::newRow("space") << QStringLiteral("test Video1") << qint64{0};
        QTest::newRow("negative clock") << video_id << qint64{-1};
    }
    void searchModeRunsOnlyTheExtractorWithTheQuery() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("search")));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); }, WindowsYoutubeBrokerMode::Search);
        DrainGuard guard{&process, h};
        const auto query = QStringLiteral("lia campos \u00e7\u00e3o");
        QVERIFY(process.start(query, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        const auto decoded = decodeYoutubeSearchResults(result.output);
        QVERIFY(decoded);
        QCOMPARE(decoded->size(), std::size_t{1});
        QCOMPARE(decoded->front().title, QStringLiteral("Can\u00e7\u00e3o"));
        verifyReport(h, QStringLiteral("extractor.pid.json"), QStringLiteral("extractor"),
                     {QStringLiteral("--search"), query});
        QCOMPARE(h.reports(), QStringList{QStringLiteral("extractor.pid.json")});
        QVERIFY(h.lease.expired());
        guard.process = nullptr;
    }
    void searchModeRefusesChallengeFramesWithoutLaunchingNode() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("search-challenge")));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); }, WindowsYoutubeBrokerMode::Search);
        DrainGuard guard{&process, h};
        QVERIFY(process.start(QStringLiteral("lia campos \u00e7\u00e3o"), 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Failure::InvalidResult);
        QVERIFY(result.output.isEmpty());
        QCOMPARE(h.reports(), QStringList{QStringLiteral("extractor.pid.json")});
        guard.process = nullptr;
    }
    void searchModeRejectsUnnormalizedQueriesAndVideoIdsMeanNothingSpecial_data() {
        QTest::addColumn<QString>("query");
        QTest::newRow("empty") << QString{};
        QTest::newRow("padded") << QStringLiteral(" lia ");
        QTest::newRow("double space") << QStringLiteral("lia  campos");
        QTest::newRow("control") << QStringLiteral("a\u0001b");
        QTest::newRow("too long") << QString(401, QLatin1Char('x'));
    }
    void searchModeRejectsUnnormalizedQueriesAndVideoIdsMeanNothingSpecial() {
        QFETCH(QString, query);
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("search")));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); }, WindowsYoutubeBrokerMode::Search);
        DrainGuard guard{&process, h};
        QVERIFY(!process.start(query, 0));
        QCOMPARE(process.poll(0).error, Failure::InvalidResult);
        QCOMPARE(h.preparations, 0);
        QVERIFY(h.reports().isEmpty());
    }
    void invalidIdentifiersAndNegativeClockNeverInvokePreparation() {
        QFETCH(QString, id);
        QFETCH(qint64, now);
        Fixture h;
        QVERIFY(h.initialize());
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(!process.start(id, now));
        const auto result = process.poll(0);
        QCOMPARE(result.state, State::Failed);
        QCOMPARE(result.error, Failure::InvalidResult);
        QCOMPARE(h.preparations, 0);
        QVERIFY(h.reports().isEmpty());
    }
    void missingThrowingAndNullRuntimePreparationFailWithoutChildren_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("nullopt") << 0;
        QTest::newRow("exception") << 1;
        QTest::newRow("no file capability") << 2;
    }
    void missingThrowingAndNullRuntimePreparationFailWithoutChildren() {
        QFETCH(int, mode);
        Fixture h;
        QVERIFY(h.initialize());
        int calls{};
        WindowsYoutubeBrokerProcess process([&]() -> std::optional<WindowsYoutubeBrokerPreparedRuntime> {
            ++calls;
            if (mode == 1)
                throw std::runtime_error("PRIVATE preparation diagnostic");
            if (mode == 2)
                return WindowsYoutubeBrokerPreparedRuntime{{}, h.environment};
            return {};
        });
        DrainGuard guard{&process, h};
        QVERIFY(!process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Failure::Unavailable);
        QCOMPARE(calls, 1);
        QVERIFY(result.output.isEmpty());
        QVERIFY(!result.errors.contains("PRIVATE"));
        QVERIFY(h.reports().isEmpty());
        QVERIFY(!process.start(video_id, 0));
    }
    void cancellationReenteredFromPreparationRefusesBothNativeStarts() {
        Fixture h;
        QVERIFY(h.initialize());
        WindowsYoutubeBrokerProcess* current{};
        WindowsYoutubeBrokerProcess process([&] {
            current->cancel();
            return h.prepare();
        });
        current = &process;
        DrainGuard guard{&process, h};
        QVERIFY(!process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Cancelled);
        QCOMPARE(h.preparations, 1);
        QVERIFY(h.reports().isEmpty());
        QVERIFY(h.lease.expired());
        QVERIFY(QDir(h.runtime_parent).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }
    void acceptedStartCompletesPreparationButDefersExtractorLaunchUntilPolling() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("gate-challenge"), false, true));
        bool preparation_completed{};
        WindowsYoutubeBrokerProcess process([&] {
            auto result = h.prepare();
            preparation_completed = true;
            return result;
        });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        QVERIFY(preparation_completed);
        QCOMPARE(h.preparations, 1);
        QVERIFY(!h.lease.expired());
        // Do not poll the broker here. Give an incorrectly eager native start
        // time to publish its real helper marker before testing absence.
        QTest::qWait(250);
        QVERIFY2(h.reports().isEmpty(), qPrintable(h.reports().join(QStringLiteral(", "))));

        Collected result;
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("extractor.pid.json"))));
        QCOMPARE(result.update.state, State::Running);
        QCOMPARE(h.preparations, 1);
        verifyReport(h, QStringLiteral("extractor.pid.json"), QStringLiteral("extractor"), {video_id});
        process.cancel();
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Cancelled);
        QVERIFY(result.output.isEmpty());
        QVERIFY(h.lease.expired());
        h.verifySources();
    }
    void cancellationBeforeFirstPollReleasesPreparedRuntimeWithoutLaunchingWorkers() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("gate-challenge"), false, true));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        QCOMPARE(h.preparations, 1);
        QVERIFY(!h.lease.expired());
        QTest::qWait(250);
        process.cancel();
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Cancelled);
        QVERIFY(result.output.isEmpty());
        QVERIFY(result.errors.isEmpty());
        QCOMPARE(h.preparations, 1);
        QVERIFY2(h.reports().isEmpty(), qPrintable(h.reports().join(QStringLiteral(", "))));
        QVERIFY(h.lease.expired());
        QVERIFY(QDir(h.runtime_parent).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(!process.start(video_id, 0));
        h.verifySources();
    }
    void firstPollRejectsExpiredOrRegressedClockBeforeAnyExtractorLaunch_data() {
        QTest::addColumn<qint64>("first_poll");
        QTest::addColumn<int>("expected_failure");
        QTest::newRow("inclusive global deadline") << qint64{60'100} << static_cast<int>(Failure::Timeout);
        QTest::newRow("regressed before launch") << qint64{99} << static_cast<int>(Failure::InvalidResult);
    }
    void firstPollRejectsExpiredOrRegressedClockBeforeAnyExtractorLaunch() {
        QFETCH(qint64, first_poll);
        QFETCH(int, expected_failure);
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("gate-challenge"), false, true));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 100));
        QCOMPARE(h.preparations, 1);
        QVERIFY(!h.lease.expired());
        QTest::qWait(250);
        Collected result;
        QVERIFY(collect(process, result, first_poll));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, static_cast<Failure>(expected_failure));
        QVERIFY(result.output.isEmpty());
        QVERIFY(result.errors.isEmpty());
        QCOMPARE(h.preparations, 1);
        QVERIFY2(h.reports().isEmpty(), qPrintable(h.reports().join(QStringLiteral(", "))));
        QVERIFY(h.lease.expired());
        QVERIFY(QDir(h.runtime_parent).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(!process.start(video_id, first_poll));
        h.verifySources();
    }
    void largeTwoGroupExchangeWaitsForEveryNodeExitBeforeForwardingAndFinalPublication() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("large-two-groups"), true, false, true));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QCOMPARE(h.preparations, 0);
        QVERIFY(process.start(video_id, 0));
        QVERIFY(!process.start(video_id, 0));
        Collected result;
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("node-1.output-ready"))));
        const NativeHandle extractor{processHandle(h.report(QStringLiteral("extractor.pid.json")))};
        const NativeHandle first_node{processHandle(h.report(QStringLiteral("node-1.pid.json")))};
        QVERIFY(extractor.value && first_node.value);
        QCOMPARE(WaitForSingleObject(first_node.value, 0), DWORD{WAIT_TIMEOUT});
        QVERIFY(!h.lease.expired());
        QVERIFY(QDir(h.runtime_directory).exists());
        pump(process, result);
        QVERIFY(result.output.isEmpty());
        QVERIFY(!QFile::exists(h.marker(QStringLiteral("extractor-response-1.json"))));
        QVERIFY(SetEvent(h.node_gates[0].value));
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("extractor-response-1.json"))));
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("node-2.output-ready"))));
        const NativeHandle second_node{processHandle(h.report(QStringLiteral("node-2.pid.json")))};
        QVERIFY(second_node.value);
        QCOMPARE(WaitForSingleObject(second_node.value, 0), DWORD{WAIT_TIMEOUT});
        pump(process, result);
        QVERIFY(result.output.isEmpty());
        QVERIFY(!QFile::exists(h.marker(QStringLiteral("extractor-response-2.json"))));
        QVERIFY(SetEvent(h.node_gates[1].value));
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("extractor-final-ready"))));
        QCOMPARE(WaitForSingleObject(extractor.value, 0), DWORD{WAIT_TIMEOUT});
        pump(process, result);
        QVERIFY(result.output.isEmpty());
        QVERIFY(!h.lease.expired());
        QVERIFY(SetEvent(h.extractor_final_gate.value));
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.update.error, Failure::None);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        const auto decoded = decodeYoutubeAudioResolution(result.output, video_id);
        QCOMPARE(decoded.error, Failure::None);
        QVERIFY(decoded.audio);
        QCOMPARE(decoded.audio->title, QStringLiteral("Synthetic broker song"));
        QVERIFY(!result.output.contains("challenge-") && !result.output.contains("pppppppp"));
        QCOMPARE(WaitForSingleObject(extractor.value, 0), DWORD{WAIT_OBJECT_0});
        QCOMPARE(WaitForSingleObject(first_node.value, 0), DWORD{WAIT_OBJECT_0});
        QCOMPARE(WaitForSingleObject(second_node.value, 0), DWORD{WAIT_OBJECT_0});
        verifyReport(h, QStringLiteral("extractor.pid.json"), QStringLiteral("extractor"), {video_id});
        verifyReport(h, QStringLiteral("node-1.pid.json"), QStringLiteral("node"),
                     {QStringLiteral("--permission"), QStringLiteral("-")});
        verifyReport(h, QStringLiteral("node-2.pid.json"), QStringLiteral("node"),
                     {QStringLiteral("--permission"), QStringLiteral("-")});
        QCOMPARE(h.preparations, 1);
        QVERIFY(h.lease.expired());
        QVERIFY(!QDir(h.runtime_directory).exists());
        h.verifySources();
        const auto stable = process.poll(0);
        QCOMPARE(stable.state, State::Exited);
        QVERIFY(stable.stdout_chunk.isEmpty());
    }
    void acknowledgedFinalProjectionFeedsTheExistingOuterResolverSession() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("two-groups")));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        YoutubeResolverSession session(process);
        QVERIFY(session.start("broker_job", video_id, 0, 1'900'000'000));
        QElapsedTimer elapsed;
        elapsed.start();
        auto result = session.snapshot();
        do {
            QCoreApplication::processEvents();
            result = session.poll(0, 1'900'000'000);
            if (result.state == YoutubeResolverState::Ready || result.state == YoutubeResolverState::Failed)
                break;
            QThread::msleep(1);
        } while (elapsed.elapsed() < 10'000);
        QCOMPARE(result.state, YoutubeResolverState::Ready);
        QCOMPARE(result.error, Failure::None);
        QVERIFY(result.audio);
        QCOMPARE(result.audio->jobId(), std::string{"broker_job"});
        QCOMPARE(result.audio->videoId(), video_id);
        QCOMPARE(result.audio->expiresAtSeconds(), qint64{2'000'000'000});
        QVERIFY(h.lease.expired());
        QVERIFY(!QDir(h.runtime_directory).exists());
    }
    void trustedEnvironmentRemovesInterpreterControlsAndPreservesRequiredPrivateValues() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("environment")));
        for (const auto* key : {"NODE_OPTIONS", "NODE_PATH", "PYTHONPATH", "PYTHONHOME", "PYTHONUSERBASE",
                                "PYTHONSTARTUP", "PYTHONINSPECT", "PYTHONBREAKPOINT"})
            h.environment.insert(QString::fromLatin1(key), QStringLiteral("PRIVATE_UNTRUSTED_INTERPRETER_CONTROL"));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Exited);
        QCOMPARE(result.update.error, Failure::None);
        for (const auto& leaf : {QStringLiteral("extractor.pid.json"), QStringLiteral("node-1.pid.json")}) {
            const auto report = h.report(leaf);
            QVERIFY(report.value("controlsAbsent").toBool());
            QCOMPARE(report.value("path").toString(), h.environment.value(QStringLiteral("PATH")));
            QCOMPARE(report.value("systemRoot").toString(), h.environment.value(QStringLiteral("SystemRoot")));
            QCOMPARE(report.value("testCase").toString(), QStringLiteral("environment"));
            QCOMPARE(report.value("markerRoot").toString(), h.markers);
        }
        QVERIFY(!result.output.contains("PRIVATE_UNTRUSTED_INTERPRETER_CONTROL"));
        QVERIFY(h.lease.expired());
    }
    void invalidOrUnsuccessfulWorkersNeverForwardAResponseOrProjection_data() {
        QTest::addColumn<QString>("test_case");
        QTest::addColumn<int>("failure");
        const auto row = [](const char* name, const Failure failure) {
            QTest::newRow(name) << QString::fromLatin1(name) << static_cast<int>(failure);
        };
        row("node-nonzero", Failure::Unavailable);
        row("node-malformed", Failure::InvalidResult);
        row("node-error", Failure::InvalidResult);
        row("node-extra-field", Failure::InvalidResult);
        row("node-oversize", Failure::LimitExceeded);
        row("malformed-frame", Failure::InvalidResult);
        row("partial-frame", Failure::InvalidResult);
        row("oversized-frame", Failure::LimitExceeded);
        row("wrong-sequence", Failure::InvalidResult);
        row("early-final", Failure::InvalidResult);
        row("failure-restricted", Failure::Restricted);
        row("final-oversize", Failure::LimitExceeded);
        row("final-nonzero", Failure::Unavailable);
    }
    void invalidOrUnsuccessfulWorkersNeverForwardAResponseOrProjection() {
        QFETCH(QString, test_case);
        QFETCH(int, failure);
        Fixture h;
        QVERIFY(h.initialize(test_case));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, static_cast<Failure>(failure));
        QVERIFY(result.output.isEmpty());
        QVERIFY(!result.errors.contains("PRIVATE_NODE_DIAGNOSTIC"));
        QVERIFY(!QFile::exists(h.marker(QStringLiteral("extractor-response-1.json"))));
        QVERIFY(h.lease.expired());
        QVERIFY(!QDir(h.runtime_directory).exists());
        h.verifySources();
    }
    void groupAndAggregateTrafficQuotasAreEnforced_data() {
        QTest::addColumn<QString>("test_case");
        QTest::newRow("ninth group") << QStringLiteral("too-many-groups");
        QTest::newRow("64 MiB wire budget") << QStringLiteral("traffic-quota");
    }
    void groupAndAggregateTrafficQuotasAreEnforced() {
        QFETCH(QString, test_case);
        Fixture h;
        QVERIFY(h.initialize(test_case));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        // This boundary moves 64 MiB through fixed-size nonblocking pipes.
        // Do not add thousands of wall-clock timer sleeps to its fixed wait.
        // Yield still lets the two helpers run; quota/deadline oracles stay.
        QVERIFY2(collect(process, result, 0, 20'000, test_case == QLatin1String("traffic-quota")),
                 qPrintable(h.reports().join(QStringLiteral(", "))));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Failure::LimitExceeded);
        QVERIFY(result.output.isEmpty());
        QVERIFY(!QFile::exists(h.marker(QStringLiteral("node-9.pid.json"))));
        QVERIFY(h.lease.expired());
    }
    void cancellationKeepsTheRuntimeUntilBothRealWorkersAcknowledgeExit() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("single"), true));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("node-1.output-ready"))));
        const NativeHandle extractor{processHandle(h.report(QStringLiteral("extractor.pid.json")))};
        const NativeHandle node{processHandle(h.report(QStringLiteral("node-1.pid.json")))};
        QVERIFY(extractor.value && node.value);
        process.cancel();
        QVERIFY(!h.lease.expired());
        QVERIFY(!process.start(video_id, 0));
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Cancelled);
        QVERIFY(result.output.isEmpty());
        QCOMPARE(WaitForSingleObject(extractor.value, 0), DWORD{WAIT_OBJECT_0});
        QCOMPARE(WaitForSingleObject(node.value, 0), DWORD{WAIT_OBJECT_0});
        QVERIFY(h.lease.expired());
        QVERIFY(!QDir(h.runtime_directory).exists());
    }
    void globalTimeoutAndClockRegressionDrainBothWorkers_data() {
        QTest::addColumn<bool>("backward");
        QTest::newRow("inclusive 60 seconds") << false;
        QTest::newRow("backward monotonic") << true;
    }
    void globalTimeoutAndClockRegressionDrainBothWorkers() {
        QFETCH(bool, backward);
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("single"), true));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("node-1.output-ready"))));
        const NativeHandle extractor{processHandle(h.report(QStringLiteral("extractor.pid.json")))};
        const NativeHandle node{processHandle(h.report(QStringLiteral("node-1.pid.json")))};
        QVERIFY(extractor.value && node.value);
        if (backward) {
            sample(process, result, 10);
            QCOMPARE(result.update.state, State::Running);
        }
        QVERIFY(collect(process, result, backward ? 9 : 60'000));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, backward ? Failure::InvalidResult : Failure::Timeout);
        QVERIFY(result.output.isEmpty());
        QCOMPARE(WaitForSingleObject(extractor.value, 0), DWORD{WAIT_OBJECT_0});
        QCOMPARE(WaitForSingleObject(node.value, 0), DWORD{WAIT_OBJECT_0});
        QVERIFY(h.lease.expired());
    }
    void unchangedRuntimeIsRequiredBeforeExtractorLaunch() {
        Fixture h;
        QVERIFY(h.initialize());
        h.pending = h.prepare();
        QVERIFY(h.pending);
        const auto unknown = h.runtime_directory + QStringLiteral("/unreviewed.dll");
        QVERIFY(writeFile(unknown, QByteArrayLiteral("unknown test fixture")));
        QVERIFY(!h.pending->files->unchanged());
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(!process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Failure::Restricted);
        QVERIFY(h.reports().isEmpty());
        QVERIFY(h.lease.expired());
        QCOMPARE(readFile(unknown), QByteArrayLiteral("unknown test fixture"));
    }
    void unchangedRuntimeIsRequiredAgainBeforeNodeLaunch() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("gate-challenge"), false, true));
        WindowsYoutubeBrokerProcess process([&] { return h.prepare(); });
        DrainGuard guard{&process, h};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        QVERIFY(waitMarker(process, result, h.marker(QStringLiteral("extractor.pid.json"))));
        const NativeHandle extractor{processHandle(h.report(QStringLiteral("extractor.pid.json")))};
        QVERIFY(extractor.value);
        const auto unknown = h.runtime_directory + QStringLiteral("/javascript/unreviewed.dll");
        QVERIFY(writeFile(unknown, QByteArrayLiteral("unknown nested test fixture")));
        QVERIFY(SetEvent(h.extractor_gate.value));
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, State::Failed);
        QCOMPARE(result.update.error, Failure::Restricted);
        QVERIFY(result.output.isEmpty());
        QVERIFY(!QFile::exists(h.marker(QStringLiteral("node-1.pid.json"))));
        QCOMPARE(WaitForSingleObject(extractor.value, 0), DWORD{WAIT_OBJECT_0});
        QVERIFY(h.lease.expired());
        QCOMPARE(readFile(unknown), QByteArrayLiteral("unknown nested test fixture"));
    }
    void destructorCancelsHangingWorkersBeforeReleasingTheirRuntime() {
        Fixture h;
        QVERIFY(h.initialize(QStringLiteral("single"), true));
        auto process = std::make_unique<WindowsYoutubeBrokerProcess>([&] { return h.prepare(); });
        DrainGuard guard{process.get(), h};
        QVERIFY(process->start(video_id, 0));
        Collected result;
        QVERIFY(waitMarker(*process, result, h.marker(QStringLiteral("node-1.output-ready"))));
        const NativeHandle extractor{processHandle(h.report(QStringLiteral("extractor.pid.json")))};
        const NativeHandle node{processHandle(h.report(QStringLiteral("node-1.pid.json")))};
        QVERIFY(extractor.value && node.value);
        guard.process = nullptr;
        process.reset();
        if (!h.lease.expired())
            h.parent.setAutoRemove(false);
        QCOMPARE(WaitForSingleObject(extractor.value, 0), DWORD{WAIT_OBJECT_0});
        QCOMPARE(WaitForSingleObject(node.value, 0), DWORD{WAIT_OBJECT_0});
        QVERIFY(h.lease.expired());
        QVERIFY(!QDir(h.runtime_directory).exists());
    }
};
QTEST_GUILESS_MAIN(WindowsYoutubeBrokerProcessTests)
#include "WindowsYoutubeBrokerProcessTests.moc"

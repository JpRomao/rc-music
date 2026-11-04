// Manual host-changing probe. Only --self-test is suitable for automatic CTest.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <userenv.h>
#include <winsock2.h>

#include "AppContainerProbeProtocol.hpp"
#include "adapters/WindowsDownloadAppContainer.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadReadInput.hpp"
#include "adapters/WindowsLocalMediaConverter.hpp"
#include "adapters/WindowsMediaFoundationLaunch.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace music_player::m0;
namespace protocol = music_player::sandbox_probe;
namespace {
enum class Mode { Refuse, Help, SelfTest, HostProbe };
struct Options final {
    Mode mode{Mode::Refuse};
    QString evidence;
};
bool mayRemoveProfile(bool created, bool native_acks) { return created && native_acks; }
bool confirmedProfileCleanup(bool create_succeeded, bool remains_owned, bool native_acks) {
    return create_succeeded && !remains_owned && native_acks;
}
QString workspace() { return QDir::fromNativeSeparators(QString::fromUtf8(RC_MUSIC_SANDBOX_WORKSPACE_ROOT)); }

// Lexical parsing only. These routines never inspect files, tokens or profiles.
bool evidencePath(const QString& original) {
    const auto path = QDir::fromNativeSeparators(original);
    if (!path.isValidUtf16() || path.size() > 4096 ||
        !path.startsWith(workspace() + QStringLiteral("/out/product/"), Qt::CaseInsensitive))
        return false;
    const auto components = path.mid(3).split(QLatin1Char('/'));
    for (const auto& component : components) {
        const auto stem = component.section(QLatin1Char('.'), 0, 0).trimmed().toLower();
        const bool serial = stem.size() == 4 &&
                            (stem.startsWith(QStringLiteral("com")) || stem.startsWith(QStringLiteral("lpt"))) &&
                            ((stem[3] >= QLatin1Char('1') && stem[3] <= QLatin1Char('9')) ||
                             stem[3].unicode() == 0x00b9 || stem[3].unicode() == 0x00b2 || stem[3].unicode() == 0x00b3);
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..") || serial ||
            stem == QStringLiteral("con") || stem == QStringLiteral("prn") || stem == QStringLiteral("aux") ||
            stem == QStringLiteral("nul") || stem == QStringLiteral("conin$") || stem == QStringLiteral("conout$") ||
            component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')))
            return false;
        for (const auto unit : component)
            if (unit.unicode() < 32 || QStringLiteral(":<>\"|?*").contains(unit))
                return false;
    }
    return protocol::validPath(path.toStdWString());
}
Options parse(const QStringList& arguments) {
    if (arguments == QStringList{QStringLiteral("--help")})
        return {Mode::Help, {}};
    if (arguments == QStringList{QStringLiteral("--self-test")})
        return {Mode::SelfTest, {}};
    if (arguments.size() == 3 && arguments[0] == QStringLiteral("--allow-profile-changes") &&
        arguments[1] == QStringLiteral("--evidence-dir") && evidencePath(arguments[2]))
        return {Mode::HostProbe, QDir::fromNativeSeparators(arguments[2])};
    return {};
}
int selfTest() {
    const auto evidence = workspace() + QStringLiteral("/out/product/sandbox-parse-only");
    const std::array invalid{
        QStringList{},
        QStringList{QStringLiteral("--allow-profile-changes")},
        QStringList{QStringLiteral("--evidence-dir"), evidence},
        QStringList{QStringLiteral("--self-test"), QStringLiteral("--allow-profile-changes")},
        QStringList{QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), evidence,
                    QStringLiteral("--profile-name"), QStringLiteral("existing-profile")},
        QStringList{QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), QStringLiteral(".")},
        QStringList{QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"),
                    workspace() + QStringLiteral("/out/product/../escape")},
        QStringList{QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"),
                    QStringLiteral("//server/share/evidence")}};
    for (const auto& arguments : invalid)
        if (parse(arguments).mode != Mode::Refuse)
            return 1;
    if (parse({QStringLiteral("--help")}).mode != Mode::Help ||
        parse({QStringLiteral("--self-test")}).mode != Mode::SelfTest ||
        parse({QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), evidence}).mode !=
            Mode::HostProbe)
        return 2;
    protocol::Request request;
    request.sid = std::vector<std::uint8_t>(44, 1);
    request.input_handle = 4;
    request.port = 12345;
    request.sentinel = L"C:/private/sentinel.bin";
    request.runtime = L"C:/private/runtime";
    auto wire = protocol::encode(request);
    if (protocol::decodeRequest(wire) != request)
        return 3;
    wire.push_back(0);
    if (protocol::decodeRequest(wire))
        return 4;
    wire = protocol::encode(request);
    wire[8] = 2;
    if (protocol::decodeRequest(wire))
        return 5;
    request.input_handle = 0;
    if (!protocol::encode(request).empty())
        return 6;
    request.input_handle = std::numeric_limits<std::uint64_t>::max();
    if (!protocol::encode(request).empty())
        return 7;
    protocol::Report report{.passed = protocol::all_checks, .capability_count = 0, .integrity_rid = 4096};
    auto reply = protocol::encode(report);
    if (protocol::decodeReport(reply) != report)
        return 8;
    reply.push_back(0);
    if (protocol::decodeReport(reply))
        return 9;
    int failures = 0;
    for (const auto* component : {"NUL", "CON.txt", "COM1"}) {
        const auto path = workspace() + QStringLiteral("/out/product/") + QString::fromLatin1(component);
        if (parse({QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), path}).mode !=
            Mode::Refuse) {
            std::printf("FAIL parser accepts reserved DOS component: %s\n", component);
            ++failures;
        }
    }
    if (mayRemoveProfile(true, false)) {
        std::puts("FAIL cleanup permits deleting an owned profile without native ACKs");
        ++failures;
    }
    if (mayRemoveProfile(false, false) || mayRemoveProfile(false, true) || !mayRemoveProfile(true, true)) {
        std::puts("FAIL cleanup ownership/ACK truth table");
        ++failures;
    }
    if (confirmedProfileCleanup(false, false, true) || confirmedProfileCleanup(false, true, true) ||
        confirmedProfileCleanup(false, false, false) || confirmedProfileCleanup(false, true, false)) {
        std::puts("FAIL failed profile creation is reported as confirmed cleanup");
        ++failures;
    }
    if (!confirmedProfileCleanup(true, false, true) || confirmedProfileCleanup(true, true, true) ||
        confirmedProfileCleanup(true, false, false) || confirmedProfileCleanup(true, true, false)) {
        std::puts("FAIL confirmed cleanup requires successful creation, removal and native ACKs");
        ++failures;
    }
    if (failures)
        return 20 + failures;
    std::puts("Sandbox probe parser/protocol self-test passed; no host operations performed.");
    return 0;
}

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}
// Mark uncertainty before entering a native owner's constructor/start. A throw
// or unacknowledged deadline leaves the aggregate false. No later case may run
// and no destructor/cleanup may turn an uncertain run into known completion.
class NativeAcknowledgement final {
  public:
    explicit NativeAcknowledgement(bool& all) : all_(all), earlier_(all) {
        require(all, "native ACK uncertain; further launches refused");
        all_ = false;
    }
    void complete(bool terminal) { all_ = earlier_ && terminal; }

  private:
    bool& all_;
    bool earlier_;
};
QByteArray hashFile(const QString& path) {
    QFile file{path};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
QStringList entries(const QString& directory) {
    return QDir{directory}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}
bool receipt(const QString& directory, const QString& leaf, QJsonObject object) {
    object.insert(QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    const auto bytes = QJsonDocument{object}.toJson();
    QFile file{directory + QLatin1Char('/') + leaf};
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() && file.flush();
}
void pump() {
    QCoreApplication::processEvents();
    QThread::msleep(5);
}
class Profile final {
  public:
    const QString name{QStringLiteral("RCMusic.Test.") + QUuid::createUuid().toString(QUuid::WithoutBraces)};
    const std::wstring native_name{name.toStdWString()};
    bool created{false};
    bool native_acks{true};
    bool cleanup_attempted{false};
    bool sid_freed{true};
    QByteArray sid;
    ~Profile() {
        // Exception fallback only. The last durable receipt remains uncertain;
        // this cannot manufacture a passing cleanup receipt.
        if (mayRemoveProfile(created, native_acks) && !cleanup_attempted)
            static_cast<void>(DeleteAppContainerProfile(native_name.c_str()));
    }
    HRESULT create() {
        PSID raw = nullptr;
        const auto result =
            CreateAppContainerProfile(native_name.c_str(), L"RC Music isolated test",
                                      L"Explicit temporary media-worker isolation probe", nullptr, 0, &raw);
        created = result == S_OK;
        struct SidGuard final {
            PSID value;
            bool& freed;
            ~SidGuard() {
                if (value)
                    freed = FreeSid(value) == nullptr;
            }
        } guard{raw, sid_freed};
        if (created && raw && IsValidSid(raw)) {
            const auto length = GetLengthSid(raw);
            if (length <= SECURITY_MAX_SID_SIZE)
                sid = QByteArray{static_cast<const char*>(raw), static_cast<qsizetype>(length)};
        }
        return result;
    }
    HRESULT remove() {
        if (!mayRemoveProfile(created, native_acks))
            return S_FALSE;
        cleanup_attempted = true;
        const auto result = DeleteAppContainerProfile(native_name.c_str());
        if (result == S_OK)
            created = false;
        return result;
    }
};

// The listener stays alive during the sandbox attempt. A successful parent
// connection is required first, so refusal cannot mean "no server existed".
class Loopback final {
  public:
    ~Loopback() {
        if (listener_ != INVALID_SOCKET)
            closesocket(listener_);
        if (started_)
            WSACleanup();
    }
    bool start() {
        WSADATA data{};
        started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!started_)
            return false;
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == INVALID_SOCKET)
            return false;
        BOOL exclusive = TRUE;
        if (setsockopt(listener_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                       sizeof(exclusive)) != 0)
            return false;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener_, 2) != 0)
            return false;
        int size = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) != 0)
            return false;
        port = ntohs(address.sin_port);
        const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET)
            return false;
        u_long nonblocking = 1;
        bool connected = false;
        if (ioctlsocket(client, FIONBIO, &nonblocking) == 0) {
            const auto result = connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            connected = result == 0;
            if (!connected && WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set writes, errors;
                FD_ZERO(&writes);
                FD_ZERO(&errors);
                FD_SET(client, &writes);
                FD_SET(client, &errors);
                timeval timeout{2, 0};
                int error = 0, length = sizeof(error);
                connected = select(0, nullptr, &writes, &errors, &timeout) > 0 &&
                            getsockopt(client, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0 &&
                            error == 0;
            }
        }
        if (connected) {
            const auto accepted = accept(listener_, nullptr, nullptr);
            connected = accepted != INVALID_SOCKET;
            if (accepted != INVALID_SOCKET)
                closesocket(accepted);
        }
        closesocket(client);
        return connected;
    }
    std::uint16_t port{};

  private:
    bool started_{false};
    SOCKET listener_{INVALID_SOCKET};
};

QJsonObject runHelper(const QString& root, const std::shared_ptr<const WindowsDownloadAppContainer>& container,
                      std::uint16_t port, bool& passed, bool& native_acks) {
    require(native_acks, "helper refused after uncertain native ACK");
    const auto private_parent = root + QStringLiteral("/sentinel");
    const auto runtimes = root + QStringLiteral("/helper-runtimes");
    require(download_artifacts::createPrivateDirectory(private_parent) && QDir{}.mkdir(runtimes), "helper parents");
    const auto sentinel = private_parent + QStringLiteral("/sentinel.bin");
    const auto challenge = QUuid::createUuid().toRfc4122() + QUuid::createUuid().toRfc4122();
    {
        QFile file{sentinel};
        require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(challenge) == challenge.size() &&
                    file.flush(),
                "sentinel creation");
    }
    const auto before = hashFile(sentinel);
    download_artifacts::Handle original{CreateFileW(download_artifacts::nativePath(sentinel).c_str(), GENERIC_READ,
                                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    require(original.valid(), "sentinel parent handle");
    auto input = WindowsDownloadReadInput::fromHandle(reinterpret_cast<std::uintptr_t>(original.get()));
    require(input != nullptr, "sentinel read capability");
    WindowsMediaFoundationLaunchConfig launch;
    launch.runtime_parent = runtimes;
    launch.source_executable = QString::fromUtf8(RC_MUSIC_TEST_SANDBOX_HELPER_PATH);
    launch.source_sha256 = hashFile(launch.source_executable);
    launch.app_container = container;
    launch.require_app_container = true;
    auto runtime = WindowsMediaFoundationLaunch::prepare(launch);
    require(runtime && runtime->unchanged(), "helper runtime preparation");
    protocol::Request request;
    const auto& sid = container->sidBytes();
    request.sid.assign(reinterpret_cast<const std::uint8_t*>(sid.constData()),
                       reinterpret_cast<const std::uint8_t*>(sid.constData()) + sid.size());
    request.input_handle = input->handleValue();
    std::copy(challenge.begin(), challenge.end(), request.challenge.begin());
    request.sentinel = sentinel.toStdWString();
    request.runtime = runtime->directory().toStdWString();
    request.port = port;
    const auto wire = protocol::encode(request);
    require(!wire.empty(), "helper request encoding");
    auto config = runtime->processConfig();
    require(config.app_container == container && config.require_app_container, "helper runtime lost isolation policy");
    config.read_input = input;
    config.standard_input = QByteArray{reinterpret_cast<const char*>(wire.data()), static_cast<qsizetype>(wire.size())};
    config.max_stdout_bytes = 512;
    config.max_stderr_bytes = 1024;
    config.max_memory_bytes = 64 * 1024 * 1024;
    config.total_timeout_ms = 5000;
    QByteArray output, errors;
    WindowsDownloadProcessUpdate update;
    bool started = false;
    struct Owners final {
        std::shared_ptr<const WindowsMediaFoundationLaunch> runtime;
        WindowsDownloadProcess process;
    };
    NativeAcknowledgement acknowledgement{native_acks};
    auto owners = std::make_unique<Owners>();
    owners->runtime = runtime;
    const auto preserve_uncertain = qScopeGuard([&] {
        // Probe-only intentional retention until OS process exit: do not run
        // owner/runtime destructors or cleanup without the required ACK.
        if (!native_acks)
            static_cast<void>(owners.release());
    });
    {
        auto& process = owners->process;
        QElapsedTimer timer;
        timer.start();
        started = process.start(config, 0);
        do {
            update = process.poll(timer.elapsed());
            output += update.stdout_chunk;
            errors += update.stderr_chunk;
            if (update.state == WindowsDownloadProcessState::Exited ||
                update.state == WindowsDownloadProcessState::Failed ||
                update.state == WindowsDownloadProcessState::Cancelled)
                break;
            if (timer.elapsed() > 7000)
                process.cancel();
            pump();
        } while (timer.elapsed() < 10'000);
    }
    const auto report = protocol::decodeReport(
        {reinterpret_cast<const std::uint8_t*>(output.constData()), static_cast<std::size_t>(output.size())});
    const bool ack = update.state == WindowsDownloadProcessState::Exited ||
                     update.state == WindowsDownloadProcessState::Failed ||
                     update.state == WindowsDownloadProcessState::Cancelled;
    acknowledgement.complete(ack && (!report || report->errors[5] != ERROR_TIMEOUT));
    passed = started && ack && update.state == WindowsDownloadProcessState::Exited && update.exit_code == 0 && report &&
             report->passed == protocol::all_checks && report->capability_count == 0 &&
             report->integrity_rid == SECURITY_MANDATORY_LOW_RID && hashFile(sentinel) == before &&
             errors == QByteArray{"sandbox-probe=complete\n"} && runtime->unchanged();
    QJsonArray codes;
    if (report)
        for (const auto code : report->errors)
            codes.append(static_cast<qint64>(code));
    QJsonObject result{{"passed", passed},
                       {"started", started},
                       {"physical_ack", ack},
                       {"all_native_acks", native_acks},
                       {"process_state", static_cast<int>(update.state)},
                       {"process_error", static_cast<int>(update.error)},
                       {"stdout_bytes", output.size()},
                       {"stderr_bytes", errors.size()},
                       {"checks", report ? static_cast<qint64>(report->passed) : 0},
                       {"win32_errors", codes},
                       {"sentinel_sha256_before", QString::fromLatin1(before)},
                       {"sentinel_sha256_after", QString::fromLatin1(hashFile(sentinel))},
                       {"runtime", runtime->directory()}};
    if (!native_acks) {
        result.insert(QStringLiteral("passed"), false);
        result.insert(QStringLiteral("runtime_cleanup"), false);
        passed = false;
        return result;
    }
    owners.reset();
    config.read_input.reset();
    config.app_container.reset();
    input.reset();
    runtime.reset();
    const bool clean = entries(runtimes).isEmpty();
    result.insert(QStringLiteral("runtime_cleanup"), clean);
    passed = passed && clean;
    result.insert(QStringLiteral("passed"), passed);
    return result;
}

bool runMedia(const QString& root, const std::shared_ptr<const WindowsDownloadAppContainer>& container,
              QJsonArray& rows, bool& native_acks) {
    require(native_acks, "media refused after uncertain native ACK");
    const auto runtimes = root + QStringLiteral("/media-runtimes");
    const auto staging = root + QStringLiteral("/staging");
    const auto publication = root + QStringLiteral("/publication");
    require(QDir{}.mkdir(runtimes) && QDir{}.mkdir(staging) && QDir{}.mkdir(publication), "media parents");
    WindowsMediaFoundationLaunchConfig launch;
    launch.runtime_parent = runtimes;
    launch.source_executable = QString::fromUtf8(RC_MUSIC_TEST_MF_WORKER_PATH);
    launch.source_sha256 = hashFile(launch.source_executable);
    launch.app_container = container;
    launch.require_app_container = true;
    auto runtime = WindowsMediaFoundationLaunch::prepare(launch);
    require(runtime && runtime->unchanged(), "media runtime preparation");
    const std::array<std::pair<const char*, const char*>, 3> fixtures{
        {{"mp3-unicode-tags.mp3", "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000"},
         {"m4a-aac-lc.m4a", "aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558"},
         {"wav-pcm-mono.wav", "03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f"}}};
    bool all_passed = true;
    for (const auto& [leaf, expected_hash] : fixtures) {
        require(native_acks, "next media case refused after uncertain native ACK");
        const auto path =
            QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + QLatin1Char('/') + QString::fromLatin1(leaf);
        require(hashFile(path) == QByteArray{expected_hash}, "fixture hash mismatch");
        QFile file{path};
        require(file.open(QIODevice::ReadOnly), "fixture read");
        const auto bytes = file.readAll();
        struct Owners final {
            std::shared_ptr<const WindowsMediaFoundationLaunch> runtime;
            std::unique_ptr<WindowsDownloadWorkspace> workspace;
            std::unique_ptr<WindowsLocalMediaConverter> converter;
            std::unique_ptr<WindowsLocalMp3Validator> validator;
        };
        auto owners = std::make_unique<Owners>();
        owners->runtime = runtime;
        owners->workspace = std::make_unique<WindowsDownloadWorkspace>(
            WindowsDownloadWorkspaceConfig{staging, publication, 2 * 1024 * 1024});
        const auto preserve_uncertain = qScopeGuard([&] {
            if (!native_acks)
                static_cast<void>(owners.release());
        });
        auto& workspace = *owners->workspace;
        require(workspace.create() && workspace.inputSink() && workspace.inputSink()->write(bytes) == bytes.size() &&
                    workspace.sealInput(),
                "media workspace");
        LocalMediaConversionUpdate conversion;
        LocalMp3ValidationUpdate validation;
        QElapsedTimer timer;
        timer.start();
        bool started = false, validated = false;
        {
            WindowsLocalMediaConverterConfig config;
            config.backend = WindowsLocalMediaBackend::MediaFoundation;
            config.prepared_launch = runtime;
            config.total_timeout_ms = 15'000;
            NativeAcknowledgement acknowledgement{native_acks};
            owners->converter = std::make_unique<WindowsLocalMediaConverter>(config);
            auto& converter = *owners->converter;
            started = converter.start(workspace, 0);
            do {
                conversion = converter.poll(timer.elapsed());
                if (conversion.state == LocalMediaConversionState::Validated ||
                    conversion.state == LocalMediaConversionState::Failed ||
                    conversion.state == LocalMediaConversionState::Cancelled)
                    break;
                if (timer.elapsed() > 17'000)
                    converter.cancel();
                pump();
            } while (timer.elapsed() < 20'000);
            acknowledgement.complete(conversion.state == LocalMediaConversionState::Validated ||
                                     conversion.state == LocalMediaConversionState::Failed ||
                                     conversion.state == LocalMediaConversionState::Cancelled);
        }
        if (!native_acks) {
            rows.append(QJsonObject{{"fixture", QString::fromLatin1(leaf)},
                                    {"passed", false},
                                    {"all_native_acks", false},
                                    {"workspace_cleanup", false}});
            return false;
        }
        owners->converter.reset();
        if (conversion.state == LocalMediaConversionState::Validated) {
            WindowsLocalMp3ValidatorConfig config;
            config.backend = WindowsLocalMediaBackend::MediaFoundation;
            config.prepared_launch = runtime;
            config.total_timeout_ms = 15'000;
            NativeAcknowledgement acknowledgement{native_acks};
            owners->validator = std::make_unique<WindowsLocalMp3Validator>(config);
            auto& validator = *owners->validator;
            timer.restart();
            const bool accepted = validator.startReadInput(workspace.readOutput(), 0);
            do {
                validation = validator.poll(timer.elapsed());
                if (validation.state == LocalMp3ValidationState::Validated ||
                    validation.state == LocalMp3ValidationState::Failed ||
                    validation.state == LocalMp3ValidationState::Cancelled)
                    break;
                if (timer.elapsed() > 17'000)
                    validator.cancel();
                pump();
            } while (timer.elapsed() < 20'000);
            acknowledgement.complete(validation.state == LocalMp3ValidationState::Validated ||
                                     validation.state == LocalMp3ValidationState::Failed ||
                                     validation.state == LocalMp3ValidationState::Cancelled);
            validated = accepted && validation.state == LocalMp3ValidationState::Validated;
        }
        if (!native_acks) {
            rows.append(QJsonObject{{"fixture", QString::fromLatin1(leaf)},
                                    {"passed", false},
                                    {"all_native_acks", false},
                                    {"workspace_cleanup", false}});
            return false;
        }
        owners->validator.reset();
        const auto output_hash = hashFile(workspace.outputPath());
        const bool clean = workspace.cleanup();
        const bool passed = started && conversion.state == LocalMediaConversionState::Validated && validated && clean &&
                            output_hash.size() == 64 && runtime->unchanged();
        rows.append(QJsonObject{{"fixture", QString::fromLatin1(leaf)},
                                {"fixture_sha256", QString::fromLatin1(expected_hash)},
                                {"passed", passed},
                                {"all_native_acks", native_acks},
                                {"conversion_state", static_cast<int>(conversion.state)},
                                {"conversion_error", static_cast<int>(conversion.error)},
                                {"independent_validation_state", static_cast<int>(validation.state)},
                                {"output_sha256", QString::fromLatin1(output_hash)},
                                {"workspace_cleanup", clean}});
        all_passed = all_passed && passed;
    }
    runtime.reset();
    return all_passed && entries(runtimes).isEmpty() && entries(staging).isEmpty() && entries(publication).isEmpty();
}

int hostProbe(const Options& options) {
    std::vector<download_artifacts::Handle> parents;
    require(download_artifacts::pinAncestors(options.evidence, parents), "evidence ancestors unavailable/reparse");
    require(download_artifacts::createPrivateDirectory(options.evidence), "evidence must be a fresh directory");
    Profile profile;
    QTemporaryDir temporary{QDir::tempPath() + QStringLiteral("/RCMusic-AppContainer-Probe-XXXXXX")};
    temporary.setAutoRemove(false); // Preserve uncertain resources on every failure.
    bool passed = false;
    QJsonObject results;
    HRESULT creation = E_FAIL;
    bool creation_attempted = false;
    try {
        const QJsonObject plan{
            {"schema_version", 1},
            {"profile_name", profile.name},
            {"capabilities", QJsonArray{}},
            {"mode", "common-AppContainer-not-LPAC"},
            {"temporary", temporary.path()},
            {"windows_kernel", QSysInfo::kernelVersion()},
            {"driver_sha256", QString::fromLatin1(hashFile(QCoreApplication::applicationFilePath()))},
            {"helper_sha256", QString::fromLatin1(hashFile(QString::fromUtf8(RC_MUSIC_TEST_SANDBOX_HELPER_PATH)))},
            {"media_worker_sha256", QString::fromLatin1(hashFile(QString::fromUtf8(RC_MUSIC_TEST_MF_WORKER_PATH)))},
            {"limitations", "Own profile storage and OS DLLs remain allowed; child denial tests the Job. Local "
                            "fixtures/loopback only; no universal FS/registry, LPAC or production approval."}};
        require(receipt(options.evidence, QStringLiteral("00-before-profile.json"), plan), "initial receipt");
        require(temporary.isValid(), "temporary directory");
        Loopback loopback;
        require(loopback.start(), "loopback parent positive control failed");
        creation_attempted = true;
        creation = profile.create();
        require(receipt(options.evidence, QStringLiteral("01-profile-result.json"),
                        {{"profile_name", profile.name},
                         {"creation_attempted", creation_attempted},
                         {"create_hresult", static_cast<qint64>(creation)},
                         {"created_by_this_run", profile.created},
                         {"sid_freed", profile.sid_freed},
                         {"sid_hex", QString::fromLatin1(profile.sid.toHex())}}),
                "profile receipt");
        // Never derive/reuse an existing profile, including ERROR_ALREADY_EXISTS.
        require(creation == S_OK && profile.created && profile.sid_freed,
                "profile creation did not exclusively succeed");
        auto container = WindowsDownloadAppContainer::fromTrustedSid(profile.sid);
        require(container != nullptr, "profile SID capability");
        bool helper_passed = false;
        const auto helper = runHelper(temporary.path(), container, loopback.port, helper_passed, profile.native_acks);
        require(receipt(options.evidence, QStringLiteral("02-helper-result.json"), helper), "helper receipt");
        require(profile.native_acks, "helper ACK uncertain; remaining cases/profile cleanup refused");
        QJsonArray media;
        const bool media_passed = runMedia(temporary.path(), container, media, profile.native_acks);
        results = {{"helper_passed", helper_passed}, {"media_passed", media_passed}, {"media", media}};
        passed = helper_passed && media_passed;
    } catch (const std::exception& error) {
        results.insert(QStringLiteral("failure"), QString::fromLatin1(error.what()));
    } catch (...) {
        results.insert(QStringLiteral("failure"), QStringLiteral("unexpected exception"));
    }
    // Only the explicit ACK aggregate permits cleanup. Destruction or elapsed
    // deadlines cannot authorize removal. An uncertain owner is retained until
    // this driver exits and the owned profile/TEMP are preserved for review.
    QJsonArray deletions;
    for (int attempt = 0; mayRemoveProfile(profile.created, profile.native_acks) && attempt < 2; ++attempt)
        deletions.append(static_cast<qint64>(profile.remove()));
    const bool cleanup = confirmedProfileCleanup(creation == S_OK, profile.created, profile.native_acks);
    const bool cleanup_receipt =
        receipt(options.evidence, QStringLiteral("04-profile-cleanup.json"),
                {{"profile_name", profile.name},
                 {"creation_attempted", creation_attempted},
                 {"created_by_this_run", creation == S_OK},
                 {"delete_hresults", deletions},
                 {"cleanup_confirmed_by_api", cleanup},
                 {"all_native_acks", profile.native_acks},
                 {"remaining_status", !creation_attempted    ? "creation-not-attempted-no-delete"
                                      : creation != S_OK     ? "creation-failed-state-uncertain-no-delete"
                                      : !profile.native_acks ? "native-ACK-uncertain-profile-preserved"
                                      : profile.created      ? "undetermined-preserve-and-review"
                                                             : "API-complete"}});
    results.insert(QStringLiteral("passed"), passed && cleanup && cleanup_receipt);
    results.insert(QStringLiteral("profile_cleanup"), cleanup);
    results.insert(QStringLiteral("creation_attempted"), creation_attempted);
    results.insert(QStringLiteral("all_native_acks"), profile.native_acks);
    results.insert(QStringLiteral("temporary"), temporary.path());
    const bool result_receipt = receipt(options.evidence, QStringLiteral("03-results.json"), results);
    const bool complete = passed && profile.native_acks && cleanup && cleanup_receipt && result_receipt;
    if (complete)
        temporary.setAutoRemove(true);
    return complete ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    QStringList arguments;
    for (int index = 1; index < argc; ++index)
        arguments.push_back(QString::fromWCharArray(argv[index]));
    const auto options = parse(arguments);
    if (options.mode == Mode::Help) {
        std::puts("AppContainer probe: --self-test | --help | --allow-profile-changes --evidence-dir <fresh workspace "
                  "out/product directory>");
        return 0;
    }
    if (options.mode == Mode::SelfTest)
        return selfTest();
    if (options.mode != Mode::HostProbe) {
        std::fputs("Refused: explicit profile-change flag and fresh workspace evidence path are required.\n", stderr);
        return 64;
    }
    int application_argc = 1;
    char name[] = "rc_music_appcontainer_probe";
    char* application_argv[]{name, nullptr};
    QCoreApplication application{application_argc, application_argv};
    try {
        return hostProbe(options);
    } catch (...) {
        std::fputs(
            "Probe failed; inspect the last receipt for profile/cleanup state. No existing profile was reused.\n",
            stderr);
        return 65;
    }
}

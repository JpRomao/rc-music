// Manual host-changing probe. Only --self-test is suitable for automatic CTest.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winsock2.h>

#include <Sddl.h>

#include "AppContainerProbeProtocol.hpp"
#include "adapters/WindowsDownloadAppContainer.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadMediaProfileStore.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"
#include "adapters/WindowsDownloadReadInput.hpp"
#include "adapters/WindowsLocalMediaConverter.hpp"
#include "adapters/WindowsMediaFoundationLaunch.hpp"
#include "adapters/WindowsMediaProfileOwner.hpp"
#include "adapters/WindowsNativeMediaProfile.hpp"

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
    request.udp_port = 12346;
    request.sentinel = L"C:/private/sentinel.bin";
    request.runtime = L"C:/private/runtime";
    auto wire = protocol::encode(request);
    if (protocol::decodeRequest(wire) != request)
        return 3;
    wire.push_back(0);
    if (protocol::decodeRequest(wire))
        return 4;
    wire = protocol::encode(request);
    wire[8] = 3;
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
bool createJournalDirectory(const QString& directory) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token(raw);
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size));
    if (!size || size > 64 * 1024)
        return false;
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
        return false;
    LPWSTR user = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &user))
        return false;
    const auto free_user = qScopeGuard([&] { LocalFree(user); });
    const auto sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(user) + ')';
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto free_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(download_artifacts::nativePath(directory).c_str(), &attributes) != FALSE;
}

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

class UdpLoopback final {
  public:
    ~UdpLoopback() {
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
        listener_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (listener_ == INVALID_SOCKET)
            return false;
        BOOL exclusive = TRUE;
        if (setsockopt(listener_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                       sizeof(exclusive)) != 0)
            return false;
        address_.sin_family = AF_INET;
        address_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener_, reinterpret_cast<const sockaddr*>(&address_), sizeof(address_)) != 0)
            return false;
        int size = sizeof(address_);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address_), &size) != 0)
            return false;
        port = ntohs(address_.sin_port);
        return positiveControl();
    }
    bool positiveControl() const {
        const auto client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (client == INVALID_SOCKET)
            return false;
        const auto close = qScopeGuard([&] { closesocket(client); });
        const auto challenge = QUuid::createUuid().toRfc4122();
        if (sendto(client, challenge.constData(), static_cast<int>(challenge.size()), 0,
                   reinterpret_cast<const sockaddr*>(&address_), sizeof(address_)) != challenge.size())
            return false;
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(listener_, &ready);
        timeval timeout{2, 0};
        if (select(0, &ready, nullptr, nullptr, &timeout) != 1)
            return false;
        std::array<char, 64> received{};
        const auto count = recvfrom(listener_, received.data(), static_cast<int>(received.size()), 0, nullptr, nullptr);
        return count == challenge.size() && QByteArray{received.data(), count} == challenge;
    }
    std::uint16_t port{};

  private:
    bool started_{false};
    SOCKET listener_{INVALID_SOCKET};
    sockaddr_in address_{};
};

QJsonObject runHelper(const QString& root, const std::shared_ptr<const WindowsDownloadAppContainer>& container,
                      std::uint16_t port, std::uint16_t udp_port, bool& passed, bool& native_acks) {
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
    request.udp_port = udp_port;
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
    // Quota is not itself a denial oracle. This separate kernel notification
    // belongs to this supervisor's Job and identifies the active-process limit.
    const bool job_limit_witnessed = report && report->errors[5] == ERROR_NOT_ENOUGH_QUOTA &&
                                     update.job_evidence.active_process_limit &&
                                     !update.job_evidence.process_memory_limit && !update.job_evidence.job_memory_limit;
    const auto effective_checks = report ? report->passed | (job_limit_witnessed ? protocol::JobChildDenied : 0U) : 0U;
    passed = started && ack && update.state == WindowsDownloadProcessState::Exited && update.exit_code == 0 && report &&
             effective_checks == protocol::all_checks && report->capability_count == 0 &&
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
                       {"effective_checks", static_cast<qint64>(effective_checks)},
                       {"job_active_process_limit_witnessed", job_limit_witnessed},
                       {"job_process_memory_limit", update.job_evidence.process_memory_limit},
                       {"job_memory_limit", update.job_evidence.job_memory_limit},
                       {"udp_error", report ? static_cast<qint64>(report->udp_error) : -1},
                       {"udp_explicit_denial", report && report->udp_error == WSAEACCES},
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

QJsonObject runNoChild(const QString& root, const std::shared_ptr<const WindowsDownloadAppContainer>& container,
                       WindowsDownloadMediaProfileStore& journal, bool& native_acks) {
    require(native_acks, "no-child refused after uncertainty");
    const auto parent = root + QStringLiteral("/no-child-runtimes");
    require(QDir{}.mkdir(parent), "no-child parent");
    const auto image = root + QStringLiteral("/deliberately-invalid-image.exe");
    {
        QFile file(image);
        const QByteArray bytes{"RCMusic inert invalid PE fixture"};
        require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() &&
                    file.flush(),
                "invalid image fixture");
    }
    WindowsMediaFoundationLaunchConfig launch;
    launch.runtime_parent = parent;
    launch.source_executable = image;
    launch.source_sha256 = hashFile(image);
    launch.app_container = container;
    launch.require_app_container = true;
    auto runtime = WindowsMediaFoundationLaunch::prepare(launch);
    require(runtime != nullptr, "no-child authenticated runtime");
    struct Owners {
        std::shared_ptr<const WindowsMediaFoundationLaunch> runtime;
        WindowsDownloadProcess process;
    };
    NativeAcknowledgement acknowledgement(native_acks);
    auto owners = std::make_unique<Owners>();
    owners->runtime = runtime;
    const auto preserve = qScopeGuard([&] {
        if (!native_acks)
            static_cast<void>(owners.release());
    });
    const bool started = owners->process.start(runtime->processConfig(), 0);
    const auto update = owners->process.poll(0);
    const bool idle = container->nativeUseFence()->state() == WindowsDownloadNativeUseFence::State::OpenIdle;
    const auto current = journal.load();
    bool passed = !started && update.state == WindowsDownloadProcessState::Failed &&
                  update.error == WindowsDownloadProcessError::Isolation && idle && current.record &&
                  current.record->stage == DownloadMediaProfileStage::InUse;
    acknowledgement.complete(passed);
    if (native_acks) {
        owners.reset();
        runtime.reset();
    }
    const bool clean = native_acks && entries(parent).isEmpty();
    passed = passed && clean;
    return {{"passed", passed},
            {"started", started},
            {"process_state", static_cast<int>(update.state)},
            {"process_error", static_cast<int>(update.error)},
            {"fence_open_idle", idle},
            {"image_existed_and_was_invalid", true},
            {"runtime_cleanup", clean},
            {"all_native_acks", native_acks}};
}

bool expectedWitness(HANDLE process, const QString& image, const WindowsDownloadAppContainer& container) {
    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length) ||
        QDir::fromNativeSeparators(QString::fromWCharArray(path.data(), length))
                .compare(QDir::fromNativeSeparators(image), Qt::CaseInsensitive) != 0)
        return false;
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token(raw);
    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenAppContainerSid, nullptr, 0, &size));
    if (size < sizeof(TOKEN_APPCONTAINER_INFORMATION) || size > 64 * 1024)
        return false;
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(token.get(), TokenAppContainerSid, bytes.data(), size, &size))
        return false;
    auto sid = reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(bytes.data())->TokenAppContainer;
    return sid && IsValidSid(sid) && GetLengthSid(sid) == container.sidBytes().size() &&
           EqualSid(sid, const_cast<char*>(container.sidBytes().constData()));
}

QJsonObject runCancellation(const QString& root, const std::shared_ptr<const WindowsDownloadAppContainer>& container,
                            WindowsMediaProfileOwner& owner, WindowsDownloadMediaProfileStore& journal,
                            WindowsNativeMediaProfile& platform, bool& native_acks) {
    require(native_acks, "cancel scenario refused after uncertainty");
    const auto parent = root + QStringLiteral("/cancel-runtimes");
    require(QDir{}.mkdir(parent), "cancel parent");
    WindowsMediaFoundationLaunchConfig launch;
    launch.runtime_parent = parent;
    launch.source_executable = QString::fromUtf8(RC_MUSIC_TEST_SANDBOX_HELPER_PATH);
    launch.source_sha256 = hashFile(launch.source_executable);
    launch.app_container = container;
    launch.require_app_container = true;
    auto runtime = WindowsMediaFoundationLaunch::prepare(launch);
    require(runtime != nullptr, "cancel runtime");
    auto config = runtime->processConfig();
    const auto nonce = QUuid::createUuid().toRfc4122().toHex();
    config.arguments = {QStringLiteral("--hold-input"), QString::fromLatin1(nonce)};
    config.standard_input = QByteArray(download_process_max_input_bytes, 'x');
    config.max_stdout_bytes = 1024;
    config.max_stderr_bytes = 1024;
    config.total_timeout_ms = 10'000;
    struct Owners {
        std::shared_ptr<const WindowsMediaFoundationLaunch> runtime;
        WindowsDownloadProcess process;
    };
    NativeAcknowledgement acknowledgement(native_acks);
    auto owners = std::make_unique<Owners>();
    owners->runtime = runtime;
    const auto preserve = qScopeGuard([&] {
        if (!native_acks)
            static_cast<void>(owners.release());
    });
    auto& process = owners->process;
    QElapsedTimer timer;
    timer.start();
    require(process.start(config, 0), "hold-input start");
    QByteArray output;
    WindowsDownloadProcessUpdate update;
    while (!output.contains('\n') && timer.elapsed() < 5000) {
        update = process.poll(timer.elapsed());
        output += update.stdout_chunk;
        require(output.size() <= 128, "bounded hold readiness");
        if (update.state != WindowsDownloadProcessState::Running)
            break;
        pump();
    }
    const auto parts = output.trimmed().split(':');
    bool pid_valid = false;
    const auto pid = parts.size() == 3 ? parts[1].toUInt(&pid_valid) : 0U;
    require(parts.size() == 3 && parts[0] == "hold-ready" && parts[2] == nonce && pid_valid && pid != 0 &&
                update.state == WindowsDownloadProcessState::Running,
            "hold readiness identity");
    download_artifacts::Handle witness(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    require(witness.valid() && expectedWitness(witness.get(), config.executable, *container) &&
                WaitForSingleObject(witness.get(), 0) == WAIT_TIMEOUT,
            "running native witness");
    require(container->nativeUseFence()->state() == WindowsDownloadNativeUseFence::State::OpenBusy,
            "ticket busy before cancel");
    process.cancel();
    // Observe kernel exit without polling away the supervisor's pending pipes.
    require(WaitForSingleObject(witness.get(), 5000) == WAIT_OBJECT_0, "native cancellation witness");
    require(container->nativeUseFence()->state() == WindowsDownloadNativeUseFence::State::OpenBusy,
            "native exit is not a ticket ACK");
    require(!owner.close() && owner.state() == WindowsMediaProfileOwnerState::Draining &&
                container->nativeUseFence()->state() == WindowsDownloadNativeUseFence::State::ClosingBusy &&
                !platform.report().remove_attempted,
            "owner must wait for supervisor ACK");
    const auto before_ack = journal.load();
    require(before_ack.record && before_ack.record->stage == DownloadMediaProfileStage::InUse,
            "no cleanup record before ACK");
    WindowsDownloadProcess denied;
    require(!denied.start(config, timer.elapsed()) &&
                denied.poll(timer.elapsed()).error == WindowsDownloadProcessError::Isolation,
            "closed admission must reject another process");
    do {
        update = process.poll(timer.elapsed());
        if (update.state == WindowsDownloadProcessState::Cancelled ||
            update.state == WindowsDownloadProcessState::Failed || update.state == WindowsDownloadProcessState::Exited)
            break;
        pump();
    } while (timer.elapsed() < 9000);
    bool passed = update.state == WindowsDownloadProcessState::Cancelled &&
                  container->nativeUseFence()->state() == WindowsDownloadNativeUseFence::State::ClosedQuiescent &&
                  !platform.report().remove_attempted;
    acknowledgement.complete(passed);
    witness = download_artifacts::Handle{};
    if (native_acks) {
        owners.reset();
        runtime.reset();
    }
    const bool clean = native_acks && entries(parent).isEmpty();
    passed = passed && clean;
    return {{"passed", passed},
            {"native_exit_before_supervisor_ack", true},
            {"owner_draining_before_ack", true},
            {"journal_in_use_before_ack", true},
            {"new_launch_refused", true},
            {"fence_closed_quiescent", passed},
            {"process_state", static_cast<int>(update.state)},
            {"runtime_cleanup", clean},
            {"all_native_acks", native_acks}};
}

int hostProbe(const Options& options) {
    std::vector<download_artifacts::Handle> parents;
    require(download_artifacts::pinAncestors(options.evidence, parents), "evidence ancestors unavailable/reparse");
    require(download_artifacts::createPrivateDirectory(options.evidence), "evidence must be a fresh directory");
    const auto operation = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto name = QString::fromStdString(downloadMediaProfileName(operation));
    const auto journal_directory = options.evidence + QStringLiteral("/profile-journal");
    auto platform = WindowsNativeMediaProfile::system();
    WindowsDownloadMediaProfileStore journal(journal_directory);
    WindowsMediaProfileOwner owner(journal, *platform, {"native-probe", [operation] { return operation; }});
    QTemporaryDir temporary{QDir::tempPath() + QStringLiteral("/RCMusic-AppContainer-Probe-XXXXXX")};
    temporary.setAutoRemove(false); // Preserve uncertain resources on every failure.
    bool passed = false;
    QJsonObject results;
    bool native_acks = true;
    try {
        const QJsonObject plan{
            {"schema_version", 1},
            {"profile_name", name},
            {"journal_directory", journal_directory},
            {"owner_composed", true},
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
        require(createJournalDirectory(journal_directory), "private journal directory");
        Loopback loopback;
        require(loopback.start(), "loopback parent positive control failed");
        UdpLoopback udp;
        require(udp.start(), "UDP parent positive control failed");
        const bool prepared = owner.prepare();
        const auto creation = platform->report();
        require(receipt(options.evidence, QStringLiteral("01-profile-result.json"),
                        {{"profile_name", name},
                         {"creation_attempted", creation.create_attempted},
                         {"create_hresult", creation.create_hresult
                                                ? QJsonValue{static_cast<qint64>(*creation.create_hresult)}
                                                : QJsonValue{QJsonValue::Null}},
                         {"created_by_this_run", creation.created},
                         {"sid_freed", creation.sid_released},
                         {"sid_hex", QString::fromLatin1(creation.sid.toHex())},
                         {"owner_prepared", prepared},
                         {"owner_state", static_cast<int>(owner.state())}}),
                "profile receipt");
        require(prepared, "durable owner preparation failed; preserve uncertain history/profile");
        const auto container = owner.capability();
        require(container && container->nativeUseFence(), "owned profile SID/fence capability");
        const auto no_child = runNoChild(temporary.path(), container, journal, native_acks);
        require(receipt(options.evidence, QStringLiteral("05-no-child.json"), no_child), "no-child receipt");
        require(native_acks && no_child.value("passed").toBool(), "no-child proof failed");
        bool helper_passed = false;
        const auto helper = runHelper(temporary.path(), container, loopback.port, udp.port, helper_passed, native_acks);
        require(receipt(options.evidence, QStringLiteral("02-helper-result.json"), helper), "helper receipt");
        require(native_acks, "helper ACK uncertain; remaining cases/profile cleanup refused");
        require(udp.positiveControl(), "UDP parent post-control failed");
        QJsonArray media;
        const bool media_passed = runMedia(temporary.path(), container, media, native_acks);
        results = {{"helper_passed", helper_passed},
                   {"media_passed", media_passed},
                   {"media", media},
                   {"no_child_passed", no_child.value("passed")},
                   {"udp_controls_passed", true}};
        require(native_acks, "media ACK uncertain; cancellation/profile cleanup refused");
        const auto cancellation = runCancellation(temporary.path(), container, owner, journal, *platform, native_acks);
        require(receipt(options.evidence, QStringLiteral("07-cancel-and-drain.json"), cancellation), "cancel receipt");
        results.insert(QStringLiteral("cancellation_passed"), cancellation.value("passed"));
        passed =
            helper_passed && media_passed && no_child.value("passed").toBool() && cancellation.value("passed").toBool();
    } catch (const std::exception& error) {
        results.insert(QStringLiteral("failure"), QString::fromLatin1(error.what()));
    } catch (...) {
        results.insert(QStringLiteral("failure"), QStringLiteral("unexpected exception"));
    }
    // Only the explicit ACK aggregate permits asking the durable owner to
    // close. Native process/runtime owners are retained on uncertainty; the
    // profile owner's destructor revokes admission without deleting/writing.
    const bool cleanup = native_acks && owner.close() && owner.state() == WindowsMediaProfileOwnerState::Removed;
    const auto profile = platform->report();
    QJsonArray deletions;
    if (profile.remove_hresult)
        deletions.append(static_cast<qint64>(*profile.remove_hresult));
    const auto final_record = journal.load();
    const bool journal_removed =
        final_record.record && final_record.record->stage == DownloadMediaProfileStage::Removed;
    const bool cleanup_receipt = receipt(
        options.evidence, QStringLiteral("04-profile-cleanup.json"),
        {{"profile_name", name},
         {"creation_attempted", profile.create_attempted},
         {"created_by_this_run", profile.created},
         {"delete_hresults", deletions},
         {"cleanup_confirmed_by_api", cleanup},
         {"all_native_acks", native_acks},
         {"absence", static_cast<int>(profile.absence)},
         {"journal_removed_saved", journal_removed},
         {"owner_state", static_cast<int>(owner.state())},
         {"remaining_status", cleanup && journal_removed ? "owner-removed-saved" : "unconfirmed-preserve-and-review"}});
    results.insert(QStringLiteral("passed"), passed && cleanup && journal_removed && cleanup_receipt);
    results.insert(QStringLiteral("profile_cleanup"), cleanup);
    results.insert(QStringLiteral("creation_attempted"), profile.create_attempted);
    results.insert(QStringLiteral("all_native_acks"), native_acks);
    results.insert(QStringLiteral("owner_removed_saved"), journal_removed && cleanup);
    results.insert(QStringLiteral("temporary"), temporary.path());
    const bool result_receipt = receipt(options.evidence, QStringLiteral("03-results.json"), results);
    const bool complete = passed && native_acks && cleanup && journal_removed && cleanup_receipt && result_receipt;
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

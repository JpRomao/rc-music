#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace {
constexpr qsizetype frame_limit = 16 * 1024 * 1024;
bool writeBytes(const QByteArray& bytes) {
    qsizetype offset{};
    while (offset < bytes.size()) {
        DWORD written{};
        const auto count = static_cast<DWORD>(std::min<qsizetype>(8192, bytes.size() - offset));
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.constData() + offset, count, &written, nullptr) ||
            !written)
            return false;
        offset += written;
    }
    return true;
}
bool readBytes(QByteArray& bytes, const qsizetype length) {
    bytes.resize(length);
    qsizetype offset{};
    while (offset < length) {
        DWORD read{};
        const auto count = static_cast<DWORD>(std::min<qsizetype>(16381, length - offset));
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), bytes.data() + offset, count, &read, nullptr) || !read)
            return false;
        offset += read;
    }
    return true;
}
QByteArray json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QByteArray frame(const QJsonObject& value) {
    const auto body = json(value);
    QByteArray result(4, '\0');
    qToLittleEndian(static_cast<quint32>(body.size()), reinterpret_cast<uchar*>(result.data()));
    return result + body;
}
std::optional<QJsonObject> readFrame() {
    QByteArray header;
    if (!readBytes(header, 4))
        return {};
    const auto size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData()));
    if (size == 0 || size > 1024 * 1024)
        return {};
    QByteArray body;
    if (!readBytes(body, size))
        return {};
    const auto document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return {};
    return document.object();
}
bool marker(const QString& name, const QByteArray& bytes) {
    QSaveFile file(qEnvironmentVariable("RC_MUSIC_BROKER_TEST_MARKERS") + '/' + name);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool blockedDescendant() {
    const auto executable = QCoreApplication::applicationFilePath();
    std::wstring command = L"\"" + executable.toStdWString() + L"\" --helper-hang";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    const bool spawned = CreateProcessW(executable.toStdWString().c_str(), command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE;
    if (spawned) {
        // A broken NoChild implementation must never leak the test's child or
        // recursively start another extractor/Node helper.
        TerminateProcess(child.hProcess, 97);
        WaitForSingleObject(child.hProcess, 5000);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
    return !spawned;
}
bool report(const QString& name, const QString& role) {
    bool controls_absent = true;
    for (const auto* key : {"NODE_OPTIONS", "NODE_PATH", "PYTHONPATH", "PYTHONHOME", "PYTHONUSERBASE", "PYTHONSTARTUP",
                            "PYTHONINSPECT", "PYTHONBREAKPOINT"})
        controls_absent = controls_absent && !qEnvironmentVariableIsSet(key);
    return marker(name, json({{"pid", qint64{GetCurrentProcessId()}},
                              {"role", role},
                              {"argv", QJsonArray::fromStringList(QCoreApplication::arguments().mid(1))},
                              {"cwd", QDir::currentPath()},
                              {"executable", QCoreApplication::applicationFilePath()},
                              {"controlsAbsent", controls_absent},
                              {"path", qEnvironmentVariable("PATH")},
                              {"systemRoot", qEnvironmentVariable("SystemRoot")},
                              {"testCase", qEnvironmentVariable("RC_MUSIC_BROKER_TEST_CASE")},
                              {"markerRoot", qEnvironmentVariable("RC_MUSIC_BROKER_TEST_MARKERS")},
                              {"spawnBlocked", blockedDescendant()}}));
}
bool waitGate(const QString& name) {
    const auto handle = OpenEventW(SYNCHRONIZE, FALSE, name.toStdWString().c_str());
    if (!handle)
        return false;
    const auto result = WaitForSingleObject(handle, 20'000);
    CloseHandle(handle);
    return result == WAIT_OBJECT_0;
}
QJsonObject finalProjection() {
    return {{"_type", "video"},
            {"id", "testVideo01"},
            {"extractor_key", "Youtube"},
            {"ext", "m4a"},
            {"acodec", "mp4a.40.2"},
            {"vcodec", "none"},
            {"protocol", "https"},
            {"duration", 123.456},
            {"filesize", 1024},
            {"availability", "public"},
            {"is_live", false},
            {"live_status", "not_live"},
            {"has_drm", false},
            {"url", "https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=A%2fb%2F"},
            {"title", "Synthetic broker song"},
            {"artist", "Synthetic broker artist"},
            {"http_headers", QJsonObject{}}};
}
QJsonObject finalFrame(const int sequence) {
    return {{"v", 1}, {"kind", "result"}, {"seq", sequence}, {"data", finalProjection()}};
}
int finishExtractor(const int sequence, const int exit_code = 0) {
    if (!writeBytes(frame(finalFrame(sequence))) ||
        !marker(QStringLiteral("extractor-final-ready"), QByteArrayLiteral("ready")) ||
        !waitGate(qEnvironmentVariable("RC_MUSIC_BROKER_TEST_EXTRACTOR_FINAL_GATE")))
        return 99;
    return exit_code;
}
int searchExtractor(const QString& test_case) {
    if (QCoreApplication::arguments().mid(1) !=
            QStringList{QStringLiteral("--search"), QStringLiteral("lia campos \u00e7\u00e3o")} ||
        QFileInfo(QDir::currentPath()).fileName() != QLatin1String("extractor") ||
        !report(QStringLiteral("extractor.pid.json"), QStringLiteral("extractor")))
        return 70;
    if (test_case == QLatin1String("search-challenge")) {
        const QJsonArray requests{QJsonObject{{"type", "n"}, {"challenges", QJsonArray{QStringLiteral("a")}}}};
        if (!writeBytes(frame({{"v", 1}, {"kind", "challenge"}, {"seq", 1}, {"player", "p"}, {"requests", requests}})))
            return 78;
        return readFrame() ? 82 : 0;
    }
    const QJsonObject data{{"entries", QJsonArray{QJsonObject{{"id", "testVideo01"},
                                                              {"title", QStringLiteral("Can\u00e7\u00e3o")},
                                                              {"channel", "Lia"},
                                                              {"duration", 217}}}}};
    if (!writeBytes(frame({{"v", 1}, {"kind", "result"}, {"seq", 1}, {"data", data}})))
        return 79;
    return 0;
}
int extractor(const QString& test_case) {
    if (test_case.startsWith(QLatin1String("search")))
        return searchExtractor(test_case);
    if (QCoreApplication::arguments().mid(1) != QStringList{QStringLiteral("testVideo01")} ||
        QFileInfo(QDir::currentPath()).fileName() != QLatin1String("extractor") ||
        !report(QStringLiteral("extractor.pid.json"), QStringLiteral("extractor")))
        return 70;
    if (test_case == QLatin1String("gate-challenge") &&
        !waitGate(qEnvironmentVariable("RC_MUSIC_BROKER_TEST_EXTRACTOR_GATE")))
        return 71;
    if (test_case == QLatin1String("malformed-frame"))
        return writeBytes(QByteArray(4, '\0')) ? 0 : 72;
    if (test_case == QLatin1String("oversized-frame")) {
        QByteArray header(4, '\0');
        qToLittleEndian(static_cast<quint32>(frame_limit + 1), reinterpret_cast<uchar*>(header.data()));
        return writeBytes(header) ? 0 : 73;
    }
    if (test_case == QLatin1String("partial-frame")) {
        QByteArray header(4, '\0');
        qToLittleEndian(quint32{100}, reinterpret_cast<uchar*>(header.data()));
        return writeBytes(header + '{') ? 0 : 74;
    }
    if (test_case == QLatin1String("failure-restricted"))
        return writeBytes(frame({{"v", 1}, {"kind", "failure"}, {"seq", 1}, {"code", "restricted"}})) ? 0 : 75;
    if (test_case == QLatin1String("final-oversize"))
        return writeBytes(frame({{"v", 1},
                                 {"kind", "result"},
                                 {"seq", 1},
                                 {"data", QJsonObject{{"large", QString(1024 * 1024, QLatin1Char('x'))}}}}))
                   ? 0
                   : 76;
    if (test_case == QLatin1String("final-nonzero"))
        return finishExtractor(1, 23);

    const int groups = test_case == QLatin1String("too-many-groups")                                                ? 9
                       : test_case == QLatin1String("traffic-quota")                                                ? 8
                       : test_case == QLatin1String("large-two-groups") || test_case == QLatin1String("two-groups") ? 2
                                                                                                                    : 1;
    const auto player_bytes = test_case == QLatin1String("traffic-quota")      ? 8 * 1024 * 1024
                              : test_case == QLatin1String("large-two-groups") ? 2 * 1024 * 1024
                                                                               : 128;
    for (int group = 1; group <= groups; ++group) {
        const auto key = QStringLiteral("challenge-%1").arg(group);
        const QJsonArray requests{QJsonObject{{"type", group % 2 ? "n" : "sig"}, {"challenges", QJsonArray{key}}}};
        const int sequence = test_case == QLatin1String("wrong-sequence") ? 2 : group;
        if (!writeBytes(frame({{"v", 1},
                               {"kind", "challenge"},
                               {"seq", sequence},
                               {"player", QString(player_bytes, QLatin1Char('p'))},
                               {"requests", requests}})))
            return 78;
        if (test_case == QLatin1String("early-final"))
            return writeBytes(frame(finalFrame(2))) ? 0 : 79;
        const auto response = readFrame();
        if (!response || response->size() != 4 || response->value("v") != 1 || response->value("kind") != "response" ||
            response->value("seq") != group || !response->value("responses").isArray())
            return 80;
        const auto values = response->value("responses").toArray();
        const QJsonObject expected{{"type", "result"}, {"data", QJsonObject{{key, QStringLiteral("solved:") + key}}}};
        if (values.size() != 1 || values.first().toObject() != expected ||
            !marker(QStringLiteral("extractor-response-%1.json").arg(group), json(*response)))
            return 81;
    }
    return finishExtractor(groups + 1);
}
int node(const QString& test_case) {
    if (QCoreApplication::arguments().mid(1) != QStringList{QStringLiteral("--permission"), QStringLiteral("-")} ||
        QFileInfo(QDir::currentPath()).fileName() != QLatin1String("javascript"))
        return 83;
    QByteArray program;
    std::array<char, 16381> block{};
    for (;;) {
        DWORD count{};
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), block.data(), static_cast<DWORD>(block.size()), &count,
                      nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE)
                break;
            return 84;
        }
        if (!count)
            break;
        if (program.size() > frame_limit - static_cast<qsizetype>(count))
            return 85;
        program.append(block.data(), count);
    }
    const QByteArray marker_text{"process.stdout.write(JSON.stringify(jsc("};
    const auto beginning = program.lastIndexOf(marker_text), end = program.lastIndexOf(")))");
    if (beginning < 0 || end <= beginning + marker_text.size())
        return 86;
    const auto document =
        QJsonDocument::fromJson(program.mid(beginning + marker_text.size(), end - beginning - marker_text.size()));
    if (!document.isObject())
        return 87;
    const auto input = document.object();
    if (input.size() != 4 || input.value("type") != "player" || input.value("output_preprocessed") != false ||
        !input.value("player").isString() || !input.value("requests").isArray() ||
        !program.contains("Object.assign(globalThis,") || !program.contains("SYNTHETIC_CORE") ||
        !program.contains("SYNTHETIC_LIBRARY"))
        return 88;
    const auto requests = input.value("requests").toArray();
    if (requests.size() != 1 || !requests.first().isObject())
        return 89;
    const auto request = requests.first().toObject();
    const auto challenges = request.value("challenges").toArray();
    if (challenges.size() != 1 || !challenges.first().isString())
        return 90;
    const auto key = challenges.first().toString();
    bool group_valid{};
    const auto group = key.mid(QStringLiteral("challenge-").size()).toInt(&group_valid);
    if (!group_valid || group < 1 || group > 9 ||
        !report(QStringLiteral("node-%1.pid.json").arg(group), QStringLiteral("node")))
        return 91;
    if (test_case == QLatin1String("large-two-groups") && input.value("player").toString().size() != 2 * 1024 * 1024)
        return 92;
    QByteArray output = json(
        {{"type", "result"},
         {"responses",
          QJsonArray{QJsonObject{{"type", "result"}, {"data", QJsonObject{{key, QStringLiteral("solved:") + key}}}}}}});
    if (test_case == QLatin1String("node-malformed"))
        output = "}{";
    else if (test_case == QLatin1String("node-error"))
        output = json({{"type", "error"}, {"error", "PRIVATE_NODE_DIAGNOSTIC secret=1"}});
    else if (test_case == QLatin1String("node-extra-field"))
        output = json({{"type", "result"}, {"responses", QJsonArray{}}, {"extra", true}});
    else if (test_case == QLatin1String("node-oversize"))
        output = QByteArray(1024 * 1024 + 1, 'x');
    if (!writeBytes(output) || !marker(QStringLiteral("node-%1.output-ready").arg(group), QByteArrayLiteral("ready")))
        return 93;
    if (!waitGate(qEnvironmentVariable("RC_MUSIC_BROKER_TEST_NODE_GATE_PREFIX") + QString::number(group)))
        return 94;
    return test_case == QLatin1String("node-nonzero") ? 29 : 0;
}
} // namespace

int main(int argc, char* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc == 2 && QByteArray(argv[1]) == QByteArrayLiteral("--helper-hang")) {
        Sleep(30'000);
        return 95;
    }
    QCoreApplication application(argc, argv);
    const auto test_case = qEnvironmentVariable("RC_MUSIC_BROKER_TEST_CASE");
    if (test_case.isEmpty())
        return 96;
    const auto name = QFileInfo(application.applicationFilePath()).fileName();
    if (name.compare(QStringLiteral("yt-dlp.exe"), Qt::CaseInsensitive) == 0)
        return extractor(test_case);
    if (name.compare(QStringLiteral("node.exe"), Qt::CaseInsensitive) == 0)
        return node(test_case);
    return 98;
}

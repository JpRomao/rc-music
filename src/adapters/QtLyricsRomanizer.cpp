#include "adapters/QtLyricsRomanizer.hpp"
#include "adapters/StrictJson.hpp"
#include "m0/domain/Lyrics.hpp"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace music_player::m0 {
namespace {
constexpr std::size_t maximum_lines = 8192;
constexpr qsizetype maximum_message_bytes = 2 * 1024 * 1024;
constexpr int process_timeout_ms = 8000;

bool validLine(const QString& line, const QByteArray& bytes, std::size_t& total) {
    const auto size = static_cast<std::size_t>(bytes.size());
    if (size > lyrics_max_input_bytes - total || QString::fromUtf8(bytes) != line)
        return false;
    for (const auto character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte < 0x20 && byte != '\t') || byte == 0x7f)
            return false;
    }
    total += size;
    return true;
}

QByteArray encode(const std::vector<std::string>& lines) {
    if (lines.empty() || lines.size() > maximum_lines)
        return {};
    QJsonArray array;
    std::size_t total{};
    for (const auto& input : lines) {
        if (input.size() > lyrics_max_input_bytes - total)
            return {};
        const auto bytes = QByteArray::fromStdString(input);
        auto line = QString::fromUtf8(bytes);
        if (!validLine(line, bytes, total) || line.toUtf8() != bytes)
            return {};
        array.append(std::move(line));
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("lines"), array}}).toJson(QJsonDocument::Compact);
}

std::optional<std::vector<std::string>> decode(const QByteArray& output, const std::size_t expected) {
    if (!detail::strictJsonPreflight(output, 3))
        return std::nullopt;
    QJsonParseError error{};
    const auto response = QJsonDocument::fromJson(output, &error);
    const auto object = response.object();
    const auto lines = object.value(QStringLiteral("lines"));
    if (error.error != QJsonParseError::NoError || object.size() != 1 || !lines.isArray())
        return std::nullopt;
    const auto array = lines.toArray();
    if (static_cast<std::size_t>(array.size()) != expected)
        return std::nullopt;
    std::vector<std::string> result;
    result.reserve(expected);
    std::size_t total{};
    for (const auto& item : array) {
        if (!item.isString())
            return std::nullopt;
        const auto line = item.toString();
        const auto bytes = line.toUtf8();
        if (!validLine(line, bytes, total))
            return std::nullopt;
        result.push_back(bytes.toStdString());
    }
    return result;
}

bool regularFile(const QString& path) {
    const QFileInfo file(path);
    return file.isFile() && !file.isSymLink();
}

std::optional<std::vector<std::string>> run(const QString& python, const QString& script,
                                            const std::vector<std::string>& lines) {
    const auto input = encode(lines);
    if (input.isEmpty() || input.size() > maximum_message_bytes || !regularFile(python) || !regularFile(script))
        return std::nullopt;
    QProcess process;
    QProcessEnvironment environment;
    const auto system = QProcessEnvironment::systemEnvironment();
    for (const auto& key : {QStringLiteral("SystemRoot"), QStringLiteral("WINDIR")}) {
        if (system.contains(key))
            environment.insert(key, system.value(key));
    }
#ifdef Q_OS_WIN
    environment.insert(QStringLiteral("PATH"), QString{});
#endif
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(QFileInfo(script).absolutePath());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setReadChannel(QProcess::StandardOutput);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    const QDeadlineTimer deadline(process_timeout_ms);
    process.start(python, {QStringLiteral("-I"), QStringLiteral("-B"), script});
    QByteArray output;
    if (process.waitForStarted(static_cast<int>(deadline.remainingTime())) && process.write(input) == input.size()) {
        process.closeWriteChannel();
        while (process.state() != QProcess::NotRunning && !deadline.hasExpired()) {
            process.waitForReadyRead(static_cast<int>(deadline.remainingTime()));
            output += process.readAllStandardOutput();
            if (output.size() > maximum_message_bytes)
                break;
        }
        output += process.readAllStandardOutput();
        if (process.state() == QProcess::NotRunning && process.exitStatus() == QProcess::NormalExit &&
            process.exitCode() == 0 && output.size() <= maximum_message_bytes && !deadline.hasExpired())
            return decode(output, lines.size());
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
    return std::nullopt;
}
} // namespace

std::function<std::optional<std::vector<std::string>>(const std::vector<std::string>&)>
makeQtLyricsRomanizer(const QString& config_root) {
    if (!QDir::isAbsolutePath(config_root))
        return {};
    const QDir directory(QDir(config_root).filePath(QStringLiteral("lyrics-romanizer")));
#ifdef Q_OS_WIN
    const auto python = directory.filePath(QStringLiteral("Scripts/python.exe"));
#else
    const auto python = directory.filePath(QStringLiteral("bin/python"));
#endif
    const auto script = directory.filePath(QStringLiteral("romanize.py"));
    if (!regularFile(python) || !regularFile(script))
        return {};
    return [python, script](const std::vector<std::string>& lines) { return run(python, script, lines); };
}

} // namespace music_player::m0

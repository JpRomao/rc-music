// Manual network smoke with the provider's public example metadata. Not CTest.
// Prints counts/status only; never prints, stores or uploads audio/lyrics text.
#include "adapters/QtLyricsService.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments() != QStringList{application.arguments().front(), "--public-example"})
        return 2;
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;
    music_player::m0::QtLyricsService service;
    bool terminal{};
    int result = 1;
    const auto finish = [&] {
        if (terminal)
            return;
        const auto status = service.status();
        if (status == music_player::m0::QtLyricsService::Status::LoadingLocal ||
            status == music_player::m0::QtLyricsService::Status::Searching)
            return;
        terminal = true;
        const auto& document = service.document();
        const bool ready = status == music_player::m0::QtLyricsService::Status::Ready &&
                           service.source() == music_player::m0::QtLyricsService::Source::Online;
        const auto json = QJsonDocument(QJsonObject{{"ready", ready},
                                                    {"status", static_cast<int>(status)},
                                                    {"cue_count", qint64(document.lines.size())},
                                                    {"plain_bytes", qint64(document.plain_text.size())},
                                                    {"message", service.message()},
                                                    {"audio_sent", false}})
                              .toJson(QJsonDocument::Compact) +
                          '\n';
        const bool printed = std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout) ==
                                 static_cast<std::size_t>(json.size()) &&
                             std::fflush(stdout) == 0;
        result = ready && printed ? 0 : 1;
        service.beginShutdown();
    };
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &application, [&] {
        if (terminal && service.stopped())
            application.quit();
    });
    QObject::connect(&service, &music_player::m0::QtLyricsService::changed, &application, finish);
    service.setTrack(temporary.filePath("public-example.mp3"), "I Want to Live", "Borislav Slavov",
                     "Baldur's Gate 3 (Original Game Soundtrack)", 233000);
    if (!service.searchOnline())
        return 1;
    poll.start(10);
    QTimer::singleShot(15000, &application, [&] {
        service.beginShutdown();
        application.quit();
    });
    application.exec();
    return result;
}

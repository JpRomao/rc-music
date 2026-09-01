#include "adapters/WindowsSmtcAdapter.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <systemmediatransportcontrolsinterop.h>
#include <windows.h>

#include <winrt/Windows.Media.h>
#include <winrt/base.h>

#include <QGuiApplication>
#include <QString>
#include <QTest>
#include <QWindow>

#include <cstdint>

namespace music_player::m0::test {
namespace {

namespace media = winrt::Windows::Media;

[[nodiscard]] QString asQString(const winrt::hstring& value) {
    return QString::fromWCharArray(value.c_str(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] media::SystemMediaTransportControls controlsForWindow(HWND window) {
    const auto interop =
        winrt::get_activation_factory<media::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
    media::SystemMediaTransportControls controls{nullptr};
    winrt::check_hresult(
        interop->GetForWindow(window, winrt::guid_of<media::SystemMediaTransportControls>(), winrt::put_abi(controls)));
    return controls;
}

void verifyTransportFlags(const media::SystemMediaTransportControls& controls, bool play_enabled, bool pause_enabled,
                          bool previous_enabled, bool next_enabled) {
    QCOMPARE(controls.IsPlayEnabled(), play_enabled);
    QCOMPARE(controls.IsPauseEnabled(), pause_enabled);
    QCOMPARE(controls.IsPreviousEnabled(), previous_enabled);
    QCOMPARE(controls.IsNextEnabled(), next_enabled);
    QCOMPARE(controls.IsStopEnabled(), false);
    QCOMPARE(controls.IsRecordEnabled(), false);
    QCOMPARE(controls.IsFastForwardEnabled(), false);
    QCOMPARE(controls.IsRewindEnabled(), false);
    QCOMPARE(controls.IsChannelUpEnabled(), false);
    QCOMPARE(controls.IsChannelDownEnabled(), false);
}

} // namespace

class WindowsSmtcNativeSmokeTests final : public QObject {
    Q_OBJECT

  private slots:
    void publishesAndClearsTheRealWindowControls() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));

        QWindow window;
        window.setTitle(QStringLiteral("M0 native SMTC smoke"));
        window.resize(320, 180);
        window.create();
        const auto native_window = reinterpret_cast<HWND>(window.winId());
        QVERIFY(native_window != nullptr);
        QVERIFY(::IsWindow(native_window));

        WindowsSmtcAdapter adapter;
        QVERIFY2(adapter.bindWindow(window), "The real WinRT SMTC boundary could not bind to the QWindow HWND");
        QVERIFY(adapter.available());

        try {
            auto controls = controlsForWindow(native_window);
            QVERIFY(controls != nullptr);
            QCOMPARE(controls.IsEnabled(), false);

            const SystemMediaSnapshot playing{
                u"Faixa Nativa",
                u"Artista SMTC",
                u"Album Windows",
                SystemMediaPlaybackStatus::Playing,
                false,
                true,
                true,
                false,
                true,
                12'345,
                98'765,
            };
            QVERIFY(adapter.publish(playing));

            QCOMPARE(controls.IsEnabled(), true);
            QCOMPARE(static_cast<int>(controls.PlaybackStatus()),
                     static_cast<int>(media::MediaPlaybackStatus::Playing));
            verifyTransportFlags(controls, false, true, true, false);
            const auto playing_properties = controls.DisplayUpdater().MusicProperties();
            QCOMPARE(asQString(playing_properties.Title()), QStringLiteral("Faixa Nativa"));
            QCOMPARE(asQString(playing_properties.Artist()), QStringLiteral("Artista SMTC"));
            QCOMPARE(asQString(playing_properties.AlbumTitle()), QStringLiteral("Album Windows"));

            QVERIFY(adapter.publish(SystemMediaSnapshot{}));

            QCOMPARE(controls.IsEnabled(), false);
            QCOMPARE(static_cast<int>(controls.PlaybackStatus()), static_cast<int>(media::MediaPlaybackStatus::Closed));
            verifyTransportFlags(controls, false, false, false, false);
            QCOMPARE(static_cast<int>(controls.DisplayUpdater().Type()),
                     static_cast<int>(media::MediaPlaybackType::Unknown));

            controls = nullptr;
        } catch (const winrt::hresult_error& error) {
            const auto message = QStringLiteral("Native WinRT SMTC query failed: 0x%1 (%2)")
                                     .arg(static_cast<quint32>(error.code().value), 8, 16, QLatin1Char('0'))
                                     .arg(asQString(error.message()));
            QFAIL(qPrintable(message));
        }

        adapter.shutdown();
        QVERIFY(!adapter.available());
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::WindowsSmtcNativeSmokeTests)

#include "WindowsSmtcNativeSmokeTests.moc"

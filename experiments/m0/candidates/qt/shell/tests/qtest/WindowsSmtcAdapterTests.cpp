#include "adapters/WindowsSmtcAdapter.hpp"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QWindow>

#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class FakeSmtcNativeBoundary final : public SmtcNativeBoundary {
  public:
    [[nodiscard]] bool bind(std::uintptr_t native_window_handle, CommandCallback callback) noexcept override {
        ++bind_calls;
        last_handle = native_window_handle;
        callback_ = std::move(callback);
        return bind_succeeds;
    }

    [[nodiscard]] bool publish(const SystemMediaSnapshot& snapshot) noexcept override {
        published.push_back(snapshot);
        return publish_succeeds;
    }

    void shutdown() noexcept override { ++shutdown_calls; }

    void send(SystemMediaCommand command) const {
        if (callback_)
            callback_(command);
    }

    bool bind_succeeds{true};
    bool publish_succeeds{true};
    int bind_calls{0};
    int shutdown_calls{0};
    std::uintptr_t last_handle{0};
    std::vector<SystemMediaSnapshot> published;

  private:
    CommandCallback callback_;
};

[[nodiscard]] SystemMediaSnapshot playingSnapshot(std::int64_t position_ms = 4'000) {
    return {u"Faixa", u"Artista",  u"Álbum", SystemMediaPlaybackStatus::Playing, true, true, true, true,
            true,     position_ms, 90'000};
}

} // namespace

class WindowsSmtcAdapterTests final : public QObject {
    Q_OBJECT

  private slots:
    void bindPublishesCachedSnapshotAndDeduplicatesUpdates() {
        auto boundary = std::make_unique<FakeSmtcNativeBoundary>();
        auto* const native = boundary.get();
        WindowsSmtcAdapter adapter(std::move(boundary));
        QSignalSpy availability(&adapter, &WindowsSmtcAdapter::availabilityChanged);
        QWindow window;

        const auto initial = playingSnapshot();
        QVERIFY(adapter.publish(initial));
        QCOMPARE(native->published.size(), std::size_t{0});
        QVERIFY(adapter.bindWindow(window));
        QVERIFY(adapter.available());
        QCOMPARE(native->bind_calls, 1);
        QCOMPARE(native->last_handle, static_cast<std::uintptr_t>(window.winId()));
        QCOMPARE(native->published, std::vector<SystemMediaSnapshot>{initial});
        QCOMPARE(availability.size(), 1);
        QCOMPARE(availability.at(0).at(0).toBool(), true);

        QVERIFY(adapter.publish(initial));
        QCOMPARE(native->published.size(), std::size_t{1});
        const auto advanced = playingSnapshot(4'250);
        QVERIFY(adapter.publish(advanced));
        QCOMPARE(native->published, (std::vector<SystemMediaSnapshot>{initial, advanced}));

        QVERIFY(adapter.bindWindow(window));
        QCOMPARE(native->bind_calls, 1);
        QCOMPARE(native->published.size(), std::size_t{2});
    }

    void rebindReleasesOldControlsAndRepublishesTheLatestSnapshot() {
        auto boundary = std::make_unique<FakeSmtcNativeBoundary>();
        auto* const native = boundary.get();
        WindowsSmtcAdapter adapter(std::move(boundary));
        QWindow first;
        QWindow second;
        const auto snapshot = playingSnapshot();

        QVERIFY(adapter.publish(snapshot));
        QVERIFY(adapter.bindWindow(first));
        QVERIFY(adapter.bindWindow(second));

        QCOMPARE(native->bind_calls, 2);
        QCOMPARE(native->shutdown_calls, 1);
        QCOMPARE(native->last_handle, static_cast<std::uintptr_t>(second.winId()));
        QCOMPARE(native->published, (std::vector<SystemMediaSnapshot>{snapshot, snapshot}));
        QVERIFY(adapter.available());
    }

    void nativeFailuresDegradeAvailabilityWithoutEscaping() {
        auto boundary = std::make_unique<FakeSmtcNativeBoundary>();
        auto* const native = boundary.get();
        native->bind_succeeds = false;
        WindowsSmtcAdapter adapter(std::move(boundary));
        QSignalSpy availability(&adapter, &WindowsSmtcAdapter::availabilityChanged);
        QWindow window;

        QVERIFY(!adapter.bindWindow(window));
        QVERIFY(!adapter.available());
        QCOMPARE(availability.size(), 0);
        QCOMPARE(native->shutdown_calls, 1);

        native->bind_succeeds = true;
        QVERIFY(adapter.bindWindow(window));
        QCOMPARE(availability.size(), 1);
        native->publish_succeeds = false;
        QVERIFY(!adapter.publish(playingSnapshot()));
        QVERIFY(!adapter.available());
        QCOMPARE(native->shutdown_calls, 2);
        QCOMPARE(availability.size(), 2);
        QCOMPARE(availability.at(1).at(0).toBool(), false);
    }

    void queuesDistinctNativeCommandsOntoTheGuiThread() {
        auto boundary = std::make_unique<FakeSmtcNativeBoundary>();
        auto* const native = boundary.get();
        WindowsSmtcAdapter adapter(std::move(boundary));
        QWindow window;
        QVERIFY(adapter.bindWindow(window));
        QSignalSpy play(&adapter, &WindowsSmtcAdapter::playRequested);
        QSignalSpy pause(&adapter, &WindowsSmtcAdapter::pauseRequested);
        QSignalSpy previous(&adapter, &WindowsSmtcAdapter::previousRequested);
        QSignalSpy next(&adapter, &WindowsSmtcAdapter::nextRequested);
        QSignalSpy seek(&adapter, &WindowsSmtcAdapter::seekRequested);
        std::vector<SystemMediaCommand> handled;
        adapter.setCommandHandler([&](SystemMediaCommand command) { handled.push_back(command); });
        QThread* dispatch_thread{nullptr};
        connect(&adapter, &WindowsSmtcAdapter::seekRequested, &adapter,
                [&](qint64) { dispatch_thread = QThread::currentThread(); });

        std::thread native_thread([&] {
            native->send({SystemMediaCommandKind::Play, 0});
            native->send({SystemMediaCommandKind::Pause, 0});
            native->send({SystemMediaCommandKind::Previous, 0});
            native->send({SystemMediaCommandKind::Next, 0});
            native->send({SystemMediaCommandKind::Seek, 73'125});
        });
        native_thread.join();

        QCOMPARE(play.size(), 0);
        QTRY_COMPARE(play.size(), 1);
        QCOMPARE(pause.size(), 1);
        QCOMPARE(previous.size(), 1);
        QCOMPARE(next.size(), 1);
        QCOMPARE(seek.size(), 1);
        QCOMPARE(seek.at(0).at(0).toLongLong(), qint64{73'125});
        QCOMPARE(dispatch_thread, QGuiApplication::instance()->thread());
        QCOMPARE(handled, (std::vector<SystemMediaCommand>{{SystemMediaCommandKind::Play, 0},
                                                           {SystemMediaCommandKind::Pause, 0},
                                                           {SystemMediaCommandKind::Previous, 0},
                                                           {SystemMediaCommandKind::Next, 0},
                                                           {SystemMediaCommandKind::Seek, 73'125}}));
    }

    void shutdownAndWindowDestructionReleaseHandlersIdempotently() {
        auto boundary = std::make_unique<FakeSmtcNativeBoundary>();
        auto* const native = boundary.get();
        WindowsSmtcAdapter adapter(std::move(boundary));
        QSignalSpy play(&adapter, &WindowsSmtcAdapter::playRequested);
        auto window = std::make_unique<QWindow>();
        QVERIFY(adapter.bindWindow(*window));

        window.reset();
        QVERIFY(!adapter.available());
        QCOMPARE(native->shutdown_calls, 1);
        native->send({SystemMediaCommandKind::Play, 0});
        QCoreApplication::processEvents();
        QCOMPARE(play.size(), 0);

        adapter.shutdown();
        adapter.shutdown();
        QCOMPARE(native->shutdown_calls, 1);
        QWindow replacement;
        QVERIFY(!adapter.bindWindow(replacement));
    }

    void nativeSurfaceRecreationReleasesAndRebindsTheWindowControls() {
        auto boundary = std::make_unique<FakeSmtcNativeBoundary>();
        auto* const native = boundary.get();
        WindowsSmtcAdapter adapter(std::move(boundary));
        QWindow window;
        const auto snapshot = playingSnapshot();

        QVERIFY(adapter.publish(snapshot));
        QVERIFY(adapter.bindWindow(window));
        QVERIFY(adapter.available());
        QCOMPARE(native->bind_calls, 1);

        window.destroy();
        QCoreApplication::processEvents();
        QVERIFY(!adapter.available());
        QCOMPARE(native->shutdown_calls, 1);

        window.create();
        QTRY_VERIFY(adapter.available());
        QCOMPARE(native->bind_calls, 2);
        QCOMPARE(native->published, (std::vector<SystemMediaSnapshot>{snapshot, snapshot}));

        window.destroy();
        QCoreApplication::processEvents();
        QVERIFY(!adapter.available());
        QCOMPARE(native->shutdown_calls, 2);

        window.create();
        window.destroy();
        QCoreApplication::processEvents();
        QVERIFY(!adapter.available());
        QCOMPARE(native->bind_calls, 2);
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::WindowsSmtcAdapterTests)

#include "WindowsSmtcAdapterTests.moc"

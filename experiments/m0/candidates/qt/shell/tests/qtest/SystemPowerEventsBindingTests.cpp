#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/ports/PlatformIntegration.hpp"
#include "m0/ports/SystemPowerEvents.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/SystemPowerEventsBinding.hpp"

#include <QTest>

#include <utility>

namespace music_player::m0::test {
namespace {

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

class TestPlatform final : public PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override {
        ++tray_reads;
        return true;
    }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override {}

    mutable int tray_reads{0};
};

class FakeSystemPowerEvents final : public SystemPowerEvents {
  public:
    [[nodiscard]] bool available() const noexcept override { return true; }
    void setEventHandler(SystemPowerEventHandler handler) override {
        ++handler_assignments;
        handler_ = std::move(handler);
    }
    void shutdown() noexcept override {
        ++shutdown_requests;
        handler_was_armed_during_shutdown = static_cast<bool>(handler_);
        handler_ = {};
    }

    void send(const SystemPowerEvent event) const {
        if (handler_)
            handler_(event);
    }

    SystemPowerEventHandler handler_{};
    int handler_assignments{0};
    int shutdown_requests{0};
    bool handler_was_armed_during_shutdown{false};
};

} // namespace

class SystemPowerEventsBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void routesTransitionsAndDisarmsThePortBeforeShutdown() {
        TestPlatform platform;
        PlayerPresenter presenter(frozenBundle(), platform);
        FakeSystemPowerEvents events;

        {
            SystemPowerEventsBinding binding(presenter, events);
            QCOMPARE(events.handler_assignments, 1);
            QVERIFY(static_cast<bool>(events.handler_));
            const int reads_before = platform.tray_reads;

            events.send(SystemPowerEvent::Resumed);
            QCOMPARE(platform.tray_reads, reads_before + 1);
            events.send(SystemPowerEvent::Resumed);
            QCOMPARE(platform.tray_reads, reads_before + 1);
            events.send(SystemPowerEvent::Suspending);
            events.send(SystemPowerEvent::Resumed);
            QCOMPARE(platform.tray_reads, reads_before + 2);
            QCOMPARE(events.shutdown_requests, 0);
        }

        QCOMPARE(events.handler_assignments, 2);
        QVERIFY(!events.handler_);
        QCOMPARE(events.shutdown_requests, 1);
        QVERIFY(!events.handler_was_armed_during_shutdown);
        const int reads_after_shutdown = platform.tray_reads;
        events.send(SystemPowerEvent::Suspending);
        QCOMPARE(platform.tray_reads, reads_after_shutdown);
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::SystemPowerEventsBindingTests)

#include "SystemPowerEventsBindingTests.moc"

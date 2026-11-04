#include "presentation/EmbeddedBrowserBinding.hpp"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace music_player::m0;

namespace {
struct WindowState final {
    int shown{0};
    int stopped{0};
    int destroyed{0};
    bool succeeds{true};
    bool throws{false};
    std::vector<std::uintptr_t> owners;
    std::function<void()> on_show;
    std::function<void()> on_shutdown;
};
class FakeWindow final : public EmbeddedBrowserWindow {
  public:
    explicit FakeWindow(std::shared_ptr<WindowState> state) : state_(std::move(state)) {}
    ~FakeWindow() override { ++state_->destroyed; }
    bool show(std::uintptr_t owner) override {
        ++state_->shown;
        state_->owners.push_back(owner);
        if (auto callback = std::exchange(state_->on_show, {}))
            callback();
        if (state_->throws)
            throw std::runtime_error("C:\\private\\secret https://private.example/?token=secret");
        return state_->succeeds;
    }
    void shutdown() noexcept override {
        ++state_->stopped;
        if (auto callback = std::exchange(state_->on_shutdown, {}))
            callback();
    }

  private:
    std::shared_ptr<WindowState> state_;
};
} // namespace

class EmbeddedBrowserBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void constructionIsInertAndSuccessfulOpenReusesWindow() {
        int factories = 0;
        auto state = std::make_shared<WindowState>();
        {
            EmbeddedBrowserBinding binding{[&] {
                ++factories;
                return std::make_unique<FakeWindow>(state);
            }};
            QVERIFY(binding.errorText().isEmpty());
            QCOMPARE(factories, 0);
            binding.setOwnerHandle(42);
            QCOMPARE(factories, 0);
            binding.open();
            QCOMPARE(factories, 1);
            QCOMPARE(state->shown, 1);
            QCOMPARE(state->owners, std::vector<std::uintptr_t>{42});
            QVERIFY(binding.errorText().isEmpty());
            binding.setOwnerHandle(43);
            binding.open();
            QCOMPARE(factories, 1);
            QCOMPARE(state->shown, 2);
            QCOMPARE(state->owners, (std::vector<std::uintptr_t>{42, 43}));
            QCOMPARE(state->stopped, 0);
        }
        QCOMPARE(state->stopped, 1);
        QCOMPARE(state->destroyed, 1);
    }
    void missingOwnerDoesNotCreateAWindowAndCanBeCorrected() {
        int factories = 0;
        auto state = std::make_shared<WindowState>();
        EmbeddedBrowserBinding binding{[&] {
            ++factories;
            return std::make_unique<FakeWindow>(state);
        }};
        QSignalSpy errors{&binding, &EmbeddedBrowserBinding::errorChanged};
        binding.open();
        QCOMPARE(factories, 0);
        QVERIFY(!binding.errorText().isEmpty());
        QCOMPARE(errors.size(), 1);
        binding.open();
        QCOMPARE(errors.size(), 1);
        binding.setOwnerHandle(42);
        binding.open();
        QCOMPARE(factories, 1);
        QVERIFY(binding.errorText().isEmpty());
        QCOMPARE(errors.size(), 2);
    }
    void factoryAndWindowFailuresAreRedactedAndAllowRetry_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("null") << 0;
        QTest::newRow("factory throws") << 1;
        QTest::newRow("show refuses") << 2;
        QTest::newRow("show throws") << 3;
    }
    void factoryAndWindowFailuresAreRedactedAndAllowRetry() {
        QFETCH(int, failure);
        int factories = 0;
        bool fail = true;
        auto state = std::make_shared<WindowState>();
        EmbeddedBrowserBinding binding{[&]() -> std::unique_ptr<EmbeddedBrowserWindow> {
            ++factories;
            if (fail && failure == 0)
                return {};
            if (fail && failure == 1)
                throw std::runtime_error("C:\\private\\secret https://private.example/?token=secret");
            state->succeeds = !fail || failure != 2;
            state->throws = fail && failure == 3;
            return std::make_unique<FakeWindow>(state);
        }};
        binding.setOwnerHandle(42);
        binding.open();
        QVERIFY(!binding.errorText().isEmpty());
        QVERIFY(!binding.errorText().contains(QStringLiteral("secret")));
        QVERIFY(!binding.errorText().contains(QStringLiteral("private")));
        QVERIFY(!binding.errorText().contains(QStringLiteral("https://")));
        if (failure >= 2) {
            QCOMPARE(state->stopped, 1);
            QCOMPARE(state->destroyed, 1);
        }
        fail = false;
        binding.open();
        QCOMPARE(factories, 2);
        QVERIFY(binding.errorText().isEmpty());
    }
    void absentFactoryReportsAnActionableError() {
        EmbeddedBrowserBinding binding{{}};
        binding.setOwnerHandle(42);
        binding.open();
        QVERIFY(!binding.errorText().isEmpty());
    }
    void reentryDuringFactoryOrShowCannotCreateAnotherWindow() {
        int factories = 0;
        auto state = std::make_shared<WindowState>();
        EmbeddedBrowserBinding* active = nullptr;
        EmbeddedBrowserBinding binding{[&] {
            ++factories;
            active->open();
            return std::make_unique<FakeWindow>(state);
        }};
        active = &binding;
        state->on_show = [&] { binding.open(); };
        binding.setOwnerHandle(42);
        binding.open();
        QCOMPARE(factories, 1);
        QCOMPARE(state->shown, 1);
        QVERIFY(binding.errorText().isEmpty());
    }
    void errorNotificationMayDestroyTheBinding() {
        QPointer<EmbeddedBrowserBinding> binding = new EmbeddedBrowserBinding{{}};
        binding->setOwnerHandle(42);
        connect(binding, &EmbeddedBrowserBinding::errorChanged, this, [&] { delete binding.data(); });
        binding->open();
        QVERIFY(binding.isNull());
    }
    void deletionDuringShowRetainsTheWindowUntilTheCallReturns() {
        auto state = std::make_shared<WindowState>();
        QPointer<EmbeddedBrowserBinding> binding =
            new EmbeddedBrowserBinding{[&] { return std::make_unique<FakeWindow>(state); }};
        bool retained = false;
        state->on_show = [&] {
            delete binding.data();
            retained = state->stopped == 0 && state->destroyed == 0;
        };
        binding->setOwnerHandle(42);
        binding->open();
        QVERIFY(binding.isNull());
        QVERIFY(retained);
        QCOMPARE(state->stopped, 1);
        QCOMPARE(state->destroyed, 1);
    }
};

QTEST_GUILESS_MAIN(EmbeddedBrowserBindingTests)
#include "EmbeddedBrowserBindingTests.moc"

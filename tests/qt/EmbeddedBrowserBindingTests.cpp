#include "m0/ports/AsyncDownloadSubmission.hpp"
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
class FakeSubmission final : public AsyncDownloadSubmission {
  public:
    DownloadAdmissionResult begin(const DownloadRequest&) override {
        ++calls;
        return {};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view) override {
        ++calls;
        return {};
    }
    int calls{0};
};

struct WindowState final {
    int shown{0};
    int stopped{0};
    int destroyed{0};
    bool succeeds{true};
    bool throws{false};
    bool throws_on_set_submission{false};
    std::vector<std::uintptr_t> owners;
    std::vector<AsyncDownloadSubmission*> submissions;
    AsyncDownloadSubmission* submission{nullptr};
    std::function<void()> on_show;
    std::function<void()> on_set_submission;
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
    void setDownloadSubmission(AsyncDownloadSubmission* submission) override {
        if (state_->throws_on_set_submission)
            throw std::runtime_error("C:\\private\\secret https://private.example/?token=secret");
        state_->submission = submission;
        state_->submissions.push_back(submission);
        if (auto callback = std::exchange(state_->on_set_submission, {}))
            callback();
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
    void trustedDownloadSubmissionIsForwardedAndCanBeRevokedWhileOpen() {
        int factories = 0;
        FakeSubmission initial, replacement;
        auto state = std::make_shared<WindowState>();
        EmbeddedBrowserBinding binding{[&] {
            ++factories;
            return std::make_unique<FakeWindow>(state);
        }};
        binding.setOwnerHandle(42);
        binding.setDownloadSubmission(&initial);
        QCOMPARE(factories, 0);
        QVERIFY(state->submissions.empty());

        binding.open();
        QCOMPARE(factories, 1);
        QCOMPARE(state->submission, static_cast<AsyncDownloadSubmission*>(&initial));
        binding.setDownloadSubmission(&replacement);
        QCOMPARE(state->submission, static_cast<AsyncDownloadSubmission*>(&replacement));
        binding.setDownloadSubmission(nullptr);
        QCOMPARE(state->submission, nullptr);
        QCOMPARE(factories, 1);
        QCOMPARE(state->shown, 1);

        binding.open();
        QCOMPARE(factories, 1);
        QCOMPARE(state->shown, 2);
        QCOMPARE(state->submission, nullptr);
        QCOMPARE(initial.calls, 0);
        QCOMPARE(replacement.calls, 0);
    }

    void reentrantDownloadRevocationReachesTheWindowBeforeShow() {
        FakeSubmission submission;
        auto state = std::make_shared<WindowState>();
        EmbeddedBrowserBinding binding{[&] { return std::make_unique<FakeWindow>(state); }};
        binding.setOwnerHandle(42);
        binding.setDownloadSubmission(&submission);
        state->on_set_submission = [&] { binding.setDownloadSubmission(nullptr); };
        bool showedWithSubmission = false;
        state->on_show = [&] { showedWithSubmission = state->submission != nullptr; };

        binding.open();

        QCOMPARE(state->shown, 1);
        QVERIFY(!showedWithSubmission);
        QCOMPARE(state->submission, nullptr);
        QVERIFY(binding.errorText().isEmpty());
    }

    void throwingDownloadSetterRetiresTheOpenWindowWithoutLeakingDetails() {
        FakeSubmission submission;
        auto state = std::make_shared<WindowState>();
        EmbeddedBrowserBinding binding{[&] { return std::make_unique<FakeWindow>(state); }};
        binding.setOwnerHandle(42);
        binding.setDownloadSubmission(&submission);
        binding.open();
        QCOMPARE(state->submission, static_cast<AsyncDownloadSubmission*>(&submission));
        state->throws_on_set_submission = true;

        bool escaped = false;
        try {
            binding.setDownloadSubmission(nullptr);
        } catch (...) {
            escaped = true;
        }

        QVERIFY(!escaped);
        QCOMPARE(state->stopped, 1);
        QCOMPARE(state->destroyed, 1);
        QVERIFY(!binding.errorText().isEmpty());
        QVERIFY(!binding.errorText().contains(QStringLiteral("secret")));
        QVERIFY(!binding.errorText().contains(QStringLiteral("https://")));
    }
    void rejectedWindowDoesNotRestoreARevokedDownloadSubmissionOnRetry() {
        FakeSubmission submission;
        int factories = 0;
        auto rejected = std::make_shared<WindowState>();
        rejected->succeeds = false;
        auto retry = std::make_shared<WindowState>();
        EmbeddedBrowserBinding binding{
            [&] { return std::make_unique<FakeWindow>(++factories == 1 ? rejected : retry); }};
        binding.setOwnerHandle(42);
        binding.setDownloadSubmission(&submission);
        binding.open();
        QVERIFY(!binding.errorText().isEmpty());
        QCOMPARE(rejected->stopped, 1);
        QCOMPARE(rejected->destroyed, 1);

        binding.setDownloadSubmission(nullptr);
        binding.open();
        QCOMPARE(factories, 2);
        QCOMPARE(retry->shown, 1);
        QCOMPARE(retry->submission, nullptr);
        QVERIFY(binding.errorText().isEmpty());
        QCOMPARE(submission.calls, 0);
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

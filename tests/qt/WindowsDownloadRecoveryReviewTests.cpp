#include "adapters/WindowsDownloadRecoveryReview.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace music_player::m0 {
namespace {
struct Gate final {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{false};
    bool released{false};
    int factories{0};
    int calls{0};
    std::thread::id factory_thread{}, work_thread{}, destruction_thread{};
    std::string received;
    void release() {
        {
            const std::scoped_lock lock(mutex);
            released = true;
        }
        condition.notify_all();
    }
    bool hasEntered() {
        const std::scoped_lock lock(mutex);
        return entered;
    }
};
struct Release final {
    Gate& gate;
    ~Release() { gate.release(); }
};
struct Resource final {
    Gate& gate;
    ~Resource() {
        const std::scoped_lock lock(gate.mutex);
        gate.destruction_thread = std::this_thread::get_id();
    }
};
} // namespace
class WindowsDownloadRecoveryReviewTests final : public QObject {
    Q_OBJECT
  private slots:
    void review_is_private_bounded_and_completion_is_not_consumed_by_stale_poll() {
        Gate gate;
        const auto caller = std::this_thread::get_id();
        WindowsDownloadRecoveryReview review([&] {
            {
                const std::scoped_lock lock(gate.mutex);
                ++gate.factories;
                gate.factory_thread = std::this_thread::get_id();
            }
            auto resource = std::make_shared<Resource>(gate);
            return [&, resource](DownloadRecoveryReviewAction, std::string_view id) {
                std::unique_lock lock(gate.mutex);
                ++gate.calls;
                gate.received = id;
                gate.work_thread = std::this_thread::get_id();
                gate.entered = true;
                gate.condition.notify_all();
                gate.condition.wait(lock, [&] { return gate.released; });
                return DownloadRecoveryReviewOutcome::OwnedFinalCandidate;
            };
        });
        // This guard is destroyed BEFORE the worker's fallback join even when
        // a failed assertion returns early. No caller wait as a timing oracle.
        Release release{gate};
        QCOMPARE(review.startReview(7, "owned-1"), DownloadRecoveryReviewCommand::Accepted);
        QTRY_VERIFY(gate.hasEntered());
        QCOMPARE(review.startReview(8, "owned-2"), DownloadRecoveryReviewCommand::Busy);
        QVERIFY(!review.poll(7));
        QVERIFY(!review.poll(999));
        QVERIFY(!review.stopped());
        gate.release();
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(7)).has_value());
        QCOMPARE(result->operation_id, std::uint64_t{7});
        QCOMPARE(result->job_id, std::string{"owned-1"});
        QCOMPARE(result->outcome, DownloadRecoveryReviewOutcome::OwnedFinalCandidate);
        QVERIFY(!review.poll(7));
        QCOMPARE(review.startReview(7, "owned-1"), DownloadRecoveryReviewCommand::InvalidRequest);
        QCOMPARE(review.startReview(8, "owned-2"), DownloadRecoveryReviewCommand::Accepted);
        result.reset();
        QTRY_VERIFY(result.has_value() || (result = review.poll(8)).has_value());
        QCOMPARE(result->operation_id, std::uint64_t{8});
        review.shutdown();
        QTRY_VERIFY(review.stopped());
        const std::scoped_lock lock(gate.mutex);
        QCOMPARE(gate.factories, 1);
        QCOMPARE(gate.calls, 2);
        QCOMPARE(gate.received, std::string{"owned-2"});
        QVERIFY(gate.factory_thread != caller);
        QCOMPARE(gate.work_thread, gate.factory_thread);
        QCOMPARE(gate.destruction_thread, gate.factory_thread);
    }
    void shutdown_does_not_wait_or_acknowledge_while_read_is_active() {
        Gate gate;
        WindowsDownloadRecoveryReview review([&] {
            return [&](DownloadRecoveryReviewAction, std::string_view) {
                std::unique_lock lock(gate.mutex);
                gate.entered = true;
                gate.condition.notify_all();
                gate.condition.wait(lock, [&] { return gate.released; });
                return DownloadRecoveryReviewOutcome::Conflict;
            };
        });
        Release release{gate};
        QCOMPARE(review.startReview(1, "owned"), DownloadRecoveryReviewCommand::Accepted);
        QTRY_VERIFY(gate.hasEntered());
        review.shutdown();
        review.shutdown();
        QVERIFY(!review.stopped());
        QCOMPARE(review.startReview(2, "owned"), DownloadRecoveryReviewCommand::Closed);
        gate.release();
        QTRY_VERIFY(review.stopped());
        QVERIFY(!review.poll(2));
        const auto result = review.poll(1);
        QVERIFY(result);
        QCOMPARE(result->outcome, DownloadRecoveryReviewOutcome::Conflict);
    }
    void invalid_requests_do_not_construct_or_consume_an_id_data() {
        QTest::addColumn<std::uint64_t>("operation");
        QTest::addColumn<QByteArray>("id");
        QTest::newRow("zero") << std::uint64_t{0} << QByteArray{"valid"};
        QTest::newRow("empty") << std::uint64_t{1} << QByteArray{};
        QTest::newRow("long") << std::uint64_t{1} << QByteArray(65, 'a');
        QTest::newRow("path") << std::uint64_t{1} << QByteArray{"C:/secret.mp3"};
        QTest::newRow("url") << std::uint64_t{1} << QByteArray{"https://example.test"};
        QTest::newRow("nul") << std::uint64_t{1} << QByteArray{"a\0b", 3};
        QTest::newRow("unicode") << std::uint64_t{1} << QByteArray::fromHex("c3a1");
    }
    void invalid_requests_do_not_construct_or_consume_an_id() {
        QFETCH(std::uint64_t, operation);
        QFETCH(QByteArray, id);
        WindowsDownloadRecoveryReview review([] {
            return [](DownloadRecoveryReviewAction, std::string_view) {
                return DownloadRecoveryReviewOutcome::Unverified;
            };
        });
        QCOMPARE(review.startReview(operation, std::string_view{id.constData(), static_cast<std::size_t>(id.size())}),
                 DownloadRecoveryReviewCommand::InvalidRequest);
        QCOMPARE(review.startReview(1, std::string(64, 'a')), DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(1)).has_value());
        QCOMPARE(result->job_id.size(), std::size_t{64});
        review.shutdown();
        QTRY_VERIFY(review.stopped());
    }
    void unused_shutdown_never_constructs_a_worker_resource() {
        bool constructed = false;
        WindowsDownloadRecoveryReview review([&] {
            constructed = true;
            return WindowsDownloadRecoveryReview::Work{};
        });
        review.shutdown();
        QTRY_VERIFY(review.stopped());
        QVERIFY(!constructed);
        QCOMPARE(review.startReview(1, "owned"), DownloadRecoveryReviewCommand::Closed);
    }
    void invalid_or_throwing_work_has_only_a_typed_failure_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("factory-exception") << 0;
        QTest::newRow("null-work") << 1;
        QTest::newRow("work-exception") << 2;
        QTest::newRow("invalid-enum") << 3;
    }
    void invalid_or_throwing_work_has_only_a_typed_failure() {
        QFETCH(int, kind);
        WindowsDownloadRecoveryReview review([kind]() -> WindowsDownloadRecoveryReview::Work {
            if (kind == 0)
                throw std::runtime_error{"private path/secret"};
            if (kind == 1)
                return {};
            return [kind](DownloadRecoveryReviewAction, std::string_view) {
                if (kind == 2)
                    throw std::runtime_error{"private path/secret"};
                return static_cast<DownloadRecoveryReviewOutcome>(999);
            };
        });
        QCOMPARE(review.startReview(1, "owned"), DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(1)).has_value());
        QCOMPARE(result->outcome, DownloadRecoveryReviewOutcome::Unreadable);
        QCOMPARE(result->job_id, std::string{"owned"});
        review.shutdown();
        QTRY_VERIFY(review.stopped());
    }
    void trusted_missing_directory_is_not_created_or_repaired() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto journal = root.filePath("journal-not-created");
        WindowsDownloadRecoveryReview review(journal, {root.filePath("staging"), root.filePath("publication"), 64});
        QCOMPARE(review.startReview(1, "owned"), DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(1)).has_value());
        QCOMPARE(result->outcome, DownloadRecoveryReviewOutcome::Unreadable);
        review.shutdown();
        QTRY_VERIFY(review.stopped());
        QVERIFY(!QDir(journal).exists());
        QCOMPARE(QDir(root.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size(), 0);
    }
    void discard_runs_privately_and_shares_the_review_slot_until_result_is_observed() {
        Gate gate;
        DownloadRecoveryReviewAction received{};
        WindowsDownloadRecoveryReview review([&] {
            return [&](DownloadRecoveryReviewAction action, std::string_view id) {
                std::unique_lock lock(gate.mutex);
                received = action;
                gate.received = id;
                gate.work_thread = std::this_thread::get_id();
                gate.entered = true;
                gate.condition.notify_all();
                gate.condition.wait(lock, [&] { return gate.released; });
                return action == DownloadRecoveryReviewAction::DiscardPartials
                           ? DownloadRecoveryReviewOutcome::Discarded
                           : DownloadRecoveryReviewOutcome::Missing;
            };
        });
        Release release{gate};
        QVERIFY(review.supportsDiscardPartials());
        QCOMPARE(review.startDiscardPartials(1, "owned"), DownloadRecoveryReviewCommand::Accepted);
        QTRY_VERIFY(gate.hasEntered());
        QCOMPARE(review.startReview(2, "owned"), DownloadRecoveryReviewCommand::Busy);
        QCOMPARE(review.startDiscardPartials(2, "owned"), DownloadRecoveryReviewCommand::Busy);
        QVERIFY(!review.poll(99));
        gate.release();
        // Drain acknowledgement does not consume the pending observation.
        review.shutdown();
        QTRY_VERIFY(review.stopped());
        QVERIFY(!review.poll(2));
        const auto result = review.poll(1);
        QVERIFY(result);
        QCOMPARE(result->action, DownloadRecoveryReviewAction::DiscardPartials);
        QCOMPARE(result->outcome, DownloadRecoveryReviewOutcome::Discarded);
        QCOMPARE(result->operation_id, std::uint64_t{1});
        QCOMPARE(result->job_id, std::string{"owned"});
        QCOMPARE(received, DownloadRecoveryReviewAction::DiscardPartials);
        QVERIFY(gate.work_thread != std::this_thread::get_id());
        QCOMPARE(gate.received, std::string{"owned"});
        QCOMPARE(review.startDiscardPartials(2, "owned"), DownloadRecoveryReviewCommand::Closed);
    }
    void discard_invalid_requests_do_not_consume_an_operation_id() {
        WindowsDownloadRecoveryReview review([] {
            return
                [](DownloadRecoveryReviewAction, std::string_view) { return DownloadRecoveryReviewOutcome::Missing; };
        });
        for (const auto id : {std::string{}, std::string(65, 'a'), std::string{"C:/private"},
                              std::string{"https://example.test"}, std::string{"a\0b", 3}})
            QCOMPARE(review.startDiscardPartials(1, id), DownloadRecoveryReviewCommand::InvalidRequest);
        QCOMPARE(review.startDiscardPartials(0, "owned"), DownloadRecoveryReviewCommand::InvalidRequest);
        QCOMPARE(review.startDiscardPartials(1, "owned"), DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(1)).has_value());
        QCOMPARE(result->action, DownloadRecoveryReviewAction::DiscardPartials);
        QCOMPARE(review.startReview(1, "owned"), DownloadRecoveryReviewCommand::InvalidRequest);
        QCOMPARE(review.startReview(2, "owned"), DownloadRecoveryReviewCommand::Accepted);
        result.reset();
        QTRY_VERIFY(result.has_value() || (result = review.poll(2)).has_value());
        QCOMPARE(result->action, DownloadRecoveryReviewAction::ReviewFiles);
        review.shutdown();
        QTRY_VERIFY(review.stopped());
    }
    void malformed_or_throwing_discard_work_is_never_promoted_to_cleanup_success_data() {
        QTest::addColumn<bool>("discard");
        QTest::addColumn<int>("kind");
        QTest::newRow("discard-throw") << true << 0;
        QTest::newRow("discard-invalid-enum") << true << 1;
        QTest::newRow("discard-readonly-outcome") << true << 2;
        QTest::newRow("review-cannot-claim-discarded") << false << 3;
        QTest::newRow("review-cannot-claim-incomplete") << false << 4;
        QTest::newRow("review-cannot-claim-unconfirmed") << false << 5;
        QTest::newRow("discard-cannot-offer-cleanup") << true << 6;
    }
    void malformed_or_throwing_discard_work_is_never_promoted_to_cleanup_success() {
        QFETCH(bool, discard);
        QFETCH(int, kind);
        WindowsDownloadRecoveryReview review([kind] {
            return [kind](DownloadRecoveryReviewAction, std::string_view) {
                if (kind == 0)
                    throw std::runtime_error{"private detail"};
                if (kind == 1)
                    return static_cast<DownloadRecoveryReviewOutcome>(999);
                if (kind == 2)
                    return DownloadRecoveryReviewOutcome::OwnedPartial;
                if (kind == 3)
                    return DownloadRecoveryReviewOutcome::Discarded;
                if (kind == 4)
                    return DownloadRecoveryReviewOutcome::DiscardIncomplete;
                if (kind == 6)
                    return DownloadRecoveryReviewOutcome::CleanupCandidate;
                return DownloadRecoveryReviewOutcome::DiscardUnconfirmed;
            };
        });
        QCOMPARE(discard ? review.startDiscardPartials(1, "owned") : review.startReview(1, "owned"),
                 DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(1)).has_value());
        QCOMPARE(result->action,
                 discard ? DownloadRecoveryReviewAction::DiscardPartials : DownloadRecoveryReviewAction::ReviewFiles);
        QCOMPARE(result->outcome, discard ? DownloadRecoveryReviewOutcome::DiscardUnconfirmed
                                          : DownloadRecoveryReviewOutcome::Unreadable);
        review.shutdown();
        QTRY_VERIFY(review.stopped());
    }
    void discard_never_creates_missing_trusted_directories() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto journal = root.filePath("journal-not-created");
        WindowsDownloadRecoveryReview review(journal, {root.filePath("staging"), root.filePath("publication"), 64});
        QCOMPARE(review.startDiscardPartials(1, "owned"), DownloadRecoveryReviewCommand::Accepted);
        std::optional<DownloadRecoveryReviewResult> result;
        QTRY_VERIFY(result.has_value() || (result = review.poll(1)).has_value());
        QCOMPARE(result->action, DownloadRecoveryReviewAction::DiscardPartials);
        QCOMPARE(result->outcome, DownloadRecoveryReviewOutcome::Unreadable);
        review.shutdown();
        QTRY_VERIFY(review.stopped());
        QCOMPARE(QDir(root.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size(), 0);
    }
};
} // namespace music_player::m0
QTEST_GUILESS_MAIN(music_player::m0::WindowsDownloadRecoveryReviewTests)
#include "WindowsDownloadRecoveryReviewTests.moc"

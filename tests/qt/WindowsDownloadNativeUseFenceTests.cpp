#include "adapters/WindowsDownloadAppContainer.hpp"
#include "adapters/WindowsDownloadNativeUseFence.hpp"

#include <QTest>

#include <barrier>
#include <memory>
#include <thread>
#include <utility>

using namespace music_player::m0;
using State = WindowsDownloadNativeUseFence::State;
namespace {
QByteArray syntheticProfileSid() {
    QByteArray bytes(40, '\0');
    bytes[0] = 1;
    bytes[1] = 8;
    bytes[7] = 15;
    bytes[8] = 2;
    for (int index = 0; index < 7; ++index)
        bytes[12 + 4 * index] = static_cast<char>(index + 1);
    return bytes;
}
} // namespace

class WindowsDownloadNativeUseFenceTests final : public QObject {
    Q_OBJECT
  private slots:
    void onlyOnePendingTicketMayUseTheFence() {
        WindowsDownloadNativeUseFence fence;
        QCOMPARE(fence.state(), State::OpenIdle);
        auto ticket = fence.begin();
        QVERIFY(ticket);
        QCOMPARE(fence.state(), State::OpenBusy);
        QVERIFY(!fence.begin());
        QCOMPARE(fence.state(), State::OpenBusy);
        QVERIFY(ticket->acknowledgeNoChild());
    }
    void explicitAcknowledgementReopensAdmission_data() {
        QTest::addColumn<bool>("native");
        QTest::newRow("known no child") << false;
        QTest::newRow("native and pipes stopped") << true;
    }
    void explicitAcknowledgementReopensAdmission() {
        QFETCH(bool, native);
        WindowsDownloadNativeUseFence fence;
        auto ticket = fence.begin();
        QVERIFY(ticket);
        QVERIFY(native ? ticket->acknowledgeNativeStopped() : ticket->acknowledgeNoChild());
        QCOMPARE(fence.state(), State::OpenIdle);
        QVERIFY(!ticket->acknowledgeNoChild());
        QVERIFY(!ticket->acknowledgeNativeStopped());
        ticket.reset();
        QCOMPARE(fence.state(), State::OpenIdle);
    }
    void closingIdleAdmissionIsIrreversible() {
        WindowsDownloadNativeUseFence fence;
        QCOMPARE(fence.closeAdmission(), State::ClosedQuiescent);
        QCOMPARE(fence.state(), State::ClosedQuiescent);
        QVERIFY(!fence.begin());
        QCOMPARE(fence.closeAdmission(), State::ClosedQuiescent);
    }
    void closingBusyAdmissionWaitsForExplicitAcknowledgement() {
        WindowsDownloadNativeUseFence fence;
        auto ticket = fence.begin();
        QVERIFY(ticket);
        QCOMPARE(fence.closeAdmission(), State::ClosingBusy);
        QCOMPARE(fence.state(), State::ClosingBusy);
        QVERIFY(!fence.begin());
        QCOMPARE(fence.closeAdmission(), State::ClosingBusy);
        QVERIFY(ticket->acknowledgeNativeStopped());
        QCOMPARE(fence.state(), State::ClosedQuiescent);
        ticket.reset();
        QVERIFY(!fence.begin());
    }
    void pendingAbandonmentMakesUncertaintySticky_data() {
        QTest::addColumn<bool>("closing");
        QTest::newRow("open") << false;
        QTest::newRow("closing") << true;
    }
    void pendingAbandonmentMakesUncertaintySticky() {
        QFETCH(bool, closing);
        WindowsDownloadNativeUseFence fence;
        auto ticket = fence.begin();
        QVERIFY(ticket);
        if (closing)
            QCOMPARE(fence.closeAdmission(), State::ClosingBusy);
        ticket.reset();
        QCOMPARE(fence.state(), State::Uncertain);
        QVERIFY(!fence.begin());
        QCOMPARE(fence.closeAdmission(), State::Uncertain);
        QCOMPARE(fence.state(), State::Uncertain);
    }
    void movingATicketTransfersItsPendingUse() {
        WindowsDownloadNativeUseFence fence;
        auto original = fence.begin();
        QVERIFY(original);
        WindowsDownloadNativeUseFence::Ticket moved{std::move(*original)};
        QVERIFY(!original->acknowledgeNativeStopped());
        original.reset();
        QCOMPARE(fence.state(), State::OpenBusy);
        QVERIFY(moved.acknowledgeNoChild());
        QCOMPARE(fence.state(), State::OpenIdle);
    }
    void acknowledgedTicketCannotAffectItsSuccessor() {
        WindowsDownloadNativeUseFence fence;
        auto first = fence.begin();
        QVERIFY(first);
        QVERIFY(first->acknowledgeNoChild());
        auto second = fence.begin();
        QVERIFY(second);
        QVERIFY(!first->acknowledgeNoChild());
        QVERIFY(!first->acknowledgeNativeStopped());
        first.reset();
        QCOMPARE(fence.state(), State::OpenBusy);
        QVERIFY(second->acknowledgeNativeStopped());
        second.reset();
        QCOMPARE(fence.state(), State::OpenIdle);
    }
    void moveAssignmentAbandonsOnlyItsPreviousPendingUse() {
        WindowsDownloadNativeUseFence first_fence, second_fence;
        auto first = first_fence.begin();
        auto second = second_fence.begin();
        QVERIFY(first);
        QVERIFY(second);
        *first = std::move(*second);
        QCOMPARE(first_fence.state(), State::Uncertain);
        QCOMPARE(second_fence.state(), State::OpenBusy);
        second.reset();
        QCOMPARE(second_fence.state(), State::OpenBusy);
        QVERIFY(first->acknowledgeNativeStopped());
        QCOMPARE(second_fence.state(), State::OpenIdle);
    }
    void ticketKeepsItsStateAliveAfterTheFenceObject() {
        auto fence = std::make_unique<WindowsDownloadNativeUseFence>();
        auto ticket = fence->begin();
        QVERIFY(ticket);
        fence.reset();
        QVERIFY(ticket->acknowledgeNoChild());
        QVERIFY(!ticket->acknowledgeNativeStopped());
    }
    void beginAndCloseAdmissionCannotPublishQuiescenceWithAnActiveTicket() {
        WindowsDownloadNativeUseFence fence;
        std::barrier ready{3};
        std::optional<WindowsDownloadNativeUseFence::Ticket> ticket;
        State closed{};
        std::thread beginning([&] {
            ready.arrive_and_wait();
            ticket = fence.begin();
        });
        std::thread closing([&] {
            ready.arrive_and_wait();
            closed = fence.closeAdmission();
        });
        ready.arrive_and_wait();
        beginning.join();
        closing.join();
        if (ticket) {
            QCOMPARE(closed, State::ClosingBusy);
            QVERIFY(ticket->acknowledgeNoChild());
        } else {
            QCOMPARE(closed, State::ClosedQuiescent);
        }
        QCOMPARE(fence.state(), State::ClosedQuiescent);
        QVERIFY(!fence.begin());
    }
    void capabilityRetainsOnlyItsExplicitOptionalFence() {
        const auto sid = syntheticProfileSid();
        auto fence = std::make_shared<WindowsDownloadNativeUseFence>();
        const auto unfenced = WindowsDownloadAppContainer::fromTrustedSid(sid);
        QVERIFY(unfenced);
        QVERIFY(!unfenced->nativeUseFence());
        auto fenced = WindowsDownloadAppContainer::fromTrustedSid(sid, fence);
        QVERIFY(fenced);
        QCOMPARE(fenced->nativeUseFence(), fence);
        std::weak_ptr<WindowsDownloadNativeUseFence> weak = fence;
        fence.reset();
        QVERIFY(!weak.expired());
        fenced.reset();
        QVERIFY(weak.expired());
        QVERIFY(!unfenced->nativeUseFence());
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadNativeUseFenceTests)
#include "WindowsDownloadNativeUseFenceTests.moc"

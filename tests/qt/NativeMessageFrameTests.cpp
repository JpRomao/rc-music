#include "NativeMessageFrame.hpp"

#include <QTest>

#include <limits>

using music_player::m0::encodeNativeMessageFrame;
using music_player::m0::native_message_max_payload_bytes;
using music_player::m0::NativeFrameStatus;
using music_player::m0::NativeMessageFrame;

namespace {

[[nodiscard]] QByteArray windowsPrefix(quint32 length) {
    QByteArray prefix(4, '\0');
    for (quint32 index = 0; index < 4; ++index)
        prefix[index] = static_cast<char>((length >> (index * 8)) & 0xff);
    return prefix;
}

[[nodiscard]] QByteArray windowsFrame(const QByteArray& payload) {
    return windowsPrefix(static_cast<quint32>(payload.size())) + payload;
}

} // namespace

class NativeMessageFrameTests final : public QObject {
    Q_OBJECT

  private slots:
    void encodesWindowsLittleEndianLengthAndPreservesBinaryPayload() {
        const QByteArray payload(258, '\xff');
        const auto encoded = encodeNativeMessageFrame(payload);
        QCOMPARE(encoded.size(), payload.size() + 4);
        QCOMPARE(encoded.first(4), QByteArray::fromHex("02010000"));
        QCOMPARE(encoded.sliced(4), payload);
        QCOMPARE(encodeNativeMessageFrame(QByteArray::fromHex("000a0dff")), QByteArray::fromHex("04000000000a0dff"));
    }

    void rejectsEmptyAndOversizedEncoding() {
        QVERIFY(encodeNativeMessageFrame({}).isEmpty());
        QVERIFY(encodeNativeMessageFrame(QByteArray(native_message_max_payload_bytes + 1, 'x')).isEmpty());
    }

    void roundTripsMaximumInclusivePayload() {
        const QByteArray payload(native_message_max_payload_bytes, 'x');
        const auto encoded = encodeNativeMessageFrame(payload);
        QCOMPARE(encoded.size(), native_message_max_payload_bytes + 4);
        QCOMPARE(encoded.first(4), QByteArray::fromHex("00000400"));

        NativeMessageFrame receiver;
        const auto result = receiver.receive(encoded, 1);
        QCOMPARE(result.status, NativeFrameStatus::Complete);
        QCOMPARE(result.payload, payload);
    }

    void completesOneBinaryFrameWithoutJsonInterpretation() {
        const auto payload = QByteArray::fromHex("007b7dff0a");
        NativeMessageFrame receiver(42);
        const auto result = receiver.receive(windowsFrame(payload), 43);
        QCOMPARE(result.status, NativeFrameStatus::Complete);
        QCOMPARE(result.payload, payload);
    }

    void acceptsEveryFragmentBoundaryWithoutPublishingPartialBytes() {
        const QByteArray payload{"{\"text\":\"M\xc3\xbasica\"}"};
        const auto frame = windowsFrame(payload);
        for (qsizetype split = 0; split < frame.size(); ++split) {
            NativeMessageFrame receiver;
            const auto partial = receiver.receive(QByteArrayView{frame}.first(split), 1);
            QCOMPARE(partial.status, NativeFrameStatus::Pending);
            QVERIFY(partial.payload.isEmpty());
            const auto complete = receiver.receive(QByteArrayView{frame}.sliced(split), 2);
            QCOMPARE(complete.status, NativeFrameStatus::Complete);
            QCOMPARE(complete.payload, payload);
        }
    }

    void acceptsPrefixAndBodyOneByteAtATime() {
        const QByteArray payload(1024, 'a');
        const auto frame = windowsFrame(payload);
        NativeMessageFrame receiver;
        for (qsizetype offset = 0; offset < frame.size() - 1; ++offset) {
            const auto result = receiver.receive(QByteArrayView{frame}.sliced(offset, 1), offset);
            QCOMPARE(result.status, NativeFrameStatus::Pending);
            QVERIFY(result.payload.isEmpty());
        }
        const auto result = receiver.receive(QByteArrayView{frame}.last(1), frame.size());
        QCOMPARE(result.status, NativeFrameStatus::Complete);
        QCOMPARE(result.payload, payload);
    }

    void rejectsInvalidLengths_data() {
        QTest::addColumn<QByteArray>("prefix");
        QTest::newRow("zero") << windowsPrefix(0);
        QTest::newRow("limit plus one") << windowsPrefix(native_message_max_payload_bytes + 1);
        QTest::newRow("signed 32-bit maximum") << windowsPrefix(0x7fffffff);
        QTest::newRow("unsigned 32-bit maximum") << windowsPrefix(0xffffffff);
        QTest::newRow("32-bit addition wraps") << windowsPrefix(0xfffffffd);
        QTest::newRow("OpenSources big-endian one is not native one") << QByteArray::fromHex("00000001");
    }

    void rejectsInvalidLengths() {
        QFETCH(QByteArray, prefix);
        NativeMessageFrame receiver;
        const auto result = receiver.receive(prefix, 0);
        QCOMPARE(result.status, NativeFrameStatus::Invalid);
        QVERIFY(result.payload.isEmpty());
        QCOMPARE(receiver.poll(1).status, NativeFrameStatus::Invalid);
        QCOMPARE(receiver.receive(windowsFrame("{}"), 2).status, NativeFrameStatus::Invalid);
    }

    void rejectsOversizedFragmentedLengthBeforeReceivingTheBody() {
        const auto prefix = windowsPrefix(native_message_max_payload_bytes + 1);
        NativeMessageFrame receiver;
        QCOMPARE(receiver.receive(QByteArrayView{prefix}.first(3), 1).status, NativeFrameStatus::Pending);
        const auto result = receiver.receive(QByteArrayView{prefix}.last(1), 2);
        QCOMPARE(result.status, NativeFrameStatus::Invalid);
        QVERIFY(result.payload.isEmpty());
    }

    void rejectsExcessInputEvenWhenAnEarlierFrameIsComplete_data() {
        QTest::addColumn<QByteArray>("extra");
        QTest::newRow("one extra byte") << QByteArray{"x"};
        QTest::newRow("another frame") << windowsFrame("second");
        QTest::newRow("larger than maximum frame") << QByteArray(native_message_max_payload_bytes + 5, 'x');
    }

    void rejectsExcessInputEvenWhenAnEarlierFrameIsComplete() {
        QFETCH(QByteArray, extra);
        const auto frame = windowsFrame("first");
        for (qsizetype split = 0; split < frame.size(); ++split) {
            NativeMessageFrame receiver;
            QCOMPARE(receiver.receive(QByteArrayView{frame}.first(split), 0).status, NativeFrameStatus::Pending);
            const auto result = receiver.receive(frame.sliced(split) + extra, 1);
            QCOMPARE(result.status, NativeFrameStatus::Invalid);
            QVERIFY(result.payload.isEmpty());
            QCOMPARE(receiver.poll(2).status, NativeFrameStatus::Invalid);
        }
    }

    void doesNotPublishCompletedPayloadAgainOrAcceptASecondFrame() {
        NativeMessageFrame receiver;
        const auto complete = receiver.receive(windowsFrame("one"), 1);
        QCOMPARE(complete.status, NativeFrameStatus::Complete);
        QCOMPARE(complete.payload, QByteArray{"one"});
        const auto observed = receiver.poll(2);
        QCOMPARE(observed.status, NativeFrameStatus::Complete);
        QVERIFY(observed.payload.isEmpty());
        const auto second = receiver.receive(windowsFrame("two"), 3);
        QCOMPARE(second.status, NativeFrameStatus::Invalid);
        QVERIFY(second.payload.isEmpty());
        QCOMPARE(receiver.poll(4).status, NativeFrameStatus::Invalid);
    }

    void rejectsEmptyInputAfterACompletedFrame() {
        NativeMessageFrame receiver;
        QCOMPARE(receiver.receive(windowsFrame("one"), 1).status, NativeFrameStatus::Complete);
        const auto result = receiver.receive({}, 2);
        QCOMPARE(result.status, NativeFrameStatus::Invalid);
        QVERIFY(result.payload.isEmpty());
    }

    void expiresAtTheInclusiveDefaultDeadline() {
        NativeMessageFrame receiver(100);
        QCOMPARE(receiver.poll(5099).status, NativeFrameStatus::Pending);
        const auto expired = receiver.poll(5100);
        QCOMPARE(expired.status, NativeFrameStatus::Expired);
        QVERIFY(expired.payload.isEmpty());
        QCOMPARE(receiver.poll(5101).status, NativeFrameStatus::Expired);
        QCOMPARE(receiver.receive(windowsFrame("late"), 5102).status, NativeFrameStatus::Invalid);
    }

    void rejectsACompleteFrameArrivingAtItsDeadline() {
        NativeMessageFrame receiver(100, 5);
        const auto result = receiver.receive(windowsFrame("late"), 105);
        QCOMPARE(result.status, NativeFrameStatus::Expired);
        QVERIFY(result.payload.isEmpty());
    }

    void expiresIncompletePrefixAndBody_data() {
        QTest::addColumn<QByteArray>("partial");
        QTest::newRow("no bytes") << QByteArray{};
        QTest::newRow("partial prefix") << QByteArray::fromHex("020000");
        QTest::newRow("only prefix") << windowsPrefix(2);
        QTest::newRow("partial body") << (windowsPrefix(2) + QByteArray{"x"});
    }

    void expiresIncompletePrefixAndBody() {
        QFETCH(QByteArray, partial);
        NativeMessageFrame receiver(10, 20);
        QCOMPARE(receiver.receive(partial, 29).status, NativeFrameStatus::Pending);
        const auto result = receiver.poll(30);
        QCOMPARE(result.status, NativeFrameStatus::Expired);
        QVERIFY(result.payload.isEmpty());
    }

    void newFragmentsDoNotExtendTheTotalDeadline() {
        const auto frame = windowsFrame("abc");
        NativeMessageFrame receiver(100, 20);
        QCOMPARE(receiver.receive(QByteArrayView{frame}.first(2), 105).status, NativeFrameStatus::Pending);
        QCOMPARE(receiver.receive(QByteArrayView{frame}.sliced(2, 3), 119).status, NativeFrameStatus::Pending);
        const auto result = receiver.receive(QByteArrayView{frame}.sliced(5), 120);
        QCOMPARE(result.status, NativeFrameStatus::Expired);
        QVERIFY(result.payload.isEmpty());
    }

    void rejectsBackwardsClock_data() {
        QTest::addColumn<qint64>("start");
        QTest::addColumn<qint64>("first");
        QTest::addColumn<qint64>("backwards");
        QTest::newRow("before start") << qint64{100} << qint64{100} << qint64{99};
        QTest::newRow("after previous receive") << qint64{100} << qint64{102} << qint64{101};
        QTest::newRow("negative values") << qint64{-100} << qint64{-90} << qint64{-91};
    }

    void rejectsBackwardsClock() {
        QFETCH(qint64, start);
        QFETCH(qint64, first);
        QFETCH(qint64, backwards);
        NativeMessageFrame receiver(start);
        QCOMPARE(receiver.receive(QByteArray::fromHex("0100"), first).status, NativeFrameStatus::Pending);
        const auto result = receiver.receive(QByteArray::fromHex("000078"), backwards);
        QCOMPARE(result.status, NativeFrameStatus::Invalid);
        QVERIFY(result.payload.isEmpty());
    }

    void pollAlsoDetectsABackwardsClock() {
        NativeMessageFrame receiver(100);
        QCOMPARE(receiver.poll(110).status, NativeFrameStatus::Pending);
        QCOMPARE(receiver.poll(109).status, NativeFrameStatus::Invalid);
    }

    void handlesClocksAcrossSignedLimitsWithoutDeadlineOverflow() {
        constexpr auto maximum = std::numeric_limits<qint64>::max();
        constexpr auto minimum = std::numeric_limits<qint64>::min();

        NativeMessageFrame above_signed_deadline(maximum - 2, 5);
        QCOMPARE(above_signed_deadline.poll(maximum).status, NativeFrameStatus::Pending);
        QCOMPARE(above_signed_deadline.receive(windowsFrame("x"), maximum).status, NativeFrameStatus::Complete);

        NativeMessageFrame long_elapsed(minimum, maximum);
        QCOMPARE(long_elapsed.poll(-2).status, NativeFrameStatus::Pending);
        QCOMPARE(long_elapsed.poll(-1).status, NativeFrameStatus::Expired);

        NativeMessageFrame largest_elapsed(minimum, maximum);
        QCOMPARE(largest_elapsed.poll(maximum).status, NativeFrameStatus::Expired);
    }

    void rejectsNonPositiveTimeouts_data() {
        QTest::addColumn<qint64>("timeout");
        QTest::newRow("zero") << qint64{0};
        QTest::newRow("negative") << qint64{-1};
        QTest::newRow("signed minimum") << std::numeric_limits<qint64>::min();
    }

    void rejectsNonPositiveTimeouts() {
        QFETCH(qint64, timeout);
        NativeMessageFrame receiver(0, timeout);
        QCOMPARE(receiver.poll(0).status, NativeFrameStatus::Invalid);
        const auto result = receiver.receive(windowsFrame("x"), 0);
        QCOMPARE(result.status, NativeFrameStatus::Invalid);
        QVERIFY(result.payload.isEmpty());
    }
};

QTEST_APPLESS_MAIN(NativeMessageFrameTests)

#include "NativeMessageFrameTests.moc"

#include "adapters/NativeMessageFrame.hpp"
#include "adapters/WindowsNativeMessagingIo.hpp"

#include <QScopeGuard>
#include <QTest>

#include <qt_windows.h>

#include <limits>

using namespace music_player::m0;
namespace {
struct Pipe final {
    HANDLE reader{INVALID_HANDLE_VALUE};
    HANDLE writer{INVALID_HANDLE_VALUE};
    explicit Pipe(const DWORD buffer_size = 4096) {
        if (!CreatePipe(&reader, &writer, nullptr, buffer_size))
            reader = writer = INVALID_HANDLE_VALUE;
    }
    ~Pipe() {
        if (reader != INVALID_HANDLE_VALUE)
            CloseHandle(reader);
        if (writer != INVALID_HANDLE_VALUE)
            CloseHandle(writer);
    }
    void closeWriter() {
        CloseHandle(writer);
        writer = INVALID_HANDLE_VALUE;
    }
    void closeReader() {
        CloseHandle(reader);
        reader = INVALID_HANDLE_VALUE;
    }
};
bool put(Pipe& pipe, const QByteArray& bytes) {
    DWORD written = 0;
    return WriteFile(pipe.writer, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
           written == static_cast<DWORD>(bytes.size());
}
QByteArray take(Pipe& pipe) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe.reader, nullptr, 0, nullptr, &available, nullptr) || available == 0)
        return {};
    QByteArray bytes{static_cast<qsizetype>(available), Qt::Uninitialized};
    DWORD received = 0;
    if (!ReadFile(pipe.reader, bytes.data(), available, &received, nullptr))
        return {};
    bytes.resize(received);
    return bytes;
}
} // namespace

class WindowsNativeMessagingIoTests final : public QObject {
    Q_OBJECT
  private slots:
    void rejectsNullInvalidAndNonPipeHandles() {
        Pipe pipe;
        WindowsNativeMessagingIo null_input{nullptr, pipe.writer};
        QVERIFY(!null_input.valid());
        WindowsNativeMessagingIo invalid_output{pipe.reader, INVALID_HANDLE_VALUE};
        QVERIFY(!invalid_output.valid());
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        QVERIFY(event != nullptr);
        const auto release = qScopeGuard([&] { CloseHandle(event); });
        WindowsNativeMessagingIo wrong_kind{event, pipe.writer};
        QVERIFY(!wrong_kind.valid());
    }
    void readsBorrowedBytePipesWithoutTextTranslationOrBlockingOnEmptyInput() {
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        QCOMPARE(io.readAvailable().state, NativeStdioInputState::Waiting);
        const auto bytes = QByteArray::fromHex("000a0d1ac3a1e99fb3");
        QVERIFY(put(input, bytes));
        const auto received = io.readAvailable();
        QCOMPARE(received.state, NativeStdioInputState::Data);
        QCOMPARE(received.bytes, bytes);
        QCOMPARE(io.readAvailable().state, NativeStdioInputState::Waiting);
        input.closeWriter();
        QCOMPARE(io.readAvailable().state, NativeStdioInputState::Closed);
        io.beginShutdown();
        QVERIFY(io.stopped());
        QVERIFY(put(output, "borrowed-output-still-open"));
    }
    void retainsAlreadyAvailableTrailingBytesAtFormerChunkBoundary() {
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        // It fits this pipe's buffer. No arbitrary read-chunk boundary hides
        // the extra byte from NativeDownloadSession's all-or-nothing frame.
        const auto frame = encodeNativeMessageFrame(QByteArray(2044, 'x'));
        QCOMPARE(frame.size(), qsizetype{2048});
        QVERIFY(put(input, frame + 'x'));
        const auto received = io.readAvailable();
        QCOMPARE(received.state, NativeStdioInputState::Data);
        QCOMPARE(received.bytes, frame + 'x');
        NativeMessageFrame assembler;
        QCOMPARE(assembler.receive(received.bytes, 0).status, NativeFrameStatus::Invalid);
    }
    void writesExactlyOneBinaryFrameAndObservesPhysicalWriterStop() {
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        const auto frame = encodeNativeMessageFrame(QByteArray::fromHex("7b220a0d1a007d"));
        QVERIFY(io.beginWrite(frame, 10));
        QVERIFY(!io.beginWrite(frame, 10));
        QTRY_VERIFY_WITH_TIMEOUT(io.stopped(), 2000);
        io.poll(11);
        QCOMPARE(io.outputState(), NativeStdioOutputState::Completed);
        QCOMPARE(take(output), frame);
        io.beginShutdown();
        QVERIFY(io.stopped());
        QVERIFY(!io.beginWrite(frame, 12));
        QVERIFY(put(output, "borrowed-output-still-open"));
    }
    void boundsEntireAvailableInputBeforeAllocation_data() {
        QTest::addColumn<qsizetype>("size");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("frame-plus-one-byte-for-rejection") << native_message_max_payload_bytes + 5 << true;
        QTest::newRow("over-snapshot-cap") << native_message_max_payload_bytes + 6 << false;
    }
    void boundsEntireAvailableInputBeforeAllocation() {
        QFETCH(qsizetype, size);
        QFETCH(bool, accepted);
        Pipe input{512 * 1024}, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        QVERIFY(put(input, QByteArray(size, 'x')));
        const auto received = io.readAvailable();
        QCOMPARE(received.state, accepted ? NativeStdioInputState::Data : NativeStdioInputState::Failed);
        QCOMPARE(received.bytes.size(), accepted ? size : qsizetype{0});
    }
    void refusesEmptyOversizedOrNegativeTimeWritesWithoutStartingAThread() {
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        QVERIFY(!io.beginWrite({}, 0));
        QVERIFY(!io.beginWrite(QByteArray(native_stdio_max_response_bytes + 1, 'x'), 0));
        QVERIFY(!io.beginWrite("x", -1));
        QVERIFY(io.stopped());
        QCOMPARE(io.outputState(), NativeStdioOutputState::Idle);
        QCOMPARE(take(output), QByteArray{});
    }
    void rejectsInvalidTrustedInputCeilings_data() {
        QTest::addColumn<qsizetype>("ceiling");
        QTest::newRow("negative") << qsizetype{-1};
        QTest::newRow("zero") << qsizetype{0};
        QTest::newRow("above-principal-limit") << native_message_max_payload_bytes + 1;
        QTest::newRow("overflow") << std::numeric_limits<qsizetype>::max();
    }
    void rejectsInvalidTrustedInputCeilings() {
        QFETCH(qsizetype, ceiling);
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer, ceiling};
        QVERIFY(!io.valid());
        QCOMPARE(io.readAvailable().state, NativeStdioInputState::Failed);
        QVERIFY(io.stopped());
    }
    void enforcesTheTrustedYoutubeSnapshotCeilingBeforeReading_data() {
        QTest::addColumn<qsizetype>("size");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("youtube-frame-and-excess-byte") << qsizetype{64 * 1024 + 5} << true;
        QTest::newRow("above-youtube-snapshot") << qsizetype{64 * 1024 + 6} << false;
    }
    void enforcesTheTrustedYoutubeSnapshotCeilingBeforeReading() {
        QFETCH(qsizetype, size);
        QFETCH(bool, accepted);
        Pipe input{128 * 1024}, output;
        WindowsNativeMessagingIo io{input.reader, output.writer, 64 * 1024};
        QVERIFY(io.valid());
        QVERIFY(put(input, QByteArray(size, 'x')));
        const auto received = io.readAvailable();
        QCOMPARE(received.state, accepted ? NativeStdioInputState::Data : NativeStdioInputState::Failed);
        QCOMPARE(received.bytes.size(), accepted ? size : qsizetype{0});
        DWORD available{};
        QVERIFY(PeekNamedPipe(input.reader, nullptr, 0, nullptr, &available, nullptr));
        QCOMPARE(available, accepted ? DWORD{0} : static_cast<DWORD>(size));
        if (!accepted)
            QCOMPARE(io.readAvailable().state, NativeStdioInputState::Closed);
    }
    void brokenOutputCannotBecomeCompleted() {
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        output.closeReader();
        QVERIFY(io.beginWrite("bounded-response", 0));
        QTRY_VERIFY_WITH_TIMEOUT(io.stopped(), 2000);
        io.poll(1);
        QCOMPARE(io.outputState(), NativeStdioOutputState::Failed);
    }
    void cancelsStalledOutputUntilNativeStopWithoutClosingBorrowedHandles() {
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        QVERIFY(put(output, QByteArray(4096, 'x')));
        QVERIFY(io.beginWrite(QByteArray(512, 'y'), 0));
        QTest::qWait(10);
        QVERIFY(!io.stopped());
        io.beginShutdown();
        QTRY_VERIFY_WITH_TIMEOUT((io.poll(1), io.stopped()), 2000);
        QCOMPARE(io.outputState(), NativeStdioOutputState::Failed);
        QCOMPARE(take(output), QByteArray(4096, 'x'));
        QVERIFY(put(output, "still-open"));
    }
    void deadlineAndRegressiveClockFailClosedWhileDrainingWriter_data() {
        QTest::addColumn<qint64>("now");
        QTest::newRow("inclusive-deadline") << qint64{2010};
        QTest::newRow("regression") << qint64{9};
    }
    void deadlineAndRegressiveClockFailClosedWhileDrainingWriter() {
        QFETCH(qint64, now);
        Pipe input, output;
        WindowsNativeMessagingIo io{input.reader, output.writer};
        QVERIFY(io.valid());
        QVERIFY(put(output, QByteArray(4096, 'x')));
        QVERIFY(io.beginWrite(QByteArray(512, 'y'), 10));
        io.poll(now);
        QTRY_VERIFY_WITH_TIMEOUT((io.poll(now), io.stopped()), 2000);
        QCOMPARE(io.outputState(), NativeStdioOutputState::Failed);
        QCOMPARE(take(output), QByteArray(4096, 'x'));
    }
};

QTEST_GUILESS_MAIN(WindowsNativeMessagingIoTests)
#include "WindowsNativeMessagingIoTests.moc"

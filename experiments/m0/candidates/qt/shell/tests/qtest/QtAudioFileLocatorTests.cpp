#include "adapters/QtAudioFileLocator.hpp"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>

#include <memory>
#include <stdexcept>
#include <vector>

namespace music_player::m0::test {
namespace {

[[nodiscard]] std::filesystem::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().toStdString());
#endif
}

class OffscreenFileDialog final : public QFileDialog {
  public:
    void setVisible(bool visible) override {
        if (visible)
            QTest::ignoreMessage(QtWarningMsg, "This plugin does not support propagateSizeHints()");
        QFileDialog::setVisible(visible);
    }
};

[[nodiscard]] QFileDialog* widgetDialog() {
    auto* dialog = new OffscreenFileDialog;
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setAttribute(Qt::WA_DontShowOnScreen);
    return dialog;
}

[[nodiscard]] std::filesystem::path missingPath() { return nativePath(QStringLiteral("Z:/ausente/música.wav")); }

} // namespace

class QtAudioFileLocatorTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void init() { QTest::failOnWarning(); }

    void opensOneAsynchronousSingleLocalFileDialogAndKeepsTimersRunning() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        int factory_calls{0};
        QtAudioFileLocator locator(QStringLiteral("Localizar arquivo"), QStringLiteral("Áudio (*.wav)"), [&] {
            ++factory_calls;
            dialog = widgetDialog();
            return dialog.data();
        });
        locator.bindWindow(window);
        std::vector<AudioFileLocationResult> results;
        QVERIFY(locator.locate(missingPath(), [&](auto result) { results.push_back(std::move(result)); }));
        QVERIFY(dialog);
        QCOMPARE(dialog->fileMode(), QFileDialog::ExistingFile);
        QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptOpen);
        QCOMPARE(dialog->supportedSchemes(), QStringList{QStringLiteral("file")});
        QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        QCOMPARE(dialog->windowHandle()->transientParent(), &window);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Localizar arquivo"));
        QCOMPARE(dialog->nameFilters(), QStringList{QStringLiteral("Áudio (*.wav)")});
        QVERIFY(!dialog->directory().absolutePath().startsWith(QStringLiteral("Z:")));
        QVERIFY(!locator.locate(missingPath(), [](auto) {}));
        QCOMPARE(factory_calls, 1);
        bool timer_ran{false};
        QTimer::singleShot(0, &locator, [&] { timer_ran = true; });
        QTRY_VERIFY(timer_ran);
        QVERIFY(results.empty());
        dialog->reject();
        QCOMPARE(results.size(), std::size_t{1});
        QCOMPARE(results.front().status, AudioFileLocationStatus::Cancelled);
        QVERIFY(results.front().source_path.empty());
        QTRY_VERIFY(dialog.isNull());
    }

    void selectedUnicodePathCompletesExactlyOnceWithoutReadingItsContents() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("arquivo 日本語.wav"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("picker only"), qint64{11});
        file.close();
        QWindow window;
        QPointer<QFileDialog> dialog;
        QtAudioFileLocator locator(QStringLiteral("Localizar"), QStringLiteral("Todos (*)"), [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        locator.bindWindow(window);
        std::vector<AudioFileLocationResult> results;
        QVERIFY(locator.locate(missingPath(), [&](auto result) { results.push_back(std::move(result)); }));
        dialog->selectFile(source);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
        QCOMPARE(results.size(), std::size_t{1});
        QCOMPARE(results.front().status, AudioFileLocationStatus::Selected);
        QCOMPARE(results.front().source_path, nativePath(source));
        if (dialog)
            QVERIFY(QMetaObject::invokeMethod(dialog, "finished", Qt::DirectConnection, Q_ARG(int, QDialog::Accepted)));
        QCOMPARE(results.size(), std::size_t{1});
        QTRY_VERIFY(dialog.isNull());
    }

    void cancelDiscardsCompletionAndAllowsANewRequest() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        QtAudioFileLocator locator({}, {}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        locator.bindWindow(window);
        int calls{0};
        QVERIFY(locator.locate(missingPath(), [&](auto) { ++calls; }));
        const QPointer<QFileDialog> cancelled = dialog;
        locator.cancel();
        locator.cancel();
        if (cancelled)
            QVERIFY(
                QMetaObject::invokeMethod(cancelled, "finished", Qt::DirectConnection, Q_ARG(int, QDialog::Accepted)));
        QCOMPARE(calls, 0);
        QVERIFY(locator.locate(missingPath(), [&](auto) { ++calls; }));
        QVERIFY(dialog != cancelled);
        dialog->reject();
        QCOMPARE(calls, 1);
        QTRY_VERIFY(cancelled.isNull());
        QTRY_VERIFY(dialog.isNull());
    }

    void completionMayStartAnotherRequestOrDestroyTheLocator() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        auto locator = std::make_unique<QtAudioFileLocator>(QString{}, QString{}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        locator->bindWindow(window);
        int calls{0};
        bool restarted{false};
        QVERIFY(locator->locate(missingPath(), [&](auto) {
            ++calls;
            restarted = locator->locate(missingPath(), [&](auto) {
                ++calls;
                locator.reset();
            });
        }));
        const QPointer<QFileDialog> first = dialog;
        first->reject();
        QVERIFY(restarted);
        QVERIFY(dialog != first);
        dialog->reject();
        QCOMPARE(calls, 2);
        QVERIFY(!locator);
        QTRY_VERIFY(first.isNull());
        QTRY_VERIFY(dialog.isNull());
    }

    void destructionAndWindowRebindingDiscardTheirPendingCallbacks() {
        auto window = std::make_unique<QWindow>();
        QPointer<QFileDialog> dialog;
        auto locator = std::make_unique<QtAudioFileLocator>(QString{}, QString{}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        locator->bindWindow(*window);
        int calls{0};
        QVERIFY(locator->locate(missingPath(), [&](auto) { ++calls; }));
        window.reset();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(!locator->locate(missingPath(), [&](auto) { ++calls; }));
        QWindow replacement;
        locator->bindWindow(replacement);
        QVERIFY(locator->locate(missingPath(), [&](auto) { ++calls; }));
        locator.reset();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(calls, 0);
    }

    void failedFactoriesAndReentrantCancellationLeaveNoRequestOrDialog() {
        QWindow window;
        int calls{0};
        QtAudioFileLocator throwing({}, {}, []() -> QFileDialog* { throw std::runtime_error("failed"); });
        throwing.bindWindow(window);
        QVERIFY(!throwing.locate(missingPath(), [&](auto) { ++calls; }));
        QtAudioFileLocator empty({}, {}, []() -> QFileDialog* { return nullptr; });
        empty.bindWindow(window);
        QVERIFY(!empty.locate(missingPath(), [&](auto) { ++calls; }));
        QPointer<QFileDialog> dialog;
        QtAudioFileLocator* reentrant_pointer{nullptr};
        QtAudioFileLocator reentrant({}, {}, [&] {
            reentrant_pointer->cancel();
            dialog = widgetDialog();
            return dialog.data();
        });
        reentrant_pointer = &reentrant;
        reentrant.bindWindow(window);
        QVERIFY(!reentrant.locate(missingPath(), [&](auto) { ++calls; }));
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(calls, 0);
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::QtAudioFileLocatorTests)
#include "QtAudioFileLocatorTests.moc"

#include "adapters/QtAudioFileDialog.hpp"
#include "adapters/QtAudioFilesPicker.hpp"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
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

} // namespace

class QtAudioFilesPickerTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void init() { QTest::failOnWarning(); }

    void opensOneAsynchronousMultipleLocalFileDialogAndKeepsTimersRunning() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        int factory_calls{0};
        QtAudioFilesPicker picker(QStringLiteral("Abrir áudios"), QStringLiteral("Áudio (*.wav)"), [&] {
            ++factory_calls;
            dialog = widgetDialog();
            return dialog.data();
        });
        std::vector<AudioFilesSelectionResult> results;
        QVERIFY(!picker.pick([&](auto result) { results.push_back(std::move(result)); }));
        picker.bindWindow(window);
        QVERIFY(!picker.pick({}));
        QVERIFY(picker.pick([&](auto result) { results.push_back(std::move(result)); }));
        QCOMPARE(dialog->fileMode(), QFileDialog::ExistingFiles);
        QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptOpen);
        QCOMPARE(dialog->supportedSchemes(), QStringList{QStringLiteral("file")});
        QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        QCOMPARE(dialog->windowHandle()->transientParent(), &window);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Abrir áudios"));
        QCOMPARE(dialog->nameFilters(), QStringList{QStringLiteral("Áudio (*.wav)")});
        QVERIFY(!picker.pick([](auto) {}));
        QCOMPARE(factory_calls, 1);
        bool timer_ran{false};
        QTimer::singleShot(0, &picker, [&] { timer_ran = true; });
        QTRY_VERIFY(timer_ran);
        QVERIFY(results.empty());
        dialog->reject();
        QCOMPARE(results.size(), std::size_t{1});
        QCOMPARE(results.front().status, AudioFilesSelectionStatus::Cancelled);
        QVERIFY(results.front().source_paths.empty());
        QTRY_VERIFY(dialog.isNull());
    }

    void selectedUnicodePathsPreserveTheDialogSelectionWithoutDecoding() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString first = temporary.filePath(QStringLiteral("A 日本語.wav"));
        const QString second = temporary.filePath(QStringLiteral("B música.wav"));
        for (const QString& path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("picker only"), qint64{11});
        }
        QWindow window;
        QPointer<QFileDialog> dialog;
        QtAudioFilesPicker picker({}, QStringLiteral("Todos (*)"), [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        picker.bindWindow(window);
        std::vector<AudioFilesSelectionResult> results;
        QVERIFY(picker.pick([&](auto result) { results.push_back(std::move(result)); }));
        dialog->setDirectory(temporary.path());
        // Only this offscreen adapter test depends on the Qt widget's filename editor.
        // The widget itself deduplicates typed filenames; the adapter preserves its returned order.
        auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        QVERIFY(edit);
        edit->setText(QStringLiteral("\"B música.wav\" \"A 日本語.wav\""));
        const QStringList selection = dialog->selectedFiles();
        QCOMPARE(selection.size(), 2);
        QVERIFY(selection.contains(first));
        QVERIFY(selection.contains(second));
        std::vector<std::filesystem::path> expected;
        for (const auto& path : selection)
            expected.push_back(nativePath(path));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
        QCOMPARE(results.size(), std::size_t{1});
        QCOMPARE(results.front().status, AudioFilesSelectionStatus::Selected);
        QCOMPARE(results.front().source_paths, expected);
        if (dialog)
            QVERIFY(QMetaObject::invokeMethod(dialog, "finished", Qt::DirectConnection, Q_ARG(int, QDialog::Accepted)));
        QCOMPARE(results.size(), std::size_t{1});
        QTRY_VERIFY(dialog.isNull());
    }

    void selectionNormalizationPreservesOrderAndDuplicatesAndRejectsInvalidPathsAtomically() {
        const QString first = QDir::temp().absoluteFilePath(QStringLiteral("A 日本語.wav"));
        const QString second = QDir::temp().absoluteFilePath(QStringLiteral("B música.wav"));
        const auto result = audioFilesSelectionFromUrls(
            {QUrl::fromLocalFile(second), QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}, true);
        QCOMPARE(result.status, AudioFilesSelectionStatus::Selected);
        QCOMPARE(result.source_paths,
                 std::vector<std::filesystem::path>({nativePath(second), nativePath(first), nativePath(second)}));
        const auto single = audioFilesSelectionFromUrls({QUrl::fromLocalFile(first)}, false);
        QCOMPARE(single.status, AudioFilesSelectionStatus::Selected);
        QCOMPARE(single.source_paths, std::vector<std::filesystem::path>{nativePath(first)});
        for (const auto& invalid : {
                 audioFilesSelectionFromUrls({}, true),
                 audioFilesSelectionFromUrls({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}, false),
                 audioFilesSelectionFromUrls(
                     {QUrl::fromLocalFile(first), QUrl(QStringLiteral("https://example.invalid/a.wav"))}, true),
                 audioFilesSelectionFromUrls({QUrl::fromLocalFile(QStringLiteral("relative.wav"))}, true),
                 audioFilesSelectionFromUrls({QUrl::fromLocalFile(first + QChar::Null)}, true),
             }) {
            QCOMPARE(invalid.status, AudioFilesSelectionStatus::Failed);
            QVERIFY(invalid.source_paths.empty());
        }
    }

#ifdef Q_OS_WIN
    void selectionRejectsDeviceNamespacesWithoutDiscardingTheFilesystemContract_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("device") << QStringLiteral(R"(\\.\PhysicalDrive0)");
        QTest::newRow("pipe") << QStringLiteral(R"(\\.\pipe\audio.wav)");
        QTest::newRow("object-manager") << QStringLiteral(R"(\??\C:\audio.wav)");
    }

    void selectionRejectsDeviceNamespacesWithoutDiscardingTheFilesystemContract() {
        QFETCH(QString, path);
        const QString valid = QDir::temp().absoluteFilePath(QStringLiteral("Válida %00 日本語.wav"));
        const auto result = audioFilesSelectionFromUrls({QUrl::fromLocalFile(valid), QUrl::fromLocalFile(path)}, true);
        QCOMPARE(result.status, AudioFilesSelectionStatus::Failed);
        QVERIFY(result.source_paths.empty());
    }

    void selectionPreservesUncAndExtendedFilesystemPaths() {
        for (const QString& path :
             {QStringLiteral(R"(\\servidor\Música\日本語.wav)"), QStringLiteral(R"(\\?\C:\Música\日本語.wav)"),
              QStringLiteral(R"(\\?\UNC\servidor\Música\日本語.wav)")}) {
            const auto result = audioFilesSelectionFromUrls({QUrl::fromLocalFile(path)}, false);
            QCOMPARE(result.status, AudioFilesSelectionStatus::Selected);
            QCOMPARE(result.source_paths, std::vector<std::filesystem::path>{nativePath(path)});
        }
    }
#endif

    void cancelDiscardsLateCompletionAndAllowsANewRequest() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        QtAudioFilesPicker picker({}, {}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        picker.bindWindow(window);
        int calls{0};
        QVERIFY(picker.pick([&](auto) { ++calls; }));
        const QPointer<QFileDialog> cancelled = dialog;
        picker.cancel();
        picker.cancel();
        if (cancelled)
            QVERIFY(
                QMetaObject::invokeMethod(cancelled, "finished", Qt::DirectConnection, Q_ARG(int, QDialog::Accepted)));
        QCOMPARE(calls, 0);
        QVERIFY(picker.pick([&](auto) { ++calls; }));
        QVERIFY(dialog != cancelled);
        dialog->reject();
        QCOMPARE(calls, 1);
        QTRY_VERIFY(cancelled.isNull());
        QTRY_VERIFY(dialog.isNull());
    }

    void completionMayRestartOrDestroyThePicker() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        auto picker = std::make_unique<QtAudioFilesPicker>(QString{}, QString{}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        picker->bindWindow(window);
        int calls{0};
        bool restarted{false};
        QVERIFY(picker->pick([&](auto) {
            ++calls;
            restarted = picker->pick([&](auto) {
                ++calls;
                picker.reset();
            });
        }));
        const QPointer<QFileDialog> first = dialog;
        first->reject();
        QVERIFY(restarted);
        QVERIFY(dialog != first);
        dialog->reject();
        QCOMPARE(calls, 2);
        QVERIFY(!picker);
        QTRY_VERIFY(first.isNull());
        QTRY_VERIFY(dialog.isNull());
    }

    void windowRebindingAndOwnerDestructionDiscardPendingCallbacks() {
        auto window = std::make_unique<QWindow>();
        QWindow replacement;
        QPointer<QFileDialog> dialog;
        auto picker = std::make_unique<QtAudioFilesPicker>(QString{}, QString{}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        picker->bindWindow(*window);
        int calls{0};
        QVERIFY(picker->pick([&](auto) { ++calls; }));
        const QPointer<QFileDialog> rebound = dialog;
        picker->bindWindow(replacement);
        QTRY_VERIFY(rebound.isNull());
        QVERIFY(picker->pick([&](auto) { ++calls; }));
        picker->bindWindow(*window);
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(picker->pick([&](auto) { ++calls; }));
        window.reset();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(!picker->pick([&](auto) { ++calls; }));
        picker->bindWindow(replacement);
        QVERIFY(picker->pick([&](auto) { ++calls; }));
        picker.reset();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(calls, 0);
    }

    void failureAndReentrantFactoryCancellationDoNotLeaveAPendingRequest() {
        QWindow window;
        int calls{0};
        QtAudioFilesPicker throwing({}, {}, []() -> QFileDialog* { throw std::runtime_error("failed"); });
        throwing.bindWindow(window);
        QVERIFY(!throwing.pick([&](auto) { ++calls; }));
        QtAudioFilesPicker empty({}, {}, []() -> QFileDialog* { return nullptr; });
        empty.bindWindow(window);
        QVERIFY(!empty.pick([&](auto) { ++calls; }));
        QPointer<QFileDialog> dialog;
        QtAudioFilesPicker* reentrant_pointer{nullptr};
        QtAudioFilesPicker reentrant({}, {}, [&] {
            reentrant_pointer->cancel();
            dialog = widgetDialog();
            return dialog.data();
        });
        reentrant_pointer = &reentrant;
        reentrant.bindWindow(window);
        QVERIFY(!reentrant.pick([&](auto) { ++calls; }));
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(calls, 0);
    }

    void unexpectedDialogDestructionCompletesFailedExactlyOnce() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        QtAudioFilesPicker picker({}, {}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        picker.bindWindow(window);
        std::vector<AudioFilesSelectionResult> results;
        QVERIFY(picker.pick([&](auto result) { results.push_back(std::move(result)); }));
        delete dialog.data();
        QCOMPARE(results.size(), std::size_t{1});
        QCOMPARE(results.front().status, AudioFilesSelectionStatus::Failed);
        QVERIFY(results.front().source_paths.empty());
        QVERIFY(picker.pick([&](auto result) { results.push_back(std::move(result)); }));
        picker.cancel();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(results.size(), std::size_t{1});
    }

    void acceptedEmptySelectionFailsAndThrowingCompletionDoesNotEscape() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        QtAudioFilesPicker picker({}, {}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        picker.bindWindow(window);
        std::vector<AudioFilesSelectionResult> results;
        QVERIFY(picker.pick([&](auto result) { results.push_back(std::move(result)); }));
        auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        QVERIFY(edit);
        edit->clear();
        QVERIFY(dialog->selectedUrls().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(dialog, "finished", Qt::DirectConnection, Q_ARG(int, QDialog::Accepted)));
        QCOMPARE(results.size(), std::size_t{1});
        QCOMPARE(results.front().status, AudioFilesSelectionStatus::Failed);
        QVERIFY(results.front().source_paths.empty());
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(picker.pick([](auto) { throw std::runtime_error("callback failed"); }));
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(picker.pick([](auto) {}));
        picker.cancel();
        QTRY_VERIFY(dialog.isNull());
    }

    void factoryMayDestroyItsOwnerWithoutLeakingTheReturnedDialog() {
        QWindow window;
        QPointer<QFileDialog> dialog;
        std::unique_ptr<QtAudioFilesPicker> picker;
        picker = std::make_unique<QtAudioFilesPicker>(QString{}, QString{}, [&] {
            picker.reset();
            dialog = widgetDialog();
            return dialog.data();
        });
        picker->bindWindow(window);
        int calls{0};
        QVERIFY(!picker->pick([&](auto) { ++calls; }));
        QVERIFY(!picker);
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(calls, 0);
    }

    void reentrantCancellationMayDestroyTheNewWindowBeforeBinding() {
        QWindow original;
        auto replacement = std::make_unique<QWindow>();
        QPointer<QFileDialog> dialog;
        QtAudioFilesPicker picker({}, {}, [&] {
            dialog = widgetDialog();
            return dialog.data();
        });
        picker.bindWindow(original);
        int calls{0};
        QVERIFY(picker.pick([&](auto) { ++calls; }));
        connect(dialog, &QDialog::rejected, this, [&] { replacement.reset(); });
        picker.bindWindow(*replacement);
        QVERIFY(!replacement);
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(!picker.pick([&](auto) { ++calls; }));
        QCOMPARE(calls, 0);
        picker.bindWindow(original);
        QVERIFY(picker.pick([&](auto) { ++calls; }));
        picker.cancel();
        QTRY_VERIFY(dialog.isNull());
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::QtAudioFilesPickerTests)
#include "QtAudioFilesPickerTests.moc"

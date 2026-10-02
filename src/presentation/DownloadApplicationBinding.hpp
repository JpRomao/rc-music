#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

namespace music_player::m0 {
class PlayerPresenter;
class DownloadAdmissionController;
class DownloadService;
enum class ExitPreparationState;

struct DownloadExecutionDrainCallbacks final {
    std::function<void()> shutdown{};
    std::function<bool()> stopped{};
};

struct DownloadApplicationBindingOptions final {
    // Test/composition seam; omitted uses a monotonic QElapsedTimer. No clock
    // callback may perform I/O, wait, or mutate borrowed controller/service.
    std::function<std::int64_t()> now_ms{};
    bool automatic_polling{true};
};

// Qt application-thread owner of projection/timer/exit coordination only. The
// presenter, controller, service and execution callbacks remain borrowed and
// outlive this binding plus physical shutdown/drain. No production executor,
// URL ingress, worker waits, filesystem access or destructor I/O is introduced.
class DownloadApplicationBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* jobsModel READ jobsModel CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString warningText READ warningText NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryStartup READ canRetryStartup NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryAdmission READ canRetryAdmission NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryFinalSave READ canRetryFinalSave NOTIFY stateChanged)
    Q_PROPERTY(bool canCloseAfterFinalSaveFailure READ canCloseAfterFinalSaveFailure NOTIFY stateChanged)

  public:
    DownloadApplicationBinding(PlayerPresenter& presenter, DownloadAdmissionController& controller,
                               DownloadService& service, DownloadExecutionDrainCallbacks execution,
                               DownloadApplicationBindingOptions options = {}, QObject* parent = nullptr);
    ~DownloadApplicationBinding() override;
    [[nodiscard]] QAbstractItemModel* jobsModel() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString warningText() const;
    [[nodiscard]] bool canRetryStartup() const;
    [[nodiscard]] bool canRetryAdmission() const;
    [[nodiscard]] bool canRetryFinalSave() const;
    [[nodiscard]] bool canCloseAfterFinalSaveFailure() const;
    // Trusted composition only, not exposed to QML. poll is deterministic when
    // automatic_polling is false. Neither method admits remote requests.
    [[nodiscard]] bool start();
    void poll();
    Q_INVOKABLE bool retryStartup();
    Q_INVOKABLE bool retryAdmission();
    Q_INVOKABLE bool retryFinalSave();
    Q_INVOKABLE bool closeAfterFinalSaveFailure();

  signals:
    void stateChanged();

  private:
    void beginShutdown();
    [[nodiscard]] ExitPreparationState pollExitPreparation();
    void refreshProjection();
    [[nodiscard]] bool performControl(const std::function<bool()>& action);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0

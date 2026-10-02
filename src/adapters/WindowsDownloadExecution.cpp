#include "WindowsDownloadExecution.hpp"

#include "DownloadNetworkPolicy.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <mutex>
#include <optional>

namespace music_player::m0 {
namespace {
bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](const char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
               value == '-' || value == '_';
    });
}
bool validText(const std::string& text, const std::size_t maximum) {
    if (text.empty() || text.size() > maximum || text.find('\0') != std::string::npos)
        return false;
    return QString::fromUtf8(text).toUtf8().toStdString() == text;
}
bool validItem(const DownloadItem& item) {
    return validText(item.url, 8192) && validText(item.title, 800) &&
           (item.kind == DownloadMediaKind::Audio || item.kind == DownloadMediaKind::Video) &&
           parseDownloadNetworkTarget(QString::fromUtf8(item.url)).has_value();
}
bool validPath(const std::filesystem::path& path) {
    const auto& native = path.native();
    if (native.empty() || native.size() > 30000 || native.find(L'\0') != std::wstring::npos || !path.is_absolute() ||
        path != path.lexically_normal())
        return false;
    auto extension = path.extension().native();
    for (auto& letter : extension) {
        if (letter >= L'A' && letter <= L'Z')
            letter = static_cast<wchar_t>(letter - L'A' + L'a');
    }
    return extension == L".mp3";
}
bool knownPhase(const DownloadPhase phase) {
    return phase == DownloadPhase::Downloading || phase == DownloadPhase::Converting ||
           phase == DownloadPhase::Publishing;
}
bool knownFailure(const DownloadFailure error) {
    switch (error) {
    case DownloadFailure::None:
    case DownloadFailure::Unavailable:
    case DownloadFailure::Network:
    case DownloadFailure::Expired:
    case DownloadFailure::Restricted:
    case DownloadFailure::UnsupportedMedia:
    case DownloadFailure::NoAudio:
    case DownloadFailure::LimitExceeded:
    case DownloadFailure::Conversion:
    case DownloadFailure::Publication:
    case DownloadFailure::Import:
    case DownloadFailure::Timeout:
    case DownloadFailure::InvalidResult:
        return true;
    }
    return false;
}
bool terminal(const DownloadExecutionState state) {
    return state == DownloadExecutionState::Published || state == DownloadExecutionState::Failed ||
           state == DownloadExecutionState::Cancelled;
}
bool validUpdate(const DownloadExecutionUpdate& update, const std::string& id) {
    // Inspect lengths before any mailbox copy. This validates a trusted task's
    // DTO; it cannot bound allocations inside the task itself.
    if (update.job_id.size() > 64 || update.job_id != id || !knownPhase(update.phase) || !knownFailure(update.error) ||
        update.published_mp3.native().size() > 30000)
        return false;
    switch (update.state) {
    case DownloadExecutionState::Running:
        return update.error == DownloadFailure::None && update.published_mp3.empty() &&
               (!update.total_units || update.completed_units <= *update.total_units);
    case DownloadExecutionState::Cancelled:
        return update.error == DownloadFailure::None && update.published_mp3.empty();
    case DownloadExecutionState::Failed:
        return update.error != DownloadFailure::None && update.published_mp3.empty();
    case DownloadExecutionState::Published:
        return update.error == DownloadFailure::None && update.phase == DownloadPhase::Publishing &&
               validPath(update.published_mp3) &&
               (!update.total_units || update.completed_units <= *update.total_units);
    }
    return false;
}
class SessionTask final : public WindowsDownloadTask {
  public:
    SessionTask(WindowsProcessDownloadIoConfig io, WindowsDownloadJobSessionConfig job)
        : io_(std::move(io)), session_(io_, std::move(job)) {}
    bool start(const std::string& id, const DownloadItem& item, const qint64 now_ms) override {
        id_ = id;
        return session_.start(id, item, now_ms);
    }
    void cancel() override { session_.cancel(); }
    DownloadExecutionUpdate poll(const qint64 now_ms) override {
        auto update = session_.poll(now_ms);
        // A one-shot session can reject trusted configuration before assigning
        // its ID. That failure has no native work; contextualize only this case.
        if (update.job_id.empty() && update.state == DownloadExecutionState::Failed)
            update.job_id = id_;
        return update;
    }
    std::filesystem::path retainedPublicationPath() const override {
        return std::filesystem::path{session_.retainedPublicationPath().toStdWString()};
    }

  private:
    std::string id_;
    WindowsProcessDownloadIo io_; // borrowed by session; destroyed AFTER session
    WindowsDownloadJobSession session_;
};
class PreparedSessionTask final : public WindowsDownloadTask {
  public:
    PreparedSessionTask(WindowsDownloadNetworkLaunchConfig launch, WindowsDownloadJobSessionConfig job,
                        WindowsDownloadRuntimePreparer prepare)
        : launch_(std::move(launch)), job_(std::move(job)), prepare_(std::move(prepare)) {}

    bool start(const std::string& id, const DownloadItem& item, const qint64 now_ms) override {
        if (started_ || terminal(update_.state))
            return false;
        started_ = true;
        update_.job_id = id;
        if (!validId(id) || !validItem(item) || now_ms < 0 || job_.total_timeout_ms <= 0) {
            fail(DownloadFailure::InvalidResult);
            return false;
        }
        item_ = item;
        started_ms_ = now_ms;
        try {
            lease_ = prepare_ ? prepare_(launch_) : WindowsDownloadNetworkLaunch::prepare(launch_);
        } catch (...) {
            // Trusted preparer guarantees no escaped work on an exception.
            fail(DownloadFailure::InvalidResult);
            return false;
        }
        if (!lease_) {
            fail(DownloadFailure::Unavailable);
            return false;
        }
        // Deliberately do not start the session here. The private executor must
        // observe cancel/shutdown after this blocking preparation returns.
        return true;
    }
    void cancel() override {
        if (session_)
            session_->cancel();
        else if (!terminal(update_.state))
            update_.state = DownloadExecutionState::Cancelled;
    }
    DownloadExecutionUpdate poll(const qint64 now_ms) override {
        if (terminal(update_.state))
            return update_;
        if (!session_) {
            if (!started_ || !lease_ || now_ms < started_ms_) {
                fail(DownloadFailure::InvalidResult);
                return update_;
            }
            // Both clocks are nonnegative here; subtraction cannot overflow.
            // Preparation is part of the original end-to-end deadline.
            if (now_ms - started_ms_ >= job_.total_timeout_ms) {
                fail(DownloadFailure::Timeout);
                return update_;
            }
            try {
                WindowsProcessDownloadIoConfig io;
                io.prepared_launch = lease_;
                session_ = std::make_unique<SessionTask>(std::move(io), job_);
            } catch (...) {
                // Session construction is inert: no native work escaped.
                fail(DownloadFailure::InvalidResult);
                return update_;
            }
        }
        if (!session_start_attempted_) {
            // Set before calling: false/throw may own work and must never retry.
            session_start_attempted_ = true;
            if (!session_->start(update_.job_id, item_, started_ms_))
                session_->cancel();
        }
        return session_->poll(now_ms); // only its real terminal ACK releases work
    }
    std::filesystem::path retainedPublicationPath() const override {
        return session_ ? session_->retainedPublicationPath() : std::filesystem::path{};
    }

  private:
    void fail(const DownloadFailure error) {
        update_.state = DownloadExecutionState::Failed;
        update_.error = error;
    }
    WindowsDownloadNetworkLaunchConfig launch_;
    WindowsDownloadJobSessionConfig job_;
    WindowsDownloadRuntimePreparer prepare_;
    DownloadExecutionUpdate update_;
    DownloadItem item_;
    qint64 started_ms_{0};
    bool started_{false}, session_start_attempted_{false};
    // Reverse destruction: session, native IO/process, then final lease. The
    // executor observes terminal ACK and destroys this task before publishing.
    std::shared_ptr<const WindowsDownloadNetworkLaunch> lease_;
    std::unique_ptr<SessionTask> session_;
};
} // namespace

std::unique_ptr<WindowsDownloadTask> makeWindowsPreparedDownloadTask(WindowsDownloadNetworkLaunchConfig launch,
                                                                     WindowsDownloadJobSessionConfig job,
                                                                     WindowsDownloadRuntimePreparer prepare) {
    return std::make_unique<PreparedSessionTask>(std::move(launch), std::move(job), std::move(prepare));
}

struct WindowsDownloadExecution::Impl final {
    struct Request final {
        std::string id;
        DownloadItem item;
    };
    struct Worker final : QThread {
        Impl& owner;
        explicit Worker(Impl& value) : owner(value) {}
        void run() override { owner.run(); }
    };
    TaskFactory factory;
    mutable std::mutex mutex;
    std::optional<Request> pending;
    DownloadExecutionUpdate snapshot;
    std::filesystem::path recovery;
    bool admitted{false}, terminal_observed{false}, cancel_requested{false}, closed{false}, wake_enqueued{false};
    QObject* dispatcher{nullptr}; // lifetime protected by mutex, one queued wake at most
    Worker worker;

    explicit Impl(TaskFactory value) : factory(std::move(value)), worker(*this) { worker.start(); }

    // Caller already holds mutex. Queuing is prompt; it never runs the task here.
    void wake() {
        if (!dispatcher || wake_enqueued)
            return;
        wake_enqueued = true;
        QMetaObject::invokeMethod(dispatcher, "start", Qt::QueuedConnection);
    }

    void run() {
        QObject context;
        QTimer wake_timer(&context);
        wake_timer.setSingleShot(true);
        // QObject has no custom slot: a coalesced signal invocation is queued on
        // a timer created IN this thread, then timeout drives the private loop.
        QTimer timer(&context);
        timer.setInterval(5);
        QEventLoop loop;
        QElapsedTimer clock;
        clock.start();
        std::unique_ptr<WindowsDownloadTask> task;
        std::optional<Request> current;
        bool invalid_result{false}, start_refused{false}, cancel_sent{false}, quitting{false}, pumping{false};

        auto cancellation = [&] {
            std::lock_guard lock(mutex);
            return cancel_requested || closed;
        };
        auto publish = [&](DownloadExecutionUpdate update, std::filesystem::path retained = {}) {
            // All task destruction, possibly blocking, precedes the terminal ACK.
            task.reset();
            current.reset();
            timer.stop();
            std::lock_guard lock(mutex);
            snapshot = std::move(update);
            recovery = std::move(retained);
        };
        auto pump = [&] {
            // A trusted task/native callback can process nested Qt events. Never
            // enter another task step/destructor from a timer in that nested loop.
            if (pumping)
                return;
            QScopedValueRollback<bool> guard(pumping, true);
            if (!task && !current) {
                {
                    std::lock_guard lock(mutex);
                    if (pending) {
                        current = std::move(pending);
                        pending.reset();
                    }
                }
                if (current) {
                    invalid_result = false;
                    start_refused = false;
                    cancel_sent = false;
                    DownloadExecutionUpdate early;
                    early.job_id = current->id;
                    if (!cancellation()) {
                        try {
                            if (factory)
                                task = factory();
                        } catch (...) {
                            // Factory's explicit contract guarantees no escaped
                            // work. Exceptions from a task never take this path.
                        }
                    }
                    if (cancellation()) {
                        early.state = DownloadExecutionState::Cancelled;
                        publish(std::move(early));
                    } else if (!task) {
                        early.state = DownloadExecutionState::Failed;
                        early.error = DownloadFailure::Unavailable;
                        publish(std::move(early));
                    } else {
                        try {
                            // false may own work: drain, never expose false from
                            // the already successful fast-port admission.
                            start_refused = !task->start(current->id, current->item, clock.elapsed());
                        } catch (...) {
                            invalid_result = true;
                        }
                        timer.start();
                    }
                }
            }
            if (task) {
                if ((cancellation() || invalid_result || start_refused) && !cancel_sent) {
                    try {
                        task->cancel();
                        cancel_sent = true;
                    } catch (...) {
                        invalid_result = true;
                    }
                }
                try {
                    auto update = task->poll(clock.elapsed());
                    if (!validUpdate(update, current->id)) {
                        invalid_result = true; // no valid native acknowledgement
                        cancel_sent = false;
                    } else if (terminal(update.state)) {
                        auto retained = update.state == DownloadExecutionState::Published
                                            ? update.published_mp3
                                            : task->retainedPublicationPath();
                        if (!retained.empty() && !validPath(retained)) {
                            retained.clear();
                            invalid_result = true;
                        }
                        if (invalid_result || (start_refused && update.state != DownloadExecutionState::Failed)) {
                            update.state = DownloadExecutionState::Failed;
                            update.error = DownloadFailure::InvalidResult;
                            update.published_mp3.clear();
                        }
                        // Preserve a real publication even if cancel arrived.
                        // Service decides not to import; this owner never deletes it.
                        publish(std::move(update), std::move(retained));
                    } else if (!invalid_result) {
                        std::lock_guard lock(mutex);
                        if (static_cast<int>(update.phase) < static_cast<int>(snapshot.phase) ||
                            (update.phase == snapshot.phase && update.completed_units < snapshot.completed_units)) {
                            invalid_result = true;
                            cancel_sent = false;
                        } else {
                            snapshot = std::move(update); // bounded, coalesced progress
                        }
                    }
                } catch (...) {
                    invalid_result = true;
                    cancel_sent = false; // exception is NOT acknowledgement
                }
            }
            {
                std::lock_guard lock(mutex);
                if (closed && !task && !current && !pending) {
                    dispatcher = nullptr;
                    quitting = true;
                }
            }
            if (quitting)
                loop.quit();
        };
        QObject::connect(&timer, &QTimer::timeout, &context, pump);
        QObject::connect(&wake_timer, &QTimer::timeout, &context, [&] {
            {
                std::lock_guard lock(mutex);
                wake_enqueued = false;
            }
            pump();
        });
        {
            std::lock_guard lock(mutex);
            dispatcher = &wake_timer;
        }
        pump(); // handles admission/shutdown before this thread was ready
        if (!quitting)
            loop.exec();
        {
            std::lock_guard lock(mutex);
            dispatcher = nullptr;
            wake_enqueued = false;
        }
    }
};

WindowsDownloadExecution::WindowsDownloadExecution(TaskFactory factory)
    : impl_(std::make_unique<Impl>(std::move(factory))) {}
WindowsDownloadExecution::WindowsDownloadExecution(WindowsProcessDownloadIoConfig io,
                                                   WindowsDownloadJobSessionConfig job)
    : WindowsDownloadExecution(
          [io = std::move(io), job = std::move(job)] { return std::make_unique<SessionTask>(io, job); }) {}
WindowsDownloadExecution::WindowsDownloadExecution(WindowsDownloadNetworkLaunchConfig launch,
                                                   WindowsDownloadJobSessionConfig job)
    : WindowsDownloadExecution([launch = std::move(launch), job = std::move(job)] {
          return makeWindowsPreparedDownloadTask(launch, job);
      }) {}
WindowsDownloadExecution::~WindowsDownloadExecution() {
    shutdown();
    impl_->worker.wait(); // owner must have observed stopped before UI destruction
}
bool WindowsDownloadExecution::start(const std::string& id, const DownloadItem& item) {
    if (!validId(id) || !validItem(item))
        return false;
    Impl::Request request{id, item};
    DownloadExecutionUpdate initial;
    initial.job_id = id;
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed || (impl_->admitted && !impl_->terminal_observed))
        return false;
    impl_->pending = std::move(request);
    impl_->snapshot = std::move(initial);
    impl_->recovery.clear();
    impl_->admitted = true;
    impl_->terminal_observed = false;
    impl_->cancel_requested = false;
    impl_->wake();
    return true;
}
void WindowsDownloadExecution::cancel(const std::string_view id) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->admitted && impl_->snapshot.job_id == id && !terminal(impl_->snapshot.state)) {
        impl_->cancel_requested = true;
        impl_->wake();
    }
}
DownloadExecutionUpdate WindowsDownloadExecution::poll(const std::string_view id) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->admitted && impl_->snapshot.job_id == id && terminal(impl_->snapshot.state))
        impl_->terminal_observed = true;
    return impl_->snapshot; // stale polling is inert and exposes the actual job ID
}
void WindowsDownloadExecution::shutdown() {
    std::lock_guard lock(impl_->mutex);
    impl_->closed = true;
    impl_->cancel_requested = true;
    impl_->wake();
}
bool WindowsDownloadExecution::stopped() const { return impl_->worker.isFinished(); }
std::filesystem::path WindowsDownloadExecution::retainedPublicationPath(const std::string_view id) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->snapshot.job_id == id ? impl_->recovery : std::filesystem::path{};
}
} // namespace music_player::m0

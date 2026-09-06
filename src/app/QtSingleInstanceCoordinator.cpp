#include "QtSingleInstanceCoordinator.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr qsizetype frame_prefix_bytes = sizeof(quint32);
constexpr qsizetype max_response_payload_bytes = 4'096;
constexpr qint64 pending_connection_read_buffer_bytes = 1;
constexpr int listen_backlog_size = 32;
constexpr int max_pending_connections = 32;

enum class WireReplyKind { Ack, Nack };

struct WireReply final {
    WireReplyKind kind{WireReplyKind::Nack};
    QString reason{};
};

enum class ClientReplyKind { Ack, Nack, Unavailable, TimedOut, Failed };

struct ClientReply final {
    ClientReplyKind kind{ClientReplyKind::Failed};
    QString detail{};
};

[[nodiscard]] QtSingleInstanceStartResult startResult(const ClientReply& reply) {
    switch (reply.kind) {
    case ClientReplyKind::Ack:
        return {.outcome = QtSingleInstanceOutcome::Forwarded};
    case ClientReplyKind::Nack:
        return {.outcome = QtSingleInstanceOutcome::Rejected, .detail = reply.detail};
    case ClientReplyKind::TimedOut:
        return {.outcome = QtSingleInstanceOutcome::TimedOut, .detail = reply.detail};
    default:
        return {.outcome = QtSingleInstanceOutcome::Failed, .detail = reply.detail};
    }
}

[[nodiscard]] bool allowSetForegroundWindowForOwner(const quint32 owner_pid) {
#ifdef Q_OS_WIN
    return ::AllowSetForegroundWindow(static_cast<DWORD>(owner_pid)) != FALSE;
#else
    Q_UNUSED(owner_pid);
    return false;
#endif
}

[[nodiscard]] int boundedMilliseconds(const std::chrono::milliseconds duration) {
    return static_cast<int>(std::clamp<std::int64_t>(duration.count(), 0, std::numeric_limits<int>::max()));
}

[[nodiscard]] QByteArray makeFrame(const QByteArray& payload) {
    QByteArray framed(frame_prefix_bytes, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), framed.data());
    framed.append(payload);
    return framed;
}

[[nodiscard]] QByteArray encodeReply(const WireReply& reply) {
    QJsonObject object{
        {QStringLiteral("status"), reply.kind == WireReplyKind::Ack ? QStringLiteral("ack") : QStringLiteral("nack")}};
    if (reply.kind == WireReplyKind::Nack)
        object.insert(QStringLiteral("reason"), reply.reason);
    return QJsonDocument{std::move(object)}.toJson(QJsonDocument::Compact);
}

[[nodiscard]] ClientReply decodeReply(const QByteArray& payload) {
    if (payload.isEmpty() || payload.size() > max_response_payload_bytes)
        return {.detail = QStringLiteral("invalidResponse")};
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return {.detail = QStringLiteral("invalidResponse")};
    const QJsonObject object = document.object();
    const auto status = object.value(QStringLiteral("status"));
    if (!status.isString())
        return {.detail = QStringLiteral("invalidResponse")};
    if (status.toString() == QStringLiteral("ack") && object.size() == 1)
        return {.kind = ClientReplyKind::Ack};
    const auto reason = object.value(QStringLiteral("reason"));
    if (status.toString() == QStringLiteral("nack") && object.size() == 2 && reason.isString() &&
        !reason.toString().isEmpty()) {
        return {.kind = ClientReplyKind::Nack, .detail = reason.toString()};
    }
    return {.detail = QStringLiteral("invalidResponse")};
}

[[nodiscard]] QString commandErrorDetail(const OpenSourcesCommandError error) {
    switch (error) {
    case OpenSourcesCommandError::None:
        return {};
    case OpenSourcesCommandError::TooManyPaths:
        return QStringLiteral("tooManyPaths");
    case OpenSourcesCommandError::PayloadTooLarge:
        return QStringLiteral("payloadTooLarge");
    case OpenSourcesCommandError::MalformedPayload:
        return QStringLiteral("malformedPayload");
    case OpenSourcesCommandError::InvalidSchema:
        return QStringLiteral("invalidSchema");
    case OpenSourcesCommandError::UnsupportedVersion:
        return QStringLiteral("unsupportedVersion");
    case OpenSourcesCommandError::UnsupportedCommand:
        return QStringLiteral("unsupportedCommand");
    case OpenSourcesCommandError::InvalidPath:
        return QStringLiteral("invalidPath");
    }
    return QStringLiteral("invalidCommand");
}

[[nodiscard]] bool writeAll(QLocalSocket& socket, const QByteArray& bytes, QDeadlineTimer& deadline) {
    if (socket.write(bytes) != bytes.size())
        return false;
    while (socket.bytesToWrite() > 0) {
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0)
            return false;
        // A Windows pipe close can retire the writer while retaining the reply.
        // No pending bytes permits reading it, but only a valid ACK confirms delivery.
        if (!socket.waitForBytesWritten(static_cast<int>(remaining)) && socket.bytesToWrite() > 0)
            return false;
    }
    return true;
}

[[nodiscard]] std::optional<QByteArray> readFrame(QLocalSocket& socket, QDeadlineTimer& deadline,
                                                  const qsizetype maximum_payload_bytes) {
    QByteArray bytes;
    std::optional<quint32> expected_size;
    while (!deadline.hasExpired()) {
        bytes.append(socket.readAll());
        if (!expected_size.has_value() && bytes.size() >= frame_prefix_bytes) {
            expected_size = qFromBigEndian<quint32>(bytes.constData());
            bytes.remove(0, frame_prefix_bytes);
            if (*expected_size == 0 || static_cast<qsizetype>(*expected_size) > maximum_payload_bytes)
                return std::nullopt;
        }
        if (expected_size.has_value() && bytes.size() >= static_cast<qsizetype>(*expected_size)) {
            if (bytes.size() != static_cast<qsizetype>(*expected_size))
                return std::nullopt;
            return bytes;
        }
        if (socket.state() == QLocalSocket::UnconnectedState)
            return std::nullopt;
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0 || !socket.waitForReadyRead(static_cast<int>(remaining)))
            return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] ClientReply exchangeConnected(QLocalSocket& socket, const QByteArray& payload, QDeadlineTimer& deadline) {
    if (!writeAll(socket, makeFrame(payload), deadline)) {
        if (deadline.hasExpired())
            return {.kind = ClientReplyKind::TimedOut, .detail = QStringLiteral("ackTimeout")};
        return {.detail = QStringLiteral("writeFailed")};
    }
    const auto response = readFrame(socket, deadline, max_response_payload_bytes);
    if (!response.has_value()) {
        if (deadline.hasExpired())
            return {.kind = ClientReplyKind::TimedOut, .detail = QStringLiteral("ackTimeout")};
        return {.detail = QStringLiteral("invalidResponse")};
    }
    return decodeReply(*response);
}

[[nodiscard]] ClientReply exchangeWithPrimary(const QString& server_name, const QByteArray& payload,
                                              QDeadlineTimer& deadline, const std::function<void()>& before_write) {
    QLocalSocket socket;
    socket.setReadBufferSize(max_response_payload_bytes + frame_prefix_bytes + 1);
    socket.connectToServer(server_name);
    if (socket.state() != QLocalSocket::ConnectedState) {
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0 || !socket.waitForConnected(static_cast<int>(remaining))) {
            if (deadline.hasExpired())
                return {.kind = ClientReplyKind::TimedOut, .detail = QStringLiteral("primaryUnavailable")};
            return {.kind = ClientReplyKind::Unavailable, .detail = QStringLiteral("primaryUnavailable")};
        }
    }

    before_write();
    return exchangeConnected(socket, payload, deadline);
}

class BoundedLocalServer final : public QLocalServer {
  public:
    explicit BoundedLocalServer(const qint64 read_buffer_size) : read_buffer_size_(read_buffer_size) {}

  protected:
    void incomingConnection(const quintptr socket_descriptor) override {
        auto* socket = new QLocalSocket(this);
        socket->setReadBufferSize(read_buffer_size_);
        if (!socket->setSocketDescriptor(static_cast<qintptr>(socket_descriptor))) {
            delete socket;
            return;
        }
        addPendingConnection(socket);
    }

  private:
    qint64 read_buffer_size_;
};

class InboundConnection final : public QObject {
  public:
    using Handler = std::function<WireReply(const QByteArray&)>;

    InboundConnection(QLocalSocket* socket, const int timeout_ms, Handler handler, QObject* parent)
        : QObject(parent), socket_(socket), timeout_ms_(timeout_ms), handler_(std::move(handler)) {
        socket_->setParent(this);
        deadline_.setSingleShot(true);
        QObject::connect(&deadline_, &QTimer::timeout, this, [this] {
            if (state_ == State::Reading)
                sendReply({.kind = WireReplyKind::Nack, .reason = QStringLiteral("frameTimeout")});
            else
                closeNow();
        });
        QObject::connect(socket_, &QLocalSocket::readyRead, this, [this] { consumeAvailableBytes(); });
        QObject::connect(socket_, &QLocalSocket::bytesWritten, this, [this](qint64) {
            if (state_ == State::Responding)
                disconnectAfterWrite();
        });
        QObject::connect(socket_, &QLocalSocket::disconnected, this, [this] { closeNow(); });
        QObject::connect(socket_, &QLocalSocket::errorOccurred, this,
                         [this](QLocalSocket::LocalSocketError) { closeNow(); });
        deadline_.start(timeout_ms_);
        consumeAvailableBytes();
    }

    ~InboundConnection() override {
        if (socket_ != nullptr && socket_->state() != QLocalSocket::UnconnectedState)
            socket_->abort();
    }

  private:
    enum class State { Reading, Responding, Closed };

    void consumeAvailableBytes() {
        if (state_ != State::Reading)
            return;
        bytes_.append(socket_->readAll());
        if (!expected_size_.has_value() && bytes_.size() >= frame_prefix_bytes) {
            expected_size_ = qFromBigEndian<quint32>(bytes_.constData());
            bytes_.remove(0, frame_prefix_bytes);
            if (*expected_size_ == 0) {
                sendReply({.kind = WireReplyKind::Nack, .reason = QStringLiteral("invalidCommand")});
                return;
            }
            if (static_cast<qsizetype>(*expected_size_) > open_sources_max_payload_bytes) {
                sendReply({.kind = WireReplyKind::Nack, .reason = QStringLiteral("payloadTooLarge")});
                return;
            }
        }
        if (!expected_size_.has_value() || bytes_.size() < static_cast<qsizetype>(*expected_size_))
            return;
        if (bytes_.size() != static_cast<qsizetype>(*expected_size_)) {
            sendReply({.kind = WireReplyKind::Nack, .reason = QStringLiteral("invalidFrame")});
            return;
        }
        sendReply(handler_(bytes_.first(static_cast<qsizetype>(*expected_size_))));
    }

    void sendReply(const WireReply& reply) {
        if (state_ != State::Reading)
            return;
        state_ = State::Responding;
        deadline_.start(timeout_ms_);
        const QByteArray response = makeFrame(encodeReply(reply));
        if (socket_->write(response) != response.size()) {
            closeNow();
            return;
        }
        disconnectAfterWrite();
    }

    void disconnectAfterWrite() {
        if (state_ != State::Responding || socket_->bytesToWrite() > 0)
            return;
        socket_->disconnectFromServer();
        if (socket_->state() == QLocalSocket::UnconnectedState)
            closeNow();
    }

    void closeNow() {
        if (state_ == State::Closed)
            return;
        state_ = State::Closed;
        deadline_.stop();
        if (socket_->state() != QLocalSocket::UnconnectedState)
            socket_->abort();
        deleteLater();
    }

    QLocalSocket* socket_;
    int timeout_ms_;
    Handler handler_;
    QTimer deadline_;
    QByteArray bytes_;
    std::optional<quint32> expected_size_;
    State state_{State::Reading};
};

} // namespace

class QtSingleInstanceCoordinator::Impl final {
  public:
    Impl(QtSingleInstanceCoordinator* owner, QtSingleInstanceOptions options,
         const ForegroundPermissionTransfer foreground_permission_transfer)
        : owner_(owner), options_(std::move(options)), foreground_permission_transfer_(foreground_permission_transfer) {
    }

    ~Impl() { closeServerAndReleaseLock(); }

    [[nodiscard]] QtSingleInstanceStartResult start(const OpenSourcesCommand& initial_command) {
        if (started_)
            return {.detail = QStringLiteral("alreadyStarted")};
        started_ = true;

        const QString option_error = validateOptions();
        if (!option_error.isEmpty())
            return {.detail = option_error};
        const auto encoded = encodeOpenSourcesCommand(initial_command);
        if (encoded.error != OpenSourcesCommandError::None)
            return {.detail = commandErrorDetail(encoded.error)};
        if (!initial_command.paths.isEmpty() &&
            (options_.max_bootstrap_commands < 1 || encoded.payload.size() > options_.max_bootstrap_payload_bytes)) {
            return {.detail = QStringLiteral("bootstrapQueueTooSmall")};
        }

        const QFileInfo lock_info{options_.lock_file_path};
        QDir lock_directory = lock_info.dir();
        if ((!lock_directory.exists() && !lock_directory.mkpath(QStringLiteral("."))) || !lock_directory.exists()) {
            return {.detail = QStringLiteral("lockDirectoryUnavailable")};
        }

        lock_ = std::make_unique<QLockFile>(options_.lock_file_path);
        lock_->setStaleLockTime(std::chrono::milliseconds::zero());
        if (lock_->tryLock(std::chrono::milliseconds::zero()))
            return becomePrimary(initial_command, encoded.payload);
        if (lock_->error() == QLockFile::PermissionError)
            return {.detail = QStringLiteral("lockPermissionDenied")};
        if (lock_->error() == QLockFile::UnknownError)
            return {.detail = QStringLiteral("lockUnavailable")};

        QDeadlineTimer retry_deadline{boundedMilliseconds(options_.secondary_retry_timeout)};
        while (!retry_deadline.hasExpired()) {
            qint64 owner_pid = 0;
            QString owner_host;
            QString owner_application;
            if (lock_->getLockInfo(&owner_pid, &owner_host, &owner_application) && owner_pid > 0) {
                Q_UNUSED(owner_host);
                Q_UNUSED(owner_application);
                const QString endpoint = endpointForPid(owner_pid);
                QDeadlineTimer exchange_deadline{retry_deadline.remainingTime()};
                const ClientReply reply =
                    exchangeWithPrimary(endpoint, encoded.payload, exchange_deadline, [this, owner_pid] {
                        static_cast<void>(QtSingleInstanceCoordinator::tryTransferForegroundPermission(
                            owner_pid, foreground_permission_transfer_));
                    });
                if (reply.kind != ClientReplyKind::Unavailable)
                    return startResult(reply);
            }

            if (lock_->tryLock(std::chrono::milliseconds::zero()))
                return becomePrimary(initial_command, encoded.payload);
            if (lock_->error() == QLockFile::PermissionError)
                return {.detail = QStringLiteral("lockPermissionDenied")};
            if (lock_->error() == QLockFile::UnknownError)
                return {.detail = QStringLiteral("lockUnavailable")};

            const qint64 remaining = retry_deadline.remainingTime();
            if (remaining <= 0)
                break;
            const auto pause = std::min<std::int64_t>(options_.secondary_retry_interval.count(), remaining);
            if (pause > 0)
                QThread::msleep(static_cast<unsigned long>(pause));
        }
        return {.outcome = QtSingleInstanceOutcome::TimedOut, .detail = QStringLiteral("primaryUnavailable")};
    }

    [[nodiscard]] bool isPrimary() const noexcept { return primary_; }
    [[nodiscard]] bool isAccepting() const noexcept {
        return primary_ && server_ != nullptr && server_->isListening() && accepting_ && !shutdown_started_;
    }
    [[nodiscard]] QString serverName() const { return server_name_; }
    [[nodiscard]] qsizetype pendingCommandCount() const noexcept { return queue_.size(); }
    [[nodiscard]] qsizetype pendingPayloadBytes() const noexcept { return queued_payload_bytes_; }

    [[nodiscard]] std::optional<OpenSourcesCommand> takeNextCommand() {
        if (queue_.isEmpty())
            return std::nullopt;
        QueuedCommand queued = std::move(queue_.head());
        queue_.dequeue();
        queued_payload_bytes_ -= queued.payload_bytes;
        return std::move(queued.command);
    }

    void setAccepting(const bool accepting) noexcept {
        if (!primary_ || server_ == nullptr || !server_->isListening() || shutdown_started_)
            return;
        accepting_ = accepting;
    }

    void beginShutdown() {
        if (!primary_ || server_ == nullptr || !server_->isListening())
            return;
        shutdown_started_ = true;
        accepting_ = false;
    }

  private:
    struct QueuedCommand final {
        OpenSourcesCommand command{};
        qsizetype payload_bytes{0};
    };

    [[nodiscard]] QString validateOptions() const {
        if (options_.lock_file_path.isEmpty() || !QFileInfo{options_.lock_file_path}.isAbsolute())
            return QStringLiteral("invalidLockPath");
        if (options_.server_name_prefix.isEmpty())
            return QStringLiteral("invalidServerName");
        if (options_.max_bootstrap_commands < 1 || options_.max_bootstrap_payload_bytes < 1)
            return QStringLiteral("invalidQueueLimit");
        if (options_.max_active_connections < 1)
            return QStringLiteral("invalidConnectionLimit");
        if (options_.secondary_retry_timeout.count() <= 0 || options_.secondary_retry_interval.count() <= 0 ||
            options_.frame_timeout.count() <= 0) {
            return QStringLiteral("invalidTimeout");
        }
        return {};
    }

    [[nodiscard]] QString endpointForPid(const qint64 pid) const {
        return options_.server_name_prefix + QLatin1Char('.') + QString::number(pid);
    }

    [[nodiscard]] QtSingleInstanceStartResult becomePrimary(const OpenSourcesCommand& initial_command,
                                                            const QByteArray& initial_payload) {
        server_name_ = endpointForPid(QCoreApplication::applicationPid());
#ifndef Q_OS_WIN
        static_cast<void>(QLocalServer::removeServer(server_name_));
#endif
        server_ = std::make_unique<BoundedLocalServer>(pending_connection_read_buffer_bytes);
        server_->setSocketOptions(QLocalServer::UserAccessOption);
        server_->setListenBacklogSize(listen_backlog_size);
        server_->setMaxPendingConnections(max_pending_connections);
        QObject::connect(server_.get(), &QLocalServer::newConnection, owner_, [this] { acceptConnections(); });
        primary_ = true;
        accepting_ = true;
        if (!server_->listen(server_name_)) {
            const QString detail = server_->errorString();
            closeServerAndReleaseLock();
            return {.detail = detail.isEmpty() ? QStringLiteral("listenFailed") : detail};
        }
        if (!initial_command.paths.isEmpty() && !enqueue(initial_command, initial_payload.size())) {
            closeServerAndReleaseLock();
            return {.detail = QStringLiteral("bootstrapQueueTooSmall")};
        }
        return {.outcome = QtSingleInstanceOutcome::Primary};
    }

    void acceptConnections() {
        while (server_ != nullptr && server_->hasPendingConnections()) {
            QLocalSocket* socket = server_->nextPendingConnection();
            if (socket == nullptr)
                continue;
            if (connections_.size() >= options_.max_active_connections) {
                rejectExcessConnection(socket);
                continue;
            }
            socket->setReadBufferSize(open_sources_max_payload_bytes + frame_prefix_bytes + 1);
            auto* connection = new InboundConnection(
                socket, boundedMilliseconds(options_.frame_timeout),
                [this](const QByteArray& payload) { return handlePayload(payload); }, owner_);
            connections_.insert(connection);
            QObject::connect(connection, &QObject::destroyed, owner_,
                             [this, connection] { connections_.remove(connection); });
        }
    }

    void rejectExcessConnection(QLocalSocket* socket) {
        socket->setReadBufferSize(pending_connection_read_buffer_bytes);
        socket->setParent(owner_);
        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QLocalSocket::errorOccurred, socket, [socket](QLocalSocket::LocalSocketError) {
            socket->abort();
            socket->deleteLater();
        });
        const QByteArray response =
            makeFrame(encodeReply({.kind = WireReplyKind::Nack, .reason = QStringLiteral("tooManyConnections")}));
        if (socket->write(response) != response.size()) {
            socket->abort();
            socket->deleteLater();
            return;
        }
        socket->disconnectFromServer();
        if (socket->state() == QLocalSocket::UnconnectedState)
            socket->deleteLater();
    }

    [[nodiscard]] WireReply handlePayload(const QByteArray& payload) {
        if (!accepting_)
            return {.reason =
                        shutdown_started_ ? QStringLiteral("shuttingDown") : QStringLiteral("temporarilyUnavailable")};
        const auto decoded = decodeOpenSourcesCommand(payload);
        if (decoded.error != OpenSourcesCommandError::None)
            return {.reason = commandErrorDetail(decoded.error)};
        if (!enqueue(decoded.command, payload.size()))
            return {.reason = QStringLiteral("queueFull")};
        return {.kind = WireReplyKind::Ack};
    }

    [[nodiscard]] bool enqueue(const OpenSourcesCommand& command, const qsizetype payload_bytes) {
        if (queue_.size() >= options_.max_bootstrap_commands ||
            payload_bytes > options_.max_bootstrap_payload_bytes - queued_payload_bytes_) {
            return false;
        }
        queue_.enqueue({.command = command, .payload_bytes = payload_bytes});
        queued_payload_bytes_ += payload_bytes;
        emit owner_->commandAvailable();
        return true;
    }

    void closeServerAndReleaseLock() {
        accepting_ = false;
        if (server_ != nullptr)
            server_->close();
        const QList<InboundConnection*> connections = connections_.values();
        connections_.clear();
        for (InboundConnection* connection : connections)
            delete connection;
        server_.reset();
        primary_ = false;
        if (lock_ != nullptr && lock_->isLocked())
            lock_->unlock();
        lock_.reset();
    }

    QtSingleInstanceCoordinator* owner_;
    QtSingleInstanceOptions options_;
    ForegroundPermissionTransfer foreground_permission_transfer_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<BoundedLocalServer> server_;
    QSet<InboundConnection*> connections_;
    QQueue<QueuedCommand> queue_;
    qsizetype queued_payload_bytes_{0};
    QString server_name_;
    bool started_{false};
    bool primary_{false};
    bool accepting_{false};
    bool shutdown_started_{false};
};

QtSingleInstanceCoordinator::QtSingleInstanceCoordinator(QtSingleInstanceOptions options, QObject* parent)
    : QtSingleInstanceCoordinator(std::move(options), &allowSetForegroundWindowForOwner, parent) {}

QtSingleInstanceCoordinator::QtSingleInstanceCoordinator(QtSingleInstanceOptions options,
                                                         const ForegroundPermissionTransfer transfer, QObject* parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>(this, std::move(options),
                                   transfer != nullptr ? transfer : &allowSetForegroundWindowForOwner)) {}

bool QtSingleInstanceCoordinator::tryTransferForegroundPermission(
    const qint64 owner_pid, const ForegroundPermissionTransfer transfer) noexcept {
#ifdef Q_OS_WIN
    constexpr qint64 maximum_pid = static_cast<qint64>(std::numeric_limits<DWORD>::max());
#else
    constexpr qint64 maximum_pid = static_cast<qint64>(std::numeric_limits<quint32>::max());
#endif
    if (owner_pid <= 0 || owner_pid > maximum_pid || transfer == nullptr)
        return false;
    return transfer(static_cast<quint32>(owner_pid));
}

QtSingleInstanceCoordinator::~QtSingleInstanceCoordinator() = default;

QtSingleInstanceStartResult QtSingleInstanceCoordinator::exchangeConnectedPrimary(QLocalSocket& socket,
                                                                                  const QByteArray& payload,
                                                                                  QDeadlineTimer& deadline) {
    return startResult(exchangeConnected(socket, payload, deadline));
}

QtSingleInstanceStartResult QtSingleInstanceCoordinator::start(const OpenSourcesCommand& initial_command) {
    return impl_->start(initial_command);
}

bool QtSingleInstanceCoordinator::isPrimary() const noexcept { return impl_->isPrimary(); }

bool QtSingleInstanceCoordinator::isAccepting() const noexcept { return impl_->isAccepting(); }

QString QtSingleInstanceCoordinator::serverName() const { return impl_->serverName(); }

qsizetype QtSingleInstanceCoordinator::pendingCommandCount() const noexcept { return impl_->pendingCommandCount(); }

qsizetype QtSingleInstanceCoordinator::pendingPayloadBytes() const noexcept { return impl_->pendingPayloadBytes(); }

std::optional<OpenSourcesCommand> QtSingleInstanceCoordinator::takeNextCommand() { return impl_->takeNextCommand(); }

void QtSingleInstanceCoordinator::setAccepting(const bool accepting) noexcept { impl_->setAccepting(accepting); }

void QtSingleInstanceCoordinator::beginShutdown() { impl_->beginShutdown(); }

} // namespace music_player::m0

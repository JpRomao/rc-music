#include "WindowsDownloadProcess.hpp"
#include "WindowsDownloadAppContainer.hpp"
#include "WindowsDownloadReadInput.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// sddl.h declares its APIs only after the Windows types have been included.
#include <sddl.h>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr DWORD pipe_bytes = static_cast<DWORD>(download_process_poll_bytes / 2);

class Handle final {
  public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const { return value_; }
    [[nodiscard]] bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) {
        if (valid())
            ::CloseHandle(value_);
        value_ = value;
    }

  private:
    HANDLE value_{nullptr};
};

constexpr std::size_t token_information_bytes = 64 * 1024;
using TokenInformation = std::array<std::byte, token_information_bytes>;

[[nodiscard]] bool readTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind, TokenInformation& storage,
                                        const DWORD minimum, DWORD& used) noexcept {
    DWORD required = 0;
    if (::GetTokenInformation(token, kind, nullptr, 0, &required))
        return false;
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < minimum || required > storage.size())
        return false;
    used = 0;
    return ::GetTokenInformation(token, kind, storage.data(), required, &used) && used >= minimum && used <= required;
}

[[nodiscard]] bool containedSid(PSID sid, const TokenInformation& storage, const DWORD used) noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(storage.data());
    const auto position = reinterpret_cast<std::uintptr_t>(sid);
    if (position < begin || position - begin > used || used - (position - begin) < 8)
        return false;
    const auto* bytes = reinterpret_cast<const BYTE*>(sid);
    const auto count = bytes[1];
    return count <= SID_MAX_SUB_AUTHORITIES && ::GetSidLengthRequired(count) <= used - (position - begin) &&
           ::IsValidSid(sid);
}

[[nodiscard]] bool verifiedAppContainerProcess(HANDLE process, HANDLE job,
                                               const WindowsDownloadAppContainer& identity) noexcept {
    HANDLE raw = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &raw))
        return false;
    const Handle token(raw);
    DWORD is_app_container = 0, used = 0;
    if (!token.valid() ||
        !::GetTokenInformation(token.get(), TokenIsAppContainer, &is_app_container, sizeof(is_app_container), &used) ||
        used != sizeof(is_app_container) || is_app_container == 0)
        return false;
    alignas(TOKEN_MANDATORY_LABEL) TokenInformation storage{};
    if (!readTokenInformation(token.get(), TokenAppContainerSid, storage, sizeof(TOKEN_APPCONTAINER_INFORMATION), used))
        return false;
    const auto sid = reinterpret_cast<const TOKEN_APPCONTAINER_INFORMATION*>(storage.data())->TokenAppContainer;
    if (!containedSid(sid, storage, used) || !::EqualSid(sid, const_cast<char*>(identity.sidBytes().constData())))
        return false;
    if (!readTokenInformation(token.get(), TokenIntegrityLevel, storage, sizeof(TOKEN_MANDATORY_LABEL), used))
        return false;
    const auto& label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(storage.data())->Label;
    alignas(DWORD) std::array<std::byte, SECURITY_MAX_SID_SIZE> low_sid{};
    DWORD low_size = static_cast<DWORD>(low_sid.size());
    if (!containedSid(label.Sid, storage, used) || !(label.Attributes & SE_GROUP_INTEGRITY) ||
        !::CreateWellKnownSid(WinLowLabelSid, nullptr, low_sid.data(), &low_size) ||
        !::EqualSid(label.Sid, low_sid.data()))
        return false;
    if (!readTokenInformation(token.get(), TokenCapabilities, storage, sizeof(DWORD), used) ||
        reinterpret_cast<const TOKEN_GROUPS*>(storage.data())->GroupCount != 0)
        return false;
    BOOL in_job = FALSE;
    return ::IsProcessInJob(process, job, &in_job) && in_job != FALSE;
}

class LocalAllocation final {
  public:
    ~LocalAllocation() {
        if (value)
            ::LocalFree(value);
    }
    void* value{nullptr};
};

[[nodiscard]] bool userPipeSecurity(LocalAllocation& descriptor) {
    HANDLE raw_token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_token))
        return false;
    Handle token(raw_token);
    DWORD bytes = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0 || bytes > 64 * 1024)
        return false;
    QByteArray information(static_cast<qsizetype>(bytes), '\0');
    if (!::GetTokenInformation(token.get(), TokenUser, information.data(), bytes, &bytes))
        return false;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(information.constData());
    wchar_t* sid = nullptr;
    if (!::ConvertSidToStringSidW(user->User.Sid, &sid))
        return false;
    LocalAllocation sid_allocation;
    sid_allocation.value = sid;
    const auto sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &raw_descriptor,
                                                                nullptr))
        return false;
    descriptor.value = raw_descriptor;
    return true;
}

class NativePipe final {
  public:
    ~NativePipe() { close(); }

    [[nodiscard]] bool create(PSECURITY_DESCRIPTOR descriptor) {
        const auto name =
            QStringLiteral("\\\\.\\pipe\\RCMusic-download-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto native_name = name.toStdWString();
        SECURITY_ATTRIBUTES server_attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        reader_.reset(::CreateNamedPipeW(native_name.c_str(),
                                         PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                         1, 0, pipe_bytes, 0, &server_attributes));
        if (!reader_.valid())
            return false;
        SECURITY_ATTRIBUTES client_attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, TRUE};
        writer_.reset(::CreateFileW(native_name.c_str(), GENERIC_WRITE, 0, &client_attributes, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!writer_.valid())
            return false;
        // Opening our client first connects it; ERROR_PIPE_CONNECTED is success.
        OVERLAPPED connection{};
        if (!::ConnectNamedPipe(reader_.get(), &connection) && ::GetLastError() != ERROR_PIPE_CONNECTED)
            return false;
        event_.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event_.valid())
            return false;
        operation_.hEvent = event_.get();
        ended_ = false;
        failed_ = false;
        beginRead();
        return !failed_;
    }

    [[nodiscard]] HANDLE writer() const { return writer_.get(); }
    void releaseWriter() { writer_.reset(); }
    [[nodiscard]] bool ended() const { return ended_; }
    [[nodiscard]] bool failed() const { return failed_; }

    [[nodiscard]] QByteArray take() {
        if (pending_) {
            DWORD count = 0;
            if (::GetOverlappedResult(reader_.get(), &operation_, &count, FALSE)) {
                pending_ = false;
                completed(count);
            } else {
                const auto error = ::GetLastError();
                if (error != ERROR_IO_INCOMPLETE) {
                    pending_ = false;
                    finishError(error);
                }
            }
        }
        if (!ready_)
            return {};
        const QByteArray bytes(buffer_.data(), static_cast<qsizetype>(ready_));
        ready_ = 0;
        beginRead();
        return bytes;
    }

    void close() {
        writer_.reset();
        if (pending_ && reader_.valid()) {
            ::CancelIoEx(reader_.get(), &operation_);
            DWORD ignored = 0;
            // The OVERLAPPED buffer cannot be freed while the native request owns
            // it. Normal terminal polling already observes completion; this is
            // a worker-only destruction safeguard for an abandoned request.
            ::GetOverlappedResult(reader_.get(), &operation_, &ignored, TRUE);
        }
        pending_ = false;
        ready_ = 0;
        ended_ = true;
        reader_.reset();
        event_.reset();
        operation_ = {};
    }

  private:
    void completed(DWORD count) {
        if (count == 0)
            ended_ = true;
        else
            ready_ = count;
    }

    void finishError(DWORD error) {
        ended_ = true;
        failed_ = error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED && error != ERROR_OPERATION_ABORTED;
    }

    void beginRead() {
        if (ended_ || pending_ || ready_ != 0)
            return;
        operation_ = {};
        operation_.hEvent = event_.get();
        ::ResetEvent(event_.get());
        DWORD count = 0;
        if (::ReadFile(reader_.get(), buffer_.data(), pipe_bytes, &count, &operation_)) {
            completed(count);
        } else {
            const auto error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                pending_ = true;
            else
                finishError(error);
        }
    }

    Handle reader_{};
    Handle writer_{};
    Handle event_{};
    OVERLAPPED operation_{};
    std::array<char, pipe_bytes> buffer_{};
    DWORD ready_{0};
    bool pending_{false};
    bool ended_{true};
    bool failed_{false};
};

class NativeInput final {
  public:
    ~NativeInput() { close(); }

    [[nodiscard]] bool create(PSECURITY_DESCRIPTOR descriptor, const QByteArray& payload) {
        const auto name =
            QStringLiteral("\\\\.\\pipe\\RCMusic-download-input-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto native_name = name.toStdWString();
        SECURITY_ATTRIBUTES server_attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        writer_.reset(::CreateNamedPipeW(native_name.c_str(),
                                         PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                         1, pipe_bytes, 0, 0, &server_attributes));
        if (!writer_.valid())
            return false;
        SECURITY_ATTRIBUTES client_attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, TRUE};
        reader_.reset(::CreateFileW(native_name.c_str(), GENERIC_READ, 0, &client_attributes, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!reader_.valid())
            return false;
        OVERLAPPED connection{};
        if (!::ConnectNamedPipe(writer_.get(), &connection) && ::GetLastError() != ERROR_PIPE_CONNECTED)
            return false;
        event_.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event_.valid())
            return false;
        payload_ = payload;
        offset_ = 0;
        ended_ = false;
        failed_ = false;
        cancelled_ = false;
        return true;
    }

    [[nodiscard]] HANDLE reader() const { return reader_.get(); }
    void releaseReader() { reader_.reset(); }
    [[nodiscard]] bool ended() const { return ended_; }
    [[nodiscard]] bool failed() const { return failed_; }

    void poll() {
        if (ended_)
            return;
        if (pending_) {
            DWORD count = 0;
            if (::GetOverlappedResult(writer_.get(), &operation_, &count, FALSE)) {
                pending_ = false;
                complete(count);
            } else {
                const auto error = ::GetLastError();
                if (error == ERROR_IO_INCOMPLETE)
                    return;
                pending_ = false;
                failed_ = !cancelled_;
                finish();
                return;
            }
        }
        if (cancelled_ || offset_ == payload_.size()) {
            finish();
            return;
        }
        if (failed_) {
            finish();
            return;
        }
        const auto count = static_cast<DWORD>(std::min<qsizetype>(pipe_bytes, payload_.size() - offset_));
        std::copy_n(payload_.constData() + offset_, count, buffer_.data());
        requested_ = count;
        operation_ = {};
        operation_.hEvent = event_.get();
        ::ResetEvent(event_.get());
        DWORD written = 0;
        if (::WriteFile(writer_.get(), buffer_.data(), count, &written, &operation_)) {
            complete(written);
            if (!failed_ && offset_ == payload_.size())
                finish();
        } else if (::GetLastError() == ERROR_IO_PENDING) {
            pending_ = true;
        } else {
            failed_ = true;
            finish();
        }
    }

    void cancel() {
        if (ended_)
            return;
        cancelled_ = true;
        if (pending_)
            ::CancelIoEx(writer_.get(), &operation_);
        else
            finish();
    }

    void close() {
        reader_.reset();
        if (pending_ && writer_.valid()) {
            ::CancelIoEx(writer_.get(), &operation_);
            DWORD ignored = 0;
            // The same worker-only lifetime safeguard as the output pipes.
            ::GetOverlappedResult(writer_.get(), &operation_, &ignored, TRUE);
        }
        pending_ = false;
        finish();
        operation_ = {};
        failed_ = false;
        cancelled_ = false;
        requested_ = 0;
    }

  private:
    void complete(DWORD count) {
        if (count == 0 || count > requested_)
            failed_ = true;
        else
            offset_ += static_cast<qsizetype>(count);
    }

    void finish() {
        writer_.reset();
        event_.reset();
        payload_.clear();
        ::SecureZeroMemory(buffer_.data(), buffer_.size());
        offset_ = 0;
        ended_ = true;
    }

    Handle reader_{};
    Handle writer_{};
    Handle event_{};
    OVERLAPPED operation_{};
    std::array<char, pipe_bytes> buffer_{};
    QByteArray payload_{};
    qsizetype offset_{0};
    DWORD requested_{0};
    bool pending_{false};
    bool ended_{true};
    bool failed_{false};
    bool cancelled_{false};
};

[[nodiscard]] bool validText(const QString& value) {
    if (value.contains(QChar::Null))
        return false;
    for (qsizetype index = 0; index < value.size(); ++index) {
        if (value.at(index).isHighSurrogate()) {
            if (++index == value.size() || !value.at(index).isLowSurrogate())
                return false;
        } else if (value.at(index).isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool localAbsolute(const QString& value) {
    return validText(value) && value.size() <= download_process_max_path_units && QDir::isAbsolutePath(value) &&
           !value.startsWith(QStringLiteral("\\\\")) && !value.startsWith(QStringLiteral("//"));
}

[[nodiscard]] bool validEnvironment(const std::optional<QProcessEnvironment>& environment) {
    if (!environment)
        return true;
    // Qt/Windows reintroduces PATH and SystemRoot from the parent when absent.
    // Never let an explicit trusted environment silently become partial inheritance.
    if (!environment->contains(QStringLiteral("PATH")) || !environment->contains(QStringLiteral("SystemRoot")) ||
        environment->value(QStringLiteral("SystemRoot")).isEmpty())
        return false;
    const auto keys = environment->keys();
    if (keys.size() > 128)
        return false;
    qsizetype units = 1; // Final environment-block terminator.
    for (const auto& key : keys) {
        const auto value = environment->value(key);
        if (key.isEmpty() || key.contains(QChar::Null) || key.contains('=') || value.contains(QChar::Null) ||
            key.size() > download_process_max_environment_units - units)
            return false;
        units += key.size();
        if (value.size() > download_process_max_environment_units - units - 2)
            return false;
        units += value.size() + 2; // '=' and this entry's terminator.
    }
    return units <= download_process_max_environment_units;
}

[[nodiscard]] bool validConfig(const WindowsDownloadProcessConfig& config) {
    if (!localAbsolute(config.executable) || !QFileInfo(config.executable).isFile() ||
        QFileInfo(config.executable).suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0 ||
        !localAbsolute(config.working_directory) || !QFileInfo(config.working_directory).isDir() ||
        config.max_stdout_bytes == 0 || config.max_stderr_bytes == 0 || config.max_memory_bytes == 0 ||
        config.max_memory_bytes > std::numeric_limits<SIZE_T>::max() || config.total_timeout_ms <= 0 ||
        config.arguments.size() > 128 || config.standard_input.size() > download_process_max_input_bytes ||
        !validEnvironment(config.environment))
        return false;
    qsizetype units = config.executable.size();
    for (const auto& argument : config.arguments) {
        if (!validText(argument) || argument.size() > 8192 || units > 16384 - argument.size() - 1)
            return false;
        units += argument.size() + 1;
    }
    return units <= 16384;
}

[[nodiscard]] bool terminal(WindowsDownloadProcessState state) {
    return state == WindowsDownloadProcessState::Exited || state == WindowsDownloadProcessState::Failed ||
           state == WindowsDownloadProcessState::Cancelled;
}

} // namespace

struct WindowsDownloadProcess::Impl final {
    WindowsDownloadProcessUpdate update{};
    WindowsDownloadProcessConfig config{};
    std::unique_ptr<QProcess> process{};
    Handle job{};
    Handle process_handle{};
    NativePipe stdout_pipe{};
    NativePipe stderr_pipe{};
    NativeInput stdin_pipe{};
    STARTUPINFOEXW startup{};
    SECURITY_CAPABILITIES security_capabilities{};
    std::array<HANDLE, 1> assigned_jobs{};
    std::array<HANDLE, 4> inherited{};
    QByteArray attribute_storage{};
    std::wstring native_executable{};
    Q_PROCESS_INFORMATION* created_information{nullptr};
    quint64 stdout_count{0};
    quint64 stderr_count{0};
    qint64 started_ms{0};
    qint64 last_ms{0};
    WindowsDownloadProcessState stopped_state{WindowsDownloadProcessState::Exited};
    bool read_input_acquired{false};
    bool read_input_inheritable{false};

    ~Impl() { release(); }

    [[nodiscard]] bool restoreInputInheritance() {
        if (!read_input_inheritable)
            return true;
        const auto restored = config.read_input->endInheritance();
        read_input_inheritable = !restored;
        return restored;
    }

    void releaseInputUse() {
        if (!read_input_acquired)
            return;
        (void)restoreInputInheritance();
        config.read_input->releaseUse();
        read_input_acquired = false;
        read_input_inheritable = false;
    }

    void releaseAttributes() {
        if (startup.lpAttributeList) {
            ::DeleteProcThreadAttributeList(startup.lpAttributeList);
            startup.lpAttributeList = nullptr;
        }
        attribute_storage.clear();
        security_capabilities = {};
    }

    void release() {
        if (job.valid())
            ::TerminateJobObject(job.get(), 91);
        stdin_pipe.close();
        stdout_pipe.close();
        stderr_pipe.close();
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
            static_cast<void>(process->waitForFinished(5000));
        }
        process.reset();
        process_handle.reset();
        job.reset();
        releaseInputUse();
        config.read_input.reset();
        releaseAttributes();
        config.app_container.reset();
        created_information = nullptr;
        config.standard_input.clear();
    }

    [[nodiscard]] bool prepare() {
        job.reset(::CreateJobObjectW(nullptr, nullptr));
        if (!job.valid())
            return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                                  JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(config.max_memory_bytes);
        limits.JobMemoryLimit = static_cast<SIZE_T>(config.max_memory_bytes);
        if (!::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            return false;
        LocalAllocation descriptor;
        if (!userPipeSecurity(descriptor) || !stdout_pipe.create(descriptor.value) ||
            !stderr_pipe.create(descriptor.value) ||
            (!config.standard_input.isEmpty() && !stdin_pipe.create(descriptor.value, config.standard_input)))
            return false;
        SIZE_T bytes = 0;
        const DWORD count = config.app_container ? 3 : 2;
        ::InitializeProcThreadAttributeList(nullptr, count, 0, &bytes);
        if (bytes == 0 || bytes > 64 * 1024)
            return false;
        attribute_storage = QByteArray(static_cast<qsizetype>(bytes), '\0');
        startup = {};
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
        if (!::InitializeProcThreadAttributeList(attributes, count, 0, &bytes))
            return false;
        startup.lpAttributeList = attributes;
        // The attribute API borrows the value storage until DeleteAttributeList.
        assigned_jobs[0] = job.get();
        if (!::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, assigned_jobs.data(),
                                         sizeof(assigned_jobs), nullptr, nullptr))
            return false;
        if (config.app_container) {
            security_capabilities.AppContainerSid = const_cast<char*>(config.app_container->sidBytes().constData());
            // The zero-initialized capability array/count/reserved grant none.
            // Both the struct and immutable SID outlive DeleteAttributeList.
            if (!::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                             &security_capabilities, sizeof(security_capabilities), nullptr, nullptr))
                return false;
        }
        return true;
    }

    void stop(WindowsDownloadProcessState state, WindowsDownloadProcessError error) {
        if (update.state == WindowsDownloadProcessState::Stopping || terminal(update.state))
            return;
        stopped_state = state;
        update.error = error;
        update.state = WindowsDownloadProcessState::Stopping;
        stdin_pipe.cancel();
        if (job.valid())
            ::TerminateJobObject(job.get(), 92);
    }

    [[nodiscard]] bool processEnded() {
        if (!process_handle.valid())
            return process && process->state() == QProcess::NotRunning;
        const auto waited = ::WaitForSingleObject(process_handle.get(), 0);
        if (waited == WAIT_TIMEOUT)
            return false;
        if (waited != WAIT_OBJECT_0) {
            stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::Io);
            return false;
        }
        DWORD exit_code = 0;
        if (::GetExitCodeProcess(process_handle.get(), &exit_code))
            update.exit_code = static_cast<int>(exit_code);
        else
            stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::Io);
        if (process && process->state() != QProcess::NotRunning)
            static_cast<void>(process->waitForFinished(0));
        return true;
    }

    [[nodiscard]] QByteArray take(NativePipe& pipe, quint64& count, quint64 maximum) {
        auto bytes = pipe.take();
        if (pipe.failed())
            stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::Io);
        if (update.state == WindowsDownloadProcessState::Stopping)
            return {};
        const auto size = static_cast<quint64>(bytes.size());
        if (size > maximum - count) {
            stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::LimitExceeded);
            return {};
        }
        count += size;
        return bytes;
    }
};

WindowsDownloadProcess::WindowsDownloadProcess() : impl_(std::make_unique<Impl>()) {}
WindowsDownloadProcess::~WindowsDownloadProcess() = default;

bool WindowsDownloadProcess::start(const WindowsDownloadProcessConfig& config, qint64 now_ms) {
    auto& value = *impl_;
    if (value.update.state == WindowsDownloadProcessState::Running ||
        value.update.state == WindowsDownloadProcessState::Stopping)
        return false;
    value.release();
    value.update = {};
    value.stdout_count = 0;
    value.stderr_count = 0;
    value.started_ms = now_ms;
    value.last_ms = now_ms;
    value.stopped_state = WindowsDownloadProcessState::Exited;
    if (config.require_app_container && !config.app_container) {
        value.update.state = WindowsDownloadProcessState::Failed;
        value.update.error = WindowsDownloadProcessError::Isolation;
        return false;
    }
    if (!validConfig(config)) {
        value.update.state = WindowsDownloadProcessState::Failed;
        value.update.error = WindowsDownloadProcessError::InvalidConfiguration;
        return false;
    }
    value.config = config;
    if (config.read_input) {
        if (!config.read_input->acquireUse()) {
            // Rejection has no child and owns no claim. Do not keep the other
            // owner's file alive after that owner has acknowledged its exit.
            value.config.read_input.reset();
            value.update.state = WindowsDownloadProcessState::Failed;
            value.update.error = WindowsDownloadProcessError::Isolation;
            return false;
        }
        value.read_input_acquired = true;
    }
    if (!value.prepare()) {
        value.release();
        value.update.state = WindowsDownloadProcessState::Failed;
        value.update.error = WindowsDownloadProcessError::Isolation;
        return false;
    }
    value.native_executable = QDir::toNativeSeparators(config.executable).toStdWString();
    value.process = std::make_unique<QProcess>();
    value.process->setProgram(config.executable);
    value.process->setArguments(config.arguments);
    value.process->setWorkingDirectory(config.working_directory);
    value.process->setStandardInputFile(QProcess::nullDevice());
    value.process->setStandardOutputFile(QProcess::nullDevice());
    value.process->setStandardErrorFile(QProcess::nullDevice());
    auto environment = config.environment ? *config.environment : QProcessEnvironment::systemEnvironment();
    // FFREPORT can otherwise write a diagnostic file outside the owned job.
    environment.remove(QStringLiteral("FFREPORT"));
    environment.remove(QStringLiteral("AV_LOG_FORCE_COLOR"));
    environment.remove(QStringLiteral("AV_LOG_FORCE_NOCOLOR"));
    value.process->setProcessEnvironment(environment);
    bool attributes_ready = true;
    value.process->setCreateProcessArgumentsModifier([&value,
                                                      &attributes_ready](QProcess::CreateProcessArguments* arguments) {
        value.startup.StartupInfo = *arguments->startupInfo;
        value.startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        value.startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        value.startup.StartupInfo.wShowWindow = SW_HIDE;
        if (!value.config.standard_input.isEmpty())
            value.startup.StartupInfo.hStdInput = value.stdin_pipe.reader();
        value.startup.StartupInfo.hStdOutput = value.stdout_pipe.writer();
        value.startup.StartupInfo.hStdError = value.stderr_pipe.writer();
        value.inherited = {value.startup.StartupInfo.hStdInput, value.stdout_pipe.writer(), value.stderr_pipe.writer()};
        std::size_t inherited_count = 3;
        if (value.config.read_input) {
            value.read_input_inheritable = value.config.read_input->beginInheritance();
            attributes_ready = value.read_input_inheritable;
            value.inherited[3] =
                reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value.config.read_input->handleValue()));
            inherited_count = 4;
        }
        attributes_ready = attributes_ready &&
                           ::UpdateProcThreadAttribute(value.startup.lpAttributeList, 0,
                                                       PROC_THREAD_ATTRIBUTE_HANDLE_LIST, value.inherited.data(),
                                                       inherited_count * sizeof(HANDLE), nullptr, nullptr) != FALSE;
        arguments->startupInfo = &value.startup.StartupInfo;
        arguments->flags |= CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
        if (value.config.app_container)
            arguments->flags |= CREATE_SUSPENDED;
        arguments->inheritHandles = true;
        // Force failure rather than creating a child with incomplete attributes.
        arguments->applicationName = value.native_executable.c_str();
        if (!attributes_ready)
            arguments->flags |= CREATE_NEW_CONSOLE | DETACHED_PROCESS; // Mutually exclusive: CreateProcess must fail.
        value.created_information = arguments->processInformation;
    });
    value.process->start();
    value.process->setCreateProcessArgumentsModifier({});
    const auto inheritance_restored = value.restoreInputInheritance();
    if (value.process->state() == QProcess::NotRunning) {
        value.release();
        value.update.state = WindowsDownloadProcessState::Failed;
        value.update.error = !config.app_container && attributes_ready && inheritance_restored
                                 ? WindowsDownloadProcessError::Start
                                 : WindowsDownloadProcessError::Isolation;
        return false;
    }
    HANDLE native_process = nullptr;
    const bool monitored =
        value.created_information &&
        ::DuplicateHandle(::GetCurrentProcess(), value.created_information->hProcess, ::GetCurrentProcess(),
                          &native_process, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
    value.process_handle.reset(native_process);
    // The initial thread must remain suspended until the kernel token and our
    // Job are witnessed. No private stdin is written before start returns/poll.
    const bool sandbox_ready =
        !config.app_container ||
        (monitored && inheritance_restored && value.created_information &&
         verifiedAppContainerProcess(value.process_handle.get(), value.job.get(), *config.app_container) &&
         ::ResumeThread(value.created_information->hThread) == 1);
    value.created_information = nullptr;
    value.releaseAttributes();
    value.stdout_pipe.releaseWriter();
    value.stderr_pipe.releaseWriter();
    value.stdin_pipe.releaseReader();
    value.config.standard_input.clear();
    value.update.state = WindowsDownloadProcessState::Running;
    if (!inheritance_restored || !sandbox_ready)
        value.stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::Isolation);
    else if (!monitored)
        value.stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::Io);
    return true;
}

WindowsDownloadProcessUpdate WindowsDownloadProcess::poll(qint64 now_ms) {
    auto& value = *impl_;
    value.update.stdout_chunk.clear();
    value.update.stderr_chunk.clear();
    if (value.update.state == WindowsDownloadProcessState::Idle || terminal(value.update.state))
        return value.update;
    if (value.update.state == WindowsDownloadProcessState::Running) {
        if (now_ms < value.last_ms)
            value.stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::InvalidConfiguration);
        else if (static_cast<quint64>(now_ms) - static_cast<quint64>(value.started_ms) >=
                 static_cast<quint64>(value.config.total_timeout_ms))
            value.stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::Timeout);
        value.last_ms = now_ms;
    } else if (value.job.valid()) {
        ::TerminateJobObject(value.job.get(), 92);
    }
    value.stdin_pipe.poll();
    if (value.stdin_pipe.failed())
        value.stop(WindowsDownloadProcessState::Failed, WindowsDownloadProcessError::Io);
    value.update.stdout_chunk = value.take(value.stdout_pipe, value.stdout_count, value.config.max_stdout_bytes);
    value.update.stderr_chunk = value.take(value.stderr_pipe, value.stderr_count, value.config.max_stderr_bytes);
    const bool process_ended = value.processEnded();
    if (process_ended && value.stdin_pipe.ended() && value.stdout_pipe.ended() && value.stderr_pipe.ended()) {
        value.update.state = value.update.state == WindowsDownloadProcessState::Stopping
                                 ? value.stopped_state
                                 : WindowsDownloadProcessState::Exited;
        value.stdout_pipe.close();
        value.stderr_pipe.close();
        value.stdin_pipe.close();
        value.process_handle.reset();
        value.job.reset();
        value.releaseInputUse();
        // Data handles must leave before the workspace removes its job tree.
        // A delete-shared handle can still keep a legacy unlink delete-pending.
        value.config.read_input.reset();
    }
    return value.update;
}

void WindowsDownloadProcess::cancel() {
    auto& value = *impl_;
    if (value.update.state == WindowsDownloadProcessState::Idle)
        value.update.state = WindowsDownloadProcessState::Cancelled;
    else if (value.update.state == WindowsDownloadProcessState::Running && value.processEnded())
        // Preserve a witnessed natural exit while pending pipes drain under their normal limits.
        return;
    else
        value.stop(WindowsDownloadProcessState::Cancelled, WindowsDownloadProcessError::None);
}

} // namespace music_player::m0

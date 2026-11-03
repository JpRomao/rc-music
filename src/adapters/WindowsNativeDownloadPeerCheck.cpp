#include "adapters/WindowsNativeDownloadPeerCheck.hpp"

#include <QStringView>
#include <qt_windows.h>

#include <atomic>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {
constexpr qsizetype maximum_image_characters = 32'767;

class Handle final {
  public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    HANDLE get() const { return value_; }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset() noexcept {
        if (valid())
            CloseHandle(value_);
        value_ = nullptr;
    }

  private:
    HANDLE value_{nullptr};
};

bool ordinalEqual(const QStringView value, const wchar_t* literal) {
    return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(value.utf16()), static_cast<int>(value.size()),
                                literal, -1, TRUE) == CSTR_EQUAL;
}

bool reservedDevice(const QStringView component) {
    const auto dot = component.indexOf(QLatin1Char('.'));
    auto stem = dot < 0 ? component : component.first(dot);
    while (!stem.isEmpty() && stem.back() == QLatin1Char(' '))
        stem = stem.first(stem.size() - 1);
    for (const auto* reserved : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"}) {
        if (ordinalEqual(stem, reserved))
            return true;
    }
    if (stem.size() != 4 || (!ordinalEqual(stem.first(3), L"COM") && !ordinalEqual(stem.first(3), L"LPT")))
        return false;
    const auto suffix = stem[3].unicode();
    return (suffix >= '1' && suffix <= '9') || suffix == 0x00b9 || suffix == 0x00b2 || suffix == 0x00b3;
}

bool localImagePath(const QString& image) {
    if (image.size() < 4 || image.size() > maximum_image_characters)
        return false;
    const auto drive = image[0].unicode();
    if (!((drive >= 'a' && drive <= 'z') || (drive >= 'A' && drive <= 'Z')) || image[1] != QLatin1Char(':') ||
        (image[2] != QLatin1Char('\\') && image[2] != QLatin1Char('/')))
        return false;
    qsizetype component_start = 3;
    for (qsizetype index = 3; index <= image.size(); ++index) {
        if (index == image.size() || image[index] == QLatin1Char('\\') || image[index] == QLatin1Char('/')) {
            if (index == component_start || image[index - 1] == QLatin1Char('.') ||
                image[index - 1] == QLatin1Char(' '))
                return false;
            if (reservedDevice(QStringView{image}.mid(component_start, index - component_start)))
                return false;
            component_start = index + 1;
            continue;
        }
        const auto unit = image[index].unicode();
        if (unit < 0x20 || image[index] == QLatin1Char(':') || image[index] == QLatin1Char('"') ||
            image[index] == QLatin1Char('<') || image[index] == QLatin1Char('>') || image[index] == QLatin1Char('|') ||
            image[index] == QLatin1Char('?') || image[index] == QLatin1Char('*'))
            return false;
        if (QChar::isHighSurrogate(unit)) {
            if (index + 1 >= image.size() || !image[index + 1].isLowSurrogate())
                return false;
            ++index;
        } else if (QChar::isLowSurrogate(unit)) {
            return false;
        }
    }
    return true;
}

bool processAlive(const Handle& process) {
    return process.valid() && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT;
}

DWORD peerPid(HANDLE pipe, const NativeDownloadPeerSide side) {
    ULONG pid = 0;
    const auto success = side == NativeDownloadPeerSide::Client ? GetNamedPipeClientProcessId(pipe, &pid)
                                                                : GetNamedPipeServerProcessId(pipe, &pid);
    return success ? pid : 0;
}

QByteArray processUserSid(HANDLE process) {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw_token))
        return {};
    const Handle token{raw_token};
    DWORD bytes = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes));
    if (bytes < sizeof(TOKEN_USER) || bytes > 64 * 1024)
        return {};
    std::vector<unsigned char> storage(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, storage.data(), bytes, &bytes))
        return {};
    const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    if (!IsValidSid(user->User.Sid))
        return {};
    const auto sid_bytes = GetLengthSid(user->User.Sid);
    if (sid_bytes == 0 || sid_bytes > SECURITY_MAX_SID_SIZE)
        return {};
    return {static_cast<const char*>(user->User.Sid), static_cast<qsizetype>(sid_bytes)};
}

bool imageMatches(HANDLE process, const QStringList& expected_images) {
    std::vector<wchar_t> buffer(static_cast<std::size_t>(maximum_image_characters + 1));
    DWORD size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size) || size == 0 || size > maximum_image_characters)
        return false;
    const auto image = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(size)).replace('/', '\\');
    if (!localImagePath(image))
        return false;
    const auto native_image = image.toStdWString();
    for (const auto& expected : expected_images) {
        const auto native_expected = QString{expected}.replace('/', '\\').toStdWString();
        if (CompareStringOrdinal(native_image.c_str(), static_cast<int>(native_image.size()), native_expected.c_str(),
                                 static_cast<int>(native_expected.size()), TRUE) == CSTR_EQUAL)
            return true;
    }
    return false;
}

struct Shared final {
    std::atomic_bool cancel{false};
    // The worker alone accesses these fields until join establishes completion.
    Handle pipe;
    Handle process;
    NativeDownloadPeerState result{NativeDownloadPeerState::Rejected};
};

void inspectPeer(const std::shared_ptr<Shared>& state, const NativeDownloadPeerSide side,
                 const QStringList& expected_images) noexcept {
    Handle eligible;
    try {
        const auto observe = [&]() -> Handle {
            if (state->cancel.load())
                return {};
            const auto pid = peerPid(state->pipe.get(), side);
            if (pid == 0)
                return {};
            Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)};
            if (!processAlive(process) || state->cancel.load())
                return {};
            const auto own_sid = processUserSid(GetCurrentProcess());
            auto peer_sid = processUserSid(process.get());
            if (own_sid.isEmpty() || peer_sid.isEmpty() ||
                !EqualSid(const_cast<char*>(own_sid.constData()), peer_sid.data()) || state->cancel.load() ||
                !imageMatches(process.get(), expected_images) || state->cancel.load() ||
                peerPid(state->pipe.get(), side) != pid || !processAlive(process))
                return {};
            return process;
        };
        eligible = observe();
    } catch (...) {
        // Native failures, allocation failure and cancellation never authorize.
    }
    // A duplicate pipe can keep EOF alive. Retire it before exposing any result;
    // the process observation is the only capability retained after completion.
    state->pipe.reset();
    if (state->cancel.load())
        eligible.reset();
    state->process = std::move(eligible);
    state->result = state->process.valid() ? NativeDownloadPeerState::Eligible : NativeDownloadPeerState::Rejected;
}
} // namespace

bool validNativePeerImages(const QStringList& expected_images) {
    if (expected_images.isEmpty() || expected_images.size() > 4)
        return false;
    for (const auto& image : expected_images) {
        if (!localImagePath(image))
            return false;
    }
    return true;
}

struct WindowsNativeDownloadPeerCheck::Impl {
    std::shared_ptr<Shared> state{std::make_shared<Shared>()};
    std::thread worker;
    NativeDownloadPeerState result{NativeDownloadPeerState::Idle};
    bool used{false};
    bool closing{false};

    bool retireWorker() {
        if (!worker.joinable())
            return true;
        // Neither a result field nor cancellation proves physical thread exit.
        // join is safe only after its native thread object is signaled.
        if (WaitForSingleObject(worker.native_handle(), 0) != WAIT_OBJECT_0)
            return false;
        worker.join();
        result = closing ? NativeDownloadPeerState::Rejected : state->result;
        if (closing || result != NativeDownloadPeerState::Eligible)
            state->process.reset();
        return true;
    }
};
WindowsNativeDownloadPeerCheck::WindowsNativeDownloadPeerCheck() : impl_(std::make_unique<Impl>()) {}
WindowsNativeDownloadPeerCheck::~WindowsNativeDownloadPeerCheck() {
    shutdown();
    if (impl_->worker.joinable())
        impl_->worker.detach(); // Shared worker state never borrows this/Impl/QObject.
}
bool WindowsNativeDownloadPeerCheck::begin(const qintptr borrowed_pipe, const NativeDownloadPeerSide side,
                                           const QStringList& expected_images) {
    if (impl_->used || impl_->closing)
        return false;
    impl_->used = true;
    impl_->result = NativeDownloadPeerState::Rejected;
    if (borrowed_pipe == 0 || borrowed_pipe == -1 ||
        (side != NativeDownloadPeerSide::Client && side != NativeDownloadPeerSide::Server) ||
        !validNativePeerImages(expected_images))
        return false;
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(borrowed_pipe), GetCurrentProcess(), &duplicate,
                         0, FALSE, DUPLICATE_SAME_ACCESS))
        return false;
    impl_->state->pipe = Handle{duplicate};
    try {
        impl_->worker =
            std::thread{[state = impl_->state, side, expected_images] { inspectPeer(state, side, expected_images); }};
    } catch (...) {
        impl_->state->pipe.reset();
        return false;
    }
    impl_->result = NativeDownloadPeerState::Pending;
    return true;
}
NativeDownloadPeerState WindowsNativeDownloadPeerCheck::poll() {
    const auto retired = impl_->retireWorker();
    if (impl_->closing)
        return NativeDownloadPeerState::Rejected;
    if (!retired)
        return NativeDownloadPeerState::Pending;
    if (impl_->result == NativeDownloadPeerState::Eligible && !processAlive(impl_->state->process)) {
        // Revocation does not release the observed process object. Keep its
        // handle until explicit shutdown retires this connection's authority.
        impl_->result = NativeDownloadPeerState::Rejected;
    }
    return impl_->result;
}
void WindowsNativeDownloadPeerCheck::shutdown() noexcept {
    impl_->closing = true;
    impl_->state->cancel.store(true);
    impl_->result = NativeDownloadPeerState::Rejected;
    try {
        if (impl_->retireWorker())
            impl_->state->process.reset();
    } catch (...) {
        // Cleanup remains pending; destruction can detach the shared worker.
    }
}
bool WindowsNativeDownloadPeerCheck::stopped() {
    if (!impl_->retireWorker())
        return false;
    if (impl_->closing)
        impl_->state->process.reset();
    return !impl_->state->pipe.valid() && !impl_->state->process.valid();
}
} // namespace music_player::m0

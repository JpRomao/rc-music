#include "adapters/WindowsMediaFoundationCodec.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace music_player::m0 {
namespace {
using Microsoft::WRL::ComPtr;
using Exit = MediaFoundationWorkerExit;
constexpr DWORD maximum_sample_bytes = 1024 * 1024;
constexpr DWORD audio_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr DWORD all_streams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);

class Handle final {
  public:
    explicit Handle(HANDLE value) : value_(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ~Handle() {
        if (valid())
            CloseHandle(value_);
    }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }

  private:
    HANDLE value_;
};

struct StreamState final {
    StreamState(Handle owned, const std::uint64_t length) : file(std::move(owned)), size(length) {}
    Handle file;
    const std::uint64_t size;
    SRWLOCK lock = SRWLOCK_INIT;
};

class StreamLock final {
  public:
    explicit StreamLock(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockExclusive(&lock_); }
    ~StreamLock() { ReleaseSRWLockExclusive(&lock_); }

  private:
    SRWLOCK& lock_;
};

// A read-only, unnamed stream over a duplicated read capability. Clones retain
// that capability and share the lock, but each has its own logical position.
// MF's IStream bridge supplies asynchronous byte-stream support to the reader.
class PinnedReadStream final : public IStream {
  public:
    explicit PinnedReadStream(std::shared_ptr<StreamState> state, const std::uint64_t position = 0)
        : state_(std::move(state)), position_(position) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_ISequentialStream && iid != IID_IStream)
            return E_NOINTERFACE;
        *object = static_cast<IStream*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining)
            delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Read(void* data, ULONG count, ULONG* received) override {
        if (received)
            *received = 0;
        if (!data && count)
            return STG_E_INVALIDPOINTER;
        const StreamLock guard{state_->lock};
        if (!count)
            return S_OK;
        if (position_ >= state_->size)
            return S_FALSE;
        const auto wanted = static_cast<DWORD>(std::min<std::uint64_t>(count, state_->size - position_));
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(position_);
        DWORD read = 0;
        if (!SetFilePointerEx(state_->file.get(), offset, nullptr, FILE_BEGIN) ||
            !ReadFile(state_->file.get(), data, wanted, &read, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());
        if (read > wanted)
            return STG_E_READFAULT;
        position_ += read;
        if (received)
            *received = read;
        return read == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* written) override {
        if (written)
            *written = 0;
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) override {
        const StreamLock guard{state_->lock};
        std::uint64_t base = 0;
        if (origin == STREAM_SEEK_CUR)
            base = position_;
        else if (origin == STREAM_SEEK_END)
            base = state_->size;
        else if (origin != STREAM_SEEK_SET)
            return STG_E_INVALIDFUNCTION;
        if (move.QuadPart < 0) {
            const auto distance = static_cast<std::uint64_t>(-(move.QuadPart + 1)) + 1;
            if (distance > base)
                return STG_E_INVALIDFUNCTION;
            base -= distance;
        } else {
            const auto distance = static_cast<std::uint64_t>(move.QuadPart);
            if (distance > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()) - base)
                return STG_E_INVALIDFUNCTION;
            base += distance;
        }
        position_ = base;
        if (position)
            position->QuadPart = base;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* destination, ULARGE_INTEGER count, ULARGE_INTEGER* read,
                                     ULARGE_INTEGER* written) override {
        if (read)
            read->QuadPart = 0;
        if (written)
            written->QuadPart = 0;
        if (!destination)
            return STG_E_INVALIDPOINTER;
        std::array<std::uint8_t, 64 * 1024> block{};
        std::uint64_t copied = 0;
        while (copied < count.QuadPart) {
            const auto wanted = static_cast<ULONG>(std::min<std::uint64_t>(block.size(), count.QuadPart - copied));
            ULONG received = 0, emitted = 0;
            const auto result = Read(block.data(), wanted, &received);
            if (read)
                read->QuadPart += received;
            if (FAILED(result))
                return result;
            const auto output = destination->Write(block.data(), received, &emitted);
            if (written)
                written->QuadPart += emitted;
            if (FAILED(output))
                return output;
            if (emitted != received)
                return STG_E_WRITEFAULT;
            copied += received;
            if (result == S_FALSE)
                return S_FALSE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* information, DWORD) override {
        if (!information)
            return STG_E_INVALIDPOINTER;
        *information = {};
        information->type = STGTY_STREAM;
        information->cbSize.QuadPart = state_->size;
        information->grfMode = STGM_READ | STGM_SHARE_DENY_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** copy) override {
        if (!copy)
            return STG_E_INVALIDPOINTER;
        const StreamLock guard{state_->lock};
        *copy = new (std::nothrow) PinnedReadStream(state_, position_);
        return *copy ? S_OK : E_OUTOFMEMORY;
    }

  private:
    ~PinnedReadStream() = default;
    std::atomic<ULONG> references_{1};
    std::shared_ptr<StreamState> state_;
    std::uint64_t position_;
};

struct Functions final {
    HMODULE platform{nullptr};
    HMODULE readwrite{nullptr};
    decltype(&MFStartup) startup{};
    decltype(&MFShutdown) shutdown{};
    decltype(&MFCreateMFByteStreamOnStream) byte_stream{};
    decltype(&MFCreateAttributes) attributes{};
    decltype(&MFCreateMediaType) media_type{};
    decltype(&MFCreateSourceReaderFromByteStream) reader{};
    decltype(&MFCreateSample) sample{};
    decltype(&MFCreateMemoryBuffer) buffer{};
    ~Functions() {
        if (readwrite)
            FreeLibrary(readwrite);
        if (platform)
            FreeLibrary(platform);
    }
    template <class Function> static Function load(HMODULE module, const char* name) {
        const auto address = GetProcAddress(module, name);
        Function function{};
        static_assert(sizeof(function) == sizeof(address));
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }
    bool initialize() {
        platform = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        readwrite = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!platform || !readwrite)
            return false;
        startup = load<decltype(startup)>(platform, "MFStartup");
        shutdown = load<decltype(shutdown)>(platform, "MFShutdown");
        byte_stream = load<decltype(byte_stream)>(platform, "MFCreateMFByteStreamOnStream");
        attributes = load<decltype(attributes)>(platform, "MFCreateAttributes");
        media_type = load<decltype(media_type)>(platform, "MFCreateMediaType");
        reader = load<decltype(reader)>(readwrite, "MFCreateSourceReaderFromByteStream");
        sample = load<decltype(sample)>(platform, "MFCreateSample");
        buffer = load<decltype(buffer)>(platform, "MFCreateMemoryBuffer");
        return startup && shutdown && byte_stream && attributes && media_type && reader && sample && buffer;
    }
};

class Runtime final {
  public:
    explicit Runtime(Functions& functions) : functions_(functions) {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        uninitialize_ = SUCCEEDED(initialized);
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
            return;
        started_ = SUCCEEDED(functions_.startup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~Runtime() {
        if (started_)
            functions_.shutdown();
        if (uninitialize_)
            CoUninitialize();
    }
    bool valid() const { return started_; }

  private:
    Functions& functions_;
    bool uninitialize_{false};
    bool started_{false};
};

class LockedBuffer final {
  public:
    explicit LockedBuffer(IMFMediaBuffer* buffer) : buffer_(buffer) {
        valid_ = SUCCEEDED(buffer_->Lock(&data_, nullptr, &size_));
    }
    ~LockedBuffer() {
        if (valid_)
            buffer_->Unlock();
    }
    bool valid() const { return valid_; }
    std::span<const std::uint8_t> bytes() const { return {data_, size_}; }

  private:
    IMFMediaBuffer* buffer_;
    BYTE* data_{nullptr};
    DWORD size_{0};
    bool valid_{false};
};

bool readAt(HANDLE file, const std::uint64_t offset, std::span<std::uint8_t> bytes) {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD read = 0;
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
           ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
}

// The native MP3 reader may conceal an incomplete final frame. Before full
// decoding, require complete Layer III framing rather than trusting suffix/EOF.
Exit strictMp3(HANDLE file, const std::uint64_t size, const std::uint64_t duration_limit) {
    std::uint64_t offset = 0;
    std::array<std::uint8_t, 10> tag{};
    if (size >= tag.size() && readAt(file, 0, tag) && std::memcmp(tag.data(), "ID3", 3) == 0) {
        if (tag[3] < 2 || tag[3] > 4 || tag[4] == 0xff ||
            std::any_of(tag.begin() + 6, tag.end(), [](auto byte) { return (byte & 0x80) != 0; }))
            return Exit::Validation;
        const std::uint64_t length =
            (std::uint64_t{tag[6]} << 21) | (std::uint64_t{tag[7]} << 14) | (std::uint64_t{tag[8]} << 7) | tag[9];
        offset = 10 + length + (tag[3] == 4 && (tag[5] & 0x10) ? 10 : 0);
        if (offset > size)
            return Exit::Validation;
    }
    constexpr std::array<unsigned, 16> bitrate_v1{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    constexpr std::array<unsigned, 16> bitrate_v2{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    constexpr std::array<unsigned, 3> rates{44'100, 48'000, 32'000};
    std::uint64_t frames = 0, samples = 0;
    unsigned first_rate = 0;
    while (offset < size) {
        std::array<std::uint8_t, 4> header{};
        if (size - offset == 128 && readAt(file, offset, header) && std::memcmp(header.data(), "TAG", 3) == 0) {
            offset = size;
            break;
        }
        if (size - offset < header.size() || !readAt(file, offset, header) || header[0] != 0xff ||
            (header[1] & 0xe0) != 0xe0)
            return Exit::Validation;
        const unsigned version = (header[1] >> 3) & 3;
        const unsigned layer = (header[1] >> 1) & 3;
        const unsigned bitrate_index = header[2] >> 4;
        const unsigned rate_index = (header[2] >> 2) & 3;
        if (version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3 ||
            (header[3] & 3) == 2)
            return Exit::Validation;
        const auto rate = rates[rate_index] / (version == 3 ? 1 : version == 2 ? 2 : 4);
        const auto bitrate = version == 3 ? bitrate_v1[bitrate_index] : bitrate_v2[bitrate_index];
        const auto frame_bytes =
            static_cast<std::uint64_t>(version == 3 ? 144'000 : 72'000) * bitrate / rate + ((header[2] >> 1) & 1);
        if (frame_bytes < 4 || frame_bytes > size - offset || (first_rate && first_rate != rate))
            return Exit::Validation;
        first_rate = rate;
        samples += version == 3 ? 1152 : 576;
        if (samples * 1'000'000 > duration_limit * rate)
            return Exit::LimitExceeded;
        ++frames;
        offset += frame_bytes;
    }
    return frames && offset == size ? Exit::Success : Exit::Validation;
}

bool supportedContainer(HANDLE file, const std::uint64_t size) {
    std::array<std::uint8_t, 12> header{};
    if (size < header.size() || !readAt(file, 0, header))
        return false;
    return std::memcmp(header.data(), "ID3", 3) == 0 || (header[0] == 0xff && (header[1] & 0xe0) == 0xe0) ||
           (std::memcmp(header.data(), "RIFF", 4) == 0 && std::memcmp(header.data() + 8, "WAVE", 4) == 0) ||
           std::memcmp(header.data() + 4, "ftyp", 4) == 0;
}

bool recognizedNonMp3Container(HANDLE file, const std::uint64_t size) {
    std::array<std::uint8_t, 12> header{};
    return size >= header.size() && readAt(file, 0, header) &&
           ((std::memcmp(header.data(), "RIFF", 4) == 0 && std::memcmp(header.data() + 8, "WAVE", 4) == 0) ||
            std::memcmp(header.data() + 4, "ftyp", 4) == 0);
}

bool pcmType(IMFMediaType* type, UINT32& rate, UINT32& channels) {
    GUID major{}, subtype{};
    UINT32 bits = 0, alignment = 0;
    return SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) && major == MFMediaType_Audio &&
           SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFAudioFormat_PCM &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) && bits == 16 &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) &&
           (rate == 32'000 || rate == 44'100 || rate == 48'000) &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) && (channels == 1 || channels == 2) &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &alignment)) && alignment == channels * 2;
}

Exit configureEncoder(IMFTransform* encoder, IMFMediaType* input, UINT32 rate, UINT32 channels) {
    for (DWORD index = 0; index < 1024; ++index) {
        ComPtr<IMFMediaType> type;
        const auto status = encoder->GetOutputAvailableType(0, index, &type);
        if (status == MF_E_NO_MORE_TYPES)
            break;
        if (FAILED(status))
            return Exit::Unavailable;
        UINT32 candidate_rate = 0, candidate_channels = 0, bytes_per_second = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &candidate_rate)) && candidate_rate == rate &&
            SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &candidate_channels)) &&
            candidate_channels == channels &&
            SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes_per_second)) &&
            bytes_per_second == (channels == 1 ? 16'000u : 24'000u) &&
            SUCCEEDED(encoder->SetOutputType(0, type.Get(), 0))) {
            if (FAILED(encoder->SetInputType(0, input, 0)) ||
                FAILED(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
                FAILED(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0)))
                return Exit::Conversion;
            return Exit::Success;
        }
    }
    return Exit::Unsupported;
}

Exit drainEncoder(Functions& functions, IMFTransform* encoder, const MediaFoundationWorkerRequest& request,
                  const MediaFoundationCodecCallbacks& callbacks, std::uint64_t& written) {
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(encoder->GetOutputStreamInfo(0, &info)) || info.cbSize > maximum_sample_bytes)
        return Exit::Conversion;
    // One decoded sample is bounded to 1 MiB. This known MPEG-1 encoder cannot
    // legitimately emit thousands of outputs for one such sample or EOF drain.
    for (unsigned iteration = 0; iteration < 4096; ++iteration) {
        ComPtr<IMFSample> provided;
        if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(functions.sample(&provided)) ||
                FAILED(functions.buffer(std::max<DWORD>(info.cbSize, 8192), &buffer)) ||
                FAILED(provided->AddBuffer(buffer.Get())))
                return Exit::Conversion;
        }
        MFT_OUTPUT_DATA_BUFFER output{};
        output.pSample = provided.Get();
        DWORD status = 0;
        const auto result = encoder->ProcessOutput(0, 1, &output, &status);
        if (output.pEvents)
            output.pEvents->Release();
        ComPtr<IMFSample> allocated;
        if (output.pSample && output.pSample != provided.Get())
            allocated.Attach(output.pSample);
        if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
            return Exit::Success;
        if (FAILED(result))
            return Exit::Conversion;
        if ((output.dwStatus & MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE) == MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE)
            continue;
        if (!output.pSample)
            return Exit::Conversion;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(output.pSample->ConvertToContiguousBuffer(&buffer)))
            return Exit::Conversion;
        LockedBuffer locked{buffer.Get()};
        if (!locked.valid() || locked.bytes().size() > maximum_sample_bytes)
            return Exit::Conversion;
        // The ACM wrapper can acknowledge EOF drain with a valid empty sample.
        // Continue only within the same bounded loop until NEED_MORE_INPUT.
        if (locked.bytes().empty())
            continue;
        if (locked.bytes().size() > request.max_output_bytes - written)
            return Exit::LimitExceeded;
        try {
            if (!callbacks.write(locked.bytes()))
                return Exit::Output;
        } catch (...) {
            return Exit::Output;
        }
        written += locked.bytes().size();
    }
    return Exit::Conversion;
}

Exit decode(Functions& functions, IMFSourceReader* reader, IMFMediaType* pcm,
            const MediaFoundationWorkerRequest& request, const MediaFoundationCodecCallbacks& callbacks) {
    UINT32 rate = 0, channels = 0;
    if (!pcmType(pcm, rate, channels))
        return Exit::Unsupported;
    const bool converting = request.mode == MediaFoundationWorkerMode::Convert;
    const auto failed = converting ? Exit::Conversion : Exit::Validation;
    ComPtr<IMFTransform> encoder;
    if (converting) {
        if (FAILED(CoCreateInstance(CLSID_MP3ACMCodecWrapper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&encoder))))
            return Exit::Unavailable;
        const auto configured = configureEncoder(encoder.Get(), pcm, rate, channels);
        if (configured != Exit::Success)
            return configured;
    }
    std::uint64_t decoded_frames = 0, written = 0;
    unsigned empty_reads = 0;
    for (;;) {
        DWORD stream = 0, flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(audio_stream, 0, &stream, &flags, &time, &sample)) ||
            (flags & MF_SOURCE_READERF_ERROR))
            return failed;
        if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
            ComPtr<IMFMediaType> current;
            UINT32 current_rate = 0, current_channels = 0;
            if (FAILED(reader->GetCurrentMediaType(audio_stream, &current)) ||
                !pcmType(current.Get(), current_rate, current_channels) || current_rate != rate ||
                current_channels != channels)
                return failed;
        }
        if (sample) {
            empty_reads = 0;
            DWORD bytes = 0;
            if (FAILED(sample->GetTotalLength(&bytes)) || bytes == 0 || bytes > maximum_sample_bytes ||
                bytes % (channels * 2))
                return failed;
            const auto frames = bytes / (channels * 2);
            const auto maximum_frames = request.max_duration_us * rate / 1'000'000;
            if (frames > maximum_frames - decoded_frames)
                return Exit::LimitExceeded;
            const auto before = decoded_frames;
            decoded_frames += frames;
            if (converting) {
                if (FAILED(sample->SetSampleTime(static_cast<LONGLONG>(before * 10'000'000 / rate))) ||
                    FAILED(sample->SetSampleDuration(static_cast<LONGLONG>(frames * std::uint64_t{10'000'000} / rate))))
                    return failed;
                auto status = encoder->ProcessInput(0, sample.Get(), 0);
                if (status == MF_E_NOTACCEPTING) {
                    const auto drained = drainEncoder(functions, encoder.Get(), request, callbacks, written);
                    if (drained != Exit::Success)
                        return drained;
                    status = encoder->ProcessInput(0, sample.Get(), 0);
                }
                if (FAILED(status))
                    return failed;
                const auto drained = drainEncoder(functions, encoder.Get(), request, callbacks, written);
                if (drained != Exit::Success)
                    return drained;
            }
            try {
                if (callbacks.progress && !callbacks.progress(decoded_frames * 1'000'000 / rate))
                    return Exit::Output;
            } catch (...) {
                return Exit::Output;
            }
        } else if (++empty_reads > 256) {
            return failed;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
    }
    if (!decoded_frames)
        return converting ? Exit::NoAudio : Exit::Validation;
    if (converting) {
        if (FAILED(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0)) ||
            FAILED(encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0)))
            return failed;
        const auto drained = drainEncoder(functions, encoder.Get(), request, callbacks, written);
        if (drained != Exit::Success)
            return drained;
        if (!written)
            return failed;
        static_cast<void>(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0));
    }
    return Exit::Success;
}
} // namespace

MediaFoundationWorkerExit runWindowsMediaFoundationCodec(const MediaFoundationWorkerRequest& request,
                                                         const MediaFoundationCodecCallbacks& callbacks) {
    if (!validMediaFoundationWorkerRequest(request) ||
        (request.mode == MediaFoundationWorkerMode::Convert && !callbacks.write))
        return Exit::InvalidRequest;
    try {
        static_assert(sizeof(wchar_t) == sizeof(char16_t));
        std::wstring path{request.input_path.begin(), request.input_path.end()};
        std::replace(path.begin(), path.end(), L'/', L'\\');
        const auto native = std::wstring{L"\\\\?\\"} + path;
        const Handle input{CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                                       nullptr)};
        BY_HANDLE_FILE_INFORMATION information{};
        LARGE_INTEGER size{};
        if (!input.valid() || GetFileType(input.get()) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(input.get(), &information) ||
            (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !GetFileSizeEx(input.get(), &size) || size.QuadPart <= 0)
            return Exit::Input;
        const auto bytes = static_cast<std::uint64_t>(size.QuadPart);
        if (bytes > request.max_input_bytes)
            return Exit::LimitExceeded;
        const bool validating = request.mode == MediaFoundationWorkerMode::Validate;
        if (validating) {
            if (recognizedNonMp3Container(input.get(), bytes))
                return Exit::Unsupported;
            const auto inspected = strictMp3(input.get(), bytes, request.max_duration_us);
            if (inspected != Exit::Success)
                return inspected;
        } else if (!supportedContainer(input.get(), bytes)) {
            return Exit::Unsupported;
        }
        Functions functions;
        if (!functions.initialize())
            return Exit::Unavailable;
        Runtime runtime{functions};
        if (!runtime.valid())
            return Exit::Unavailable;
        HANDLE duplicate = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), input.get(), GetCurrentProcess(), &duplicate, 0, FALSE,
                             DUPLICATE_SAME_ACCESS))
            return Exit::Input;
        Handle owned{duplicate};
        auto state = std::make_shared<StreamState>(std::move(owned), bytes);
        ComPtr<IStream> local;
        local.Attach(new (std::nothrow) PinnedReadStream(std::move(state)));
        if (!local)
            return Exit::Unavailable;
        ComPtr<IMFByteStream> stream;
        // No filename/URL reaches the MF bridge or source reader. The duplicated
        // read capability survives asynchronous work without reopening the file.
        if (FAILED(functions.byte_stream(local.Get(), &stream)))
            return Exit::Input;
        // A byte-stream handler receives local bytes, not an origin URL to
        // reopen with incompatible sharing or interpret as another resource.
        ComPtr<IMFAttributes> stream_attributes;
        const auto stream_metadata = stream.As(&stream_attributes);
        if (stream_metadata != E_NOINTERFACE &&
            (FAILED(stream_metadata) || FAILED(stream_attributes->DeleteItem(MF_BYTESTREAM_ORIGIN_NAME))))
            return Exit::Unavailable;
        ComPtr<IMFAttributes> attributes;
        if (FAILED(functions.attributes(&attributes, 2)) ||
            FAILED(attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE)) ||
            FAILED(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE)))
            return Exit::Unavailable;
        ComPtr<IMFSourceReader> reader;
        if (FAILED(functions.reader(stream.Get(), attributes.Get(), &reader)))
            return validating ? Exit::Validation : Exit::Unsupported;
        ComPtr<IMFMediaType> source;
        const auto native_type = reader->GetNativeMediaType(audio_stream, 0, &source);
        if (native_type == MF_E_INVALIDSTREAMNUMBER || native_type == MF_E_NO_MORE_TYPES)
            return validating ? Exit::Validation : Exit::NoAudio;
        if (FAILED(native_type))
            return validating ? Exit::Validation : Exit::Unsupported;
        GUID subtype{};
        if (validating && (FAILED(source->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFAudioFormat_MP3))
            return Exit::Validation;
        UINT32 native_rate = 0, native_channels = 0;
        if (FAILED(source->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &native_rate)) ||
            FAILED(source->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &native_channels)) ||
            (native_channels != 1 && native_channels != 2))
            return Exit::Unsupported;
        const auto decoded_rate =
            native_rate == 32'000 || native_rate == 44'100 || native_rate == 48'000 ? native_rate : 44'100;
        ComPtr<IMFMediaType> requested;
        if (FAILED(functions.media_type(&requested)) ||
            FAILED(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
            FAILED(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
            FAILED(requested->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
            FAILED(requested->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, decoded_rate)) ||
            FAILED(requested->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, native_channels)) ||
            FAILED(requested->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, native_channels * 2)) ||
            FAILED(requested->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, decoded_rate * native_channels * 2)) ||
            FAILED(reader->SetStreamSelection(all_streams, FALSE)) ||
            FAILED(reader->SetStreamSelection(audio_stream, TRUE)) ||
            FAILED(reader->SetCurrentMediaType(audio_stream, nullptr, requested.Get())))
            return validating ? Exit::Validation : Exit::Unsupported;
        ComPtr<IMFMediaType> pcm;
        if (FAILED(reader->GetCurrentMediaType(audio_stream, &pcm)))
            return validating ? Exit::Validation : Exit::Unsupported;
        return decode(functions, reader.Get(), pcm.Get(), request, callbacks);
    } catch (...) {
        return request.mode == MediaFoundationWorkerMode::Validate ? Exit::Validation : Exit::Conversion;
    }
}
} // namespace music_player::m0

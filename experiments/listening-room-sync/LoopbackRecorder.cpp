// Spike descartável: gravação WASAPI loopback (AUDCLNT_STREAMFLAGS_LOOPBACK) do dispositivo padrão.
#include "LoopbackRecorder.h"

#define NOMINMAX
#include <windows.h>

// A ordem importa: mmdeviceapi.h define DEFINE_PROPERTYKEY para o header de chaves.
// clang-format off
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
// clang-format on

#include <algorithm>
#include <cstring>
#include <sstream>

namespace {

double qpcFrequency() {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return frequency;
}

std::string narrow(const wchar_t* text) {
    if (text == nullptr) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 1)) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

template <typename T> void release(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

enum class SampleKind { Float32, Int16, Int24, Int32, Unknown };

} // namespace

double qpcNowMs() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * 1000.0 / qpcFrequency();
}

double Capture::timeOfSample(std::size_t i) const {
    if (chunks.empty()) {
        return 0.0;
    }
    auto it = std::upper_bound(chunks.begin(), chunks.end(), i,
                               [](std::size_t value, const CaptureChunk& c) { return value < c.firstSample; });
    if (it != chunks.begin()) {
        --it;
    }
    return it->qpcMs + static_cast<double>(i - it->firstSample) * 1000.0 / sampleRate;
}

LoopbackRecorder::~LoopbackRecorder() { stop(); }

bool LoopbackRecorder::start(std::string* error) {
    m_running = true;
    m_thread = std::thread([this] { run(); });
    while (!m_ready && !m_failed) {
        Sleep(5);
    }
    if (m_failed) {
        if (error != nullptr) {
            *error = m_error;
        }
        stop();
        return false;
    }
    return true;
}

void LoopbackRecorder::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void LoopbackRecorder::beginSegment() {
    std::lock_guard lock(m_mutex);
    const int rate = m_segment.sampleRate;
    m_segment = Capture{};
    m_segment.sampleRate = rate;
}

Capture LoopbackRecorder::takeSegment() {
    std::lock_guard lock(m_mutex);
    Capture out = std::move(m_segment);
    m_segment = Capture{};
    m_segment.sampleRate = out.sampleRate;
    return out;
}

void LoopbackRecorder::run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;

    auto fail = [&](const char* what, HRESULT hr) {
        std::ostringstream s;
        s << what << " falhou (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ")";
        m_error = s.str();
        m_failed = true;
    };

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        fail("CoCreateInstance(MMDeviceEnumerator)", hr);
    } else if (FAILED(hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        fail("GetDefaultAudioEndpoint(eRender) - sem dispositivo de saida padrao?", hr);
    } else if (FAILED(hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                            reinterpret_cast<void**>(&client)))) {
        fail("IMMDevice::Activate(IAudioClient)", hr);
    } else if (FAILED(hr = client->GetMixFormat(&format))) {
        fail("GetMixFormat", hr);
    }

    SampleKind kind = SampleKind::Unknown;
    if (!m_failed) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id))) {
            m_deviceId = narrow(id);
            CoTaskMemFree(id);
        }
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
                m_deviceName = narrow(name.pwszVal);
            }
            PropVariantClear(&name);
            props->Release();
        }

        bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        bool isPcm = format->wFormatTag == WAVE_FORMAT_PCM;
        int validBits = format->wBitsPerSample;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
            isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
            isPcm = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != 0;
            validBits = ext->Samples.wValidBitsPerSample;
        }
        if (isFloat && format->wBitsPerSample == 32) {
            kind = SampleKind::Float32;
        } else if (isPcm && format->wBitsPerSample == 16) {
            kind = SampleKind::Int16;
        } else if (isPcm && format->wBitsPerSample == 24) {
            kind = SampleKind::Int24;
        } else if (isPcm && format->wBitsPerSample == 32) {
            kind = SampleKind::Int32;
        }
        std::ostringstream s;
        s << format->nSamplesPerSec << " Hz, " << format->nChannels << " canais, " << format->wBitsPerSample
          << " bits (" << validBits << " validos), " << (isFloat ? "float" : (isPcm ? "PCM" : "outro"));
        m_format = s.str();
        if (kind == SampleKind::Unknown || format->nChannels < 2) {
            m_error = "formato de mixagem nao suportado: " + m_format;
            m_failed = true;
        }
    }

    if (!m_failed) {
        const REFERENCE_TIME bufferDuration = 2'000'000; // 200 ms
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, bufferDuration, 0, format,
                                nullptr);
        if (FAILED(hr)) {
            fail("IAudioClient::Initialize(LOOPBACK)", hr);
        } else if (FAILED(hr = client->GetService(__uuidof(IAudioCaptureClient),
                                                  reinterpret_cast<void**>(&capture)))) {
            fail("GetService(IAudioCaptureClient)", hr);
        } else if (FAILED(hr = client->Start())) {
            fail("IAudioClient::Start", hr);
        }
    }

    if (!m_failed) {
        {
            std::lock_guard lock(m_mutex);
            m_segment.sampleRate = static_cast<int>(format->nSamplesPerSec);
        }
        m_ready = true;
        const int channels = format->nChannels;
        const int bytesPerSample = format->wBitsPerSample / 8;
        while (m_running) {
            UINT32 packet = 0;
            while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, &devicePosition, &qpcPosition))) {
                    break;
                }
                {
                    std::lock_guard lock(m_mutex);
                    CaptureChunk chunk;
                    chunk.firstSample = m_segment.left.size();
                    chunk.qpcMs = static_cast<double>(qpcPosition) / 10'000.0; // 100 ns -> ms
                    chunk.silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    chunk.discontinuity = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
                    if (chunk.discontinuity) {
                        ++m_discontinuities;
                    }
                    m_segment.chunks.push_back(chunk);
                    for (UINT32 f = 0; f < frames; ++f) {
                        float lr[2] = {0.0f, 0.0f};
                        if (!chunk.silent) {
                            for (int c = 0; c < 2; ++c) {
                                const BYTE* p = data + (static_cast<std::size_t>(f) * channels + c) * bytesPerSample;
                                switch (kind) {
                                case SampleKind::Float32:
                                    std::memcpy(&lr[c], p, 4);
                                    break;
                                case SampleKind::Int16: {
                                    int16_t v;
                                    std::memcpy(&v, p, 2);
                                    lr[c] = v / 32768.0f;
                                    break;
                                }
                                case SampleKind::Int24: {
                                    const int32_t v = (static_cast<int32_t>(p[0]) << 8) |
                                                      (static_cast<int32_t>(p[1]) << 16) |
                                                      (static_cast<int32_t>(p[2]) << 24);
                                    lr[c] = static_cast<float>(v / 2147483648.0);
                                    break;
                                }
                                case SampleKind::Int32: {
                                    int32_t v;
                                    std::memcpy(&v, p, 4);
                                    lr[c] = static_cast<float>(v / 2147483648.0);
                                    break;
                                }
                                case SampleKind::Unknown:
                                    break;
                                }
                            }
                        }
                        m_segment.left.push_back(lr[0]);
                        m_segment.right.push_back(lr[1]);
                    }
                }
                capture->ReleaseBuffer(frames);
            }
            Sleep(2);
        }
        client->Stop();
    }

    release(capture);
    release(client);
    release(device);
    release(enumerator);
    if (format != nullptr) {
        CoTaskMemFree(format);
    }
    CoUninitialize();
}

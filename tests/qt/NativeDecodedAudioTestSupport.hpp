#pragma once

// Independent test-only MF reader. It observes the recovered artifact samples,
// not the converter's progress or its edit/window implementation. Only fresh
// fixture paths are supplied; this filename API is never linked into the worker.
#include <QScopeGuard>
#include <QString>
#include <qt_windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

namespace native_audio_test {
struct Pcm final {
    UINT32 rate{0};
    UINT32 channels{0};
    std::vector<std::int16_t> samples;
    bool valid{false};
};
inline Pcm decode(const QString& path) {
    Pcm result;
    const auto platform = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto readwrite = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto unload = qScopeGuard([&] {
        if (readwrite)
            FreeLibrary(readwrite);
        if (platform)
            FreeLibrary(platform);
    });
    if (!platform || !readwrite)
        return result;
    const auto startup = reinterpret_cast<HRESULT(WINAPI*)(ULONG, DWORD)>(GetProcAddress(platform, "MFStartup"));
    const auto shutdown = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(platform, "MFShutdown"));
    const auto media_type =
        reinterpret_cast<HRESULT(WINAPI*)(IMFMediaType**)>(GetProcAddress(platform, "MFCreateMediaType"));
    const auto reader_from_url = reinterpret_cast<HRESULT(WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**)>(
        GetProcAddress(readwrite, "MFCreateSourceReaderFromURL"));
    if (!startup || !shutdown || !media_type || !reader_from_url)
        return result;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return result;
    const auto uninitialize = qScopeGuard([&] {
        if (SUCCEEDED(initialized))
            CoUninitialize();
    });
    if (FAILED(startup(MF_VERSION, MFSTARTUP_LITE)))
        return result;
    const auto stop = qScopeGuard([&] { static_cast<void>(shutdown()); });
    using Microsoft::WRL::ComPtr;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> source, requested, current;
    const auto wide = path.toStdWString();
    constexpr DWORD audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    constexpr DWORD all = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
    if (FAILED(reader_from_url(wide.c_str(), nullptr, &reader)) ||
        FAILED(reader->GetNativeMediaType(audio, 0, &source)) ||
        FAILED(source->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &result.rate)) ||
        FAILED(source->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &result.channels)) ||
        (result.rate != 48000 && result.rate != 44100 && result.rate != 32000) ||
        (result.channels != 1 && result.channels != 2) || FAILED(media_type(&requested)) ||
        FAILED(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(requested->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
        FAILED(requested->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, result.rate)) ||
        FAILED(requested->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, result.channels)) ||
        FAILED(requested->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, result.channels * 2)) ||
        FAILED(requested->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, result.rate * result.channels * 2)) ||
        FAILED(reader->SetStreamSelection(all, FALSE)) || FAILED(reader->SetStreamSelection(audio, TRUE)) ||
        FAILED(reader->SetCurrentMediaType(audio, nullptr, requested.Get())) ||
        FAILED(reader->GetCurrentMediaType(audio, &current)))
        return result;
    UINT32 bits = 0;
    if (FAILED(current->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) || bits != 16)
        return result;
    for (unsigned reads = 0; reads < 4096; ++reads) {
        DWORD stream = 0, flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(audio, 0, &stream, &flags, &timestamp, &sample)) ||
            (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED |
                      MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)))
            return result;
        if (sample) {
            ComPtr<IMFMediaBuffer> buffer;
            BYTE* bytes = nullptr;
            DWORD size = 0;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&bytes, nullptr, &size)))
                return result;
            const auto unlock = qScopeGuard([&] { static_cast<void>(buffer->Unlock()); });
            if (size % (result.channels * 2) || size > 1024 * 1024 || result.samples.size() + size / 2 > 10 * 48000 * 2)
                return result;
            const auto old = result.samples.size();
            result.samples.resize(old + size / 2);
            if (size)
                std::memcpy(result.samples.data() + old, bytes, size);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            result.valid = !result.samples.empty();
            return result;
        }
    }
    return result;
}
inline double rms(const Pcm& pcm, std::size_t begin, std::size_t end) {
    if (!pcm.valid || pcm.channels != 1 || begin >= end || end > pcm.samples.size())
        return -1;
    double power = 0;
    for (auto i = begin; i < end; ++i) {
        const auto value = static_cast<double>(pcm.samples[i]);
        power += value * value;
    }
    return std::sqrt(power / static_cast<double>(end - begin));
}
inline double tonePower(const Pcm& pcm, double frequency, std::size_t begin, std::size_t end) {
    if (!pcm.valid || pcm.channels != 1 || begin >= end || end > pcm.samples.size())
        return -1;
    double real = 0, imaginary = 0;
    for (auto i = begin; i < end; ++i) {
        const auto phase = 2 * std::numbers::pi * frequency * static_cast<double>(i - begin) / pcm.rate;
        real += pcm.samples[i] * std::cos(phase);
        imaginary += pcm.samples[i] * std::sin(phase);
    }
    const auto n = static_cast<double>(end - begin);
    return (real * real + imaginary * imaginary) / (n * n);
}
} // namespace native_audio_test

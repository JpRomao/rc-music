// Spike descartável: gravação WASAPI loopback do dispositivo de saída padrão.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Relógio comum: QueryPerformanceCounter em milissegundos (mesma base do qpcPosition do WASAPI).
double qpcNowMs();

struct CaptureChunk {
    std::size_t firstSample = 0; // índice da primeira amostra do pacote em left/right
    double qpcMs = 0.0;          // instante (QPC) da primeira amostra do pacote
    bool silent = false;
    bool discontinuity = false;
};

struct Capture {
    int sampleRate = 0;
    std::vector<float> left;
    std::vector<float> right;
    std::vector<CaptureChunk> chunks;

    // Instante QPC (ms) da amostra i, a partir do pacote que a contém.
    double timeOfSample(std::size_t i) const;
};

class LoopbackRecorder {
  public:
    LoopbackRecorder() = default;
    ~LoopbackRecorder();
    LoopbackRecorder(const LoopbackRecorder&) = delete;
    LoopbackRecorder& operator=(const LoopbackRecorder&) = delete;

    // Abre o endpoint de renderização padrão (eConsole) em loopback e começa a capturar.
    bool start(std::string* error);
    void stop();

    // Descarta o que foi capturado até agora e começa um novo trecho.
    void beginSegment();
    // Devolve o trecho capturado desde beginSegment().
    Capture takeSegment();

    std::string deviceId() const { return m_deviceId; }
    std::string deviceName() const { return m_deviceName; }
    std::string formatDescription() const { return m_format; }
    std::size_t discontinuities() const { return m_discontinuities.load(); }

  private:
    void run();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_failed{false};
    std::atomic<std::size_t> m_discontinuities{0};
    std::string m_error;
    std::string m_deviceId;
    std::string m_deviceName;
    std::string m_format;

    std::mutex m_mutex;
    Capture m_segment;
};

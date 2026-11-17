// Spike descartável S1.1: mede a precisão de início, seek e deriva de dois QMediaPlayer no mesmo PC.
//
// O player A toca clicks-left.mp3 (clique só no canal esquerdo) e o B toca clicks-right.mp3 (só no
// direito), os dois no dispositivo de saída padrão. A gravação WASAPI loopback mostra o que chegou à
// mixagem do sistema. Para cada clique, o erro de linha do tempo do player é
//     e = conteúdo do clique - (P + (t - T)),
// em que T é o instante agendado, P a posição inicial e t o instante do clique na gravação (QPC).
// A dessincronia real é eA - eB (positivo: A adiantado). O índice do clique vem de position(),
// amostrado a cada 10 ms.
#include "LoopbackRecorder.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <tuple>
#include <vector>

namespace {

constexpr double kClickOffsetMs = 500.0; // cliques em 500 + 1000k ms do arquivo
constexpr double kClickPeriodMs = 1000.0;

struct Options {
    QString mediaDir;
    QString outDir;
    QStringList scenarios;
    int runs = 20;
    int driftRuns = 3;
    double volume = 0.5;
    double nudgeMs = 20.0;
    double seekMs = 200.0;
    double seekLeadMs = 0.0;
    double nudgeRate = 0.01;
    double thresholdOverride = 0.0;
    std::optional<bool> pitchCompensation; // vazio: padrão do Qt
};

Options g_opt;
LoopbackRecorder* g_rec = nullptr;
float g_thrL = 0.05f;
float g_thrR = 0.05f;

// ---------------------------------------------------------------------------------------------
// Utilidades de tempo e estatística

void waitUntil(double tMs) {
    const double d = tMs - qpcNowMs();
    if (d <= 0.0) {
        QCoreApplication::processEvents();
        return;
    }
    QEventLoop loop;
    QTimer::singleShot(static_cast<int>(std::ceil(d)), Qt::PreciseTimer, &loop, &QEventLoop::quit);
    loop.exec();
}

void waitMs(double ms) { waitUntil(qpcNowMs() + ms); }

bool waitFor(const std::function<bool()>& cond, int timeoutMs) {
    const double end = qpcNowMs() + timeoutMs;
    while (!cond()) {
        if (qpcNowMs() > end) {
            return false;
        }
        waitMs(5);
    }
    return true;
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return std::nan("");
    }
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Percentil por posto mais próximo (nearest-rank).
double percentile(std::vector<double> v, double p) {
    if (v.empty()) {
        return std::nan("");
    }
    std::sort(v.begin(), v.end());
    const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * v.size()));
    return v[std::clamp<std::size_t>(rank, 1, v.size()) - 1];
}

// Inclinação (por minuto) de y em função de x (ms), mínimos quadrados.
double slopePerMinute(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() < 2) {
        return std::nan("");
    }
    const double mx = std::accumulate(x.begin(), x.end(), 0.0) / x.size();
    const double my = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        num += (x[i] - mx) * (y[i] - my);
        den += (x[i] - mx) * (x[i] - mx);
    }
    return den > 0.0 ? num / den * 60'000.0 : std::nan("");
}

// ---------------------------------------------------------------------------------------------
// Players, amostragem de position() e correção

struct Player {
    QMediaPlayer* mp = nullptr;
    QAudioOutput* out = nullptr;
    QString name;
    // Cada positionChanged: (instante QPC, posição, velocidade). É a leitura mais fresca de position().
    std::vector<std::tuple<double, qint64, double>> changes;
    double cooldownUntil = 0.0;
    int mode = 0; // 0: 1.0x, +1: acelerando, -1: freando
    int seeks = 0;
    int nudges = 0;

    ~Player() {
        if (mp != nullptr) {
            mp->stop();
            delete mp;
        }
        delete out;
    }
};

struct PosSample {
    double t;
    qint64 posA;
    qint64 posB;
    double rateA;
    double rateB;
};

struct Event {
    double t;
    QString what;
};

struct Run {
    QString scenario;
    int index = 0;
    std::unique_ptr<Player> a;
    std::unique_ptr<Player> b;
    double T = 0.0;  // instante agendado (QPC ms)
    double P = 0.0;  // posição do arquivo em T
    bool correcting = false;
    std::vector<PosSample> pos;
    std::vector<Event> events;
    std::vector<std::tuple<double, int, double>> estimates; // (t, player 0/1, erro estimado por position())

    double ideal(double t) const { return P + (t - T); }
    void event(const QString& what) { events.push_back({qpcNowMs(), what}); }
};

Run* g_run = nullptr;

std::unique_ptr<Player> makePlayer(const QString& file, const QString& name,
                                   std::optional<bool> pitchCompensation = std::nullopt) {
    auto p = std::make_unique<Player>();
    p->name = name;
    p->mp = new QMediaPlayer;
    p->out = new QAudioOutput(QMediaDevices::defaultAudioOutput());
    p->out->setVolume(static_cast<float>(g_opt.volume));
    p->mp->setAudioOutput(p->out);
    if (!pitchCompensation) {
        pitchCompensation = g_opt.pitchCompensation;
    }
    if (pitchCompensation) {
        p->mp->setPitchCompensation(*pitchCompensation);
    }
    Player* raw = p.get();
    QObject::connect(p->mp, &QMediaPlayer::positionChanged, p->mp, [raw](qint64 pos) {
        raw->changes.emplace_back(qpcNowMs(), pos, raw->mp->playbackRate());
    });
    QObject::connect(p->mp, &QMediaPlayer::errorOccurred, p->mp, [name](QMediaPlayer::Error, const QString& s) {
        std::fprintf(stderr, "player %s erro: %s\n", qPrintable(name), qPrintable(s));
    });
    p->mp->setSource(QUrl::fromLocalFile(QDir(g_opt.mediaDir).filePath(file)));
    return p;
}

bool waitLoaded(Player& p) {
    return waitFor([&] { return p.mp->mediaStatus() == QMediaPlayer::LoadedMedia; }, 5000);
}

void samplePositions() {
    if (g_run == nullptr) {
        return;
    }
    PosSample s{qpcNowMs(), -1, -1, 0.0, 0.0};
    if (g_run->a) {
        s.posA = g_run->a->mp->position();
        s.rateA = g_run->a->mp->playbackRate();
    }
    if (g_run->b) {
        s.posB = g_run->b->mp->position();
        s.rateB = g_run->b->mp->playbackRate();
    }
    g_run->pos.push_back(s);
}

// Correção contra a linha do tempo ideal, só com o que o app vê: position() e o relógio.
// position() muda em degraus; por isso o erro vem de cada positionChanged, no instante em que o
// valor é novo: mediana de (posição - ideal) nos últimos 500 ms, com pelo menos 3 leituras.
void correctPlayer(Run& run, Player& p) {
    if (p.mp->playbackState() != QMediaPlayer::PlayingState) {
        return;
    }
    const double now = qpcNowMs();
    if (now < p.cooldownUntil) {
        return;
    }
    std::vector<double> errs;
    for (auto it = p.changes.rbegin(); it != p.changes.rend(); ++it) {
        const auto& [t, pos, rate] = *it;
        if (t < now - 500.0 || t < p.cooldownUntil) {
            break;
        }
        errs.push_back(static_cast<double>(pos) - run.ideal(t));
    }
    if (errs.size() < 3) {
        return;
    }
    const double e = median(errs);
    run.estimates.emplace_back(now, &p == run.a.get() ? 0 : 1, e);
    const double expected = run.ideal(now);
    if (std::abs(e) > g_opt.seekMs) {
        p.mp->setPlaybackRate(1.0);
        p.mode = 0;
        p.mp->setPosition(static_cast<qint64>(std::llround(expected + g_opt.seekLeadMs)));
        p.cooldownUntil = now + 1000.0;
        ++p.seeks;
        run.event(QStringLiteral("seek %1 err=%2").arg(p.name).arg(e, 0, 'f', 1));
    } else if (p.mode == 0 && std::abs(e) > g_opt.nudgeMs) {
        p.mode = e > 0 ? -1 : +1;
        p.mp->setPlaybackRate(1.0 + p.mode * g_opt.nudgeRate);
        ++p.nudges;
        run.event(QStringLiteral("nudge %1 %2 err=%3").arg(p.name).arg(p.mode).arg(e, 0, 'f', 1));
    } else if (p.mode != 0 && (std::abs(e) < g_opt.nudgeMs / 2.0 || (e > 0) == (p.mode > 0))) {
        // Para ao chegar à metade do limiar ou ao cruzar o zero.
        p.mode = 0;
        p.mp->setPlaybackRate(1.0);
        run.event(QStringLiteral("nudge-end %1 err=%2").arg(p.name).arg(e, 0, 'f', 1));
    }
}

void correctionTick() {
    if (g_run == nullptr || !g_run->correcting) {
        return;
    }
    if (g_run->a) {
        correctPlayer(*g_run, *g_run->a);
    }
    if (g_run->b) {
        correctPlayer(*g_run, *g_run->b);
    }
}

// ---------------------------------------------------------------------------------------------
// Detecção de cliques na gravação

struct Onset {
    double t;
    std::size_t idx;
    float peak; // pontuação do filtro casado
    bool valid = false;
};

// Filtro casado para o pulso retangular de 1 ms: segunda diferença (passa-altas) na borda de subida
// menos a mesma 1 ms depois (borda de descida). Música e outros sons quase não têm essa forma; os
// candidatos ainda precisam cair numa grade de 1 s com outro candidato do mesmo canal.
std::vector<float> clickScore(const std::vector<float>& x, int sampleRate) {
    const std::size_t lag = static_cast<std::size_t>(sampleRate / 1000);
    std::vector<float> h(x.size(), 0.0f);
    for (std::size_t i = 2; i < x.size(); ++i) {
        h[i] = x[i] - 2.0f * x[i - 1] + x[i - 2];
    }
    std::vector<float> m(x.size(), 0.0f);
    for (std::size_t i = 0; i + lag < x.size(); ++i) {
        m[i] = h[i] - h[i + lag];
    }
    return m;
}

std::vector<Onset> detectOnsets(const std::vector<float>& x, const Capture& cap, float thr) {
    std::vector<Onset> out;
    const std::vector<float> m = clickScore(x, cap.sampleRate);
    const std::size_t span = static_cast<std::size_t>(cap.sampleRate / 200); // 5 ms
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (std::abs(m[i]) < thr) {
            continue;
        }
        std::size_t best = i;
        for (std::size_t j = i; j < std::min(m.size(), i + span); ++j) {
            if (std::abs(m[j]) > std::abs(m[best])) {
                best = j;
            }
        }
        out.push_back({cap.timeOfSample(best), best, std::abs(m[best])});
        i = best + span;
    }
    // Só vale o que cai na grade de 1 s com outro candidato (12 ms de tolerância: ±1% de velocidade) e
    // está isolado: outro candidato a menos de 30 ms no mesmo canal indica rajada de outro áudio do
    // sistema, não o clique de 1 ms.
    for (auto& o : out) {
        bool isolated = true;
        for (const auto& other : out) {
            const double d = std::abs(other.t - o.t);
            if (&other != &o && d < 30.0) {
                isolated = false;
            }
            for (int k = 1; k <= 3 && !o.valid; ++k) {
                o.valid = std::abs(d - k * kClickPeriodMs) <= 12.0;
            }
        }
        o.valid = o.valid && isolated;
    }
    return out;
}

struct ClickError {
    double t;    // instante do clique na gravação (QPC ms)
    double e;    // conteúdo - ideal(t)
    double lead; // position() naquele instante - conteúdo do clique
};

std::optional<qint64> positionAt(const Run& run, double t, bool playerA) {
    const PosSample* best = nullptr;
    for (const auto& s : run.pos) {
        if (s.t > t) {
            break;
        }
        best = &s;
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    const qint64 p = playerA ? best->posA : best->posB;
    return p < 0 ? std::nullopt : std::optional<qint64>(p);
}

std::vector<ClickError> clickErrors(const Run& run, const std::vector<Onset>& onsets, bool playerA) {
    // Um clique por índice k: se dois candidatos válidos caem no mesmo k, fica o de maior pontuação.
    std::map<long long, std::pair<float, ClickError>> byIndex;
    for (const auto& o : onsets) {
        if (!o.valid) {
            continue;
        }
        const auto pos = positionAt(run, o.t, playerA);
        if (!pos) {
            continue;
        }
        const double k = std::round((static_cast<double>(*pos) - kClickOffsetMs) / kClickPeriodMs);
        const double content = kClickOffsetMs + k * kClickPeriodMs;
        const ClickError c{o.t, content - run.ideal(o.t), static_cast<double>(*pos) - content};
        auto [it, inserted] = byIndex.try_emplace(static_cast<long long>(k), o.peak, c);
        if (!inserted && o.peak > it->second.first) {
            it->second = {o.peak, c};
        }
    }
    std::vector<ClickError> out;
    for (const auto& [k, v] : byIndex) {
        out.push_back(v.second);
    }
    std::sort(out.begin(), out.end(), [](const ClickError& a, const ClickError& b) { return a.t < b.t; });
    return out;
}

struct Window {
    double eA = std::nan("");
    double eB = std::nan("");
    double desync = std::nan("");
    double leadA = std::nan("");
    double leadB = std::nan("");
    int clicksA = 0;
    int clicksB = 0;
    bool ok() const { return clicksA >= 2 && clicksB >= 2; }
};

Window measure(const std::vector<ClickError>& ea, const std::vector<ClickError>& eb, double t0, double t1) {
    Window w;
    std::vector<double> a, b, la, lb;
    for (const auto& c : ea) {
        if (c.t >= t0 && c.t <= t1) {
            a.push_back(c.e);
            la.push_back(c.lead);
        }
    }
    for (const auto& c : eb) {
        if (c.t >= t0 && c.t <= t1) {
            b.push_back(c.e);
            lb.push_back(c.lead);
        }
    }
    w.clicksA = static_cast<int>(a.size());
    w.clicksB = static_cast<int>(b.size());
    w.eA = median(a);
    w.eB = median(b);
    w.leadA = median(la);
    w.leadB = median(lb);
    w.desync = w.eA - w.eB;
    return w;
}

// ---------------------------------------------------------------------------------------------
// Saída

struct Results {
    std::map<QString, std::vector<double>> metrics;
    QFile csv;
    QTextStream* out = nullptr;
    std::vector<QString> notes;
    int excludedRuns = 0;
    std::size_t discardedOnsets = 0;
    std::size_t totalOnsets = 0;
    std::vector<double> updateIntervals; // ms entre mudanças de position()
    std::vector<double> updateSteps;     // ms de cada mudança
    std::vector<double> signalIntervals; // ms entre positionChanged
    std::vector<double> signalSteps;     // ms entre valores de positionChanged
    std::vector<double> estimatorNoise;  // |estimativa - mediana| na janela final de (b) e (c)

    void add(const Run& run, const QString& metric, double value) {
        metrics[run.scenario + "." + metric].push_back(value);
        *out << run.scenario << ',' << run.index << ',' << metric << ',' << QString::number(value, 'f', 3) << '\n';
        out->flush();
    }
    void note(const QString& s) {
        notes.push_back(s);
        std::printf("  ! %s\n", qPrintable(s));
        *out << "#," << s << '\n';
        out->flush();
    }
};

Results g_res;

void writeWav(const QString& path, const Capture& cap) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    const quint32 frames = static_cast<quint32>(cap.left.size());
    const quint32 dataBytes = frames * 4;
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF");
    u32(36 + dataBytes);
    f.write("WAVEfmt ");
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<quint32>(cap.sampleRate));
    u32(static_cast<quint32>(cap.sampleRate) * 4);
    u16(4);
    u16(16);
    f.write("data");
    u32(dataBytes);
    std::vector<qint16> buf;
    buf.reserve(cap.left.size() * 2);
    for (std::size_t i = 0; i < cap.left.size(); ++i) {
        buf.push_back(static_cast<qint16>(std::clamp(cap.left[i], -1.0f, 1.0f) * 32767));
        buf.push_back(static_cast<qint16>(std::clamp(cap.right[i], -1.0f, 1.0f) * 32767));
    }
    f.write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size() * 2));
}

void collectGranularity(const Run& run) {
    for (const Player* p : {run.a.get(), run.b.get()}) {
        if (p == nullptr) {
            continue;
        }
        for (std::size_t i = 1; i < p->changes.size(); ++i) {
            const auto& [t0, v0, r0] = p->changes[i - 1];
            const auto& [t1, v1, r1] = p->changes[i];
            if (r0 == 1.0 && r1 == 1.0 && v1 > v0 && v1 - v0 < 500) {
                g_res.signalIntervals.push_back(t1 - t0);
                g_res.signalSteps.push_back(static_cast<double>(v1 - v0));
            }
        }
    }
    for (int which = 0; which < 2; ++which) {
        double lastChange = -1.0;
        qint64 lastValue = -1;
        for (const auto& s : run.pos) {
            const qint64 v = which == 0 ? s.posA : s.posB;
            const double rate = which == 0 ? s.rateA : s.rateB;
            if (v < 0) {
                continue;
            }
            if (lastValue >= 0 && v != lastValue) {
                if (lastChange >= 0.0 && rate == 1.0 && v > lastValue && v - lastValue < 500) {
                    g_res.updateIntervals.push_back(s.t - lastChange);
                    g_res.updateSteps.push_back(static_cast<double>(v - lastValue));
                }
                lastChange = s.t;
            }
            lastValue = v;
        }
    }
}

void saveRaw(const Run& run, const Capture& cap, const std::vector<Onset>& onL, const std::vector<Onset>& onR) {
    const QString base = QDir(g_opt.outDir).filePath(QStringLiteral("raw/%1-%2").arg(run.scenario).arg(run.index));
    QFile f(base + "-positions.csv");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream s(&f);
        s << "t_ms,posA,posB,rateA,rateB\n";
        for (const auto& p : run.pos) {
            s << QString::number(p.t - run.T, 'f', 3) << ',' << p.posA << ',' << p.posB << ',' << p.rateA << ','
              << p.rateB << '\n';
        }
    }
    QFile g(base + "-events.csv");
    if (g.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream s(&g);
        s << "kind,t_ms,detail\n";
        for (const auto& e : run.events) {
            s << "event," << QString::number(e.t - run.T, 'f', 3) << ',' << e.what << '\n';
        }
        for (const auto& o : onL) {
            s << "onsetL," << QString::number(o.t - run.T, 'f', 3) << ",peak=" << o.peak << " valid=" << o.valid
              << '\n';
        }
        for (const auto& o : onR) {
            s << "onsetR," << QString::number(o.t - run.T, 'f', 3) << ",peak=" << o.peak << " valid=" << o.valid
              << '\n';
        }
    }
    if (run.index < 2) {
        writeWav(base + "-loopback.wav", cap);
    }
}

struct Analysis {
    Capture cap;
    std::vector<Onset> onL;
    std::vector<Onset> onR;
    std::vector<ClickError> ea;
    std::vector<ClickError> eb;
};

Analysis finishRun(Run& run) {
    Analysis an;
    an.cap = g_rec->takeSegment();
    an.onL = detectOnsets(an.cap.left, an.cap, g_thrL);
    an.onR = detectOnsets(an.cap.right, an.cap, g_thrR);
    for (const auto* v : {&an.onL, &an.onR}) {
        for (const auto& o : *v) {
            ++g_res.totalOnsets;
            g_res.discardedOnsets += o.valid ? 0 : 1;
        }
    }
    an.ea = clickErrors(run, an.onL, true);
    an.eb = clickErrors(run, an.onR, false);
    collectGranularity(run);
    saveRaw(run, an.cap, an.onL, an.onR);
    for (const auto& c : an.ea) {
        if (std::abs(c.lead) > 400.0) {
            g_res.note(QStringLiteral("%1-%2: lead de A %3 ms perto do limite de 500 ms; índice do clique pode estar errado")
                           .arg(run.scenario)
                           .arg(run.index)
                           .arg(c.lead));
            break;
        }
    }
    return an;
}

bool reportWindow(Run& run, const Window& w, const QString& prefix) {
    if (!w.ok()) {
        g_res.note(QStringLiteral("%1-%2 %3: cliques insuficientes (A=%4, B=%5); execução excluída")
                       .arg(run.scenario)
                       .arg(run.index)
                       .arg(prefix)
                       .arg(w.clicksA)
                       .arg(w.clicksB));
        ++g_res.excludedRuns;
        return false;
    }
    g_res.add(run, prefix + "desync", w.desync);
    g_res.add(run, prefix + "abs_desync", std::abs(w.desync));
    g_res.add(run, prefix + "eA", w.eA);
    g_res.add(run, prefix + "eB", w.eB);
    g_res.add(run, prefix + "leadA", w.leadA);
    g_res.add(run, prefix + "leadB", w.leadB);
    return true;
}

void beginRun(Run& run) {
    g_run = &run;
    g_rec->beginSegment();
}

void endRun(Run& run) {
    g_run = nullptr;
    run.correcting = false;
    if (run.a) {
        run.a->mp->stop();
    }
    if (run.b) {
        run.b->mp->stop();
    }
    waitMs(400); // deixa o fim do áudio chegar ao loopback
}

void schedulePlay(Run& run, std::vector<Player*> players) {
    // O QTimer pode disparar ~1 ms antes; dispara 3 ms antes e completa em espera ativa até T.
    const double delay = run.T - qpcNowMs() - 3.0;
    QTimer::singleShot(std::max(0, static_cast<int>(std::floor(delay))), Qt::PreciseTimer, [&run, players] {
        while (qpcNowMs() < run.T) {
        }
        run.event(QStringLiteral("tick"));
        for (auto* p : players) {
            p->mp->play();
        }
        run.event(QStringLiteral("play-called"));
    });
}

// ---------------------------------------------------------------------------------------------
// Cenários

bool loadPair(Run& run, std::optional<bool> pitchCompensation = std::nullopt) {
    run.a = makePlayer("clicks-left.mp3", "A", pitchCompensation);
    run.b = makePlayer("clicks-right.mp3", "B", pitchCompensation);
    if (!waitLoaded(*run.a) || !waitLoaded(*run.b)) {
        g_res.note(QStringLiteral("%1-%2: mídia não carregou").arg(run.scenario).arg(run.index));
        return false;
    }
    return true;
}

double scenarioPosition(int index) { return 15'137.0 + ((index * 7'919) % 120'000); }

void scenarioA(int index) {
    Run run;
    run.scenario = "a";
    run.index = index;
    if (!loadPair(run)) {
        return;
    }
    beginRun(run);
    run.P = 0.0;
    run.T = qpcNowMs() + 600.0;
    schedulePlay(run, {run.a.get(), run.b.get()});
    waitUntil(run.T + 4'700.0);
    endRun(run);
    auto an = finishRun(run);
    reportWindow(run, measure(an.ea, an.eb, run.T, run.T + 4'700.0), "");
}

// (b) e (c): mede sem correção, liga a correção e mede de novo no fim.
void measureWithCorrection(Run& run, Analysis& an, double uncorrectedFrom, double correctedFrom, double end) {
    reportWindow(run, measure(an.ea, an.eb, uncorrectedFrom, uncorrectedFrom + 3'000.0), "uncorr_");
    // Ruído do estimador: distância de cada estimativa à mediana do mesmo player na janela final.
    for (int who = 0; who < 2; ++who) {
        std::vector<double> v;
        for (const auto& [t, player, e] : run.estimates) {
            if (player == who && t >= correctedFrom && t <= end) {
                v.push_back(e);
            }
        }
        const double m = median(v);
        for (double e : v) {
            g_res.estimatorNoise.push_back(std::abs(e - m));
        }
        if (!v.empty()) {
            g_res.add(run, who == 0 ? "corr_estA" : "corr_estB", m);
        }
    }
    if (reportWindow(run, measure(an.ea, an.eb, correctedFrom, end), "corr_")) {
        g_res.add(run, "seeks", run.a->seeks + run.b->seeks);
        g_res.add(run, "nudges", run.a->nudges + run.b->nudges);
    }
}

void scenarioB(int index) {
    Run run;
    run.scenario = "b";
    run.index = index;
    if (!loadPair(run)) {
        return;
    }
    run.P = scenarioPosition(index);
    run.a->mp->setPosition(static_cast<qint64>(run.P));
    run.b->mp->setPosition(static_cast<qint64>(run.P));
    waitMs(300);
    beginRun(run);
    run.T = qpcNowMs() + 600.0;
    schedulePlay(run, {run.a.get(), run.b.get()});
    waitUntil(run.T + 3'500.0);
    run.correcting = true;
    run.event("correction-on");
    waitUntil(run.T + 13'500.0);
    endRun(run);
    auto an = finishRun(run);
    measureWithCorrection(run, an, run.T, run.T + 10'500.0, run.T + 13'500.0);
}

void scenarioC(int index) {
    Run run;
    run.scenario = "c";
    run.index = index;
    run.a = makePlayer("clicks-left.mp3", "A");
    if (!waitLoaded(*run.a)) {
        g_res.note(QStringLiteral("c-%1: mídia não carregou").arg(index));
        return;
    }
    run.P = scenarioPosition(index);
    run.a->mp->setPosition(static_cast<qint64>(run.P));
    waitMs(300);
    beginRun(run);
    run.T = qpcNowMs() + 600.0;
    schedulePlay(run, {run.a.get()});
    waitUntil(run.T + 2'000.0);
    run.event("b-join");
    run.b = makePlayer("clicks-right.mp3", "B");
    if (!waitLoaded(*run.b)) {
        endRun(run);
        g_res.note(QStringLiteral("c-%1: mídia de B não carregou").arg(index));
        return;
    }
    const double computed = run.ideal(qpcNowMs());
    run.b->mp->setPosition(static_cast<qint64>(std::llround(computed)));
    run.b->mp->play();
    const double joined = qpcNowMs();
    run.event(QStringLiteral("b-play pos=%1 load_ms=%2").arg(computed, 0, 'f', 0).arg(joined - run.T - 2'000.0));
    waitUntil(joined + 3'500.0);
    run.correcting = true;
    run.event("correction-on");
    waitUntil(joined + 13'500.0);
    endRun(run);
    auto an = finishRun(run);
    g_res.add(run, "join_load_ms", joined - run.T - 2'000.0);
    measureWithCorrection(run, an, joined, joined + 10'500.0, joined + 13'500.0);
}

// (s) seek com a música tocando: B vai para a posição ideal do momento, sem mudar de faixa.
// Mede o erro que o próprio seek introduz e o silêncio que ele causa.
void scenarioS(int index) {
    Run run;
    run.scenario = "s";
    run.index = index;
    if (!loadPair(run)) {
        return;
    }
    run.P = scenarioPosition(index);
    run.a->mp->setPosition(static_cast<qint64>(run.P));
    run.b->mp->setPosition(static_cast<qint64>(run.P));
    waitMs(300);
    beginRun(run);
    run.T = qpcNowMs() + 600.0;
    schedulePlay(run, {run.a.get(), run.b.get()});
    const double seekAt = run.T + 3'000.0;
    waitUntil(seekAt);
    const double target = run.ideal(qpcNowMs());
    run.b->mp->setPosition(static_cast<qint64>(std::llround(target + g_opt.seekLeadMs)));
    run.event(QStringLiteral("seek B pos=%1").arg(target, 0, 'f', 0));
    waitUntil(run.T + 7'500.0);
    endRun(run);
    auto an = finishRun(run);
    const Window before = measure(an.ea, an.eb, run.T, seekAt);
    const Window after = measure(an.ea, an.eb, seekAt + 500.0, run.T + 7'500.0);
    if (!reportWindow(run, before, "before_") || !reportWindow(run, after, "after_")) {
        return;
    }
    g_res.add(run, "seek_shift", after.eB - before.eB);
    g_res.add(run, "abs_seek_shift", std::abs(after.eB - before.eB));
}

void scenarioD(int index) {
    Run run;
    run.scenario = "d";
    run.index = index;
    if (!loadPair(run)) {
        return;
    }
    beginRun(run);
    run.P = 0.0;
    run.T = qpcNowMs() + 600.0;
    schedulePlay(run, {run.a.get(), run.b.get()});
    const double end = run.T + 179'000.0;
    while (qpcNowMs() < end) {
        waitMs(1'000.0);
    }
    endRun(run);
    auto an = finishRun(run);
    const Window first = measure(an.ea, an.eb, run.T, run.T + 5'700.0);
    const Window last = measure(an.ea, an.eb, end - 5'500.0, end);
    if (!reportWindow(run, first, "start_") || !reportWindow(run, last, "end_")) {
        return;
    }
    // Deriva: desync por pares de cliques de mesmo índice, e eA/eB contra o relógio QPC.
    std::vector<double> tx, dy, ta, ya, tb, yb;
    for (const auto& c : an.ea) {
        ta.push_back(c.t - run.T);
        ya.push_back(c.e);
    }
    for (const auto& c : an.eb) {
        tb.push_back(c.t - run.T);
        yb.push_back(c.e);
    }
    for (const auto& ca : an.ea) {
        for (const auto& cb : an.eb) {
            if (std::abs(ca.t - cb.t) < 400.0) {
                tx.push_back(ca.t - run.T);
                dy.push_back(ca.e - cb.e);
            }
        }
    }
    g_res.add(run, "desync_drift_ms_per_min", slopePerMinute(tx, dy));
    g_res.add(run, "eA_drift_ms_per_min", slopePerMinute(ta, ya));
    g_res.add(run, "eB_drift_ms_per_min", slopePerMinute(tb, yb));
    double maxAbs = 0.0;
    for (double v : dy) {
        maxAbs = std::max(maxAbs, std::abs(v));
    }
    g_res.add(run, "max_abs_desync", maxAbs);
    g_res.add(run, "clicks_paired", static_cast<double>(dy.size()));
    // A mesma deriva vista por position() (o que o app enxerga).
    std::vector<double> tp, pa, pb;
    for (const auto& s : run.pos) {
        if (s.t > run.T + 2'000.0 && s.posA >= 0 && s.posB >= 0) {
            tp.push_back(s.t - run.T);
            pa.push_back(s.posA - run.ideal(s.t));
            pb.push_back(s.posB - run.ideal(s.t));
        }
    }
    g_res.add(run, "posA_drift_ms_per_min", slopePerMinute(tp, pa));
    g_res.add(run, "posB_drift_ms_per_min", slopePerMinute(tp, pb));
}

// (e) com o padrão do Qt; (e2) igual, com compensação de tom desligada.
void scenarioE(int index, const QString& name, std::optional<bool> pitchCompensation) {
    Run run;
    run.scenario = name;
    run.index = index;
    if (!loadPair(run, pitchCompensation)) {
        return;
    }
    run.P = scenarioPosition(index);
    run.a->mp->setPosition(static_cast<qint64>(run.P));
    run.b->mp->setPosition(static_cast<qint64>(run.P));
    waitMs(300);
    beginRun(run);
    run.T = qpcNowMs() + 600.0;
    schedulePlay(run, {run.a.get(), run.b.get()});
    const double up = run.T + 3'600.0;
    const double down = run.T + 8'900.0;
    waitUntil(up);
    run.b->mp->setPlaybackRate(1.0 + g_opt.nudgeRate);
    run.event("rate-up");
    waitUntil(up + 2'000.0);
    run.b->mp->setPlaybackRate(1.0);
    run.event("rate-1");
    waitUntil(down);
    run.b->mp->setPlaybackRate(1.0 - g_opt.nudgeRate);
    run.event("rate-down");
    waitUntil(down + 2'000.0);
    run.b->mp->setPlaybackRate(1.0);
    run.event("rate-1");
    waitUntil(run.T + 14'300.0);
    endRun(run);
    auto an = finishRun(run);
    const Window w1 = measure(an.ea, an.eb, run.T, up);
    const Window w2 = measure(an.ea, an.eb, up + 2'300.0, down);
    const Window w3 = measure(an.ea, an.eb, down + 2'300.0, run.T + 14'300.0);
    if (!reportWindow(run, w1, "w1_") || !reportWindow(run, w2, "w2_") || !reportWindow(run, w3, "w3_")) {
        return;
    }
    const double expected = 2'000.0 * g_opt.nudgeRate;
    // B adiantado aparece como queda de eA - eB; o deslocamento de B é eB(depois) - eB(antes).
    const double shiftUp = w2.eB - w1.eB;
    const double shiftDown = w3.eB - w2.eB;
    g_res.add(run, "shift_up", shiftUp);
    g_res.add(run, "shift_down", shiftDown);
    g_res.add(run, "shift_up_error", shiftUp - expected);
    g_res.add(run, "shift_down_error", shiftDown + expected);
    g_res.add(run, "abs_shift_error", std::abs(shiftUp - expected));
    g_res.add(run, "abs_shift_error", std::abs(shiftDown + expected));
    g_res.add(run, "desync_change_up", w2.desync - w1.desync);
    g_res.add(run, "desync_change_down", w3.desync - w2.desync);
}

// Sonda de tom: um player, seno de 1 kHz, velocidades 1.0 / 1.01 / 0.99.
void pitchProbe(const QString& label, std::optional<bool> compensation) {
    Run run;
    run.scenario = "p";
    run.index = 0;
    run.a = makePlayer("tone-1k.mp3", "A");
    if (!waitLoaded(*run.a)) {
        g_res.note("p: mídia não carregou");
        return;
    }
    const auto availability = run.a->mp->pitchCompensationAvailability();
    if (compensation) {
        run.a->mp->setPitchCompensation(*compensation);
    }
    std::printf("  sonda %s: disponibilidade=%d compensacao=%d\n", qPrintable(label), static_cast<int>(availability),
                run.a->mp->pitchCompensation() ? 1 : 0);
    g_rec->beginSegment();
    run.a->mp->play();
    const double t0 = qpcNowMs();
    // Segmentos: 1.0, 1.01, 0.99, 1.0 e, no último, um seek para a própria posição (velocidade 1.0).
    const double rates[] = {1.0, 1.0 + g_opt.nudgeRate, 1.0 - g_opt.nudgeRate, 1.0, 1.0};
    std::vector<double> starts;
    for (int i = 0; i < 5; ++i) {
        const double start = t0 + 500.0 + i * 2'500.0;
        waitUntil(start);
        if (i == 4) {
            run.a->mp->setPosition(run.a->mp->position());
        } else {
            run.a->mp->setPlaybackRate(rates[i]);
        }
        starts.push_back(qpcNowMs());
    }
    waitUntil(starts.back() + 2'500.0);
    run.a->mp->stop();
    waitMs(400);
    Capture cap = g_rec->takeSegment();
    QFile f(QDir(g_opt.outDir).filePath("pitch.csv"));
    (void)f.open(QIODevice::Append | QIODevice::Text);
    QTextStream s(&f);
    for (int i = 0; i < 5; ++i) {
        // Frequência pela mediana dos períodos entre cruzamentos ascendentes de zero (interpolados).
        std::vector<double> crossings;
        // Silêncio perto da troca: janelas de 5 ms com RMS abaixo de 30% do RMS do seno contínuo.
        const double steadyRms = 0.2 * g_opt.volume / std::sqrt(2.0);
        int silentWindows = 0;
        const std::size_t win = static_cast<std::size_t>(cap.sampleRate / 200); // 5 ms
        for (std::size_t j = 1; j < cap.left.size(); ++j) {
            const double t = cap.timeOfSample(j);
            if (t >= starts[i] - 100.0 && t < starts[i] + 600.0 && j % win == 0 && j >= win) {
                double acc = 0.0;
                for (std::size_t q = j - win; q < j; ++q) {
                    acc += cap.left[q] * cap.left[q];
                }
                silentWindows += std::sqrt(acc / win) < 0.3 * steadyRms ? 1 : 0;
            }
            if (t < starts[i] + 700.0 || t > starts[i] + 2'200.0) {
                continue;
            }
            const float a = cap.left[j - 1];
            const float b = cap.left[j];
            if (a < 0.0f && b >= 0.0f) {
                crossings.push_back(static_cast<double>(j - 1) + a / (a - b));
            }
        }
        std::vector<double> periods;
        for (std::size_t j = 1; j < crossings.size(); ++j) {
            periods.push_back(crossings[j] - crossings[j - 1]);
        }
        const double freq = periods.empty() ? 0.0 : cap.sampleRate / median(periods);
        const double silenceMs = silentWindows * 5.0;
        const QString what = i == 4 ? QStringLiteral("seek") : QString::number(rates[i]);
        std::printf("  sonda %s: %s -> %.2f Hz; silencio perto da troca: %.0f ms\n", qPrintable(label),
                    qPrintable(what), freq, silenceMs);
        s << label << ',' << int(availability) << ',' << (run.a->mp->pitchCompensation() ? 1 : 0) << ',' << what
          << ',' << QString::number(freq, 'f', 2) << ',' << silenceMs << '\n';
    }
    if (compensation.has_value() || label == "padrao") {
        writeWav(QDir(g_opt.outDir).filePath(QStringLiteral("raw/pitch-%1-loopback.wav").arg(label)), cap);
    }
}

// Calibração: ruído de fundo, pico de cada canal e separação L/R.
bool calibrate() {
    std::printf("Calibracao\n");
    g_rec->beginSegment();
    waitMs(1'000.0);
    Capture bg = g_rec->takeSegment();
    float bgL = 0.0f;
    float bgR = 0.0f;
    for (std::size_t i = 0; i < bg.left.size(); ++i) {
        bgL = std::max(bgL, std::abs(bg.left[i]));
        bgR = std::max(bgR, std::abs(bg.right[i]));
    }
    std::printf("  fundo (1 s, nada tocando aqui): %zu amostras, pico L=%.4f R=%.4f\n", bg.left.size(), bgL, bgR);

    float peaks[2][2] = {{0, 0}, {0, 0}};  // [player][canal], amplitude bruta
    float scores[2][2] = {{0, 0}, {0, 0}}; // [player][canal], mediana do filtro casado nos cliques válidos
    int valid[2][2] = {{0, 0}, {0, 0}};
    for (int which = 0; which < 2; ++which) {
        auto p = makePlayer(which == 0 ? "clicks-left.mp3" : "clicks-right.mp3", which == 0 ? "A" : "B");
        if (!waitLoaded(*p)) {
            std::printf("BLOCKED: midia de calibracao nao carregou\n");
            return false;
        }
        g_rec->beginSegment();
        p->mp->play();
        waitMs(3'200.0);
        p->mp->stop();
        waitMs(400.0);
        Capture c = g_rec->takeSegment();
        for (std::size_t i = 0; i < c.left.size(); ++i) {
            peaks[which][0] = std::max(peaks[which][0], std::abs(c.left[i]));
            peaks[which][1] = std::max(peaks[which][1], std::abs(c.right[i]));
        }
        for (int ch = 0; ch < 2; ++ch) {
            std::vector<double> s;
            for (const auto& o : detectOnsets(ch == 0 ? c.left : c.right, c, 0.08f)) {
                if (o.valid) {
                    s.push_back(o.peak);
                }
            }
            valid[which][ch] = static_cast<int>(s.size());
            scores[which][ch] = s.empty() ? 0.0f : static_cast<float>(median(s));
        }
        std::printf("  so %s tocando: %zu amostras, pico L=%.4f R=%.4f; cliques validos L=%d (%.3f) R=%d (%.3f)\n",
                    which == 0 ? "A (esq.)" : "B (dir.)", c.left.size(), peaks[which][0], peaks[which][1],
                    valid[which][0], scores[which][0], valid[which][1], scores[which][1]);
        writeWav(QDir(g_opt.outDir).filePath(QStringLiteral("raw/calibration-%1.wav").arg(which == 0 ? "A" : "B")),
                 c);
    }
    if (valid[0][0] < 2 || valid[1][1] < 2) {
        std::printf("BLOCKED: o loopback nao contem os cliques (validos A/esq.=%d, B/dir.=%d; pico L=%.4f, R=%.4f)\n",
                    valid[0][0], valid[1][1], peaks[0][0], peaks[1][1]);
        return false;
    }
    if (valid[0][1] > 0 || valid[1][0] > 0) {
        std::printf("  aviso: cliques no canal oposto (A->R=%d, B->L=%d): mistura L/R no sistema?\n", valid[0][1],
                    valid[1][0]);
    }
    g_thrL = g_opt.thresholdOverride > 0 ? static_cast<float>(g_opt.thresholdOverride) : 0.5f * scores[0][0];
    g_thrR = g_opt.thresholdOverride > 0 ? static_cast<float>(g_opt.thresholdOverride) : 0.5f * scores[1][1];
    std::printf("  limiar do filtro casado: L=%.4f R=%.4f (metade da pontuacao mediana dos cliques)\n", g_thrL, g_thrR);
    return true;
}

void printSummary() {
    QFile f(QDir(g_opt.outDir).filePath("summary.txt"));
    (void)f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream s(&f);
    auto line = [&](const QString& text) {
        s << text << '\n';
        std::printf("%s\n", qPrintable(text));
    };
    line(QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6").arg("metrica", -32).arg("n", 3).arg("p50", 9).arg("p95", 9).arg(
        "max", 9).arg("min", 9));
    for (const auto& [name, values] : g_res.metrics) {
        line(QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6")
                 .arg(name, -32)
                 .arg(values.size(), 3)
                 .arg(percentile(values, 50), 9, 'f', 2)
                 .arg(percentile(values, 95), 9, 'f', 2)
                 .arg(*std::max_element(values.begin(), values.end()), 9, 'f', 2)
                 .arg(*std::min_element(values.begin(), values.end()), 9, 'f', 2));
    }
    auto stats = [&](const QString& name, const std::vector<double>& v) {
        if (v.empty()) {
            return;
        }
        line(QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6")
                 .arg(name, -32)
                 .arg(v.size(), 3)
                 .arg(percentile(v, 50), 9, 'f', 2)
                 .arg(percentile(v, 95), 9, 'f', 2)
                 .arg(*std::max_element(v.begin(), v.end()), 9, 'f', 2)
                 .arg(*std::min_element(v.begin(), v.end()), 9, 'f', 2));
    };
    stats("position.amostra10ms.intervalo_ms", g_res.updateIntervals);
    stats("position.amostra10ms.degrau_ms", g_res.updateSteps);
    stats("positionChanged.intervalo_ms", g_res.signalIntervals);
    stats("positionChanged.degrau_ms", g_res.signalSteps);
    stats("estimador.ruido_abs_ms", g_res.estimatorNoise);
    line(QStringLiteral("onsets: %1 detectados, %2 descartados (fora da grade de 1 s)")
             .arg(g_res.totalOnsets)
             .arg(g_res.discardedOnsets));
    line(QStringLiteral("janelas excluidas: %1; descontinuidades no loopback: %2")
             .arg(g_res.excludedRuns)
             .arg(g_rec->discontinuities()));
    for (const auto& n : g_res.notes) {
        line("nota: " + n);
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"media", "pasta com os MP3 gerados por make_clicks.py", "dir"});
    parser.addOption({"out", "pasta de saida", "dir"});
    parser.addOption({"scenarios", "lista: a,b,c,d,e,p", "list", "a,b,c,d,e,p"});
    parser.addOption({"runs", "repeticoes de a,b,c,e", "n", "20"});
    parser.addOption({"drift-runs", "repeticoes de d", "n", "3"});
    parser.addOption({"nudge", "room_drift_nudge_ms provisório", "ms", "20"});
    parser.addOption({"seek", "room_drift_seek_ms provisório", "ms", "200"});
    parser.addOption({"seek-lead", "avanço somado ao alvo do seek", "ms", "0"});
    parser.addOption({"volume", "volume de cada QAudioOutput", "v", "0.5"});
    parser.addOption({"pitch-comp", "compensação de tom em a-d: padrao|on|off", "modo", "padrao"});
    parser.addOption({"threshold", "limiar fixo de detecção (0 = calibrado)", "v", "0"});
    parser.process(app);
    g_opt.mediaDir = parser.value("media");
    g_opt.outDir = parser.value("out");
    g_opt.scenarios = parser.value("scenarios").split(',', Qt::SkipEmptyParts);
    g_opt.runs = parser.value("runs").toInt();
    g_opt.driftRuns = parser.value("drift-runs").toInt();
    g_opt.nudgeMs = parser.value("nudge").toDouble();
    g_opt.seekMs = parser.value("seek").toDouble();
    g_opt.seekLeadMs = parser.value("seek-lead").toDouble();
    g_opt.volume = parser.value("volume").toDouble();
    g_opt.thresholdOverride = parser.value("threshold").toDouble();
    if (parser.value("pitch-comp") == "on") {
        g_opt.pitchCompensation = true;
    } else if (parser.value("pitch-comp") == "off") {
        g_opt.pitchCompensation = false;
    }
    if (g_opt.mediaDir.isEmpty() || g_opt.outDir.isEmpty()) {
        std::fprintf(stderr, "uso: listening_room_sync --media DIR --out DIR\n");
        return 1;
    }
    QDir().mkpath(QDir(g_opt.outDir).filePath("raw"));

    const QAudioDevice qtDevice = QMediaDevices::defaultAudioOutput();
    if (qtDevice.isNull()) {
        std::printf("BLOCKED: sem dispositivo de saida padrao no Qt\n");
        return 2;
    }
    LoopbackRecorder recorder;
    std::string error;
    if (!recorder.start(&error)) {
        std::printf("BLOCKED: loopback: %s\n", error.c_str());
        return 2;
    }
    g_rec = &recorder;
    std::printf("Qt %s; QT_MEDIA_BACKEND=%s\n", qVersion(), qgetenv("QT_MEDIA_BACKEND").constData());
    std::printf("Saida padrao no Qt: %s (%s), formato preferido %d Hz %d canais\n", qPrintable(qtDevice.description()),
                qtDevice.id().constData(), qtDevice.preferredFormat().sampleRate(),
                qtDevice.preferredFormat().channelCount());
    std::printf("Loopback WASAPI: %s (%s), mixagem %s\n", recorder.deviceName().c_str(), recorder.deviceId().c_str(),
                recorder.formatDescription().c_str());
    if (QByteArray::fromStdString(recorder.deviceId()) != qtDevice.id()) {
        std::printf("aviso: o id do Qt difere do endpoint do loopback\n");
    }
    std::printf("nudge=%.0f ms, seek=%.0f ms, seek-lead=%.0f ms, volume=%.2f\n", g_opt.nudgeMs, g_opt.seekMs,
                g_opt.seekLeadMs, g_opt.volume);

    g_res.csv.setFileName(QDir(g_opt.outDir).filePath("results.csv"));
    (void)g_res.csv.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream csvStream(&g_res.csv);
    g_res.out = &csvStream;
    csvStream << "scenario,run,metric,value\n";

    QTimer sampler;
    sampler.setTimerType(Qt::PreciseTimer);
    QObject::connect(&sampler, &QTimer::timeout, &samplePositions);
    sampler.start(10);
    QTimer corrector;
    corrector.setTimerType(Qt::PreciseTimer);
    QObject::connect(&corrector, &QTimer::timeout, &correctionTick);
    corrector.start(100);

    if (!calibrate()) {
        recorder.stop();
        return 2;
    }

    const std::map<QString, std::function<void(int)>> scenarios = {
        {"a", scenarioA}, {"b", scenarioB}, {"c", scenarioC}, {"d", scenarioD},
        {"e", [](int i) { scenarioE(i, "e", std::nullopt); }},
        {"e2", [](int i) { scenarioE(i, "e2", false); }},
        {"s", scenarioS}};
    for (const QString& name : g_opt.scenarios) {
        if (name == "p") {
            std::printf("Cenario p (tom)\n");
            pitchProbe("padrao", std::nullopt);
            pitchProbe("comp-on", true);
            pitchProbe("comp-off", false);
            continue;
        }
        const auto it = scenarios.find(name);
        if (it == scenarios.end()) {
            continue;
        }
        const int n = name == "d" ? g_opt.driftRuns : g_opt.runs;
        for (int i = 0; i < n; ++i) {
            std::printf("Cenario %s, execucao %d/%d\n", qPrintable(name), i + 1, n);
            std::fflush(stdout);
            it->second(i);
            waitMs(300.0);
        }
    }
    printSummary();
    recorder.stop();
    return 0;
}

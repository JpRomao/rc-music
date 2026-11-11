#include "BrowserMediaPanel.hpp"

#include "BrowserMediaSelection.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"

#include <QDeadlineTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {
bool ownThread(const QWidget* widget) { return QThread::currentThread() == widget->thread(); }
struct AdmissionObservation final {
    std::string id;
    std::size_t item_count;
    QDeadlineTimer deadline{10'000};
};
} // namespace

struct BrowserMediaPanel::Impl final {
    BrowserMediaSelection selection;
    ProbeHandler handler;
    AsyncDownloadSubmission* submission{};
    std::optional<std::uint64_t> physical_probe;
    std::optional<std::uint64_t> download_probe;
    std::optional<AdmissionObservation> admission;
    QPushButton* detect{};
    QPushButton* confirm{};
    QPushButton* download{};
    QListWidget* list{};
    QLabel* status{};
    QLabel* count{};
    QLabel* unavailable{};
    QTimer* timer{};
    QTimer* admission_timer{};
    std::vector<DownloadItem> displayed;
    QString hint;
    bool invoking{false};
    bool refreshing{false};

    void forgetAdmission() {
        admission_timer->stop();
        admission.reset();
    }
};

BrowserMediaPanel::BrowserMediaPanel(QWidget* parent) : QWidget(parent), impl_(std::make_unique<Impl>()) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* scope = new QLabel(tr("Página principal e componentes acessíveis"), this);
    scope->setObjectName(QStringLiteral("browser-media-scope"));
    scope->setTextFormat(Qt::PlainText);
    scope->setWordWrap(true);
    scope->setToolTip(tr("Inclui frames acessíveis; frames de outra origem ou sandbox inacessíveis ficam de fora."));
    layout->addWidget(scope);

    auto* header = new QHBoxLayout;
    impl_->detect = new QPushButton(tr("Detectar mídia"), this);
    impl_->detect->setObjectName(QStringLiteral("browser-media-detect"));
    impl_->detect->setAccessibleName(impl_->detect->text());
    header->addWidget(impl_->detect);
    header->addStretch(1);
    impl_->count = new QLabel(this);
    impl_->count->setObjectName(QStringLiteral("browser-media-count"));
    impl_->count->setTextFormat(Qt::PlainText);
    header->addWidget(impl_->count);
    layout->addLayout(header);

    impl_->list = new QListWidget(this);
    impl_->list->setObjectName(QStringLiteral("browser-media-list"));
    impl_->list->setAccessibleName(tr("Áudios e vídeos detectados"));
    impl_->list->setSelectionMode(QAbstractItemView::NoSelection);
    impl_->list->setTabKeyNavigation(false);
    impl_->list->setWordWrap(true);
    impl_->list->setMinimumHeight(60);
    layout->addWidget(impl_->list, 1);

    auto* actions = new QHBoxLayout;
    impl_->confirm = new QPushButton(tr("Conferir seleção"), this);
    impl_->confirm->setObjectName(QStringLiteral("browser-media-confirm"));
    impl_->confirm->setAccessibleName(impl_->confirm->text());
    impl_->download = new QPushButton(tr("Baixar selecionados"), this);
    impl_->download->setObjectName(QStringLiteral("browser-media-download"));
    impl_->download->setAccessibleName(impl_->download->text());
    impl_->download->setToolTip(tr("Downloads indisponíveis nesta versão."));
    actions->addWidget(impl_->confirm);
    actions->addWidget(impl_->download);
    actions->addStretch(1);
    layout->addLayout(actions);
    auto* reason = impl_->unavailable = new QLabel(tr("Downloads indisponíveis nesta versão."), this);
    reason->setObjectName(QStringLiteral("browser-media-unavailable"));
    reason->setTextFormat(Qt::PlainText);
    reason->setWordWrap(true);
    layout->addWidget(reason);
    impl_->status = new QLabel(this);
    impl_->status->setObjectName(QStringLiteral("browser-media-status"));
    impl_->status->setTextFormat(Qt::PlainText);
    impl_->status->setWordWrap(true);
    layout->addWidget(impl_->status);

    QWidget::setTabOrder(impl_->detect, impl_->list);
    QWidget::setTabOrder(impl_->list, impl_->confirm);
    QWidget::setTabOrder(impl_->confirm, impl_->download);
    impl_->timer = new QTimer(this);
    impl_->timer->setSingleShot(true);
    impl_->timer->setInterval(5000);
    connect(impl_->timer, &QTimer::timeout, this, [this] {
        if (impl_->physical_probe)
            expireProbe(*impl_->physical_probe);
    });
    impl_->admission_timer = new QTimer(this);
    impl_->admission_timer->setInterval(50);
    connect(impl_->admission_timer, &QTimer::timeout, this, &BrowserMediaPanel::pollAdmission);
    connect(impl_->detect, &QPushButton::clicked, this, [this] { startProbe(ProbePurpose::Detection); });
    connect(impl_->confirm, &QPushButton::clicked, this, [this] { startProbe(ProbePurpose::Confirmation); });
    connect(impl_->download, &QPushButton::clicked, this, [this] { startProbe(ProbePurpose::Download); });
    connect(impl_->list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (impl_->refreshing)
            return;
        const auto row = impl_->list->row(item);
        const bool available = !impl_->invoking && !impl_->physical_probe && !impl_->admission;
        const bool changed =
            available && row >= 0 &&
            impl_->selection.setSelected(static_cast<std::size_t>(row), item->checkState() == Qt::Checked);
        impl_->hint = changed ? QString{} : tr("Selecione até 20 itens por vez.");
        refresh();
    });
    refresh();
}

BrowserMediaPanel::~BrowserMediaPanel() {
    impl_->timer->stop();
    impl_->forgetAdmission();
}

void BrowserMediaPanel::setDownloadSubmission(AsyncDownloadSubmission* submission) {
    if (!ownThread(this) || impl_->submission == submission)
        return;
    if (impl_->download_probe)
        (void)impl_->selection.fail(*impl_->download_probe);
    impl_->submission = submission;
    impl_->download_probe.reset();
    impl_->forgetAdmission();
    impl_->hint.clear();
    refresh();
}

void BrowserMediaPanel::setProbeHandler(ProbeHandler handler) {
    if (!ownThread(this))
        return;
    impl_->handler = std::move(handler);
    refresh();
}
void BrowserMediaPanel::setPage(QString trusted_url) {
    if (!ownThread(this))
        return;
    impl_->download_probe.reset();
    impl_->forgetAdmission();
    impl_->selection.navigationChanged(std::move(trusted_url));
    if (!impl_->physical_probe)
        impl_->hint.clear();
    // Navigation invalidates data, but it cannot acknowledge a previous
    // ExecuteScript call or free its physical slot.
    refresh();
}
void BrowserMediaPanel::resetEngine() {
    if (!ownThread(this))
        return;
    impl_->timer->stop();
    impl_->physical_probe.reset();
    impl_->download_probe.reset();
    impl_->forgetAdmission();
    impl_->selection.navigationChanged();
    impl_->hint.clear();
    refresh();
}
void BrowserMediaPanel::completeProbe(const std::uint64_t operation, const QByteArray& payload) {
    if (!ownThread(this) || impl_->physical_probe != operation)
        return;
    impl_->timer->stop();
    impl_->physical_probe.reset();
    impl_->hint.clear();
    const bool download = std::exchange(impl_->download_probe, std::nullopt) == operation;
    const bool confirmed = impl_->selection.complete(operation, payload);
    if (download && confirmed && impl_->submission) {
        beginAdmission(impl_->selection.takeConfirmedItems());
        return;
    }
    refresh();
}
void BrowserMediaPanel::failProbe(const std::uint64_t operation) {
    if (!ownThread(this) || impl_->physical_probe != operation)
        return;
    impl_->timer->stop();
    impl_->physical_probe.reset();
    impl_->download_probe.reset();
    impl_->hint.clear();
    (void)impl_->selection.fail(operation);
    refresh();
}
void BrowserMediaPanel::expireProbe(const std::uint64_t operation) {
    if (!ownThread(this) || impl_->physical_probe != operation)
        return;
    impl_->timer->stop();
    impl_->download_probe.reset();
    (void)impl_->selection.timeout(operation);
    impl_->hint = tr("A página não respondeu a tempo. Reabra o navegador para tentar novamente.");
    refresh();
}
std::optional<std::uint64_t> BrowserMediaPanel::pendingProbe() const {
    return ownThread(this) ? impl_->physical_probe : std::nullopt;
}

void BrowserMediaPanel::startProbe(const ProbePurpose purpose) {
    const bool download = purpose == ProbePurpose::Download;
    if (!ownThread(this) || impl_->invoking || impl_->physical_probe || impl_->admission || !impl_->handler ||
        (download && !impl_->submission))
        return;
    // The callable must outlive a callback that destroys this widget.
    auto handler = impl_->handler;
    const auto operation =
        purpose == ProbePurpose::Detection ? impl_->selection.beginDetection() : impl_->selection.beginConfirmation();
    if (!operation)
        return;
    impl_->physical_probe = operation;
    if (download)
        impl_->download_probe = operation;
    impl_->hint.clear();
    impl_->invoking = true;
    impl_->timer->start();
    const QPointer<BrowserMediaPanel> guard{this};
    refresh();
    if (!guard)
        return;
    try {
        handler(*operation);
    } catch (...) {
        if (!guard)
            return;
        // An exception cannot prove a script physically finished. Keep its
        // slot occupied until a matching callback or engine retirement.
        if (impl_->physical_probe == operation) {
            impl_->download_probe.reset();
            (void)impl_->selection.fail(*operation);
            impl_->hint = tr("A verificação falhou. Reabra o navegador para tentar novamente.");
        }
    }
    if (!guard)
        return;
    impl_->invoking = false;
    refresh();
}

void BrowserMediaPanel::beginAdmission(std::vector<DownloadItem> items) {
    if (items.empty() || !impl_->submission) {
        refresh();
        return;
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    impl_->admission = AdmissionObservation{id, items.size()};
    const auto selected = impl_->selection.selectedIndices();
    for (const auto index : selected)
        (void)impl_->selection.setSelected(index, false);
    auto* const submission = impl_->submission;
    const QPointer<BrowserMediaPanel> guard{this};
    DownloadAdmissionResult decision;
    try {
        decision = submission->begin({id, std::move(items)});
    } catch (...) {
        // A failed observation cannot prove that durable admission did not begin.
    }
    if (!guard || !impl_->admission || impl_->admission->id != id)
        return;
    if (decision.state == DownloadAdmissionState::Completed) {
        finishAdmission(decision.result);
        return;
    }
    impl_->hint = tr("Confirmando o salvamento do pedido…");
    impl_->admission_timer->start();
    refresh();
}

void BrowserMediaPanel::pollAdmission() {
    if (!impl_->admission || !impl_->submission)
        return;
    const auto id = impl_->admission->id;
    const QPointer<BrowserMediaPanel> guard{this};
    std::optional<DownloadSubmissionResult> result;
    try {
        result = impl_->submission->poll(id);
    } catch (...) {
        result = DownloadSubmissionResult{};
    }
    if (!guard || !impl_->admission || impl_->admission->id != id)
        return;
    if (result || impl_->admission->deadline.hasExpired())
        finishAdmission(result.value_or(DownloadSubmissionResult{}));
}

void BrowserMediaPanel::finishAdmission(const DownloadSubmissionResult& result) {
    const auto count = impl_->admission->item_count;
    impl_->forgetAdmission();
    if (result.error == DownloadSubmissionError::None && result.job_ids.size() == count &&
        std::ranges::none_of(result.job_ids, [](const auto& id) { return id.empty(); }))
        impl_->hint = tr("Pedido aceito na fila de downloads. Acompanhe no painel Downloads.");
    else if (result.error == DownloadSubmissionError::Busy)
        impl_->hint = tr("A fila de downloads está ocupada. Confira Downloads antes de tentar novamente.");
    else if (result.error == DownloadSubmissionError::InvalidRequest ||
             result.error == DownloadSubmissionError::UnsupportedSource)
        impl_->hint = tr("A fila de downloads recusou a seleção. Confira as fontes e selecione novamente.");
    else if (result.error == DownloadSubmissionError::LimitExceeded)
        impl_->hint = tr("O pedido ultrapassou o limite da fila. Confira Downloads antes de tentar novamente.");
    else
        impl_->hint = tr("Não foi possível confirmar o pedido. Confira Downloads antes de tentar novamente.");
    refresh();
}

void BrowserMediaPanel::refresh() {
    const auto& snapshot = impl_->selection.snapshot();
    const auto candidates = snapshot ? snapshot->candidates : std::vector<DownloadItem>{};
    const auto& selected = impl_->selection.selectedIndices();
    impl_->refreshing = true;
    const QPointer<BrowserMediaPanel> guard{this};
    QSignalBlocker blocker{impl_->list};
    if (candidates != impl_->displayed) {
        const auto current = impl_->list->currentRow();
        impl_->list->clear();
        if (!guard) {
            blocker.dismiss();
            return;
        }
        impl_->displayed = candidates;
        for (const auto& item : candidates) {
            const auto title = QString::fromStdString(item.title);
            const auto host = QUrl{QString::fromStdString(item.url), QUrl::StrictMode}.host();
            const auto kind = item.kind == DownloadMediaKind::Audio ? tr("Áudio") : tr("Vídeo");
            auto* row =
                new QListWidgetItem(title + QLatin1Char('\n') + kind + QStringLiteral(" · ") + host, impl_->list);
            if (!guard) {
                blocker.dismiss();
                return;
            }
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        }
        if (!candidates.empty())
            impl_->list->setCurrentRow(std::clamp(current, 0, impl_->list->count() - 1));
    }
    for (int index = 0; index < impl_->list->count(); ++index)
        impl_->list->item(index)->setCheckState(
            std::ranges::find(selected, static_cast<std::size_t>(index)) != selected.end() ? Qt::Checked
                                                                                           : Qt::Unchecked);
    impl_->refreshing = false;
    const bool available = !impl_->invoking && !impl_->physical_probe && !impl_->admission && impl_->handler;
    impl_->detect->setEnabled(available && impl_->selection.canDetect());
    impl_->confirm->setEnabled(available && impl_->selection.canConfirm());
    impl_->download->setEnabled(available && impl_->submission && impl_->selection.canConfirm());
    impl_->download->setToolTip(impl_->submission ? tr("Conferir novamente e enviar a seleção à fila de downloads.")
                                                  : impl_->unavailable->text());
    impl_->unavailable->setVisible(!impl_->submission);
    impl_->list->setEnabled(available && snapshot.has_value());
    impl_->count->setText(tr("Selecionados: %1 de 20").arg(static_cast<qulonglong>(selected.size())));

    QString message;
    switch (impl_->selection.state()) {
    case BrowserMediaSelectionState::Unavailable:
        message = tr("Abra uma página HTTPS compatível para detectar mídia.");
        break;
    case BrowserMediaSelectionState::Idle:
        message = impl_->physical_probe ? tr("Aguardando a verificação anterior antes de detectar nesta página.")
                                        : tr("Clique em Detectar mídia para procurar áudios e vídeos.");
        break;
    case BrowserMediaSelectionState::Detecting:
        message = tr("Procurando áudios e vídeos nesta página…");
        break;
    case BrowserMediaSelectionState::Confirming:
        message = tr("Conferindo novamente as fontes selecionadas…");
        break;
    case BrowserMediaSelectionState::Ready:
        if (snapshot && snapshot->truncated)
            message =
                tr("Lista parcial: os limites de leitura foram atingidos. Clique em Detectar mídia para atualizar.");
        else if (candidates.empty())
            message =
                tr("Nenhuma mídia compatível encontrada. Inicie a mídia na página e tente Detectar mídia novamente.");
        else
            message = impl_->submission ? tr("Selecione até 20 itens para conferir ou baixar.")
                                        : tr("Selecione até 20 itens e clique em Conferir seleção.");
        break;
    case BrowserMediaSelectionState::Confirmed:
        message = impl_->submission ? tr("Seleção conferida. Use Baixar selecionados para enviar o pedido.")
                                    : tr("Seleção conferida. Downloads indisponíveis nesta versão.");
        break;
    case BrowserMediaSelectionState::InvalidSnapshot:
        message = tr("Não foi possível validar a lista. Clique em Detectar mídia para tentar novamente.");
        break;
    case BrowserMediaSelectionState::SourceChanged:
        message = tr("Uma fonte selecionada mudou. Clique em Detectar mídia e refaça a seleção.");
        break;
    case BrowserMediaSelectionState::Failed:
        message = tr("Não foi possível verificar a página. Clique em Detectar mídia para tentar novamente.");
        break;
    case BrowserMediaSelectionState::TimedOut:
        message = tr("A página não respondeu a tempo. Clique em Detectar mídia para tentar novamente.");
        break;
    }
    impl_->status->setText(impl_->hint.isEmpty() ? message : impl_->hint);
}
} // namespace music_player::m0

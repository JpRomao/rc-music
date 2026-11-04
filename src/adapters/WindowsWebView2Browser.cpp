#include "adapters/WindowsWebView2Browser.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// SDK forward declarations require COM before WebView2, and its options header
// must precede Qt's __has_attribute fallback on MSVC.
// clang-format off
#include <Windows.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
// clang-format on

#include "adapters/BrowserNavigationPolicy.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMoveEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

namespace music_player::m0 {
namespace {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

class Surface final : public QWidget {
  public:
    using QWidget::QWidget;
    std::function<void()> resized;
    std::function<void()> focused;

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (resized)
            resized();
    }
    void moveEvent(QMoveEvent* event) override {
        QWidget::moveEvent(event);
        if (resized)
            resized();
    }
    void focusInEvent(QFocusEvent* event) override {
        QWidget::focusInEvent(event);
        if (focused)
            focused();
    }
};

class BrowserWindow final : public QWidget {
  public:
    std::function<void()> closed;

  protected:
    void closeEvent(QCloseEvent* event) override {
        if (closed)
            closed();
        QWidget::closeEvent(event);
    }
};

struct CoTaskFree {
    void operator()(wchar_t* value) const noexcept { CoTaskMemFree(value); }
};
using NativeString = std::unique_ptr<wchar_t, CoTaskFree>;

QString boundedUri(LPWSTR value) {
    const NativeString owned{value};
    if (!value)
        return {};
    const auto size = wcsnlen_s(value, 8193);
    return size > 8192 ? QString{} : QString::fromWCharArray(value, static_cast<qsizetype>(size));
}

struct BrowserState;

template <class Function>
HRESULT dispatch(const std::weak_ptr<BrowserState>& weak, std::uint64_t generation, Function&& function) noexcept;

struct BrowserState final : std::enable_shared_from_this<BrowserState> {
    std::unique_ptr<BrowserWindow> window;
    QLineEdit* address{};
    QLabel* status{};
    QPushButton* back{};
    QPushButton* forward{};
    QPushButton* reload{};
    QPushButton* stop{};
    QPushButton* go{};
    Surface* surface{};
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::vector<std::function<void()>> detach;
    std::uint64_t generation{};
    QString profile;
    bool starting{};
    bool apartment{};
    bool retired{};

    ~BrowserState() {
        closeEngine();
        window.reset();
        if (apartment)
            CoUninitialize();
    }

    void message(const wchar_t* text) {
        if (status)
            status->setText(QString::fromWCharArray(text));
    }

    void buttons() {
        BOOL can_back = FALSE;
        BOOL can_forward = FALSE;
        if (webview) {
            (void)webview->get_CanGoBack(&can_back);
            (void)webview->get_CanGoForward(&can_forward);
        }
        back->setEnabled(can_back);
        forward->setEnabled(can_forward);
        reload->setEnabled(webview != nullptr);
        stop->setEnabled(webview != nullptr);
        go->setEnabled(webview != nullptr);
    }

    void closeEngine() noexcept {
        ++generation;
        starting = false;
        for (auto& remove : detach)
            remove();
        detach.clear();
        webview.Reset();
        if (controller)
            (void)controller->Close();
        controller.Reset();
        environment.Reset();
    }

    void unavailable() {
        closeEngine();
        buttons();
        message(L"Navegação indisponível. Reabra Navegar para tentar novamente; o player continua disponível.");
    }

    void resize() {
        if (!controller)
            return;
        RECT bounds{};
        if (GetClientRect(reinterpret_cast<HWND>(surface->winId()), &bounds)) {
            (void)controller->put_Bounds(bounds);
            (void)controller->NotifyParentWindowPositionChanged();
        }
    }

    void navigate() {
        if (!webview)
            return;
        const auto uri = address->text();
        if (!browserNavigationAllowed(uri)) {
            message(L"Informe um endereço HTTPS completo, sem credenciais.");
            return;
        }
        const auto native = uri.toStdWString();
        if (FAILED(webview->Navigate(native.c_str())))
            message(L"Não foi possível abrir este endereço.");
    }

    bool configure() {
        ComPtr<ICoreWebView2Settings> settings;
        ComPtr<ICoreWebView2Settings4> settings4;
        ComPtr<ICoreWebView2_4> downloads;
        ComPtr<ICoreWebView2_5> client_certificates;
        ComPtr<ICoreWebView2_10> authentication;
        ComPtr<ICoreWebView2_14> certificates;
        ComPtr<ICoreWebView2_18> external_schemes;
        if (FAILED(controller->get_CoreWebView2(&webview)) || FAILED(webview->get_Settings(&settings)) ||
            FAILED(settings.As(&settings4)) || FAILED(webview.As(&downloads)) ||
            FAILED(webview.As(&client_certificates)) || FAILED(webview.As(&authentication)) ||
            FAILED(webview.As(&certificates)) || FAILED(webview.As(&external_schemes)) ||
            FAILED(settings->put_AreHostObjectsAllowed(FALSE)) || FAILED(settings->put_IsWebMessageEnabled(FALSE)) ||
            FAILED(settings->put_AreDevToolsEnabled(FALSE)) ||
            FAILED(settings->put_AreDefaultContextMenusEnabled(FALSE)) ||
            FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)) ||
            FAILED(settings4->put_AreBrowserAcceleratorKeysEnabled(FALSE)) ||
            FAILED(settings4->put_IsPasswordAutosaveEnabled(FALSE)) ||
            FAILED(settings4->put_IsGeneralAutofillEnabled(FALSE)))
            return false;

        const auto weak = weak_from_this();
        const auto epoch = generation;
        EventRegistrationToken token{};
        const auto navigation = Callback<ICoreWebView2NavigationStartingEventHandler>(
            [weak, epoch](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) noexcept {
                return dispatch(weak, epoch, [&](BrowserState& state) {
                    LPWSTR raw = nullptr;
                    const auto uri = SUCCEEDED(args->get_Uri(&raw)) ? boundedUri(raw) : QString{};
                    if (!browserNavigationAllowed(uri)) {
                        (void)args->put_Cancel(TRUE);
                        state.message(L"Navegação bloqueada: este navegador aceita somente HTTPS.");
                    } else {
                        state.message(L"Carregando…");
                    }
                });
            });
        if (FAILED(webview->add_NavigationStarting(navigation.Get(), &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_NavigationStarting(token); });
        if (FAILED(webview->add_FrameNavigationStarting(navigation.Get(), &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_FrameNavigationStarting(token); });

        if (FAILED(webview->add_PermissionRequested(
                Callback<ICoreWebView2PermissionRequestedEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) noexcept {
                        return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_PermissionRequested(token); });
        if (FAILED(webview->add_NewWindowRequested(
                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) noexcept {
                        return args->put_Handled(TRUE);
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_NewWindowRequested(token); });
        if (FAILED(downloads->add_DownloadStarting(
                Callback<ICoreWebView2DownloadStartingEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) noexcept {
                        return args->put_Cancel(TRUE);
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = downloads, token] { (void)view->remove_DownloadStarting(token); });
        if (FAILED(client_certificates->add_ClientCertificateRequested(
                Callback<ICoreWebView2ClientCertificateRequestedEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2ClientCertificateRequestedEventArgs* args) noexcept {
                        return args->put_Cancel(TRUE);
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = client_certificates, token] { (void)view->remove_ClientCertificateRequested(token); });
        if (FAILED(authentication->add_BasicAuthenticationRequested(
                Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2BasicAuthenticationRequestedEventArgs* args) noexcept {
                        return args->put_Cancel(TRUE);
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = authentication, token] { (void)view->remove_BasicAuthenticationRequested(token); });
        if (FAILED(certificates->add_ServerCertificateErrorDetected(
                Callback<ICoreWebView2ServerCertificateErrorDetectedEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2ServerCertificateErrorDetectedEventArgs* args) noexcept {
                        return args->put_Action(COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL);
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = certificates, token] { (void)view->remove_ServerCertificateErrorDetected(token); });
        if (FAILED(external_schemes->add_LaunchingExternalUriScheme(
                Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) noexcept {
                        return args->put_Cancel(TRUE);
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = external_schemes, token] { (void)view->remove_LaunchingExternalUriScheme(token); });

        if (FAILED(webview->add_HistoryChanged(
                Callback<ICoreWebView2HistoryChangedEventHandler>([weak, epoch](ICoreWebView2*, IUnknown*) noexcept {
                    return dispatch(weak, epoch, [](BrowserState& state) { state.buttons(); });
                }).Get(),
                &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_HistoryChanged(token); });
        if (FAILED(webview->add_SourceChanged(
                Callback<ICoreWebView2SourceChangedEventHandler>([weak, epoch](
                                                                     ICoreWebView2*,
                                                                     ICoreWebView2SourceChangedEventArgs*) noexcept {
                    return dispatch(weak, epoch, [](BrowserState& state) {
                        LPWSTR raw = nullptr;
                        if (SUCCEEDED(state.webview->get_Source(&raw))) {
                            const auto uri = boundedUri(raw);
                            if (browserNavigationAllowed(uri))
                                state.address->setText(uri);
                        }
                    });
                }).Get(),
                &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_SourceChanged(token); });
        if (FAILED(webview->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                    [weak, epoch](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) noexcept {
                        return dispatch(weak, epoch, [&](BrowserState& state) {
                            BOOL success = FALSE;
                            (void)args->get_IsSuccess(&success);
                            state.message(success ? L"Página carregada." : L"A página não pôde ser carregada.");
                            state.buttons();
                        });
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_NavigationCompleted(token); });
        if (FAILED(webview->add_ProcessFailed(
                Callback<ICoreWebView2ProcessFailedEventHandler>([weak, epoch](
                                                                     ICoreWebView2*,
                                                                     ICoreWebView2ProcessFailedEventArgs*) noexcept {
                    return dispatch(weak, epoch, [](BrowserState& state) { state.unavailable(); });
                }).Get(),
                &token)))
            return false;
        detach.push_back([view = webview, token] { (void)view->remove_ProcessFailed(token); });
        if (FAILED(controller->add_MoveFocusRequested(
                Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
                    [weak, epoch](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) noexcept {
                        return dispatch(weak, epoch, [&](BrowserState& state) {
                            state.address->setFocus(Qt::TabFocusReason);
                            (void)args->put_Handled(TRUE);
                        });
                    })
                    .Get(),
                &token)))
            return false;
        detach.push_back([view = controller, token] { (void)view->remove_MoveFocusRequested(token); });
        return true;
    }

    void begin() {
        if (starting || webview || retired)
            return;
        // Refuse explicit environment overrides instead of importing another
        // application's profile or arbitrary browser switches into this session.
        for (const auto* name :
             {L"WEBVIEW2_USER_DATA_FOLDER", L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER",
              L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"WEBVIEW2_RELEASE_CHANNELS", L"WEBVIEW2_CHANNEL_SEARCH_KIND",
              L"WEBVIEW2_WAIT_FOR_SCRIPT_DEBUGGER", L"WEBVIEW2_PIPE_FOR_SCRIPT_DEBUGGER"}) {
            if (GetEnvironmentVariableW(name, nullptr, 0) != 0) {
                message(L"Navegação indisponível com overrides WEBVIEW2 ativos neste processo.");
                return;
            }
        }
        if (profile.isEmpty()) {
            const auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            if (base.isEmpty()) {
                unavailable();
                return;
            }
            profile = QDir(base).filePath(QStringLiteral("EmbeddedBrowser/") +
                                          QUuid::createUuid().toString(QUuid::WithoutBraces));
            if (!QDir().mkpath(profile)) {
                unavailable();
                return;
            }
        }
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        ComPtr<ICoreWebView2EnvironmentOptions2> exclusive;
        ComPtr<ICoreWebView2EnvironmentOptions6> extensions;
        if (!options || FAILED(options.As(&exclusive)) || FAILED(options.As(&extensions)) ||
            FAILED(options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE)) ||
            FAILED(exclusive->put_ExclusiveUserDataFolderAccess(TRUE)) ||
            FAILED(extensions->put_AreBrowserExtensionsEnabled(FALSE))) {
            unavailable();
            return;
        }
        starting = true;
        const auto weak = weak_from_this();
        const auto epoch = ++generation;
        const auto data_folder = QDir::toNativeSeparators(profile).toStdWString();
        const auto result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, data_folder.c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [weak, epoch](HRESULT result, ICoreWebView2Environment* created) noexcept {
                    return dispatch(weak, epoch, [&](BrowserState& state) {
                        if (FAILED(result) || !created) {
                            state.unavailable();
                            return;
                        }
                        state.environment = created;
                        ComPtr<ICoreWebView2Environment7> actual;
                        LPWSTR raw = nullptr;
                        if (FAILED(created->QueryInterface(IID_PPV_ARGS(&actual))) ||
                            FAILED(actual->get_UserDataFolder(&raw))) {
                            state.unavailable();
                            return;
                        }
                        const auto actual_folder = boundedUri(raw);
                        if (QDir::cleanPath(QDir::fromNativeSeparators(actual_folder))
                                .compare(QDir::cleanPath(state.profile), Qt::CaseInsensitive) != 0) {
                            state.unavailable();
                            return;
                        }
                        ComPtr<ICoreWebView2Environment10> versioned;
                        ComPtr<ICoreWebView2ControllerOptions> control_options;
                        if (FAILED(created->QueryInterface(IID_PPV_ARGS(&versioned))) ||
                            FAILED(versioned->CreateCoreWebView2ControllerOptions(&control_options)) ||
                            FAILED(control_options->put_ProfileName(L"RCMusicInternal")) ||
                            FAILED(control_options->put_IsInPrivateModeEnabled(TRUE))) {
                            state.unavailable();
                            return;
                        }
                        const auto started = versioned->CreateCoreWebView2ControllerWithOptions(
                            reinterpret_cast<HWND>(state.surface->winId()), control_options.Get(),
                            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [weak, epoch](HRESULT controller_result,
                                              ICoreWebView2Controller* created_controller) noexcept {
                                    return dispatch(weak, epoch, [&](BrowserState& target) {
                                        if (FAILED(controller_result) || !created_controller) {
                                            target.unavailable();
                                            return;
                                        }
                                        target.controller = created_controller;
                                        if (!target.configure()) {
                                            target.unavailable();
                                            return;
                                        }
                                        target.starting = false;
                                        target.resize();
                                        (void)target.controller->put_IsVisible(TRUE);
                                        target.buttons();
                                        target.message(
                                            L"Informe um endereço HTTPS para navegar. Downloads estão desativados.");
                                    });
                                })
                                .Get());
                        if (FAILED(started))
                            state.unavailable();
                    });
                })
                .Get());
        if (FAILED(result))
            unavailable();
    }

    bool create(std::uintptr_t owner) {
        window = std::make_unique<BrowserWindow>();
        window->setWindowTitle(QStringLiteral("RC Music — Navegar"));
        window->resize(1000, 700);
        window->setMinimumSize(600, 360);
        auto* layout = new QVBoxLayout(window.get());
        auto* toolbar = new QHBoxLayout;
        back = new QPushButton(QStringLiteral("Voltar"), window.get());
        forward = new QPushButton(QStringLiteral("Avançar"), window.get());
        reload = new QPushButton(QStringLiteral("Recarregar"), window.get());
        stop = new QPushButton(QStringLiteral("Parar"), window.get());
        address = new QLineEdit(window.get());
        address->setMaxLength(8192);
        address->setPlaceholderText(QStringLiteral("Endereço HTTPS"));
        address->setAccessibleName(QStringLiteral("Endereço HTTPS"));
        go = new QPushButton(QStringLiteral("Ir"), window.get());
        for (auto* button : {back, forward, reload, stop})
            toolbar->addWidget(button);
        toolbar->addWidget(address, 1);
        toolbar->addWidget(go);
        layout->addLayout(toolbar);
        surface = new Surface(window.get());
        surface->setAttribute(Qt::WA_NativeWindow);
        surface->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(surface, 1);
        status = new QLabel(QStringLiteral("Preparando navegação…"), window.get());
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        layout->addWidget(status);
        const auto native = reinterpret_cast<HWND>(window->winId());
        SetLastError(0);
        if (SetWindowLongPtrW(native, GWLP_HWNDPARENT, static_cast<LONG_PTR>(owner)) == 0 && GetLastError() != 0)
            return false;
        const auto weak = weak_from_this();
        window->closed = [weak] {
            if (const auto state = weak.lock()) {
                state->closeEngine();
                state->buttons();
            }
        };
        surface->resized = [weak] {
            if (const auto state = weak.lock())
                state->resize();
        };
        surface->focused = [weak] {
            if (const auto state = weak.lock(); state && state->controller)
                (void)state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        };
        QObject::connect(address, &QLineEdit::returnPressed, window.get(), [weak] {
            if (const auto state = weak.lock())
                state->navigate();
        });
        QObject::connect(go, &QPushButton::clicked, address, &QLineEdit::returnPressed);
        QObject::connect(back, &QPushButton::clicked, window.get(), [weak] {
            if (const auto state = weak.lock(); state && state->webview)
                (void)state->webview->GoBack();
        });
        QObject::connect(forward, &QPushButton::clicked, window.get(), [weak] {
            if (const auto state = weak.lock(); state && state->webview)
                (void)state->webview->GoForward();
        });
        QObject::connect(reload, &QPushButton::clicked, window.get(), [weak] {
            if (const auto state = weak.lock(); state && state->webview)
                (void)state->webview->Reload();
        });
        QObject::connect(stop, &QPushButton::clicked, window.get(), [weak] {
            if (const auto state = weak.lock(); state && state->webview)
                (void)state->webview->Stop();
        });
        buttons();
        return true;
    }
};

template <class Function>
HRESULT dispatch(const std::weak_ptr<BrowserState>& weak, std::uint64_t generation, Function&& function) noexcept {
    try {
        if (const auto state = weak.lock(); state && !state->retired && state->generation == generation)
            function(*state);
        return S_OK;
    } catch (...) {
        if (const auto state = weak.lock())
            state->closeEngine();
        return E_FAIL;
    }
}

class WindowsWebView2Browser final : public EmbeddedBrowserWindow {
  public:
    WindowsWebView2Browser() : state_(std::make_shared<BrowserState>()) {}
    ~WindowsWebView2Browser() override { shutdown(); }
    bool show(std::uintptr_t owner) override {
        const auto handle = reinterpret_cast<HWND>(owner);
        DWORD process_id = 0;
        if (state_->retired || !owner || !IsWindow(handle) ||
            GetWindowThreadProcessId(handle, &process_id) != GetCurrentThreadId() ||
            process_id != GetCurrentProcessId() || !qobject_cast<QApplication*>(QCoreApplication::instance()) ||
            QThread::currentThread() != QCoreApplication::instance()->thread())
            return false;
        if (!state_->apartment) {
            if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
                return false;
            state_->apartment = true;
        }
        if (!state_->window && !state_->create(owner))
            return false;
        state_->window->show();
        state_->window->raise();
        state_->window->activateWindow();
        state_->address->setFocus();
        state_->begin();
        return true;
    }
    void shutdown() noexcept override {
        if (state_->retired)
            return;
        state_->retired = true;
        state_->closeEngine();
        if (state_->window)
            state_->window->hide();
    }

  private:
    std::shared_ptr<BrowserState> state_;
};
} // namespace
std::unique_ptr<EmbeddedBrowserWindow> makeWindowsWebView2Browser() {
    return std::make_unique<WindowsWebView2Browser>();
}
} // namespace music_player::m0

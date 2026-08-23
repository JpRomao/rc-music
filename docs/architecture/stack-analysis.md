# Análise e recomendação de stack

**Estado:** recomendação técnica; decisão ainda não aceita  
**Data da pesquisa:** 2026-08-23  
**ADR relacionado:** `decisions/0001-language-and-ui-stack.md`

## 1. Resumo executivo

A recomendação para o primeiro spike é:

- **host e domínio:** Rust;
- **desktop shell:** Tauri 2;
- **interface:** Svelte + TypeScript estrito + Vite, sem SSR;
- **áudio:** Rodio sobre CPAL, usando decodificação Symphonia;
- **metadados:** Lofty e metadados do sistema de arquivos;
- **integrações:** tray e single-instance do Tauri; adapters próprios para Windows SMTC e Linux MPRIS;
- **persistência:** JSON versionado e gravação atômica no diretório de dados do usuário;
- **testes:** `cargo test` no núcleo, Vitest/Testing Library na apresentação e poucos E2E/smoke tests nativos;
- **distribuição inicial:** NSIS por usuário no Windows; `.deb` e, depois, AppImage no Linux.

Esta não é a opção de menor RAM absoluta. Rust + Slint provavelmente venceria esse critério. A recomendação otimiza o requisito mais importante — uma UX moderna e refinada — sem empacotar Chromium e mantendo áudio, arquivos e regras críticas fora do WebView.

**C++20 + Qt Quick/QML permanece a alternativa mais forte.** Deve substituir a recomendação se o spike Tauri falhar em performance, acessibilidade ou integração e o custo/licenciamento do Qt for aceitável. Não se recomenda misturar Qt e Tauri.

## 2. Critérios de decisão

Os pesos refletem o pedido do mantenedor e são instrumentos de comparação, não benchmarks:

| Critério | Peso | O que significa |
|---|---:|---|
| Qualidade e velocidade de UX | 30% | Componentes, animação, tema, tipografia, acessibilidade, iteração visual e testes |
| Leveza e resposta | 20% | Cold start, memória, CPU, tamanho e latência dos controles |
| Simplicidade pessoal | 15% | Quantidade de infraestrutura e conhecimento necessários para manter o app sozinho |
| Windows e Linux | 10% | Suporte real, alta densidade, Wayland/X11 e distribuição |
| Integração desktop | 10% | Tray, single-instance, arquivos, SMTC/MPRIS e instaladores |
| Segurança e testabilidade | 10% | Memória, concorrência, isolamento, fakes e automação |
| Licença e distribuição | 5% | Obrigações, codecs e previsibilidade do bundle |

## 3. Opções avaliadas

Escala qualitativa: 1 (fraco) a 5 (forte). O total serve apenas para visualizar trade-offs; medições do M0 prevalecem.

| Opção | UX | Leveza | Simplicidade | Multiplataforma | Integração | Segurança/TDD | Licença | Leitura |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Rust + Tauri 2 + Svelte | 5 | 3,5 | 4 | 4 | 4,5 | 4,5 | 4 | Melhor equilíbrio para a prioridade de UX |
| C++20 + Qt Quick/QML | 5 | 4 | 4 | 5 | 5 | 2,5 | 2,5 | Solução mais coesa e madura, com custo de segurança/licença |
| Rust + Slint | 3,5 | 5 | 3,5 | 4 | 3 | 5 | 3,5 | Melhor footprint, maior risco de acabamento e integrações |
| Rust + Iced | 3 | 4,5 | 3 | 3,5 | 2,5 | 5 | 5 | Bom desenho Rust, ainda arriscado para consumer UI refinada |

### 3.1 Rust + Tauri 2 + Svelte

Vantagens:

- WebView2 no Windows e WebKitGTK no Linux, sem distribuir um Chromium completo;
- ecossistema de CSS, animação, tipografia, acessibilidade e teste visual maduro;
- APIs oficiais de tray, eventos, diálogo, single-instance e bundles; associações básicas existem, mas o contrato de Default Apps deste projeto exige customização do NSIS;
- Rust mantém arquivos, áudio, estado persistente e integrações sensíveis fora da UI;
- Svelte/Vite evita framework de servidor e reduz a camada frontend.

Riscos:

- dois ambientes de build e duas linguagens;
- IPC pode virar gargalo se usado como loop de áudio/progresso;
- consumo e startup maiores que UI Rust nativa;
- diferenças entre WebView2 e versões de WebKitGTK exigem teste Linux;
- superfície web precisa de CSP e capabilities mínimas.

Mitigação principal: a UI recebe snapshots em baixa frequência e interpola o progresso; nunca trafega PCM nem consulta o backend a cada frame.

O `bundle.fileAssociations` do Tauri não deve ser tratado como suficiente para FR-035: o template padrão pode escrever handlers por extensão, mas não materializa sozinho o registro completo de `Capabilities`/`RegisteredApplications` e o fluxo de escolha desejado. O M0 deve usar template/hook NSIS próprio, inspecionar o registro resultante e testar instalação, upgrade e remoção.

Fontes: [arquitetura do Tauri](https://v2.tauri.app/concept/architecture/), [WebViews por plataforma](https://v2.tauri.app/reference/webview-versions/), [tray](https://v2.tauri.app/learn/system-tray/), [single-instance](https://v2.tauri.app/plugin/single-instance/), [file associations](https://v2.tauri.app/reference/config/#bundleconfig) e [customização do instalador Windows](https://v2.tauri.app/distribute/windows-installer/).

### 3.2 C++20 + Qt Quick/QML

Vantagens:

- toolkit integrado e maduro para UI, acessibilidade, high DPI, tray, D-Bus e multimídia;
- Qt Quick usa scene graph acelerado e QML é adequado a interfaces animadas;
- Qt Multimedia com backend FFmpeg reduz muito o trabalho inicial de codecs, URL, posição e seek;
- acesso direto a Win32/WinRT e integração Linux consolidada.

Riscos:

- segurança de memória e concorrência depende de disciplina C++;
- bundle e runtime podem superar os orçamentos;
- link dinâmico é o caminho LGPL mais simples; link estático pode ser possível se o distribuidor cumprir relink e demais obrigações. Ambos exigem avisos/auditoria, e builds FFmpeg podem alterar obrigações;
- testar domínio isolado é possível, mas a separação tende a exigir mais vigilância;
- QML também introduz uma segunda linguagem.

O estilo `Basic` customizado é a base recomendável para um produto próprio. `FluentWinUI3` é referência, não dependência, enquanto estiver em desenvolvimento ou limitar customização.

Fontes: [Qt Quick Controls styles](https://doc.qt.io/qt-6/qtquickcontrols-styles.html), [performance do Qt Quick](https://doc.qt.io/qt-6/qtquick-performance.html), [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html), [QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html), [plataformas suportadas](https://doc.qt.io/qt-6/supported-platforms.html) e [obrigações LGPL resumidas pelo Qt](https://www.qt.io/development/open-source-lgpl-obligations).

### 3.3 Rust + Slint

Vantagens:

- UI declarativa compilada, sem WebView;
- renderer por GPU ou software e footprint potencialmente menor;
- integração direta com tipos e testes Rust;
- estilos e temas adequados a um spike nativo.

Riscos:

- ecossistema menor para componentes consumer, automação visual e integrações desktop;
- acabamento equivalente pode exigir mais componentes próprios;
- acessibilidade Windows/Linux deve ser provada com leitores de tela reais;
- a licença royalty-free/GPL/comercial precisa ser conferida para o modo de distribuição escolhido.

É o fallback correto se os únicos bloqueios do Tauri forem memória e startup.

Fontes: [backends e renderers](https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/), [estilos](https://docs.slint.dev/latest/docs/slint/reference/std-widgets/style/) e [licenciamento](https://slint.dev/terms-and-conditions).

### 3.4 Rust + Iced/egui

Iced possui arquitetura funcional atraente e licença permissiva, mas seu próprio projeto ainda alerta sobre evolução/experimentação. egui é excelente para ferramentas immediate-mode, porém não é a primeira escolha para um player consumer altamente polido. Nenhum dos dois reduz o risco total frente a Tauri, Qt ou Slint neste caso.

Fontes: [Iced](https://github.com/iced-rs/iced) e [egui](https://docs.rs/egui/latest/egui/).

## 4. Stack proposta por camada

| Camada | Escolha proposta | Observação |
|---|---|---|
| Linguagem central | Rust stable, edition 2024 | MSRV pinada no bootstrap; `unsafe` somente isolado, justificado e testado |
| Shell desktop | Tauri 2 | Capabilities mínimas; sem conteúdo remoto no WebView |
| UI | Svelte + TypeScript `strict` + Vite | SPA local, sem SvelteKit, SSR ou backend JS |
| Estilo | CSS variables/tokens + componentes locais | Sistema/claro/escuro; sem framework visual pesado no início |
| Áudio | Rodio/CPAL + Symphonia | Um único worker/owner do dispositivo; corpus decide formatos |
| Tags/propriedades | Lofty + `std::fs::metadata` | Cache por path, tamanho e mtime |
| Serialização | Serde + JSON | Esquema versionado e escrita atômica |
| Erros | `thiserror` no core; contexto no boundary | Mensagens de UI mapeadas, sem vazar detalhes internos |
| Logs | `tracing`, sink local rotativo futuro | Sem telemetria; paths e URLs redigidos por padrão |
| Windows | `windows`/windows-rs para SMTC e APIs específicas | Atrás de trait; registro de arquivos no instalador |
| Linux | `mpris-server` ou adapter D-Bus validado | Atrás do mesmo contrato de media controls |
| Empacotamento | Bundler Tauri, NSIS, `.deb` | AppImage após smoke de integração |

Não adicionar runtime assíncrono, ORM, banco, container de DI, gerador de código ou framework de estado global sem necessidade concreta. O runtime já trazido pelo shell deve ser usado apenas nas bordas assíncronas; o domínio permanece síncrono e determinístico.

## 5. Áudio e formatos

### 5.1 Escolha inicial

Rodio fornece API de reprodução, posição e seek; CPAL cuida da saída por plataforma e a versão Symphonia trazida por Rodio cuida dos contêineres/codecs habilitados. Lofty lê tags e propriedades sem precisar inicializar o player.

Fontes: [Rodio e `Player`](https://docs.rs/rodio/latest/rodio/struct.Player.html), [Symphonia](https://github.com/pdeljanov/Symphonia), [CPAL](https://github.com/RustAudio/cpal), [Lofty](https://github.com/Serial-ATA/lofty-rs) e [`FileProperties`](https://docs.rs/lofty/latest/lofty/file/struct.FileProperties.html).

Na fotografia consultada, Rodio 0.22.2 depende de Symphonia 0.5.5. Symphonia 0.6.1 era a release direta mais recente, mas adicioná-la separadamente não faria o `Player` do Rodio usá-la e poderia duplicar versões no bundle. O M0 deve pinçar uma única cadeia compatível e verificar `cargo tree -d -e features`; uma migração para Symphonia 0.6 exige nova versão do Rodio ou adapter direto deliberado. Lofty 0.25.x é outra fotografia, não pin de implementação. Rodio e Lofty usam `MIT OR Apache-2.0`, enquanto Symphonia usa `MPL-2.0`; as obrigações da MPL e a composição transitiva entram no gate antes da aceitação.

Há também um risco de tempo real: Rodio pode consumir o decoder/arquivo enquanto o callback CPAL solicita amostras. Portanto, “worker dono do engine” não prova que todo I/O saiu do callback. O spike deve medir underruns e stalls com arquivos locais/lentos. Se necessário, o adapter introduz prefetch/ring buffer limitado; se isso crescer demais, a escolha do backend é reavaliada.

### 5.2 Recursos a habilitar

Habilitar somente codecs testados, em vez de “all features”. O baseline desejado é:

- MP3;
- MP4/M4A com AAC-LC;
- FLAC;
- Ogg/Vorbis;
- WAV/PCM;
- ALAC e Opus como candidatos do spike.

M4A é contêiner, não codec. HE-AAC, DRM e arquivos incomuns não são prometidos. As associações do instalador só incluem linhas aprovadas na matriz de corpus.

### 5.3 Alternativas de engine

| Engine | Quando considerar | Custo |
|---|---|---|
| GStreamer | Streams HTTP, buffering e cobertura máxima virarem requisitos centrais | Runtime/plugins C, bundle e distribuição maiores |
| libmpv | URLs complexas e compatibilidade baseada em FFmpeg justificarem substituir o engine | API/configuração, FFmpeg, licenças e empacotamento |
| Qt Multimedia | Migração completa para Qt for decidida | Não coexistir com o stack Tauri |

Não empacotar dois motores de áudio. Uma troca substitui o adapter e requer testes de contrato e comparação do bundle.

## 6. Integrações de plataforma

### Windows

- tray pelo shell;
- instância única encaminhando argumentos para a janela existente;
- SMTC via `ISystemMediaTransportControlsInterop::GetForWindow(HWND)` em adapter próprio;
- ProgID, extensions, `Capabilities` e `RegisteredApplications` declarados por template/hook NSIS próprio, sem depender apenas de `bundle.fileAssociations`;
- botão “Definir como padrão” abre Configurações; não altera `UserChoice`;
- NSIS por usuário primeiro, assinatura quando houver distribuição pública.

Fontes: [SMTC para desktop](https://learn.microsoft.com/en-us/windows/win32/api/systemmediatransportcontrolsinterop/nf-systemmediatransportcontrolsinterop-isystemmediatransportcontrolsinterop-getforwindow), [modelo de associações](https://learn.microsoft.com/en-us/windows/compatibility/file-type-and-protocol-associations-model), [plataforma de apps padrão](https://learn.microsoft.com/en-us/windows/apps/develop/windows-integration/default-apps-platform) e [instalador Tauri no Windows](https://v2.tauri.app/distribute/windows-installer/).

### Linux

- MPRIS 2 por adapter D-Bus;
- `.desktop`, ícones e MIME types no pacote;
- detecção de tray; fallback nunca deixa processo invisível;
- `.deb` na distribuição de referência; AppImage como conveniência, não garantia de associação;
- build na base mínima suportada para controlar glibc e WebKitGTK.

Fontes: [MPRIS](https://specifications.freedesktop.org/mpris/latest/Player_Interface.html) e [MIME Apps](https://specifications.freedesktop.org/mime-apps/latest-single/).

## 7. YouTube e fontes remotas

Tecnicamente, mpv pode integrar `yt-dlp` e pedir somente áudio. Isso não torna o recurso adequado ao produto. A resolução é frágil a mudanças do site, adiciona sidecar atualizável e pode conflitar com termos e políticas que restringem separação de áudio e reprodução em background.

Decisão recomendada:

1. não prometer YouTube no MVP;
2. manter `MediaSourceResolver` como porta, começando apenas com `LocalFile`;
3. avaliar primeiro streams HTTP/podcasts autorizados;
4. exigir spike jurídico/técnico e ADR antes de integrar YouTube;
5. nunca abrir conteúdo remoto dentro do WebView privilegiado.

Fontes: [políticas para desenvolvedores do YouTube](https://developers.google.com/youtube/terms/developer-policies-guide), [termos do YouTube](https://www.youtube.com/static?template=terms), [manual do mpv](https://mpv.io/manual/stable/) e [yt-dlp](https://github.com/yt-dlp/yt-dlp).

## 8. Licenças e distribuição

Antes de aceitar a stack:

- registrar a licença de toda dependência direta e transitiva;
- validar a licença royalty-free do Slint apenas se ele virar candidato final;
- se Qt for escolhido, usar preferencialmente link dinâmico e cumprir LGPL; link estático só após validar mecanismo de relink e todas as demais obrigações;
- se FFmpeg/mpv/GStreamer entrar, registrar opções do build e componentes GPL/nonfree;
- verificar patentes/royalties de codecs para os territórios de distribuição;
- manter `LICENSES/` e notices no pacote quando a implementação começar;
- usar `cargo-deny` ou ferramenta equivalente como gate, sem tratar relatório automático como parecer jurídico.

Referência: [checklist de licença do FFmpeg](https://ffmpeg.org/legal.html).

## 9. Spike de decisão M0

A stack só passa de `Proposto` para `Aceito` quando o vertical slice comprovar o fluxo Tier 1 no Windows e ao menos build/smoke Tier 2 no Linux de referência:

1. janela Tauri/Svelte com estado vazio, tocando, painel de fila, tema e slider acessível;
2. MP3 e M4A/AAC tocando, pausando e buscando com Rodio/Symphonia;
3. metadata e capa lidas sem bloquear a UI;
4. tray, fechar/restaurar, single-instance e abertura de path com Unicode;
5. nomes/estados corretos em NVDA e escala de 200%;
6. build Release e instalador NSIS reproduzíveis;
7. cold start, working set, CPU e latência dentro das metas ou com desvio aceito;
8. CSP/capabilities revisadas e nenhum acesso remoto implícito.
9. build Linux reproduzível, abertura/playback de fixture e smoke de integração disponível no ambiente de referência.

O spike de áudio também deve registrar `cargo tree -d -e features`, provar que não há duas versões Symphonia acidentais e medir underruns/stalls do caminho decoder → callback. O spike de associação deve demonstrar `Capabilities`/`RegisteredApplications`, ausência de tomada silenciosa de `UserChoice` e limpeza escopada no uninstall.

### Fallbacks predefinidos

- falha apenas em RAM/startup do WebView → spike equivalente em Slint;
- falha de codecs, integração ou acessibilidade combinada → spike C++/Qt Quick + Qt Multimedia;
- nenhum candidato atende → revisar orçamentos/requisitos antes de adicionar outro framework.

## 10. Decisões que ainda pertencem ao mantenedor

- aceitar Rust/Tauri/Svelte como direção do M0;
- escolher licença e se o repositório será público;
- definir nome e identidade visual;
- confirmar suporte mínimo a Windows 10 além do ciclo do Qt/WebView2;
- escolher a distribuição Linux de referência;
- decidir se a versão portátil é requisito ou somente conveniência;
- aprovar ou reajustar os orçamentos após as primeiras medições.

# Análise e recomendação de stack

**Estado:** recomendação técnica; decisão ainda não aceita  
**Data da pesquisa:** 2026-08-23  
**ADR relacionado:** `decisions/0001-language-and-ui-stack.md`

## 1. Resumo executivo

O M0 compara dois candidatos, sem aceitar antecipadamente nenhum deles:

1. **hipótese principal de desempenho:** C++20 + Qt 6 Quick/QML, estilo Basic customizado, Qt Multimedia/FFmpeg e TagLib;
2. **desafiante A/B:** Rust + Tauri 2 + Svelte/Vite, Rodio/CPAL/Symphonia e Lofty.

Qt é a hipótese principal porque a prioridade Windows-first e os budgets estritos tornam menor cold start e menor piso de memória especialmente valiosos. Isso não constitui benchmark: Qt Quick também possui engine QML/JavaScript, scene graph e módulos distribuídos, enquanto Qt Multimedia/FFmpeg pode elevar memória e pacote. Tauri reutiliza o WebView do sistema e favorece segurança de memória e velocidade de iteração visual, mas sua árvore multiprocesso pode elevar o piso de RAM.

O primeiro ensaio isola UI/shell com o mesmo recorte visual e backend falso. O segundo compara áudio e metadata no mesmo corpus. Somente combinações viáveis são integradas e medidas novamente. UX, acessibilidade e manutenção para uma pessoa são gates, não desempates cosméticos.

Não se recomenda misturar Qt e Tauri nem distribuir dois motores de áudio. A escolha final continua `Proposta` até o ADR registrar números reproduzíveis nos termos de NFR-STACK-001–006.

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
| Rust + Tauri 2 + Svelte | 5 | 3,5 | 4 | 4 | 4,5 | 4,5 | 4 | Desafiante forte em UX, segurança e iteração |
| C++20 + Qt Quick/QML | 5 | 4 | 4 | 5 | 5 | 2,5 | 2,5 | Hipótese principal de desempenho e integração |
| Rust + Slint | 3,5 | 5 | 3,5 | 4 | 3 | 5 | 3,5 | Melhor footprint, maior risco de acabamento e integrações |
| Rust + Iced | 3 | 4,5 | 3 | 3,5 | 2,5 | 5 | 5 | Bom desenho Rust, ainda arriscado para consumer UI refinada |

Com os pesos publicados, Tauri (4,300) e Qt (4,275) ficam separados por apenas 0,025 ponto, muito abaixo da precisão dessa avaliação subjetiva. A ordem do M0 é uma hipótese de medição, não a conclusão da matriz.

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

Fontes: [arquitetura do Tauri](https://v2.tauri.app/concept/architecture/), [modelo de processos do Tauri](https://v2.tauri.app/concept/process-model/), [WebViews por plataforma](https://v2.tauri.app/reference/webview-versions/), [modelo de processos do WebView2](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/process-model), [tray](https://v2.tauri.app/learn/system-tray/), [single-instance](https://v2.tauri.app/plugin/single-instance/), [file associations](https://v2.tauri.app/reference/config/#bundleconfig) e [customização do instalador Windows](https://v2.tauri.app/distribute/windows-installer/).

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

Fontes: [Qt Quick Controls styles](https://doc.qt.io/qt-6/qtquickcontrols-styles.html), [performance do Qt Quick](https://doc.qt.io/qt-6/qtquick-performance.html), [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html), [QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html), [deployment no Windows](https://doc.qt.io/qt-6/windows-deployment.html), [plataformas suportadas](https://doc.qt.io/qt-6/supported-platforms.html) e [obrigações LGPL resumidas pelo Qt](https://www.qt.io/development/open-source-lgpl-obligations).

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

Permanece alternativa de contingência se nenhum dos dois candidatos principais atender aos budgets e o problema comprovado estiver concentrado no footprint da UI.

Fontes: [backends e renderers](https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/), [estilos](https://docs.slint.dev/latest/docs/slint/reference/std-widgets/style/) e [licenciamento](https://slint.dev/terms-and-conditions).

### 3.4 Rust + Iced/egui

Iced possui arquitetura funcional atraente e licença permissiva, mas seu próprio projeto ainda alerta sobre evolução/experimentação. egui é excelente para ferramentas immediate-mode, porém não é a primeira escolha para um player consumer altamente polido. Nenhum dos dois reduz o risco total frente a Tauri, Qt ou Slint neste caso.

Fontes: [Iced](https://github.com/iced-rs/iced) e [egui](https://docs.rs/egui/latest/egui/).

## 4. Baselines candidatos por camada

| Camada | Hipótese Qt | Desafiante Tauri | Regra comum |
|---|---|---|---|
| Linguagem central | C++20 | Rust stable, edition pinada | domínio síncrono, determinístico e independente do toolkit |
| Shell/UI | Qt 6 Quick/QML | Tauri 2 + Svelte/TypeScript/Vite | assets locais, estados e roteiro equivalentes |
| Estilo | Qt Quick Controls Basic customizado | tokens CSS + componentes locais | sistema/claro/escuro, foco e acessibilidade |
| Áudio | Qt Multimedia com backend FFmpeg | Rodio/CPAL + Symphonia | um único owner; corpus decide formatos |
| Tags/propriedades | TagLib + filesystem | Lofty + filesystem | trabalho cancelável, limites e cache equivalentes |
| Persistência | JSON versionado por adapter C++ | Serde + JSON | escrita atômica e esquema comum |
| Windows | Qt/Win32/C++/WinRT quando necessário | Tauri + windows-rs quando necessário | portas iguais para tray, SMTC e defaults |
| Linux | Qt/D-Bus | Tauri + adapter D-Bus | MPRIS, MIME e fallback visível de tray |
| Testes | Catch2 e Qt Test | `cargo test`, Vitest/Testing Library | contratos e jornadas observáveis iguais |
| Empacotamento | CMake/Qt deployment + NSIS; `.deb` | Bundler Tauri + NSIS; `.deb` | footprint offline e licenças medidos separadamente |

No candidato Qt, `QObject`, tipos Qt e QML não entram no domínio nem nos casos de uso. No candidato Tauri, DTOs/commands e tipos do WebView também não entram no núcleo. Ambos implementam as mesmas portas conceituais e fakes para tornar a comparação sobre produto, não sobre regras diferentes.

Não adicionar runtime assíncrono, ORM, banco, container de DI, gerador de código ou framework de estado global sem necessidade concreta. O projeto começa como uma única unidade de build e distribuição, com módulos internos; uma stack substitui a outra após a decisão.

## 5. Áudio e formatos

### 5.1 Comparação inicial

No candidato Qt, `QMediaPlayer`/Qt Multimedia usa o backend FFmpeg padrão para reprodução, duração e seek; TagLib lê tags e propriedades sem iniciar o player. O spike registra plugins realmente distribuídos, opções/licença do FFmpeg e obrigações de link dinâmico do Qt. Fontes: [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html), [QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html), [TagLib](https://taglib.org/) e [checklist do FFmpeg](https://ffmpeg.org/legal.html).

No candidato Tauri, Rodio fornece a API de reprodução/seek, CPAL cuida da saída e a versão Symphonia resolvida por Rodio cuida dos codecs habilitados; Lofty lê metadata. O M0 não adiciona uma segunda Symphonia sem decisão e registra `cargo tree -d -e features`. Fontes: [Rodio e `Player`](https://docs.rs/rodio/latest/rodio/struct.Player.html), [Symphonia](https://github.com/pdeljanov/Symphonia), [CPAL](https://github.com/RustAudio/cpal), [Lofty](https://github.com/Serial-ATA/lofty-rs) e [`FileProperties`](https://docs.rs/lofty/latest/lofty/file/struct.FileProperties.html).

Os dois caminhos executam a mesma suite `PlaybackBackend` e o mesmo corpus. A comparação separa open-to-audible, seek-to-audible, duração/precisão, CPU, memória, entradas inválidas e underruns. Em qualquer candidato, trabalho bloqueante no caminho de tempo real exige prefetch/ring buffer limitado ou rejeição do backend.

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
| FFmpeg direto | DSP ou controle de pipeline não atendido pelos candidatos virar requisito | API extensa, concorrência, segurança e distribuição próprias |

Qt Multimedia e Rodio são candidatos mutuamente exclusivos do M0, não dois engines do produto. A decisão mantém apenas o vencedor; qualquer troca posterior substitui o adapter e exige testes de contrato e nova comparação do bundle.

## 6. Integrações de plataforma

### Windows

- tray e close/restore pelo shell candidato;
- instância única encaminhando argumentos para a janela existente, por mecanismo escopado ao usuário;
- SMTC via `ISystemMediaTransportControlsInterop::GetForWindow(HWND)` em adapter próprio C++/WinRT ou windows-rs;
- ProgID, extensions, `Capabilities` e `RegisteredApplications` declarados por template/hook NSIS próprio, sem depender apenas de `bundle.fileAssociations`;
- botão “Abrir Aplicativos Padrão” usa uma URI interna fixa para abrir Configurações; não aceita URI da UI, não altera `UserChoice` e o handoff não substitui evidência de ProgID/capabilities/instalador;
- NSIS por usuário como baseline experimental, com Qt deployment ou bundle Tauri conforme o candidato; a tecnologia final é decidida no M0 e recebe assinatura quando houver distribuição pública.

Fontes: [SMTC para desktop](https://learn.microsoft.com/en-us/windows/win32/api/systemmediatransportcontrolsinterop/nf-systemmediatransportcontrolsinterop-isystemmediatransportcontrolsinterop-getforwindow), [modelo de associações](https://learn.microsoft.com/en-us/windows/compatibility/file-type-and-protocol-associations-model), [plataforma de apps padrão](https://learn.microsoft.com/en-us/windows/apps/develop/windows-integration/default-apps-platform) e [instalador Tauri no Windows](https://v2.tauri.app/distribute/windows-installer/).

### Linux

- MPRIS 2 por adapter D-Bus;
- `.desktop`, ícones e MIME types no pacote;
- detecção de tray; fallback nunca deixa processo invisível;
- `.deb` na distribuição de referência; AppImage como conveniência, não garantia de associação;
- build na base mínima suportada para controlar glibc e as dependências Qt ou WebKitGTK do candidato.

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

A stack só passa de `Proposto` para `Aceito` após três comparações controladas, detalhadas no roadmap e em NFR-STACK-001–006:

1. **shell/UI:** Qt Quick e Tauri/Svelte implementam o mesmo recorte com backend falso, assets e roteiro comuns; áudio e rede reais ficam fora;
2. **áudio/metadata:** Qt Multimedia/FFmpeg/TagLib e Rodio/Symphonia/Lofty executam a mesma suite de contrato e corpus sem UI final;
3. **combinação:** somente candidatos viáveis integram shell, áudio e o mínimo de plataforma para revalidar budgets e UX.

Para cada candidato, o gate exige:

- fluxo Tier 1 no Windows e build/smoke Tier 2 no Linux de referência;
- estado vazio/tocando/erro, fila, preferências, temas, slider e tray equivalentes;
- MP3 e M4A/AAC com play, pause, duração e seek pelo mesmo corpus;
- metadata/capa sem bloquear a UI e entrada inválida sem crash reproduzível;
- teclado, NVDA, alto contraste, movimento reduzido e escala 100/150/200%;
- build Release e pacote de teste reproduzíveis;
- cold start, árvore completa de processos, CPU, memória e latência dentro dos budgets vigentes ou com desvio aprovado;
- riscos e licenças específicos registrados: Qt/FFmpeg/TagLib no ramo Qt; capabilities/CSP e árvore Rust/JS no ramo Tauri.

O protocolo e as tolerâncias são congelados antes da coleta. Resultados dentro da tolerância são empate e deslocam a decisão para UX comprovada, acessibilidade, manutenção pessoal, segurança e licenciamento. O spike de associação demonstra `Capabilities`/`RegisteredApplications`, ausência de tomada silenciosa de `UserChoice` e limpeza escopada no uninstall.

### Contingências predefinidas

- apenas um candidato atende → integrar e revalidar esse candidato antes de aceitar;
- os dois atendem e empatam → aplicar os critérios não numéricos registrados, sem inventar precisão;
- nenhum atende por footprint da UI → considerar spike Rust + Slint equivalente;
- nenhum candidato atende formatos ou plataforma → revisar backend, orçamento ou requisito antes de adicionar framework.

## 10. Decisões que ainda pertencem ao mantenedor

- escolher licença e se o repositório será público;
- definir nome e identidade visual;
- decidir se a versão portátil é requisito ou somente conveniência;
- aprovar ou reajustar os orçamentos após as primeiras medições.

A execução M0 foi autorizada em D0.9. Windows 11 Tier 1, Windows 10 legado e Ubuntu 24.04 LTS Tier 2 foram congelados no [ambiente M0-E1](../m0/environment.md), sem aceitar Qt ou Tauri.

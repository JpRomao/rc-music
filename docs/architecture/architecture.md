# Arquitetura do sistema

**Estado:** arquitetura vigente da stack aceita; núcleo e composição gráfica promovidos, cobertura restante e gates M1 pendentes
**Stack do produto:** C++20 + Qt 6 Quick/QML, Qt Multimedia/FFmpeg e TagLib
**Última revisão:** 2026-11-05

O [ADR-0005](decisions/0005-windows-product-stack.md) registra a escolha explícita do mantenedor, sem exigir A/B antes do desenvolvimento Windows. As invariantes de domínio, portas, concorrência, privacidade local e integração por capabilities permanecem independentes do toolkit. O mapeamento Qt passa a ser a direção do produto; Tauri permanece somente como alternativa histórica, não dependência a implementar. Aceitação técnica não encerra migração, benchmarks, acessibilidade nativa, Linux ou distribuição.

## 1. Objetivos arquiteturais

A arquitetura deve:

- manter a thread de UI livre de trabalho bloqueante e impedir trabalho adicional do código próprio no callback de saída;
- permitir TDD do comportamento sem toolkit visual, dispositivo ou arquivo real;
- isolar Windows, Linux, codecs, metadados, armazenamento e fontes remotas;
- preservar um único estado autoritativo de reprodução;
- começar em um pacote simples e crescer somente quando necessário;
- tornar falhas locais e remotas recuperáveis;
- manter arquivos locais independentes de rede;
- permitir substituir UI ou backend de áudio sem reescrever o domínio.

Não é objetivo criar framework de plugins, microserviços, servidor local público, banco ou sincronização.

O [desenho de aquisição YouTube](decisions/0017-youtube-audio-and-lyrics-acquisition.md) mantém ingresso próprio separado da mídia direta da extensão principal. `YoutubeAudioResolution` interpreta a projeção JSON limitada de um único áudio AAC/M4A por HTTPS, verifica identidade, restrições e headers, extrai o prazo do stream e constrói argv fixos; não executa ferramentas nem consulta letras. `YoutubeResolverSession` coordena um processo por job e só cria `ResolvedYoutubeAudioCapability` após start aceito, exit 0, EOF dos pipes e dados válidos ainda utilizáveis. JSON, ID de vídeo e URL são dados, não autoridade de aquisição.

A capability imutável conserva o job e a URL assinada somente em memória. O ingresso da principal mantém seus bloqueios YouTube/Googlevideo; o caminho específico exige a capability correspondente e reaplica política de HTTPS, host, DNS/IP e redirects. `WindowsYoutubeResolverProcess` recebe preparação C++ confiável com configuração explícita e lease do runtime, reutiliza `WindowsDownloadProcess` e mantém NoChild. Start recusado, cancelamento ou destruição não substituem ACK; incerteza conserva estado/lease em vez de autorizar limpeza. Os testes desse adapter usam exclusivamente o helper local de processos.

O [stdin interativo opt-in](../youtube-audio-and-lyrics.md#entrada-interativa-supervisionada) reutiliza o pipe overlapped do supervisor para diálogo futuro com o broker JSC. Fila de até 256 KiB e quota acumulada de até 64 MiB são limites distintos; somente polling escreve, backpressure é atômico e EOF segue os dados aceitos. Saída natural/cancelamento encerram a admissão e conservam a confirmação física de processo e pipes. O modo padrão continua one-shot; não se altera NoChild nem se liga o protocolo a fontes remotas no app.

O incremento de `WindowsYoutubeRuntime` prepara, somente na worker, um inventário privado declarado integralmente por configuração C++ revisada: roles únicos `Extractor`, `Node`, `EjsCore` e `EjsLibrary`, mais arquivos `Support`. Não há discovery ou busca no `PATH`. O contrato limita o conjunto a 256 arquivos/512 MiB, cada arquivo a 128 MiB e cada caminho relativo a 16 segmentos/1024 unidades UTF-16. Reutiliza pins por handle/hash/cópia, mantém subdiretórios no inventário fechado e expõe conferência `unchanged()`; o lease deve durar até o ACK nativo/EOF de qualquer futuro usuário. O [registro do incremento](../youtube-audio-and-lyrics.md#inventário-privado-do-runtime) conserva Red/Green, 4/4 CTest e 184 casos Qt no fechamento proporcional. A mecânica em bytes sintéticos/TEMP não qualifica uma distribuição real. Essa classe não produz configuração de processo ou autoridade de lançamento; a coordenação seguinte tem contrato separado no broker abaixo, e Node descendente do CLI padrão continua recusado.

O [broker de desafios JavaScript](../youtube-audio-and-lyrics.md#broker-de-desafios-javascript) compõe um entrypoint Python próprio com a API pública JSC do yt-dlp `2026.03.17` e recursos EJS `0.8.0`. Sem CLI stock, descoberta de plugins, runtime detection ou lançamento de Node pelo extractor, o pai supervisiona extractor/Node irmãos, cada um com Job=1/NoChild. `YoutubeBrokerProtocol` valida framing binário LE/JSON estrito, sequência, tipos, limites e correlação; o provider Python só recebe um ID de vídeo e devolve projeção limitada. Dados/scripts não concedem autoridade de aquisição ou publicação. Codec C++ passou 96 Qt e provider 34 métodos falsos; supervisão nativa passou 36/36 Qt e a seleção final 4/4 CTest/239 Qt, sem falhas/skips.

O contrato de `WindowsYoutubeBrokerProcess` exige Node exit/EOF reconhecidos antes de encaminhar response e todos os ACKs antes de publicar projeção à sessão existente. `SendingResponse` acompanha chunks aceitos para stdin, sem afirmar escrita física/consumo. Buffers/handles overlapped e lease do runtime permanecem até ACK, com drain em erro/cancelamento. A prova nativa usa helpers reais com extractor/Node/EJS falsos; não qualifica runtime real, host instalado, sandbox de filesystem/rede ou privacidade/distribuição. Produção continua OFF, sem novos perfis AppContainer/WFP ou retomada dos gates anteriores.

`makeWindowsYoutubeAudioExecution` encadeia resolução e tarefa de destino na worker privada existente de `WindowsDownloadExecution`. A tarefa seguinte recebe a capability após a resolução reconhecida; cancelamento/timeout e encerramento drenam os donos antes de publicar terminal. Não se acrescenta uma URL assinada ao checkpoint público nem se troca a composição local do player por um executor de comandos. O [registro do recorte](../youtube-audio-and-lyrics.md) separa esses componentes de runtime EJS/Node autenticado e compatível com NoChild, host instalado, aquisição no app normal e cadeia Letras → LRCLIB ainda pendentes. Não há parser Letras ou novo provedor de letras implementado por essa composição.

O incremento de admissão acrescenta `DownloadItem.youtube`, contexto opcional com `YoutubeAudioItem` completo: video ID, título, artista, página Letras e opção de buscar letra. É dado de identidade e quota, não autorização. Validação, igualdade e fingerprint consideram o contexto integral, preservando o algoritmo v1 dos pedidos legados sem contexto. Os metadados completos não são gravados como payload de pedido: o recibo conserva fingerprint/contagem e IDs; replay explícito após reiniciar não retoma a execução de áudio automaticamente.

`YoutubeAudioAdmission` liga `AsyncYoutubeAudioSubmission` ao mesmo controlador de admissão durável da mídia direta. `NativeYoutubeAudioSession` é um observador separado: exige origem exata fornecida por transporte/configuração confiáveis e limita o frame a 64 KiB antes do corpo; Pending não produz ACK. EOF, reentrada, exceção ou encerramento do observador não cancelam nem repetem o trabalho admitido. `makeWindowsMediaAndYoutubeExecution` compartilha executor/fila e seleciona a rota de resolução somente para contexto tipado consistente dentro dessa composição confiável; os construtores e ingresso legados não ganham YouTube por um campo do payload. `WindowsPlayerDownloads` expõe os dois ports de admissão compartilhados. A jornada instrumentada cobre frame → checkpoint Saved → resolver/destino falsos → importação com Session Saved sem autoplay; a evidência desse incremento está no [guia YouTube](../youtube-audio-and-lyrics.md#evidência-da-admissão-durável-e-fila-compartilhada). Host IPC instalado e identidade real do browser/runtime permanecem gates próprios.

As [letras locais e consulta online explícita](decisions/0013-lyrics-and-online-lookup.md) usam `LyricsDocument`/parser e seleção por posição no domínio puro. `QtLyricsService` limita a leitura de um `.lrc` irmão a um worker ativo e uma substituição pendente; a busca de texto usa Qt Network com endpoint HTTPS fixo, prazo, limites e revogação de respostas antigas. `LyricsBinding` projeta texto/estado e usa a posição confirmada do presenter. O bootstrap inclui sua drenagem na saída e rearma a leitura quando a saída é cancelada. QML apresenta a letra e solicita confirmação dos metadados; não lê arquivos nem acessa rede diretamente.

A evolução de [versões de letras](decisions/0016-lyrics-variants.md) mantém formas original/romanizada por path exato do áudio, com domínio puro, porta de armazenamento e JSON limitado na configuração. Texto colado ou letra carregada só é persistido mediante ação explícita; a seleção usa a posição confirmada existente. O arquivo de mídia e o `.lrc` irmão não são alterados. Trabalho de arquivos pertence a um job fora da UI; resultados antigos e falhas de confirmação conservam identidade/candidato em vez de atingir outra faixa. Isso não adiciona scraping, provedor de MP3 ou LLM.

A [associação de letras ao áudio adquirido](../download-lyrics.md) começa somente com publicação reconhecida e contexto original vivo, oferecidos por `DownloadService::publishedYoutubeAudio`. O owner opcional verifica `fetch_lyrics`, oferece uma revisão por vez e mantém históricos recuperados inertes. `DownloadLyricsLookup` possui um `QtLyricsService` exclusivo: `setLookupTrack` não abre áudio/`.lrc`, e a busca LRCLIB conserva cooldown/revogação. Letras permanece explicitamente indisponível, sem parser ou requisição a esse provedor. Falhas de letra não alteram aquisição/importação de áudio.

`DownloadLyricsBinding` projeta candidatos sincronizados/texto distintos e exige nome/forma explícitos; idioma/escrita não são inferidos. Backend, alvo, metadados e `requestToken` revogam ações antigas. `LyricsBinding::appendAcquiredLyrics` usa o mesmo writer manual, reservado durante load → merge puro → save; candidato/ticket permanecem após falha, e retry de gravação não faz HTTP ou novo download. Catálogo schema 1, path exato e versões existentes são preservados, sem CAS multiprocesso. Guardar não troca reprodução ou seleção; **Versões** permite escolher o texto depois. O diálogo no painel de downloads e a composição são opcionais; downloads normais continuam OFF. O [registro proporcional](../download-lyrics.md#evidência-deste-incremento) registra os checks e distingue catálogo TEMP real de rede/resolver/publicação falsos.

O [cliente Lidarr opcional](decisions/0014-lidarr-integration.md) usa Qt Network no adapter, com conexão e credenciais fornecidas explicitamente e mantidas em memória. `LidarrBinding` projeta catálogo, escolhas de pasta/perfis e resultados tipados; QML separa consulta de confirmação para adicionar/buscar um álbum. O servidor gerencia seus downloads. A integração é comum às edições Lite/Completa e não fornece bytes ou caminhos importáveis ao núcleo local.

As [playlists nomeadas](decisions/0015-named-playlists.md) usam `PlaylistLibrary` e `PlaylistStore` separados da sessão de reprodução. O catálogo preserva IDs próprios, ordem e duplicatas; a apresentação prepara uma alteração e só a publica após Saved. `QtJsonPlaylistStore` lê/grava um documento JSON local com backup/lock, em um job Qt Concurrent por binding. A composição do app fornece callbacks tipados para copiar a fila e acrescentar um lote sem seleção/autoplay; os mesmos gates e a transação do append local já usado pela importação são reutilizados. A UI não abre documentos ou arquivos de áudio.

## 2. Contexto

```text
                    ┌──────────────────────────────┐
 usuário / teclado ─►                              ├──► dispositivo de áudio
 shell / tray ──────►       Desktop Player         ├──► arquivos locais
 SMTC / MPRIS ──────►                              ├──► preferências/sessão
                    └──────────────┬───────────────┘
                                   │ porta futura, desativada no MVP
                                   ▼
                           resolver de fonte remota
```

O aplicativo é uma instância desktop única do ponto de vista do usuário. A quantidade de processos internos do shell é detalhe medido, não assumido. A camada visual somente apresenta estado e envia intenções; não hospeda regras de domínio nem abre conteúdo remoto.

O módulo opcional [de apresentação dos downloads](../download-jobs.md#painel-compartilhado-e-preparação-de-saída) é compartilhado entre Lite e Completa: binding/timer/model e painel QML não dependem de engine de navegação. O presenter usa preparação de saída C++ tipada, separada de source-drain e da persistência local; aprovação exige parada física e Saved ou abandono explícito confirmado. O [bootstrap opcional](../player-downloads-bootstrap.md) compõe a mesma `main` e UI de produção por um perfil C++ explícito, ausente por padrão. Sua jornada local usa stores/workers, conversão/publicação e ingresso reais, com transporte por helper de fixtures. O [host e IPC Native Messaging Windows](../browser-extension.md#host-e-transporte-native-messaging-windows) conservam identidade de produção vazia/desabilitada. O player distribuído continua sem ingresso remoto ativo; identidade instalada, ferramentas/sandbox e distribuição mantêm seus gates.

A [reserva Windows do dono dos downloads](../browser-extension.md#exclusividade-cooperativa-do-dono) pertence ao adapter do servidor, não ao domínio ou à eleição `OpenSources`. A chave por SID do usuário/endpoint case-insensitive independe de origem, edição, perfil e CWD; criação atômica recusa concorrentes cooperativos antes do `listen`. Falha de `listen` libera a tentativa; sucesso retém a reserva até destruição do servidor, inclusive após ingresso `stopped`. A composição deve conservar servidor, serviço/controller e stores/workers vivos na ordem de drenagem, sem interpretar encerramento do socket como encerramento do dono. Não é autenticação, exclusividade contra pipe hostil ou escolha de identidade instalada entre Lite/Completa.

### Composição opcional de downloads

O [perfil relativo ao pacote](../packaged-download-profile.md) reutiliza `WindowsPlayerDownloadProfile` como protótipo compilado. O helper resolve somente as fontes pelo diretório real da imagem e obtém o diretório Windows por API, sem descoberta em CWD/ambiente ou I/O na UI. A receita de build produz cópias e header pelo mesmo inventário; os workers conservam a autenticação e seus leases. Um pacote direto omite o canal YouTube compilado quando não inclui o resolver, preservando a integração principal. O candidato das ferramentas permanece separado do runtime Qt e do instalador.

O [storage opt-in do bootstrap](../player-download-storage.md) deriva um layout fixo do diretório de configuração existente e o prepara somente no load do worker de checkpoint. O wrapper conserva retry explícito após falha, sem reprovisionar depois do primeiro sucesso ou reparar objetos existentes. O default continua usando os caminhos já provisionados. Nenhuma página ou documento persistido escolhe essas pastas; Saved, ownership, importação e drenagem mantêm seus contratos anteriores.

O [gerador de manifest Native YouTube](../youtube-native-manifest.md) é uma receita de empacotamento separada: confere recibo de build confiável, ID e host e escreve somente dois JSONs candidatos. Não fornece configuração ao domínio ou ao player, não monta runtime e não registra componentes. O candidato de fontes continua com identidade pendente até a escolha real da extensão.

O [perfil compilado dos hosts](../native-host-build-profile.md) tem uma fonte CMake para os headers de cada host, o header C++ compartilhado e seus recibos após o link. O provider do player pode obter os perfis compilados pelo helper; canais ativos divergentes são recusados antes da fábrica de execução. Os recibos JSON servem ao empacotamento, sem serem carregados como autoridade no produto. Origem exata, pipe e allowsets coerentes não atestam Brave/publisher ou segurança do runtime.

A [composição opcional de downloads do player](../download-jobs.md#composição-opcional-do-player-windows) possui execução, importador `Session`, worker de checkpoint, serviço, controlador de admissão e binding no mesmo proprietário. A UI e os adaptadores continuam usando os contratos existentes; o proprietário expõe os ports assíncronos principal/YouTube sobre o mesmo controller. O journal pertence à tarefa preparada no worker. Cada variante liga exatamente um ABI do presenter, mantendo cenários fora da biblioteca de produção. `makeWindowsConfiguredDownloadExecution` captura perfis próprios e reúne o destino preparado com o broker opcional; a UI não prepara arquivos ou descobre ferramentas. O bootstrap preserva a sessão antes do move para o presenter e conserva o owner entre o lifetime do engine QML e seus writers emprestados. Header de build vazio não cria owner ou ingresso; identidade instalada, recuperação e sandbox conservam seus gates.

### Seleção de mídia no navegador experimental

A [composição WebView2 opt-in](../embedded-browser.md) usa o [coletor compartilhado](../browser-extension.md#coletor-compartilhado-com-a-navegação-experimental) como recurso próprio, em chamada explícita e sem globals ou ponte da página. A detecção nativa atual alcança documento principal e Shadow DOM aberto; frames permanecem na cobertura da extensão isolada e exigem lifecycle nativo antes de entrarem nessa composição. Lite mantém engine e esse módulo fora do grafo padrão.

[`rc_music_media_data`](../../cmake/RcMusicMediaData.cmake) concentra o preflight Unicode/JSON estrito e a policy de URL canônica, extraídos do Native codec sem alterar seu contrato. É biblioteca Qt Core sem serviço de jobs, transporte, execução ou UI. `NativeDownloadCodec` e `BrowserMediaSnapshotCodec` aplicam seus schemas/limites próprios sobre esses helpers; o segundo recebe somente dados não autenticados do script, preservando page URL/fragmento para comparação exata e URLs de mídia assinadas sem renormalização.

`BrowserMediaSelection`, na apresentação, coordena tokens fornecidos pelo host, invalidação por navegação, seleção de até 20 itens e confirmação por varredura fresca das tuplas escolhidas. Seu estado puro não inicia script, rede, download ou importação. O painel Qt Widgets projeta esse estado, enquanto o adapter Win32/WebView2 controla HWND, eventos, chamada assíncrona e lifetime do engine. O limite de chamadas nativas pendentes pertence ao adapter/painel, separado do token lógico da seleção. Nenhuma regra de engine ou Qt entra no domínio; o ingresso/download protegido de produção continua desligado.

## 3. Componentes do produto e alternativa histórica

O fluxo invariante é:

`ui → boundary/presenter → application → domain/ports ← adapters`

### Produto: C++20 + Qt 6 Quick/QML

```text
QML / Qt Quick UI
        │ propriedades, sinais e comandos tipados
        ▼
Boundary / Presenter C++ com Qt
        │ tipos e DTOs C++ sem Qt
        ▼
Application C++ puro → Domain/Ports C++ puro ← Adapters
                                               ├─ Qt Multimedia/FFmpeg
                                               ├─ TagLib/filesystem
                                               ├─ persistência atômica
                                               └─ Qt/plataforma Windows/Linux
```

`QObject`, tipos QML e detalhes de sinais/slots terminam no boundary/presenter ou nos adapters. `domain`, `application` e `ports` não importam headers Qt e permanecem testáveis como C++ puro.

### Alternativa histórica não adotada: Rust + Tauri 2 + Svelte

```text
┌─────────────────────────────────────────────────────────────────┐
│ UI Svelte                                                       │
│ Views · componentes · acessibilidade · interpolação visual      │
└──────────────────────────────┬──────────────────────────────────┘
                               │ comandos tipados / snapshots
┌──────────────────────────────▼──────────────────────────────────┐
│ Boundary IPC / Presenter                                        │
│ valida entrada · mapeia erros · serializa DTOs · limita eventos │
└──────────────────────────────┬──────────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────────┐
│ Application                                                     │
│ PlayerController · QueueService · SessionCoordinator            │
└──────────────────────────────┬──────────────────────────────────┘
                               │ usa
┌──────────────────────────────▼──────────────────────────────────┐
│ Domain                                                          │
│ PlaybackState · Queue · Track · Preferences · invariantes       │
└──────────────────────────────┬──────────────────────────────────┘
                               │ contratos
┌──────────────────────────────▼──────────────────────────────────┐
│ Ports                                                           │
│ Playback · Metadata · Session · Platform · SourceResolver       │
└─────────┬────────────┬───────────────┬───────────────┬──────────┘
          │            │               │               │
     Rodio/CPAL      Lofty/FS       JSON atômico    Win/Linux
                                                        │
                                                 resolver futuro
```

### Regra de dependência

- domínio não importa Qt, QML, Tauri, Svelte, Rodio, Lofty, TagLib, FFmpeg, Win32 ou D-Bus;
- adapters implementam contratos definidos no núcleo;
- DTOs da apresentação ou do IPC não são entidades do domínio;
- UI nunca acessa arquivos ou APIs privilegiadas diretamente;
- nenhum adapter chama componente visual.

## 4. Estrutura do produto após a decisão

Os spikes e seus inputs autenticados continuam preservados pelo [layout M0-L1](../m0/layout.md), cujo check pré-M0.10 permanece histórico e não governa os novos diretórios de produto. O ADR-0005 satisfaz a decisão de M0.10 e permite M1 com uma única unidade C++ de build/distribuição.

O [primeiro recorte implementado](../core-build.md) moveu headers para `include/m0/{domain,application,ports}`, regras para `src/{domain,application}`, workers para `src/adapters` e testes para `tests/core`. `rc_music_core` depende somente de contratos C++ puros; `rc_music_workers` contém concorrência/filesystem e o adapter Windows, enquanto `rc_music_test_support` isola o backend determinístico. O shell legado consome esses alvos sem cópia das regras. O core raiz OFF compila inclusive numa exportação sem `experiments` e `tests`; os namespaces `m0` foram mantidos para evitar alteração incidental de APIs.

O [segundo recorte](../bootstrap-separation.md) separa a construção real da apresentação dos dados congelados: `PlayerPresenter` recebe portas, preferências e serviços sem `FixtureBundle`; `ShellUiContract` possui defaults próprios, equivalentes ao contrato visual histórico. Os JSONs de demonstração ficam em um alvo de recursos exclusivo dos consumidores históricos/testes, ausente do executável combinado ON/OFF. Construtores históricos preservam os mesmos cenários e validações, sem duplicar regras de reprodução.

O terceiro recorte exclui da compilação combinada OFF os tipos/fontes de fixture e o backend determinístico. A capacidade interna `RC_MUSIC_ENABLE_FIXTURE_SCENARIOS`, pública nos alvos Qt para manter layout/registro coerentes, só existe nos builds com testes ou no shell histórico sem áudio. A API QML permanece estável, com ações históricas inertes em produção e uma única implementação dos fluxos reais. O [check do grafo de produção](../../scripts/Test-ProductionBoundary.ps1) exige fontes reais presentes e dependências históricas ausentes.

A [unidade gráfica promovida](../desktop-build.md) agora compila na raiz: `RC_MUSIC_BUILD_DESKTOP=ON` entra em `src/CMakeLists.txt`, que carrega o módulo compartilhado `cmake/RcMusicDesktop.cmake`. Isso limita o scanner Qt às fontes reais e não encaminha ao experimento. O consumidor histórico usa os mesmos alvos/fontes e injeta seus próprios cenários; ligar testes na raiz não os ativa na aplicação. O build OFF não encontra QtTest/QuickTest nem lê corpus, protocolo ou fixture. A proveniência cobre também `assets` e `tools`; nenhum novo runtime empacotado foi gerado.

A [UI de testes](../ui-tests.md) usa a mesma fábrica de fontes C++/QML, mas compila uma variante separada com a macro de cenário: objetos, registrar, plugins e recursos não se misturam com a aplicação real. Os [testes de integrações Windows](../windows-integration-tests.md) reutilizam adapters reais com boundaries controladas e stores temporários; quatro bindings consomem somente essa UI de teste. O binding de diagnóstico compila sua mesma fonte diretamente no executável de teste, sem ligar a biblioteca de produção que traria a ABI real do presenter. A inspeção PE lê o executável sem executá-lo.

As [jornadas de processo promovidas](../process-tests.md), com gates de build/testes validados, compilam a mesma `src/app/main.cpp` em dois shells exclusivos de testes pela composição compartilhada `RcMusicShell.cmake`. Ambos usam somente a ABI/plugin da UI de teste e o binding de diagnóstico compilado nessa ABI: o combinado usa áudio real sem JSON; o shell de cenário liga exatamente os dois JSONs autenticados. A guarda inspeciona comandos e QRCs alcançáveis dos três executáveis. A aplicação real conserva sua composição sem hooks/cenários em ON/OFF; os quatro smokes QPA Windows continuam separados do runner diário.

```text
/
├── agents.md
├── README.md
├── docs/
├── CMakeLists.txt
├── cmake/                       # core, desktop e testes compartilhados
├── include/m0/                  # domain, application, ports e headers públicos
├── assets/                      # SVGs próprios; originais históricos preservados
├── src/
│   ├── CMakeLists.txt           # escopo do scanner Qt
│   ├── domain/                  # C++ puro; sem QObject/QML
│   ├── application/             # C++ puro; sem QObject/QML
│   ├── adapters/                # áudio, metadata, persistência e Windows
│   ├── boundary/                # conversão segura de caminhos
│   ├── presentation/            # boundary/presenters C++ com Qt
│   ├── ui/                      # qml/ e translations/
│   └── app/                     # bootstrap, IPC e recurso Windows
├── tests/
│   ├── core/
│   ├── qt/                      # adapters, persistência, plataforma e áudio Qt
│   ├── audio/                   # metadata e corpus sintético privado
│   ├── ui/                      # variante de cenários, bindings, Quick e fixtures
│   ├── cmake/                   # integridade, registro e fronteiras do build
│   └── support/                 # fake determinístico exclusivo de testes
├── tools/                       # gerador do ícone Windows
└── packaging/windows/           # runtime, instalador e testes de planejamento
```

Como registro da alternativa não adotada, o mapeamento Tauri previsto era (não implementar no produto):

```text
/
├── agents.md
├── README.md
├── docs/
├── src/                         # Svelte/TypeScript
│   ├── components/
│   ├── features/
│   ├── lib/
│   ├── state/
│   └── styles/
├── src-tauri/
│   ├── src/
│   │   ├── domain/
│   │   ├── application/
│   │   ├── ports/
│   │   ├── adapters/
│   │   │   ├── audio/
│   │   │   ├── metadata/
│   │   │   └── persistence/
│   │   ├── platform/
│   │   │   ├── windows/
│   │   │   └── linux/
│   │   ├── ipc/
│   │   ├── lib.rs
│   │   └── main.rs
│   └── tests/
├── tests/
│   ├── e2e/
│   └── fixtures/audio/
└── tools/
```

Extrair uma biblioteca, crate ou pacote separado somente quando pelo menos um destes casos existir:

- o núcleo precisa ser consumido por outro binário real;
- fronteira de compilação ou features por plataforma traz benefício mensurável;
- testes e dependências não conseguem permanecer isolados por módulos;
- o adapter possui ciclo de release próprio.

## 5. Modelo de domínio

### Tipos principais

| Tipo | Responsabilidade |
|---|---|
| `TrackId` | Identidade única de uma ocorrência na fila |
| `MediaSource` | Origem tipada; `LocalFile` no MVP |
| `Track` | Fonte, metadata conhecida e capacidades |
| `Queue` | Ordem, item atual e navegação por ID |
| `PlaybackState` | Estado autoritativo da reprodução |
| `PlaybackSnapshot` | Estado imutável entregue à apresentação |
| `Position` / `Duration` | Valores validados, nunca negativos |
| `PlaybackCapabilities` | Seek, duração, device switch e outras capacidades |
| `Preferences` | Tema, saltos, volume, tray e esquema versionado |
| `Session` | Fila, item, posição e estado restaurável |

### Invariantes

- fila referencia o atual por `TrackId`, nunca por índice persistido;
- cada ocorrência tem ID próprio, mesmo para o mesmo path;
- posição está entre zero e duração conhecida;
- volume lógico está entre 0 e 1;
- valores de salto estão entre 1 e 300 s;
- estado `Playing` exige uma fonte carregada e reproduzível;
- sessão restaurada inicia `Paused` ou `Empty`, nunca `Playing`;
- erro de um item não invalida a fila inteira;
- capabilities vêm do backend/fonte e controlam a UI.

## 6. Máquina de estados

```text
Empty ──load──► Loading ──ready──► Ready ──play──► Playing
  ▲                │                 ▲  │            │
  │                ├──error──► Failed  └──pause──────┘
  │                │                 │               │
  └────clear───────┴─────────────────┴──end/clear────┘
                                      │
                                      └──next──► Loading
```

`Seeking` não precisa ser estado global de playback. A aplicação mantém uma operação de seek pendente; a UI mantém `SeekPreview` local. Assim pausa/tocando continuam representados corretamente.

Transições inválidas retornam erro tipado e não alteram o snapshot. Eventos atrasados carregam geração/ID para serem descartados.

## 7. Portas

Assinaturas finais serão escritas por TDD, mas os contratos conceituais são:

### `PlaybackBackend`

- carregar uma fonte local e retornar capacidades;
- play, pause e unload;
- seek absoluto assíncrono com sucesso/erro;
- alterar volume/mudo e, futuramente, dispositivo;
- produzir eventos de ready, position, ended, device-lost e error;
- encerrar de forma idempotente.

### `MetadataReader`

- ler tags e propriedades com cancelamento;
- impor limites de arte e texto;
- diferenciar ausente, inválido e não suportado;
- não depender de playback.

### `SessionStore`

- carregar a última sessão válida ou o snapshot vazio default;
- salvar somente a sessão em arquivo próprio, atomicamente; preferências pertencem ao `PreferencesStore`;
- migrar formatos conhecidos;
- manter backup limitado da última gravação válida;
- preservar versões futuras contra downgrade ou sobrescrita sem confirmação explícita.

### `PlatformIntegration`

- tray e visibilidade da janela;
- distinguir suporte da plataforma e disponibilidade momentânea da página de apps padrão, e abri-la por operação tipada sem aceitar URI, path ou ProgID da UI;
- concluir cada solicitação uma única vez na thread que iniciou a operação, distinguindo `Dispatched`, `Unavailable` e `Failed`; `Dispatched` comprova somente o handoff ao handler registrado, nunca que a página ficou visível ou que um padrão mudou;
- publicar capacidades disponíveis.

### `SystemMediaControls`

- publicar título, artista, álbum, estado, capacidades, posição e duração;
- receber comandos distintos de play, pause, anterior, próxima e seek;
- entregar callbacks na thread de aplicação/UI que instalou o handler;
- expor disponibilidade e shutdown idempotente, falhando fechado quando a integração nativa desaparece.

### `SystemPowerEvents`

- publicar somente as transições tipadas `Suspending` e `Resumed`, sem transportar mensagens, handles ou códigos nativos para o núcleo;
- entregar notificações sincronamente na thread de aplicação/UI que instalou o handler;
- expor disponibilidade, substituição explícita do handler e shutdown idempotente;
- manter o receptor nativo privado e estável separado da janela visual, com ciclo de vida encerrado somente no shutdown;
- limitar retries de inscrição nativa por backoff finito e permitir reativação somente por ação explícita ou mudança observada do estado da aplicação.

### Downloads opcionais e edições Lite/Completa

O [journal worker-only de arquivos](../download-ownership.md) é separado do checkpoint de pedidos: provas físicas e gates antes de sinks/rename/retorno importável, upsert nativo limitado sob lock e assessment sem mutação. Pais/folhas não vêm de página ou checkpoint; falha pósrename preserva final sem ACK/importação. Overload volátil é seam explícito. Composição deve emprestar o store na thread da sessão e mantê-lo vivo até teardown, sem I/O na UI. Recovery não dá autoridade de cleanup/reimportação, recibo durável da fila ou cobertura de resíduos do runtime. Componentes seguem opcionais/fora do app.

O [ADR-0006](decisions/0006-browser-editions-and-extensions.md) aceita o escopo planejado de Brave/Chromium, extensões principal/YouTube independentes e duas composições da mesma aplicação. Lite preserva o player atual sem engine de navegação; Completa acrescentará uma superfície remota isolada, sem acesso direto à fachada privilegiada. Ambas usam os mesmos contratos de integração externa, quando o componente opcional estiver habilitado. A engine interna continua pendente; os componentes de rede/conversão abaixo já existem no alvo opcional Windows, mas ferramentas, artefatos e distribuição de produção ainda precisam de seus gates.

Pedidos de download terão fronteira própria: seleção explícita → execução cancelável/limitada → MP3 local validado e finalizado → importação local existente. `OpenSources` continua aceitando paths locais; rede não entra no backend de reprodução por conveniência. A principal não recebe capacidade de YouTube por encaminhamento, mesmo se o componente independente estiver presente. O ADR registra os critérios de isolamento, permissões, instalação e testes; os componentes incrementais não tornam esse fluxo disponível no player nem aprovam as edições instaladas.

O [cliente externo e fronteira de recepção](../browser-extension.md), regidos pelo [ADR-0007](decisions/0007-main-extension-boundary.md), mantêm JavaScript em `extensions/chromium/main` fora do grafo C++/Qt. Detector isolado só relata; popup próprio valida documento/seleção e chama Native Messaging, sem service worker com comandos genéricos. A biblioteca C++ opcional `rc_music_browser_integration` usa Qt Core para codec/framing/sessão/model de jobs; não depende de playback ou engine de navegação. `rc_music_native_messaging` acrescenta Qt Network em alvo separado e `rc_music_native_host` encaminha a porta assíncrona ao dono dos downloads por IPC dedicado. `RC_MUSIC_BUILD_NATIVE_MESSAGING` é OFF por padrão/Windows-only; configuração confiável vazia deixa o host de produção desabilitado. O [serviço do ADR-0008](../download-jobs.md) implementa admissão no alvo puro `rc_music_downloads`, com execução e importação atrás de portas separadas. Produção não instancia ingresso/serviço remoto; a composição comum instrumentada mantém UI e stores reais com publicação sintética. O [ADR-0009 proposto](decisions/0009-download-execution-backends.md) avalia ferramentas/rede; as [ameaças de rede e do host](../security/browser-extension-threat-model.md) precisam de provas antes de ativar downloads reais. Fakes de execução não as substituem.

### Importação local e recuperação de downloads

O [handoff local de resultados publicados](../download-jobs.md#importação-local-de-resultado-publicado) tem `PlayerDownloadImport` concreto na apresentação, usando o mesmo presenter/fila do player. A porta recebe ID/path de composição confiável, não DTO de página nem chamada QML; lexicalidade não prova conteúdo/publicação/ownership. Acrescenta ocorrência sem seleção/autoplay/FS/decoder/discovery ou alteração do backend. O modo volátil conserva recibos por lifetime do owner, limitados e idempotentes mesmo após remoção manual; não é evidência durável.

`LocalQueueListModel::appendWithCommit` prepara/valida/reserva a linha antes do callback não lançante que move fila/navegação e marca o recibo comprometido, antes de sinais Qt. Guard recusa reentrância de fila/transporte/saída e posterga metadata/eventos até liberar a transação. `Ended` da geração atual prevalece sobre snapshots no slot diferido; power conserva suspend observado e último estado, não um histórico ilimitado de edges. Callbacks de linhas não podem lançar/destruir owner/model e importador/adapters emprestados devem sobreviver à chamada.

No modo `Session`, o mesmo callback compromete fila e `DownloadImportReceipt` no presenter; o snapshot único v2 é submetido ao writer existente sem I/O de store na UI. `begin`/`pollImport` expõem `Pending` até `SessionAutosave::savedRevision()` reconhecer `Saved` da revisão que contém o recibo, ou uma posterior que o preserva. `Failed` recuperável/readback não é ACK, e polling não reescreve nem repete a importação; retry explícito conserva o mesmo ID/path sem novo append/download. O serviço projeta `Importing` enquanto aguarda e só publica `Completed` após `Committed`. Recibos carregados de sessão válida são históricos inertes mesmo sem a ocorrência na fila, sem abrir/importar o path. Backup recuperado, defaults inválidos e versão futura bloqueiam novas importações duráveis até reconciliação explícita; nenhum candidato recuperado vira autoridade de filesystem. Os componentes não são uma transação global entre sessão, checkpoint de jobs e journal de arquivos, nem garantia exactly-once universal. Nenhum serviço/ingresso remoto é instanciado no player de produção; lifecycle/model/UI têm composição instrumentada, mas recuperação/capacidade acionáveis e workflow protegido de produção continuam pendentes.

O overload estrito `DownloadService::restore(checkpoint, confirmed_imports)` reconcilia o checkpoint com recibos de sessão fornecidos pela composição confiável. ID exato em ledger saudável comprova reconhecimento histórico mesmo se o checkpoint anterior conserva Queued/progresso parcial ou não tem path: torna o job `Imported`, mantém contadores/total anteriores e conserva o path do recibo somente como candidato privado. Isso cobre crash após Session Saved e antes de outro checkpoint, sem chamar importer/executor/FS. Path candidato presente e diferente ou estado Failed/Cancelled/Cancelling com recibo produzem `ReceiptConflict` e recusam a restauração inteira sem rewrite. Completed/Imported/Importing/ImportFailed sem recibo viram Interrupted/Unavailable, com disposição `Unconfirmed`; não são ACK. O restore legado de um argumento mantém seu contrato explícito sem reconciliação.

O construtor durável de `DownloadAdmissionController` recebe `SessionLoadResult` da leitura confiável: somente Loaded primário validado ou Missing com snapshot exatamente vazio estabelecem saúde. Copia um ledger limitado/detached antes de permitir Ready; backup/defaults/futuro/DTO inválido bloqueiam novos ingressos. `retryStartup()` relê apenas downloads e não melhora a saúde de sessão capturada; revalidar suas fontes exige novo owner. O relatório limitado contém status, IDs/disposições e `session_only_job_ids`, nunca URLs/paths ou ações. Recibos órfãos protegem o namespace novo junto com todo o ledger, inclusive com checkpoint Missing, sem criar jobs/pedidos/fingerprints ou consumir quotas de downloads; não são expulsos da sessão. O model projeta Imported como “Já importado na fila”, sem path/ações e com percentual final 1, que representa estado histórico e não bytes de transferência inventados. Recuperação de ownership/resíduos e resolução acionável de divergências seguem separadas; serviço/model/binding/QML são compostos somente nos ensaios instrumentados, não ativados em produção.

Os [componentes de conversão local Windows](../download-conversion.md) acrescentam temporários privados/limitados, supervisor de processo e pipeline de inspeção → conversão MP3 → inspeção/decodificação integral. São adapters do mesmo alvo opcional, não regras no domínio/QML nem ferramentas distribuídas automaticamente. O conversor recebe somente bytes locais selados; o controlador worker conserva ownership do workspace e supervisiona rede e ferramenta separadamente. Nenhuma etapa altera playback ou importa parciais. A [porta rápida Windows](../download-conversion.md#execução-assíncrona-windows) existe fora do player; a composição do importador, persistência e tela continuam pendentes. Lite/Completa compartilharão esse serviço sem tornar a engine dependência da Lite.

O [checkpoint privado](../download-jobs.md#checkpoint-privado-e-recuperação-conservadora) exporta/restaura histórico limitado de jobs/recibos, sem campos de URL. Identidade SHA-256 via porta é calculada na admissão; restore atomiza validação/cópias e não chama ports de execução/importação. Trabalho antigo vira Interrupted e paths candidatos recuperados ficam fora da projeção pública; histórico Completed não hidrata o importador. Codec/store opcionais Qt Core ficam fora da composição default. Store Windows worker-only exige diretório privado de composição confiável, pins/lock/arquivos regulares e troca atômica; dados inválidos/futuros/ilegíveis bloqueiam escrita. Não é journal de ownership/residuais ou checkpoint conjunto com a fila; as mesmas funções locais e as duas edições continuam o objetivo.

A [admissão durável assíncrona](../download-jobs.md#admissão-durável-assíncrona) acrescenta `DownloadAdmissionController`, portas puras de store/IO/submissão assíncrona e `SingleWorkerDownloadCheckpointIo`, sem Qt no alvo `rc_music_downloads`. Jobs novos são staged/FIFO; checkpoint prospectivo cabe no schema antes de consumir IDs/quotas. O controller restaura histórico prístino por Load e só libera estágio/recibo após Saved correspondente. Uma escrita pendente não suspende progresso/cancelamento/timeout do job ativo. Store/factory/I/O/destruição ficam no worker; controller/session só admitem ou observam. A sessão nativa tem overload de espera com deadline próprio sem mensagem Pending no wire, mantendo legado/v1. Shutdown drena serviço/escrita, salva estado final saudável uma vez e aguarda destruição do IO; `stopped` é parada física e `saveStatus` separa falha de durabilidade. Checkpoints de admissão/final não são autosave periódico ou transação com fila/importador. O host opcional só encaminha/observa; owner/stores ficam no app instrumentado. Composição instalada/protegida de produção e tratamento acionável de erro/recuperação na UI seguem gates.

O [transporte opcional de downloads](../download-network.md) acrescenta `rc_music_download_network`, com Qt Network atrás de `DirectDownloadIo`. `DirectDownloadSession` valida cada destino/redirect e escreve bytes em sink do job; não é `DownloadExecution` completo nem publica MP3. `PinnedDownloadStream` conecta ao IP numérico aprovado, preservando Host/request-target; o parser HTTP é estreito, incremental e limitado, sem cliente genérico exposto à UI.

O [incremento de rede diskless](../download-conversion.md#incremento-de-rede-diskless) acrescenta o executável real `rc_music_download_network_worker`, `DownloadWorkerProtocol` e `WindowsProcessDownloadIo`. O filho recebe uma única operação privada/limitada e exige o runtime TLS restrito antes de DNS ou socket, inclusive em HTTP. DNS literal não faz PTR; nomes usam resolução assíncrona, e todos os endereços voltam à policy comum. Stream recebe endpoint numérico confiável, sem nova resolução ou acesso a arquivos de mídia. Frames limitados transportam bytes e EOF observado explicitamente. O proxy só anuncia sucesso DNS/`Finished` após sequência/Terminal válidos, exit zero e reconhecimento do processo e pipes encerrados; redirect não inicia o sucessor antes desse reconhecimento.

`WindowsDownloadJobSession` compõe transferência, workspace, conversor e publicação no controlador, exigindo reconhecimento de parada de I/O antes de selar/converter, limpar ou publicar terminal. `DirectDownloadIo::failure()` conserva falhas tipadas antes de cancelar; `QtDownloadIo` in-process não comprova término DNS nativo. Ensaios separam worker real DNS literal/HTTP loopback de helper com transferência simulada e processos/filesystem/conversor reais, sem comprovar internet/TLS completo.

`WindowsDownloadExecution` mantém sessão, I/O e destruição em thread/event loop Qt privado, com um pedido, snapshot e wake limitados. Métodos da porta não aguardam esse trabalho; terminal válido só fica visível após destruir a tarefa no worker. Somente observação do terminal correspondente permite novo start. `shutdown` fecha admissão; `stopped` reconhece teardown, separado de `DownloadService::stopped`. Factory cria objetos inertes; start falso/exception da tarefa exige drenagem sem ACK sintético. Final fica retido como evidência privada, sem apagar/importar automaticamente. Ensaio executor→serviço já usa MP3/MP4 sintéticos e importador falso. O alvo permanece OFF por padrão e fora do app; composição do importador, journal/recuperação, host/UI, sandbox, CLI corrigido e distribuição são gates abertos.

`WindowsDownloadNetworkLaunch` autentica fontes por papel fixo, copia somente por handle para nove destinos novos privados e conserva pins de destino/ancestrais. `WindowsProcessDownloadIoConfig::prepared_launch` é capability de composição confiável, nunca DTO de página: deriva/impõe processo/runtime, revalida antes de cada filho e mantém ownership até destruir o supervisor depois de ACK. O overload de `WindowsDownloadExecution` recebe configuração confiável do launcher/job e cria uma tarefa inerte; `start` da tarefa prepara a capability nova e o primeiro `poll` não cancelado inicia a sessão. A thread privada observa cancelamento/shutdown entre as duas fases e o deadline original inclui preparo, sem interrupção instantânea de hash/cópia nativa. Sessão/I/O são destruídos antes da última lease e do terminal público. Bootstrap nativo `/MT` com imports OS-only restringe busca antes de carregar payload Qt `/MD` absoluto; QRC fixa configuração Qt e ABI não transfere objetos/allocations. Fontes e player não são alterados. OS/ICU/API sets/KnownDLL/SxS/AppCompat e ausência de host comprometido são precondições; revisão do perfil PE/distribuição e journal residual continuam gates. Não compor essa preparação síncrona na UI, não distribuir o EXE Qt direto legado e não tratar Job/ACL como sandbox.

A [alternativa nativa Windows de conversão](../download-conversion.md#conversão-nativa-com-media-foundation) acrescenta `RCMusicMediaWorker.exe` ao alvo opcional de rede, sem Qt/CLI no filho nem ligação ao desktop padrão. `WindowsLocalMediaConverter` e `WindowsLocalMp3Validator` recebem backend explícito; Media Foundation não faz fallback ao legado. O worker recebe um pedido binário por stdin, lê o arquivo por HANDLE retido e byte stream anônimo e devolve somente MP3 por stdout ou valida sem escrita. O proprietário conserva sinks/cotas/publicação, supervisiona término e pipes e exige validação por um segundo processo. Leitura/decodificação permanecem no worker; seleção de paths/ferramentas não pertence à página. A prova local não resolve sandbox, identidade do runtime, conteúdo remoto ou publicação/importação duráveis, que continuam nos gates do ADR-0009 proposto.

O [runtime privado Media Foundation](../download-conversion.md#runtime-privado-do-worker-media-foundation) reutiliza os helpers de arquivos do launcher de rede para autenticar e copiar por HANDLE um único EXE em diretório novo privado. `WindowsMediaFoundationLaunch` conserva pins, identidade e inventário fechado; conversor e segundo validador derivam EXE/CWD/ambiente da mesma capability, revalidada antes de cada launch. O overload preparado de `WindowsDownloadExecution` cria as capabilities de rede e mídia na thread privada, antes do primeiro poll da sessão, sem executar caminhos brutos de MF nessa rota. Hashes vinculam bytes à configuração C++ confiável, sem autenticar publicador ou todos os componentes do Windows. Destruição da sessão/processos precede a liberação das capabilities; terminal público não é ordem para apagar um runtime ainda retido por seus owners.

A [superfície experimental da Completa](../embedded-browser.md) fica em alvo próprio `RC_MUSIC_BUILD_EMBEDDED_BROWSER`, OFF por padrão. A porta C++ pura `EmbeddedBrowserWindow` separa o binding Qt da janela/COM WebView2. `EmbeddedBrowserBinding` é construído sem engine e cria a janela somente pela ação explícita; Main recebe uma propriedade nullable, ausente na Lite. O adapter usa uma janela modeless Qt Widgets com viewport HWND, perfil próprio por instância e callbacks fracos correlacionados por geração, no STA da UI. Não expõe QObject/presenter, DOM ou serviços de downloads à página. Política de navegação/permissões e recusas nativas são distintas das policies de autorização de downloads. WebView2 permanece uma avaliação do ADR-0010; execução real, privacidade/limpeza do perfil e distribuição têm gates próprios. O planner do runtime local recusa a composição ON até existir receita específica.

### `MediaSourceResolver`

- converter origem solicitada em descriptor reproduzível;
- MVP aceita somente arquivo local canônico;
- implementação remota futura retorna URL, headers, validade e capabilities;
- resolver é cancelável, limitado por timeout e nunca executa string de shell.

## 8. Comandos, eventos e boundary de apresentação

Comandos externos entram por uma única fachada de aplicação:

```text
OpenSources, Play, Pause, TogglePlayback,
SeekTo, SkipBy, Previous, Next,
SetVolume, ToggleMute,
QueueAdd, QueueMove, QueueRemove, QueueClear,
UpdatePreferences, ShowWindow, Exit
```

Os mesmos comandos são usados por UI, tray, atalhos, SMTC/MPRIS e segunda instância. Isso impede regras divergentes. Play e Pause permanecem comandos distintos nas fronteiras externas: eventos declarativos do sistema operacional não são convertidos em toggle.

Eventos para a UI são snapshots ou notificações semânticas:

- `PlaybackChanged(snapshot)`;
- `QueueChanged(snapshot)`;
- `MetadataUpdated(track_id, metadata)`;
- `PreferenceChanged(snapshot)`;
- `RecoverableError(error_dto)`;
- `PlatformCapabilityChanged(capabilities)`.

No candidato Qt, o presenter C++ traduz propriedades/sinais QML para esses comandos e snapshots sem expor `QObject` ao núcleo. No desafiante Tauri, a mesma fachada atravessa IPC tipado. Regras comuns:

- validar tamanho, tipo e intervalo de todo payload na borda;
- compartilhar a [validação lexical de paths](../../experiments/m0/candidates/qt/README.md#filesystem-url-boundary) entre IPC e URLs do drop/seletor, sem resolver disco/rede no caller; somente a entrada QUrl converte separadores para nativos antes da validação;
- não expor command genérico para executar path/URL;
- agrupar ticks de posição a 5–10 Hz;
- incluir `track_id`/geração em respostas assíncronas;
- no [foco deferido da fila](../../experiments/m0/candidates/qt/README.md#queue-deferred-focus), capturar a ocorrência no instante da notificação e adiar somente o foco; validar identidade, revisão e autoridade ao executar, sem reaplicar índice anterior após inserções;
- manter DTOs pequenos; capa é servida por cache ou protocolo controlado pela apresentação, não copiada integralmente a cada evento;
- aplicar backpressure/coalescing a posição, nunca perder eventos ended/error.

## 9. Modelo de concorrência

O desenho de workers é invariante e independe de como cada toolkit implementa processos ou threads internos.

```text
UI thread
    │ commands                     snapshots/eventos
    ▼                                      ▲
Boundary/presenter / application coordinator
    │                                      │
    ├──► Audio worker (dono do stream, sink e decoder)
    ├──► Metadata/file worker pool
    └──► Atomic persistence worker
```

No candidato Qt, `QML/GUI thread → presenter C++ → application coordinator`. No desafiante Tauri, `WebView/UI thread → IPC Tauri → application coordinator`. A árvore de processos pode diferir, mas ownership, cancelamento, backpressure e estado autoritativo obedecem ao mesmo contrato.

- exatamente um worker é dono do dispositivo/engine de áudio;
- código próprio chamado no callback de saída não loga, não chama UI, não toma lock longo e não adiciona I/O. O M0 mede underruns, latência e comportamento real de Qt Multimedia/FFmpeg e Rodio/CPAL/Symphonia separadamente; se um backend bloquear de modo relevante, seu adapter ganha prefetch/ring buffer limitado ou é rejeitado;
- metadata, arte, diretórios e hash/cache nunca rodam na thread visual;
- comandos usam canal limitado; posição pode ser coalescida;
- operações carregam cancellation token/geração;
- shutdown para novas entradas, salva snapshot, encerra adapters e aguarda apenas dentro de timeout definido.

Evitar mutex global contendo todo o estado. O controller serializa transições; snapshots imutáveis são compartilhados para leitura.

## 10. Progresso e seek

O engine é autoridade para posição. A UI mantém:

- posição confirmada;
- instante monotônico do snapshot;
- estado play/pause;
- preview local enquanto o usuário arrasta.

Enquanto toca, a UI interpola entre snapshots, limitada pela duração. Durante drag, ticks não movem o thumb. Soltar envia `SeekTo`; confirmação reconcilia a posição. Falha restaura o confirmado e oferece feedback.

Saltos são `SeekTo(clamp(position ± preference))`, compartilhados por botão, teclado e controles do sistema. Não existem implementações paralelas na UI.

## 11. Metadata, arte e cache

Pipeline:

1. validar/canonizar path sem perder a forma adequada para exibição;
2. ler `size` e `mtime`;
3. extrair tags/propriedades em worker;
4. validar texto, dimensões e bytes máximos da arte;
5. gerar thumbnail em cache se necessário;
6. publicar resultado somente se `TrackId` e geração ainda forem válidos.

Chave de cache proposta: path canônico + tamanho + mtime. Cache é otimização descartável, com limite de tamanho e LRU simples. Não usar hash integral de arquivos grandes na jornada de abertura.

## 12. Fila e persistência

O recorte Windows autorizado em 2026-09-06 adiciona as ações `toggleShuffleQueue` e `toggleRepeatQueue` ao presenter existente. Aleatório mantém a ocorrência atual e o prefixo percorrido; selecionar uma candidata entre as restantes move seu ID para a próxima posição sem perder as intermediárias. A fonte de aleatoriedade fica fora do domínio, com seed controlado somente nos testes. A rodada conserva somente IDs transitórios já percorridos, não uma segunda playlist; com repetição, as disponíveis são percorridas antes de reiniciar o sorteio. A fila viva continua autoritativa para adições, remoções e indisponibilidade. Desligar segue a ordem exibida, sem restaurar outra ordem, reset do modelo ou releitura de metadata.

Com repetição ligada, um `Ended` válido carrega a próxima ocorrência disponível em ordem circular e move a concluída ao fim somente após aceitação da carga. Uma única ocorrência pode voltar a tocar. Erros de origem são ignorados em busca limitada às ocorrências disponíveis; não se recriam removidas ou duplicatas. A ordem resultante usa o autosave de sessão existente. Repetição desligada preserva a política anterior de fim da fila, inclusive a distinção entre limpar seleção e apagar itens.

`Preferences::repeat_queue_enabled` e `shuffle_queue_enabled` são bools puros, inicialmente falsos. O codec mantém schema 2 e acrescenta as chaves opcionais `repeatQueueEnabled` e `shuffleQueueEnabled`: documentos conhecidos v1/v2 sem uma chave usam falso; quando presente ela deve ser booleana, e o encoder sempre emite ambas. É uma extensão compatível, sem mudança das raízes `MusicPlayer`, reescrita durante leitura ou alteração do schema de sessão. Autosave, backup, bloqueio de versões futuras e restauração pausada continuam aplicáveis; a rodada aleatória é reiniciada sobre a fila restaurada.

No candidato Qt/Windows, o [consentimento de substituição futura](../../experiments/m0/candidates/qt/README.md#replacement-consent) é vinculado à revisão do aviso e ao conjunto de fontes exibidas. QML captura a revisão ao abrir o diálogo e a invalida quando aviso, escopo ou interação mudam; o presenter valida a revisão e congela as fontes antes de agendar. Revisão antiga ou reentrada não autoriza outra fonte. Esse token local de apresentação não substitui a inspeção do store, não identifica bytes em disco e não altera a autorização de retry já concedida ao autosave.

Um arquivo JSON de preferências e outro de sessão simplificam migração e recuperação. Formato conceitual:

```json
{
  "schemaVersion": 1,
  "theme": "system",
  "motion": "system",
  "volume": 0.8,
  "muted": false,
  "skipBackwardSeconds": 5,
  "skipForwardSeconds": 5,
  "closeBehavior": "minimize_to_tray",
  "restoreLastSession": true,
  "startMinimized": false,
  "queueEndBehavior": "stop_keep_selection"
}
```

O schema acima está materializado no adapter experimental Windows. A porta, o codec, o store Windows, a coordenação assíncrona e a restauração pausada no presenter/bootstrap dos recortes de M4.1 são descritos abaixo; M4.4 integra o efeito local de `startMinimized` depois da restauração do placement. `PreferencesStore` não substitui `SessionStore`.

Ao autorizar a substituição de preferências de versão futura, o store mantém o backup intacto até confirmar a gravação principal. O [teste do store Windows](../../tests/qt/QtJsonWindowsPreferencesStoreTests.cpp) cobre falha antes da confirmação, resultado ambíguo após o commit e nova tentativa autorizada.

Em M4.2, a evolução funcional preserva o schema v1 acima como formato conhecido de leitura e passa a emitir sempre o schema v2:

```json
{
  "schemaVersion": 2,
  "theme": "system",
  "motion": "system",
  "volume": 0.8,
  "muted": false,
  "skipBackwardSeconds": 5,
  "skipForwardSeconds": 5,
  "closeBehavior": "minimize_to_tray",
  "restoreLastSession": true,
  "startMinimized": false,
  "queueEndBehavior": "stop_keep_selection",
  "windowPlacement": {
    "x": -1920,
    "y": 40,
    "width": 960,
    "height": 640
  }
}
```

`Preferences` acrescenta `std::optional<WindowPlacement>` ao fim do snapshot, e `windowPlacement` pode ser o objeto mostrado ou `null`. O decoder reconhece v1 como `Valid` com placement ausente, sem exigir a chave nova nem promover ou reescrever o documento durante `load()`. Em v2, a chave é obrigatória e aceita somente `null` ou quatro inteiros representáveis por `int`, com dimensões positivas; v3 ou superior é classificado como versão futura antes da leitura dos demais campos. A tolerância a chaves desconhecidas, o limite de 64 KiB e a substituição atômica do store permanecem.

A decisão de restaurar é uma policy pura, sem dependência de Qt: recebe o placement salvo, as áreas de trabalho conectadas e o tamanho mínimo `720 × 520`. O adapter fornece cada `QScreen::availableGeometry()` em DIPs e na ordem observada; a policy normaliza largura e altura ao mínimo, aceita coordenadas negativas, faz os limites em `int64_t` com rejeição de overflow e só devolve placement e índice quando o retângulo inteiro cabe em uma área válida individual. Áreas adjacentes nunca são tratadas como uma união virtual. Dados inválidos, nenhuma tela ou ausência de contenção integral produzem fallback, inclusive quando uma tela foi removida ou a área disponível mudou com o DPI.

O root QML nasce oculto. No bootstrap, a aplicação resolve o placement, chama `setScreen()` e depois `setGeometry()` antes de liberar a revelação da janela. Durante o uso, o adapter captura somente geometria de janela visível em estado normal, coalesce rajadas de mudanças e entrega o último placement ao autosave de preferências. A amostra pendente é drenada antes de ocultar, minimizar ou sair pelos casos de uso. Como o backend Windows pode publicar a geometria expandida antes do novo estado, uma transição nativa para minimizado, maximizado ou tela cheia cancela qualquer candidato que ainda não estabilizou; o último placement normal já persistido permanece autoritativo, e o retorno a normal agenda nova captura. Assim, estados oculto ou não normais não substituem a última geometria normal válida.

Um restart instrumentado entre dois processos exercita a persistência no primeiro processo e a restauração antes da revelação no segundo. Essa prova continua local e instrumentada: pacote/nativo, `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` e cenários físicos de DPI/múltiplos monitores permanecem abertos. Portanto, este incremento não marca M4.2 globalmente concluída.

O fechamento automatizado do recorte passou 225/225 no CTest Release combinado e 183/183 num core fresco sem Qt com MSVC 19.44/toolset 14.44. O alvo Quick passou 1/1 com 100 casos, `all_qmllint` ficou verde e o formato cobriu 112 arquivos C++ e 46 QML. Como `qmlformat` 6.11.2 não oferece `--check`, a validação QML comparou sua saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado de restart de preferências com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas; nenhum benchmark foi executado.

O schema v2 da sessão, usado enquanto a lista de IDs aposentados está vazia, é:

```json
{
  "schemaVersion": 2,
  "queue": {
    "items": [
      {
        "occurrenceId": "9007199254740993",
        "sourcePath": "C:\\Música\\Abertura 日本語.mp3"
      }
    ],
    "currentOccurrenceId": "9007199254740993"
  },
  "positionMs": "123456",
  "downloadImports": [
    {
      "jobId": "download-job-1",
      "sourcePath": "C:\\Música\\Abertura 日本語.mp3"
    }
  ]
}
```

`queue` é uma unidade porque itens e seleção compartilham a mesma invariável. Sem seleção, `currentOccurrenceId` é `null` e `positionMs` precisa ser `"0"`. IDs e posição usam strings ASCII decimais canônicas para preservar exatamente inteiros acima de `2^53`; sinais, espaços, zeros à esquerda, expoentes, overflow e dígitos não ASCII são inválidos. Paths da fila são preservados literalmente, sem canonicalização nem consulta ao filesystem no codec. Chaves extras de raiz/fila/itens continuam toleradas, e os campos de fila/posição permanecem obrigatórios.

O decoder aceita v1 como migração com `download_import_receipts` vazio. Em v2 e v3, `downloadImports` é obrigatório, inclusive vazio. Cada recibo contém `jobId` e `sourcePath`. IDs distintos têm de 1 a 64 caracteres ASCII `[A-Za-z0-9_-]`, e os paths MP3 são absolutos, normalizados e Unicode válidos. O vetor tem teto de 128 recibos e 1 MiB de IDs e paths nativos. A remoção manual da fila preserva a evidência de importação.

O schema v3 acrescenta `retiredDownloadImportJobIds` com os IDs dos recibos compactados. Cada ID é único, separado dos recibos ativos e precisa aparecer nos jobs arquivados do checkpoint confirmado. O snapshot conserva evidência para reconhecer importações. As operações em arquivos recuperados exigem a prova própria de ownership. [Compactação dos recibos](../download-jobs.md#liberar-vagas-dos-recibos-de-importação)

`session.json` mantém o limite inclusivo de 16 MiB. O codec rejeita entrada maior antes do parse e não produz bytes se o snapshot for inválido, contiver path com `U+0000` ou exceder o limite serializado. Versão >2 dentro do limite é classificada antes da leitura dos campos atuais. Os três primeiros recortes de M4.1 materializaram snapshot/hidratação, porta/codec v1 e store Windows; o quarto acrescentou writer/autosave no core C++ puro; o quinto integrou startup e presenter; o sexto acrescentou os cenários destrutivos locais e a contenção multiprocesso. Naquele bootstrap v1, restauração desligada evitava a leitura. O bootstrap vigente preserva o ledger v2 independentemente de `restore_last_session` e dos opcionais de downloads, evitando que o autosave comum apague reconhecimentos existentes. Desligar restauração continua impedindo hidratação da fila, abertura do backend e autoplay; preservar recibos não inicia serviço/download nem lê seus arquivos de mídia.

Antes de expor o estado ao QML, o presenter hidrata a fila e abre a ocorrência selecionada com a intenção `RestorePaused`, que nunca chama play. O seek restaurado é vinculado à geração aberta, espera duração conhecida quando o destino é positivo e aceita confirmação do backend dentro de 50 ms; trocar de geração descarta a restauração pendente. Uma marca de EOF da mesma geração sobrevive ao snapshot pausado e impede que o `Ended` relacionado avance ou inicie outra faixa. Alterações estruturais alimentam o autosave coalescido, mas a posição persistida vem somente de snapshots confirmados pelo backend. Retry, substituição explícita de versão futura e recuperação de timeout ficam no boundary existente; alternar restauração de `false` para `true` força uma gravação estrutural mesmo quando o snapshot corrente é vazio, reparando ou substituindo estado antigo autorizado.

Na recuperação local integrada, um primário inválido com backup válido hidrata a sessão pausada e publica `RecoveredFromBackup`. Se o replace primário falhar de verdade por sharing Windows, o resultado continua falho e o aviso expõe a ação QML de retry; depois que a contenção externa desaparece, essa ação regrava um primário válido sem autoplay. Se primário e backup forem inválidos, o bootstrap usa snapshot vazio, publica `UsingDefaults` e permite dispensar o aviso sem reescrever silenciosamente os documentos corrompidos.

Gravação:

1. serializar para arquivo temporário no mesmo diretório;
2. flush e, quando apropriado, sincronizar;
3. substituir atomicamente o destino;
4. manter no máximo um backup válido;
5. nunca apagar o arquivo bom antes de validar o novo.

O adapter Windows usa `session.json` e um único `session.json.bak` na raiz absoluta de configuração. `QSaveFile` grava no mesmo diretório com fallback direto desabilitado, e um `QLockFile` cooperativo envolve inspeção, rotação e commits; contenção falha imediatamente. A leitura nunca repara nem regrava. Sua precedência é: primário futuro, primário válido, backup futuro, backup válido, ambos ausentes e, por fim, defaults para arquivos inválidos ou ilegíveis. No save, arquivo ilegível falha fechado antes de qualquer tentativa de escrita, e qualquer versão futura bloqueia a operação sem confirmação explícita. A contenção é exercitada entre PIDs em `BeforeBackupCommit` e `AfterPrimaryCommit`: `getLockInfo()` identifica o holder, um writer concorrente retorna `Failed` sem mudar os slots e, depois da liberação, o holder e uma nova gravação terminam com o lock removido.

Na rotação comum, os bytes exatos do primário válido anterior são commitados no backup antes do novo primário. Quando o backup é futuro e a substituição foi confirmada, ele permanece intacto até o novo primário ser commitado; só então recebe os bytes exatos do primário anterior ou, quando este não era válido, o novo documento. `Failed` pode representar falha antes ou depois de um commit e não garante disco inalterado; o writer deve reconciliar por `load()` e manter a autorização explícita nos retries correspondentes. Além dos fault points e handles Windows, o fluxo local agora encerra à força o processo real em `BeforePrimaryCommit`, depois de verificar o primário anterior, o backup byte a byte e o PID proprietário do lock. O próximo save recupera o snapshot e remove o lock obsoleto, e o relançamento restaura a sessão pausada. Isso prova terminação de processo nesse checkpoint, não perda física de energia, durabilidade sob falha de hardware ou a jornada empacotada/nativa.

A consolidação automatizada final desse sexto recorte passou 214/214 no CTest Release combinado, 172/172 num core fresco sem Qt com MSVC 19.44/toolset 14.44 e 1/1 no alvo Quick final, que cobriu 99 casos. `all_qmllint` ficou verde. O formato cobriu 109 arquivos C++ e 46 QML; para estes, `qmlformat` 6.11.2 foi conferido pela saída padrão normalizada em LF porque não oferece `--check`. A composição `BUILD_TESTING=OFF` foi recompilada, expôs zero testes e rejeitou o hook reservado de crash com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Essa evidência continua local/instrumentada: pacote/nativo, `CHK-E2E`, M4.1 e os demais gates contratuais permanecem abertos.

O `SingleWorkerSessionWriter` possui uma única thread e mantém `save()` e a eventual reconciliação fora do caller. Cada `SessionRevision` identifica de forma indivisível o par snapshot + autorização `replace_future_version`; a mesma revisão não pode representar outro conteúdo ou outra autorização. Há no máximo uma gravação ativa, e `flush()` pode deixar somente a revisão solicitada mais recente atrás dela. A completion devolvida por `flush()` é consumida nesse caminho e não pode reaparecer em `poll()`.

Se `save()` retornar `Failed` ou lançar, o mesmo worker executa `load()` sem promover a operação a sucesso. O readback tri-state registra `true` apenas para `Loaded`/`RecoveredFromBackup` exatamente igual ao snapshot solicitado, `false` para resultado observado divergente ou status não recuperável e ausência de valor quando o próprio `load()` lança. `Saved` e `BlockedByFutureVersion` não reconciliam. Mesmo com readback `true`, o status permanece `Failed`, a sessão continua suja e a autorização de substituição permanece nos retries até um `Saved`, pois um primário legível ainda pode ocultar backup ou limpeza incompletos.

Chamadas concorrentes de `shutdown()` são serializadas, drenam uma gravação já aceita e fazem join uma única vez de forma idempotente. Esse join continua sem deadline: se o store síncrono não retornar, o encerramento pode permanecer bloqueado e a futura composição visual não pode tratá-lo como operação limitada por tempo.

O `SessionAutosave` mantém deadlines independentes e escolhe sempre o mais próximo. Mudanças estruturais usam debounce trailing de 500 ms, limitado a 5 s desde a primeira mudança estrutural ainda não aceita. Posição confirmada altera somente o escalar `position_ms` em O(1); o primeiro tick arma um checkpoint fixo de 10 s e ticks posteriores não o adiam. Preview de seek, interpolação visual e ticks não confirmados pelo backend nunca alimentam o snapshot persistido.

`checkpointConfirmedPosition()` e `requestSaveNow()` tornam o estado sujo imediatamente elegível. Na integração atual, seek confirmado, pausa, parada/fim, erro terminal, saída e a transição `Suspending` usam esses gatilhos somente depois de obter a posição confirmada. O caminho de suspensão inicia a escrita assíncrona, sem `flush()` bloqueante e sem comandar pause, play ou autoplay; por isso ele reduz a janela de perda, mas não garante que o replace já esteja durável antes de o sistema congelar o processo. Após `Failed` ou bloqueio por versão futura da revisão atual, deadlines automáticos são removidos: alterações posteriores continuam coalescidas no snapshot/revisão mais recentes, mas apenas retry explícito, confirmação explícita de substituição, save-now ou flush rearmam a escrita. Completion obsoleta nunca limpa a revisão atual. `savedRevision()` começa em zero e avança somente por completion `Saved` correspondente à gravação submetida pelo owner; estado inicial limpo e readback recuperável não criam ACK. Uma revisão antiga salva pode avançar essa confirmação sem limpar o snapshot mais novo ainda sujo.

Salvar por debounce após mudanças importantes e no shutdown. A fila preserva paths ausentes como itens indisponíveis para que o usuário possa localizar ou remover. SQLite só entra com biblioteca indexada e busca que comprovem necessidade.

## 13. Instância única e abertura pelo shell

Sequência de startup:

1. capturar argumentos como valores nativos, sem reparse de string;
2. adquirir instância única cedo;
3. se secundária, encaminhar lista tipada e ativar a principal, então sair;
4. principal carrega preferências e a sessão para preservar seus recibos; somente `restore_last_session=true` hidrata a fila/ocorrência pausada;
5. inicializa engine e integrações;
6. processa argumentos novos conforme política de fila;
7. resolve a disposição inicial depois do placement: fonte explícita mostra/restaura; sem fonte, a preferência só oculta quando a tray está disponível;
8. mostra, oculta ou restaura a janela conforme essa policy.

Cobrir paths Unicode, aspas, múltiplos arquivos, chamadas simultâneas e diretório de trabalho diferente. O canal local entre instâncias deve aceitar somente mensagens esperadas e impor limites; IPC adicional do shell, quando existir, segue as mesmas regras.

No recorte funcional local de M4.3 do candidato Qt/Windows, a eleição usa um `QLockFile` na raiz de configuração do usuário e um `QLocalServer` nomeado pelo PID que solicita `UserAccessOption`. A instância secundária serializa somente o comando fechado `openSources` v1 (`version`, `command`, `paths`), tenta transferir a permissão de foreground ao PID proprietário como operação best-effort, aguarda ACK/NACK dentro de um prazo limitado e nunca inicializa o backend de áudio. A principal restaura/ativa a janela e entrega os mesmos paths ao caso de uso tipado de importação. A ACL efetiva entre usuários continua um check nativo de segurança, não uma conclusão desta implementação local.

O limite inclusivo é de 1.000 paths e 1 MiB do JSON UTF-8 serializado; o prefixo big-endian de quatro bytes do frame não participa desse MiB. A política é rejeitar o lote inteiro, nunca dividi-lo silenciosamente. A fila de bootstrap aceita no máximo 16 comandos e 4 MiB, e o servidor limita simultaneamente conexões ativas e o buffer individual; frames incompletos expiram. Antes do IPC, a instância invocada resolve paths relativos e rooted-only contra seu próprio diretório/volume e transmite somente paths de filesystem plenamente qualificados. Paths `:/` de recursos Qt, relativos a drive, valores com NUL, namespaces `\\.\`/`\\??\` de dispositivo e prefixos extended não canônicos são recusados.

ACK significa que o comando passou pela validação e ficou sob responsabilidade da instância principal. Ao iniciar uma saída, a principal pausa novos ingressos e drena os comandos já confirmados, inclusive a importação ativa pertencente ao dispatcher, antes do checkpoint final; uma importação local da UI sem ownership do IPC mantém a semântica anterior de cancelamento na saída. Se o prazo de saída expirar, a intenção é cancelada e a aplicação volta a aceitar trabalho, em vez de descartar o lote. Depois do commit da saída, o NACK muda de indisponibilidade transitória para encerramento definitivo.

Na [transação secundária já conectada](../../experiments/m0/candidates/qt/README.md#ipc-buffered-reply), falha da espera de escrita com zero bytes pendentes permite validar a resposta retida pelo socket; não confirma entrega por si só. Framing, schema e deadline continuam obrigatórios. Falha após iniciar a escrita é terminal, sem reconexão/retransmissão; somente indisponibilidade anterior a ela permite retry.

O fechamento automatizado local passou 229/229 no CTest Release combinado. O teste de processo exerceu dois ACKs, incluindo um path Windows comum acima de 260 caracteres, NACK transitório durante a drenagem, persistência dos dois itens e um único backend. `m0_qt_shell_ui_qmllint`, formato de 122 C++/46 QML e validação documental ficaram verdes. A composição `BUILD_TESTING=OFF` foi recompilada, expôs zero testes, rejeitou o hook reservado com exit 1 sob o runtime autenticado e preservou `longPathAware=true` no manifesto. Como `qmlformat` 6.11.2 não possui `--check`, a conferência QML comparou a saída padrão normalizada em LF. Nenhum benchmark foi executado.

### Ingresso separado de downloads

O [cliente da extensão](../browser-extension.md#descoberta-em-documentos-e-shadow-dom-aberto) mantém a descoberta no mundo isolado e no documento principal autorizado, caminhando também por Shadow DOM aberto e frames acessíveis pelo DOM. O protocolo privado v2 inclui sourceId efêmero por conexão/Document/URL; uma coleta fresca antes do envio revalida somente os selecionados, entre duas conferências da aba/documento principal. Roots e observadores têm limites agregados e retirement ao destacar/parar. Esses contextos não atravessam o DTO nativo v1, que conserva URL/título/tipo dos selecionados. Frames inacessíveis/roots fechados e integração Brave real permanecem fora da evidência atual; não há nova permissão, engine ou ativação de downloads de produção.

O [host opcional Native Messaging Windows](../browser-extension.md#host-e-transporte-native-messaging-windows) não modifica eleição, schema, framing big-endian ou ACK de `OpenSources`. Usa endpoint próprio e preâmbulo privado de **61 bytes** (oito bytes `RCMDLIPC`, versão `1` de um byte, origem exata de 52 bytes), seguido do mesmo schema/frame v1 little-endian, com JSON de download reserializado estritamente pelo codec a partir do DTO tipado. Preserva exatamente lote e strings de URL/query/título, não bytes JSON, whitespace ou ordem original das chaves. A identidade de build confiável nunca vem da página, ambiente ou argumentos. Origem crua em argv deve coincidir exatamente; o opcional uint64 canônico `--parent-window` é inerte. Defaults vazios encerram o host com exit 2 antes de `QCoreApplication`/I/O/conexão.

Servidor `UserAccessOption` limita backlog/sessões a oito, impõe buffers/prazos totais e chama somente admissão assíncrona rápida. Não espera store/I/O/tick na UI. `Pending` não é ACK: aceitação exige `Saved` correspondente do owner. Cliente faz uma conexão, sem retry/autolaunch, e só reconhece resposta única, estrita e correlacionada de até 4.096 bytes após EOF do peer. Falha pós-escrita/timeout/malformação encerra observação como incerta, sem cancelar jobs ou inventar rejeição. Shutdown fecha ingresso/observadores antes do executor; owner/stores permanecem vivos até callbacks/recursos drenados e `stopped` reconhecido.

O host curto tem stdin/stdout Win32 binários de pipe, snapshot limitado antes de alocar e writer com buffer próprio/prazo de 2 s/cancelamento repetido até parada física. O relógio da sessão é consultado imediatamente depois de `readAvailable`, antes de validar/admitir, sem reutilizar instante anterior à leitura para avaliar o deadline. Um watchdog independente de 15 s cobre leitura/teardown não cooperantes e termina somente o próprio host, nunca o dono persistente ou outro processo. O [modelo de ameaças](../security/browser-extension-threat-model.md#host-binário-e-canal-privado-do-dono) explicita que origem/pipe do mesmo usuário não autenticam o browser e que primeira instância/colisão do pipe Windows, identidade instalada, HWND/Brave e registro permanecem gates. A produção segue sem ingresso/serviço remoto ativo; Lite/Completa compartilham a futura composição, sem engine na Lite ou YouTube na extensão principal.

### Revisão física explícita de downloads interrompidos

[DownloadRecoveryReview](../../include/m0/ports/DownloadRecoveryReview.hpp) é uma porta pura e rápida separada de execução/admissão/importação. A composição entrega um job ID limitado e operation ID crescente; start/poll/shutdown/stopped não fazem I/O no caller. Revisão e descarte opt-in compartilham o slot único incluindo completion não observada; poll obsoleto não a consome. Resultado contém correlação de operação/job/action e enum fechado, nunca path ou handle. Observação não é capacidade sobre arquivos.

[WindowsDownloadRecoveryReview](../../src/adapters/WindowsDownloadRecoveryReview.hpp) possui configuração confiável copiada e uma thread privada para factory, leitura limitada do journal, procura por ID exato e destruição. `ReviewFiles` preserva o assessment somente leitura; `DiscardPartials` relê/revalida ownership com fresh DELETE pins e remove somente temporários próprios/diretório vazio por handles. Final nunca é apagado. Discarded exige ausência física e Saved de cleanup_complete no mesmo registro; Incomplete/Unconfirmed/Missing não são ACK nem autoridade de importação. Não cria workspace, repara permissões/histórico, baixa ou decodifica mídia.

O binding aceita ações explícitas de um job `Interrupted` conhecido com o controller `Ready` e a execução ociosa. Encerrar a recuperação exige a observação `OwnedPartial` ou `CleanupCandidate` e consentimento vinculado ao job. Somente `Discarded` correspondente encerra o item no serviço. Após reiniciar, `CleanupCandidate` permite confirmar a ausência e regravar o registro exato antes do encerramento. O owner configurado usa os mesmos diretórios privados para execução e recuperação e reserva a operação até a drenagem. [Composição e validação](../download-jobs.md#recuperação-de-arquivos-no-player-configurado)

Resultado tipado normal admite recheck explícito. Exceção da porta ou completion fora do contrato fecha apenas esta capacidade, conserva a correlação incerta até drenagem e a mantém desativada até owner novo, sem promover resultado tardio ou interferir nos jobs/reprodução locais. Para possível descarte já admitido, projeta DiscardUnconfirmed, não promessa de preservação de todos os parciais.

## 14. Integração com plataforma

O [ADR-0004](decisions/0004-rc-music-identity.md) fixa **RC Music** como nome visível. O candidato mantém `MusicPlayer` nos identificadores internos do Qt para conservar a mesma raiz de configuração e o mesmo lock; a apresentação e a futura identidade de associação não devem impor uma migração implícita dos dados.

### Windows

- tray e menu;
- SMTC/metadata/teclas de mídia;
- eventos de sessão/power;
- página oficial de default apps;
- associações por configuração/template de instalador próprio, com ProgID, `Capabilities` e `RegisteredApplications` por usuário; não confiar somente no default do empacotador;
- high contrast, reduced motion e UI Automation pela integração de acessibilidade do toolkit escolhido, validados em build empacotado;
- assinatura futura do executável/instalador.

### Linux

- MPRIS por D-Bus;
- StatusNotifier/tray quando ambiente suportar;
- XDG `.desktop`, ícones, MIME e diretórios de dados;
- teste Wayland e X11 na distribuição de referência;
- fallback visível quando tray estiver ausente.

Cada adapter publica capabilities. A aplicação não finge que uma integração indisponível funcionou.

No recorte funcional local de M4.4, a policy de startup é explícita. Depois de aplicar tela/geometria, `startMinimized=false` mostra a janela. Com a opção ligada e tray disponível, o root continua oculto antes da entrada no event loop; sem tray, ele é revelado e um aviso explica o fallback. Uma fonte inicial explícita prevalece sobre a preferência, e um comando `openSources` por IPC restaura/ativa a principal que iniciou oculta. O menu da tray usa ações reais para mostrar/ocultar/mostrar. O toggle de início minimizado permanece utilizável para desligar a opção depois que a capacidade de tray desaparece.

`QtShellPlatform` não deduz intenção a partir de `QWindow::isVisible()`: um estado interno `hidden_to_tray` é armado apenas por ocultação explícita, limpo por restauração/rebind e é o único que ativa a vigilância da capacidade. Assim, a janela invisível durante o bootstrap não é minimizada por uma falsa perda de tray. Se a capacidade desaparecer enquanto `hidden_to_tray` está ativo, o adapter interrompe a vigilância e recupera a janela como visível/minimizada.

A saída do presenter permanece transacional também em relação ao áudio. Novos ingressos são pausados; trabalho de fonte já ACKado é drenado; preferências e sessão são persistidas; só então o controller executa `PlaybackBackend::shutdown()` uma vez e, depois, solicita a saída da plataforma. Repetir a intenção depois do commit é inerte. Falha ou timeout antes do commit cancela a saída, mantém o backend ativo e restaura a aplicação alcançável.

Os Reds do recorte exigiram a nova policy de disposição, ownership explícito da ocultação, toggle recuperável, precedência de fonte e a ordem shutdown → saída; os Greens focados antecederam a consolidação. O CTest Release combinado passou 229/229 e o core Qt OFF 183/183; `m0_qt_shell_ui_qmllint` ficou verde e o formato cobriu 122 C++/46 QML, com `qmlformat` validado por comparação da saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou os dois hooks M4.4 com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas; nenhum benchmark foi executado.

Essa evidência não encerra M4.4. Pacote/desktop e tray nativos, primeiro frame perceptível sem flash, fallback Linux real, restart do Explorer, NVDA/acessibilidade nativa e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y` e `CHK-CLOSE` permanecem abertos.

No recorte funcional local de M4.5, `SystemMediaControlsBinding` projeta o estado do presenter na porta separada `SystemMediaControls`: título, artista, álbum, status, capacidades, posição confirmada e duração. Snapshots iguais não são republicados; seek permanece desabilitado durante preview ou enquanto aguarda confirmação. Play, Pause, Previous, Next e Seek retornam aos mesmos casos de uso do presenter usados por UI, atalhos locais e tray. Descarregar, entrar em erro ou sair publica `Closed`; no teardown, o binding remove o handler e encerra a porta.

O transporte distingue a intenção explícita desejada do comando ainda em voo. Repetir a mesma intenção é inerte; uma intenção oposta antes do ACK é preservada e emitida somente após a confirmação obsoleta, sem duplicação. Navegação usa esse estado efetivo, e troca de geração, estado terminal ou EOF confirmado encerram a intenção pendente. Assim, eventos Play/Pause declarativos do sistema não herdam a ambiguidade de um toggle nem perdem a solicitação mais recente por atraso do backend.

No Windows, `WindowsSmtcAdapter` obtém os controles da janela pelo HWND real com C++/WinRT e `ISystemMediaTransportControlsInterop::GetForWindow`. A boundary publica status, metadata, botões e timeline, desabilita explicitamente operações não suportadas, entrega callbacks na thread da UI e degrada para indisponível sem deixar exceções atravessarem a fronteira. Destruição da `QPlatformSurface` libera o binding nativo; recriação válida faz rebind e republica o snapshot mais recente, enquanto geração e presença da surface impedem um rebind enfileirado obsoleto. `Closed` limpa o `DisplayUpdater` e desabilita os controles; shutdown revoga handlers e torna callbacks tardios inertes.

Os três alvos focados `m0.qt.system-media-controls.binding`, `m0.qt.windows.smtc` e `m0.qt.windows.smtc-native` ficaram verdes. O smoke nativo, sob QPA Windows, consultou externamente os controles do HWND e comprovou estado inicialmente desabilitado, status Playing, flags de transporte, título/artista/álbum e limpeza em `Closed`. A consolidação Release passou 239/239 no CTest combinado e 190/190 no core Qt OFF. `m0_qt_shell_ui_qmllint` ficou verde; nenhum QML mudou. A composição `BUILD_TESTING=OFF` recompilou, listou zero testes e rejeitou o hook reservado com exit 1. Nenhum benchmark foi executado.

Essa evidência não sintetiza uma tecla de mídia física, não percorre a jornada empacotada e não implementa MPRIS no Linux. Portanto M4.5, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E` e `CHK-CLOSE` permanecem abertos globalmente.

No primeiro recorte funcional local de M4.6, `PlatformIntegration` expõe separadamente `defaultAppsSettingsSupported`, que informa se a operação pertence à plataforma, e `defaultAppsSettingsAvailable`, que representa a disponibilidade momentânea do handler, além da operação fechada `openDefaultAppsSettings`. Seu resultado tipado é `Dispatched`, `Unavailable` ou `Failed`. O presenter projeta essa fronteira nos estados somente leitura `Idle`, `Opening`, `Dispatched`, `Unavailable` e `Failed`, notificados pelo sinal dedicado `defaultAppsSettingsChanged`: recusa uma segunda abertura enquanto a primeira está em voo, usa geração para invalidar conclusão tardia e torna callbacks inertes depois da destruição ou do commit efetivo da saída. Uma saída ainda reversível por drenagem/persistência não descarta prematuramente a conclusão; se a saída falhar e for cancelada, a ação volta a ficar utilizável. A conclusão é reenfileirada para a thread da UI e exceções da fronteira degradam para falha recuperável.

No Windows, `QtShellPlatform` mantém internamente a única URI permitida, `ms-settings:defaultapps`, e a entrega a `QDesktopServices::openUrl`; QML não fornece nem altera esse valor. O retorno verdadeiro vira `Dispatched` apenas para indicar que o handoff ao handler registrado foi aceito. Retorno falso, exceção, operação ausente ou saída já iniciada produzem estado fechado e explícito, sem inferir que as Configurações abriram ou que qualquer associação foi modificada.

O painel Geral oferece no Windows um botão com nome e descrição acessíveis, foco de teclado e motivo de indisponibilidade. Quando a plataforma não suporta a operação, botão e feedback não são renderizados. Falha e indisponibilidade aparecem como texto localizado; o alerta só é anunciado assertivamente enquanto o painel está aberto e visível, sem fala tardia quando fechado, e a falha inclui um caminho manual pelas Configurações do Windows.

Os Reds focados exigiram a porta/estado ainda ausentes e os contratos do adapter e da UI. Os três Greens focados passaram individualmente: `m0.qt.shell-platform` 1/1, `m0.qt.presenter.initial-load` 1/1 e `m0.qt.quick.shell-states` 1/1. A consolidação passou 239/239 no CTest Release combinado, 190/190 no core Qt OFF e 1/1 no Quick com 100 casos internos; `m0_qt_shell_ui_qmllint`, formato de 131 C++/46 QML, composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado, e validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas ficaram verdes. Nenhum benchmark foi executado neste recorte.

Esse recorte não escreve `UserChoice`, não cria ProgID, `Capabilities` ou `RegisteredApplications`, não publica uma matriz de formatos, não instala associações, não comprova double-click/Explorer ou pacote e não implementa equivalente Linux. M4.6 e os checks nativos, de pacote, E2E, acessibilidade e Linux continuam abertos globalmente.

No primeiro recorte funcional local de M4.7, suspensão e retomada deixam `PlatformIntegration` e passam pela porta separada `SystemPowerEvents`. No Windows, `WindowsPowerEvents` cria na thread da UI um `QWindow` privado, invisível e exclusivo para receber notificações. Seu `HWND` real é independente da janela visual e permanece estável enquanto a integração está ativa. `RegisterSuspendResumeNotification(..., DEVICE_NOTIFY_WINDOW_HANDLE)` fica registrado nesse receptor até shutdown. O filtro aceita somente `WM_POWERBROADCAST` para esse mesmo handle: `PBT_APMSUSPEND` produz `Suspending` e `PBT_APMRESUMEAUTOMATIC` produz `Resumed`. A entrega é síncrona na thread da UI; cada mensagem reconhecida é consumida com resultado `TRUE` antes de devolver controle ao QPA, inclusive quando a transição consecutiva é deduplicada, o callback lança ou encerra a integração durante a entrega. Eventos não reconhecidos, outro `HWND` e qualquer mensagem recebida após shutdown falham fechados e retornam `false`. A porta entrega exclusivamente essas transições nativas. Transições consecutivas iguais são deduplicadas sobre o registro contínuo, mas um `Resumed` inicial continua válido para reconciliar um suspend que o processo não observou.

Uma falha inicial de `RegisterSuspendResumeNotification` mantém o receptor privado e aplica backoff coarse single-shot finito de 1 s, 5 s e 30 s. Se a tentativa posterior aos 30 s também falhar, o timer para; o orçamento só reinicia por `start()` explícito ou por um `applicationStateChanged` observado enquanto a integração continua indisponível. Shutdown cancela qualquer retry, remove o filtro, desfaz o registro quando presente e destrói o receptor, tornando timeout e callback tardios inertes. Criar, destruir ou recriar a surface da janela visual não afeta o `HWND` privado nem interrompe o registro. O binding remove o handler antes de encerrar a porta. A fronteira Win32 fica isolada no adapter e ligada a `User32`; o core recebe somente o enum tipado.

Ao receber `Suspending`, o presenter captura a última posição confirmada da geração corrente, solicita `requestSaveNow()` e deixa inerte o polling funcional até a retomada; polling ainda necessário à persistência assíncrona pode continuar. A integração de power não chama `flush()`, `pause()`, `play()` nem cria autoplay. Em `Resumed`, o presenter atualiza capabilities da plataforma e consulta o snapshot atual do backend; quando a geração ainda corresponde à sessão ativa, reconcilia estado, posição e erro pelos mesmos contratos de playback. O evento de retomada também rearma o polling aplicável. Transições duplicadas e eventos após o commit da saída são inertes.

Esse é apenas o recorte de suspend/resume de `FR-038`. Os testes registram uma janela real e exercitam a boundary sem suspender fisicamente a máquina; não cobrem Modern Standby, hibernação, ordem/delay reais do sistema nem garantem que o save assíncrono se tornou durável antes do congelamento. Perda de dispositivo, drive desconectado, arquivo removido, restart do Explorer, pacote, E2E nativo e Linux permanecem pendentes; M4.7 continua parcial. Nenhum benchmark pertence a este recorte.

O segundo recorte de M4.7 mantém a recuperação de fonte dentro das fronteiras existentes. A UI solicita `retryQueueOccurrence(ID)`; o presenter valida a ocorrência indisponível e resolve seu path na fila interna, sem aceitar outro path do QML. A aceitação do novo load precede a mudança de seleção. `LocalFileQueue::selectUnavailableForRetry` não altera a disponibilidade, e `markAvailable` somente confirma a transição após `Ready`, `Playing` ou `Paused` da geração correspondente. O `LocalQueueListModel` conserva metadata e identidade, limpa o diagnóstico com sinais restritos ao item recuperado e pode atualizar somente o diagnóstico de um item que segue indisponível.

O presenter mantém o ID da tentativa em voo separado da posição da linha. Assim, duplicatas e reordenação não confundem loading, e callbacks obsoletos não recuperam outra ocorrência. Rejeição síncrona do load preserva a sessão anterior; erro de tentativa aceita mantém diagnóstico recuperável e suprime a continuação automática específica dessa tentativa. Outro retry exige uma ação explícita. A tentativa aceita também assume a intenção de reprodução sobre a importação incremental: lotes que chegam depois não tomam a seleção de volta. O histórico de problemas dessa importação usa IDs de ocorrência, separado da disponibilidade mutável, para conservar a contagem após recuperação e deduplicar novas falhas do mesmo item.

Fila e erro apresentam “Tentar reproduzir novamente” por ação acessível, com loading associado ao ID e “Abrir outro áudio” preservado. A [evidência central do segundo recorte](../../experiments/m0/candidates/qt/README.md#retry-m47) inclui os contratos de teste e distingue a integração com uma WAV restaurada no mesmo path temporário dos cenários ainda pendentes de dispositivo, drive físico, arquivo relocalizado, Explorer, pacote e Linux. Esse recorte não encerra M4.7 nem `NFR-REL-005`.

O terceiro recorte de M4.7 mantém a enumeração e a seleção da saída padrão dentro do adapter Qt. `QtAudioOutputDevices` é uma fronteira de teste específica desse adapter, não uma nova porta de domínio: encapsula identidade, `QAudioDevice`, aplicação ao `QAudioOutput` e notificação de mudança. A implementação real observa `QMediaDevices::audioOutputsChanged` e consulta `defaultAudioOutput()`. Cada load fixa a saída corrente; ausência inicial ou mudança de identidade em uma geração ativa produz `AudioOutputUnavailable`/`AudioOutputChanged`, preservando posição/duração confirmadas e retirando o player antigo antes do evento. Mudanças que conservam a identidade padrão são inertes. Comandos e sinais de mídia também reconciliam essa identidade; reconexão não dispara load ou play.

Esses erros atravessam a porta `PlaybackBackend` como códigos puros e não marcam uma ocorrência indisponível em `LocalTrackSession`. A apresentação usa `recover-audio-output` em `executeErrorAction()` para abrir novamente o path interno em `RestorePaused`. A restauração da posição continua vinculada à geração, com tolerância, tratamento de mídia sem seek e proteção de EOF. Outra seleção de faixa invalida a restauração. Fila, duplicatas e identidade do modelo são preservadas. Troca física, áudio audível, driver, pacote e Linux seguem pendentes. [Evidência de M4.7](../../experiments/m0/candidates/qt/README.md#audio-output-m47)

Em M6.1, a porta opcional `AudioOutputSelection` expõe à apresentação opções com rótulos e IDs opacos. O adapter Qt mantém a escolha para a sessão e resolve o ID em uma enumeração atual antes de usar o `QAudioDevice`. A escolha explícita continua selecionada quando a saída desaparece. O presenter usa a mesma recuperação pausada quando o usuário muda a rota efetiva e a operação ainda pertence à mesma ocorrência e geração. Uma atualização de topologia apenas atualiza as escolhas e pode sinalizar perda de saída.

O quarto recorte de M4.7 fica inteiramente no adapter de shell. No QPA Windows real, `QtShellPlatform` possui `WindowsTaskbarEvents`, que registra `TaskbarCreated` em um `QWindow` top-level privado e invisível. O filtro aceita somente a mensagem registrada para seu próprio `HWND` e entrega o sinal enfileirado com guardas de lifetime; shutdown remove filtro e receptor. A leitura de capacidade exige esse monitor pronto, ícone pretendido não vazio e presença de shell/taskbar por `GetShellWindow`/`FindWindowW`, falhando fechada quando o monitor não inicia; `refreshAvailability()` permite nova tentativa. Essa prova de presença não é confirmação de `NIM_ADD`, e geometria do ícone não é critério de disponibilidade por causa do overflow legítimo.

O Qt conserva a responsabilidade de registrar novamente o ícone. `handleTrayHostRestart()` apenas torna alcançável, minimizada e sem solicitar ativação, a janela cuja ocultação pertence ao adapter. A política não manipula o ícone privado, não introduz flag persistente de janela e não altera o domínio, áudio, fila ou preferências. O estado de interação vindo do binding adia recuperação durante saída pendente; retorno da interação revalida ownership, e rebind/saída confirmada invalidam a intenção antiga. Janela de bootstrap apenas oculta, janela já visível/minimizada e retorno do shell não reaplicam a disposição inicial. O [registro central de taskbar](../../experiments/m0/candidates/qt/README.md#taskbar-m47) separa os contratos do adapter e a caracterização do registro nativo do Qt da validação ainda pendente com Explorer realmente reiniciado, pacote e Linux.

O quinto recorte de M4.7 introduz a porta C++ pura `AudioFileLocator`, separando seleção de arquivo e reprodução. `locateQueueOccurrence(ID)` resolve o path anterior dentro do presenter; a UI não fornece nem modifica paths. `QtAudioFileLocator` mantém um diálogo assíncrono de seleção local vinculado à janela, com uma única solicitação e cancelamento, sem sondar ou navegar automaticamente ao path ausente. A conclusão tipada retorna pela thread da UI e o presenter a reenfileira, validando geração da solicitação, identidade da ocorrência e path original antes de aplicar. Cancelamento, remoção, substituição, recuperação concorrente e saída/destruição invalidam conclusões obsoletas.

A operação pura `replaceUnavailableSource` substitui apenas o path de uma ocorrência indisponível, sem I/O, alocação de outro ID ou mudança de seleção. O modelo reconstrói somente a linha afetada com fallback do novo nome, metadata limpa e diagnóstico fechado `source_relocated`, mantendo `available=false`. Em uma ocorrência não atual, não existe chamada de áudio ou metadata: o retry posterior verifica a nova origem. Na ocorrência atual em erro, o novo load é aceito antes da troca da fila e usa `RestorePaused`, renovando gerações, arte e metadata e restaurando a posição confirmada conforme o contrato de seek. Rejeição conserva a origem anterior. O snapshot estrutural já contém paths por ID, portanto a troca usa o autosave existente sem alterar schema v1; disponibilidade continua transitória.

O alvo de posição restaurada ainda pendente deve sobreviver aos erros de origem recuperáveis `source_unavailable`, `invalid_media` e `access_denied`, preservando o valor no snapshot até recuperação ou cancelamento previsto por seleção/transporte explícito e gestão de geração. O [registro central de localização](../../experiments/m0/candidates/qt/README.md#file-location-m47) documenta as ações acessíveis da UI, a regressão de persistência/decoder que motivou esse ajuste, os checks automatizados verdes e os riscos residuais. A seleção isolada não comprova decode; a integração com decoder real usa arquivo temporário renomeado e não comprova dispositivo ou drive físico, diálogo nativo, pacote ou Linux. M4.7 continua parcial.

A seleção principal acrescenta a porta C++ pura `AudioFilesPicker`, ligada por `setAudioFilesPicker` fora do QML. A produção usa `QtAudioFilesPicker`; o helper interno `QtAudioFileDialog` compartilha lifecycle, cancelamento e seleção local com o `QtAudioFileLocator`, variando somente a cardinalidade. `open()` substitui o seletor estático síncrono, e callbacks sempre retornam enfileirados ao presenter. Geração, lifetime e exclusão mútua entre seleção principal/localização evitam aplicar conclusões obsoletas. O ato de selecionar não interrompe o processamento normal de backend, SMTC, IPC ou persistência.

O presenter só aplica o lote após validar todos os paths locais absolutos; entrada inválida é rejeitada atomicamente e não apaga uma descoberta já ativa. A aplicação usa a fila viva: Abrir anexa e toca a primeira nova ocorrência; Adicionar preserva a faixa corrente no momento da conclusão ou inicia a primeira nova se não houver faixa. As propriedades `selectingAudioFiles`/`audioFileSelectionFailed` e o sinal `audioFileSelectionFinished(bool changed)` separam solicitação aceita de alteração concluída. A UI também limpa marcadores quando cancelamento externo termina o estado ocupado sem esse sinal; restaurações de foco diferidas são revalidadas, inclusive na abertura da fila. Pickers síncronos legados ficam limitados a fakes/testes, sem mudar schemas de persistência. O [registro central](../../experiments/m0/candidates/qt/README.md#async-audio-picker) reúne contratos implementados, checks automatizados consolidados e riscos residuais; M2/M3 continuam parciais pelos gates restantes, sem apresentar intermitência de IPC como corrigida.

A sanitização de metadata pertence ao domínio C++ puro em `MetadataSanitization`, sem Qt, TagLib ou I/O. O resultado textual separa `Unchanged`, `Sanitized`, `Empty` e `InvalidEncoding`; os parsers de faixa/ano devolvem valores opcionais sem conversão parcial. A transformação valida escalares UTF-8, conserva os bytes dos caracteres significativos e não faz transliteração nem normalização de forma Unicode. Seu contrato de linha única e a lista fechada de controles removidos ficam no [registro central](../../experiments/m0/candidates/qt/README.md#metadata-sanitization).

O adapter aplica o limite por campo aos bytes UTF-8 originais antes da policy, conserva ausência/fallback e publica somente diagnósticos tipados `TextSanitized`/`InvalidValue` associados a `MetadataField`, que agora também distingue `TrackNumber` e `Year`. O orçamento agregado contabiliza os textos efetivamente retidos após sanitização e quatro bytes por número válido; o máximo integrado dos cinco campos é 48 KiB + 8 bytes, ainda abaixo de 256 KiB. Conversão do formato TagLib e escolha dos valores brutos permanecem no adapter, incluindo fallback numérico por campo entre subtags e aliases e a recuperação da representação binária unsigned de `trkn` MP4. A apresentação traduz códigos, não strings livres do arquivo. Os checks automatizados de core, adapter, apresentação, integração real, suíte combinada e composição de produção estão consolidados no registro central. Limites de retenção próprios não limitam alocações internas do parser de terceiros, e este recorte não altera schemas, fila, playback, corpus canônico ou decisão de stack.

## 15. Segurança e privacidade

O [filtro de processos do canal Windows](../browser-extension.md#filtro-de-imagem-dos-processos) é um adapter opcional de transporte. Perfis de paths por papel vêm da composição confiável e, no build, de siblings fixos da própria imagem. Uma verificação rápida delega PID/TokenUser/imagem a worker limitado; o transporte mantém o reader e envio desativados até Eligible. Pending usa os mesmos slots/deadline, saída aguarda retirement físico, e callbacks não podem podar ou observar estado parcialmente drenado. O processo é retido por HANDLE até shutdown; o pipe duplicado fecha antes do resultado para preservar EOF. O núcleo de domínio não recebe esses tipos, paths ou handles. Elegibilidade observada não atesta código/publicador/Brave e não habilita o workflow remoto de produção; identidade/integridade instaladas, namespace e sandbox continuam gates separados.

Os princípios de input não confiável, privilégio mínimo e ausência de rede são invariantes. Controles específicos complementam, mas não substituem, as regras comuns.

- no desafiante Tauri, o WebView carrega somente assets locais, com CSP restrita, sem navegação remota e com capabilities limitadas aos comandos/janelas necessários;
- no candidato Qt, QML não recebe paths arbitrários nem acesso direto a filesystem/plataforma; somente tipos e operações deliberadamente expostos pelo presenter ficam visíveis;
- paths/URLs nunca são executados como comandos;
- capas e tags são entrada não confiável, com limites de bytes/dimensões/texto;
- protocolos locais validam IDs opacos, não paths arbitrários vindos da UI;
- logs redigem paths, query strings e conteúdo por padrão;
- nenhuma porta de rede, telemetria, analytics ou auto-update no MVP;
- dependências passam por auditoria de licença e advisories; Qt LGPL e a configuração/licença do FFmpeg possuem gate explícito;
- `unsafe`, FFI e APIs nativas inseguras ficam em adapters ou módulos de plataforma, com invariantes documentadas.

## 16. Erros e observabilidade

No candidato combinado Windows, o [diagnóstico local opt-in](../../experiments/m0/candidates/qt/README.md#local-diagnostics) materializa a fronteira de privacidade sem persistência complexa: `DiagnosticEntry`/`DiagnosticRecord` puros, `DiagnosticSink`/`DiagnosticLogStore`, writer único com buffer fixo e store Windows com slots limitados. Só enums e números chegam ao formatter; mensagens externas não são redigidas por heurística, mas descartadas. Um binding na thread da UI projeta eventos públicos do presenter, sem I/O no callback de áudio. O bootstrap instala a proteção de Qt/stderr antes dos plugins, cria o logger apenas após opt-in e obtenção de primária, e o destrói depois dos produtores. Retenção de 3 × 256 KiB e 7 dias é aplicada no append; logs inativos não são apagados por tarefa em background. Detalhes de falha/retenção, canais cobertos e riscos residuais constam do registro central.

Taxonomia mínima:

- `UnsupportedFormat`;
- `CorruptMedia`;
- `FileUnavailable` / `PermissionDenied`;
- `OutputDeviceUnavailable`;
- `SeekUnsupported` / `SeekFailed`;
- `MetadataInvalid`;
- `PersistenceCorrupt`;
- `PlatformCapabilityUnavailable`;
- `RemoteResolutionFailed` futuro.

Erros carregam código estável, contexto seguro, possibilidade de retry e ação sugerida. A UI recebe mensagem localizada; log local recebe cadeia de causas redigida. Panic, abort e exceção não tratada são bugs, não mecanismos de erro.

Métricas de performance são produzidas por benchmarks locais/CI e comandos diagnósticos, não telemetria do usuário.

## 17. Distribuição

### Windows

- release x64 primeiro;
- instalador NSIS por usuário como baseline experimental até a decisão M0;
- file associations somente para matriz aprovada, por template/hook que cumpra o modelo de apps padrão do Windows;
- dependências do candidato são declaradas e medidas separadamente da RAM: WebView2/bootstrapper no Tauri ou DLLs, plugins QML e componentes Qt/FFmpeg no Qt;
- versão portátil é artefato separado e declara limitações;
- assinatura é requisito antes de distribuição pública, não para builds locais.

O candidato implementa o [template NSIS de associações RC Music](../../experiments/m0/candidates/qt/windows-installer/README.md) como etapa reutilizável, separada do domínio, da UI e do deployment. Usa HKCU/vista 64, ProgIDs próprios, `Capabilities`, `RegisteredApplications`, comando com executável/argumento entre aspas e `OpenWithProgids`, sem modificar padrões ou `UserChoice`. Validação prévia de conflitos, marcas de propriedade e remoção condicional preservam entradas externas e o cadastro de uma atualização concluída diante de desinstalador antigo. O recorte inicial exigia a mesma lista de formatos/descrições; concorrência geral e rollback atômico continuam fora do módulo. Seus testes compilados usam somente uma base GUID isolada, sem `SHChangeNotify` ou payload real. Não houve alteração nos identificadores Qt de armazenamento nem aceitação de stack/engine.

A integração versionada permite adicionar/remover tipos por listas compiladas em cada desinstalador, com `AssociationCleanupProtocol=2` no marker. A limpeza anterior remove apenas ProgIDs ainda pertencentes ao executável anterior, preservando tipos transferidos à nova versão; os registros globais exigem o proprietário global correspondente. Descrições dos tipos mantidos permanecem estáveis. Legado sem protocolo só aceita a mesma assinatura ou a assinatura canônica vazia `SHA256('[]')` dos artefatos OFF, sempre com o próprio `/VERIFY` e hash do desinstalador conferidos. A assinatura global registra a última tentativa de publicação, não autentica a versão ativa. Falha de cadastro/limpeza conserva as duas versões e as pendências para retry; não autoriza uma terceira atualização enquanto a limpeza estiver pendente.

O atalho do menu Iniciar pertence ao instalador por usuário, não ao domínio nem ao bootstrap do player. Seu destino acompanha a versão ativa; hashes de propriedade atual/pendente e um temporário adjacente permitem publicar a troca sem adotar entradas externas pelo nome. A remoção só atua sobre o atalho próprio da versão ativa e conserva a pasta compartilhada do menu. A raiz é explícita na receita, substituída por GUID nos testes; nenhum atalho de desktop, startup ou lançamento automático é inferido dessa integração.

### Linux

- `.deb` para distribuição de referência;
- AppImage depois de validar codecs, toolkit/runtime escolhido, tray e MIME;
- build em ambiente compatível com a glibc mínima;
- pacotes informam dependências reais e não embutem runtimes sem revisão.

O [instalador local do candidato](../../experiments/m0/candidates/qt/windows-installer/README.md) mantém binários versionados sob a raiz própria por usuário e os dados Qt fora dela. O ID inclui inventário, receita e matriz/assinatura de formatos, inclusive para correções exclusivas do instalador. Estado ativo e pendências ficam em namespace próprio do instalador; os ProgIDs continuam separados e com propriedade por executável. Upgrade prepara a nova versão, publica seu cadastro e aposenta a anterior por inventário/hash conferidos; falha no cadastro preserva evidência para retry, sem prometer rollback atômico ou reparo arbitrário de extração incompleta. Remoção não recursiva preserva arquivos desconhecidos e não remove dados pessoais. Associações permanecem OFF por padrão; habilitá-las exige `-FormatMatrix` explícito. Testes desse vínculo continuam isolados, sem aceitar stack ou distribuição.

A [matriz JSON local](../../experiments/m0/candidates/qt/windows-installer/formats.windows.local.json), revisada e versionada, tem escopo `windows-x64-local-development` e vincula hashes do manifesto de runtime, corpus e fonte do teste. `result_sha256` é uma referência à evidência para revisão humana, não uma prova automática de execução ou autenticidade. A declaração limita-se a WAV/PCM, MP3, FLAC, M4A/AAC e Ogg/Vorbis, incluindo os aliases explícitos `.mp4`/AAC e `.oga`/Vorbis: sete extensões, sem inferência pela política de descoberta. Opus/ALAC candidatos não são declarados. M4.6/M4.8, jornada nativa e distribuição permanecem abertos.

Cada release gera manifesto com versão, commit, hashes, plataforma, formatos testados e validações executadas.

## 18. Evolução para fontes remotas

```text
URL solicitada
    │
    ▼
RemoteSourceResolver
    │ policy + timeout + cancellation
    ▼
ResolvedStream { url, headers, expiry, seek_capability, metadata }
    │
    ▼
PlaybackBackend
```

O resolver não entra no domínio local nem na camada visual de qualquer candidato. URLs expiradas podem ser resolvidas novamente de modo limitado. Cache possui cota e política de limpeza. Lives e streams sem duração publicam capabilities correspondentes.

YouTube só recebe implementação após ADR específico; o caminho oficialmente permitido pode exigir player visível e, portanto, ser incompatível com a proposta audio-only/tray.

## 19. Pontos de teste por fronteira

| Fronteira | Teste principal |
|---|---|
| Domínio | unitários e propriedades sobre estado/fila/tempo |
| Application | casos de uso com fakes e relógio determinístico |
| Playback adapter | contrato comum + corpus por formato |
| Metadata adapter | fixtures válidas, ausentes, hostis e limites |
| Persistence | round-trip, migração, corrupção e falha durante replace |
| Boundary/IPC | schema, tradução de tipos, limites, input inválido e coalescing |
| UI | componentes/estados/acessibilidade sem engine real |
| Windows/Linux | smoke de tray, associações, media controls e lifecycle |
| Pacote | instalação, upgrade, abertura, desinstalação e arquivos preservados |

## 20. Decisões e gates

Decisões vigentes:

- ADR-0005 aceito: C++20/Qt Quick, Qt Multimedia/FFmpeg e TagLib; substitui ADR-0001/0002 e retira A/B como pré-requisito do produto Windows;
- ADR-0004 aceito: RC Music visível e identificadores internos preservados;
- player local-first/offline continua requisito confirmado;
- ADR-0003 continua `Proposto`: não implementar resolver remoto no MVP por causa da decisão de stack.

NFR-STACK-001–006 preservam separação do núcleo e comprovação proporcional. A migração do código experimental para uma unidade de produto ainda exige checks; os resultados históricos não provam o build promovido. Desempenho e Linux ficam para etapas posteriores, sem alegar aprovação; segurança, licenças, corpus, acessibilidade e distribuição mantêm seus próprios gates antes de release.

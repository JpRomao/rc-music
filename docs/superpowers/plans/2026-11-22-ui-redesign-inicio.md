# Redesenho da interface, fatia 4: Início — plano de implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: use superpowers:subagent-driven-development (recomendado) ou superpowers:executing-plans para executar este plano tarefa por tarefa. Os passos usam caixas (`- [ ]`) para acompanhamento.

**Objetivo:** dar à Início do modo Aurora o desenho aprovado (“Início B”):
- cabeçalho editorial com “Abrir áudio” como ação principal;
- destaque em cartão sem borda (capa de 168 px, título de 36 px, “Detalhes da faixa” e “Letra”);
- estados vazio e de erro com a peça de estado vazio;
- estantes “Na sua fila”, com capas reais e o selo “Tocando”, e “Da sua biblioteca”, com “Ver toda a biblioteca”.

Também fecha as pendências da fatia 3 que são da Início.

**Arquitetura:**
- **Arquivos novos:** `HomePage.qml` (a página Aurora inteira), `HomeHero.qml` (o cartão de destaque), `HomeShelf.qml` (cabeçalho de estante e lista horizontal), `HomeShelfCard.qml` (o cartão de faixa da estante) e `AuroraIconCircle.qml` (o círculo de ícone, extraído do `AuroraEmptyState`).
- **Modos:**
  - a página Aurora vive num `Loader` com `active: window.inlinePages`;
  - os três estados do fixture (`EmptyState`, `NowPlayingState`, `ErrorState`) passam para um `Loader` com `active: !window.inlinePages`, com a mesma geometria de hoje;
  - assim, ids repetidos entre os modos (`open-audio`, `current-title`, `cover-track`, `playing-state`, `track-details-playing`, `error-state`…) nunca convivem.
- **`ErrorState.qml`** continua sendo o único dono da lógica de erro (recuperar, tentar novamente, localizar). Só o seu ramo Aurora muda de desenho.
- **`QueuePreview.qml`** sai, substituído por `HomeShelf` + `HomeShelfCard`.
- **C++:**
  - capas por ocorrência da fila: um segundo índice e um segundo provedor de capas, sobre as classes da biblioteca;
  - `LibraryBinding::recentModel`, com as faixas mais novas da biblioteca;
  - `PlayerPresenter::nextQueueTitle`, para a linha “a próxima é”.

**Stack:** C++20, Qt 6.11.2 Quick/QML com estilo Basic, Qt Test e Qt Quick Test, CMake/Ninja.

**Spec:** [2026-11-22-ui-redesign-design.md](../specs/2026-11-22-ui-redesign-design.md). Seções usadas:
- “Linguagem visual”, “Peças” 1, 2, 9, 15 e 16;
- “Casca › Páginas”;
- “Telas › Início” e “Janela mínima”;
- “Comportamento novo” 1 e 5, só para marcar o que fica fora (ver “Decisões”);
- “Textos”, “Acessibilidade” e “Testes e validação”.

O anexo de textos é o [2026-11-22-ui-redesign-copy.md](../specs/2026-11-22-ui-redesign-copy.md), seções `NavigationText` e `ErrorText`. Os mockups aprovados são `out/product/ui-redesign-mockups-20261122/gerador/home.mjs` (opção B), `compacto.mjs` (Início a 720×520) e `kit.mjs`.

## Restrições globais

As mesmas do [plano 2](2026-11-22-ui-redesign-casca.md) (“Restrições globais”) e do [plano 3](2026-11-22-ui-redesign-gavetas.md), com estes ajustes:

- **Fixture M0 intacto:**
  - os ramos `!inlinePages`, `!editorial`, `!overlayMode` e `usesFrozenFixture` não mudam de aparência, ordem de Tab, ids nem textos;
  - passam **sem edição**: `tst_empty_shell`, `tst_high_contrast`, `tst_lifecycle`, `tst_lifecycle_transitions`, `tst_keyboard_shortcuts`, `tst_keyboard_exit`, `tst_aurora_shell`, `tst_lyrics_reader` e os casos com `testPlayerPresenter` de `tst_preferences` e `tst_accessibility_*`;
  - desses, tocam a Início do fixture (`open-audio`, `empty-title`, `current-title`, `state-layer`): `tst_empty_shell`, `tst_high_contrast`, `tst_keyboard_shortcuts`, `tst_keyboard_exit`, `tst_lifecycle`, `tst_lifecycle_transitions` e `tst_preferences`. `tst_aurora_shell` e `tst_lyrics_reader` não tocam;
  - se um deles precisar mudar, pare: é sinal de que o ramo congelado foi tocado.
- **Ids que existem nos dois modos** vivem em `Loader`s de `active` exclusivo. Dentro do `ErrorState`, os botões do fixture e os do Aurora ficam em dois `Component`s, escolhidos por um único `Loader`.
- **Texto visível** só em `*Text.qml`, com `qsTr`.
  - O scanner aceita literais só em `objectName`, `iconName`, `controlId` e `trailingIconName`, e recusa ternário com literais.
  - `"transparent"` só direto em `color:` ou `border.color:`.
  - Quando uma linha do anexo diz “troca” para uma chave que o fixture ainda lê, crie uma chave nova para o Aurora e mantenha a antiga. Nesta fatia nenhuma troca cai nesse caso: `pageDescription`, `playTrack` e `ErrorText` só são lidos pelo Aurora. Confira com `rg` antes de editar.
- **Traduções:** o `.ts` é curado à mão e só guarda plurais. Esta fatia não cria plural novo, porque reusa `NavigationText.trackCount` e `QueueText.trackCount`. Nunca rode o alvo `m0_qt_shell_ui_lupdate` nem `develop.ps1 format -Apply`.
- **Fontes:** todo `Text` do Aurora define família e tamanho com tokens do contrato (`headingFontFamily`/`bodyFontFamily`, `type*`). Nenhum `Text` fica com a fonte padrão.
- **Foco:**
  - `KeyNavigation` é recíproco; use relés de tamanho zero (modelo do `DownloadsPanel`) e não aponte de item oculto para item de fora;
  - nada focável fica sob gaveta ou sobreposição: com a gaveta da Fila aberta, a Início continua `enabled: false`, como hoje.
- **Testes Quick:**
  - rodam com `QT_FATAL_WARNINGS=1`;
  - sem esperas de tempo: use `tryVerify`, `tryCompare` e `waitForPolish`;
  - nenhum teste toca som real.
- **Arquivos QML novos** entram em `cmake/RcMusicUi.cmake`, e `QueuePreview.qml` sai de lá.
- **Ícone novo:** `folder-open.svg`.
  - Origem: Phosphor regular, de `phosphor-icons/core` no commit `2b75f3ad12b420c9504ef05df8d2564a28f8500e` (`assets/regular/folder-open.svg`).
  - Formato: o mesmo de `assets/icons/external.svg` (viewBox 256, `fill="currentColor"`, só o `path`).
  - A licença já está em `packaging/windows/notices/phosphor/`.
- **Sem dependência nova e sem telemetria.** As capas da fila são lidas do arquivo local, como as da biblioteca, sem rede.
- **Capturas:** confira cada uma a olho, nos dois tamanhos e nos dois temas, antes de dar a tarefa por pronta.

### Comandos

Os mesmos dos planos 2 e 3, mais estes:

| Para | Comando | Passou quando |
|---|---|---|
| Teste C++ do presenter | `ctest --test-dir $b -R '^rc_music\.ui\.presenter$' --output-on-failure --no-tests=error` | `100% tests passed` |
| Teste C++ da biblioteca | `ctest --test-dir $b -R '^rc_music\.library\.(binding\|artwork)$' --output-on-failure --no-tests=error` | `100% tests passed` |

A suíte Quick inteira, o contrato, o `build-boundary` e o `all_qmllint` rodam uma vez, na tarefa 8.

## Foco da revisão

1. **“A próxima é” com Aleatório ligado ou na última faixa.** A estante nunca nomeia uma faixa que não vai tocar. Com Aleatório ligado, na última faixa ou sem faixa atual, o subtítulo mostra só a contagem. Testes: tarefa 6, `nextQueueTitleIsEmptyWithShuffleOrAtTheEnd` (C++) e `test_queueSubtitleNamesTheNextOnlyWhenKnown` (Quick).
2. **Filtro da Biblioteca ativo.** A pessoa filtra a Biblioteca e volta à Início. A estante mostra as 12 faixas mais novas, sem o filtro. Teste: tarefa 5, `recentModelIgnoresTheFilterAndListsTheNewestFirst`.
3. **Título e artista longos na janela mínima.**
   - O título do destaque corta com reticências e tem o nome acessível inteiro.
   - Os botões continuam com 40 px e dentro da janela.
   - O título do cartão quebra em até duas linhas e corta depois.

   Testes: tarefa 2, `test_heroElidesLongMetadataAtMinimumWindow`; tarefa 6, `test_cardTitleWrapsToTwoLinesAndKeepsTheFullName`.
4. **Foco num botão do destaque quando o estado muda** (fila limpa, erro, recuperação). O foco vai para `open-audio` do cabeçalho, ou para a ação de recuperar no erro, e nunca se perde. Teste: tarefa 2, `test_heroFocusMovesToOpenAudioWhenTheTrackGoes`.
5. **Faixa da fila indisponível ou relocalizada.**
   - O cartão fica desabilitado, com “Arquivo não disponível.” em Aviso, e o Tab o pula.
   - Não se tenta ler a capa.
   - Depois de localizar o arquivo, a capa volta.

   Testes: tarefa 4, `queueArtworkFollowsAvailabilityAndRelocation` (C++); tarefa 6, `test_unavailableQueueCardSaysSoAndIsSkipped`.

## Decisões deste plano

- **Escopo:** a fatia 4 do spec. Ficam para os planos donos:
  - **plano 5 (Biblioteca e Tocar agora):** tocar a partir da estante “Da sua biblioteca”. Nesta fatia, os cartões dessa estante são **só de leitura**: não recebem foco, não respondem a clique e o leitor de tela os lê como texto. O spec põe a estante da Início na linha “Biblioteca” da tabela de “Comportamento novo” 5, cujo gesto (Tocar agora) é a fatia 5. Ligar um gesto provisório agora mudaria o comportamento duas vezes.
  - **plano 6 (menu da faixa):** botão direito, ⋯, tecla Menu, Shift+F10 e clique duplo nas duas estantes. O mockup da Início não mostra ▶, + nem ⋯ nos cartões, e esta fatia não os cria. A estante da fila mantém o gesto de hoje: clique e Enter tocam a partir dela.
  - **plano 11 (Configurações):** `NavigationText.settingsEyebrow` e `settingsDescription`, que estão no catálogo da Início mas são da página de Configurações.
  - **plano 13 (Avisos):** `SourceImportText.selectingFiles` e `fileSelectionFailed`, a linha de status do seletor sob a página.
  - **plano 14:** remover `TrackArtwork.frozen`, se só o fixture o usar no fim.
- **Composição em `Main.qml`:**
  - o `ColumnLayout homeLayout` (linhas ~752-802) dá lugar a um `Loader` `homeLoader` (`objectName: "home-page-loader"`, `active: window.inlinePages`, `sourceComponent` com `HomePage`), dentro do `stateLayer`;
  - `EmptyState`, `NowPlayingState` e `ErrorState` do fixture (linhas ~815-857) vão, como estão, para um `Loader` `fixtureStates` (`active: !window.inlinePages`), que preenche `stateLayer.contentItem`. Cada estado continua com `anchors.centerIn`, a largura de hoje calculada a partir de `stateLayer.width` e os mesmos `compact`;
  - o título do fixture (linha ~803) não muda;
  - `stateLayer.contentHeight` no Aurora passa a usar `homeLoader.implicitHeight`.
- **Foco por estado:**
  - `Main.playbackFocusScope(state)` pede o escopo à página ativa: `homeLoader.item.scopeFor(state)` no Aurora e os itens do `fixtureStates` no fixture. Sem item carregado, devolve `null`;
  - `Main.focusPlaybackPrimary(state)` chama `homeLoader.item.focusPrimary(state)` no Aurora;
  - no Aurora, o foco principal de Unloaded, Loading, Ready, Playing e Paused é `open-audio` do cabeçalho; o de Error é a ação de recuperar do `ErrorState` (`error-open-audio` ou `error-recover-audio-output`);
  - `open-audio` fica fora dos escopos de estado, então abrir um arquivo a partir dele não move o foco. Isso também resolve o título cortado da pendência 1: o foco não desce mais para um botão do destaque.
- **Rolagem:** `Main.revealHomeFocus` volta a `contentY = 0` quando o alvo cabe inteiro na primeira tela (`ponto.y + altura <= stateLayer.height`). Senão, mantém a regra de hoje. Assim o título da página só sai de vista quando o alvo está mais abaixo.
- **Espaço em volta do painel de letra de altura 0:**
  - no Aurora, `mainLayout.spacing` passa a 0;
  - a linha `audio-file-selection-status` ganha `Layout.topMargin: 8`;
  - no fixture, o espaçamento continua o padrão.
- **Cabeçalho:**
  - `PageHeader` com `objectName: "home-header"`;
  - rótulo `NavigationText.space` (“SEU ESPAÇO MUSICAL”), título `NavigationText.heading` e descrição `NavigationText.pageDescription`;
  - ação `AuroraButton` principal `open-audio`, com `iconName: "folder-open"`, texto `contract.openAudioText` e `enabled` igual ao `openingEnabled` de hoje;
  - a janela compacta mantém o cabeçalho inteiro, porque é nele que fica “Abrir áudio”. O mockup compacto o omite, mas omiti-lo deixaria a ação só no Ctrl+O.
- **Estado vazio:**
  - `AuroraEmptyState` `home-empty` dentro do destaque, com ícone `folder-open`, título `contract.emptyTitle` e dica `contract.emptyHint`;
  - **sem botões**, porque “Abrir áudio” está logo acima, no cabeçalho, e o spec pede uma ação principal à vista por lugar. O título e a dica formam uma frase só: “Abra um áudio para começar / ou arraste arquivos para esta janela”.
- **Destaque (`HomeHero`, `objectName: "home-hero"`):**
  - **Cartão:** `surfaceColor`, raio `radiusLarge` (16), sem borda. Em alto contraste, contorno de 2 px em `textPrimaryColor` e sem sombra.
  - **Normal:** padding 22, espaço de 28 entre capa e texto, capa `cover-track` de 168 com raio `radiusMedium`.
    - O texto tem rótulo `hero-eyebrow` (os textos de estado de hoje, `typeEyebrow`, espaçamento de letra 1,4, `textSecondaryColor`).
    - Depois vem `current-title`, em `headingFontFamily` e `typeDisplay` (36), com reticências e `Accessible.name` igual ao texto.
    - Depois, `hero-artist-album` em `typeBody` e `textSecondaryColor`: `TransportText.artistAlbum` com os dois valores, ou só o que existir.
    - Por fim, os botões, com 10 de espaço entre eles e 20 acima.
  - **Compacto** (`window.width <= contract.compactWidth`): padding 14, espaço de 16, capa de 96, título em `typeSection` (24), botões `compact`, com 8 de espaço entre eles e 10 acima.
    - Esse é o mesmo critério da lateral e do player. Ele troca o `compact: lyricsPanel.visible || …` de hoje, que deixava o destaque sempre compacto com o painel de altura 0 (pendência 2).
  - **Botões:**
    - `track-details-playing`: `AuroraButton` tonal com `info`, texto `contract.detailsTitle` (no compacto, `NavigationText.detailsShort`), `Accessible.name` sempre `contract.detailsTitle` e `enabled: presenter.detailsAvailable`;
    - `home-lyrics`: tonal com `lyrics`, texto `TransportText.lyricsAction` (“Letra”, pelo glossário, no lugar do “Letras” do mockup), `enabled` igual a `lyricsPanel.readerAvailable`, abrindo a gaveta com `lyricsPanel.showReader(opener)`.
  - Ids mantidos do fixture: `playing-state`, `cover-track`, `embedded-artwork` e `current-title`. Eles convivem só por estarem em `Loader`s exclusivos.
  - `current-artist` e `current-album` ficam só no fixture; o Aurora usa a linha única `hero-artist-album`, como o mockup.
- **Estado de erro no Aurora (`ErrorState` com `editorial: !presenter.usesFrozenFixture`):**
  - no topo, um `AuroraIconCircle` (`objectName: "error-icon-circle"`) com `warning-circle` em `errorColor`;
  - `error-title` em `headingFontFamily`/`typeTitle`, `error-body` em `typeBody`/`textSecondaryColor` e `error-context-title` em `typeCaption`, todos centrados;
  - as ações num `Flow` centrado, com os ids de hoje:
    - a de recuperar como `AuroraButton` principal;
    - `error-open-another-audio`, `error-retry-occurrence` e `error-locate-occurrence` como tonais;
    - `track-details-error` como tonal com `info`;
  - depois, `error-location-feedback`;
  - no fixture, os dois botões de hoje (`error-skip` e `track-details-error`) ficam iguais, no `Component` do fixture.
- **`AuroraIconCircle.qml`:** o círculo de 64 px que hoje está dentro do `AuroraEmptyState`.
  - Propriedades: `contract`, `iconName` e `iconColor` (padrão `accentColor`; em alto contraste, `textPrimaryColor`).
  - `objectName` padrão `empty-icon-circle`, com o ícone `empty-icon`, para os testes da peça não mudarem.
  - O `AuroraEmptyState` passa a usá-lo.
- **Capas da fila:**
  - `LibraryArtworkIndex::replace(std::vector<std::pair<std::uint64_t, std::filesystem::path>>)` troca o conteúdo inteiro;
  - `LibraryArtworkProvider::queueProviderId()` devolve `"queueart"`, e `queueSourceBase()` devolve `"image://queueart/"`;
  - o `main.cpp` registra uma segunda instância do provedor, sobre um segundo índice, com o mesmo extrator da biblioteca e só quando ele existe;
  - `PlayerPresenter::setQueueArtwork(QueueArtworkSink sink, QString source_base)`, com `using QueueArtworkSink = std::function<void(std::vector<std::pair<std::uint64_t, std::filesystem::path>>)>`:
    - entrega na hora as ocorrências disponíveis da fila local;
    - entrega de novo a cada `modelReset`, `rowsInserted`, `rowsRemoved` e `dataChanged` de `local_queue_model_`;
    - um `sink` vazio é aceito;
  - `LocalQueueListModel` ganha o papel `artworkSource` (`QString`): `base + occurrence_id.value` para item disponível, e vazio sem base ou para item indisponível;
  - **no QML:** a faixa atual usa `presenter.currentArtworkSource` quando ele não está vazio; as outras usam `artworkSource`. Isso vale para a estante e para as linhas da gaveta da Fila (pendência 4).
  - **no harness Quick:** `driver.setLocalQueueArtworkBase(base)` chama `setQueueArtwork({}, base)`, e um provedor de teste `queueart` devolve uma imagem transparente de 1×1, para o QML poder ser provado sem aviso.
- **Faixas recentes da biblioteca:**
  - `LibraryBinding::recentModel` (`Q_PROPERTY(QObject* recentModel READ recentModel CONSTANT)`) é um segundo `LibraryListModel`;
  - ele guarda os `recentLimit = 12` registros confirmados de maior `id`, do maior para o menor;
  - nunca recebe filtro e é atualizado junto com o `model` de hoje.
- **Próxima faixa:** `PlayerPresenter::nextQueueTitle` (`Q_PROPERTY(QString nextQueueTitle READ nextQueueTitle NOTIFY stateChanged)`) é o título (papel `title`) da linha `currentQueueIndex + 1` da fila local. Fica vazio:
  - com Aleatório ligado;
  - sem faixa atual;
  - na última linha, mesmo com Repetir;
  - para o convidado de sala;
  - no fixture.
- **Estantes (`HomeShelf`):**
  - cabeçalho com título `shelf-title` em `headingFontFamily`/`typeSection`, subtítulo `shelf-subtitle` em `typeCaption`/`textSecondaryColor` e o link como `AuroraButton` discreto compacto, com `arrow` à esquerda, como o `lyrics-line-open` da fatia 3 (a peça não tem ícone à direita);
  - lista horizontal com espaço de 18, sem rolagem vertical própria e com barra horizontal `AccessibleScrollBar`;
  - 12 entre o cabeçalho e os cartões, e 26 entre os blocos da página (`HomePage.spacing: 26`);
  - a lógica de Tab de hoje (`focusCard`, `leaveCardsForward`, `focusCurrentCard`, Backtab do primeiro cartão para o link) passa do `QueuePreview` para o `HomeShelf`, e só vale com `cardsFocusable`.
- **Cartão (`HomeShelfCard`):**
  - 150 de largura e capa `shelf-card-artwork` de 150×150 (`TrackArtwork`, raio `radiusMedium`);
  - selo `shelf-card-playing` (`AuroraChip` Destaque, ícone `play`, “Tocando”) a 8 px da esquerda e de baixo da capa;
  - título `shelf-card-title` em `typeBody` DemiBold, com quebra em até 2 linhas e reticências depois (é o “nome inteiro” do spec, que hoje é cortado numa linha);
  - subtítulo `shelf-card-subtitle` em `typeCaption`, numa linha, em `textSecondaryColor`, ou em `warningColor` quando avisa;
  - altura fixa, com lugar para duas linhas de título;
  - foco com `AuroraFocusRing` em volta da capa.
- **Estante “Na sua fila”** (`home-queue-shelf`, `home-queue-list`, `home-see-queue`):
  - visível com a fila local carregada;
  - subtítulo: `NavigationText.queueNext.arg(QueueText.trackCount(n), presenter.nextQueueTitle)`, ou só `QueueText.trackCount(n)` quando não há próxima;
  - cartão com foco e Accessible “Tocar %1”; clique e Enter chamam `playQueueOccurrence`;
  - o selo aparece na ocorrência atual;
  - o subtítulo do cartão é o artista, ou `NavigationText.localAudio`;
  - item indisponível: desabilitado, com `QueueText.sourceUnavailable` em Aviso;
  - “Ver fila” abre a gaveta, como hoje.
- **Estante “Da sua biblioteca”** (`home-library-shelf`, `home-library-list`, `home-see-library`):
  - visível com `library !== null && library.ready && library.count > 0`;
  - modelo `library.recentModel`;
  - subtítulo `NavigationText.trackCount(library.count)`. O “faixas guardadas” do mockup sai, porque o glossário evita “guardar”;
  - cartões só de leitura, com `Accessible.role: StaticText` e nome `NavigationText.titleAndArtist`;
  - capa `library.artworkBase + recordId`, como na Biblioteca;
  - registro indisponível: subtítulo `LibraryText.unavailable` em Aviso;
  - “Ver toda a biblioteca” chama `window.openLibrary()`.
- **Ordem de Tab do Aurora:** lateral → topo (`theme-toggle`) → `open-audio` → `track-details-playing` → `home-lyrics` → `home-see-queue` → cartões da fila → `home-see-library` → `lyrics-line-open` (quando a linha aparece) → player. No erro, as ações do erro entram no lugar dos dois botões do destaque.
- **Capturas (pendência 5):**
  - `loadEightTracks` passa por cada ocorrência (`playQueueOccurrence` + `publishLocalPlaying` + `publishLocalPosition(0, d)`, com durações de 2:41 a 4:12) e volta à faixa da cena. Assim a fila conhece todas as durações, e o subtítulo da gaveta mostra “· N min”;
  - a cena `lyrics-empty` usa como título da letra o `presenter.currentTitle`, em vez de “Luz de Inverno”.

### Textos novos e trocados

Exatos. Não há plural novo.

| Catálogo | Chave | Texto |
|---|---|---|
| `NavigationText` | `pageDescription` (troca) | `O que está tocando, sua fila e sua biblioteca.` |
| `NavigationText` | `playTrack` (troca) | `Tocar %1` |
| `NavigationText` | `queueCount` (sai; sem uso depois da estante nova) | — |
| `NavigationText` | `queueNext` (novo) | `%1, a próxima é “%2”` |
| `NavigationText` | `libraryShelf` (novo) | `Da sua biblioteca` |
| `NavigationText` | `seeLibrary` (novo) | `Ver toda a biblioteca` |
| `NavigationText` | `playingBadge` (novo) | `Tocando` |
| `NavigationText` | `detailsShort` (novo) | `Detalhes` |
| `NavigationText` | `titleAndArtist` (novo) | `%1, %2` |
| `ErrorText` | `sourceUnavailableMessage` (troca) | `O arquivo não está mais no lugar de antes. Localize o arquivo ou abra outro áudio.` |
| `ErrorText` | `invalidMediaMessage` (troca) | `O arquivo pode estar corrompido ou em um formato que o RC Music não reconhece. Abra outro áudio.` |
| `ErrorText` | `accessDeniedTitle` (troca) | `Sem permissão para abrir o arquivo` |
| `ErrorText` | `backendFailureTitle` (troca) | `Não foi possível continuar tocando` |
| `ErrorText` | `backendFailureMessage` (troca) | `Abra outro áudio para continuar.` |
| `ErrorText` | `audioOutputUnavailableMessage` (troca) | `Conecte uma saída de áudio ou escolha outra nas configurações de som do Windows e tente novamente. A faixa volta pausada.` |
| `ErrorText` | `audioOutputUnavailableInAppMessage` (troca) | `Reconecte a saída de áudio e tente novamente, ou escolha outra em Configurações > Reprodução. A faixa volta pausada.` |
| `ErrorText` | `audioOutputChangedMessage` (troca) | `A reprodução parou. Tente novamente para retomar a faixa pausada, na mesma posição quando possível.` |
| `ErrorText` | `audioOutputChangedInAppMessage` (troca) | `A reprodução parou. Tente novamente ou escolha outra saída em Configurações > Reprodução. A faixa volta pausada, na mesma posição quando possível.` |
| `ErrorText` | `unknownErrorMessage` (troca) | `Abra outro áudio para continuar.` |
| `ErrorText` | `retryAction` (troca) | `Tentar tocar novamente` |
| `ErrorText` | `retryDescription` (troca) | `Abre o mesmo arquivo novamente, sem mudar a fila.` |
| `ErrorText` | `locateDescription` (troca) | `Escolha onde o arquivo está agora. A faixa volta pausada, no mesmo lugar da fila e na mesma posição quando possível.` |
| `ErrorText` | `recoverOutputDescription` (troca) | `Abre a saída de áudio novamente e retoma esta faixa pausada, sem mudar a fila. A posição volta quando possível.` |

Os textos reusados sem mudança são estes:
- `contract.openAudioText`, `emptyTitle`, `emptyHint` e `detailsTitle`;
- `NavigationText.space`, `heading`, `queueShelf`, `seeQueue`, `localAudio`, `trackCount` e os rótulos de estado;
- `QueueText.trackCount` e `sourceUnavailable`;
- `LibraryText.unavailable`;
- `TransportText.lyricsAction` e `artistAlbum`.

O “Tentar reproduzir novamente” de `QueueText.retry` continua até o plano 6, que é o dono do menu da Fila.

---

### Task 1: página Aurora em `HomePage` e fixture em `Loader` próprio

Refatoração sem mudança visual, mais a correção do espaçamento (pendência 3).

**Arquivos:**
- Criar: `src/ui/qml/HomePage.qml` (e acrescentar em `cmake/RcMusicUi.cmake`)
- Modificar: `src/ui/qml/Main.qml` (linhas ~150-212 e ~710-860)
- Testes: `tests/ui/quick/tst_aurora_composition.qml`

**Interfaces:**
- Produz:
  - `HomePage` com as propriedades `contract`, `presenter`, `labels: ErrorText`, `library`, `compact: bool`, `lyricsAvailable: bool`, `openingEnabled: bool` e `locationFocusRestoreAllowed: bool`;
  - os sinais `openAudioRequested(Item opener)`, `detailsRequested(Item opener)`, `lyricsRequested(Item opener)`, `audioFilesOpening(Item opener)`, `queueRequested()`, `libraryRequested()` e `playRequested(string occurrenceId)`;
  - as funções `scopeFor(state: int): Item` e `focusPrimary(state: int): void`;
  - em `Main`, os ids `homeLoader` e `fixtureStates`.

- [ ] **Passo 1: teste que falha** em `tst_aurora_composition.qml`, `test_hiddenVerseLineAddsNoGap`:
  - com faixa atual e letra só de texto simples, `lyrics-panel` está visível com `implicitHeight` 0;
  - a base de `state-layer` fica exatamente 30 px acima de `player-footer`. Hoje dá 35, por causa do `spacing` do layout.
- [ ] **Passo 2: ver falhar** (35 ≠ 30).
- [ ] **Passo 3: implementar.**
  - Mova o conteúdo do `homeLayout` para `HomePage.qml`, que nesta tarefa ainda hospeda o `EmptyState`, o `NowPlayingState` e o `ErrorState` editoriais e o `QueuePreview`, como hoje.
  - Carregue a página no `homeLoader` e os estados do fixture no `fixtureStates`, conforme as “Decisões”.
  - Ligue `playbackFocusScope`/`focusPlaybackPrimary` por `scopeFor`/`focusPrimary`, sem mudar ainda o alvo do foco.
  - Aplique o espaçamento 0 no Aurora.
- [ ] **Passo 4: ver passar** `tst_aurora_composition`, `tst_aurora_navigation`, `tst_accessibility_state_focus`, `tst_error_microcopy`, `tst_metadata_layout` e os oito congelados (sem edição).
- [ ] **Passo 5: commit** `refactor: separa a Início do Aurora e os estados do fixture em Loaders`

### Task 2: cabeçalho com “Abrir áudio” e destaque sem borda

**Arquivos:**
- Criar: `src/ui/qml/HomeHero.qml` (e acrescentar em `cmake/RcMusicUi.cmake`), `assets/icons/folder-open.svg`
- Modificar: `src/ui/qml/HomePage.qml`, `src/ui/qml/Main.qml` (`revealHomeFocus`, ligações da página), `src/ui/qml/NavigationText.qml`, `src/ui/qml/EmptyState.qml` e `src/ui/qml/NowPlayingState.qml` (só o passo 6)
- Testes: `tests/ui/quick/tst_aurora_composition.qml`, `tst_aurora_navigation.qml`, `tst_accessibility_state_focus.qml`, `tst_metadata_layout.qml`, `tst_audio_file_selection.qml`, `tst_file_location.qml`, `tst_lyrics.qml` (linha ~608) e `tst_aurora_player.qml` (linhas ~1256)

**Interfaces:**
- Consome: `HomePage` (tarefa 1).
- Produz:
  - `HomeHero` com as propriedades `contract`, `presenter`, `labels`, `compact`, `lyricsAvailable` e `locationFocusRestoreAllowed`;
  - os aliases readonly `emptyScope`, `nowPlayingScope`, `errorScope` e `errorState`;
  - os sinais `detailsRequested(Item)`, `lyricsRequested(Item)` e `audioFilesOpening(Item)`;
  - `HomePage.openAudioControl: Item` (readonly).

- [ ] **Passo 1: testes que falham.**

  Em `tst_aurora_composition.qml`:
  - `test_homeHeaderOffersOpenAudio`:
    - `home-header` tem `page-eyebrow` “SEU ESPAÇO MUSICAL”, `page-title` “Bom ter sua música por perto.” e `page-description` “O que está tocando, sua fila e sua biblioteca.”;
    - `open-audio` é um `AuroraButton` principal, com `iconName` `folder-open` e 40 de altura, e continua visível e habilitado com uma faixa tocando.
  - `test_emptyHeroUsesTheEmptyStatePiece`:
    - sem faixa, `home-empty` está visível, com `empty-title` “Abra um áudio para começar”, `empty-hint` “ou arraste arquivos para esta janela” e nenhum botão visível (`home-empty-primary`/`-secondary`);
    - `home-hero` tem `border.width` 0 e raio 16.
  - `test_playingHeroMatchesTheMockup`, a 1440×900:
    - `cover-track` mede 168×168;
    - `current-title` usa `headingFontFamily` com 36 px;
    - `hero-artist-album` diz “Artista · Álbum” depois de `publishCompleteLocalMetadata`, “Artista” só com artista e fica vazio sem nenhum dos dois;
    - `track-details-playing` diz “Detalhes da faixa”;
    - `home-lyrics` diz “Letra”, só fica habilitado com letra e faixa atual e abre `lyrics-reader`;
    - fechar a gaveta devolve o foco a `home-lyrics`.
  - `test_compactHeroAtMinimumWindow`:
    - a 720×520, a capa mede 96, o título usa 24 px e `track-details-playing` mostra “Detalhes”, com `Accessible.name` “Detalhes da faixa”;
    - a 960×640, com letra sincronizada (linha do verso visível), a capa mede 168 (pendência 2).
  - `test_heroElidesLongMetadataAtMinimumWindow`: a 720×520, com título e artista de 200 caracteres:
    - `current-title` usa `Text.ElideRight` e tem `Accessible.name` inteiro;
    - `track-details-playing` e `home-lyrics` medem 40 de altura e ficam dentro de `home-hero` e da janela.

  Em `tst_aurora_navigation.qml`:
  - `test_openingAFileKeepsThePageTitleInView`: a 720×520, com letra sincronizada, foque `open-audio`, abra um arquivo e chegue a Playing com a linha do verso visível. `state-layer.contentY` fica 0 e `page-title` está inteiro dentro de `state-layer` (pendência 1).
  - `test_revealKeepsThePageTopWhenTheTargetFitsTheFirstScreen`: a 720×520, role a página até o fim e volte com Shift+Tab até `track-details-playing`. `contentY` volta a 0.
  - `test_homeTabOrder`: de `theme-toggle`, o Tab passa por `open-audio`, `track-details-playing` e `home-lyrics`, nessa ordem, e depois sai da página como hoje.

  Em `tst_accessibility_state_focus.qml`:
  - `test_heroFocusMovesToOpenAudioWhenTheTrackGoes`: com foco em `track-details-playing`, `clearQueue()` leva o foco a `open-audio`; com foco em `home-lyrics` e um erro publicado, o foco vai para `error-open-audio`.
  - Em `test_focusMovesToThePrimaryActionWhenItsPlaybackStateDisappears`, o alvo de Loading/Playing passa de `open-another-audio` para `open-audio`. A prova de entrar no erro e sair dele continua igual.

  **Trocas de mesma força:**
  - `open-another-audio` vira `open-audio` em `tst_aurora_composition` (`test_homeKeepsFocusedActionVisibleWhenResized`), `tst_metadata_layout`, `tst_audio_file_selection:132`, `tst_file_location:95` e `tst_lyrics:608`;
  - em `tst_metadata_layout`, `current-artist`/`current-album` viram `hero-artist-album` (“Artista · Álbum” e vazio), medido como `[x, y, altura]`;
  - `emptyState` vira `home-empty` em `tst_aurora_composition:685` e `tst_aurora_player:1256`.
- [ ] **Passo 2: ver falhar** (`home-header` e `hero-artist-album` não existem; a capa mede 158).
- [ ] **Passo 3: implementar** `HomeHero.qml` e o cabeçalho conforme as “Decisões”.
  - O `HomeHero` hospeda o `ErrorState` de hoje sem mudar o seu desenho; o desenho novo dele é a tarefa 3.
  - Mude `revealHomeFocus`.
  - Ligue `lyricsRequested` a `lyricsPanel.showReader(opener)` e `compact` a `window.width <= uiContract.compactWidth`.
  - Troque `pageDescription`.
- [ ] **Passo 4: ver passar** os arquivos da lista, `tst_track_details`, `tst_details_scroll_contrast`, `tst_accessible_interface_tree`, `tst_keyboard_full_journey`, `tst_primary_keyboard` e os congelados (sem edição).
- [ ] **Passo 5: commit** `feat: dá à Início o cabeçalho com Abrir áudio e o destaque sem borda`
- [ ] **Passo 6: tirar os ramos mortos.**
  - `EmptyState` e `NowPlayingState` agora só são criados pelo fixture: remova `editorial`/`spread` e tudo o que só valia com eles, deixando o ramo do fixture como está.
  - Rode os congelados sem edição e `all_qmllint` nos dois arquivos.
  - Commit `refactor: deixa EmptyState e NowPlayingState só com o ramo do fixture`.

### Task 3: estado de erro com a peça de estado vazio e textos de erro

**Arquivos:**
- Criar: `src/ui/qml/AuroraIconCircle.qml` (e acrescentar em `cmake/RcMusicUi.cmake`)
- Modificar: `src/ui/qml/AuroraEmptyState.qml`, `src/ui/qml/ErrorState.qml`, `src/ui/qml/ErrorText.qml`
- Testes: `tests/ui/quick/tst_error_microcopy.qml`, `tst_file_location.qml`, `tst_local_transport_recovery.qml`, os testes das peças que olham `empty-icon-circle`

**Interfaces:**
- Produz:
  - `AuroraIconCircle` com as propriedades `contract`, `iconName` e `iconColor: color`;
  - `ErrorState.editorial: bool`, igual a `!presenter.usesFrozenFixture`.
- Consome: `HomeHero.errorState` (tarefa 2).

- [ ] **Passo 1: testes que falham** em `tst_error_microcopy.qml`:
  - `test_auroraErrorUsesTheEmptyStateLook`, com mídia inválida:
    - `error-icon-circle` visível, com `empty-icon` `warning-circle` em `errorColor`;
    - `error-title` em `headingFontFamily` com `typeTitle`;
    - `error-body` em `bodyFontFamily`/`typeBody`;
    - `error-open-audio` é um `AuroraButton` principal e `track-details-error` um tonal com `info`, ambos com 40 de altura e sem borda.
  - `test_auroraErrorKeepsEveryRecoveryAction`: com `canRetryCurrentOccurrence` e `canLocateCurrentOccurrence` verdadeiros, `error-retry-occurrence` (“Tentar tocar novamente”) e `error-locate-occurrence` (“Localizar arquivo…”) são tonais, visíveis e alcançáveis por Tab a partir de `error-open-audio`, e cabem na janela a 720×520.

  Troque os pinos de texto, com a mesma força:
  - as linhas ~113 e ~118 para o novo `invalidMediaMessage`;
  - as linhas ~136 e ~138 para os novos `audioOutputUnavailableMessage` e `audioOutputChangedMessage`;
  - as linhas ~141, ~216 e ~218 para “Configurações > Reprodução”;
  - a linha ~328 para “Tentar tocar novamente”.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar.**
  - Extraia o círculo para `AuroraIconCircle` e use-o no `AuroraEmptyState`.
  - No `ErrorState`, aplique as fontes por modo nos três textos (padrão do `NowPlayingState`: `font: editorial ? Qt.font({...}) : contract.applicationFont`).
  - Ponha os botões em `fixtureActions` e `auroraActions`, escolhidos por um `Loader` `actions`. `focusPrimary`, `locateCurrent` e o `Connections` acham os botões pelo `actions.item`, que expõe `recovery` e `locate`.
  - O `Loader` do fixture recebe os `Layout.*` que os dois botões tinham, para a geometria não mudar.
  - Troque os 14 textos de `ErrorText`.
- [ ] **Passo 4: ver passar** os arquivos listados, `tst_accessibility_state_focus`, `tst_track_details` (o caso `track-details-error`), os testes das peças (`tst_aurora_*` que usam `AuroraEmptyState`) e os congelados (sem edição).
- [ ] **Passo 5: commit** `feat: desenha o erro da Início com a peça de estado vazio e revisa os textos de erro`

### Task 4: capa real para cada faixa da fila

**Arquivos:**
- Modificar: `src/presentation/LibraryArtworkIndex.hpp`/`.cpp`, `src/presentation/LibraryArtworkProvider.hpp`/`.cpp`, `src/presentation/LocalQueueListModel.hpp`/`.cpp`, `src/presentation/PlayerPresenter.hpp`/`.cpp`, `src/app/main.cpp` (linhas ~895-905), `tests/ui/quick/quick_main.cpp` (driver e provedor de teste), `src/ui/qml/QueuePanel.qml` (capa da linha Aurora)
- Testes: `tests/qt/LibraryArtworkTests.cpp`, `tests/ui/qtest/PlayerPresenterTests.cpp`, `tests/ui/quick/tst_aurora_player.qml`

**Interfaces:**
- Produz:
  - `void LibraryArtworkIndex::replace(std::vector<std::pair<std::uint64_t, std::filesystem::path>> entries)`;
  - `static QString LibraryArtworkProvider::queueProviderId()` e `queueSourceBase()`;
  - `using PlayerPresenter::QueueArtworkSink = std::function<void(std::vector<std::pair<std::uint64_t, std::filesystem::path>>)>`;
  - `void PlayerPresenter::setQueueArtwork(QueueArtworkSink sink, QString source_base)`;
  - o papel `artworkSource` do `LocalQueueListModel`, com `void setArtworkBase(const QString& base)`;
  - no driver de teste, `Q_INVOKABLE void setLocalQueueArtworkBase(const QString& base)`.

- [ ] **Passo 1: testes C++ que falham.**

  Em `LibraryArtworkTests.cpp`:
  - `replaceSwapsTheWholeIndex`: `replace({{7, a}, {9, b}})` acha os dois; um `replace({{9, b}})` seguinte esquece o 7;
  - `queueProviderUsesItsOwnScheme`: `queueSourceBase()` é `"image://queueart/"`, e uma segunda instância do provedor sobre outro índice serve a capa de `7`.

  Em `PlayerPresenterTests.cpp`:
  - `queueArtworkSinkReceivesTheAvailableOccurrences`: com o sink ligado, enfileire três arquivos. A última entrega tem os três valores de ocorrência com os seus caminhos. Depois de remover um, ele some.
  - `queueArtworkFollowsAvailabilityAndRelocation`: uma ocorrência indisponível fica fora da entrega e tem `artworkSource` vazio. Depois de localizar o arquivo, ela volta, com o caminho novo.
  - `queueModelExposesArtworkSourcePerOccurrence`: com base `"image://queueart/"`, a linha 0 tem `artworkSource` igual a `"image://queueart/" + valor`; sem base, fica vazio.
- [ ] **Passo 2: teste Quick que falha** em `tst_aurora_player.qml`, `test_queueRowsShowEachOccurrenceCover`:
  - com `driver.setLocalQueueArtworkBase("image://queueart/")` e três faixas, a segunda tocando com capa (`publishArtworkLocalMetadata`);
  - o `queue-row-artwork` da linha 0 tem `source` `"image://queueart/<n>"` (o `n` vem de `occurrenceId` sem o prefixo `local-`);
  - o da linha atual tem `presenter.currentArtworkSource`.

  O teste volta a base para `""` no `cleanup()`.
- [ ] **Passo 3: ver falhar** (`replace`, `setQueueArtwork` e o papel não existem).
- [ ] **Passo 4: implementar** conforme as “Decisões”.
  - O presenter liga os sinais do `local_queue_model_` no construtor e lê `local_queue_.items()`.
  - O `main.cpp` registra `queueart` ao lado de `libraryart`.
  - O harness registra o provedor de teste.
  - A linha Aurora da gaveta usa a regra de capa (atual com `currentArtworkSource`, as outras com `artworkSource`).
- [ ] **Passo 5: ver passar** `rc_music.library.artwork`, `rc_music.ui.presenter`, `tst_aurora_player`, `tst_local_queue_crud` e `tst_empty_shell` (sem edição).
- [ ] **Passo 6: commit** `feat: mostra a capa de cada faixa da fila`

### Task 5: faixas mais novas da biblioteca

**Arquivos:**
- Modificar: `src/presentation/LibraryBinding.hpp`/`.cpp` (linha ~254)
- Testes: `tests/qt/LibraryBindingTests.cpp`

**Interfaces:**
- Produz:
  - `QObject* LibraryBinding::recentModel() const`, com `Q_PROPERTY(QObject* recentModel READ recentModel CONSTANT)`;
  - `static constexpr int LibraryBinding::recentLimit = 12`.

- [ ] **Passo 1: testes que falham**, com a montagem dos testes vizinhos:
  - `recentModelIgnoresTheFilterAndListsTheNewestFirst`:
    - admita 14 arquivos (`01.mp3` a `14.mp3`) e espere o Saved;
    - `recentModel` tem 12 linhas, a 0 com título `14` e a 11 com título `03`;
    - com `setFilterText("nada")`, `visibleCount` vai a 0 e `recentModel` continua com 12.
  - `recentModelFollowsRemovals`: depois de `remove` do registro mais novo, a linha 0 passa a ser `13`.
  - `recentModelIsEmptyBeforeLoading`: antes de `start()`, `rowCount` é 0.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** um segundo `LibraryListModel` no `Impl`. Ele recebe os registros confirmados ordenados por `id` decrescente, cortados em `recentLimit`, no mesmo ponto em que o `model` recebe os seus.
- [ ] **Passo 4: ver passar** `rc_music.library.binding` e `rc_music.library.store`.
- [ ] **Passo 5: commit** `feat: expõe as faixas mais novas da biblioteca`

### Task 6: estantes “Na sua fila” e “Da sua biblioteca”

**Arquivos:**
- Criar: `src/ui/qml/HomeShelf.qml`, `src/ui/qml/HomeShelfCard.qml` (e acrescentar em `cmake/RcMusicUi.cmake`)
- Remover: `src/ui/qml/QueuePreview.qml` (e tirar de `cmake/RcMusicUi.cmake`)
- Modificar: `src/ui/qml/HomePage.qml`, `src/ui/qml/NavigationText.qml`, `src/presentation/PlayerPresenter.hpp`/`.cpp`
- Testes: `tests/ui/qtest/PlayerPresenterTests.cpp`, `tests/ui/quick/tst_aurora_composition.qml`, `tst_aurora_navigation.qml`; as bibliotecas falsas dos testes que montam a Início ganham `recentModel` (um `ListModel`)

**Interfaces:**
- Consome:
  - `LocalQueueListModel.artworkSource` e `driver.setLocalQueueArtworkBase` (tarefa 4);
  - `LibraryBinding.recentModel` (tarefa 5).
- Produz:
  - `PlayerPresenter::nextQueueTitle() const -> QString`;
  - `HomeShelf` com as propriedades `contract`, `controlId`, `listObjectName`, `linkControlId`, `title`, `subtitle`, `linkText`, `linkEnabled`, `model`, `delegate: Component` e `cardsFocusable: bool`, e o sinal `linkTriggered()`;
  - `HomeShelfCard` com as propriedades `contract`, `title`, `subtitle`, `subtitleWarning`, `artworkSource`, `playing`, `interactive` e `accessibleName`, e o sinal `activated()`.

- [ ] **Passo 1: testes C++ que falham** em `PlayerPresenterTests.cpp`:
  - `nextQueueTitleNamesTheFollowingOccurrence`: com três faixas e a primeira atual, o valor é o título da segunda;
  - `nextQueueTitleIsEmptyWithShuffleOrAtTheEnd`: fica vazio com Aleatório ligado, com a última faixa atual (mesmo com Repetir) e com a fila vazia.
- [ ] **Passo 2: testes Quick que falham** em `tst_aurora_composition.qml`:
  - `test_queueShelfMatchesTheMockup`, com 8 faixas e a segunda tocando:
    - `home-queue-shelf` tem `shelf-title` “Na sua fila”, e `home-see-queue` diz “Ver fila” e abre a gaveta;
    - cada cartão mede 150 de largura, e a capa 150×150;
    - só o cartão atual mostra `shelf-card-playing`, com “Tocando”;
    - o Accessible de um cartão é “Tocar <título>”;
    - com base de capa ligada, um cartão que não é o atual usa `"image://queueart/<n>"`.
  - `test_queueSubtitleNamesTheNextOnlyWhenKnown`:
    - `shelf-subtitle` diz “8 faixas, a próxima é “Cais””;
    - com Aleatório ligado, “8 faixas”;
    - com a última faixa atual, “8 faixas”;
    - com uma faixa só, “1 faixa”.
  - `test_cardTitleWrapsToTwoLinesAndKeepsTheFullName`: um título de 120 caracteres usa no máximo 2 linhas (`lineCount <= 2`), com `Text.ElideRight`, e o `Accessible.name` do cartão tem o título inteiro.
  - `test_unavailableQueueCardSaysSoAndIsSkipped`:
    - com o primeiro arquivo ausente, o cartão está desabilitado, e `shelf-card-subtitle` diz “Arquivo não disponível.” em `warningColor`;
    - Tab a partir de `home-see-queue` cai no segundo cartão.
  - `test_libraryShelfShowsTheNewestTracksReadOnly`, com uma biblioteca falsa (`ready`, `count: 128` e `recentModel` com 12 linhas, uma delas indisponível):
    - `home-library-shelf` tem “Da sua biblioteca” e “128 faixas”;
    - os cartões têm `activeFocusOnTab` falso, `Accessible.role` `StaticText` e o nome “Título, Artista”;
    - o indisponível mostra “Indisponível” em `warningColor`;
    - `home-see-library` diz “Ver toda a biblioteca” e abre a Biblioteca.
  - `test_libraryShelfHidesWithoutTracks`: com `count: 0`, ou com `library` nulo, `home-library-shelf` não aparece.

  Em `tst_aurora_navigation.qml`, estenda `test_homeTabOrder`: depois de `home-lyrics` vêm `home-see-queue`, os cartões da fila e `home-see-library`, e então `lyrics-line-open` (com letra sincronizada) ou `progress`.

  Troque, com a mesma força:
  - `queue-card-artwork` por `shelf-card-artwork` em `test_queueCardsKeepExactOccurrenceIdentity`;
  - a comparação com `""` vira “a capa do segundo cartão é a da sua ocorrência e difere de `currentArtworkSource`”.
- [ ] **Passo 3: ver falhar.**
- [ ] **Passo 4: implementar.**
  - A propriedade C++ segue as “Decisões”.
  - `HomeShelf` e `HomeShelfCard` seguem as medidas das “Decisões”, e a lógica de Tab vem do `QueuePreview`.
  - Na `HomePage`, monte as duas estantes com os delegados de fila e de biblioteca.
  - Troque `playTrack`, crie as chaves novas, tire `queueCount` e remova o `QueuePreview`.
- [ ] **Passo 5: ver passar** `rc_music.ui.presenter`, `tst_aurora_composition`, `tst_aurora_navigation`, `tst_aurora_player`, `tst_library`, `tst_listening_room` (a estante do convidado não muda) e `tst_empty_shell` (sem edição).
- [ ] **Passo 6: commit** `feat: troca a prévia da fila pelas estantes da fila e da biblioteca`

### Task 7: contraste e alto contraste da Início

**Arquivos:**
- Testes: `tests/ui/quick/tst_color_contrast.qml`, `tests/ui/quick/tst_aurora_composition.qml`
- Modificar: só se um par falhar (troque o token, nunca o limite)

- [ ] **Passo 1: testes** em `tst_color_contrast.qml`, nos temas claro e escuro, medindo sobre o fundo renderizado como os testes vizinhos:
  - `test_heroTextContrast`: `hero-eyebrow` e `hero-artist-album` com 4,5:1 sobre `surfaceColor`;
  - `test_shelfTextContrast`: `shelf-subtitle` e `shelf-card-subtitle` com 4,5:1 sobre `canvasColor`; o subtítulo em `warningColor` também com 4,5:1;
  - `test_playingBadgeContrast`: o `chip-label` de `shelf-card-playing` com 4,5:1 sobre o `chip-background`;
  - `test_stateIconsContrast`: o `empty-icon` de `home-empty` e o de `error-icon-circle` com 3:1 sobre o círculo.

  Em `tst_aurora_composition.qml`, `test_homeHighContrast`, em alto contraste:
  - `home-hero` tem contorno de 2 px em `textPrimaryColor` e nenhuma sombra;
  - os botões tonais do destaque e do erro têm contorno de 2 px;
  - os textos do destaque e das estantes usam as cores do sistema.
- [ ] **Passo 2: rodar.** O que passar já fica como regressão. O que falhar troca de token, com teste vermelho antes.
- [ ] **Passo 3: commit** `test: fixa o contraste da Início` (ou `fix:`, se algum token mudar)

### Task 8: capturas, documentação e validação da fatia

**Arquivos:**
- Modificar: `tests/ui/quick/tst_aurora_composition.qml` (capturas e correção dos dados das capturas), `docs/ui-tests.md`
- Capturas em `$w/out/product/ui-redesign-captures/inicio` (não versionadas)

- [ ] **Passo 1: corrigir os dados das capturas** (pendência 5), conforme as “Decisões”.
  - `loadEightTracks` passa a dar duração a todas as faixas.
  - A cena `lyrics-empty` usa o título da faixa atual.
  - Rode `test_drawerCaptures` de novo, em `.../ui-redesign-captures/gavetas`, e confira que o subtítulo da gaveta mostra “8 faixas · N min” e que a gaveta vazia nomeia a faixa do player.
- [ ] **Passo 2: capturas da Início.** Acrescente `test_homeCaptures`, a 1440×900 e 720×520, nos temas claro e escuro, com os backends falsos montados como no plano 3 e uma biblioteca falsa com 12 faixas recentes. As cenas são:
  - `home-empty`, sem faixa e com a estante da biblioteca;
  - `home-playing`, com 8 faixas, a segunda tocando, e as duas estantes;
  - `home-playing-lyrics`, a mesma cena com a linha do verso;
  - `home-error`, com arquivo ausente e localizar disponível.

  Os nomes ficam `aurora-inicio-<cena>-<largura>-<light|dark>.png`. O foco fica em `theme-toggle` e `contentY` em 0. Abra todas as 16 imagens e confira:
  - que batem com `home.mjs` (opção B) e, na compacta, com `compacto.mjs`;
  - que não há texto cortado nem sobreposto;
  - que o título da página está inteiro.
- [ ] **Passo 3: `docs/ui-tests.md`.** Acrescente a seção “Início (fatia 4)”, com:
  - os ids novos e os que mudaram (`home-header`, `home-empty`, `home-lyrics`, `hero-artist-album`, `shelf-*`, `home-library-*`; `open-another-audio`, `current-artist`/`current-album` e `queue-card-artwork` saem do Aurora);
  - a ordem de Tab;
  - os focos principais por estado;
  - a regra de rolagem;
  - as capas `queueart`;
  - a pasta das capturas.
- [ ] **Passo 4: validação da fatia:**
  - a suíte Quick inteira (0 failed, 0 XFAIL);
  - `rc_music.ui.contract`, `rc_music.ui.build-boundary`, `rc_music.ui.presenter`, `rc_music.library.binding` e `rc_music.library.artwork`;
  - `all_qmllint`, onde só vale o aviso antigo de `ListeningRoomPanel.qml:37`.

  Relate à parte o que passou, o que falhou e o que ficou pendente.
- [ ] **Passo 5: commit** `test: grava as capturas da Início e documenta a fatia 4`

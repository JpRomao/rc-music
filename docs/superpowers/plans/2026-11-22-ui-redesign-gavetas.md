# Redesenho da interface, fatia 3: linha do verso e gavetas da Fila e da Letra — plano de implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: use superpowers:subagent-driven-development (recomendado) ou superpowers:executing-plans para executar este plano tarefa por tarefa. Os passos usam caixas (`- [ ]`) para acompanhamento.

**Objetivo:** no modo Aurora, trocar a faixa de letras de 96 px com quatro botões por uma **linha do verso** de 48 px e dar às gavetas da **Fila** e da **Letra** o desenho aprovado:
- cabeçalho comum;
- barras de ícones;
- estados vazios;
- Aleatório e Repetir fora da gaveta, com F1/F2 sem depender dela.

**Arquitetura:**
- **Arquivos:** tudo continua onde está hoje:
  - `QueuePanel.qml` é a gaveta da Fila;
  - `LyricsPanel.qml` hospeda a linha e os diálogos de letra;
  - `LyricsReader.qml` é a gaveta da Letra;
  - `Main.qml` faz a composição.
- **Peça nova:** só o `LyricsLine.qml`, para a linha do verso.
- **Modos:**
  - o ramo Aurora é escolhido por `overlayMode` na Fila e por uma propriedade nova `editorial` em `LyricsPanel`/`LyricsReader`, ligada a `window.inlinePages`;
  - o ramo do fixture M0 fica como está.
- **Peças do plano 1:** `AuroraDrawerHeader`, `AuroraIconButton`, `AuroraButton`, `AuroraEmptyState`, `AuroraMenu`/`AuroraMenuItem` e `AuroraConfirmDialog`.
- **C++:** só uma propriedade derivada no `PlayerPresenter`, a duração total da fila.

**Stack:** C++20, Qt 6.11.2 Quick/QML com estilo Basic, Qt Test e Qt Quick Test, CMake/Ninja.

**Spec:** [2026-11-22-ui-redesign-design.md](../specs/2026-11-22-ui-redesign-design.md). Seções usadas:
- “Casca › Linha do verso”;
- “Telas › Gaveta da Fila” e “Telas › Gaveta da Letra”;
- “Peças” 13 (Gaveta) e 15 (Estado vazio);
- “Comportamento novo” 8;
- “Acessibilidade”.

O anexo de textos é o [2026-11-22-ui-redesign-copy.md](../specs/2026-11-22-ui-redesign-copy.md), seções `QueueText`, `LyricsText` e 4.4. O mockup aprovado é `out/product/ui-redesign-mockups-20261122/gerador/letras.mjs` (opção B e as duas gavetas), mais `compacto.mjs`.

## Restrições globais

As mesmas do [plano 2](2026-11-22-ui-redesign-casca.md) (“Restrições globais”), com estes ajustes:

- **Fixture M0 intacto:**
  - os ramos `!overlayMode`, `!inlinePages`, `!editorial` e `usesFrozenFixture` não mudam de aparência, ordem de Tab, ids nem textos;
  - os testes congelados da lista do plano 2 passam sem edição;
  - `tst_empty_shell` fixa no fixture “Adicionar arquivos”, “Remover”, “Limpar fila” e “Arquivo não disponível.”, então os textos novos da gaveta entram em chaves novas, e as chaves antigas ficam para o fixture.
- **Traduções:** o `.ts` (`src/ui/translations/music-player_pt_BR.ts`) é curado à mão e só guarda plurais.
  - Nunca rode o alvo `m0_qt_shell_ui_lupdate`.
  - Plural novo entra à mão no contexto do catálogo, com `numerus="yes"` e duas `numerusform`.
- **Scanner de texto:** aceita literais só em `objectName`, `iconName`, `controlId` e `trailingIconName`, e rejeita ternário com literais.
- **Ícones novos:** `trash.svg` e `waveform.svg`.
  - Origem: Phosphor regular, de `phosphor-icons/core` no commit `2b75f3ad12b420c9504ef05df8d2564a28f8500e` (`assets/regular/trash.svg` e `assets/regular/waveform.svg`).
  - Formato: o mesmo de `assets/icons/external.svg` (viewBox 256, `fill="currentColor"`, só o `path`).
  - A licença já está em `packaging/windows/notices/phosphor/`.
- **Medida das gavetas:** as duas têm 400 px de largura no Aurora (`Main.drawerWidth: 400`), encostadas à direita, do topo até o player.
  - O `contract.queuePanelMaximumWidth` (380) continua para o fixture.
- **Som:** nenhum teste toca som.

### Comandos

Os mesmos do plano 2. A suíte Quick inteira, o contrato, o `build-boundary` e o `all_qmllint` rodam uma vez, na tarefa 8.

| Para | Comando | Passou quando |
|---|---|---|
| Teste C++ do presenter | `ctest --test-dir $b -R '^rc_music\.ui\.presenter$' --output-on-failure --no-tests=error` | `100% tests passed` |

## Foco da revisão

1. **A faixa muda com a gaveta da Letra aberta.** A gaveta fecha, como hoje, e o foco volta a quem a abriu. Se quem abriu sumiu (a linha do verso some sem letra sincronizada), o foco vai para `player-lyrics`. Teste: tarefa 6, `test_drawerClosesAndReturnsFocusWhenTrackChanges`.
2. **Verso longo na janela mínima.**
   - A linha compacta corta o verso com reticências e o botão continua visível e com 40 px.
   - O nome acessível do verso continua inteiro.
   - Teste: tarefa 5, `test_compactLineElidesLongVerse`.
3. **Convidado da sala:**
   - F1/F2 não mudam nada;
   - a gaveta mostra a lista da sala, sem estado vazio local e sem o subtítulo de contagem local;
   - Limpar fila fica desabilitado.

   Testes: tarefa 1, `test_f1F2DoNothingForAGuest`; tarefa 3, `test_guestSeesTheRoomListInsteadOfTheEmptyState`.
4. **Fila com duração desconhecida.** O subtítulo mostra só a contagem (“3 faixas”), sem “0 min”. Testes: tarefa 2, `queueTotalDurationIsUnknownWhenAnyTrackHasNoDuration` (C++) e `test_subtitleOmitsDurationWhenUnknown` (Quick).
5. **As duas gavetas disputando o lugar:**
   - abrir a Fila com a Letra aberta (Ctrl+L ou `player-queue`) fecha a Letra;
   - abrir a Letra fecha a Fila;
   - Esc fecha a que está aberta e devolve o foco a quem a abriu.

   Teste: tarefa 6, `test_openingOneDrawerClosesTheOther`.

## Decisões deste plano

- **Escopo:** a fatia 3 do spec. Ficam para os planos donos:
  - **menu da faixa (plano 6):** o menu da linha da Fila e os seus textos (`manageLyrics`, `retry`, `retrying`, `locateDescription`, `sourceRelocated`);
  - **sala (plano 10):** o glossário “anfitrião” (`hostControls`, `hostNotAllowed`);
  - **diálogos de letra (plano 12):** os diálogos de Versões e LRCLIB, e as mensagens do `LyricsBinding.cpp`.
- **F1/F2:**
  - `Main.toggleShuffle()` e `Main.toggleRepeat()` passam a ser os únicos alvos dos atalhos;
  - no Aurora, chamam `presenter.toggleShuffleQueue()`/`toggleRepeatQueue()` quando `transport.queueModesAvailable`;
  - no fixture, chamam `queuePanel.toggleShuffle()`/`toggleRepeat()` como hoje;
  - na gaveta do Aurora, a linha `queue-modes` fica `visible: !root.overlayMode`.
- **Cabeçalho da Fila:**
  - `AuroraDrawerHeader` com `controlId: "queue-header"`, rótulo “Reprodução”, título `contract.queueTitle` (“Fila”) e subtítulo `QueueText.countAndDuration`;
  - o fechar é `queue-header-close`, com o texto “Fechar fila”;
  - os textos antigos `queue-eyebrow` e `queue-title` ficam só no fixture (`visible: !root.overlayMode`).
- **Nome do fechar:** o spec (Peças 2) pede “Fechar fila” como nome acessível; o anexo 4.4 sugere “Fechar a fila”. Vale o spec, com o mesmo texto no nome e na dica. O mesmo para “Fechar letra”.
- **Duração total:** `PlayerPresenter::queueTotalDurationMs` (`Q_PROPERTY(QVariant queueTotalDurationMs READ queueTotalDurationMs NOTIFY stateChanged)`).
  - É a soma das durações da fila local.
  - Fica nulo (`QVariant()`) com a fila vazia ou se alguma faixa não tiver duração.
  - Formato: menos de 60 min mostra “`M` min”, com `M` arredondado para cima e no mínimo 1. A partir de 60 min, “`H` h `M` min”.
- **Barra da Fila no Aurora:**
  - à esquerda, `AuroraIconButton`s com os mesmos `controlId`s de hoje:
    - `queue-add` (`plus`, “Adicionar arquivos…”);
    - `queue-move-up` (`arrow-up`);
    - `queue-move-down` (`arrow-down`);
    - `queue-remove` (`minus`, “Remover da fila”);
    - `queue-room-upload`, quando houver;
  - à direita, `queue-clear` como `AuroraButton` discreto (Quiet), com o ícone `trash`, o texto “Limpar fila…”, a tinta em `errorColor` e `Accessible.name` “Limpar a fila”;
  - as dicas de atalho e os motivos de hoje continuam (`shortcutHint`/`description`);
  - no fixture, os `QueueActionButton`s de hoje ficam iguais, cada lado num `Loader` de `active` exclusivo, porque os ids se repetem.
- **Linhas da Fila no Aurora:**
  - altura de 56;
  - capa de 40 (`TrackArtwork`, com `source` só na faixa atual, como na estante da Início);
  - na faixa atual, o número dá lugar ao ícone `waveform` em `accentColor` e o título fica em `accentColor`;
  - o estado (Localizando…, Arquivo não disponível.) aparece em `warningColor` no lugar do artista;
  - sem divisória entre linhas;
  - o nome acessível da linha (`rowAccessibleName`) não muda;
  - o texto “Arquivo não disponível.” fica, porque a seção “Gaveta da Fila” do spec o usa, e ele está fixado no fixture.
- **Estado vazio da Fila:**
  - `AuroraEmptyState` `queue-empty`, com ícone `queue`, título “A fila está vazia.” e ação principal “Abrir áudio” (a mesma chave e a mesma ação de `open-audio` da Início);
  - só para a fila local fora de sala.
- **Limpar fila:**
  - no Aurora, vira `AuroraConfirmDialog` com `controlId: "queue-clear-confirmation"` e `destructive: true`;
  - pergunta “Limpar a fila?”, consequência `clearOneMessage`/`clearManyMessage` (textos novos) e botão “Limpar fila”;
  - os ids dos botões passam a ser `queue-clear-confirmation-cancel` e `queue-clear-confirmation-confirm`;
  - o `QueueClearConfirmation` fica para o fixture; os dois ficam em `Loader`s exclusivos, porque o id da raiz se repete.
- **Linha do verso:**
  - `LyricsLine.qml`, filho de `LyricsPanel`, visível só com `editorial`;
  - no Aurora, a faixa antiga (`RowLayout` de status e botões, o verso, o texto simples e a atribuição) fica `visible: !root.editorial`;
  - os ids novos são `lyrics-line`, `lyrics-line-current`, `lyrics-line-next`, `lyrics-line-open`, `lyrics-line-status` e `lyrics-line-review`.
  - **Modo verso:**
    - com letra sincronizada, mostra o ícone `lyrics` (18, `accentColor`), o verso atual com serifa (17, até 48% da largura), o próximo em texto secundário (13) e “Abrir letra” (`AuroraButton` Quiet compacto com `arrow`);
    - o nome acessível do botão é “Abrir a letra completa”.
  - **Modo aviso:**
    - entra com alteração de versão pendente (`variantsPending`) ou com retorno da versão enviada da fila (`queuedFeedbackText`);
    - mostra o texto de status e, com `reviewSaveAvailable`, o botão tonal “Revisar alterações”, que abre as Versões;
    - o aviso tem precedência sobre o verso.
  - Sem nenhum dos dois modos, a linha some e `LyricsPanel.implicitHeight` vale 0 no Aurora.
  - O painel continua visível como hospedeiro: os diálogos e a gaveta precisam dele.
  - **Medidas:**
    - 48 px de altura, raio 12, `surfaceColor`, margens de 32 dos lados, 6 em cima e 12 embaixo;
    - na janela compacta (`window.width <= compactWidth`), 40 px, margens de 16, só o verso atual e um `AuroraIconButton` (`arrow`) com o texto “Abrir a letra completa”;
    - em alto contraste, contorno de 2 px em `textPrimaryColor`.
- **Ordem de Tab:** o spec pede lateral → topo → página → linha do verso → player.
  - `Main.pageExitFocusItem` passa a ser `lyricsPanel.lineEntryItem` quando ele existe e está visível e habilitado; senão, `transport.firstControl`.
  - O Backtab do primeiro controle do player segue a cadeia natural até a linha.
  - Não crie `KeyNavigation` de item oculto para item de fora (ele é recíproco; veja os relés do `DownloadsPanel`).
- **Gaveta da Letra no Aurora:**
  - `LyricsReader` ganha `editorial`, `drawerWidth` e `title`/`eyebrow`/`subtitle`, preenchidos pelo `LyricsPanel`.
  - **Geometria:** largura `drawerWidth` (400), do topo até o player, com `padding` 22 dos lados e 20 em cima, como no mockup.
  - **Cabeçalho:** `AuroraDrawerHeader` `lyrics-drawer`:
    - rótulo “Letra sincronizada” com letra sincronizada, “Letra” nos outros casos;
    - título `lyrics.title`;
    - subtítulo “Versão: <nome> · <artista>”, onde o nome vem de `lyrics.variants`, pelo item com `id === lyrics.selectedVariantId`, campo `name`;
    - sem versão escolhida, o subtítulo é só o artista.
  - **Barra:**
    - `lyrics-drawer-versions` (Tonal compacto): “Versões”, ou “Revisar alterações” com `reviewSaveAvailable`;
    - `lyrics-drawer-search` (Tonal compacto, `search`): “Buscar letra”;
    - `lyrics-drawer-more` (`AuroraIconButton` tonal `more`): “Mais ações”, visível só com `romajiAvailable`; abre um `AuroraMenu` com `lyrics-drawer-romaji` (“Gerar romaji”);
    - à direita, `lyrics-drawer-smaller` (`minus`), o rótulo “Aa” e `lyrics-drawer-larger` (`plus`), com os nomes de hoje (“Diminuir tamanho da letra”/“Aumentar tamanho da letra”);
    - os controles antigos `lyrics-reader-close`, `-smaller` e `-larger` ficam `visible: !root.editorial`.
  - **Versos:**
    - o atual em `accentColor`, DemiBold e `textSize + 4`, sem caixa de borda;
    - os passados em `textSecondaryColor`; os seguintes em `textPrimaryColor`;
    - o anel de foco do cursor de teclado continua (3 px);
    - em alto contraste, o atual usa as cores de sistema de hoje.
  - **Status:** `lyrics-drawer-status`, abaixo da barra, mostra o texto de status de hoje (`lyrics-status`) quando houver, anunciado de forma educada (`Accessible.Polite`) ao mudar.
  - **Estado vazio:**
    - sem letra sincronizada e sem texto simples, `AuroraEmptyState` `lyrics-drawer-empty`, com ícone `lyrics`;
    - título “Esta faixa ainda não tem letra.” (`TransportText.lyricsUnavailable`) e dica “Busque no LRCLIB ou adicione a letra você mesmo.”;
    - ações “Buscar letra” (principal; some sem `searchAvailable`) e “Adicionar versão” (tonal; só com `variantsAvailable && lyrics.variantsCanEdit`; abre o formulário de adicionar das Versões).
  - **Abertura:**
    - `player-lyrics` fica habilitado com faixa atual e binding de letra (`window.lyrics !== null && presenter.hasCurrentTrack`), mesmo sem letra, e abre a gaveta no estado vazio;
    - `LyricsPanel.showReader(opener)` guarda quem abriu, e o fechar devolve o foco a ele;
    - se quem abriu não estiver mais visível e habilitado, o foco vai para `player-lyrics`.
- **Gavetas exclusivas:**
  - `window.setQueueOpen(true)` fecha a Letra;
  - `lyricsPanel.showReader()` fecha a Fila (`setQueueOpen(false)`) antes de abrir;
  - a Letra continua não modal e sem prender o foco; a Fila continua com fundo escurecido e prendendo o foco.

### Textos novos e trocados

Exatos. Os plurais vão para o `.ts` à mão.

| Catálogo | Chave | Texto |
|---|---|---|
| `QueueText` | `drawerEyebrow` (troca) | `Reprodução` |
| `QueueText` | `trackCount` (novo, plural) | `%n faixa` / `%n faixas` |
| `QueueText` | `durationMinutes` (novo) | `%1 min` |
| `QueueText` | `durationHours` (novo) | `%1 h %2 min` |
| `QueueText` | `countAndDuration` (novo) | `%1 · %2` |
| `QueueText` | `closeQueue` (novo) | `Fechar fila` |
| `QueueText` | `addFiles` (novo; o `add` fica para o fixture) | `Adicionar arquivos…` |
| `QueueText` | `removeFromQueue` (novo; o `remove` fica para o fixture) | `Remover da fila` |
| `QueueText` | `clearEllipsis` (novo) | `Limpar fila…` |
| `QueueText` | `clearName` (novo) | `Limpar a fila` |
| `QueueText` | `clearTitle` (troca) | `Limpar a fila?` |
| `QueueText` | `clearOneMessage` (troca) | `A faixa sai da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito.` |
| `QueueText` | `clearManyMessage` (troca) | `As {count} faixas saem da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito.` |
| `QueueText` | `emptyTitle` (novo) | `A fila está vazia.` |
| `LyricsText` | `openLyrics` (novo) | `Abrir letra` |
| `LyricsText` | `openLyricsName` (novo) | `Abrir a letra completa` |
| `LyricsText` | `closeReader` (troca) | `Fechar letra` |
| `LyricsText` | `synchronizedEyebrow` (novo) | `Letra sincronizada` |
| `LyricsText` | `lyricsEyebrow` (novo) | `Letra` |
| `LyricsText` | `versionAndArtist` (novo) | `Versão: %1 · %2` |
| `LyricsText` | `textSizeLabel` (novo) | `Aa` |
| `LyricsText` | `emptyHint` (novo) | `Busque no LRCLIB ou adicione a letra você mesmo.` |
| `LyricsText` | `reviewSave` (troca) | `Revisar alterações` |
| `LyricsText` | `reviewLyricsSave` (troca) | `Revisar alterações não salvas da letra` |
| `ControlsText` | `moreActions` (novo) | `Mais ações` |

---

### Tarefa 1: F1/F2 sem a gaveta, Aleatório e Repetir fora dela

**Arquivos:**
- Modificar: `src/ui/qml/Main.qml` (atalhos F1/F2, linhas ~1291-1314; funções novas), `src/ui/qml/QueuePanel.qml` (`queue-modes`)
- Testes: `tests/ui/quick/tst_primary_keyboard.qml`, `tests/ui/quick/tst_aurora_player.qml` (linha ~542), `tests/ui/quick/tst_listening_room.qml` (linhas ~325 e ~343)

**Interfaces:**
- Produz: `Main.toggleShuffle(): void` e `Main.toggleRepeat(): void`.

- [ ] **Passo 1: testes que falham.** Em `tst_primary_keyboard.qml`:
  - `test_f1F2WorkWithTheQueueClosed`: com fila local carregada e a gaveta fechada, F1 inverte `presenter.shuffleQueueEnabled` e `player-shuffle.checked` acompanha; F2 faz o mesmo com `repeatQueueEnabled` e `player-repeat`. `findChild(shell, "queue-panel").visible` continua falso.
  - `test_queueDrawerHasNoShuffleOrRepeat`: com a gaveta aberta, `queue-shuffle` e `queue-repeat` têm `visible` falso, e Tab a partir de `queue-header-close`, ou do primeiro controle da gaveta, nunca para neles.
  - Troque `test_queueModesHaveVisibleAccessibleControls` para conferir `player-shuffle`/`player-repeat`:
    - nome “Modo aleatório”/“Repetir fila”;
    - `Accessible.description` “F1”/“F2”;
    - F2 e Enter mudam `checked`/`Accessible.checked`.

    É a mesma força de verificação de hoje. Só o alvo mudou, porque os controles saíram da gaveta.

  Em `tst_listening_room.qml`:
  - `test_f1F2DoNothingForAGuest`: como convidado, F1 e F2 não mudam `shuffleQueueEnabled` nem `repeatQueueEnabled`;
  - os laços das linhas ~325 e ~343 passam a conferir só `player-shuffle`/`player-repeat`.

  Em `tst_aurora_player.qml` (~542): a checagem de ícone e de texto de `queue-shuffle`/`queue-repeat` vira a checagem de que eles não aparecem na gaveta do Aurora.
- [ ] **Passo 2: rodar e ver falhar** os testes novos: `queue-shuffle` ainda visível.
- [ ] **Passo 3: implementar.**
  - `Main.toggleShuffle()`/`toggleRepeat()`: no Aurora, `if (transport.queueModesAvailable) presenter.toggle…()`; no fixture, `queuePanel.toggle…()`.
  - Os `Shortcut`s F1/F2 chamam essas funções.
  - Em `QueuePanel.qml`, a linha `queue-modes` fica com `visible: !root.overlayMode`.
- [ ] **Passo 4: rodar e ver passar** `tst_primary_keyboard`, `tst_aurora_player`, `tst_listening_room` e `tst_listening_room_roles`.
- [ ] **Passo 5: commit** `feat: desliga F1/F2 da gaveta e tira Aleatório e Repetir da Fila`

### Tarefa 2: cabeçalho e barra da gaveta da Fila

**Arquivos:**
- Modificar: `src/presentation/PlayerPresenter.hpp`/`.cpp`, `src/ui/qml/QueuePanel.qml`, `src/ui/qml/QueueText.qml`, `src/ui/translations/music-player_pt_BR.ts`, `src/ui/qml/Main.qml` (`drawerWidth`; largura e borda da gaveta)
- Criar: `assets/icons/trash.svg`
- Testes: `tests/ui/qtest/PlayerPresenterTests.cpp`, `tests/ui/quick/tst_aurora_player.qml`, `tests/ui/quick/tst_local_queue_crud.qml`

**Interfaces:**
- Produz:
  - `PlayerPresenter::queueTotalDurationMs() const -> QVariant`;
  - `QueuePanel.subtitleText: string` (readonly);
  - `Main.drawerWidth: int` (400).

- [ ] **Passo 1: testes C++ que falham** em `PlayerPresenterTests.cpp`:
  - `queueTotalDurationSumsKnownDurations`: duas faixas, de 90 000 e 120 000 ms, dão `210000`;
  - `queueTotalDurationIsUnknownWhenAnyTrackHasNoDuration`: uma faixa sem duração dá `QVariant` nulo;
  - `queueTotalDurationIsUnknownForAnEmptyQueue`.

  Use o mesmo padrão de montagem de fila dos testes vizinhos.
- [ ] **Passo 2: testes Quick que falham** em `tst_aurora_player.qml`:
  - `test_queueDrawerHeaderShowsCountAndDuration`: com 3 faixas de 600 000 ms cada, `queue-header` mostra `drawer-eyebrow` “Reprodução”, `drawer-title` “Fila” e `drawer-subtitle` “3 faixas · 30 min”; `queue-header-close` tem `Accessible.name` “Fechar fila” e devolve o foco a quem abriu; a gaveta mede 400 de largura.
  - `test_subtitleOmitsDurationWhenUnknown`: uma faixa sem duração dá o subtítulo “1 faixa”.
  - `test_durationRoundsUpAndShowsHours`: 3 601 000 ms somados dão “1 h 1 min”; 61 000 ms dão “2 min”.
  - `test_queueToolbarUsesAuroraIconButtons`:
    - `queue-add`, `queue-move-up`, `queue-move-down` e `queue-remove` são `AuroraIconButton`, com 40×40 e nomes “Adicionar arquivos…”, “Mover para cima”, “Mover para baixo” e “Remover da fila”;
    - as descrições de hoje continuam: Alt+↑, Alt+↓, Delete e os motivos da sala;
    - `queue-clear` mostra o texto “Limpar fila…”, tem `Accessible.name` “Limpar a fila” e tinta com 4,5:1 sobre `surfaceColor`, medida como nos testes de contraste do plano 2.

  Em `tst_local_queue_crud.qml`, troque as checagens de `queue-eyebrow`/`queue-title`/nomes da barra que existirem para os ids e textos novos, com a mesma força.
- [ ] **Passo 3: ver falhar** (`queueTotalDurationMs` não existe; `queue-header` não existe).
- [ ] **Passo 4: implementar.**
  - A propriedade C++ notifica junto com `stateChanged`.
  - O subtítulo junta `trackCount` com `countAndDuration` quando há duração.
  - O cabeçalho e a barra seguem as “Decisões”.
  - O ícone `trash.svg` segue as “Restrições”.
  - Os botões do fixture ficam em `Loader` com `active: !root.overlayMode`, e os do Aurora em `Loader` com `active: root.overlayMode`; os ids de dentro são os mesmos. As ligações de hoje (`moveUpButton.enabled` etc.) passam pelos `item`s dos Loaders ou por propriedades calculadas na raiz.
  - A gaveta usa `window.drawerWidth` no Aurora.
- [ ] **Passo 5: ver passar** o teste C++ do presenter, `tst_aurora_player`, `tst_local_queue_crud`, `tst_local_queue_deferred_focus`, `tst_keyboard_full_journey` e `tst_empty_shell` (este sem edição).
- [ ] **Passo 6: commit** `feat: dá à gaveta da Fila o cabeçalho e a barra do redesenho`

### Tarefa 3: linhas e estado vazio da Fila

**Arquivos:**
- Modificar: `src/ui/qml/QueuePanel.qml`, `src/ui/qml/QueueText.qml`
- Criar: `assets/icons/waveform.svg`
- Testes: `tests/ui/quick/tst_aurora_player.qml`, `tests/ui/quick/tst_listening_room.qml`

- [ ] **Passo 1: testes que falham** em `tst_aurora_player.qml`:
  - `test_queueRowsMatchTheDrawerDesign`, com 3 faixas, a segunda tocando:
    - cada `queue-row-*` mede 56 de altura;
    - tem `queue-row-artwork` de 40×40;
    - na linha atual, `queue-row-now-icon` (`iconName` `waveform`) está visível, `queue-row-number` não, e `queue-row-title.color` é `accentColor`;
    - não há `queue-row-divider` visível;
    - o `Accessible.name` da linha é igual ao de antes da mudança, então compare com `rowAccessibleName` calculado no teste.
  - `test_unavailableRowShowsStatusInWarning`: uma faixa indisponível mostra `queue-row-status` “Arquivo não disponível.” em `warningColor`, no lugar do artista.
  - `test_emptyQueueShowsTheEmptyState`: com a fila local vazia e a gaveta aberta, `queue-empty` aparece com `empty-title` “A fila está vazia.”; `queue-empty-primary` diz “Abrir áudio” e dispara a mesma seleção de arquivos de `open-audio` (confira pelo driver de seleção, como os testes de `open-audio`); `queue-list` não aparece.

  Em `tst_listening_room.qml`, `test_guestSeesTheRoomListInsteadOfTheEmptyState`: o convidado com a fila local vazia vê `room-queue-list` e não vê `queue-empty`.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** conforme as “Decisões”. O delegado do fixture não muda: use `overlayMode` nas propriedades, ou componentes de delegado separados, escolhidos por `overlayMode`.
- [ ] **Passo 4: ver passar** `tst_aurora_player`, `tst_listening_room`, `tst_empty_shell` (sem edição), `tst_local_queue_crud` e `tst_queue_microcopy`.
- [ ] **Passo 5: commit** `feat: desenha as linhas e o estado vazio da gaveta da Fila`

### Tarefa 4: confirmação de limpar a fila com a peça de diálogo

**Arquivos:**
- Modificar: `src/ui/qml/QueuePanel.qml`, `src/ui/qml/QueueText.qml`
- Testes: `tests/ui/quick/tst_local_queue_crud.qml`, `tst_local_queue_deferred_focus.qml`, `tst_keyboard_full_journey.qml` e `tst_primary_keyboard.qml` (onde usam a confirmação)

- [ ] **Passo 1: testes que falham.** Em `tst_local_queue_crud.qml`, `test_clearAsksWithTheDangerDialog`:
  - com 2 faixas, `queue-clear` abre `queue-clear-confirmation` (um `AuroraConfirmDialog`);
  - o `dialog-title` diz “Limpar a fila?”;
  - o `confirm-consequence` diz “As 2 faixas saem da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito.”;
  - o foco abre em `queue-clear-confirmation-cancel`;
  - `queue-clear-confirmation-confirm` diz “Limpar fila”, é do tipo perigo preenchido e limpa a fila;
  - Esc e Cancelar devolvem o foco a `queue-clear`.

  Com 1 faixa, o texto é o de `clearOneMessage`. Troque os ids `queue-clear-cancel`/`queue-clear-confirm` nos testes Aurora listados para os novos, com a mesma força.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** conforme as “Decisões”. O `confirmClear()` e a devolução de foco de hoje passam a responder a `confirmed` e `closed` do diálogo novo.
- [ ] **Passo 4: ver passar** os quatro arquivos e `tst_empty_shell` (sem edição).
- [ ] **Passo 5: commit** `feat: pede a confirmação de limpar a fila com o diálogo de perigo`

### Tarefa 5: linha do verso

**Arquivos:**
- Criar: `src/ui/qml/LyricsLine.qml` (e acrescentar em `cmake/RcMusicUi.cmake`)
- Modificar: `src/ui/qml/LyricsPanel.qml`, `src/ui/qml/LyricsText.qml`, `src/ui/qml/Main.qml` (`editorial` e `compact` no painel; `pageExitFocusItem`)
- Testes: `tests/ui/quick/tst_lyrics.qml`, `tests/ui/quick/tst_lyrics_variants.qml`, `tests/ui/quick/tst_aurora_navigation.qml`, `tests/ui/quick/tst_aurora_composition.qml`

**Interfaces:**
- Produz:
  - `LyricsPanel.editorial: bool`;
  - `LyricsPanel.compact: bool`;
  - `LyricsPanel.lineEntryItem: Item` (readonly; o botão da linha, ou `null` quando a linha some);
  - `LyricsPanel.lineVisible: bool` (readonly).
- Consome: `Main.pageExitFocusItem` (plano 2).

- [ ] **Passo 1: testes que falham.**

  Em `tst_lyrics.qml`:
  - `test_verseLineShowsCurrentAndNextVerse`: com letra sincronizada, `lyrics-line` mede 48 de altura; `lyrics-line-current` mostra `lyrics.currentLine` com a fonte de títulos; `lyrics-line-next` mostra `nextLine`; `lyrics-line-open` tem o texto “Abrir letra”, `Accessible.name` “Abrir a letra completa” e abre `lyrics-reader`.
  - `test_verseLineHidesWithoutSynchronizedLyrics`:
    - com letra simples, com letra protegida ou sem letra, `lyrics-line` não aparece;
    - `lyrics-panel.implicitHeight` é 0;
    - os controles antigos `lyrics-versions`, `lyrics-search`, `lyrics-generate-romaji` e `lyrics-open-reader` não aparecem no Aurora.
  - `test_compactLineElidesLongVerse`: a 720×520, com um verso de 300 caracteres, a linha mede 40; `lyrics-line-current` usa `Text.ElideRight` e tem `Accessible.name` com o verso inteiro; `lyrics-line-next` não aparece; `lyrics-line-open` mede 40×40 e fica dentro da janela.
  - `test_verseLineHighContrastOutline`: em alto contraste, o contorno é de 2 px em `textPrimaryColor`.

  Em `tst_lyrics_variants.qml`, `test_verseLineShowsPendingChangeAndReview`: com alteração pendente, `lyrics-line-status` mostra o status de hoje; `lyrics-line-review` diz “Revisar alterações” e abre as Versões. Troque nos testes de hoje:
  - `lyrics-status` por `lyrics-line-status`;
  - o texto da linha 718 por “Revisar alterações”.

  Em `tst_aurora_navigation.qml`, `test_tabVisitsTheVerseLineBetweenPageAndPlayer`: com letra sincronizada, Tab parte da Início e Shift+Tab do player. A sequência tem `lyrics-line-open` logo antes de `progress` e logo depois do último controle da página; sem letra sincronizada, a página vai direto para `progress`, como no plano 2.

  Em `tst_aurora_composition.qml`, `test_verseLineSitsAboveThePlayer`: a base de `lyrics-line` fica 12 px acima do topo de `player-footer`, nas larguras 1440 e 720.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** `LyricsLine.qml` conforme as “Decisões” (modo verso, modo aviso, compacto e alto contraste). Depois:
  - ligue `editorial: window.inlinePages` e `compact` no `LyricsPanel` em `Main.qml`;
  - esconda a faixa antiga no Aurora;
  - mude `Main.pageExitFocusItem`.

  O `LyricsLine` recebe o que precisa por propriedades (`currentLine`, `nextLine`, `statusText`, `reviewAvailable`, `compact`) e emite `openRequested(Item opener)` e `reviewRequested()`. Ele não conhece o binding.
- [ ] **Passo 4: ver passar** `tst_lyrics`, `tst_lyrics_variants`, `tst_lyrics_reader`, `tst_aurora_navigation`, `tst_aurora_composition`, `tst_downloads` e `tst_download_lyrics`.
- [ ] **Passo 5: commit** `feat: troca a faixa de letras pela linha do verso`

### Tarefa 6: gaveta da Letra

**Arquivos:**
- Modificar: `src/ui/qml/LyricsReader.qml`, `src/ui/qml/LyricsPanel.qml`, `src/ui/qml/LyricsText.qml`, `src/ui/qml/ControlsText.qml`, `src/ui/qml/Main.qml` (`player-lyrics` habilitado; gavetas exclusivas)
- Testes: `tests/ui/quick/tst_lyrics.qml`, `tests/ui/quick/tst_lyrics_variants.qml`, `tests/ui/quick/tst_aurora_player.qml`

**Interfaces:**
- Consome: `Main.drawerWidth` (tarefa 2), `LyricsPanel.editorial` (tarefa 5).
- Produz:
  - `LyricsPanel.showReader(opener: Item): void`;
  - `LyricsPanel.closeReader(): void`;
  - `LyricsPanel.readerOpen: bool` (readonly);
  - `LyricsReader.editorial: bool`;
  - `LyricsReader.drawerWidth: int`;
  - `LyricsReader.eyebrow`, `LyricsReader.title` e `LyricsReader.subtitle` (string).

- [ ] **Passo 1: testes que falham.**

  Em `tst_lyrics.qml`:
  - `test_lyricsDrawerHeaderAndToolbar`:
    - com letra sincronizada, a gaveta aberta mede 400 de largura e vai do topo até o player;
    - `lyrics-drawer` tem `drawer-eyebrow` “Letra sincronizada”, `drawer-title` igual ao título e `drawer-subtitle` “Versão: <nome> · <artista>”;
    - `lyrics-drawer-close` tem o nome “Fechar letra”;
    - `lyrics-drawer-versions` diz “Versões” e abre as Versões;
    - `lyrics-drawer-search` diz “Buscar letra” e abre `lyrics-query-dialog`;
    - `lyrics-drawer-smaller`/`-larger` mudam o tamanho como os botões de hoje.
  - `test_romajiLivesInTheMoreMenu`: com romaji disponível, `lyrics-drawer-more` (nome “Mais ações”) abre o menu com `lyrics-drawer-romaji` (“Gerar romaji”), que abre as Versões para o romaji; sem romaji, `lyrics-drawer-more` não aparece.
  - `test_currentVerseIsAccentWithoutBorder`: o verso atual tem `color` igual a `accentColor`, `font.weight` DemiBold e nenhuma borda; um verso passado usa `textSecondaryColor`.
  - `test_emptyLyricsDrawer`:
    - sem letra, `player-lyrics` está habilitado e abre a gaveta;
    - `drawer-eyebrow` diz “Letra”;
    - `lyrics-drawer-empty` mostra “Esta faixa ainda não tem letra.” e a dica;
    - `lyrics-drawer-empty-primary` (“Buscar letra”) abre a busca;
    - `lyrics-drawer-empty-secondary` (“Adicionar versão”) abre o formulário de adicionar.
  - `test_drawerClosesAndReturnsFocusWhenTrackChanges`: abra pela linha do verso e troque para uma faixa sem letra sincronizada. A gaveta fecha e o foco vai para `player-lyrics`. Abrindo e fechando pela mesma faixa, o foco volta a `lyrics-line-open`.
  - `test_drawerStatusIsAnnounced`: um status novo do binding aparece em `lyrics-drawer-status`.

  Em `tst_aurora_player.qml`, `test_openingOneDrawerClosesTheOther`: com a Letra aberta, Ctrl+L fecha a Letra e abre a Fila; Esc fecha a Fila e devolve o foco a quem a abriu; `player-lyrics` com a Fila aberta fecha a Fila e abre a Letra.

  Atualize os testes de hoje que procuram `lyrics-reader-close` ou o nome “Fechar letra completa” no Aurora.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** conforme as “Decisões”. O `LyricsReader` emite `versionsRequested`, `searchRequested`, `romajiRequested` e `addVersionRequested`, e o `LyricsPanel` liga esses sinais aos diálogos que já existem. O ramo `!editorial` do `LyricsReader` fica igual, porque `tst_lyrics_reader` o monta sozinho.
- [ ] **Passo 4: ver passar** `tst_lyrics`, `tst_lyrics_reader` (sem edição), `tst_lyrics_variants`, `tst_aurora_player`, `tst_aurora_navigation` e `tst_download_lyrics`.
- [ ] **Passo 5: commit** `feat: dá à gaveta da Letra o cabeçalho, a barra e o estado vazio`

### Tarefa 7: contraste e alto contraste das gavetas e da linha

**Arquivos:**
- Testes: `tests/ui/quick/tst_color_contrast.qml`, `tests/ui/quick/tst_aurora_player.qml`
- Modificar: só se um par falhar (troque o token, nunca o limite)

- [ ] **Passo 1: testes** em `tst_color_contrast.qml`, nos temas claro e escuro, medindo sobre o fundo renderizado como os testes vizinhos:
  - `test_verseLineTextContrast`: `lyrics-line-current` e `lyrics-line-next` com 4,5:1 sobre `surfaceColor`;
  - `test_drawerCurrentVerseContrast`: o verso atual em `accentColor` com 4,5:1 sobre o fundo da gaveta;
  - `test_queueStatusAndNowContrast`: `queue-row-status` em `warningColor` com 4,5:1; o título da linha atual em `accentColor` com 4,5:1; o ícone `waveform` com 3:1;
  - `test_clearQueueInkContrast`: a tinta de `queue-clear` com 4,5:1.

  Em `tst_aurora_player.qml`, `test_drawersHighContrast`: em alto contraste, as duas gavetas e a linha não têm sombra, e os botões tonais têm contorno de 2 px.
- [ ] **Passo 2: rodar.** O que passar já fica como regressão. O que falhar troca de token, com teste vermelho antes.
- [ ] **Passo 3: commit** `test: fixa o contraste da linha do verso e das gavetas` (ou `fix:`, se algum token mudar)

### Tarefa 8: capturas, documentação e validação da fatia

**Arquivos:**
- Modificar: `tests/ui/quick/tst_aurora_composition.qml` (capturas), `docs/ui-tests.md`
- Capturas em `$w/out/product/ui-redesign-captures/gavetas` (não versionadas)

- [ ] **Passo 1: capturas.** Acrescente casos de captura (com `RC_MUSIC_UI_ARTIFACT_DIR`), com os backends falsos montados como no plano 2, a 1440×900 e 720×520, nos temas claro e escuro:
  - a linha do verso;
  - a gaveta da Letra com letra;
  - a gaveta da Letra vazia;
  - a gaveta da Fila com 8 faixas (uma tocando e uma indisponível);
  - a Fila vazia;
  - a confirmação de limpar.

  Os nomes ficam `aurora-gavetas-<cena>-<tema>.png`. Abra pelo menos uma clara e uma escura e confira que as cenas batem com o mockup.
- [ ] **Passo 2: `docs/ui-tests.md`.** Acrescente:
  - os ids novos;
  - a ordem de Tab com a linha do verso;
  - as gavetas exclusivas;
  - F1/F2 fora da gaveta;
  - a pasta das capturas.
- [ ] **Passo 3: validação da fatia:**
  - a suíte Quick inteira (0 failed, 0 XFAIL);
  - `rc_music.ui.contract`, `rc_music.ui.build-boundary` e `rc_music.ui.presenter`;
  - `all_qmllint`, onde só valem os 2 avisos antigos (ListeningRoomPanel.qml:37, QueuePanel.qml:154 ou a linha nova do mesmo aviso).
- [ ] **Passo 4: commit** `test: grava as capturas das gavetas e documenta a fatia 3`

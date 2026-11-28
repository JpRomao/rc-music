# Redesenho da interface, fatia 5: Biblioteca e Tocar agora — plano de implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: use superpowers:subagent-driven-development (recomendado) ou superpowers:executing-plans para executar este plano tarefa por tarefa. Os passos usam caixas (`- [ ]`) para acompanhamento.

**Objetivo:** dar à Biblioteca do modo Aurora o desenho aprovado e o gesto novo “Tocar agora”:
- barra com o campo de filtro, a contagem e a troca Lista/Capas;
- lista de linhas de 56 px (#, capa e faixa, álbum, tempo), com onda na faixa que toca e ▶, + e ⋯ ao passar o mouse ou focar;
- capas com ▶, + e ⋯ sobre o cartão e os selos “Tocando” e “Indisponível”;
- menu da faixa elevado (Tocar agora · Adicionar à fila · Adicionar à playlist › · Localizar arquivo… · ─ · Remover da biblioteca…);
- linha de confirmação com “Ver fila”, estado vazio com a peça de estado vazio e uma única rolagem por página;
- “Tocar agora”: clique duplo, Enter, ▶ e o item do menu põem a faixa logo depois da atual e começam a tocá-la. “Adicionar à fila” continua sendo a ação separada que põe no fim.

Também fecha as pendências do plano 4 que são desta fatia: o `recentModel` que volta ao primeiro cartão a cada gravação, o foco perdido em `home-see-library` quando a biblioteca recarrega e o gesto dos cartões da estante “Da sua biblioteca”.

**Arquitetura:**
- **C++:**
  - um caso de uso novo no presenter, `PlayerPresenter::playSourcesNow`, sobre o `LocalFileQueue::insert` que o domínio já tem, no mesmo molde de `applySourceImportBatch`;
  - `PlayerPresenter::currentLocalSource`, para a biblioteca saber qual registro toca;
  - `LibraryListModel` passa a aplicar diferenças (remoções, inserções e `dataChanged`) em vez de `beginResetModel` a cada gravação e a cada letra do filtro;
  - `LibraryBinding` ganha `playNow`, `playingRecordId` e `statusProblem`, e os textos revisados do anexo.
- **Peças novas:** `AuroraTrackRow.qml` (a linha de faixa da peça 16) e `LibraryTrackMenu.qml` (o menu da faixa da Biblioteca). `HomeShelfCard.qml` vira `AuroraTrackCard.qml` (o cartão de faixa da peça 16), com as ações sobre a capa.
- **Página:** `LibraryPanel.qml` é reescrita sobre as peças Aurora. Ela só existe no Aurora (`openLibrary` exige `inlinePages`), então não há ramo do fixture a preservar nela.
- **Início:** os cartões da estante “Da sua biblioteca” passam a tocar agora.

**Stack:** C++20, Qt 6.11.2 Quick/QML com estilo Basic, Qt Test e Qt Quick Test, CMake/Ninja.

**Spec:** [2026-11-22-ui-redesign-design.md](../specs/2026-11-22-ui-redesign-design.md). Seções usadas:
- decisão 5 (“Biblioteca toca na hora”);
- “Peças” 3, 11 e 16 e “Rolagem única por página”;
- “Telas › Biblioteca” e “Janela mínima”;
- “Comportamento novo” 1 e 2 e a linha “Biblioteca” da tabela de menus;
- “Textos”, “Acessibilidade” e “Testes e validação”.

O anexo de textos é o [2026-11-22-ui-redesign-copy.md](../specs/2026-11-22-ui-redesign-copy.md), seções `LibraryText`, 4.1, `LibraryBinding.cpp`, R1 e o glossário. O rascunho `revisao-textos.md` não vale: o anexo é a versão aprovada. Os mockups aprovados são `out/product/ui-redesign-mockups-20261122/gerador/biblioteca.mjs`, `menus.mjs`, `compacto.mjs` (Biblioteca a 720×520) e o item 7 de `decisoes.md`.

## Restrições globais

As mesmas do [plano 2](2026-11-22-ui-redesign-casca.md), do [plano 3](2026-11-22-ui-redesign-gavetas.md) e do [plano 4](2026-11-22-ui-redesign-inicio.md) (“Restrições globais”), com estes ajustes:

- **Fixture M0 intacto:**
  - os ramos `!inlinePages`, `!editorial`, `!overlayMode` e `usesFrozenFixture` não mudam de aparência, ordem de Tab, ids nem textos. O Aurora é escolhido por `!presenter.usesFrozenFixture`;
  - passam **sem edição**: `tst_empty_shell`, `tst_high_contrast`, `tst_lifecycle`, `tst_lifecycle_transitions`, `tst_keyboard_shortcuts`, `tst_keyboard_exit`, `tst_aurora_shell`, `tst_lyrics_reader` e os casos com `testPlayerPresenter` de `tst_preferences` e `tst_accessibility_*`;
  - `playSourcesNow` recusa o fixture (`uses_frozen_fixture_`), como `appendLocalSourcesWithoutPlayback`;
  - se um deles precisar mudar, pare: é sinal de que o ramo congelado foi tocado.
- **Ids que existem nos dois modos** vivem em `Loader`s de `active` exclusivo. Nesta fatia nenhum id novo existe no fixture: confira com `rg` antes de criar `library-*`, `track-row-*` e `shelf-card-*`.
- **Texto visível** só em `*Text.qml`, com `qsTr`.
  - O scanner (`tests/ui/qtest/ShellUiContractTests.cpp`) recusa literais em qualquer outro lugar, comentários incluídos. Aceita só `objectName`, `iconName`, `controlId` e `trailingIconName`.
  - Nada de ternário com literais: escolhas de texto viram funções no catálogo (ex.: `LibraryText.countLine(visible, total, filtered)`).
  - `"transparent"` só direto em `color:` ou `border.color:`.
  - Duas lacunas no mesmo texto se preenchem numa passada só, com `NavigationText.fillTwo`, nunca `.arg(a).arg(b)`. Isso vale também para o aviso de “adicionado à playlist”, que hoje usa `.arg().arg()`.
  - Quando uma linha do anexo diz “troca” para uma chave que o fixture ainda lê, crie uma chave nova para o Aurora e mantenha a antiga. Hoje só `HomePage.qml` e `LibraryPanel.qml` leem `LibraryText`, e os dois são só do Aurora: as trocas são diretas. Confira com `rg` antes de editar.
- **Traduções:** o `.ts` (`src/ui/translations/music-player_pt_BR.ts`) é curado à mão e só guarda plurais. Esta fatia cria **um** plural, `LibraryText.filteredCount`, acrescentado à mão num `<context>` `LibraryText` novo, no formato dos vizinhos. Nunca rode o alvo `m0_qt_shell_ui_lupdate` nem `develop.ps1 format -Apply`. Formate só os arquivos tocados, com `qmlformat` ou `clang-format`.
- **Fontes:** todo `Text` do Aurora define família e tamanho com tokens do contrato (`headingFontFamily`/`bodyFontFamily`, `type*`). Nenhum `Text` fica com a fonte padrão.
- **Foco:**
  - `KeyNavigation` é recíproco; use relés de tamanho zero (modelo do `DownloadsPanel`) e não aponte de item oculto para item de fora;
  - nada focável fica sob gaveta, menu ou diálogo;
  - fatos do Qt 6.11.2: um popup é uma cerca de Tab; no `aboutToHide` o popup já soltou o foco, então devolva o foco no `closed`; um `Dialog` Basic com `title` desenha o próprio cabeçalho (use `header: null`); `CloseOnEscape` engole os atalhos da janela;
  - listas horizontais ou virtualizadas usam `cacheBuffer: 0`.
- **Testes Quick:**
  - rodam com `QT_FATAL_WARNINGS=1`;
  - sem esperas de tempo: use `tryVerify`, `tryCompare` e `waitForPolish`;
  - nenhum teste toca som real: os testes C++ usam o `PresenterPlaybackBackend` falso, e os Quick usam a biblioteca falsa (`catalog`) e o driver.
- **TDD:** cada mudança de comportamento começa por um teste que falha pelo motivo certo.
- **Arquivos QML novos** entram em `cmake/RcMusicUi.cmake`. O `git mv` de `HomeShelfCard.qml` troca o nome lá também.
- **Ícones:** nenhum novo. A fatia usa `play`, `plus`, `more`, `playlist`, `trash`, `folder-open`, `waveform`, `search`, `warning`, `check-circle`, `caret-right`, `library` e `queue`, que já estão em `assets/icons`. Se um ícone faltar, ele vem do Phosphor regular, de `phosphor-icons/core` no commit `2b75f3ad12b420c9504ef05df8d2564a28f8500e`, no formato de `assets/icons/external.svg`.
- **Sem dependência nova, sem telemetria e sem rede.**
- **Commits:** pequenos, atômicos, Conventional Commits no imperativo em português, terminando com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. A documentação muda no mesmo commit da mudança que a afeta.
- **Capturas:** confira cada uma a olho, nos dois tamanhos e nos dois temas, antes de dar a tarefa por pronta.

### Comandos

Os mesmos dos planos 2, 3 e 4 (`$b='out/product/ui-redesign-tests'`, ambiente `QT_QPA_PLATFORM=offscreen`, `QSG_RHI_BACKEND=software`, `QT_QUICK_CONTROLS_STYLE=Basic` e `QT_FATAL_WARNINGS=1`), mais estes:

| Para | Comando | Passou quando |
|---|---|---|
| Compilar os testes de UI | `cmake --build $b --target rc_music_ui_quick_tests rc_music_ui_contract_tests` | sem erro |
| Um arquivo Quick | `& "$b/tests/ui/rc_music_ui_quick_tests.exe" -input tests/ui/quick/<arquivo>.qml -import "$b/tests/ui/qml" -o -,txt` | `Totals: N passed, 0 failed` |
| Teste C++ do presenter | `ctest --test-dir $b -R '^rc_music\.ui\.presenter$' --output-on-failure --no-tests=error` | `100% tests passed` |
| Teste C++ da biblioteca | `ctest --test-dir $b -R '^rc_music\.library\.(binding\|artwork\|list-model)$' --output-on-failure --no-tests=error` | `100% tests passed` |
| Smokes de processo | `ctest --test-dir $b -R '^rc_music\.process\.(combined-startup\|error-recovery\|tray-lifecycle)$' --output-on-failure --no-tests=error` | `100% tests passed` |

Se os testes do `LibraryListModel` não tiverem um ctest próprio, eles entram em `LibraryBindingTests.cpp` e o filtro fica `(binding|artwork)`. Confira em `tests/qt/CMakeLists.txt` antes da tarefa 2.

A suíte Quick inteira (`rc_music.ui.quick`), o contrato, o `build-boundary`, os smokes de processo e o `all_qmllint` rodam uma vez, na tarefa 9.

## Foco da revisão

1. **Tocar agora com a faixa atual em Erro ou Carregando, ou sem faixa atual depois que a fila acabou.** A faixa nova entra logo depois da atual (ou no fim, sem atual), vira a atual e começa a tocar; a faixa com erro continua na fila. Nunca toca a faixa errada nem reinicia a fila. Testes: tarefa 1, `playSourcesNowInsertsAfterAFailedOrLoadingCurrentTrack` e `playSourcesNowWithoutACurrentTrackAppendsAndStarts`.
2. **Arquivo que sumiu depois que a biblioteca o leu.** O registro ainda parece disponível, mas a abertura falha. A ocorrência nova fica indisponível na fila, a chamada devolve falso e a Biblioteca mostra “Não foi possível tocar agora. Tente novamente.” em Erro; nada mais toca no lugar. Testes: tarefa 1, `playSourcesNowMarksARejectedFileUnavailable`; tarefa 5, `test_playNowFailureSaysSo`.
3. **Biblioteca grande enquanto se digita no filtro.** Uma só rolagem; a lista continua virtualizada; cada letra remove e insere linhas sem reset, e a posição e a linha atual não pulam quando continuam no resultado. Testes: tarefa 2, `filteringRemovesAndRestoresRowsWithoutAReset`; tarefa 5, `test_bigLibraryKeepsOneScrollerAndFewDelegates`.
4. **Remover a linha focada ou a que toca.** Depois de confirmar, o foco vai para a linha que ocupou o lugar (ou para a anterior, se era a última, ou para o filtro, se a lista esvaziou); a faixa que toca continua tocando, só a onda some. Testes: tarefa 6, `test_removingTheFocusedRowKeepsTheFocusInTheList` e `test_removingThePlayingRecordKeepsPlaying`.
5. **“Ver fila” fecha a página.** Abrir a Fila fecha a Biblioteca (`setQueueOpen`), então o abridor da gaveta é `player-queue`, não o botão que sumiu; ao fechar a gaveta, o foco volta a `player-queue`. Teste: tarefa 5, `test_seeQueueOpensTheDrawerFromThePlayer`.

## Decisões deste plano

- **Escopo:** a fatia 5 do spec. Ficam para os planos donos:
  - **plano 6 (menu da faixa em todo lugar):** o menu nas estantes da Início, na Fila e nas playlists. Nesta fatia, os cartões da estante “Da sua biblioteca” ganham só o gesto principal (clique e Enter tocam agora); botão direito, Menu, Shift+F10 e ⋯ neles são do plano 6;
  - **plano 7 (Buscar):** nada da página Buscar muda;
  - **plano 8 (playlist):** Tocar agora nas playlists reusa `playSourcesNow`, que já recebe uma lista de caminhos;
  - **“Nova playlist…” no submenu:** o anexo pede “Nenhuma playlist ainda” desabilitado e “Nova playlist…”. Criar playlist a partir da Biblioteca precisa do fluxo de nome da página Playlists, que ainda não é uma peça reutilizável. Esta fatia mostra só “Nenhuma playlist ainda”; “Nova playlist…” fica para o plano 6, junto com o menu comum.
- **Caso de uso (`PlayerPresenter::playSourcesNow`):**
  - chamada C++ confiável, não `Q_INVOKABLE`: só o `main.cpp` a liga à biblioteca;
  - as mesmas guardas de `appendLocalSourcesWithoutPlayback` (fixture, `exiting_`, `localSourcesBlocked()`, importação em curso, limite da fila) e só caminhos absolutos. Recusa devolve `false` e não muda nada;
  - com faixa atual (em qualquer estado, inclusive Erro e Carregando), insere em `currentIndex + 1`; sem faixa atual (fila vazia, ou seleção limpa por `StopClearSelection` no fim da fila), insere **no fim**. O spec diz só que a faixa “vira a atual”; no fim, ela não reordena nada do que já estava na fila;
  - carrega a primeira ocorrência inserida com `Autoplay`, no molde de `applySourceImportBatch`: `local_queue_.insert`, `local_queue_model_.insert(subspan, index)` (sem reset), `navigation_items_`, `loadLocalOccurrenceWithResult`;
  - abertura recusada (`InvalidArgument`): a ocorrência fica na fila marcada indisponível (`markUnavailable` + `open_rejected`), como na importação, e a chamada devolve `false`. Tirá-la da fila esconderia o problema; deixá-la indisponível é o que a fila já faz com arquivos ausentes;
  - Aleatório ligado: a faixa nova toca agora, e a ordem de navegação é recalculada pelo `navigationItems` de sempre;
  - sala: o convidado é recusado (`blocks_local_sources`), como em “Adicionar à fila”; o host segue `room_play_order_`, em que a ordem da fila é a ordem de tocar;
  - `notifyLocalAdmission` com os caminhos, como “Adicionar à fila”.
- **Faixa que toca na biblioteca:** `LibraryBinding.playingRecordId` é o registro cujo caminho é `presenter.currentLocalSource()`. O `main.cpp` o atualiza em `stateChanged` e `playbackChanged`, e o binding o recalcula a cada mudança confirmada do catálogo. Na linha, o título em destaque marca a faixa atual; a onda no lugar do número e o selo “Tocando” só aparecem enquanto ela toca (Playing), como o selo da Início.
- **Modelo da lista sem reset:** `setRecords` e `setFilter` comparam a lista de ids visíveis antiga com a nova. Se os ids em comum mantêm a ordem, removem as linhas que saíram (de trás para frente, em blocos contíguos), inserem as novas (em ordem crescente) e emitem `dataChanged` só para as linhas em comum que mudaram; `countChanged` só quando a contagem muda. Se a ordem dos comuns mudou, faz reset, como hoje. Isso resolve o `recentModel` que volta ao primeiro cartão e o pulo da lista ao digitar.
- **Rolagem única:** a página não rola. O cabeçalho, a barra e a linha de confirmação ficam fixos, e a lista (ou a grade) preenche o resto da altura e é a única coisa que rola, com `cacheBuffer: 0`. O `PageSurface` não muda: o `implicitHeight` do conteúdo é a soma das partes fixas mais uma linha, então `page-viewport` só rola se nem uma linha couber. Hoje a lista mede `Math.min(contentHeight, 640)` dentro da página que rola, o que viola a regra.
- **Teclado na lista:**
  - a lista (e a grade) é **uma** parada de Tab; as setas movem a linha atual, Home/End vão às pontas, PageUp/PageDown andam uma tela;
  - Enter toca agora; a tecla Menu e Shift+F10 abrem o menu da linha atual; clique duplo toca agora; botão direito abre o menu na linha clicada;
  - ▶, +, ⋯ e “Localizar” têm `activeFocusOnTab: false`: com até 20 000 linhas, um Tab por botão tornaria a página intransitável. Toda ação deles também está no menu, e o leitor de tela lê a linha com nome e dica;
  - a linha atual da lista com foco mostra o contorno de foco interno de 3 px em `accentColor` e as ações, como no hover.
- **Ordem de Tab do Aurora:** lateral → topo (`theme-toggle`) → `library-filter` → `library-view` (opção marcada) → `library-see-queue` (quando a linha de confirmação a mostra) → `library-status-notice-primary` (quando há erro com nova tentativa) → `library-list` ou `library-grid` → player. No estado vazio, `library-empty-primary` toma o lugar da barra e da lista.
- **Menu e confirmação:** `LibraryTrackMenu` é um `AuroraMenu` de 248 px com:
  - `library-menu-play` (Tocar agora, ícone `play`, atalho “Enter”), desabilitado no registro indisponível, com o motivo na descrição;
  - `library-menu-enqueue` (Adicionar à fila, `plus`);
  - o submenu `library-menu-playlists` (Adicionar à playlist, `playlist`, com ›), com um `AuroraMenuItem` `library-playlist-choice-<id>` por playlist, criado por `Instantiator` (`insertItem`/`removeItem`), ou `library-playlist-none` desabilitado;
  - `library-menu-locate` (Localizar arquivo…, `folder-open`), só no registro indisponível e com `canLocate`. Oculto, tem `visible: false`, `enabled: false` e altura 0;
  - separador e `library-menu-remove` (Remover da biblioteca…, `trash`, `danger`).

  “Remover” abre `AuroraConfirmDialog` `library-remove-confirmation` (destrutivo), com a pergunta “Remover “<título>” da biblioteca?”, a consequência `removeAdvice` e o botão “Remover da biblioteca”. Ao fechar o menu ou o diálogo, o foco volta à linha de onde saiu (ou ao alvo do Foco 4, depois de remover). `blockingUiOpen` inclui o menu, o submenu e o diálogo.
- **Linha de confirmação e erros:**
  - `library-feedback` (`AuroraFeedback`) mostra a última mensagem: confirmação de ação (adicionado à fila ou à playlist) em Sucesso, falha de ação em Erro e o `statusText` do binding (Sucesso, ou Erro quando `statusProblem`);
  - `library-see-queue` (“Ver fila”, `AuroraButton` Quiet compacto) aparece só depois de “Adicionar à fila” bem-sucedido. Tocar agora bem-sucedido não mostra mensagem: o player e a onda já dizem o que aconteceu;
  - falhas que pedem nova tentativa (`canRetrySave`, ou carga falhada) usam `library-status-notice` (`AuroraNotice` Erro) com a ação “Tentar salvar novamente” ou “Tentar carregar novamente”;
  - enquanto carrega, `library-status` mostra o texto de carregamento de hoje.
- **“Ver fila”:** chama `window.openQueueFrom(transport.queueControl)`, função nova de `Main` que foca o abridor e só então abre a gaveta. O `onClosed` da Biblioteca não move o foco quando `window.queueOpen`.
- **Contagem:** sem filtro, “N faixas” (`NavigationText.trackCount`, como a lateral e a Início); com filtro, “V de N faixas” (`LibraryText.filteredCount`, plural novo). A escolha fica numa função do catálogo, `LibraryText.countLine(visible, total, filtered)`, que chama `NavigationText.trackCount` por uma instância própria.
- **Janela compacta (`window.width <= uiContract.compactWidth`):** o cabeçalho mostra só o título (`PageHeader.titleOnly`), o cabeçalho das colunas some, a coluna Álbum some, as linhas medem 52 com capa de 36, como em `compacto.mjs`. Na larga, eyebrow `libraryEyebrow`, título `library` e descrição `libraryDescription`, como hoje.
- **Linha (`AuroraTrackRow`), medidas de `biblioteca.mjs`:**
  - colunas 44 | 1,4fr | 1fr | 56 | 96, espaço 12, padding 10, altura 56, capa 40 com raio 6;
  - número em `textSecondaryColor`; na faixa que toca, `track-row-wave` (`waveform` em `accentColor`) no lugar dele, e título em `accentColor`;
  - no hover ou na linha atual com foco, o número vira `track-row-play` (círculo `accentColor` de 32 com `play`), e à direita aparecem `track-row-enqueue` (+, botão de ícone 36) e `track-row-more` (⋯, tonal); fundo `hoverColor`;
  - nos fundos de hover e de destaque, os textos secundários passam a `textPrimaryColor`, para manter 4,5:1 (o mockup usa secundário sobre hover, que não passa no tema escuro);
  - indisponível: subtítulo “· Indisponível” em `warningColor`, título em `textSecondaryColor` (sem opacidade nos textos, que derrubaria o contraste), ▶ desabilitado e `track-row-locate` (“Localizar”, `AuroraButton` Quiet compacto com `folder-open`) sempre visível no lugar do +.
- **Cartão (`AuroraTrackCard`, ex-`HomeShelfCard`):**
  - mantém os ids `shelf-card-*` de hoje, para os testes da Início não mudarem de força;
  - com `actionsShown` (hover ou foco), mostra `shelf-card-play` (círculo `accentColor` de 40, a 8 da direita e de baixo), `shelf-card-enqueue` (elevado de 40, a 54 da direita) e `shelf-card-more` (elevado de 32, no canto de cima);
  - selos `shelf-card-playing` (“Tocando”, agora com `waveform`) e `shelf-card-unavailable` (“Indisponível”, Aviso);
  - na grade da Biblioteca, um clique seleciona e o clique duplo toca agora (`activateOnClick: false`); na estante da Início, um clique toca agora (`activateOnClick: true`), como a estante da fila já faz.
- **Estante “Da sua biblioteca”:**
  - os cartões passam a interativos: clique e Enter chamam `library.playNow(recordId)`; o Tab os percorre como os da fila (`cardsFocusable: root.openingEnabled`), pulando os indisponíveis;
  - nome acessível “Tocar <título> agora”; descrição “Tocando” na faixa que toca e o motivo no indisponível;
  - com `openingEnabled` falso (inclusive convidado de sala), os cartões ficam desabilitados, como os da fila;
  - uma falha de Tocar agora na Início não ganha linha própria: o destaque já mostra o erro da faixa;
  - o foco: com o modelo sem reset, um cartão focado continua focado quando o catálogo grava. Se a estante sumir (biblioteca recarregando ou vazia) com o foco nela, o foco vai para `open-audio`.
- **Peças tocadas:** `AuroraIconButton` ganha `accent`, `raised`, `round` e `drawnSize` (a área de toque continua 40); `AuroraMenu` ganha `iconName`, que o delegado passa ao item de submenu; `PageHeader` ganha `titleOnly`.

### Textos novos e trocados

Exatos. Um plural novo (`filteredCount`).

| Catálogo | Chave | Texto |
|---|---|---|
| `LibraryText` | `filterPlaceholder` (troca) | `Filtrar a biblioteca…` |
| `LibraryText` | `countText` (troca de forma: vira a função `filteredCount`) | — |
| `LibraryText` | `filteredCount(visible, total)` (plural) | `%1 de %n faixas` (1: “1 de 1 faixa”; `.ts`: fonte `%1 de %n faixa`, formas `%1 de %n faixa` / `%1 de %n faixas`, como os plurais vizinhos) |
| `LibraryText` | `timeHeader` (troca) | `DURAÇÃO` |
| `LibraryText` | `removeAdvice` (troca) | `A faixa sai só da biblioteca. O arquivo, a fila, as playlists e o que está tocando continuam como estão.` |
| `LibraryText` | `emptyAdvice` (troca) | `Abra arquivos do computador ou baixe faixas em Buscar. Tudo fica guardado aqui, mesmo quando a fila muda.` |
| `LibraryText` | `enqueueFailed` (troca, R1) | `Não foi possível adicionar à fila agora. Tente novamente.` |
| `LibraryText` | `addToPlaylistFailed` (troca, R1) | `Não foi possível adicionar à playlist agora. Confira a página Playlists e tente novamente.` |
| `LibraryText` | `retryLoad` (troca, R1) | `Tentar carregar novamente` |
| `LibraryText` | `retrySave` (troca, R1) | `Tentar salvar novamente` |
| `LibraryText` | `playNow` (novo) | `Tocar agora` |
| `LibraryText` | `playNowName` (novo) | `Tocar %1 agora` |
| `LibraryText` | `playNowDescription` (novo) | `Entra logo depois da faixa atual e começa a tocar.` |
| `LibraryText` | `playNowShortcut` (novo) | `Enter` |
| `LibraryText` | `playNowFailed` (novo) | `Não foi possível tocar agora. Tente novamente.` |
| `LibraryText` | `rowHint` (novo) | `Clique duas vezes para tocar.` |
| `LibraryText` | `enqueueAction` (novo) | `Adicionar à fila` |
| `LibraryText` | `enqueueName` (novo) | `Adicionar %1 à fila` |
| `LibraryText` | `enqueueDescription` (novo) | `Entra no fim da fila.` |
| `LibraryText` | `addToPlaylistAction` (novo) | `Adicionar à playlist` |
| `LibraryText` | `addToPlaylistName` (novo) | `Adicionar %1 a uma playlist` |
| `LibraryText` | `noPlaylists` (novo) | `Nenhuma playlist ainda` |
| `LibraryText` | `locateAction` (novo) | `Localizar arquivo…` |
| `LibraryText` | `removeAction` (novo) | `Remover da biblioteca…` |
| `LibraryText` | `removeName` (novo) | `Remover %1 da biblioteca` |
| `LibraryText` | `removeQuestion` (novo) | `Remover “%1” da biblioteca?` |
| `LibraryText` | `removeConfirmAction` (novo) | `Remover da biblioteca` |
| `LibraryText` | `unavailableReason` (novo) | `Arquivo não encontrado. Localize o arquivo para tocar a faixa.` |
| `LibraryText` | `countLine(visible, total, filtered)` (nova função) | escolhe entre `NavigationText.trackCount(total)` e `filteredCount` |
| `LibraryText` | `cancel`, `removeTitle`, `removeConfirm` (saem, se o `rg` não achar outro leitor) | — |
| `ControlsText` | `moreActionsFor` (novo) | `Mais ações para %1` |
| `LibraryBinding.cpp` | :183 | `Não foi possível adicionar à biblioteca. Ela chegou ao limite ou o caminho do arquivo é inválido.` |
| `LibraryBinding.cpp` | :283 | `Arquivo localizado. A faixa continua no mesmo lugar da biblioteca.` |
| `LibraryBinding.cpp` | :295 | `Não foi possível abrir a janela de arquivos. Tente novamente.` |
| `LibraryBinding.cpp` | :306 | `A biblioteca foi salva por uma versão mais nova do RC Music. Nada foi alterado.` |
| `LibraryBinding.cpp` | :308 | `Não foi possível carregar a biblioteca. Nada foi alterado. Tente novamente.` |
| `LibraryBinding.cpp` | :316 | `Biblioteca recuperada da cópia de segurança.` |
| `LibraryBinding.cpp` | :335 | `A biblioteca foi salva por uma versão mais nova do RC Music. Ela não foi alterada, e as faixas novas não foram salvas.` |

Os textos reusados sem mudança são estes:
- `contract.openAudioText`;
- `NavigationText.library`, `libraryEyebrow`, `libraryDescription`, `trackCount`, `fillTwo`, `titleAndArtist`, `playingBadge`, `seeQueue` e `localAudio`;
- `ControlsText.moreActions`;
- `LibraryText.emptyTitle`, `noMatches`, `numberHeader`, `trackHeader`, `albumHeader`, `unknownArtist`, `unavailable`, `viewChoice`, `coversView`, `listView`, `filterName`, `enqueued`, `addedToPlaylist`.

Confira cada texto do anexo contra a tabela antes do commit da tarefa 5; o anexo prevalece.

---

### Task 1: caso de uso “Tocar agora” no presenter

**Arquivos:**
- Modificar: `src/presentation/PlayerPresenter.hpp`/`.cpp`
- Testes: `tests/ui/qtest/PlayerPresenterTests.cpp`

**Interfaces:**
- Produz:
  - `[[nodiscard]] bool PlayerPresenter::playSourcesNow(const std::vector<std::filesystem::path>& source_paths)`;
  - `[[nodiscard]] std::optional<std::filesystem::path> PlayerPresenter::currentLocalSource() const`, vazio no fixture, na reprodução de sala do convidado e sem ocorrência atual.

- [ ] **Passo 1: testes que falham** em `PlayerPresenterTests.cpp`, com o `PresenterPlaybackBackend` falso (`loaded_paths`, `play_calls`, `load_results`):
  - `playSourcesNowInsertsAfterTheCurrentTrackAndStartsIt`: com `a`, `b`, `c` e `a` tocando, `playSourcesNow({x})` devolve `true`; a fila fica `a, x, b, c`; `loaded_paths.back()` é `x`, há um `play` a mais e `currentOccurrenceId` é o de `x`. O `queueModel` emite um `rowsInserted(1, 1)` e nenhum `modelReset`;
  - `playSourcesNowWithoutACurrentTrackAppendsAndStarts`: com a fila vazia, `x` vira a única faixa e toca; com `a, b` e a seleção limpa pelo fim da fila (`StopClearSelection`), a fila fica `a, b, x` e `x` toca;
  - `playSourcesNowInsertsAfterAFailedOrLoadingCurrentTrack`: com `a` em Erro (`load_results` recusando `a`), `x` entra depois de `a` e toca, e `a` continua na fila; o mesmo com `a` ainda em Loading;
  - `playSourcesNowWithShuffleStartsTheInsertedTrack`: com Aleatório ligado, `x` é a atual e toca;
  - `playSourcesNowMarksARejectedFileUnavailable`: com a abertura de `x` recusada (`InvalidArgument`), a chamada devolve `false`, a ocorrência de `x` fica indisponível (`available` falso na linha) e nenhuma outra faixa começa a tocar;
  - `playSourcesNowRefusesInvalidInputAndGuestRooms`: lista vazia, caminho relativo, fixture congelado, presenter saindo e `setRoomSessionGate({.blocks_local_sources = true})` devolvem `false` e não mudam a fila, os `loaded_paths` nem os `play_calls`;
  - `playSourcesNowFollowsTheRoomPlayOrderForTheHost`: com a montagem de host dos testes de sala (~10245), `x` entra logo depois da atual na fila e é a próxima ocorrência carregada;
  - `playSourcesNowAdmitsTheFilesToTheLibrary`: o observador de `setLocalAdmissionObserver` recebe `{x}`;
  - `currentLocalSourceFollowsTheCurrentOccurrence`: vazio sem fila; o caminho de `a` com `a` atual; o de `x` depois de `playSourcesNow({x})`; vazio no fixture.
- [ ] **Passo 2: ver falhar** (as funções não existem).
- [ ] **Passo 3: implementar** conforme as “Decisões”. Extraia de `applySourceImportBatch` o que for comum (inserir, atualizar a navegação, carregar com resultado, marcar recusa) numa função privada, sem mudar o comportamento da importação.
- [ ] **Passo 4: ver passar** `rc_music.ui.presenter` inteiro (a importação e “Adicionar à fila” continuam verdes).
- [ ] **Passo 5: commit** `feat: toca uma faixa logo depois da atual pelo presenter`

### Task 2: modelo da biblioteca sem reset

**Arquivos:**
- Modificar: `src/presentation/LibraryListModel.hpp`/`.cpp`
- Testes: `tests/qt/LibraryBindingTests.cpp` (ou um `LibraryListModelTests.cpp` novo, registrado em `tests/qt/CMakeLists.txt` como `rc_music.library.list-model`, se o modelo puder ser montado sozinho)

**Interfaces:**
- Produz: o mesmo contrato público de `LibraryListModel` (`setRecords`, `setFilter`, papéis), agora com sinais de linha em vez de reset quando a ordem dos comuns se mantém.

- [ ] **Passo 1: testes que falham**, com `QSignalSpy` em `modelAboutToBeReset`, `rowsRemoved`, `rowsInserted` e `dataChanged`:
  - `unchangedRecordsEmitNothing`: `setRecords` com os mesmos registros não emite nada;
  - `aChangedTagUpdatesOnlyItsRow`: trocar o título do registro da linha 2 emite um `dataChanged(2, 2)` e nenhum reset;
  - `aNewestRecordEntersTheRecentListWithoutAReset`: com 12 recentes, um registro mais novo emite `rowsRemoved(11, 11)` e `rowsInserted(0, 0)`; um `QPersistentModelIndex` da linha 5 passa a apontar para a linha 6, com o mesmo `recordId`;
  - `filteringRemovesAndRestoresRowsWithoutAReset`: com 1 000 registros, `setFilter("mar")` só remove linhas, e voltar a `""` só insere; um índice persistente de uma linha que casa com o filtro sobrevive às duas mudanças;
  - `aReorderFallsBackToAReset`: se a ordem dos ids em comum muda, há um reset e o conteúdo final está certo.

  Em `LibraryBindingTests.cpp`:
  - `savingTheSameCatalogDoesNotResetTheModels`: depois do Saved, uma nova gravação sem mudança não reseta `model` nem `recentModel`;
  - `recentModelKeepsTheFirstCardOnAReload` (pendência do plano 4): um `retryLoad` que devolve o mesmo catálogo não emite reset no `recentModel`.
- [ ] **Passo 2: ver falhar** (hoje cada chamada reseta).
- [ ] **Passo 3: implementar** a diferença conforme as “Decisões”. A busca de ids usa um `QHash<QString, int>` da lista antiga, então o custo é linear; nada de comparação quadrática com 20 000 registros.
- [ ] **Passo 4: ver passar** `rc_music.library.binding`, `rc_music.library.store` e o ctest novo, se houver.
- [ ] **Passo 5: commit** `fix: atualiza a lista da biblioteca sem reset a cada gravação e filtro`

### Task 3: Tocar agora, faixa que toca e textos no binding da biblioteca

**Arquivos:**
- Modificar: `src/presentation/LibraryBinding.hpp`/`.cpp`, `src/app/main.cpp` (linhas ~849-905)
- Testes: `tests/qt/LibraryBindingTests.cpp`

**Interfaces:**
- Consome: `PlayerPresenter::playSourcesNow` e `currentLocalSource` (tarefa 1).
- Produz:
  - `using LibraryBinding::QueuePlayer = std::function<bool(std::vector<std::filesystem::path>)>` e `void setQueuePlayer(QueuePlayer)`;
  - `Q_INVOKABLE bool LibraryBinding::playNow(const QString& record_id)`;
  - `Q_PROPERTY(QString playingRecordId READ playingRecordId NOTIFY playingRecordIdChanged)` e `void setNowPlaying(std::optional<std::filesystem::path> path)`;
  - `Q_PROPERTY(bool statusProblem READ statusProblem NOTIFY changed)`.

- [ ] **Passo 1: testes que falham** em `LibraryBindingTests.cpp`, com o `Harness` ganhando `played` ao lado de `enqueued`:
  - `playingNowSendsTheFileToThePlayer`: `playNow(id)` devolve `true` e `played` tem o caminho do registro; `enqueued` continua vazio;
  - `playNowRefusesUnknownUnavailableOrUnwiredRecords`: id desconhecido, registro indisponível, binding sem player e player que devolve `false` dão `false` (o último sem mudar `statusText`; a mensagem de falha é da página);
  - `nowPlayingNamesTheRecordOfTheCurrentFile`: `setNowPlaying(caminho)` põe `playingRecordId` no id do registro e emite o sinal uma vez; um caminho fora da biblioteca ou vazio deixa `""`; remover o registro que toca volta a `""`;
  - `statusProblemMarksOnlyFailures`: verdadeiro depois de uma carga falhada e de uma gravação falhada; falso no pronto, no localizado e na recuperação da cópia de segurança;
  - troque, com a mesma força, os pinos dos sete textos de `LibraryBinding.cpp` para os do anexo, e acrescente um pino para “Biblioteca recuperada da cópia de segurança.”.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar.**
  - `playNow` usa as mesmas condições de `enqueue`, mais `available`, e chama o `QueuePlayer` com o caminho do registro.
  - `playingRecordId` é recalculado em `setNowPlaying` e em `Impl::finished()`.
  - Troque os sete textos.
  - No `main.cpp`, `setQueuePlayer` chama `presenter.playSourcesNow`, e `stateChanged`/`playbackChanged` do presenter chamam `setNowPlaying(presenter.currentLocalSource())`. Essa ligação só é coberta pelos smokes de processo da tarefa 9; registre isso no relato.
- [ ] **Passo 4: ver passar** `rc_music.library.binding`, `rc_music.library.store` e `rc_music.ui.presenter`.
- [ ] **Passo 5: commit** `feat: toca agora a partir da biblioteca e marca a faixa que toca`

### Task 4: peças de faixa (linha, cartão e menu com ícone)

**Arquivos:**
- Criar: `src/ui/qml/AuroraTrackRow.qml` (e acrescentar em `cmake/RcMusicUi.cmake`)
- Mover: `src/ui/qml/HomeShelfCard.qml` → `src/ui/qml/AuroraTrackCard.qml` (`git mv`, e trocar em `cmake/RcMusicUi.cmake` e em `HomePage.qml`)
- Modificar: `src/ui/qml/AuroraIconButton.qml`, `src/ui/qml/AuroraMenu.qml`, `src/ui/qml/PageHeader.qml`, `src/ui/qml/ControlsText.qml`
- Testes: `tests/ui/quick/tst_aurora_tracks.qml` (novo), `tst_aurora_menus.qml`, `tst_aurora_composition.qml` (só os que olham `shelf-card-*`)

**Interfaces:**
- Produz:
  - `AuroraIconButton`: `accent: bool`, `raised: bool`, `round: bool`, `drawnSize: int` (padrão 40);
  - `AuroraTrackRow` (`ItemDelegate` com `focusPolicy: Qt.NoFocus`): propriedades `contract`, `number: int`, `title`, `subtitle`, `album`, `durationText`, `artworkSource: url`, `available`, `current`, `playing`, `compact`, `highlighted`, `locateEnabled`, `accessibleName`, `description`; sinais `activated()`, `playRequested()`, `enqueueRequested()`, `moreRequested(Item opener)`, `locateRequested()`, `contextMenuRequested(Item opener)`; ids filhos `track-row-number`, `track-row-wave`, `track-row-play`, `track-row-artwork`, `track-row-title`, `track-row-subtitle`, `track-row-unavailable`, `track-row-album`, `track-row-duration`, `track-row-enqueue`, `track-row-more`, `track-row-locate`;
  - `AuroraTrackCard`: as propriedades do `HomeShelfCard`, mais `actionsShown`, `unavailableBadge`, `highlighted`, `activateOnClick` (padrão `true`); sinais `selected()`, `playRequested()`, `enqueueRequested()`, `moreRequested(Item opener)`, além de `activated()`; ids novos `shelf-card-play`, `shelf-card-enqueue`, `shelf-card-more`, `shelf-card-unavailable`;
  - `AuroraMenu.iconName: string`, passado ao `AuroraMenuItem` do submenu;
  - `PageHeader.titleOnly: bool`;
  - `ControlsText.moreActionsFor`.

- [ ] **Passo 1: testes que falham** em `tst_aurora_tracks.qml` (peças soltas, sem `Main`, como em `tst_aurora_menus`):
  - `test_rowMatchesTheMockup`: a 900 de largura, a linha mede 56, a capa 40, e as colunas começam onde `44 | 1,4fr | 1fr | 56 | 96` com espaço 12 e padding 10 mandam (tolerância 0,5); em `compact`, 52 de altura, capa 36 e `track-row-album` oculto;
  - `test_rowShowsActionsOnHoverOrHighlight`: sem hover, `track-row-play`, `track-row-enqueue` e `track-row-more` estão ocultos e `track-row-number` visível; com `mouseMove` sobre a linha ou `highlighted`, aparecem, e os textos secundários usam `textPrimaryColor`;
  - `test_playingRowShowsTheWave`: com `current` e `playing`, `track-row-wave` substitui o número e o título usa `accentColor`; com `current` e sem `playing`, o número volta e o título continua em `accentColor`;
  - `test_unavailableRowOffersLocate`: com `available` falso, `track-row-unavailable` diz “Indisponível” em `warningColor`, `track-row-play` fica desabilitado, `track-row-locate` aparece e `track-row-enqueue` some; nenhum texto tem `opacity` menor que 1;
  - `test_rowSignals`: clique duplo emite `activated`; clique em ▶, + e ⋯ emite `playRequested`, `enqueueRequested` e `moreRequested(track-row-more)`; botão direito emite `contextMenuRequested`; os botões têm `activeFocusOnTab` falso;
  - `test_cardOverlays`: com `actionsShown`, `shelf-card-play` (círculo de 40 a 8 da direita e de baixo da capa), `shelf-card-enqueue` (a 54 da direita) e `shelf-card-more` (32, canto de cima) aparecem; `unavailableBadge` mostra `shelf-card-unavailable` em Aviso; com `activateOnClick` falso, um clique emite `selected` e o duplo `activated`;
  - `test_iconButtonVariants`: `accent` pinta o fundo de `accentColor` com ícone em `accentTextColor`; `drawnSize: 32` desenha 32 e mantém a área de toque de 40.

  Em `tst_aurora_menus.qml`, `test_submenuItemShowsItsIcon`: um submenu com `iconName: "playlist"` mostra o ícone e o caret, e o texto alinha com o dos itens irmãos (corrige o desalinhamento M9 sem ícone).
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** conforme as “Decisões”. O `git mv` vem num commit só de mudança de nome (`refactor: renomeia HomeShelfCard para AuroraTrackCard`), com `tst_aurora_composition` verde antes e depois.
- [ ] **Passo 4: ver passar** `tst_aurora_tracks`, `tst_aurora_menus`, `tst_aurora_composition`, `tst_aurora_navigation`, `tst_aurora_pieces_visual` e `tst_empty_shell` (sem edição).
- [ ] **Passo 5: commit** `feat: cria a linha e o cartão de faixa com as ações da Biblioteca`

### Task 5: página da Biblioteca

**Arquivos:**
- Criar: `src/ui/qml/LibraryTrackMenu.qml` (e acrescentar em `cmake/RcMusicUi.cmake`)
- Modificar: `src/ui/qml/LibraryPanel.qml` (reescrita), `src/ui/qml/LibraryText.qml`, `src/ui/qml/Main.qml` (instância ~l.654, `openQueueFrom`), `src/ui/translations/music-player_pt_BR.ts`
- Testes: `tests/ui/quick/tst_library.qml`, `tst_lyrics.qml` (~l.975), `tst_aurora_navigation.qml`

**Interfaces:**
- Consome: `LibraryBinding.playNow`, `playingRecordId`, `statusProblem` (tarefa 3); `AuroraTrackRow`, `AuroraTrackCard`, `AuroraMenu.iconName`, `PageHeader.titleOnly` (tarefa 4).
- Produz:
  - `LibraryPanel`: propriedades `compact: bool`, `playbackPlaying: bool`, `openingEnabled: bool`; sinais `queueRequested(Item opener)` e `openAudioRequested(Item opener)`;
  - `LibraryTrackMenu`: propriedades `contract`, `recordId`, `recordTitle`, `available`, `canLocate`, `playlists: var`; sinais `playRequested()`, `enqueueRequested()`, `playlistChosen(string id, string name)`, `locateRequested()`, `removeRequested()`;
  - `Main.openQueueFrom(opener: Item)`;
  - ids `library-filter`, `library-count`, `library-view` (`library-view-0` Lista, `library-view-1` Capas), `library-feedback`, `library-see-queue`, `library-status`, `library-status-notice`, `library-empty`, `library-no-matches`, `library-columns`, `library-list` com `library-row-<id>`, `library-grid` com `library-card-<id>`, `library-track-menu` e `library-remove-confirmation`.

- [ ] **Passo 1: testes que falham** em `tst_library.qml`. O `catalog` falso ganha `playNow(id)` (que registra em `played` e devolve `playResult`), `playingRecordId` e `statusProblem`, e passa a ter 3 registros (um tocando):
  - `test_theLibraryMatchesTheMockup`, a 1440×900: `library-filter` é um `AuroraTextField` com ícone `search` e o texto de ajuda “Filtrar a biblioteca…”; `library-count` diz “3 faixas”, e com `filterText` “mar” e `visibleCount` 1, “1 de 3 faixas”; `library-view-0` (“Lista”) está marcado; `library-columns` mostra “Nº”, “FAIXA / ARTISTA”, “ÁLBUM” e “DURAÇÃO”; as linhas medem 56;
  - `test_compactLibraryShowsTitleAndListOnly`, a 720×520: `page-eyebrow` e `page-description` ocultos, `library-columns` oculto, `track-row-album` oculto, linhas de 52 e pelo menos cinco linhas inteiras visíveis;
  - `test_bigLibraryKeepsOneScrollerAndFewDelegates`: com 5 000 registros no `ListModel`, `page-viewport` não rola (`contentHeight <= height`), `library-list` rola até o fim e o número de delegados criados fica abaixo de 40;
  - `test_doubleClickAndPlayButtonPlayNow`: clique duplo na linha `3` e clique em `track-row-play` da linha `5` registram `["3", "5"]` em `played`; nenhum `enqueued`;
  - `test_playNowFailureSaysSo`: com `playResult` falso, `library-feedback` mostra “Não foi possível tocar agora. Tente novamente.” com o ícone de Erro;
  - `test_enqueueConfirmsAndOffersSeeQueue`: `track-row-enqueue` registra em `enqueued`, `library-feedback` cita o título em Sucesso e `library-see-queue` aparece;
  - `test_seeQueueOpensTheDrawerFromThePlayer`: “Ver fila” fecha a Biblioteca e abre `queue-panel`; Esc fecha a gaveta e o foco fica em `player-queue`;
  - `test_playingRecordShowsTheWave`: com `playingRecordId` “3” e o driver em Playing (`publishLocalPlaying`), a linha `3` mostra `track-row-wave`; em pausa (`publishLocalPaused`), mostra o número;
  - `test_menuOffersTheLibraryActions`: ⋯ da linha `3` abre `library-track-menu` com, nessa ordem, “Tocar agora” (atalho “Enter”), “Adicionar à fila”, “Adicionar à playlist” (com ›) e, depois do separador, “Remover da biblioteca…” em Erro; `library-menu-locate` oculto; na linha indisponível, `library-menu-play` desabilitado com o motivo na descrição e `library-menu-locate` visível;
  - `test_menuAddsToAPlaylistOrSaysThereIsNone`: o submenu tem `library-playlist-choice-7`; escolher registra `3:7` e o aviso cita “Favoritas 日本” e o título, preenchidos numa passada (um título com `%2` não é trocado); sem playlists, o submenu mostra `library-playlist-none` desabilitado;
  - `test_coversViewShowsOverlaysAndBadges`: em Capas, `library-card-3` mostra `shelf-card-playing` (“Tocando”) enquanto toca e o cartão indisponível mostra `shelf-card-unavailable`; com o mouse sobre um cartão, ▶, + e ⋯ aparecem e funcionam (`played`, `enqueued`, menu);
  - `test_emptyLibraryUsesTheEmptyStatePiece`: com `count` 0, `library-empty` mostra o título de hoje, a dica `emptyAdvice` e `library-empty-primary` (“Abrir áudio”), que emite `openAudioRequested`; a barra fica oculta.

  Troque, com a mesma força, os testes de hoje:
  - `library-view-list`/`library-view-covers` → `library-view-0`/`library-view-1`;
  - `library-enqueue`, `library-locate` → `track-row-enqueue`, `track-row-locate`; `library-card-enqueue`, `library-card-art` → `shelf-card-enqueue`, `shelf-card-artwork`;
  - `library-remove` e `library-add-to-playlist` → os itens do menu; `library-remove-cancel`/`-confirm` → `library-remove-confirmation-cancel`/`-confirm`;
  - `library-notice` e o “Arquivo localizado.” de `library-status` → `library-feedback`;
  - `library-no-matches` lido por `empty-title`;
  - “2 de 2 faixas” → “3 faixas”;
  - em `tst_lyrics.qml` (~l.975), o clique em `library-filter` mira o campo interno do `AuroraTextField`.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** conforme as “Decisões”.
  - Reescreva o `LibraryPanel` sobre `PageHeader`, `AuroraTextField`, `AuroraSegmented`, `AuroraFeedback`, `AuroraNotice`, `AuroraEmptyState`, `AuroraTrackRow`, `AuroraTrackCard`, `LibraryTrackMenu` e `AuroraConfirmDialog` (`header: null`).
  - A lista e a grade usam `cacheBuffer: 0`, `boundsBehavior: Flickable.StopAtBounds` e `Layout.fillHeight`.
  - Em `Main`, passe `compact`, `playbackPlaying`, `openingEnabled`, ligue `queueRequested` a `openQueueFrom` e `openAudioRequested` a `openAudio`.
  - Troque e crie os textos da tabela; acrescente o plural ao `.ts` à mão.
- [ ] **Passo 4: ver passar** `tst_library`, `tst_lyrics`, `tst_aurora_navigation` (`test_tabFollowsSidebarHeaderPageAndPlayerOnEveryPage` com `library`), `tst_aurora_composition`, `tst_search` (as páginas fecham juntas), `tst_listening_room` e `tst_empty_shell` (sem edição).
- [ ] **Passo 5: commit** `feat: dá à Biblioteca o desenho Aurora com Tocar agora`

### Task 6: teclado, menu e foco na Biblioteca

**Arquivos:**
- Modificar: `src/ui/qml/LibraryPanel.qml`, `src/ui/qml/LibraryTrackMenu.qml`, `docs/user-guide.md` (seção “Abrir música e organizar a fila”)
- Testes: `tests/ui/quick/tst_library.qml`, `tst_aurora_navigation.qml`

**Interfaces:**
- Consome: a página da tarefa 5.
- Produz: nenhuma interface nova; o contrato de teclado das “Decisões”.

- [ ] **Passo 1: testes que falham** em `tst_library.qml`:
  - `test_tabOrderThroughTheLibrary`: de `theme-toggle`, o Tab passa por `library-filter`, `library-view-0`, `library-list` (a linha atual com o contorno de foco) e sai para o player; o Shift+Tab faz o caminho de volta; com a confirmação visível, `library-see-queue` entra entre a troca de vista e a lista;
  - `test_arrowsMoveTheCurrentRowAndEnterPlaysIt`: na lista, Down duas vezes e Enter registram a terceira linha em `played`; End vai à última e a mostra inteira; Home volta à primeira;
  - `test_menuKeysOpenTheRowMenu`: a tecla Menu e Shift+F10 abrem `library-track-menu` na linha atual com o foco no primeiro item; Esc fecha e o foco volta à linha; o atalho Esc não fecha a página junto;
  - `test_rightClickOpensTheMenuOnTheClickedRow`: botão direito na linha `5` abre o menu dela e torna-a a atual;
  - `test_unavailableRowEnterDoesNothingButSaysWhy`: Enter na linha indisponível não registra nada; o `Accessible.description` da linha é o motivo;
  - `test_removingTheFocusedRowKeepsTheFocusInTheList`: remover pelo menu a linha atual (meio da lista) deixa o foco na lista com a linha seguinte atual; remover a última leva à anterior; remover a única leva o foco a `library-filter`;
  - `test_removingThePlayingRecordKeepsPlaying`: remover o registro que toca não muda `presenter.playbackState` nem a fila;
  - `test_gridKeyboard`: em Capas, as setas andam na grade em duas dimensões, Enter toca e a tecla Menu abre o menu do cartão atual;
  - `test_filterKeepsTheCurrentRowWhenItStillMatches`: com a linha `5` atual, digitar um filtro que ainda a inclui mantém `library-list.currentItem` no registro `5`.

  Em `tst_aurora_navigation.qml`, ajuste `test_tabFollowsSidebarHeaderPageAndPlayerOnEveryPage` para a ordem nova da Biblioteca, com a mesma força.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** o teclado, o reposicionamento do foco depois de remover e a devolução do foco no `closed` do menu e do diálogo. Acrescente ao guia de uso um item curto: clique duplo, Enter e ▶ tocam agora (logo depois da atual); “Adicionar à fila” põe no fim; tecla Menu e Shift+F10 abrem o menu.
- [ ] **Passo 4: ver passar** `tst_library`, `tst_aurora_navigation`, `tst_keyboard_full_journey`, `tst_primary_keyboard`, `tst_accessible_interface_tree` e os congelados (sem edição).
- [ ] **Passo 5: commit** `feat: leva o Tocar agora e o menu da Biblioteca ao teclado`

### Task 7: estante “Da sua biblioteca” toca agora

**Arquivos:**
- Modificar: `src/ui/qml/HomePage.qml`, `src/ui/qml/HomeShelf.qml`
- Testes: `tests/ui/quick/tst_aurora_composition.qml`, `tst_aurora_navigation.qml`; as bibliotecas falsas de `tst_aurora_composition`, `tst_aurora_navigation`, `tst_lyrics` e `tst_library` ganham `playNow` e `playingRecordId`

**Interfaces:**
- Consome: `LibraryBinding.playNow`, `playingRecordId` (tarefa 3); `AuroraTrackCard` (tarefa 4).
- Produz: `HomeShelf.focusLost()` (sinal emitido quando a estante some com o foco dentro).

- [ ] **Passo 1: testes que falham** em `tst_aurora_composition.qml`:
  - `test_libraryShelfCardsPlayNow` (substitui `test_libraryShelfShowsTheNewestTracksReadOnly`, mantendo os asserts de título, contagem, capa e indisponível): clique num cartão e Enter noutro registram os dois ids em `played`; o nome acessível é “Tocar <título> agora”; o cartão do `playingRecordId` mostra `shelf-card-playing` só em Playing; o indisponível está desabilitado;
  - `test_libraryShelfCardsAreDisabledWithoutOpening`: com `openingEnabled` falso (convidado de sala), os cartões estão desabilitados e fora do Tab;
  - `test_libraryShelfKeepsItsPlaceOnSave`: com a lista rolada até o cartão 8 e o foco nele, uma gravação que só muda a tag de outro registro (a biblioteca falsa troca um `ListModel.setProperty`) mantém `contentX` e o foco no cartão 8;
  - `test_focusLeavesAHidingLibraryShelfForOpenAudio` (pendência do plano 4): com o foco em `home-see-library` e depois num cartão, `catalog.ready = false` esconde a estante e o foco vai para `open-audio`.

  Em `tst_aurora_navigation.qml`, `test_homeTabOrder`: depois de `home-see-library` vêm os cartões disponíveis da biblioteca, um por Tab, e então `lyrics-line-open` ou `progress`.
- [ ] **Passo 2: ver falhar.**
- [ ] **Passo 3: implementar** conforme as “Decisões”. O `HomeShelf` observa `visible`: se a estante some com o foco dentro (link ou cartão), emite `focusLost`, e a `HomePage` foca `open-audio`.
- [ ] **Passo 4: ver passar** `tst_aurora_composition`, `tst_aurora_navigation`, `tst_lyrics`, `tst_library`, `tst_listening_room` e `tst_empty_shell` (sem edição).
- [ ] **Passo 5: commit** `feat: toca agora a partir da estante da biblioteca na Início`

### Task 8: contraste e alto contraste da Biblioteca

**Arquivos:**
- Testes: `tests/ui/quick/tst_color_contrast.qml`, `tests/ui/quick/tst_library.qml`
- Modificar: só se um par falhar (troque o token, nunca o limite)

- [ ] **Passo 1: testes** em `tst_color_contrast.qml`, nos temas claro e escuro, medindo sobre o fundo renderizado como os testes vizinhos:
  - `test_libraryRowTextContrast`: `track-row-subtitle`, `track-row-album`, `track-row-duration` e `track-row-number` com 4,5:1 sobre `canvasColor` e sobre `hoverColor` (linha em hover); `track-row-title` da faixa que toca, em `accentColor`, com 4,5:1 sobre os dois fundos;
  - `test_libraryUnavailableContrast`: `track-row-unavailable` (Aviso) e o título secundário do indisponível com 4,5:1;
  - `test_libraryIconsContrast`: `track-row-wave` e o ícone de `track-row-play` sobre o círculo com 3:1; o contorno de foco da linha com 3:1 sobre o fundo;
  - `test_libraryBadgesContrast`: o `chip-label` de `shelf-card-playing` e de `shelf-card-unavailable` com 4,5:1 sobre o `chip-background`;
  - `test_libraryFeedbackContrast`: `feedback-text` de `library-feedback` em Sucesso e Erro com 4,5:1.

  Em `tst_library.qml`, `test_libraryHighContrast`, em alto contraste:
  - a linha atual usa as cores de seleção do sistema, e o contorno de foco tem 2 px;
  - ▶, + e ⋯ têm contorno de 2 px e nenhuma sombra; os botões elevados dos cartões também;
  - o menu tem contorno de 2 px em `textPrimaryColor`; o item destrutivo continua distinguível;
  - os selos têm contorno e os textos usam as cores do sistema.
- [ ] **Passo 2: rodar.** O que passar já fica como regressão. O que falhar troca de token, com teste vermelho antes.
- [ ] **Passo 3: commit** `test: fixa o contraste da Biblioteca` (ou `fix:`, se algum token mudar)

### Task 9: capturas, documentação e validação da fatia

**Arquivos:**
- Modificar: `tests/ui/quick/tst_library.qml` (capturas), `docs/ui-tests.md`
- Capturas em `$w/out/product/ui-redesign-captures/biblioteca` (não versionadas)

- [ ] **Passo 1: capturas da Biblioteca.** Acrescente `test_libraryCaptures`, a 1440×900 e 720×520, nos temas claro e escuro, com `RC_MUSIC_UI_ARTIFACT_DIR`, o `artifactPath` do driver e uma biblioteca falsa de 128 registros (um indisponível e um tocando, em Playing pelo driver). As cenas são:
  - `library-list`, com a linha da faixa que toca, uma linha em hover com ▶, + e ⋯, a linha indisponível com “Localizar” e a confirmação “Ver fila”;
  - `library-menu`, com o menu de uma linha aberto e o submenu de playlists;
  - `library-covers`, na vista Capas, com os selos e um cartão em hover;
  - `library-empty`, sem registros.

  Os nomes ficam `aurora-biblioteca-<cena>-<largura>-<light|dark>.png`. Abra todas as 16 imagens e confira:
  - que batem com `biblioteca.mjs` e `menus.mjs` e, na compacta, com `compacto.mjs`;
  - que não há texto cortado nem sobreposto, e que só a lista rola;
  - que o título da página está inteiro.
- [ ] **Passo 2: `docs/ui-tests.md`.** Acrescente a seção “Biblioteca (fatia 5)”, no formato da “Início (fatia 4)”, com:
  - os ids novos e os que mudaram (`library-view-0/1`, `library-feedback`, `library-see-queue`, `library-status-notice`, `library-track-menu`, `library-menu-*`, `library-playlist-none`, `track-row-*`, `shelf-card-play/enqueue/more/unavailable`; `library-enqueue`, `library-locate`, `library-remove`, `library-add-to-playlist`, `library-notice`, `library-view-list/covers` e `library-card-art` saem; `HomeShelfCard.qml` virou `AuroraTrackCard.qml`);
  - a ordem de Tab e o teclado da lista e da grade;
  - Tocar agora: onde a faixa entra, sem faixa atual, indisponível, sala e Aleatório;
  - a regra de rolagem única e o modelo sem reset;
  - a estante “Da sua biblioteca” interativa e a ordem de Tab nova da Início;
  - a pasta das capturas.
- [ ] **Passo 3: validação da fatia:**
  - a suíte Quick inteira, `rc_music.ui.quick` (0 failed, 0 XFAIL);
  - `rc_music.ui.contract`, `rc_music.ui.build-boundary`, `rc_music.ui.presenter`, `rc_music.library.binding` e `rc_music.library.artwork` (e `rc_music.library.list-model`, se existir);
  - os smokes de processo `rc_music.process.combined-startup`, `rc_music.process.error-recovery` e `rc_music.process.tray-lifecycle`;
  - `all_qmllint`, onde só vale o aviso antigo de `ListeningRoomPanel.qml:37`.

  Relate à parte o que passou, o que falhou e o que ficou pendente. Registre como risco residual que a ligação `main.cpp` entre biblioteca e presenter só é coberta pelos smokes, sem teste que toque som.
- [ ] **Passo 4: commit** `test: grava as capturas da Biblioteca e documenta a fatia 5`

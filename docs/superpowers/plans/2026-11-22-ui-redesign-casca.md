# Redesenho da interface, fatia 2: casca e player — plano de implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: use superpowers:subagent-driven-development (recomendado) ou superpowers:executing-plans para executar este plano tarefa por tarefa. Os passos usam caixas (`- [ ]`) para acompanhamento.

**Objetivo:** dar à casca do modo Aurora a forma aprovada:
- lateral sem a Fila, na ordem nova, com contagens, Navegar com ícone de janela externa e o chip do perfil;
- topo com o caminho e o botão de tema tonal;
- player C, com o progresso na borda de cima e sem os botões de ±5 s.

Também fecha as duas pendências que o plano 1 deixou para cá.

**Arquitetura:** a casca continua nos arquivos de hoje:
- `Main.qml`, `SideNavigation.qml` e `NavigationButton.qml`;
- `WorkspaceHeader.qml`, `TransportBar.qml` e `TransportSlider.qml`.

Tudo muda só no ramo Aurora (`editorial`/`inlinePages` verdadeiros). O ramo do fixture M0 (`usesFrozenFixture`) fica como está. O player continua sendo um único `TransportBar`, dono de toda a lógica de reprodução, sala, busca de posição e dicas. A identidade da faixa passa de `Main.qml` para dentro dele. Peças novas do plano 1 entram onde o spec pede peça nova:
- `AuroraIconButton` tonal para o tema;
- `AuroraAvatar` no chip do perfil.

**Stack:** C++20, Qt 6.11.2 Quick/QML com estilo Basic, Qt Test e Qt Quick Test, CMake/Ninja.

**Spec:** [2026-11-22-ui-redesign-design.md](../specs/2026-11-22-ui-redesign-design.md), seções “Casca”, “Acessibilidade” e “Comportamento novo” (item 8). O anexo de textos é o [2026-11-22-ui-redesign-copy.md](../specs/2026-11-22-ui-redesign-copy.md). Os mockups aprovados são `out/product/ui-redesign-mockups-20261122/gerador/shell.mjs`, `player.mjs` (opção C) e `compacto.mjs`.

## Restrições globais

- **Fixture M0 intacto:**
  - os ramos `!inlinePages`, `!editorial` e `usesFrozenFixture` não mudam de aparência, ordem de Tab nem ids;
  - os testes Quick que montam o shell congelado continuam passando sem edição: `tst_empty_shell`, `tst_high_contrast`, `tst_lifecycle`, `tst_lifecycle_transitions`, `tst_keyboard_shortcuts`, `tst_keyboard_exit`, `tst_aurora_shell` e os casos de `tst_preferences` e `tst_accessibility_*` que usam `testPlayerPresenter`;
  - se um deles precisar mudar, pare: é sinal de que o ramo congelado foi tocado.
- **Ids que mudam de lugar continuam únicos.** Quando os dois modos precisam do mesmo `objectName` (por exemplo `theme-toggle`), cada um vive num `Loader` com `active` exclusivo, para que `findChild` nunca ache a cópia do outro modo.
- **Sem módulo Qt novo no runtime:** nada de `QtQuick.Effects`, `QtQuick.Shapes`, `Qt5Compat` ou shaders.
- **Texto visível só em catálogos `*Text.qml` com `qsTr`.**
  - Fora deles, literais só como identificador (`objectName`, `iconName`, `controlId`).
  - `"transparent"` só direto em `color:` ou `border.color:`.
  - O teste `rc_music.ui.contract` varre `src/ui/qml` inteiro, comentários incluídos.
  - Ao mudar ou criar um `qsTr`, atualize `src/ui/translations/music-player_pt_BR.ts` com o `lupdate` do build e preencha as formas de plural à mão, como em `TransportText.skipSeconds` (`%n segundo`).
- **Cores e medidas** vêm do `ShellUiContract`. Nenhuma cor literal.
- **Contraste:**
  - `textSecondaryColor` nunca sobre `selectionColor` nem `hoverColor` (no claro dá 4,37:1); nesses fundos, use `textPrimaryColor`;
  - texto pede 4,5:1;
  - ícones, marcador do progresso e anel de foco pedem 3:1.
- **Alvo de clique:** toda área clicável tem pelo menos `contract.controlTargetMinimum` (40 px). Quando o spec pede um botão visualmente menor (aleatório e repetir com 36 px), o botão mede 40 px e só o fundo desenhado mede 36. Exceção: a barra de progresso ocupa a largura inteira com 24 px de altura.
- **Foco:** os botões da casca (`NavigationButton`, `AccessibleTransportButton`) mantêm o contorno de foco de 3 px em `focusColor` que já têm. Peças do plano 1 usam `AuroraFocusRing`.
- **Movimento:** animações usam `contract.feedbackTransitionMs` ou `contract.panelTransitionMs`.
- **Alto contraste:** segue as regras das peças do plano 1. A casca já tem os ramos `contract.highContrast`; o que é novo nesta fatia segue o mesmo padrão: contagens e textos em `textPrimaryColor`, o selo da Fila com contorno de 2 px em vez de preenchimento, e sem sombra.
- **Testes Quick:**
  - rodam com `QT_FATAL_WARNINGS=1`, então qualquer aviso é falha;
  - os testes do modo Aurora usam `testLocalPlayerPresenter` com `testProductUiContract`;
  - todo `cleanup()` volta para claro, sem movimento reduzido e sem alto contraste.
- **Quando um teste existente muda** porque o id, a ordem de Tab ou o texto mudou, ele muda no mesmo commit, para o comportamento novo aprovado, e nunca verifica menos do que antes. A mensagem de commit diz qual verificação mudou e por quê.
- **Som:** nenhum passo toca som. Se algum teste novo precisar tocar, pare e peça permissão ao mantenedor antes.
- **Formato:** não rode `develop.ps1 format -Apply`, porque ele reescreve o fim de linha de uns 110 QML. Formate só os seus arquivos, com `clang-format -i` e `qmlformat -i` da toolchain.
- **Commits:** pequenos, Conventional Commits em português no imperativo, terminando com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Antes de cada commit: `git status` e revisão do diff.

### Comandos

Os mesmos do plano 1 (seção “Comandos”). A pasta do build (`$b`) é a do worktree desta fatia; o despacho de cada tarefa informa o caminho.

| Para | Comando | Passou quando |
|---|---|---|
| Compilar | `cmake --build $b --target rc_music_ui_quick_tests rc_music_ui_contract_tests` | termina sem erro |
| Um arquivo Quick | `& "$b/tests/ui/rc_music_ui_quick_tests.exe" -input tests/ui/quick/<arquivo>.qml -import "$b/tests/ui/qml" -o -,txt` | `Totals: N passed, 0 failed, 0 skipped` |
| Uma função Quick | o mesmo, acrescentando `<NomeDoTestCase>::<função>` | idem |
| Contrato de UI | `ctest --test-dir $b -R '^rc_music\.ui\.contract$' --output-on-failure --no-tests=error` | `100% tests passed` |
| qmllint | `cmake --build $b --target all_qmllint` | nenhum aviso novo nos arquivos tocados |

Cada tarefa roda os arquivos Quick que tocou e os que a seção “Arquivos” lista como consumidores. A suíte Quick inteira roda uma vez, na tarefa 8.

## Foco da revisão

Cinco situações que o spec implica e que nenhum teste de hoje cobre. Cada uma ganha um teste na tarefa dona do código.

1. **Ctrl+L sem o item Fila na lateral.**
   - A fila abre com fila carregada vazia e para o convidado da sala, como hoje.
   - Esc devolve o foco a quem estava focado antes de abrir.
   - Teste: tarefa 2, `test_ctrlLOpensTheQueueWithoutTheSidebarItem`.
2. **Título, artista e álbum longos na janela mínima.** O texto é cortado com reticências, nada sobrepõe os controles e o nome acessível continua inteiro. Teste: tarefa 6, `test_identityElidesLongMetadataAtMinimumWindow`.
3. **Convidado da sala no player novo.**
   - O selo da sala cabe no lado direito a 720×520.
   - Aleatório, repetir, anterior e próxima mantêm o motivo do desabilitado.
   - Teste: tarefa 7, `test_roomBadgeFitsTheRightClusterAtMinimumWindow`.
4. **Perfil ausente ou vazio.**
   - Sem `profile`, ou com `available` falso, o chip some.
   - Com apelido vazio, mostra só “Seu perfil”.
   - Sem imagem, mostra o disco de reserva.
   - Teste: tarefa 4, `test_profileChipHandlesMissingProfileAndEmptyNickname`.
5. **Trocar o tema pelo topo.** O foco fica no botão de tema e a reprodução não é interrompida. Teste: tarefa 5, `test_themeToggleKeepsFocusAndPlayback`.

## Decisões deste plano

- **Escopo:** a fatia 2 do spec (casca e player). Ficam para os planos donos:
  - **plano 3:** F1/F2 desligados da gaveta da Fila e a saída de Aleatório e Repetir da gaveta;
  - **plano de cada página:** a retirada do botão de fechar, onde se confere o Esc de cada uma.
- **Fila fora da lateral:** no Aurora, o `NavigationButton` `queue` fica com `visible: !editorial`. O fixture continua com ele.
  - Ctrl+L passa a depender de `presenter.queueLoaded || roomGuest`, e não mais do botão.
  - Ao fechar a gaveta, o foco volta ao item que estava focado quando ela abriu. Se ele não estiver mais visível e habilitado, vai para `player-queue` e, por fim, para `play-pause`.
- **Ordem da lateral no Aurora:** Início, Biblioteca, Buscar, Playlists, Ouvir junto, Atividade, Navegar. Depois um espaço, Configurações e o chip do perfil.
  - Mudar a ordem dos filhos não muda o fixture: lá, Biblioteca, Buscar e Ouvir junto são invisíveis, e Início, Fila, Playlists, Atividade e Navegar mantêm a ordem relativa.
- **Contagens:**
  - Biblioteca: `library.count`, só com `library.ready`.
  - Playlists: `playlists.playlists.length`, só com `playlists.ready`.
  - Atividade fica sem contagem: nenhum binding expõe quantos downloads precisam de atenção, e o spec proíbe criar estado para isso.
- **Navegar:**
  - mantém o ícone `browser` (globo);
  - ganha à direita o ícone novo `external` (Phosphor `regular/arrow-square-out`), que marca a janela externa;
  - na lateral compacta, o ícone externo some.
- **Tema:**
  - **Aurora:** um `AuroraIconButton` tonal com `controlId: "theme-toggle"`, que mostra sol no tema escuro e lua no claro, dentro de um `Loader` no `WorkspaceHeader` com `active` igual ao modo Aurora.
  - **Fixture:** o botão de hoje continua na lateral, também num `Loader`, com `active` igual ao modo do fixture.
  - Os dois ficam ligados ao mesmo sinal `themeRequested`.
- **Topo:** 60 px de altura, no tamanho normal e no compacto, com 28 px de margem lateral, conforme o mockup.
- **Perfil:** o chip abre Configurações › Perfil.
  - O `PreferencesPanel` ganha a seção pedida, que vale na próxima abertura.
  - Se ele já estiver aberto, muda de seção na hora.
- **Player C:** o progresso é um `TransportSlider` com `edge: true`:
  - **Área:** largura inteira do player e 24 px de altura, que é a área de clique.
  - **Trilho:** 4 px colado na borda de cima, em `dividerColor`, com o preenchimento em `accentColor`.
  - **Marcador:** 14 px em `accentColor`, com aro de 3 px em `surfaceColor`, centrado no trilho.
  - **Foco:** anel em volta do marcador.
  - **Linha separadora:** some.
- **Medidas do player (mockup C):**
  - Lado esquerdo: 260 px, ou 200 px na janela compacta. Tem a capa de 64 px (48 px na compacta), o título com serifa em `contract.typeTitle`, “artista · álbum” e “m:ss / m:ss”.
  - Centro: aleatório e repetir com 36 px visuais (ícone de 18 px), anterior e próxima com 40 px (ícone de 22 px), e play/pausa redondo de 52 px (ícone de 22 px). O espaço entre eles é de 6 px.
  - Lado direito: 260 px, ou a largura natural na janela compacta. Tem Letra, Fila e volume, só com ícone. A barra de volume mede 90 px e a porcentagem 34 px; na janela compacta, as duas somem.
  - Margens internas: 20 px nas laterais e 14 px embaixo. O conteúdo começa logo abaixo da área de 24 px do progresso.
- **Janela compacta:** `window.width <= contract.compactWidth` (900 px), o mesmo critério da lateral.
- **Tempo:** no Aurora, o player mostra “1:19 / 3:37”. Os minutos não têm zero à esquerda, e acima de uma hora o formato vira `h:mm:ss`. O fixture continua com `elapsed-time` e `duration-time` em `mm:ss`.
- **Textos desta fatia:**
  - linhas do anexo:
    - `TransportText`: `seekInactive`, `lyricsAction` e `lyricsUnavailable`;
    - `NavigationText`: `localAudio`, e `collectionHint` sai;
    - `PlaybackAccessibilityText`: `loadingAudio` e `skippedDuringImport`;
  - textos novos desta tabela:

    | Catálogo | Propriedade | Texto |
    |---|---|---|
    | `NavigationText` | `trackCount(count)` | `qsTr("%n faixa", "Contagem da Biblioteca na lateral", count)` |
    | `NavigationText` | `playlistCount(count)` | `qsTr("%n playlist", "Contagem de Playlists na lateral", count)` |
    | `ProfileText` | `yourProfile` | “Seu perfil” |
    | `ProfileText` | `openProfile` | “Abrir seu perfil” |
    | `TransportText` | `elapsedOfDuration` | “%1 / %2” |
    | `TransportText` | `artistAlbum` | “%1 · %2” |

  - Os outros textos de Perfil e Configurações ficam para o plano 8, e `playTrack` e `pageDescription` para o plano 4.

---

### Tarefa 1: Pendências do plano 1 (foco do diálogo e anel da chave segmentada)

**Arquivos:**
- Modificar: `src/ui/qml/AuroraDialog.qml:50`
- Modificar: `src/ui/qml/AuroraSegmented.qml:163` (o `AuroraFocusRing` de cada segmento)
- Teste: `tests/ui/quick/tst_aurora_dialogs.qml`, `tests/ui/quick/tst_aurora_choices.qml`, `tests/ui/quick/tst_aurora_pieces_visual.qml`

**Interfaces:**
- Nenhuma API nova. O diálogo continua focando o primeiro controle do corpo, ou o ×, ao abrir. O anel do segmento passa a usar `inside: true`.

- [ ] **Passo 1: Escrever os testes que falham.**
  - `tst_aurora_dialogs.qml`, `test_openingFocusDoesNotPopTheCloseToolTip`:
    - abra um diálogo sem controles no corpo;
    - verifique que o × tem `activeFocus` e que `visualFocus` é falso;
    - espere 700 ms (`wait(700)`) e verifique que `ToolTip.visible` do × continua falso;
    - depois, um Tab e um Shift+Tab mostram a dica.
  - `tst_aurora_choices.qml`, `test_segmentFocusRingStaysInsideTheTrack`:
    - foque o segundo de três segmentos com Tab e setas;
    - o `focus-ring` visível tem `x >= 0`, `y >= 0`, `x + width <= segment.width` e `y + height <= segment.height`, nas coordenadas do segmento.
  - `tst_aurora_pieces_visual.qml`: acrescente à galeria uma chave segmentada com o segundo segmento focado e salve `aurora-pecas-segmentada-foco-<tema>.png` com `driver.artifactPath(...)`, em claro e escuro.
- [ ] **Passo 2: Rodar e ver falhar.** O teste do diálogo falha porque a dica aparece. O da chave falha porque o anel sai 5 px do segmento.
- [ ] **Passo 3: Implementar.**
  - No diálogo, `forceActiveFocus(Qt.OtherFocusReason)`.
  - No segmento, `inside: true` no anel.
- [ ] **Passo 4: Rodar e ver passar.**
  - Os três arquivos passam com `0 failed`.
  - Rode a galeria com `RC_MUSIC_UI_ARTIFACT_DIR` apontando para `out/product/ui-redesign-captures/casca` (crie a pasta antes) e confira as duas capturas: o anel inteiro dentro do segmento, sem cobrir o vizinho.
- [ ] **Passo 5: Commit.** `fix: abre o diálogo sem a dica do fechar e mantém o anel do segmento dentro da chave`.

### Tarefa 2: Fila fora da lateral, com Ctrl+L e retorno do foco

**Arquivos:**
- Modificar: `src/ui/qml/SideNavigation.qml:181-198` (item `queue`)
- Modificar: `src/ui/qml/Main.qml`, nos pontos abaixo:
  - `setQueueOpen`, linhas 279-301;
  - `restoreFocusAfterSourceImport`, linhas 428-437;
  - o atalho Ctrl+L, linhas 1215-1221;
  - o alias `queueButton`, linha 55.
- Modificar: `src/ui/qml/TransportBar.qml` (alias para o botão da Fila)
- Teste: `tests/ui/quick/tst_aurora_navigation.qml`, mais os arquivos que fazem `findChild(shell, "queue")` no modo Aurora. Hoje são estes:
  - `tst_aurora_composition`, `tst_accessibility_state_focus`, `tst_error_microcopy` e `tst_local_source_drop`;
  - `tst_playlists:819-841` e `tst_primary_keyboard`;
  - `tst_keyboard_shortcuts:182`, só se o caso usar o presenter local.

  Os casos que usam `testPlayerPresenter` não mudam.

**Interfaces:**
- Produz:
  - `TransportBar.queueControl: Item`, alias de `player-queue`;
  - `Main.queueReturnFocusItem: Item`, o item focado quando a gaveta abriu;
  - `Main.queueShortcutEnabled: bool`, igual a `windowShortcutsEnabled && (presenter.queueLoaded || roomGuest) && !presenter.exiting && !preferencesPanel.visible`. No fixture, continua `queueButton.enabled`.
- Consome: nada de tarefas anteriores.

- [ ] **Passo 1: Escrever os testes que falham** em `tst_aurora_navigation.qml`.
  - `test_auroraSidebarHasNoQueueItem`: o `queue` existe mas `visible === false`, e Tab a partir de `now` nunca o alcança (no máximo 20 Tabs, até chegar ao `transport-bar`).
  - `test_ctrlLOpensTheQueueWithoutTheSidebarItem`:
    - com a fila sintética carregada, foque `play-pause` e pressione Ctrl+L: `queue-panel` fica visível e `queue-list` recebe o foco;
    - Esc fecha a gaveta e devolve o foco a `play-pause`;
    - abrir pelo `player-queue` (clique) e fechar com Esc devolve o foco a `player-queue`.
  - O mesmo caso com o presenter sem faixas, mas com `queueLoaded` verdadeiro: Ctrl+L abre a gaveta vazia.
  - Os testes atuais que clicavam no `queue` da lateral, no modo Aurora, passam a abrir pela `player-queue` ou por Ctrl+L. Os que conferiam o foco de volta no `queue` passam a conferir em quem abriu.
- [ ] **Passo 2: Rodar e ver falhar.** `test_auroraSidebarHasNoQueueItem` falha porque o item está visível.
- [ ] **Passo 3: Implementar.**
  - `queue` com `visible: !root.editorial`.
  - Em `setQueueOpen(true)`, guarde `queueReturnFocusItem = window.activeFocusItem`, só no modo Aurora.
  - Ao fechar, devolva o foco na ordem das decisões: quem abriu, depois `transport.queueControl` e por fim `transport.primaryControl`.
  - `restoreFocusAfterSourceImport` usa a mesma cadeia no Aurora.
  - No fixture, nada muda.
- [ ] **Passo 4: Rodar e ver passar.** Rode `tst_aurora_navigation` e cada arquivo da lista acima, mais `tst_empty_shell` e `tst_keyboard_full_journey` como guarda do fixture e do Ctrl+L. Todos com `0 failed`.
- [ ] **Passo 5: Commit.** `feat: tira a Fila da lateral e mantém Ctrl+L e o retorno do foco`.

### Tarefa 3: Lateral na ordem nova, com contagens e Navegar externo

**Arquivos:**
- Criar: `assets/icons/external.svg`. A origem é `https://raw.githubusercontent.com/phosphor-icons/core/2b75f3ad12b420c9504ef05df8d2564a28f8500e/assets/regular/arrow-square-out.svg`, baixado com `curl -fsSL`. O aviso do Phosphor já cobre o arquivo.
- Modificar: `src/ui/qml/SideNavigation.qml` (ordem dos filhos e propriedades de contagem), `src/ui/qml/NavigationButton.qml` e `src/ui/qml/Main.qml:439-487`
- Modificar: `src/ui/qml/NavigationText.qml` (`trackCount`, `playlistCount`, e `collectionHint` sai) e `src/ui/translations/music-player_pt_BR.ts`
- Teste: `tests/ui/quick/tst_aurora_navigation.qml`, `tests/ui/quick/tst_shell_icons.qml` e `tests/ui/quick/tst_playlists.qml:819-841`

**Interfaces:**
- Produz:
  - `NavigationButton.count: int`, com padrão `-1`, que esconde a contagem;
  - `NavigationButton.countText: string`, que vai para `Accessible.description` quando não está vazio;
  - `NavigationButton.trailingIconName: string`, com padrão `""`;
  - `SideNavigation.libraryCount: int` e `SideNavigation.playlistCount: int`, ambos com padrão `-1`.
- Filhos novos do `NavigationButton`:
  - `navigation-count`: um `Text` à direita, em `contract.typeCaption`, visível com `count >= 0 && !compact`;
  - `navigation-trailing-icon`: um `ShellIcon` de 16 px à direita, visível com `trailingIconName` preenchido e `!compact`.
- Cor da contagem e do ícone externo: `textSecondaryColor` em repouso, e `textPrimaryColor` quando o item está escolhido, com hover ou em alto contraste.

- [ ] **Passo 1: Escrever os testes que falham.**
  - `tst_shell_icons.qml`: acrescente `external` ao `catalog`.
  - `tst_aurora_navigation.qml`, `test_sidebarFollowsTheApprovedOrder`:
    - monte o shell com `library`, `playlists`, `onlineSearch`, `room`, `downloads` e `embeddedBrowser` falsos, como nos testes de cada página;
    - os itens visíveis têm `y` crescente, nesta ordem: `now`, `library`, `search`, `playlists`, `room`, `downloads`, `embedded-browser`, `preferences`.
  - `test_sidebarShowsCountsOnlyFromBindings`:
    - com a biblioteca falsa (`ready: true`, `count: 128`), `navigation-count` de `library` mostra “128” e `Accessible.description` é “128 faixas”;
    - com duas playlists, mostra “2” e “2 playlists”;
    - `downloads` não tem contagem visível;
    - com `ready: false`, a contagem some;
    - a 720×520, a contagem some e a descrição continua.
  - `test_navegarMarksTheExternalWindow`: `embedded-browser` tem `trailingIconName === "external"`, com o ícone visível a 1440×900 e invisível a 720×520.
  - `test_productContrastUsesActualBackgrounds`: acrescente a contagem e o ícone externo. Texto pede 4,5:1 e ícone 3:1, contra `navigationColor`, `hoverColor` e `selectionColor`, nos dois temas.
  - `tst_playlists.qml:819-841`: troque a lista esperada pela ordem nova, sem `queue`.
- [ ] **Passo 2: Rodar e ver falhar.** A ordem e as contagens falham, e `external` falha no catálogo.
- [ ] **Passo 3: Implementar.**
  - Baixe o ícone.
  - Reordene os filhos conforme as decisões.
  - Em `Main.qml`:
    - `libraryCount: library !== null && library.ready ? library.count : -1`;
    - `playlistCount: playlists !== null && playlists.ready ? playlists.playlists.length : -1`.
  - `NavigationButton` reserva à direita a largura da contagem ou do ícone, para o nome elidir antes deles.
- [ ] **Passo 4: Rodar e ver passar.** Rode `tst_aurora_navigation`, `tst_shell_icons`, `tst_playlists`, `tst_library`, `tst_search` e `tst_embedded_browser`, mais o contrato de UI. Todos verdes.
- [ ] **Passo 5: Commit.** `feat: ordena a lateral, mostra contagens e marca o Navegar como janela externa`.

### Tarefa 4: Chip do perfil que abre Configurações › Perfil

**Arquivos:**
- Criar: `src/ui/qml/ProfileChip.qml`, registrado em `cmake/RcMusicUi.cmake`
- Modificar: `src/ui/qml/SideNavigation.qml` (chip depois de `preferences`)
- Modificar: `src/ui/qml/Main.qml`, em `openPreferences` (linhas 330-346) e na instância da lateral
- Modificar: `src/ui/qml/PreferencesPanel.qml:14` e `:125-126`
- Modificar: `src/ui/qml/ProfileText.qml` (`yourProfile`, `openProfile`) e o `.ts`
- Teste: `tests/ui/quick/tst_profile.qml`

**Interfaces:**
- Produz:
  - `ProfileChip`: um `AbstractButton` com `controlId: "profile-chip"` e estas propriedades:
    - `required property ShellUiContract contract`;
    - `property var profile`;
    - `property bool compact`.
  - Filhos do chip:
    - `profile-chip-avatar`: um `AuroraAvatar` de 32 px, com `backgroundColor` igual à cor atual do fundo do chip;
    - `profile-chip-nickname`: semibold, elidido;
    - `profile-chip-caption`: “Seu perfil”, em `textSecondaryColor` em repouso e em `textPrimaryColor` com hover.
  - O nome acessível é “Abrir seu perfil” e a descrição é o apelido.
  - Na lateral compacta, só o avatar aparece. A dica mostra o nome acessível.
  - Visível com `profile !== null && profile.available`.
  - `SideNavigation.profile: var` e o sinal `SideNavigation.profileRequested()`.
  - `PreferencesPanel`:
    - `readonly property int profileSection: 4`;
    - `property int requestedSection: 0`, lido e zerado no `onAboutToShow` no lugar de `section = 0`.
  - `Main.openProfile()`:
    - se o painel já estiver aberto, faz `section = profileSection`;
    - senão, põe `requestedSection` e chama `openPreferences()`.
    - O retorno do foco segue a regra das outras páginas, com o chip como quem abriu.

- [ ] **Passo 1: Escrever os testes que falham** em `tst_profile.qml`, com o perfil falso que o arquivo já usa.
  - `test_sidebarProfileChipOpensProfileSettings`:
    - o chip está visível, abaixo de `preferences`;
    - mostra o apelido e “Seu perfil”;
    - o `source` do avatar é igual a `profile.avatarSource`.
    - Clique e Enter abrem Configurações com `section === profileSection`, e `preferences-profile-nickname` fica visível.
    - Com Configurações já aberto em outra seção, o chip muda para Perfil sem fechar a página.
    - Esc fecha e devolve o foco ao chip.
  - `test_profileChipIsAvatarOnlyWhenCompact`: a 720×520, o apelido e o rótulo ficam invisíveis, `Accessible.name === "Abrir seu perfil"` e a descrição é o apelido.
  - `test_profileChipHandlesMissingProfileAndEmptyNickname`:
    - com `profile: null`, o chip não fica visível;
    - com `available: false`, também não;
    - com apelido vazio, mostra só “Seu perfil” e a descrição fica vazia;
    - com `avatarSource` vazio, o `avatar-fallback` fica visível.
  - Contraste de `profile-chip-caption` e do apelido contra `navigationColor` e `hoverColor`, nos dois temas.
- [ ] **Passo 2: Rodar e ver falhar.** `profile-chip` não existe.
- [ ] **Passo 3: Implementar** conforme as interfaces. A ordem de Tab fica: `preferences`, `profile-chip`, depois o topo.
- [ ] **Passo 4: Rodar e ver passar.** Rode `tst_profile`, `tst_preferences`, `tst_aurora_navigation` e `tst_playlists`, este último incluindo o chip na verificação de ordem e encaixe da lateral. Rode também o contrato de UI. Todos verdes.
- [ ] **Passo 5: Commit.** `feat: põe o chip do perfil na lateral, abrindo Configurações › Perfil`.

### Tarefa 5: Topo com o botão de tema tonal

**Arquivos:**
- Modificar: `src/ui/qml/WorkspaceHeader.qml` (altura 60, margens de 28 px e `Loader` do tema)
- Modificar: `src/ui/qml/SideNavigation.qml:308-327` (o botão do fixture vai para um `Loader` com `active: !editorial`, e `themeHost` sai)
- Modificar: `src/ui/qml/Main.qml:447`, `:481` e `:488-498`
- Teste: `tests/ui/quick/tst_aurora_navigation.qml`, `tests/ui/quick/tst_aurora_composition.qml`, `tests/ui/quick/tst_aurora_pages.qml`, `tests/ui/quick/tst_playlist_page_lifecycle.qml` e `tests/ui/quick/tst_playlists.qml`

**Interfaces:**
- Produz:
  - `WorkspaceHeader.darkTheme: bool` e o sinal `WorkspaceHeader.themeRequested()`;
  - dentro dele, `AuroraIconButton`, com:
    - `controlId: "theme-toggle"`;
    - `tonal: true`;
    - `iconName: darkTheme ? "sun" : "moon"`;
    - `text: darkTheme ? labels.themeLight : labels.themeDark`.
  - `SideNavigation.themeButton` passa a apontar para o `Loader.item` do fixture.
  - `Main.themeButton: Item` aponta para o botão do modo ativo.

- [ ] **Passo 1: Escrever os testes que falham.**
  - `tst_aurora_navigation.qml`: reescreva `test_quickThemeUsesThePreferenceAndPreservesPlayback` para o botão tonal:
    - `findChild(shell, "theme-toggle")` está dentro de `workspace-header`;
    - `tonal === true`;
    - `iconName` é “moon” no claro e “sun” no escuro;
    - `Accessible.name` é “Ativar tema escuro” e depois “Ativar tema claro”.
    - Enter, Espaço e `Accessible.pressAction` trocam `presenter.themePreference` e a cor do shell como hoje.
  - `test_themeToggleKeepsFocusAndPlayback`:
    - com reprodução sintética em curso, foque o botão pelo teclado e troque o tema duas vezes;
    - o botão mantém `activeFocus`, `presenter.playbackState` e `positionMs` não regridem, e nenhum comando de áudio é emitido.
  - Em `test_productContrastUsesActualBackgrounds` e `test_navigationUsesLineIconsAndTheAuroraBrandMark`, as verificações do `theme-toggle` passam a usar os campos do `AuroraIconButton`:
    - ícone contra `fillColor` a 3:1;
    - anel de foco contra `canvasColor` a 3:1.
  - `tst_aurora_composition.qml`: `workspace-header.height === 60` em todos os tamanhos, no lugar de `>= 64`.
  - `tst_aurora_pages.qml`, `tst_playlist_page_lifecycle.qml` e `tst_playlists.qml`: ajuste o que localiza o tema pela lateral.
- [ ] **Passo 2: Rodar e ver falhar.** `tonal` não existe no botão de hoje.
- [ ] **Passo 3: Implementar** conforme as interfaces, com o `Loader` Aurora em `active: !presenter.usesFrozenFixture`.
- [ ] **Passo 4: Rodar e ver passar.** Rode os arquivos acima, mais `tst_empty_shell` e `tst_high_contrast` como guarda do botão do fixture. Todos verdes.
- [ ] **Passo 5: Commit.** `feat: põe o botão de tema tonal no topo e ajusta a altura do topo`.

### Tarefa 6: Player C — progresso na borda, identidade e controles centrais

**Arquivos:**
- Modificar: `src/ui/qml/TransportSlider.qml` (`edge`, `handleSize`)
- Modificar: `src/ui/qml/TransportBar.qml` (layout do ramo editorial, identidade, `compact`, botões de ±5 s e tempo)
- Modificar: `src/ui/qml/Main.qml:690-775` (o rodapé do Aurora passa a conter só o `TransportBar`; a identidade sai daqui)
- Modificar: `src/ui/qml/TransportText.qml` (`elapsedOfDuration`, `artistAlbum`, `seekInactive`) e `src/ui/qml/NavigationText.qml` (`localAudio` → “Artista desconhecido”), mais o `.ts`
- Teste: `tests/ui/quick/tst_aurora_player.qml`, `tests/ui/quick/tst_keyboard_full_journey.qml`, `tests/ui/quick/tst_metadata_layout.qml` e `tests/ui/quick/tst_aurora_composition.qml`

**Interfaces:**
- Produz:
  - `TransportSlider.edge: bool`, com padrão `false`, e `TransportSlider.handleSize: int`, com padrão 28.
  - `TransportBar.compact: bool`, que vem de `Main`.
  - `TransportBar.formattedCompactTime(ms): string`.
  - Os ids `player-identity`, `player-artwork`, `player-track-title` e `player-track-artist` passam para dentro do `TransportBar`.
  - O id novo `player-time` mostra `elapsedOfDuration.arg(...)`.
  - `elapsed-time` e `duration-time` ficam só no ramo do fixture.
  - `skip-backward` e `skip-forward` ficam com `visible: !editorial`.
  - `player-track-artist` mostra `artistAlbum.arg(artista, álbum)` quando há álbum, e só o artista (ou `localAudio`) quando não há.

- [ ] **Passo 1: Escrever os testes que falham** em `tst_aurora_player.qml`.
  - `test_progressRunsAlongTheTopEdge`, a 1440×900 e 720×520:
    - `progress.edge === true`;
    - `progress.width` é igual a `player-footer.width`;
    - a borda de cima de `progress` coincide com a de `player-footer` (tolerância de 1 px);
    - `progress.height === 24`;
    - o marcador tem 14 px e `color` igual a `accentColor`;
    - o contraste do marcador contra `surfaceColor` é pelo menos 3:1;
    - `player-footer` não tem mais a linha divisória do Aurora.
  - `test_centerControlsFollowPlayerC`:
    - `play-pause` mede 52×52 e é redondo;
    - `player-shuffle` e `player-repeat` medem 40×40, com fundo desenhado de 36;
    - `previous` e `next` medem 40;
    - `skip-backward` e `skip-forward` não ficam visíveis em nenhum tamanho;
    - ←/→ continuam movendo a posição (o mesmo caminho de hoje em `tst_primary_keyboard`), e um clique na barra também.
  - `test_identityShowsCoverTitleArtistAlbumAndTime`:
    - capa de 64 px a 1440×900 e de 48 px a 720×520;
    - `player-track-title` com `font.family === contract.headingFontFamily`;
    - `player-track-artist` igual a `artistAlbum.arg(currentArtist, currentAlbum)`, com os dados da fila sintética do arquivo;
    - `player-time` igual aos tempos do presenter em `m:ss / m:ss` (com a fila de `tst_aurora_navigation`, “0:41 / 3:00”);
    - sem álbum, só o artista; sem artista, “Artista desconhecido”.
  - `test_identityElidesLongMetadataAtMinimumWindow`:
    - a 720×520, com título e artista de 200 caracteres;
    - `player-identity` não sobrepõe `player-shuffle`;
    - `player-track-title.truncated === true`.
  - Ajuste:
    - `test_footerStaysCompactWithoutOverlaps`: tire os botões de ±5 s da lista;
    - `test_aGuestFollowingARoomShowsTheRoomTrackAndItsTime`: passa a ler `player-time`, com “0:01 / 10:00” e “1:02 / 10:00”;
    - em `tst_keyboard_full_journey.qml:93-125`, as verificações de dica e descrição de `skip-backward` e `skip-forward` dão lugar à prova de que ← e → mudam `positionMs` e que a dica de `progress` cita as setas;
    - `tst_metadata_layout.qml`: a geometria do título no rodapé passa a usar o pai novo.
- [ ] **Passo 2: Rodar e ver falhar.** `edge` não existe.
- [ ] **Passo 3: Implementar.**
  - Ramo editorial do `TransportBar`:
    - `GridLayout` de 3 colunas;
    - linha 0: `progress-tooltip-area`, com `columnSpan: 3` e sem margens laterais;
    - linha 1: identidade, `controlsRow` e `player-side-actions`, com as medidas das decisões.
  - O ramo do fixture continua com 1 coluna e as linhas de hoje.
  - Um ramo novo de `TransportSlider` para `edge`. O ramo antigo não muda.
- [ ] **Passo 4: Rodar e ver passar.** Rode estes arquivos:
  - `tst_aurora_player`, `tst_keyboard_full_journey`, `tst_metadata_layout` e `tst_aurora_composition`;
  - `tst_primary_keyboard`, `tst_volume_tooltip`, `tst_listening_room` e `tst_lyrics`;
  - `tst_accessible_interface_tree`;
  - `tst_empty_shell`, como guarda do fixture.

  Todos devem ficar verdes.
- [ ] **Passo 5: Commit.** `feat: monta o player C com progresso na borda e sem os botões de cinco segundos`.

### Tarefa 7: Player C — lado direito e textos do player

**Arquivos:**
- Modificar: `src/ui/qml/TransportBar.qml:354-536` (`player-side-actions`, selo da Fila, volume e selo da sala)
- Modificar: `src/ui/qml/TransportText.qml` (`lyricsAction` → “Letra”, `lyricsUnavailable` → “Esta faixa ainda não tem letra.”), `src/ui/qml/PlaybackAccessibilityText.qml` (`loadingAudio`, `skippedDuringImport`) e o `.ts`
- Teste: `tests/ui/quick/tst_aurora_player.qml`, `tests/ui/quick/tst_listening_room.qml`, `tests/ui/quick/tst_lyrics.qml`, `tests/ui/quick/tst_volume_tooltip.qml`, e os testes que fixam as frases de `PlaybackAccessibilityText` (procure com `rg -n "Carregando áudio|foram puladas" tests`)

**Interfaces:**
- `player-side-actions` vira uma coluna:
  - em cima, `player-room-badge`, só na sala, elidido em até 200 px;
  - embaixo, uma linha com `player-lyrics`, `player-queue`, `mute`, `volume` e `player-volume-percent`.
- `player-lyrics` fica só com ícone (`iconWithText: false`), com o nome acessível e a dica “Letra”.
- `player-queue-count`:
  - 16 px de altura, largura mínima de 16 e raio 8;
  - fundo `selectionColor` e texto `textPrimaryColor` a 10 px, semibold;
  - no canto superior direito, com 4 px de margem em cima e 2 px na direita;
  - em alto contraste, fundo `canvasColor` com contorno de 2 px em `textPrimaryColor`.
- `volume`: 90 px, com `handleSize: 16` no Aurora.
- `player-volume-percent`: 34 px, alinhado à direita.
- Na janela compacta, os dois ficam com `visible: false`. `mute` continua.

- [ ] **Passo 1: Escrever os testes que falham** em `tst_aurora_player.qml`.
  - `test_rightClusterIsIconOnly`:
    - `player-lyrics` com `contentItem.text === ""`, `iconName === "lyrics"` e `accessibleName === "Letra"`;
    - quando desabilitado, `disabledReason === "Esta faixa ainda não tem letra."`;
    - `player-queue-count` com altura 16, `color` igual a `selectionColor`, texto “2” e contraste do texto de pelo menos 4,5:1.
  - `test_volumeBarHidesOnCompactWindow`:
    - a 1440×900, `volume.width === 90` e `player-volume-percent` mostra “52%”;
    - a 720×520, os dois ficam invisíveis, `mute` continua visível e habilitado, e M continua silenciando.
  - `test_roomBadgeFitsTheRightClusterAtMinimumWindow`:
    - a 720×520, convidado com o apelido do host de 40 caracteres;
    - `player-room-badge` visível, dentro de `player-side-actions` e sem sobrepor `player-lyrics`;
    - `player-shuffle.disabledReason` é igual a “O host controla”.
  - Atualize as frases fixadas:
    - em `tst_aurora_player.qml`, `tst_lyrics.qml` e `tst_listening_room.qml`, “Letras” vira “Letra” e o novo `lyricsUnavailable`;
    - em `tst_keyboard_full_journey.qml:119`, o novo `seekInactive`;
    - os dois textos de `PlaybackAccessibilityText` nos testes encontrados pelo `rg`.
  - As frases “Letras” em testes C++ e de extensão que o anexo cita com `~` são de outros catálogos. Confirme com `rg` que não leem `TransportText` e não mexa nelas.
- [ ] **Passo 2: Rodar e ver falhar.** O selo e as frases falham.
- [ ] **Passo 3: Implementar** conforme as interfaces.
- [ ] **Passo 4: Rodar e ver passar.** Rode os arquivos acima, mais `tst_accessibility_announcements` e `tst_primary_keyboard`, e o contrato de UI. Todos verdes.
- [ ] **Passo 5: Commit.** `feat: simplifica o lado direito do player e revisa os textos do player`.

### Tarefa 8: Ordem de Tab, capturas e marco da casca

**Arquivos:**
- Teste: `tests/ui/quick/tst_aurora_composition.qml` e `tests/ui/quick/tst_aurora_navigation.qml`
- Modificar: `docs/ui-tests.md` (ids novos da casca e capturas da fatia 2)

**Interfaces:**
- Consome os ids das tarefas 2 a 7:
  - `profile-chip` e `theme-toggle` no topo;
  - `player-time`, `navigation-count` e `navigation-trailing-icon`.

- [ ] **Passo 1: Escrever o teste da ordem de Tab** em `tst_aurora_navigation.qml`, `test_tabFollowsSidebarHeaderPageAndPlayer`:
  - shell Aurora a 1440×900, com fila sintética e os bindings falsos da tarefa 3;
  - partindo de `now`, Tab passa pelos itens visíveis da lateral na ordem da tarefa 3, depois `preferences`, `profile-chip` e `theme-toggle`;
  - depois entra na página;
  - ao sair da página, passa por `progress`, `player-shuffle`, `previous`, `play-pause`, `next`, `player-repeat`, `player-queue`, `mute` e `volume`, nesta ordem, pulando os desabilitados;
  - Shift+Tab faz o caminho inverso;
  - no máximo 60 Tabs.
- [ ] **Passo 2: Rodar.** Se falhar, corrija a ordem dos filhos no QML, sem `KeyNavigation` novo, e rode de novo até `0 failed`.
- [ ] **Passo 3: Capturas.**
  - Rode `tst_aurora_composition.qml` e `tst_aurora_navigation.qml` com `RC_MUSIC_UI_ARTIFACT_DIR` em `out/product/ui-redesign-captures/casca`.
  - Confira `aurora-redesign-{wide,medium,minimum}-{light,dark}.png` e `aurora-native-{light,dark}.png`:
    - lateral sem Fila, com contagens e chip;
    - topo com o tema tonal;
    - player C sem sobreposição a 720×520.
- [ ] **Passo 4: Marco.**
  - Rode a suíte Quick inteira uma vez, com o script de todos os arquivos.
  - Rode o contrato de UI, `rc_music.ui.qtest` (`ShellUiContractTests`) e `all_qmllint`.
  - Esperado: `0 failed` e nenhum aviso novo de qmllint nos arquivos tocados.
- [ ] **Passo 5: Documentar.** Em `docs/ui-tests.md`, registre:
  - os ids novos da casca;
  - que `theme-toggle` vive no topo no Aurora e na lateral no fixture;
  - as capturas da fatia 2 e a pasta delas.
- [ ] **Passo 6: Commit.** `test: fixa a ordem de Tab da casca e registra as capturas da fatia 2`.

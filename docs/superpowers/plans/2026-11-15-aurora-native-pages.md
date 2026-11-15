# Aurora — páginas nativas e composição musical Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Substituir a composição do player compacto por páginas musicais reais e uma inicial editorial Aurora, preservando todos os recursos existentes e sem produzir instalador intermediário.

**Architecture:** Um único presenter/backend e os bindings atuais permanecem acima da navegação. `PageSurface.qml` hospeda um único formulário: inline como `FocusScope` no produto e, exclusivamente para consumidores legados, em um `Dialog` fino; não manter cópias do formulário nem uma popup escondida no produto. `Main.qml` continua dono da navegação, recuperação, atalhos e do transporte persistente.

**Tech Stack:** C++20, Qt Quick/QML 6.11.2, Qt Multimedia/FFmpeg e CMake/Ninja/MSVC existentes; sem dependência nova.

**Spec:** [Contrato Aurora aprovado e condição posterior do instalador](../specs/2026-11-15-aurora-shell-design.md).

## Global Constraints

- Base desta frente: `af8bef7e6fcea602af8219c7a8bb8800dff90c59`, branch `feat/brave-letras-mp3`, worktree isolada `out/product/brave-letras`.
- Composição de `visual-v1/content/aurora-v3.html` e cores refinadas de `visual-v2`; não trocar a stack nem incorporar o HTML ao produto.
- Sem instalador até terminar completamente o redesenho; sem instalação, Registro, UAC, push, merge, app/músicas/configuração/clipboard pessoais.
- Preservar Sistema/Claro/Escuro, HC, movimento reduzido, mínimo 720×520, alvos mínimos40/preferidos44px e contratos/fixtures históricos M0.
- Não recriar áudio por navegação/tema. Conservar faixa, posição, volume, IDs string, duplicatas, seleção distinta de reprodução e todos os consentimentos/revisões Saved.
- Esta frente migra capacidades existentes. Biblioteca permanente/provider online conservam desenho e gates próprios; não apresentar fila como biblioteca nem resultados fictícios. Esta frente não fecha as onze fases nem libera empacotamento.
- Validação proporcional: focados durante a implementação, uma regressão agrupada da UI pelo risco transversal de composição/foco/atalhos. Não executar suíte global de core/rede/pacote sem necessidade causal.

## Review Focus

- Navegar durante reprodução ou final-save recovery deve preservar o serviço/estado e manter ações finais acessíveis. Task1 testa comandos, identidade e recuperação.
- Resposta tardia de picker/consentimento após sair ou trocar owner não pode gravar, adquirir nem restaurar foco na página antiga. Task1 conserva testes de owner/token e acrescenta troca de página.
- Teclado em listas/toggles/editores deve pertencer à página; lateral e player não podem ficar presos num ciclo modal. Task1 testa Tab e Espaço/setas com foco explícito.
- Fila real vazia, duplicada, indisponível ou sem metadados deve ser apresentada honestamente, sem coleção/arte atribuída incorretamente. Task2 testa contagens reativas, ordem e identidade.
- Geometria mínima, títulos longos e HC não podem esconder ações nem roubar foco durante playback. Task2 e Task3 verificam geometria/renderização e consumidores existentes.

---

### Task 1: Páginas reais com formulários únicos e navegação segura

**Files:**
- Create: `src/ui/qml/PageSurface.qml`.
- Modify: `src/ui/qml/Main.qml`, `SideNavigation.qml`, `PlaylistsPanel.qml`, `DownloadsPanel.qml`, `PreferencesPanel.qml`, `NavigationText.qml`.
- Modify: `cmake/RcMusicUi.cmake`.
- Test: `tests/ui/quick/tst_aurora_pages.qml`; consumidores existentes de playlists/downloads/preferences/foco/atalhos.

**Interfaces:**
- Consumes: `Main` properties `presenter`, `playlists`, `downloads`, `lyrics`; APIs atuais dos três painéis e bindings, sem mudar C++.
- Produces: `PageSurface` é `FocusScope` com `contentItem/background/padding`, `previousFocusItem/nextFocusItem`, `bool embedded`, `readonly bool opened`, `bool modal`, `bool dim`, `int closePolicy`, `int popupType`, `Transition enter/exit`, sinais `aboutToShow`, `aboutToHide`, `surfaceOpened`, `closed` e métodos `open()/close()`; o viewport inline ou o Dialog legado é o único dono da geometria do formulário.
- Produces: `Main.currentPage` readonly string (`home`, `queue`, `playlists`, `downloads`, `preferences`), derivada da única superfície ativa; métodos públicos existentes continuam. Produto usa inline; fixtures congeladas continuam com host legado. Cada painel expõe `blockingUiOpen` para seus transientes, que permanecem modais.
- Produces: mesma interface de seleções/owners/consentimentos e objectNames dos formulários, sem duplicação, incluindo `focusSaveRecovery`, `openFrom`, `focusFirst`, `selectedEntryId` e `section`.

- [x] **Step 1: Escrever regressões `test_pagesShareTheWorkspaceWithoutOwningThePlayer`, `test_pageNavigationPreservesPlaybackAndRevokesTransientOwners`, `test_inlinePageDoesNotTrapTabAndEditorsOwnTheirKeys`** em `tst_aurora_pages.qml`. Usar presenter local real do harness e bindings de teste completos; afirmar página inline não modal e contida no workspace, lateral/player utilizáveis, apenas um destino visível, mesma instância de transporte/ocorrência/posição/volume e contadores de áudio inalterados. Sair com confirmação aberta deve fechá-la, sem ação confirmada ou foco tardio sobre o destino novo.
- [x] **Step 2: Executar o arquivo contra o produto atual** com ambiente real do registro `rc_music.ui.quick`, capturando Red comportamental (páginas ainda modais/ausência de currentPage), não erro de import/compilação.
- [x] **Step 3: Implementar `PageSurface` e migrar os três hosts**. Preservar o corpo e a lógica de cada formulário; renomear handlers de abertura para `onSurfaceOpened`. Inline não cria o Dialog legado; fechar emite invalidações existentes antes de ocultar. Remover ciclos Tab exclusivos do modal somente no modo inline; transientes continuam iguais. Distinguir bloqueio modal de foco dentro de formulário em Main e proteger restaurações tardias contra a página selecionada.
- [x] **Step 4: Executar novo arquivo e consumidores** `tst_playlists.qml`, `tst_downloads.qml`, `tst_preferences.qml`, `tst_lifecycle_transitions.qml`, `tst_primary_keyboard.qml`, `tst_audio_file_selection.qml`; atualizar apenas asserts da modalidade deliberadamente alterada, mantendo todos os asserts de dados/ações/consentimentos. Esperado: seleção positiva, zero falhas/warnings.
- [x] **Step 5: Revisar diff e commitar** `refactor: integrar as áreas musicais em páginas nativas` com contrato/documentação relacionada; não incluir outputs. Commit `26e2f0e`.

### Task 2: Composição editorial Aurora e player compacto persistente

**Files:**
- Create: `src/ui/qml/TrackArtwork.qml`, `NavigationButton.qml`, `WorkspaceHeader.qml`, `QueuePreview.qml`.
- Modify: `src/ui/qml/Main.qml`, `SideNavigation.qml`, `NavigationText.qml`, `NowPlayingState.qml`, `EmptyState.qml`, `TransportBar.qml`, `AccessibleTransportButton.qml`; reutilizar `TransportText.qml`/`QueueText.qml` existentes.
- Modify: `cmake/RcMusicUi.cmake`.
- Test: `tests/ui/quick/tst_aurora_navigation.qml`, `tst_aurora_pages.qml`, `tst_metadata_layout.qml`, `tst_lyrics.qml` e novo `tst_aurora_composition.qml`.

**Interfaces:**
- Consumes: Task1 `Main.currentPage` e páginas inline; dados atuais `currentTitle/Artist/Album/ArtworkSource`, `queueModel` com roles `occurrenceId,title,fileName,artist,album,durationMs,available`, estados do presenter e `playlists.playlists` `{id,name,count}`.
- Produces: `TrackArtwork` com `contract`, `url source`, `bool frozen`; placeholder neutro Aurora só em produção, arte real apenas quando corresponde à identidade atual.
- Produces: `NavigationButton` mantém API acessível do botão existente e acrescenta `glyph/compact`, ícone+texto alinhados à esquerda e seleção por superfície, com HC/foco preservados.
- Produces: `QueuePreview` lista horizontal virtualizada de ocorrências reais, `model` e `contract`, `currentOccurrenceId/currentArtworkSource` e sinal `playRequested(string occurrenceId)`; chamada de áudio somente por ação explícita de reprodução, nunca por renderização.
- Produces: cabeçalho nativo com contexto e tema, início rolável com destaque horizontal e prateleira da fila; `TransportBar` conserva API e lógica de seek/volume, reorganizando controles/tempos/progresso/volume. Rodapé com identidade fixa, capa e artista.

- [x] **Step 1: Escrever `test_editorialHomeShowsRealQueueAndKeepsIdentity`, `test_headerHeroAndFooterUseSeparateResponsiveRegions`, `test_queueCardsKeepExactOccurrenceIdentity`**. Em 1440×900 exigir cabeçalho, hero horizontal e fila real na mesma inicial; em 720×520 exigir conteúdo rolável e todos os controles alcançáveis ≥40px. Atualizar fila com duplicatas/remoção e afirmar contagem/ordem por ID, sem comandos no render/tema/navegação. Testar Unicode/arte ausente e não duplicar a arte atual em outro cartão.
- [x] **Step 2: Executar Red focado antes das mudanças** pelo registro Quick; ausência de cabeçalho/hero/prateleira deve falhar como comportamento ausente.
- [x] **Step 3: Implementar composição**. Lateral224px ampla/196px média/68px compacta, cabeçalho78/66px, margens38/23px, títulos Georgia36px; hero capa204/130px e texto à esquerda. Exibir somente prateleiras sustentadas por dados reais. Rodapé próximo de112px em ampla e altura implícita maior em compacta, sem reduzir alvos. Preservar render/layout congelado quando `usesFrozenFixture`.
- [x] **Step 4: Executar novos testes e consumidores de navegação/transporte/metadata/HC/lyrics**; capturar claro/escuro/compacto com o próprio Qt Quick e inspecionar as imagens. Esperado: nenhum comando extra de áudio, controles contidos e contraste válido nos fundos reais.
- [ ] **Step 5: Revisar diff e commitar** `feat: aproximar a composição nativa do Aurora`.

### Task 3: Integração visual nativa, evidências e revisão independente

**Files:**
- Modify: plano atual, `docs/project-status.md`, `docs/delivery/aurora-native-pages-20261115.md`.
- Evidence: workspace ignorado próprio deste plano; não sobrescrever recibos/capturas/setups anteriores.

**Interfaces:**
- Consumes: fontes e testes das Tasks1–2; build ON existente `out/product/dl-settings-tests` e build OFF próprio já existente, ambos da mesma worktree; tokens e fixtures originais intactos.
- Produces: capturas Qt e recibos identificados por fontes, comando/ambiente/exit/contagem, revisão independente do snapshot, documentação do alcance e dos gates ainda abertos. Não produz runtime/setup.

- [ ] **Step 1: Justificar e executar uma regressão agrupada de UI**: navegação, transientes e teclado atravessam todas as páginas; focados isolados não cobrem todas as combinações. Build `rc_music_ui_quick_tests`, `rc_music_ui_contract_tests`, `m0_qt_shell`, shells de processo e lints de ambos os módulos; seleção CTest `rc_music.ui.quick`, `rc_music.ui.contract`, `rc_music.ui.build-boundary`, `rc_music.process.combined-startup`, `rc_music.process.tray-lifecycle`. Esperado: seleção positiva e Green, logs completos preservados.
- [ ] **Step 2: Formato dirigido, links/documentação e diff**. Conferir que assets/fixtures/stores/modelos não mudaram e que não foi gerado instalador. Não confundir capturas offscreen com NVDA/DPI físico ou aceite humano.
- [ ] **Step 3: Revisão independente somente-leitura** do delta desde af8bef7 com manifesto de fontes/evidências. Corrigir Critical/Important com Red→Green e consumidores afetados, sem repetir suíte já compatível; preservar veredicto original e registrar minors/decisões.
- [ ] **Step 4: Atualizar status, registrar evidências/limites e commitar** `docs: registrar a migração das páginas Aurora`. Informar checks executados e pendentes separadamente. Não declarar todo o redesenho concluído ou emitir instalador se biblioteca/provider, paridade ou aceite visual ainda estiverem abertos.

## Comandos de execução e evidência

Usar CMake/CTest locais e o ambiente do registro real, não inventar runner. O capturador no workspace deste plano chama `cmake --build out/product/dl-settings-tests --parallel 4 --target <alvos>` e o binário registrado de Quick com `-input <arquivo>` e `-import <build>/tests/ui/qml`, reproduzindo `ENVIRONMENT` e `ENVIRONMENT_MODIFICATION`. Guardar stdout/stderr integrais, seleção positiva, hashes de entradas e exit code por tentativa. Novos QML exigem rebuild antes de executar; testes externos QML são lidos pelo runner em runtime. Reutilizar evidencia anterior somente para entradas inalteradas, nunca para o novo desenho.

## Self-review

Cobertura: paridade de formulários/consentimentos e navegação Task1; composição/placeholder/transporte e extremos Task2; integração/revisão/documentação Task3. Interfaces entre tarefas mantêm Main/presenter autoritativos e contentItem único. Gates de biblioteca/provider/instalação não foram promovidos a aceites visuais. A ordem torna cada entrega utilizável e revisável sem empacotamento.

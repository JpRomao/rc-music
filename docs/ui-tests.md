# UI e presenter — variante exclusiva de testes

**Estado:** cobertura histórica promovida ao build raiz; M1 e release permanecem abertos.
**Data:** 2026-09-06.

## Downloads opcionais compartilhados

[DownloadsPanel](../src/ui/qml/DownloadsPanel.qml) e [DownloadApplicationBinding](../src/presentation/DownloadApplicationBinding.hpp) são comuns a Lite/Completa e não dependem de navegador. O default nullable de Main não ativa botão, serviço ou executor. Quick exercita o painel real com proxy/ListModel só no harness: progresso, capacidades/IDs, privacidade/PlainText, confirmação revogável, recusa sem saída, escolha aceita com nova intenção de saída, foco/Tab/Escape, atalhos e temas em 720×520. Cada caso possui cenário próprio para que uma saída efetiva não congele o presenter compartilhado dos testes seguintes; não existe reset de lifecycle no produto.

O teste do cartão de downloads reproduziu o indicador ausente, End parado na primeira tarefa e o foco acessível mantido no cartão após Tab. O Green passou em 47/47 Qt Quick, incluindo a última de 40 tarefas, retorno com Shift+Tab e envio do ID correto ao acionar o botão. Build Release focado e qmlformat passaram. O qmllint apresentou o mesmo aviso do baseline. Logs em `out/product/test-runs/downloads-keyboard-20261111/`. [Controles no manual](user-guide.md#downloads)

Qt Test compõe binding/model/controller/serviço reais com IO/execução falsos e prova Saved separado de parada física, retry/abandono explícitos, exceções/clock/reentrada e gate que não aprova owner expirado. A [main instrumentada](process-tests.md#composição-instrumentada-dos-downloads) acrescenta stores/workers reais e áudio sintético autenticado com publicação falsa, sem ingresso remoto de produção ou engine. As evidências e lacunas ficam no [checkpoint central](development-plan.md); nenhuma suíte UI comprova extensão instalada, navegação da Completa, download/conversão/sandbox ou release.

### Validação de candidatos com worker real

`DownloadRecoveryValidationCompositionTests` liga Main/QML, binding/model/controller e os workers Windows reais de checkpoint, revisão e validação. Só é registrado com Windows, rede/persistência opcionais e paths explícitos de FFmpeg/ffprobe confiáveis. Isso não ativa os serviços no executável de produção. O ensaio usa o MP3 sintético autenticado do corpus em TEMP Unicode, sem páginas, mídia pessoal ou rede.

As duas linhas Published/Prepared sem Saved percorrem os cliques Verificar arquivos → Validar MP3 → Liberar → Validar novamente → sair. Conferem texto literal sem paths, ausência de importação/seleção/playback, fila vazia e journal/final inalterados. Um HANDLE de escrita é recusado enquanto a validação retém a prova e volta a ser permitido depois de Released ou saída fisicamente drenada. Playback, metadata, execução de rede e importador são fakes observados; o probe e a decodificação MP3, workspace, journals, workers e UI são reais. Não é download da internet ou instalação da extensão.

Com o cache de testes combinado já configurado:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_ui_download_recovery_validation_tests --parallel 4
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music\.ui\.download-recovery-validation$' --output-on-failure --no-tests=error
```

O ensaio inicial passou **4/4 casos Qt** (duas linhas mais init/cleanup), sem skips. Binding tem regressão separada para retirar a aprovação antes do shutdown: uma falha no checkpoint final pode reverter a intenção de saída, mas não restaurar uma prova já liberada nem projetar sucesso tardio. Resultados finais e limites ficam no [checkpoint](development-plan.md#17-próxima-sequência-segura); o teste offscreen não comprova integração instalada, Brave, áudio audível, acessibilidade nativa ou release.

### Importação recuperada com sessão real

O mesmo alvo de composição agora preserva as duas linhas anteriores e acrescenta quatro Published/Prepared × Saved/falha real AfterPrimaryCommit. Usa `PlayerDownloadImport` em Session e `SingleWorkerSessionWriter`/`QtJsonWindowsSessionStore` reais. O store escreve o JSON em TEMP privado e retém o ACK por um gate no próprio worker; a UI continua respondendo e não acessa o filesystem. Enquanto isso, fila/recibo existem uma única vez, mas o job permanece Interrupted, release é recusado e o arquivo segue protegido.

Na saída durante Pending, o worker nativo continua vivo até a confirmação da sessão. Na falha póscommit, bytes legíveis não viram Imported: novo consentimento, preparação nativa e retry Session confirmados concluem sem outro `rowsInserted`. O restart com restauração desligada reconhece o recibo em histórico saudável, sem nova importação, metadata ou reprodução. As quatro linhas e as duas anteriores passaram: **8/8 casos Qt**, incluindo init/cleanup.

O primeiro ensaio teve falha no segundo ciclo de validação da linha anterior e timeout durante desmontagem. O helper de mouse passou a usar `QQuickTest::qWaitForPolish`, comprovar exatamente um `clicked()` e preservar diagnóstico de hit miss; fallback abre o gate e observa os workers parados antes da destruição. Não houve troca para teclado, aumento de timeout ou retirada de assertions. O worker real separado também passou quatro ciclos consecutivos por Published/Prepared; isso não prova causa exclusiva de layout para a falha inicial.

Quick passou de **258 passagens/8 falhas** para **266/266**. Consentimento exige token atual e separado de descarte/saída, abre com Cancelar focado, revoga em owner/job/capacidade/token obsoletos e mantém navegação/layout das sete ações em 720×520. Binding passou **80/80**, incluindo a guarda final de fechamento após consulta reentrante de capacidade. Esses checks não habilitam serviços de produção, extensão/Brave ou navegação interna; os demais gates permanecem no [plano](development-plan.md#17-próxima-sequência-segura).

## Atalhos principais e modos de fila

O [teste de navegação na fila](../tests/ui/quick/tst_local_queue_menu_context.qml) reproduziu a remoção da faixa que acabara de começar enquanto outra ocorrência estava selecionada. Com a lista em foco, o avanço da reprodução mantém a ocorrência selecionada como alvo de `Delete`. `Home` e `End` revelam os extremos de uma lista rolável e `Enter` toca a ocorrência selecionada. O build incremental Release e os 396 casos Qt Quick passaram. Logs em `out/product/test-runs/queue-cursor-20261111/`.

Pedido funcional confirmado em 2026-09-06 (`FR-037`/`FR-019`): Espaço controla reprodução inclusive depois de clicar em Mudo ou outro botão; setas usam saltos configurados/volume e Shift+setas navega. F1 alterna modo aleatório persistido e F2 repetição da fila, com estado textual e acessível. Editor, menus e modais mantêm suas teclas; Enter ativa controles e Ctrl+↑/↓ seleciona linhas. O [manual](user-guide.md) contém o contrato completo, incluindo seguir a ordem exibida ao desligar aleatório.

Os testes usam o presenter real com portas falsas, relógios controlados e IDs de ocorrências, sem arquivos pessoais ou dispositivo. Cobrem rodada aleatória sem reposição, Next/EOF, prefixo já percorrido, duplicatas, metadata/modelo preservados, rotação para o fim, faixa única, indisponíveis, remoções, carga recusada, autosave e eventos antigos. A regressão de destruição durante `rowsMoved` reproduziu acesso inválido no reindex posterior do modelo; a guarda local evita continuar após destruição. Desligar F1 num observador também invalida a permutação pendente.

TDD observado: Red inicial do teclado com 18 falhas comportamentais; Red de presenter com 16 falhas nas ações ainda inertes, seguido de quatro no modo F1 confirmado; Red de desligamento reentrante com uma falha. Controller e codec tiveram Reds próprios para cada nova preferência. Green focado final: presenter **30/30**, teclado **29/29**; Quick integral **234/234**, sem falhas/skips. Os oráculos anteriores de Espaço/setas foram alinhados ao pedido explícito; a contagem de `commitSeekPreview` não é usada para a rota global de salto, que verifica comando/alvo/posição e mantém as regressões de mouse/arraste.

Fechamento proporcional: `test -Diagnostics` compilou Release ON e executou **300 registros**, com **299 passagens/1 falha** no classificador técnico de textos (`sequences` confundido com texto visível). Incluiu presenter integral **292/292** e Quick **234/234**. A regressão específica do classificador teve Red **6 passagens/2 falhas**; após reconhecer `sequence` e `sequences` sem liberar textos visíveis, o único alvo reexecutado passou **1/1 CTest / 35/35 internos**. Não se declara uma segunda execução integral 300/300. O recibo original de falha permanece em `out/product/validation-runs/run-1ab776c434cd45e88fc722f0db3fe9d6`; os quatro skips internos de symlink por política do host permanecem explícitos.

Build Release OFF e `all_qmllint` de produção passaram. Formato **172 C++/57 QML**, documentos **61 Markdown/83 requisitos/17 checks/76 etapas**, rastreabilidade **83 IDs** e diff foram validados. Essa prova offscreen não equivale a teclas físicas/SMTC, NVDA, pacote instalado ou Linux. Não houve benchmark, mudança de dependência, instalação, alteração de Registry ou acesso aos áudios pessoais.

A raiz agora executa os testes de presenter, contrato visual, repositório/modelo de fixture e Qt Quick, sem habilitar cenários na aplicação real. As fontes de suporte e testes foram movidas para [tests/ui](../tests/ui), e o consumidor histórico aponta para os mesmos arquivos. As assertions foram preservadas; o bootstrap Quick seleciona somente o nome do recurso de tradução correspondente à variante compilada.

## Uma implementação, duas compilações isoladas

A fábrica [rc_music_add_ui](../cmake/RcMusicUi.cmake) recebe alvo, suporte e diretório QML. Ela compila as mesmas fontes de apresentação, os mesmos componentes QML, tradução e assets da raiz para duas composições:

| Composição | UI/plugin | Suporte e recursos de cenário |
|---|---|---|
| Aplicação real | `m0_qt_shell_ui` / `m0_qt_shell_uiplugin` | Ausentes, inclusive com testes ON |
| Testes UI | `rc_music_test_ui` / `rc_music_test_uiplugin` | `rc_music_ui_scenario_support`; JSONs somente nos consumidores de teste |

[tests/ui/CMakeLists.txt](../tests/ui/CMakeLists.txt) só é incluído com desktop e `BUILD_TESTING=ON`. O suporte reúne `FixtureRepository`, `FixtureDecoder`, `QueueListModel`, controles auxiliares de cenário/teste e o backend determinístico já existente. Não injeta esses componentes no core ou na UI de produção.

`RC_MUSIC_ENABLE_FIXTURE_SCENARIOS` altera o layout C++ de `PlayerPresenter`. Por isso, a macro é pública na variante de testes e acompanha seus consumidores, inclusive código gerado por moc/registro QML. Nenhum executável pode ligar simultaneamente as duas variantes ou consumir o cabeçalho com uma configuração diferente da biblioteca.

Ambas conservam a URI `MusicPlayer.M0`, mas têm alvos, plugins e diretórios de geração separados. O Quick recebe explicitamente o import root `<build>/tests/ui/qml`; a aplicação usa `<build>/qml`. Criar os targets em `tests/ui` também mantém o scanner QML fora da raiz inteira e dos caches. Isso é recompilação controlada de fonte única, não uma segunda implementação da UI.

## Fixtures privadas preservadas

[tests/ui/fixtures](../tests/ui/fixtures) contém somente três cópias textuais byte-idênticas, totalizando **591.252 bytes**:

| Arquivo | Bytes | SHA-256 |
|---|---:|---|
| `fixture.v1.json` | 8.615 | `03fd2a762758c524e226296a63575df5e3972a9a0ea09c1109bb641089a58464` |
| `queue.v1.json` | 582.271 | `a3c2e890f08885af1de319ed330f71a4b657aeaecd7bf7c640dc633095d55ef3` |
| `LICENSE.assets.txt` | 366 | `c77c86115e7da5fff31ab28c903e78a8a12f65f9b2792f7e0d137c458fae5b94` |

[RcMusicUiFixtures.cmake](../cmake/RcMusicUiFixtures.cmake) fixa esses valores e valida tamanho/hash no configure de testes, recusando ausência, diretório ou divergência. Não lê o manifesto ou protocolo comparativo. Os originais e seus registros autenticados permanecem intactos; não é necessário configurar o experimento para executar a suite raiz.

A [licença preservada](../tests/ui/fixtures/LICENSE.assets.txt), `LicenseRef-MusicPlayer-M0-Private`, autoriza uso, cópia e modificação apenas dentro deste projeto privado local e de seus experimentos; não autoriza redistribuição pública. Os dois SVGs continuam em `assets`, sem novas cópias: o contrato UI mantém suas assertions de tamanho/hash contra os recursos efetivamente embutidos.

Os JSONs são ligados por `m0_qt_fixture_data` somente aos testes que precisam dos recursos. Presenter/repositório também recebem caminhos explícitos para as cópias locais. A tradução pt-BR e os assets usam as mesmas fontes de produção; o Quick inicializa a tradução da sua própria variante.

A [promoção posterior das jornadas de processo](process-tests.md), com gates de build/testes validados, reutiliza essa mesma UI de teste em dois shells compilados da main compartilhada. Ambos preservam a ABI de cenário; somente `rc_music_fixture_test_shell` incorpora os JSONs, enquanto `rc_music_test_shell` usa áudio combinado sem esses recursos. Não é uma terceira implementação da UI nem instrumentação do executável real.

## Comandos e fronteira verificável

Os [comandos de desenvolvimento existentes](development-commands.md) já cobrem a promoção; não há novo wrapper:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 test
pwsh -NoProfile -File scripts/develop.ps1 validate
```

Esses comandos usam `.tools/windows.psd1` por padrão; `-ToolchainFile tools/windows.example.psd1` seleciona o exemplo local. Para um check focado depois do configure/build ON, use `ctest --test-dir out/product/desktop-tests --output-on-failure --no-tests=error -R '^rc_music\.ui\.'`.

O novo teste [rc_music.ui.build-boundary](../tests/cmake/UiBuildBoundaryTests.cmake) examina os comandos alcançáveis dos executáveis real e Quick. Recusa suporte, macro, JSONs ou UI de cenário na aplicação; recusa a UI/plugin de produção no Quick; e exige uma compilação coerente do presenter, moc e registro de tipos para cada variante. Também verifica a separação dos diretórios usados pelos comandos RCC. Não infere independência pela ausência de um arquivo antigo: o conteúdo dos QRCs alcançáveis permanece uma revisão separada.

## Evidência do incremento

- Baseline histórico de presenter, contrato UI e Quick: **3/3** antes da promoção, em **34,70 s**. A extração mecânica não muda o comportamento de produção nem usa um Red artificial.
- Equivalência das fontes movidas: **34 arquivos byte-idênticos**; o 35º, `quick_main.cpp`, altera somente a seleção condicional do símbolo de tradução. As assertions C++/QML não mudaram. A fábrica preserva as fontes e opções do bloco original após parametrização de nomes/diretórios.
- A guarda de fronteira teve Red porque `rc_music_ui_quick_tests` ainda não existia, mantendo as verificações de produção aprovadas; Green com a composição isolada. A regressão do formatter teve Red **17 passagens/1 falha**, pela ausência de Quick no inventário, seguido de Green **18/18**.
- `validate` raiz passou **276/276** registros CTest em **88,93 s**, incluindo as quatro suites UI e o novo check de fronteira, além da cobertura raiz já existente de core, adapters e áudio.
- Contagens internas lidas no `LastTest.log` do build `out/product/m1-desktop-release-on-20260906`: repositório/modelo **41/41**, presenter **265/265**, contrato UI **29/29** e Quick **187/187**; todos sem falhas ou skips. Essas contagens QtTest não são somadas ao total de registros CTest.
- Formato real passou em **154 C++/52 QML**: o inventário QML agora inclui os 24 arquivos Quick promovidos, além dos 28 componentes da aplicação. Testes do runner **33/33** e formatter **18/18** passaram.
- Os dois alvos `qmllint`, de produção e da variante de testes, passaram na validação raiz.
- Produção OFF reconfigurada/compilada, **zero testes**, sem Qt Test/QuickTest no cache ou suporte UI de cenário no grafo. Sua aplicação conserva **246 comandos**, com as fontes reais e sem referências históricas/de teste.
- Revisão dos QRCs realmente alcançados no build ON: aplicação com quatro QRCs sob `src/.qt/rcc` (28 QML, dois SVGs, tradução e qmldir reais), sem JSON; Quick com cinco QRCs sob `tests/ui/.qt/rcc`, incluindo somente as duas cópias JSON de teste no recurso adicional. A API QML gerada de produção e teste tem SHA-256 idêntico: `e8534f10016ff84190bfde8da909d3c692d96794328db1d05750cabdb20887df`, preservando o baseline. Revisão independente sem achados P0–P2 remanescentes.
- Consumidor histórico recompilado com as fontes movidas: seleção **8/8** em **36,18 s**, cobrindo as quatro suites UI, bindings de diagnóstico/mídia/energia e startup combinado. Mantém seus inputs originais e o comportamento histórico; não é execução completa do experimento.
- Check final aprovado: formato **154 C++/52 QML**, runner **33/33**, formatter **18/18**, documentos **53 Markdown/83 requisitos/17 checks/76 etapas** e diff sem erros. O índice Git conserva os 34 movimentos em 100% de similaridade e somente o ajuste de tradução no Quick; nenhum processo de app/build/teste do workspace permaneceu ativo.

## Peças Aurora

Oito arquivos de `tests/ui/quick` cobrem as peças `src/ui/qml/Aurora*.qml`: `tst_aurora_buttons`, `tst_aurora_fields`, `tst_aurora_choices`, `tst_aurora_badges`, `tst_aurora_menus`, `tst_aurora_dialogs`, `tst_aurora_surfaces` e `tst_aurora_pieces_visual`. Os demais `tst_aurora_*` (`composition`, `navigation`, `pages`, `player` e `shell`) cobrem as telas Aurora anteriores e não são testes de peça. Os testes de peça rodam com `QT_FATAL_WARNINGS=1` e nenhum toca som.

- `tst_aurora_buttons`: cores por tipo de botão, contraste do texto com hover e pressionado, alvo de 40 px, ativação por teclado, anel de foco só com foco e com folga de 2 px, estado desabilitado (inclusive em alto contraste e sem ação do leitor de tela), contorno tonal em alto contraste, rótulo longo cortado mantendo o nome acessível, descrição acessível, troca de tema, ícone no botão; botão de ícone com nome, descrição na dica, dica também pelo teclado, marca de ligado (inclusive desabilitado em alto contraste), alto contraste e teclas em botão marcável.
- `tst_aurora_fields`: campo com linha em vez de borda, erro abaixo do campo colorindo a linha, espaço do ícone inicial, contorno em alto contraste, controlId no campo interno, anel de foco e parada de Tab; caixa de seleção abrindo e escolhendo por teclado, marcando a opção atual, com anel de foco por qualquer motivo, anel interno na opção do teclado, fade conforme o contrato e descrição acessível.
- `tst_aurora_choices`: segmentado (uma parada de tabulação, setas, marca da opção, estado externo por binding, desabilitado em alto contraste), interruptor (estados, movimento conforme o contrato, desabilitado ligado em alto contraste, descrição acessível), stepper (passos, edição, indicadores, centro em repouso sobre a superfície, anel em volta do centro), caixinha (estados, desabilitada marcada em alto contraste, Enter e Return alternando) e anel com folga nos controles marcados; nenhum controle desabilitado obedece à ação do leitor de tela.
- `tst_aurora_badges`: tons do selo, selo em alto contraste, avatar redondo, anel de escolhido e disco de reserva sem imagem (claro, escuro e alto contraste).
- `tst_aurora_menus`: superfície do menu, itens de 40 px navegáveis, controlId do item, item de perigo em `errorColor`, texto de atalho legível, sem sombra em alto contraste, Esc devolvendo o foco a quem abriu, anel interno no item atual do teclado e cor de hover sem anel no mouse, largura pelo item mais largo com teto na janela e movimento conforme o contrato.
- `tst_aurora_dialogs`: controlId do diálogo, véu sobre a janela inteira, Esc fechando e devolvendo o foco, botão de fechar nomeado, confirmação (abre em cancelar, consequência com a fonte do corpo e lida em Cancelar e Confirmar, Cancelar e Esc não confirmam), fonte do título, título longo quebrando linha, troca de tema com o diálogo aberto, movimento conforme o contrato, janela mínima, rolagem só quando o conteúdo é alto e até o controle com foco, anel de foco do corpo sem recorte, foco inicial no primeiro controle do corpo, ordem de Tab cabeçalho, corpo e rodapé, e superfície em alto contraste.
- `tst_aurora_surfaces`: controlId na raiz do cabeçalho de gaveta, do aviso e do estado vazio, cabeçalho de gaveta, tons do aviso, alto contraste, ações e nome acessível do aviso, sombra do cartão flutuante conforme o contraste, mensagem de retorno oculta quando vazia e estado vazio (ações e alto contraste).
- `tst_aurora_pieces_visual`: contraste dos 30 pares de cores usados pelas peças nos temas claro e escuro (mínimos 4,5:1 ou 3:1 conforme o par) e galeria com as peças em claro, escuro e alto contraste dentro de `ColumnLayout`/`RowLayout`, com um Principal e um campo focados. A galeria prova que tudo renderiza sem aviso de layout ou de binding nos três temas; não afirma cada peça individualmente. Com `RC_MUSIC_UI_ARTIFACT_DIR` definido, grava `aurora-pecas-claro.png`, `aurora-pecas-escuro.png` e `aurora-pecas-alto-contraste.png` para o aceite visual, mais `aurora-pecas-segmentada-foco-claro.png` e `aurora-pecas-segmentada-foco-escuro.png`, só com a chave segmentada focada; as imagens não são versionadas.

### Convenções das peças

Valem para as telas que usarem as peças nos planos seguintes:

- **controlId:** toda peça que o recebe o põe no `objectName` e em `Accessible.id` da raiz. No campo de texto, os dois ficam no `TextField` interno, exposto como `input`. O diálogo é um Popup, que não leva `Accessible`, e o cabeçalho de gaveta e o estado vazio não são acessíveis: nesses três, só o `objectName`. Os filhos usam o controlId com sufixo, como `-close` e `-primary`.
- **label e text:** `label` é só o nome acessível, quando o texto visível fica fora da peça (interruptor, segmentado, caixa de seleção, stepper e campo). `text` é o texto que aparece e também dá o nome.
- **description:** botão, botão de ícone, interruptor e caixa de seleção levam `description` para `Accessible.description`, por exemplo o motivo de um desabilitado. No botão de ícone, a descrição entra também na dica.
- **Chave segmentada:** quem espelha um estado externo guia `currentIndex` com um elemento `Binding` e grava a escolha de volta em `onActivated`, porque a escolha atribui `currentIndex` e quebraria uma ligação simples.
- **Avatar:** `backgroundColor` é a cor do fundo de quem hospeda o avatar; o anel de escolhido usa essa cor como folga.
- **Anel de foco:** 3 px em `focusColor`, separado do controle por uma folga de 2 px. Em menus e listas, onde os itens ficam colados, o anel é interno. O corpo do diálogo tem recuo para o anel caber dentro da área que rola.

Resultado do marco do fim das peças (`develop.ps1 validate`): as etapas de formato, documentos, build e lint passaram, e o CTest teve 667 de 669 aprovados. As duas falhas: `rc_music.ui.quick` estoura o prazo de 90 s (a suíte leva cerca de 150 s ou mais nesta máquina), o que já ocorria antes da fatia; e `rc_music.ui.presenter` (`roomHostLeadsEachStartOnceOverQueuedConfirmations`) falhou uma vez sob carga e passa sozinho (336/336 no binário inteiro). O Quick rodado direto, sem o limite do CTest, deu 695 aprovados e 6 falhas, todas linhas já falhando de `AuroraComposition::test_headerHeroAndFooterUseSeparateResponsiveRegions`, que exige `RC_MUSIC_UI_ARTIFACT_DIR`. `LocalRuntime.Tests.ps1` ficou pendente: precisa de um runtime montado. `tst_aurora_pieces_visual` passou 7/7.

As três condições foram corrigidas em 2026-11-22:

- `rc_music.ui.quick` tem prazo de 360 s. A suíte inteira levou 108 s com a máquina livre, cerca de 160 s dentro do `validate` e 309 s com a máquina ocupada. O prazo só pega um processo travado; cada `tryCompare` tem o próprio limite.
- `test_headerHeroAndFooterUseSeparateResponsiveRegions` confere o layout e o tamanho da captura sempre, e só grava `aurora-redesign-<linha>.png` com `RC_MUSIC_UI_ARTIFACT_DIR` definido.
- `roomHostLeadsEachStartOnceOverQueuedConfirmations` espera o player da sala receber a confirmação do backend, que chega pela fila de eventos, antes de avançar o relógio da sala. Se o timer de 300 ms soltava a faixa no fim de uma espera, a confirmação ficava na fila, e a sala via o player parado na posição da âncora depois dela. Então reancorava duas vezes por desvio. Uma sonda que forçava essa ordem reproduziu a falha (2 estados a mais) e passou com a correção.

## Casca Aurora (fatia 2)

Ids que a casca nova expõe aos testes Quick:

- `profile-chip` (com `profile-chip-nickname`, `profile-chip-caption` e `profile-chip-avatar`): chip do perfil no pé da lateral, só com `profile` disponível. Abre Configurações › Perfil.
- `theme-toggle`: no Aurora vive no topo (`workspace-header`), como `AuroraIconButton` tonal; no fixture congelado continua na lateral. Cada modo o carrega num `Loader` de `active` exclusivo, então `findChild` nunca acha a cópia do outro modo.
- `navigation-count` (contagem de Biblioteca e Playlists, só a partir dos bindings) e `navigation-trailing-icon` (ícone `external` do Navegar).
- `player-time` (“1:19 / 3:37”) e `progress` na borda de cima do player, com `player-lyrics`, `player-queue`, `mute` e `volume` à direita. Na janela compacta (até 900 px) o `volume` fica oculto e o `mute` permanece.
- A lateral do Aurora não mostra `queue`: a Fila abre por `player-queue` e Ctrl+L.

Ordem de Tab fixada por `tst_aurora_navigation::test_tabFollowsSidebarHeaderPageAndPlayer` (Início) e `test_tabFollowsSidebarHeaderPageAndPlayerOnEveryPage` (Atividade, Biblioteca, Playlists, Buscar, Ouvir junto e Configurações), a 1440×900: lateral (`now`, `library`, `search`, `playlists`, `room`, `downloads`, `embedded-browser`, `preferences`, `profile-chip`), `theme-toggle`, página e então `progress`, `player-shuffle`, `previous`, `play-pause`, `next`, `player-repeat`, `player-queue`, `mute`, `volume`, de volta a `now`. No Aurora as páginas ficam na árvore entre o topo e a área principal, e o Shift+Tab percorre o mesmo ciclo ao contrário. Na Atividade, o Shift+Tab do primeiro controle da página vai a `theme-toggle`, como `tst_downloads::test_pageFocusKeyboardAndReturnDoNotAffectPlayback` também confere; no fixture, a página é um diálogo e volta a quem a abriu. A página de Atividade aponta o `KeyNavigation` para relés internos (`focus-relay-before`/`focus-relay-after`): o `KeyNavigation` é recíproco e um alvo externo teria o próprio Tab reescrito pela página oculta. O relé de saída entrega o foco a `transport.firstControl`, o primeiro controle focável do player; sem alvo, ou com o alvo oculto ou desabilitado, o relé segue a cadeia de Tab da janela.

Capturas da fatia, em `out/product/ui-redesign-captures/casca` (via `RC_MUSIC_UI_ARTIFACT_DIR`): `aurora-redesign-{wide,medium,minimum}-{light,dark}.png` (`tst_aurora_composition`) e `aurora-native-{light,dark}.png` (`tst_aurora_navigation`). Os dois testes montam os backends falsos e o perfil antes de capturar, então a lateral mostra as contagens de Biblioteca e Playlists, o ícone de janela externa de Navegar e o chip do perfil (na lateral compacta, só os ícones).

## Gavetas e linha do verso (fatia 3)

Ids novos que os testes Quick usam no Aurora:

- Fila (`queue-panel`): cabeçalho `queue-header` com `queue-header-close` ("Fechar fila"); barra com `queue-add`, `queue-move-up`, `queue-move-down`, `queue-remove`, `queue-room-upload` e `queue-clear` ("Limpar fila…", botão discreto em `errorColor`); linhas de 56 px, com o ícone de onda na faixa tocando e o aviso em `queue-row-status`; estado vazio `queue-empty` com `queue-empty-primary` ("Abrir áudio"); confirmação `queue-clear-confirmation` (diálogo de perigo, com `-cancel` e `-confirm`). A gaveta continua aberta quando a fila esvazia, mas nunca abre vazia. O Tab fica preso na gaveta por `queue-focus-wrap-start` e `queue-focus-wrap-end`.
- Linha do verso: `lyrics-line` (48 px; 40 px na janela compacta), com `lyrics-line-current`, `lyrics-line-next`, `lyrics-line-open`, `lyrics-line-status` e `lyrics-line-review`. Tem o modo verso e o modo aviso. Com a gaveta da Letra aberta, a linha termina à esquerda dela, à mesma distância da borda da gaveta (32 px; 16 px na compacta), e `lyrics-line-open` continua visível (`tst_lyrics::test_verseLineStaysBesideTheOpenDrawer`, em 1440×900 e 720×520).
- Gaveta da Letra: `lyrics-drawer` com `lyrics-drawer-close` ("Fechar letra"), `lyrics-drawer-versions`, `lyrics-drawer-search`, `lyrics-drawer-more` (que abre `lyrics-drawer-romaji`), `lyrics-drawer-smaller` e `lyrics-drawer-larger`, `lyrics-drawer-status` e o estado vazio `lyrics-drawer-empty` (`-primary` "Buscar letra", `-secondary` "Adicionar versão").

Ordem de Tab com a linha do verso (`tst_aurora_navigation`): lateral, topo, página, `lyrics-line-open`, player. Sem linha, a página segue direto para o player. Na Atividade, o Tab também alcança a linha.

Gavetas exclusivas: abrir uma fecha a outra, e Ctrl+L abre a Fila. A Fila prende o Tab. A Letra não prende: o Tab sai do último controle para o player, o Shift+Tab sai do primeiro para a linha do verso (ou para `player-lyrics`, sem linha), e o Tab de fora não entra nela. Enter em `lyrics-line-open` ou `player-lyrics` devolve o foco à gaveta; Esc fecha e devolve o foco a quem a abriu. O fechamento só move o foco quando a gaveta o tinha: com o foco fora dela (no filtro da Biblioteca, em `player-queue`), a troca de faixa ou a abertura da Fila deixam o foco onde está, e a Fila devolve o foco ao seu próprio opener. `player-lyrics` só fica habilitado com binding de letra e faixa atual; sem faixa, o aviso da linha do verso (`lyrics-line-review`) é o caminho até as Versões.

F1/F2 chamam o presenter direto no Aurora: `queue-modes` fica oculto na gaveta, e `player-shuffle` e `player-repeat` são os controles marcáveis.

Capturas da fatia, em `out/product/ui-redesign-captures/gavetas` (via `RC_MUSIC_UI_ARTIFACT_DIR`, `tst_aurora_composition::test_drawerCaptures`): `aurora-gavetas-<cena>-<largura>-<tema>.png`, com cena em `verse-line`, `lyrics-drawer`, `lyrics-empty`, `queue` (8 faixas, a primeira indisponível), `queue-empty` e `queue-clear`, largura 1440 (1440×900) ou 720 (720×520) e tema `light` ou `dark`. Usam só backends falsos e não tocam som.

## Início (fatia 4)

Ids novos ou trocados que os testes Quick usam no Aurora:

- Topo da página: `home-header` (`PageHeader` com eyebrow, título e descrição) e, à direita, `open-audio` ("Abrir áudio", primário, ícone `folder-open`). O botão fica fora de todos os estados do herói, então existe em qualquer estado.
- Herói (`home-hero`, `HomeHero.qml`): `home-empty` (sem faixa; `home-empty-primary` e `home-empty-secondary` ficam ocultos no Aurora, porque a ação é o `open-audio` do topo), `playing-state` com `cover-track`, `hero-eyebrow` ("Agora tocando"), `current-title`, `hero-artist-album` (vazio sem artista e álbum), `track-details-playing` e `home-lyrics`. O estado de erro usa `error-icon-circle` (`AuroraIconCircle`), `error-title`, `error-body`, `error-open-another-audio`, `error-retry-occurrence`, `error-locate-occurrence` e `track-details-error`. As ações do erro ocupam a largura do herói, em linhas centradas uma a uma: a 1440 cabem numa linha só.
- Estantes (`HomeShelf.qml`, cartões `AuroraTrackCard.qml`): `home-queue-shelf` com `home-queue-list` e `home-see-queue` ("Ver fila"), e `home-library-shelf` com `home-library-list` e `home-see-library` ("Ver toda a biblioteca"). Dentro de cada estante: `shelf-title`, `shelf-subtitle` e, em cada cartão, `shelf-card-artwork`, `shelf-card-title`, `shelf-card-subtitle` e `shelf-card-playing` (chip "Tocando", só no cartão da ocorrência atual e só no estado Tocando: some em pausa, pronto, carregando e erro). `HomeShelf.linkObjectName` nomeia o link; o `linkControlId` do plano não existe.
- Saem do Aurora: `open-another-audio`, `current-artist`, `current-album` e `queue-card-artwork`. `QueuePreview.qml` foi removido.

Ordem de Tab, a 1440×900 e a 720×520: `theme-toggle`, `open-audio`, os controles do herói do estado (`track-details-playing` e `home-lyrics`; no erro, as ações do `ErrorState`), `home-see-queue`, os cartões da fila (um por Tab, pulando os indisponíveis; sem nenhum disponível, o Tab e o Shift+Tab atravessam a estante), `home-see-library`, a linha do verso (`lyrics-line-open`, se houver) e o player. Os cartões da biblioteca não entram na ordem: são só leitura (`interactive: false`, `activeFocusOnTab: false`).

Foco principal por estado: sem faixa e tocando, `open-audio`; com erro, o botão primário do `ErrorState`. Com a gaveta da Fila aberta, a Início fica `enabled: false`.

Regra de rolagem: a página rola dentro de `state-layer`. Um alvo de foco já inteiro na vista não move a página, nem num clique. Fora da vista, um alvo que cabe na primeira tela mostra a página pelo topo (`contentY` 0), para o título nunca sair de vista; um alvo mais abaixo rola só o necessário. Abrir o áudio deixa o foco em `open-audio`, no topo, e a 720×520 o título da página continua inteiro.

Capas da fila: o `LibraryArtworkProvider` ganhou uma segunda instância, `image://queueart/<valor da ocorrência>`, registrada em `src/app/main.cpp`; o cartão da faixa tocando usa a capa atual. A biblioteca expõe até 12 registros recentes em `LibraryBinding.recentModel`. No harness, `driver.setLocalQueueArtworkBase("image://queueart/")` liga a base e o provider de teste responde com um pixel transparente, então as capas aparecem vazias nas capturas (o ícone de disco é o que se vê): as capturas não provam pixels de capa.

Capturas da fatia, em `out/product/ui-redesign-captures/inicio` (via `RC_MUSIC_UI_ARTIFACT_DIR`, `tst_aurora_composition::test_homeCaptures`): `aurora-inicio-<cena>-<largura>-<tema>.png`, com cena em `home-empty` (com a estante da biblioteca), `home-playing` (8 faixas, a primeira indisponível e a segunda tocando), `home-playing-lyrics` (a mesma cena com a linha do verso) e `home-error` (arquivo ausente, com Localizar), largura 1440 (1440×900) ou 720 (720×520) e tema `light` ou `dark`. O foco fica em `theme-toggle` e `contentY` em 0. As capturas da gaveta (`gavetas`) passam a conhecer as durações de todas as faixas, então a Fila mostra "8 faixas · 28 min", e a gaveta da Letra vazia nomeia a faixa do player.

## Limites

### Geometria durante metadata e capa

Em 2026-09-06, a lacuna automatizada de UX-009 foi coberta por [tst_metadata_layout.qml](../tests/ui/quick/tst_metadata_layout.qml). O teste compara capa/placeholder, bloco central, slots de texto, ações e controles internos do transporte entre Loading, Playing, conclusão assíncrona de metadata e abertura da faixa seguinte, em 720×520 e 960×640. Títulos fallback são verificados; larguras intrínsecas de texto podem variar, mas centro horizontal, posição vertical e altura do slot não podem saltar. A tolerância geométrica é de 0,5 pixel lógico, sem medir performance.

O baseline sem capa passou **4/4** no executável existente: não foi demonstrado defeito no QML de produção. A primeira tentativa de sincronização aguardava render de um Item sem conteúdo próprio e terminou em timeout; isso era limitação do harness, não Red do produto. `waitForPolish(Window)`, conforme a API Qt local, aguarda o layout de todos os itens sem sleep artificial. A extensão de capa teve Red **4 passagens/2 falhas** pela ausência do helper; o [bootstrap Quick](../tests/ui/quick/quick_main.cpp) passou a compartilhar o cache e registrar o mesmo provider da aplicação, com uma thumbnail RGBA 2×2 sintética em memória. `Image.Ready`, visibilidade e URL do provider são exigidos antes da comparação. A troca seguinte limpa a capa e conserva a geometria.

O alvo Quick Release foi recompilado; Green focal **6/6**, seguido de CTest **3/3** (Quick completo **193/193**, contrato UI **29/29** e fronteira da composição), sem falhas/skips. A revisão acrescentou controles internos da barra e confirmação do placeholder/títulos; o focal final permaneceu **6/6**. Nenhum código de produção mudou, portanto não houve rebuild OFF, corpus de áudio ou execução completa dos 297 registros neste incremento. O helper anterior preserva seus mesmos dados e a montagem agora é compartilhada, sem copiar assets ou alterar fixtures autenticadas.

Formato final **172 C++/53 QML**, rastreabilidade **83 IDs**, documentos **56 Markdown/83 requisitos/17 checks/76 etapas** e diff passaram. O incremento paralelo de diagnósticos dos comandos tem validação/commit próprios, sem somar seus testes à cobertura da UI.

A prova cobre geometria assentada após polish, não cada frame transitório, strings extremas, pixel-perfect entre plataformas ou inspeção visual Windows. UX-009 permanece `em andamento` no [manifesto](quality/traceability.json), com o gate nativo separado.

### Contraste de indicadores e textos

O incremento de UX-006 de 2026-09-06 testa as cores efetivas dos controles, incluindo opacidade sobre o fundo adjacente. O critério segue os textos explicativos oficiais de [WCAG 2.2, contraste de texto](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) e [contraste não textual](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html): pelo menos 4,5:1 para os textos normais examinados e 3:1 para indicadores gráficos essenciais. Não se exige contraste de uma borda meramente decorativa nem de controles inativos; valores não são arredondados para aprovação.

O [teste de cores](../tests/ui/quick/tst_color_contrast.qml) monta a janela real com presenter/portas locais controlados e vinte paths sintéticos, sem acessar áudio pessoal. Os sliders ativos de progresso/volume são verificados contra o canvas com e sem foco; os contornos externo/interno do thumb e o trecho preenchido têm assertions separadas. O thumb ativo da fila é composto sobre `surfaceColor`, incluindo sua opacidade. As combinações dos cinco tokens textuais com canvas/surface/surface-raised e `on-accent`/accent são conferidas em claro/escuro; o botão primário real também é medido nos estados normal, hover, pressed e foco. Essa tabela explícita não é um detector automático de todas as combinações QML possíveis.

Red confirmado no executável anterior: **3 passagens/3 falhas**, com trilho claro **2,9301:1** e thumb da fila **2,2526:1 claro/2,5374:1 escuro**. A caracterização adicional dos textos e botão passou **4/4**, sem defeito de texto demonstrado. O trilho/contorno neutro passou a usar `textSecondaryColor`, sem mudar tokens, estado do transporte ou paleta histórica autenticada. A rolagem da fila e dos detalhes compartilha o estilo com opacidade final 1; suas cores de sistema em alto contraste e o comportamento de rolagem permanecem preservados.

O [teste de rolagem dos detalhes](../tests/ui/quick/tst_details_scroll_contrast.qml) também confirmou Red **2 passagens/2 falhas**, após roda e arraste já moverem o viewport: **2,2526:1 claro/2,3481:1 escuro** no thumb herdado do estilo Basic. O primeiro estímulo de roda atingia um editor em vez da área de rolagem; ajustar o alvo para um label expôs a falha causal, sem alterar a produção ou enfraquecer assertions. O componente compartilhado [AccessibleScrollBar.qml](../src/ui/qml/AccessibleScrollBar.qml) mantém a política de visibilidade/fade da fila; o consumidor dos detalhes preserva parent/geometria/ativação do ScrollView Basic e rolagem horizontal desativada. Os testes também verificam rolagem e cores opacas após alternar para alto contraste injetado.

Validação do incremento:

- `lint -Diagnostics` no build ON `out/product/m1-desktop-release-on-20260906` passou configure, build Release e os dois alvos QML lint. Recibo local `out/product/validation-runs/run-7689933556c74190943d8b985c15f9a3`;
- Green focal: cores/sliders/fila **8/8**, detalhes **4/4**. A revisão acrescentou verificação do retorno de polish, estado normal sem hover, aro externo real do slider e término da rolagem cinética antes do arraste. HC focal acusou somente sua antiga expectativa de opacidade 0,75 (**2 passagens/1 falha**): atualizada para 1, mantendo o estado inativo 0 e todas as assertions de cores do sistema;
- a seleção CTest `^rc_music\.ui\.(quick|contract|build-boundary)$` passou contrato **29/29** e fronteira; Quick ficou **204 passagens/1 falha** apenas pela antiga cor de trilho `#69717d` no teste de preferências. A expectativa foi atualizada para o token neutro efetivamente escolhido, `#bbc2cc`; as assertions de seleção do tema, preenchimento e thumb foram preservadas. A reexecução somente de `rc_music.ui.quick` passou **205/205**, zero falhas/skips, em 31,92 s no CTest. Não se anuncia essa sequência como uma execução nova de 3/3 ou 297/297;
- `build -Diagnostics` reconfigurou/compilou a produção OFF em `out/product/m1-desktop-release-off-20260906`; recibo `out/product/validation-runs/run-cd6197978c9742b486a1d3d83667c5a6`. Os dois recibos registram `passed` sobre HEAD `9eea1d6e5b3f1d5121e9176a29b4094b8195325a` com alterações locais ainda não commitadas, não checkout limpo ou CI;
- formato passou **172 C++/56 QML**; a guarda OFF inspecionou **248 comandos** com fontes reais e sem suporte histórico, e CTest OFF listou **zero testes**. Documentação e rastreabilidade continuaram válidas: **56 Markdown/83 requisitos/17 checks/76 etapas**, sem mudar estados de cumprimento. A revisão independente final não deixou achados P1/P2 remanescentes. Não mudou código C++ de domínio/aplicação, fixture, schema, store, dependência ou benchmark; testes amplos de core/áudio/processos não foram repetidos por esta alteração visual.

As provas numéricas usam cores declaradas e estados assentados com movimento reduzido para excluir transições em curso. Não atestam cada frame do fade, rasterização/antialiasing nativos, todas as combinações de controles, NVDA ou preferências reais de alto contraste. Esses limites e a verificação visual final de UX-006/007 continuam separados.

### Gates externos

Os testes UI usam `offscreen`, renderização software e estilo Basic, com backends/portas determinísticos para as jornadas. Isso preserva regressões de foco, teclado, acessibilidade, fila, preferências e recuperação, mas não comprova NVDA, DPI/alto contraste nativos, diálogo do sistema ou dispositivos físicos.

Não houve nova interação nativa, uso de áudio pessoal, benchmark, Linux, instalação/associação no host ou atualização de runtime/instalador neste recorte. A promoção não aprova release. O [incremento Windows posterior](windows-integration-tests.md) acrescenta os quatro bindings à variante e amplia o teste de fronteira, sem alterar esta evidência histórica de 276 registros. A [composição dos drivers instrumentados](process-tests.md) também tem gates de build/testes validados; smokes nativos, CI Windows e os gates de segurança, licenças e distribuição continuam separados no [plano](development-plan.md).

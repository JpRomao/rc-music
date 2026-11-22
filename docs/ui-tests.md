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

Os arquivos `tests/ui/quick/tst_aurora_*.qml` cobrem as peças do redesenho em `src/ui/qml/Aurora*.qml`. Cada um hospeda a peça numa `Window` própria e roda com `QT_FATAL_WARNINGS=1`; nenhum toca som.

- `tst_aurora_buttons`: botões (cinco tipos, compacto, rótulo longo cortado com nome acessível inteiro) e botão de ícone ligado/desligado.
- `tst_aurora_fields`: campo de texto (foco, erro, linha de estado) e caixa de seleção.
- `tst_aurora_choices`: segmentado, interruptor, stepper e caixinha, incluindo movimento reduzido.
- `tst_aurora_badges`: selos de cinco tons, avatar com anel de escolhido e alto contraste.
- `tst_aurora_menus`: menu com itens, separador, Esc devolvendo o foco e troca de tema com o menu aberto.
- `tst_aurora_dialogs`: diálogo e diálogo de confirmação, foco, janela mínima e movimento reduzido.
- `tst_aurora_surfaces`: avisos, mensagem de retorno, estado vazio, cabeçalho de gaveta e cartão flutuante.
- `tst_aurora_pieces_visual`: contraste dos 18 pares de cores que as peças usam, nos temas claro e escuro, e galeria com todas as peças em claro, escuro e alto contraste dentro de `ColumnLayout`/`RowLayout` sem tamanhos explícitos, para que um aviso de layout falhe o teste. Com `RC_MUSIC_UI_ARTIFACT_DIR` definido, grava `aurora-pecas-claro.png`, `aurora-pecas-escuro.png` e `aurora-pecas-alto-contraste.png` para o aceite visual; as imagens não são versionadas.

Contagens do marco (`develop.ps1 validate` e execução direta do Quick): `tst_aurora_pieces_visual` 7/7; Quick inteiro 695 passaram e 6 falharam, todas linhas de `AuroraComposition::test_headerHeroAndFooterUseSeparateResponsiveRegions`, que exige `RC_MUSIC_UI_ARTIFACT_DIR` e já falhava antes desta fatia. No CTest, `rc_music.ui.quick` estoura o prazo de 90 s (a suíte leva cerca de 150 s ou mais nesta máquina) e `rc_music.ui.presenter` falhou uma vez em `roomHostLeadsEachStartOnceOverQueuedConfirmations` sob carga, passando 336/336 quando rodado isolado.

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

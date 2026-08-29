# Registro de autorização e evidências M0

**Estado:** M0 autorizado; M0.1 concluído; M0.2 permanece aberto, com empacotamento/A-B/automação nativa/benchmarks pesados suspensos por orientação do mantenedor; M0.5 possui 12 fixtures autenticadas e M0.6 já cobre `PB-001`/`PB-006` na matriz básica experimental de sete fixtures, `PB-004` em MP3 VBR/M4A AAC, extração por conteúdo no TagLib, polling assíncrono limitado e jornada aplicação/QML de nove linhas válidas com aliases `.mp4`/`.oga` e seletor de sete extensões em caixa baixa/alta. Quatro regressões adicionais derivam prefixos temporários de 64 bytes dos codecs básicos para cobrir erro tipado, contexto Unicode/reabertura, recuperação e descarte de geração antiga sem ampliar o corpus canônico; Opus/ALAC possuem somente caracterização `UnsupportedFormat` no extrator, sem rejeição ponta a ponta; M0.5/M0.6 permanecem abertos e stack, engines e formatos não foram escolhidos

**Data:** 2026-08-29

**Escopo:** autorização, contratos M0.0–M0.1, progresso do candidato Qt em M0.2, fundação comum M0.5, slices funcionais e primeira integração de playback/metadata M0.6 e índice das evidências experimentais

## 1. Autorização D0.9

O mantenedor instruiu: “Siga completamente o plano em `docs/development-plan.md`. Faça checks sempre que necessário”. Para fins do contrato do projeto, essa instrução:

- aprova o baseline atual de requisitos confirmados e derivados para orientar a implementação;
- autoriza executar M0.0–M0.10 e criar os dois candidatos comparáveis;
- confirma fila e restauração pausada de sessão como P0 do MVP;
- exige TDD para comportamento observável e checks/commits em cada incremento;
- não aceita antecipadamente Qt, Tauri, Qt Multimedia, Rodio ou qualquer empacotador final;
- não autoriza publicação externa, telemetria, auto-update ou fontes remotas no MVP.

M1 continua bloqueado até M0.10 produzir evidência comparável e uma decisão explícita nos ADR-0001/0002 ou em ADR sucessor.

## 2. Inventário aprovado de preferências do MVP

| Preferência | Default inicial | Valores/regras |
|---|---|---|
| Tema | sistema | sistema, claro, escuro |
| Movimento | sistema | sistema, reduzido |
| Volume | 80% | 0–100%; sem amplificação |
| Mudo | desligado | booleano; desmutar recupera último volume diferente de zero |
| Retrocesso | 5 s | inteiro entre 1 e 300 s |
| Avanço | 5 s | inteiro entre 1 e 300 s |
| Ao fechar no Windows | minimizar para bandeja | minimizar para bandeja ou encerrar |
| Ao fechar no Linux | minimizar para bandeja quando disponível | sem tray funcional, minimizar normalmente ou encerrar; nunca ficar invisível |
| Restaurar última sessão | ligado | restaura fila/item/posição sempre pausado |
| Iniciar minimizado | desligado | só pode ser ligado quando tray funcional |
| Fim da fila | parar e manter seleção | parar e manter seleção ou parar e limpar seleção; repeat continua fora do MVP |

O botão para abrir Aplicativos Padrão é uma ação, não preferência persistida. Escolha de dispositivo, shuffle, repeat, velocidade e editor de tema continuam M6.

## 3. Contratos M0.0

- [Ambiente `M0-E1`](environment.md): Windows 11 Tier 1, Ubuntu 24.04 LTS Tier 2, host, toolchains e distribuição privada.
- [Protocolo `M0-P1`](protocol.md): amostras, métricas externas, descartes, estatística, áudio, soak e evidência.
- [Layout `M0-L1`](layout.md): isolamento dos candidatos, inputs comuns, raw ignorado e runners previstos.

Decisões que permanecem abertas:

- tecnologia final de instalador;
- versão portátil no 0.1;
- nome e identidade visual;
- licença para eventual distribuição pública.

Enquanto o repositório permanecer local/pessoal e os protótipos não forem publicados, nenhum direito de redistribuição do código próprio é concedido. Dependências e fixtures continuam exigindo licença/proveniência conhecida.

## 4. Índice M0

| Evidência | Estado |
|---|---|
| D0.9 — autorização e preferências | concluído neste documento |
| M0.0 — [ambiente](environment.md), [protocolo](protocol.md), [layout](layout.md) e [evidência](evidence/m0-0.md) | concluído em `81f9f2f` |
| M0.1 — fixture comum, schemas, roteiro e harness | [concluído](evidence/m0-1.md) em `24d12ea`; piloto sintético `P` não decisório, zero coleta A/B |
| M0.2 — shell C++20/Qt Quick/QML com backend falso | em andamento; [preflight concluído](evidence/m0-2-preflight.md), core/presenter determinísticos, jornada vazia/tocando/erro, transporte, fila com virtualização funcional e navegação manual anterior/próxima; recursos `qrc` autenticados; atalhos locais, preferências/temas, alto contraste e bandeja sob as políticas comuns das seções 5–8; pacote MSVC 14.44 histórico com smokes offscreen original/relocado aprovados; [SDK Qt composto autenticado](evidence/m0-2-qt-composite-sdk.md) e receita `package-v2` congelada, ainda sem execução real de build/pacote/smoke; validação nativa parcial, foco, bandeja/alto contraste nativos, acessibilidade nativa/NVDA, segurança e medições pendentes |
| M0.2 — [triagem de segurança do pacote](evidence/m0-2-security-triage.md) | parcial; vínculo PE/SPDX conferido, fontes FreeType/PCRE2 com achados, MSVC do pacote histórico fora de suporte e pendências explícitas; não encerra `CHK-SEC` |
| M0.2 — [regressão standalone FreeType](evidence/m0-2-freetype-backport.md) | três falhas em nove casos no vendor Qtbase original e Green 9/9 na cópia isolada com `5a280ec`; fonte original preservada, sem reconstruir ou corrigir Qt6Gui/pacote e sem encerrar `CHK-SEC`/M0.2 |
| M0.2 — [rebuild Qtbase autenticado](evidence/m0-2-qtbase-refresh.md) | configure/build/install v2 concluídos no run `96bvah3m0t0t71m990olv5gpvs`; manifesto/receipt e 4.289 folhas revalidados em processo independente; não comprova SDK composto, shell/pacote, runtime/ABI, redistribuição ou encerramento de `CHK-SEC`/M0.2 |
| M0.2 — [SDK Qt composto autenticado](evidence/m0-2-qt-composite-sdk.md) | run `f75eb9bea8280844c6d1a2b3f4`, contrato `f75eb9b…`, manifesto `221afa…` e receipt `30399b…`; 9.880 arquivos/2.400.301.667 bytes e verificador independente com `verified=True`; ainda não comprova build, pacote, runtime, redistribuição ou encerramento de `CHK-SEC`/M0.2 |
| M0.2 — [atribuições e LicenseRef](evidence/m0-2-third-party-attributions.md) / [aplicabilidade por símbolos](evidence/m0-2-qt-symbol-applicability.md) | identificação documental delimitada: 19 grupos, 62 recursos e 7/7 textos LicenseRef; PDBs pareados ao PE, funções FreeType presentes e forte evidência de serialização PCRE2 ausente nessa DLL; não aprova segurança global, cumprimento de licenças ou redistribuição |
| M0.2 — [instalação MSVC 14.44 e build](evidence/m0-2-msvc-installation.md) | instalação autorizada concluída sem reboot, ferramentas/contrato/receipt reais autenticados e novo CTest Release 65/65; histórico preservado; integração posterior registrada abaixo |
| M0.2 — [integração MSVC ao pacote](evidence/m0-2-msvc-package.md) | receita integrada por TDD; 206/206 checks PowerShell, Qt Release 65/65 e core Qt OFF 55/55; diagnóstico preservado e pacote da receita commitada `63717e5` aprovado nos smokes offscreen 2/2 original e 2/2 relocado; validação nativa parcial, sem encerrar M0.2 |
| M0.3 — shell Rust/Tauri equivalente | pendente |
| M0.4 — resultados de shell | pendente |
| M0.5 — corpus e catálogo comum | em andamento; 12 fixtures autenticadas cobrem WAV, MP3 CBR/VBR/Unicode, M4A/AAC-LC, FLAC, Ogg/Vorbis, candidatos Opus/ALAC e bordas iniciais; prefixos truncados temporários exercitados em testes não são fixtures canônicas; arte/limites, truncados versionados por contêiner e licença pública pendentes |
| M0.6 — [slices Qt Multimedia/TagLib](evidence/m0-6-qt-audio-first-slices.md) | em andamento; lifecycle/eventos tipados e geração isolada verdes; `PB-001`/`PB-006` percorrem WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis, `PB-004` cobre MP3 VBR/M4A AAC e `MR-001`, `MR-003`, `MR-018`, `MR-019` e `MR-101`–`MR-103` cobrem a matriz básica por conteúdo, Unicode, fallback e hostis existentes; Opus/ALAC retornam `UnsupportedFormat` apenas numa caracterização separada do extrator, fora de `MR-018`, enquanto playback permanece independente; polling limitado em `MR-016`/`MR-017`/`MR-020` e composição aplicação/QML de nove linhas válidas sobre sete fixtures, com aliases `.mp4`/`.oga`, fallback Unicode, autoplay, play/pause, seek e metadata assíncrona. Quatro linhas temporárias truncado → válido cobrem erro tipado, M4A sem faixa de áudio, contexto Unicode/reabertura, recuperação e descarte da geração substituída sem ampliar o catálogo canônico; áudio audível, automação nativa, comportamento ponta a ponta dos candidatos, hostis canônicos restantes, arte/limites e pacote pendentes |
| M0.7–M0.8 — adapter desafiante e comparação de áudio | suspensos/pendentes |
| M0.9 — integração/pacote | pendente |
| M0.10 — combinação e decisão | pendente |

O teste funcional da fila verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação; não conta todo o pool de reutilização nem as instanciações ao longo da execução e não fecha NFR-PERF-006. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida; nenhuma nova verificação manual é alegada para a navegação. NVDA e o protocolo externo de desempenho continuam pendentes.

Resultados brutos grandes não entram no Git sem necessidade. O repositório preserva schemas, protocolo, resumos e hashes suficientes para reproduzir ou auditar cada decisão.

## 5. Política experimental de navegação manual

Para manter paridade entre M0.2 e M0.3, os dois shells usam esta política sobre as ocorrências da fila congelada. Ela não altera a fixture nem o protocolo M0-P1 e não aceita antecipadamente uma stack. As guardas de estado e seek têm precedência sobre as regras de direção.

| Comando/condição | Resultado no shell experimental |
|---|---|
| Anterior, posição maior que 3.000 ms e seek permitido | seek para zero na mesma ocorrência |
| Anterior, posição até 3.000 ms ou sem seek | procurar a ocorrência anterior disponível, sem circular a fila |
| Próxima | procurar a ocorrência seguinte disponível, pulando indisponíveis, sem circular a fila |
| Busca de vizinho sem ocorrência disponível no sentido solicitado | operação inerte |
| Vazio/`Unloaded`, `Loading`, `Error` ou seek em preview/aguardando ack | anterior e próxima inertes |
| Troca manual de ocorrência | posição zero e duração da linha de destino; preservar `Ready`/`Paused`/`Playing`, volume, mudo e identidade do modelo da fila |

Com a fila aberta, seleção e rolagem acompanham a ocorrência atual sem roubar o foco do transporte. O título longo da faixa atual permanece dentro da área livre ao lado do painel. No candidato Qt, regressões automatizadas cobrem a preservação do foco e os limites do título de `queue-0512`; isso não substitui inspeção manual/NVDA.

EOF permanece `clamp_paused`, sem avanço automático. “Pular” no cenário de erro é uma recuperação separada: `queue-0999` com contexto `queue-0017` volta ao seed `queue-0017` em 84 s; não é o comando Próxima. CRUD da fila, avanço automático por `ended`, política real de fim da fila e persistência não pertencem a esta fatia. FR-011/FR-016/FR-017 continuam destinados às etapas do produto, sem alegação de conclusão pelo experimento.

### Atalhos de janela e foco

Para paridade M0.2/M0.3, os atalhos dos [requisitos](../product/requirements.md) atuam somente na janela ativa e reutilizam os mesmos casos de uso dos controles. Não são atalhos globais do sistema, não implementam teclas de mídia nativas e não concluem FR-037. Fixture e protocolo continuam inalterados.

| Tecla | Política do shell experimental |
|---|---|
| `Ctrl+O` | no modo de fixture, aciona uma vez a abertura falsa já existente; no executável combinado, abre o seletor nativo `.mp3/.MP3`, `.m4a/.M4A`, `.mp4/.MP4`, `.flac/.FLAC`, `.ogg/.OGG`, `.oga/.OGA` e `.wav/.WAV` e também permite substituir a faixa atual; filtro não equivale a suporte, e a automação do diálogo visual continua pendente |
| `Space` | reproduz/pausa com foco genérico ou no título; botões, sliders, fila e editores preservam sua própria interação, sem ativação dupla |
| `M` | alterna mudo fora de edição de texto, usando o caso de uso existente |
| `Left` / `Right` | fora de editores, sliders, fila e seletores que usam setas, usa os segundos configurados de retrocesso/avanço e as mesmas guardas de seek dos botões |
| `Up` / `Down` | fora desses consumidores de setas, altera volume em 1 ponto percentual, preservando a fração existente, como o slider; limitado a 0–100% |
| `Ctrl+Left` / `Ctrl+Right` | anterior/próxima conforme esta seção; preserva a navegação por palavras dos editores |
| `Ctrl+L`, `Ctrl+,`, `Esc` | fila, abertura/refoco de Preferências e fechamento de painel/modal mantêm as políticas de foco das seções 6 e 8 |
| `Ctrl+Q` | solicita saída explícita, idempotente, inclusive em Preferências; não consulta a preferência de fechar nem pede o primeiro aviso de bandeja |

Vazio, carregamento e erro deixam os atalhos de transporte inertes. Preview/ack de seek suspendem saltos e navegação de faixa. Abrir, reproduzir/pausar, mudo, alternar fila, refocar Preferências e sair ignoram autorrepetição de tecla; setas podem repetir, respeitando essas guardas. Botões de transporte consomem `Space`/`Return`/`Enter`, mas não bloqueiam os atalhos de salto e volume pelas setas. As setas locais do progresso/volume e a seleção da fila não são substituídas pelos atalhos da janela, e o transporte não muda o foco.

Enquanto Preferências estiver visível, abertura de áudio, transporte e fila ficam suspensos; `Ctrl+,` continua refocando o painel e `Ctrl+Q` continua sendo saída explícita. Enquanto o aviso de ciclo de vida estiver visível, inclusive no fade de saída, todos esses atalhos da janela ficam suspensos. As ações próprias do aviso, como `Esc` para cancelar e `Space` em seus botões, continuam disponíveis.

No Qt, `Ctrl+Q` e `Ctrl+,` usam o [contexto de aplicação de `Shortcut`](https://doc.qt.io/qt-6/qml-qtquick-shortcut.html#context-prop), com guarda adicional da janela ativa e do aviso, para atravessar somente o modal de Preferências. O [matcher do Qt 6.11.2](https://raw.githubusercontent.com/qt/qtdeclarative/v6.11.2/src/quicktemplates/qquickshortcutcontext.cpp) bloqueia atalhos de contexto de janela fora de um popup modal. A guarda explícita impede que outra janela da aplicação acione esses comandos; não há registro de atalhos no SO.

### Evidência diagnóstica de teclado — 2026-08-27

A retomada do desktop anterior à correção dos atalhos foi autorizada. Essa [tentativa nativa](evidence/m0-2-native-attempt-20260827.md) registrou `Ctrl+Q` inerte, mas a observação de `Space` e a evidência visual permaneceram inconclusivas; autorização não equivale a aprovação desses gates. A falha de `Space` foi estabelecida separadamente por Red offscreen com foco em `current-title`: o estado permaneceu `Playing` em vez de `Paused`. Reds adicionais reproduziram ausência de abertura por `Ctrl+O`, mudo por `M`, alteração de volume por seta, salto configurado, refoco de Preferências e saída por `Ctrl+Q`, além de alternância indevida da fila por autorrepetição de `Ctrl+L`. A validação nativa parcial posterior do pacote `63717e5` tem registro próprio na seção 9 e não reclassifica essa primeira tentativa.

A revisão identificou que a primeira guarda de foco bloqueava também setas sobre botões, que não as consomem. O Red sobre `play-pause` focado manteve 118.000 ms em vez dos 138.000 ms esperados. A guarda de `Space` foi separada da guarda das setas; a regressão cobre saltos e volume com foco em `play-pause`, `mute` e `previous`, preservando foco e a interação local dos demais controles.

Outro Red reproduziu arredondamento indevido no volume fracionário: partindo de 0,805, `Up` produzia 0,82 em vez de 0,815. A correção remove somente esse arredondamento; a regressão também verifica `Down` de volta a 0,805 e os limites de 0–100%.

Checks finais sobre a fonte corrigida: CTest Release completo, com checks Qt estritos, 65/65 em 19,23 s; Quick completo 22/22 sem skips; Quick e cada um dos dois smokes de executável passaram dez vezes — 30 execuções em 88,24 s. `qmllint` terminou silencioso, os checks de formato passaram em 43 arquivos C++ e 22 QML, e o diff check passou. A consulta final encontrou zero processos do player/teste Quick desse build. Os logs locais `keyboard-full-release-final-ctest.log` e `keyboard-final-ctest.log` ficam em `out/m0/builds/qt-shell/keyboard-shortcuts-diagnostic/`, junto aos registros Red/Green.

O build isolado `keyboard-shortcuts-diagnostic` usa o compilador histórico MSVC 19.43.34808.0: seus resultados servem ao Red/Green da correção, não como aprovação do refresh de toolchain ou do pacote. Os testes usam backend/plataforma falsos, eventos Qt Test e `offscreen`; não executam áudio/diálogos reais nem substituem desktop nativo, NVDA, DPI/HC ou as medições de M0.4.

## 6. Política experimental de preferências, tema e movimento

Esta política completa o recorte de apresentação comum a M0.2/M0.3 sem alterar a fixture, seus defaults ou o protocolo M0-P1. As preferências do shell são somente em memória. O painel exibe uma nota que informa essa limitação e que restauração de sessão e comportamento real de fim da fila pertencem a etapas futuras. Alterar esses valores não modifica o EOF falso `clamp_paused`; FR-042/FR-043/FR-044 não são declarados concluídos.

### Painel, foco e atalhos

- O painel é modal, limitado à área de conteúdo; o transporte permanece visível.
- Diálogo, botões, seletores, campos e toggles possuem roles explícitos. `Tab`/`Shift+Tab` ficam no painel, e somente uma categoria permanece selecionada.
- Abrir pelo botão congelado `preferences` ou por `Ctrl+,` fecha o painel da fila, seleciona “Geral” e foca a categoria. As três categorias são “Geral”, “Reprodução” e “Aparência”.
- `Ctrl+,` abre ou refoca o painel; não alterna para fechado. `Esc` ou “Fechar preferências” fecha e devolve o foco ao botão `preferences`; se a lista de um seletor estiver aberta, o primeiro `Esc` fecha somente essa lista e mantém o painel modal.
- Enquanto o painel está aberto, atalhos de transporte e de fila não atuam. `Space` continua pertencendo à edição ou ao controle focado.
- Volume e mudo permanecem no transporte, sem controles duplicados no painel. Aplicativos Padrão não aparece como ação sem efeito; sua integração fica em M0.9.

### IDs, textos e edição

Os textos já presentes na fixture continuam vindo dela. Os IDs e textos adicionais abaixo ficam centralizados em cada candidato e obedecem à mesma tabela; não são novas chaves da fixture congelada.

| ID | Nome | Opções/regra |
|---|---|---|
| `preferences-close` | Fechar preferências | fecha e devolve foco ao acionador |
| `preferences-general` | Geral | categoria inicial |
| `preferences-playback` | Reprodução | categoria |
| `preferences-appearance` | Aparência | categoria |
| `close-behavior` | Ao fechar | Geral; “Minimizar para bandeja” / “Encerrar” |
| `restore-session` | Restaurar última sessão | Geral; booleano |
| `start-minimized` | Iniciar minimizado | Geral; booleano, desabilitado sem tray funcional |
| `skip-backward-seconds` | Segundos para retroceder | Reprodução; inteiro de 1 a 300 s |
| `skip-forward-seconds` | Segundos para avançar | Reprodução; inteiro de 1 a 300 s |
| `queue-end-behavior` | Ao terminar a fila | Reprodução; “Parar e manter seleção” / “Parar e limpar seleção” |
| `theme-mode` | Tema | Aparência; “Sistema” / “Claro” / “Escuro” |
| `motion-mode` | Movimento | Aparência; “Sistema” / “Reduzido” |

Cada campo de salto é avaliado a cada tecla e aceita dígitos decimais ASCII que representem um inteiro entre 1 e 300, aplicando cada valor válido imediatamente e de forma independente. Vazio, número fracionário, texto não aceito ou valor fora do intervalo permanece como draft inválido com a mensagem junto ao campo “Use um número inteiro entre 1 e 300.”; essa avaliação inválida não altera o último valor válido no core. Prefixos válidos já podem ter sido aplicados antes de o draft ficar inválido: ao digitar `350`, por exemplo, `3` e `35` são válidos, mas `350` não é. Não há rollback automático da edição. Fechar e reabrir descarta o draft e apresenta o último valor válido em memória, não necessariamente o valor anterior à edição. Os botões, nomes acessíveis e atalhos de salto usam esses mesmos valores.

### Resolução do sistema e limites da evidência

No candidato Qt, o tema “Sistema” consulta `QStyleHints::colorScheme`; esquema desconhecido usa a paleta clara. “Claro” e “Escuro” substituem a escolha do sistema. A resolução ocorre antes de criar o QML e é atualizada quando o esquema ou a preferência muda, usando os 12 tokens de cada paleta congelada sem reconstruir o estado do player. As cores também são aplicadas à palette dos controles Basic e aos sliders. O candidato Tauri deve reproduzir essa semântica e o mesmo fallback por seu adapter. A [documentação oficial de QStyleHints](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop) descreve o acompanhamento do esquema do sistema e o sinal de alteração; cores explicitamente definidas pelo aplicativo exigem atualização própria. O fallback claro e as paletas da fixture são políticas deste experimento, não garantias da API.

No Windows, movimento “Sistema” usa uma consulta somente leitura `SPI_GETCLIENTAREAANIMATION`. A [referência oficial de SystemParametersInfoW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow) define essa ação como a consulta de um booleano que informa se animações na área cliente estão habilitadas; o candidato não usa a operação de alteração da preferência. Resultado desconhecido ou API indisponível usa movimento reduzido por segurança, como política do experimento; a opção “Reduzido” prevalece sobre a preferência do sistema. Os valores vêm da fixture: painel em 160 ms e feedback em 120 ms no modo normal, ambos em 0 ms no reduzido, sem animação contínua em nenhum modo. A mesma política de resultado/fallback vale para os dois candidatos; detalhes de plataforma não entram no core.

### Evidência automatizada de preferências/temas (`ab87803`)

UI, presenter e adapter de sistema estão implementados no candidato Qt com Red/Green e testes de categorias exclusivas, semântica, foco modal, edição dos saltos e aparência. Os testes de `QtSystemAppearance` usam leitores, sinais e eventos controlados. O harness de `FixtureRepository` e `PlayerPresenter` passou a usar `QGuiApplication` offscreen: `QAbstractItemModelTester` exercitava papéis que geravam warnings de `QPixmap` com `QCoreApplication`. O core permanece sem Qt.

Dois Reds adicionais dos itens de seletor Basic reproduziram contraste 1:1 no item destacado e altura de 38 px abaixo do mínimo de 40 px. O delegate passou a usar os pares `Highlight`/`HighlightedText` e `Base`/`Text`, com altura preferida de 44 px que acomoda o conteúdo. A regressão também cobre `Esc` dentro do dropdown preservando o modal e `Tab`/`Shift+Tab` em Aparência. Isso não comprova o modo de alto contraste nativo do Windows.

- CTest Release estrito após os ajustes de lifecycle QML e delegates: 58/58 em 7,25 s; suíte isolada de `QtSystemAppearance`: 20/20.
- Quick Test estrito/offscreen após o ajuste final dos delegates: 7/7 com `QT_SCALE_FACTOR=1.5` e 7/7 com `QT_SCALE_FACTOR=2`; dez repetições do Quick e dez do smoke `shell-executable.error-recovery` passaram.
- Formato: 35 arquivos C++ e 15 QML aprovados; `qmllint` final silencioso, exit 0; contratos de fixture/markers aprovados com hashes inalterados.
- Build Qt OFF: 51/51 testes e 473 asserções; auditoria de 113 comandos, cache e imports sem Qt.
- Build novo Qt ON com `BUILD_TESTING=OFF`, seguido de rebuild do `main` e dos delegates finais: aprovado, zero testes registrados e nenhum executável de teste ou macro de controle de teste nos 152 comandos auditados; cenário indisponível e smoke artificiais rejeitados com exit 1.

O smoke do build sem testes observou o default vivo por 1,5 s sem output; o alias normal `--m0-scenario=empty` também foi aceito. Os processos dos smokes foram encerrados por PID e não restaram processos de validação. Essa observação não comprova saída graciosa do default. Os fatores de escala offscreen não comprovam DPI nativo nem NVDA.

No fechamento de preferências/temas em `ab87803`, alto contraste permanecia pendente para a fatia seguinte; as paletas claras/escuras e os fallbacks não comprovavam esse gate. Nenhuma validação manual nativa, NVDA, escala ou movimento reduzido foi executada nessa fatia, que não encerrou M0.2 nem aprovou integralmente UX-006 ou desempenho. Seus resultados acima são históricos; a evidência de alto contraste é registrada separadamente a seguir.

## 7. Política experimental de alto contraste do sistema

Esta política define a fatia de alto contraste implementada no candidato M0.2 e a paridade exigida do candidato M0.3. Os checks automatizados e a revisão de código da fatia foram aprovados, com evidência abaixo. A fixture e o protocolo congelados permanecem inalterados.

Quando o sistema solicita alto contraste, sua paleta prevalece temporariamente sobre “Sistema”, “Claro” e “Escuro”, sem alterar a preferência guardada em memória nem criar um quarto tema selecionável. Ao sair do alto contraste, reaplica-se a preferência atual, inclusive se ela tiver sido alterada enquanto o modo estava ativo. Movimento continua independente: alto contraste não muda a escolha “Sistema”/“Reduzido” nem os valores de duração definidos na seção 6.

Enquanto o modo estiver ativo, o painel de preferências apresenta a nota “O alto contraste do sistema está ativo. O tema escolhido será usado quando ele for desativado.”, com ID de automação e `Accessible.id` iguais a `preferences-high-contrast-note`. Esse texto adicional é centralizado no candidato e comum ao A/B, sem alteração da fixture.

### Cores e estados

A projeção usa cinco papéis de cor do sistema, sempre em pares opacos:

| Uso | Fundo | Primeiro plano |
|---|---|---|
| Conteúdo e controles em estado normal | `Window` | `WindowText` |
| Realce de seleção ou interação | `Highlight` | `HighlightedText` |
| Conteúdo e controles desabilitados | `Window` | `GrayText` |

Foco, hover, pressionado, seleção e feedback devem respeitar esses pares; não se derivam cores com alpha ou `darker`. O estado desabilitado usa a cor mapeada, sem reduzir a opacidade do controle para indicá-lo. A orientação oficial da Microsoft para [alto contraste](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter) recomenda consultar as cores escolhidas pelo usuário e preservar pares de primeiro plano/fundo. A referência de [GetSysColor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsyscolor) identifica os pares de janela, realce e conteúdo desabilitado usados acima.

Sliders e scrollbar controlados pelo candidato seguem a mesma paleta. Em alto contraste, a scrollbar mantém opacidade 1; fora dele, seu fade usa o tempo de feedback de 120 ms no modo normal e 0 ms no “Reduzido”, sem fade residual de aparecimento ou desaparecimento. Essa política não pressupõe cobertura dos componentes internos nativos do Qt.

### Consulta e atualização por plataforma

No candidato Qt, a detecção usa `QAccessibilityHints::contrastPreference`, acessível por `QStyleHints::accessibility`; a [documentação de QAccessibilityHints](https://doc.qt.io/qt-6/qaccessibilityhints.html#contrastPreference-prop) descreve a preferência de contraste como informação somente leitura do sistema. No Windows, as cinco cores vêm de `GetSysColor` (`COLOR_WINDOW`, `COLOR_WINDOWTEXT`, `COLOR_HIGHLIGHT`, `COLOR_HIGHLIGHTTEXT` e `COLOR_GRAYTEXT`), não de um accent WinRT. Essa escolha evita usar a cor de destaque comum como substituta da paleta de alto contraste. No Linux, `QPalette` é o fallback experimental, sem evidência de validação nativa; o candidato M0.3 deve preservar a mesma precedência e os mesmos pares por seu adapter. Nenhuma dessas consultas altera as configurações do sistema ou introduz dependência de plataforma no core.

A primeira resolução ocorre antes da criação do QML. Em execução, notificações de contraste, esquema e paleta agendam uma atualização coalescida após o processamento do Qt, para ler o estado atualizado. A [documentação de QStyleHints](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop) alerta que o sinal de mudança do esquema ainda pode observar a paleta anterior e orienta acompanhar eventos de paleta. No Windows, a atualização também considera `WM_SYSCOLORCHANGE`, que [notifica as janelas sobre mudanças nas cores do sistema](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-syscolorchange). A coalescência é política do adapter, não garantia dessas APIs.

### Evidência automatizada da fatia de alto contraste

`HighContrastPalette` transporta as cinco cores tipadas entre adapter e apresentação, sem entrar no core. O contrato rejeita cores inválidas ou não opacas antes de publicar a aparência; a atualização é atômica e idempotente. `TransportSlider` reúne a apresentação controlada de progresso/volume, e `tst_high_contrast.qml` exercita paletas injetadas escuras/claras, foco, hover, seleção da fila, controles desabilitados, nota de precedência e saída do modo sem perder tema escolhido, modelo ou posição. A scrollbar controlada cobre opacidade 1 em alto contraste e transição imediata em movimento reduzido.

A auditoria independente encontrou três P2, cada um reproduzido por um Red Quick de 9 passagens/1 falha: contorno do handle focado em `Highlight` (`#f8f800`) sobre `Window`, em vez de `WindowText` (`#fafafa`); `Accessible.id` vazio na nota HC; e anel de foco visível do ComboBox com contraste de aproximadamente 1,09:1 sobre o realce. As correções usam os pares correspondentes ao fundo e o ID acessível explícito, com regressões de foco/contraste/identidade. A revisão independente de código confirmou os três achados resolvidos; isso não constitui inspeção nativa.

- Contrato C++ de apresentação: Red com 12 passagens/6 falhas; Green 18/18, repetido três vezes. Os testes cobrem precedência sobre os três temas, movimento independente, saída do modo, alterações de cada cor e rejeição sem mutação.
- Adapter `QtSystemAppearance`: Red com 17 passagens/16 falhas; Green 33/33, com 30 repetições por CTest. Leitores, sinais Qt e mensagens injetadas verificam coalescência após o turno de eventos, snapshot coerente e cancelamento de atualização pendente na destruição.
- Quick Test: o primeiro Red acusou o método de cenário ausente; após sua introdução, o Red mostrou HC ainda falso. A nova jornada ficou Green, com suíte completa 10/10 sem skips.
- CTest Release estrito após as três correções da auditoria: 58/58 em 8,38 s; `qmllint` silencioso. Formato final aprovado para 36 arquivos C++ e 17 QML, com `git diff --check` limpo.
- Quick estrito/offscreen reexecutado após as correções: 10/10 em `QT_SCALE_FACTOR=1.5` (2,748 s) e 10/10 em `QT_SCALE_FACTOR=2` (2,763 s). Quick e smoke `shell-executable.error-recovery` passaram dez vezes cada, 20 execuções finais por `--repeat until-fail:10` em 31,96 s.
- Core Qt OFF: 51/51 por CTest em 1,50 s e execução direta com 51 casos/473 asserções; 113 comandos de compilação, cache e imports auditados sem Qt. Fixture e markers revalidados com hashes inalterados.

O build Qt ON com `BUILD_TESTING=OFF` recebeu rebuild incremental após as três correções QML: 11 ações em 7,82 s, zero testes registrados pelo CTest, 153 comandos de compilação sem hooks ou fontes de teste, nenhum executável de teste e nenhuma DLL Qt Test. O default estrito permaneceu vivo por 1,5 s com stdout/stderr vazios; foi encerrado pelo próprio PID e não deixou processos residuais. Isso não comprova saída graciosa. O cenário indisponível, isolado e combinado com o smoke artificial, foi rejeitado com exit 1 no build HC anterior às três correções visuais; o parsing não mudou entre esses builds. Esta evidência é incremental, não um configure/build novo, e não reutiliza os resultados de preferências/temas da seção 6.

Rastreabilidade de `CHK-CLOSE`: commit que introduz a fatia HC e este registro, com os checks acima e revisão do diff.

As paletas e notificações injetadas comprovam a boundary e o QML do candidato, não a leitura real de `GetSysColor`, a alternância do Windows ou os ramos internos de alto contraste do estilo Qt Basic. Também não comprovam Linux, NVDA, DPI real ou a jornada nativa de movimento reduzido. Nenhuma nova inspeção manual da UI foi realizada. Alto contraste nativo requer evidência própria; UX-006 e M0.2 permanecem em andamento, sem novos requisitos funcionais declarados concluídos e sem aprovação de desempenho.

## 8. Política experimental de bandeja e ciclo de vida

Esta seção define a política implementada no candidato Qt, comum aos shells Qt/M0.2 e Tauri/M0.3. Não altera a fixture, o protocolo ou os defaults congelados. A [UX, seção 7](../product/ux-specification.md), continua como referência para o aviso e o menu mínimo.

### Ações, menu e restauração

Fechar a janela respeita a preferência de fechamento: “Encerrar” solicita saída; “Minimizar para bandeja” segue o fluxo de ocultação. A ação explícita “Ocultar” do menu ignora a escolha “Encerrar”, sem alterá-la, e não pede o aviso de primeiro fechamento. “Sair” sempre solicita encerramento real e idempotente, sem alterar preferências. Reprodução/pausa e anterior/próxima da bandeja usam os mesmos casos de uso e capacidades do transporte principal, inclusive as restrições da navegação da seção 5.

| ID comum | Conteúdo e ação |
|---|---|
| `tray-current-track` | título atual ou contextual, quando disponível, ou “Nenhuma faixa selecionada”; restaura a janela |
| `tray-play-pause` | rótulo de reproduzir/pausar da fixture, conforme o estado; aciona o transporte comum |
| `tray-previous` | anterior, com a capacidade atual do transporte |
| `tray-next` | próxima, com a capacidade atual do transporte |
| `tray-show-hide` | “Ocultar” com a janela exibida; “Mostrar” quando oculta ou minimizada |
| `tray-exit` | “Sair” |

O título do menu é uma projeção de texto plano, elidida à direita em 280 DIP, com quebras de linha/tabs e espaços normalizados; ampersands permanecem literais. Não se modifica a metadata original. No Qt, `&` é escapado apenas na projeção de `QAction`, cuja [documentação de texto](https://doc.qt.io/qt-6/qaction.html#text-prop) distingue mnemônicos de ampersands literais.

`Trigger` e `DoubleClick` no ícone solicitam restauração e ativação idempotentes, nunca alternância de visibilidade. A restauração preserva o último modo não minimizado — normal, maximizado ou fullscreen —, seleção, fila, posição/estado de reprodução e foco válido. Repetir o gesto não pode voltar a ocultar a janela. A [API de QSystemTrayIcon](https://doc.qt.io/qt-6/qsystemtrayicon.html#ActivationReason-enum) distingue os motivos de ativação; a escolha de restaurar nos dois casos é a política comum do experimento.

### Avisos por execução, sem persistência

Na primeira tentativa de fechar para a bandeja de cada execução, com o aviso habilitado, um aviso aparece na própria janela **antes de ocultá-la**: Popup QML no Qt, com equivalente no Tauri. Título: “Continuar na bandeja”. Corpo exato da UX: “O player continuará na bandeja. Use Sair para encerrar.” “Continuar” confirma a ocultação e consome o primeiro aviso; “Cancelar” ou `Esc` mantêm a janela e não consomem esse primeiro aviso. Não há confirmação ou ocultação automática por tempo. Um balão do sistema não substitui esse fluxo: a [documentação de showMessage](https://doc.qt.io/qt-6/qsystemtrayicon.html#showMessage) informa que a mensagem pode não aparecer conforme a configuração do usuário.

O checkbox “Avisar ao fechar para a bandeja nesta sessão” começa habilitado em cada execução e aplica a escolha imediatamente em memória. Desmarcar e depois cancelar deixa o aviso desabilitado para o próximo fechamento, embora a tentativa cancelada não tenha consumido o primeiro aviso. Confirmar “Continuar” já consome o primeiro aviso mesmo mantendo o checkbox habilitado. Não há alegação de opt-out persistente entre execuções.

Sem bandeja disponível, ações de ocultação — inclusive fechar configurado para bandeja — resultam em minimização normal da janela, nunca apenas em ocultação. A ausência conhecida pelo core gera `minimize_window`; se ela for observada somente pelo adapter durante a tentativa de ocultar, o evento `hideFellBackToMinimize` publica a nota após a minimização efetiva, mesmo sem mudança do cache de disponibilidade. Isso não substitui a saída explícita de “Sair” ou de fechar configurado para “Encerrar”. A limitação fica disponível em um Popup na restauração normal da janela, com título “Bandeja indisponível” e corpo “Não há bandeja disponível neste ambiente. Ao fechar, a janela é minimizada normalmente.” “Entendi” apenas dispensa a nota, sem ocultar, encerrar ou alterar a preferência. Perda de disponibilidade observada enquanto a janela está oculta causa a mesma minimização e disponibiliza a mesma nota.

O aviso conserva seu texto durante o fade de saída. Uma nova tentativa de fechar durante esse fade reabre o aviso sem perder o foco de retorno; callbacks de foco da fila e do fechamento de Preferências não podem retirar foco do modal ativo. Atalhos da janela ficam suspensos enquanto o aviso está visível. Movimento normal/reduzido continua usando 160/0 ms, sem timeout de confirmação.

Os textos adicionais de menu e avisos acima ficam centralizados em `LifecycleText` C++ no candidato Qt e devem ter os mesmos textos no candidato Tauri; rótulos de transporte já congelados continuam vindo da fixture.

### Adapter Qt, capacidades e limites

`QtShellPlatform` usa `QSystemTrayIcon`, `QMenu` e `QApplication`, acrescentando Qt Widgets somente ao adapter/shell experimental. A justificativa é usar o ícone e o menu do toolkit com integração de plataforma, mantendo a UI principal em Qt Quick e o core sem Qt. [QSystemTrayIcon](https://doc.qt.io/qt-6/qsystemtrayicon.html) e [QMenu](https://doc.qt.io/qt-6/qmenu.html) pertencem a Widgets; [QApplication](https://doc.qt.io/qt-6/qapplication.html#details) fornece a inicialização específica necessária aos widgets. Widgets vem do `qtbase` já provisionado; os SBOMs com `NOASSERTION` e demais bloqueios de distribuição do [preflight](evidence/m0-2-preflight.md) permanecem. O ícone provisório reutiliza `cover-track.svg` autenticado, sem alterar a fixture. O menu segue o estilo/paleta do sistema; não se forçam nele as cores do QML nem se alteram preferências do SO.

O adapter consulta disponibilidade com um timer Qt coarse de 1 s **somente enquanto a janela estiver oculta**, interrompido ao mostrar, sair ou destruir a janela/adapter. Isso não prova presença real da bandeja no Windows: no [backend Windows do Qt 6.11.2](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/windows/qwindowssystemtrayicon.h), `isSystemTrayAvailable()` retorna `true`; o Qt já possui [tratamento de TaskbarCreated](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/windows/qwindowssystemtrayicon.cpp). O fallback responde à indisponibilidade observada, sem alegar detecção garantida de queda/reinício do Explorer. Tauri deve preservar o comportamento comum por suas capacidades e adapter, registrando as limitações próprias.

Uma trava de saída é marcada antes de agendar o fechamento explícito da `QWindow` e `quit`, impedindo pedidos duplicados ou reativação posterior. Esse fechamento é necessário porque o [caminho de saída de QApplication 6.11.2](https://github.com/qt/qtbase/blob/v6.11.2/src/widgets/kernel/qapplication.cpp#L1551) trata janelas QWidget e pula o fechamento de QWindows de `QGuiApplication`. `quitOnLastWindowClosed` fica `false`: a política de fechar é explícita, não o encerramento implícito da última janela descrito pela [API de QGuiApplication](https://doc.qt.io/qt-6/qguiapplication.html#quitOnLastWindowClosed-prop). O smoke exige saída real em código 0 e janela QML fechada, sem encerramento forçado como evidência de sucesso.

Essa política não aprova bandeja nativa, recuperação do Explorer, NVDA, DPI real, Linux ou empacotamento, nem conclui integralmente FR-030/FR-031/FR-032. Áudio real, salvamento persistente antes de sair e sessão completa continuam fora desta fatia experimental. M0.2 permanece em andamento.

### Evidência automatizada da fatia Qt de ciclo de vida — 2026-08-27

O incremento parte de `4b4bf42` (HC) e usa Qt 6.11.2/MSVC x64, CMake Release e a mesma fixture autenticada. Os resultados abaixo pertencem ao código desta fatia, não à bandeja real do Windows:

- **Core:** Red de APIs ausentes e depois quatro falhas comportamentais na distinção fechar/ocultar/sair; Green Qt OFF 55/55, 1.849 asserções. As 113 entradas de compilação do build isolado continuam sem Qt.
- **Presenter:** Red com 20 passagens/24 falhas nas novas transições; Green 44/44. Cinco repetições do teste após refactor passaram. Há cobertura de aviso inicial, confirmação/cancelamento, opt-out, reentrância, saída idempotente com timer parado, preservação de fila/modelo/seek e whitelist QML.
- **Adapter:** Red de comportamento seguido de Green 24/24, com menu, intenções, modos da janela, disponibilidade, polling restrito, saída e destruição. Snapshots de transporte idênticos não repetem atualização de ações nem leitura de disponibilidade.
- **Binding integrado:** três Reds reproduziram minimização sem nota quando a bandeja desaparecia entre a consulta do core e a guarda do adapter. Green final 8/8 cobre cache previamente verdadeiro/falso, confirmação do primeiro aviso, preservação de preferências/modelo/seek e lifetimes/saída reentrante. Adapter, binding e presenter passaram três repetições cada, em 17,24 s.
- **Opções de lançamento:** Reds de smoke desconhecido e de combinação com `Unavailable` nas duas ordens; Green 24/24 e dez repetições. `tray-lifecycle` exige `Empty`; duplicatas e combinações inválidas são rejeitadas.
- **QML:** o Red inicial confirmou que fechar escondia a janela sem política. Reds adicionais reproduziram troca indevida de texto durante fade, perda do novo aviso ao reabrir durante saída, foco de Preferências e foco enfileirado da fila escapando do modal. As regressões foram corrigidas; o teste de sobreposição exige uma amostra durante a entrada do aviso, sem aprovação vacuamente verdadeira.
- **Release completo:** CTest estrito 65/65 em 14,95 s, com `QT_FATAL_WARNINGS=1`; `qmllint` silencioso. Formato de 43 arquivos C++ e 20 QML aprovado.
- **Jornadas repetidas:** Quick e os smokes reais de erro/ciclo de vida passaram dez vezes cada — 30 execuções, 56,40 s. Quick passou 16/16 em cada escala offscreen `1.5` e `2`, em 4.760 ms e 4.837 ms. Isso não é prova de DPI nativo.
- **Saída real:** o smoke primeiro falhou sem adapter/política explícita e depois detectou que `QApplication::quit` sozinho deixava a QWindow visível. Com fechamento enfileirado explícito, sucesso exige retorno 0, trava de saída e janela fechada; não se usa kill para aprovar essa jornada.
- **Build novo sem testes:** `tray-no-tests-20260827`, Qt ON/Release/`BUILD_TESTING=OFF`, completou 106 ações Ninja e gerou 156 entradas de compilação. CTest descobriu zero testes; não há executáveis de teste, define de hooks ou Qt Test nas dependências do app. Qt Widgets foi confirmado nos imports e módulos carregados. Cenário indisponível e ambos os smokes foram rejeitados com saída 1.
- **Startup sem testes:** permaneceu vivo por 1,5 s, offscreen/estrito, com stdout/stderr vazios. O PID retido foi encerrado à força somente para cleanup; esse check não comprova saída graciosa. A inspeção final não encontrou processos Qt do workspace remanescentes.
- **Contratos:** fixture `21a150477811aa62f59f4f958d594786bcef4a7c2dfd2bfda3ef9d29dee7c8e5` e markers `60d73a785cfeb67c0cec45fb2c77d1687edf151e8d44a122d669b4800e345d4c` permanecem inalterados; validação documental e diff check aprovados.

Logs locais ficam em `out/m0/builds/qt-shell/final-release`, incluindo `lifecycle-full-ctest.log`, `lifecycle-repeat-10.log` e os Reds/Greens QML, além dos builds isolados `navigation-core`, `lifecycle-presenter`, `tray-adapter`, `tray-binding` e `lifecycle-launch-options`. Nenhuma inspeção manual foi retomada após a interrupção de Computer Use; naquela fatia, pacote, bandeja/Explorer nativos, NVDA, HC/DPI reais, Linux e medições permaneciam pendentes. O pacote e seu smoke posterior são registrados na seção 9.

## 9. Política experimental de pacote Qt para avaliação local

Esta fatia prepara uma pasta executável para avaliação privada do mantenedor, não um instalador nem uma release redistribuível. Usa o shell determinístico, sem áudio ou rede reais, e preserva a fixture e o protocolo. O pacote inclui os controles fechados de cenário/smoke de `BUILD_TESTING=ON`; não deve ser apresentado como build de produção sem hooks. M0.2 continua em andamento.

### Receita, estrutura e proveniência

A receita [New-QtShellPackage.ps1](../../experiments/m0/candidates/qt/package/New-QtShellPackage.ps1) recebe um build e um nome de destino, exige CMake/Ninja Release x64 com Qt e hooks habilitados e reconstrói o alvo `m0_qt_shell`. Shell, fixture e protocolo devem estar commitados e limpos antes e depois do build. O cache e o receipt de refresh MSVC são autenticados antes e depois do build; falha ou drift impede criar o stage. A receita rejeita nomes existentes/inseguros antes de construir e só cria um destino novo sob `out/m0/packages/qt-shell` após as verificações finais, sem sobrescrita, escape de paths, aliases Windows inseguros ou reparse points. Os diretórios de entrada e saída devem permanecer quiescentes; não há garantia contra um escritor concorrente hostil.

| Caminho relativo ao destino | Conteúdo |
|---|---|
| `app/` | executável, DLLs, `plugins/`, `qml/`, `qt.conf`, aviso `LOCAL-EVALUATION.txt` e inventários `third-party/*.spdx` |
| `evidence/` | log de build, receipt autenticado de refresh MSVC, plano de deployment e relatório de imports PE; a evidência do smoke é registrada separadamente |
| `manifest.v1.json` | metadata de proveniência e inventário exato de `app/`, com paths relativos, tamanhos e SHA-256 |
| `manifest.v1.json.sha256` | digest do manifesto, que deve ser confrontado com uma referência externa antes da verificação |

O manifesto fica fora do payload para não incluir a si próprio. A verificação rejeita arquivos ausentes, extras ou alterados. Quantidade de arquivos e bytes do payload são relatadas separadamente dos pré-requisitos externos, sem convertê-las em métricas de RAM ou desempenho.

A proveniência registra `source_commit`, `source_paths_clean` e `recipe_dirty`, incluindo a receita e o contrato de refresh na avaliação deste último campo. A limpeza dos diretórios de fonte não significa que todo o worktree ou a receita estejam commitados. O manifesto também inclui `configuration`, `architecture`, `test_hooks`, as versões Qt/MSVC/linker/toolset, observações do ambiente e hashes de cache, identidade do compilador, contrato de toolchain, fixture, protocolo, plano de deployment, relatório PE, ferramentas e arquivos da receita. O contrato histórico permanece em `toolchain_contract_sha256`; o contrato e o receipt de refresh têm hashes separados. `evidence/msvc-refresh-receipt.v1.json` fica fora do payload e é vinculado pelo manifesto, mas validar o inventário do payload não reautentica automaticamente esse arquivo externo. `external_msvc_runtime`, `redistribution_allowed=false` e `known_limits` tornam os pré-requisitos e limites explícitos.

### Dependências QML, plugins e imports PE

O `windeployqt` executa scan real com `--qmldir` apontando para o QML do shell e `--qmlimport` para o QML gerado pelo build. O plano do dry-run e o plano da cópia devem coincidir, e cada arquivo copiado deve conservar tamanho e hash da origem. `--no-patchqt` evita alteração dos bytes de Qt durante o deployment. A [documentação oficial de deployment Windows](https://doc.qt.io/qt-6/windows-deployment.html) descreve o scan por `qmlimportscanner` e as opções de seleção de dependências.

As origens permitidas são a toolchain Qt e o diretório explícito `build/qml/MusicPlayer/M0`, identificado como `application-qml`; essa exceção não libera outros arquivos do build nem DLLs do aplicativo. A receita conserva a cópia do módulo QML e os estilos opcionais encontrados pelo scanner, sem otimização desse conjunto nesta fatia. Os recursos `qrc` continuam dentro do executável.

O `qt.conf` usa `Prefix=.`, `Plugins=plugins` e `QmlImports=qml`. Assim, os paths são relativos ao executável, conforme a [documentação de qt.conf](https://doc.qt.io/qt-6/qt-conf.html). O payload mantém o plugin Windows, o plugin offscreen usado nos checks e os plugins SVG. A independência efetiva dos paths de desenvolvimento deve ser comprovada pelo smoke com ambiente saneado.

A seleção automática de D3D compiler do sistema é desativada com `--no-system-d3d-compiler`. A receita adiciona explicitamente `QtRoot/d3dcompiler_47.dll`, já provisionado, ao plano e à cópia; não usa a cópia descoberta no Windows SDK ou em System32. `--no-system-dxc-compiler` exclui DXC/Direct3D 12 desta avaliação. Compiler runtime, FFmpeg, traduções e plugins de debug QML não entram pela seleção automática; ferramentas, bibliotecas de teste e arquivos de áudio são rejeitados no payload.

O relatório PE verifica x64 e imports diretos e de delay-load. Cada dependência deve estar no payload ou ser um pré-requisito Windows identificado. Arquivos de System32 são somente lidos e inventariados, nunca copiados. Essa análise não comprova todas as cargas dinâmicas nem execução em máquina limpa.

### Compilador, runtime e limites

A receita atual exige Qt 6.11.2 compilado com MSVC 19.44.35227 e shell compilado com MSVC 19.44.35228.0, usando o toolset `14.44.35207` e linker/dumpbin `14.44.35228.0` do [contrato de refresh](../../experiments/m0/provisioning/qt-shell/msvc-refresh-contract.v1.json). Cache, compiler info e qconfig são validados e têm seus hashes registrados; bytes/versões/assinaturas de cl/link/dumpbin devem corresponder aos pins antes e depois do build. O manifesto usa a identidade final. Não há fallback para 19.43 nem substituição do contrato histórico. Os pré-requisitos autenticam CMake, Ninja, `windeployqt` e `qtpaths` pelo descriptor histórico e exigem ambiente MSVC 14.44/SDK 10.0.22621.0 coerente, sem corrigir silenciosamente flags herdadas. Isso não autentica todos os inputs transitivos: `qmlimportscanner`, demais codegen, headers/libs e DLLs auxiliares não são integralmente atestados por esses executores.

O CRT x64 é um pré-requisito externo. O pacote não inclui nem instala o redistributable local 14.42 ou DLLs do runtime retiradas do Windows. A versão usada no host é registrada no inventário. A Microsoft exige runtime compatível com o componente compilado pelo toolset mais recente e impõe condições também à versão do linker. A ressalva do shell/linker antigo com Qt compilado por 19.44 permanece nos stages históricos feitos com MSVC 19.43; eles não são atualizados pela nova receita. O novo linker atende ao piso de versão exigido pelo contrato, sem certificar por si só ABI, servicing do CRT, máquina limpa ou redistribuição. Referências: [Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) e [compatibilidade binária MSVC](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170).

As cópias dos SBOMs Qt não substituem notices, fontes correspondentes ou revisão de licenças. O fechamento de licenças/notices de terceiros, D3D e a licença do projeto continuam pendentes antes de redistribuir. `NOASSERTION` não é uma licença nem prova de incompatibilidade. Mesa não está neste payload; sua pendência histórica no preflight não é resolvida por essa ausência.

Os smokes devem usar ambiente saneado, sem depender dos paths de desenvolvimento do Qt, e verificar o manifesto antes e depois da execução. A jornada de ciclo de vida exige saída normal em código 0. Encerramento forçado serve somente ao cleanup e não aprova essa jornada.

Esses checks locais não concluem `CHK-PACKAGE` integral nem M0.2. Comparação, métricas e gates comuns dos dois candidatos pertencem a M0.4; instalador, associações, upgrade e uninstall ficam em M0.9. Bandeja/Explorer, NVDA, HC/DPI reais e Linux continuam pendentes. O preflight histórico conserva seu escopo e seus resultados; esta política não os reinterpreta.

### Evidência histórica do pacote inicial — 2026-08-27

O stage `out/m0/packages/qt-shell/tray-34edef1-local-02` deriva do shell de `34edef16e98b576e292c2b66b0349d2da07e583a`, Qt 6.11.2/MSVC x64, Release/`BUILD_TESTING=ON`. Shell/fixture/protocolo estavam limpos; a receita ainda estava em desenvolvimento e registra `recipe_dirty=true`. O manifesto externo tem SHA-256 `5e1368e58431fb9f6ef7908ac25a0b68ebe668eff8b2a8b9d37587379acb1d90`. Isso identifica este artefato de diagnóstico, não um batch comparativo nem uma release pública.

| Origem | Arquivos | Bytes |
|---|---:|---:|
| Toolchain Qt | 1.360 | 75.285.179 |
| Cópia QML da aplicação | 12 | 71.521 |
| Executável, `qt.conf`, aviso e três SPDX | 6 | 7.031.647 |
| Payload total | 1.378 | 82.388.347 |

A revisão independente conferiu o manifesto, todas as origens do plano e os 69 PEs x64 por `dumpbin`, sem executar o payload. Há 29 DLLs Qt na raiz, 12 plugins nativos, 26 plugins QML e um D3D compiler, além do executável. As 733 referências PE se dividem em 228 internas, 109 CRT, 133 Windows e 263 API sets. Os 41 arquivos externos conferem em hash/versão no host; os cinco componentes MSVC CRT são `14.51.36247.0`. Não há Qt Test, executáveis de teste, CRT/redist, ferramentas, áudio, arquivos debug, DXC ou Mesa no payload. O D3D compiler confere com a cópia provisionada na toolchain, não com Windows Kits/System32.

Cinco componentes da aplicação aparecem somente como recursos `qrc`, preferidos pelo `qmldir`, e não como cópia QML solta. Seus nomes de recursos estão no executável, idêntico ao build. A inspeção estática não identificou componente ausente; a comprovação das jornadas offscreen com resolução efetiva veio posteriormente, no stage de receita commitada descrito abaixo.

Checks da receita:

- **Builder:** Red inicial 0/10; Green final 14/14. Casos cobrem origem/destino, sobrescrita, aliases/nomes reservados, reparse, cache Release/hooks, imports PE, arquivos proibidos e versões. Reds adicionais reproduziram a origem QML gerada fora da toolchain e o parsing do compiler com CRLF. A primeira tentativa de deployment com ambiente VS foi corretamente rejeitada por selecionar D3D de Windows Kits; a receita passou a usar explicitamente a cópia Qt provisionada.
- **Manifesto:** Red inicial de inventário vazio e Red adicional de igualdade cultural indevida entre caminhos Unicode distintos; Green 113/113, repetido três vezes. Há cobertura de conjunto exato, hashes/tamanhos, UTF-8/JSON, duplicatas, caminhos, ADS, hardlinks, junctions e arquivos bloqueados. A comparação final de paths é ordinal.
- **Smoke com fakes:** Green final 38/38, repetido três vezes, sem executar Qt. Cobre ambiente filho, DLLs/paths, captura/timeout/cleanup, guard contra diálogos do loader e revalidação do manifesto após ambas as jornadas. Red de pós-validação confirmou que uma alteração no payload impedia aprovação; a whitelist não foi relaxada.
- **Release comum:** CTest estrito 65/65 em 13,45 s após o rebuild do alvo. Esses testes usam o ambiente de desenvolvimento e não substituem o smoke isolado do pacote.
- **Checks finais:** core Qt OFF 55/55, 1.849 asserções; formato de 43 C++/20 QML e `qmllint` silencioso; parser de oito scripts/módulos PowerShell aprovado. Validação documental passou com 30 Markdown, 83 requisitos, 17 checks e 76 etapas; diff check aprovado.
- **Relocação de arquivos:** cópia para `out/m0/packages/qt-shell/relocação com espaços 34edef1-01`, com os mesmos 1.378 arquivos e o mesmo hash de manifesto, aprovada pelo validador. Não houve execução do aplicativo nessa cópia.

A primeira tentativa real de `tray-lifecycle` do pacote foi rejeitada porque o processo carregou o módulo externo `RTSSHooks64.dll`, injetado pelo RivaTuner Statistics Server. O runner encerrou somente o processo retido; a inspeção posterior não encontrou players do workspace remanescentes. O smoke `error-recovery` **do pacote** não foi executado naquela tentativa. Naquele checkpoint, a retomada aguardava o fechamento do RivaTuner, sem alterar sua configuração nem relaxar a whitelist do runner. A execução posterior aprovada é registrada separadamente abaixo; não reclassifica esta tentativa como sucesso.

As evidências locais ficam em `evidence/` desse stage: `deployment-plan.v1.json`, `pe-closure.v1.json`, `smoke-blocked-rtss.v1.json`, `release-ctest.txt` e logs dos testes PowerShell. Parser/whitespace e validação documental fazem parte do fechamento. PSScriptAnalyzer não está instalado e não é alegado como lint aprovado. A rastreabilidade de `CHK-CLOSE` é o commit que introduz a receita, testes e este registro; o artefato acima preserva a proveniência anterior explicitamente indicada.

### Regeneração com a receita commitada

Após o commit `200b9838dc9e86427e083771ef71bc9f1ee49f4b`, a receita gerou `out/m0/packages/qt-shell/tray-200b983-clean-01` com o worktree limpo, `source_paths_clean=true` e `recipe_dirty=false`. O novo manifesto tem SHA-256 `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99`. A verificação dos dois manifestos confirmou os mesmos 1.378 paths, tamanhos e hashes de payload, totalizando 82.388.347 bytes; os hashes da receita conferem com os arquivos commitados. O relatório PE continua com 69 arquivos e 41 pré-requisitos externos.

O build existente foi reutilizado (`ninja: no work to do.`); esta evidência prova a regeneração do stage pela receita limpa, não um rebuild do zero nem reprodução binária em outra máquina. O resultado está em `evidence/committed-recipe-check.v1.json` do novo stage. Na conclusão dessa regeneração estática, não houve execução do aplicativo ou nova tentativa de smoke: o RivaTuner continuava ativo e a autorização para retomar o desktop não havia sido recebida. A relocação registrada acima é a cópia do stage anterior; a execução relocada posterior do novo pacote está registrada abaixo.

Nesse checkpoint, M0.2 permaneceu em andamento, com execução isolada/relocada, bandeja/Explorer nativos, NVDA, HC/DPI reais, Linux, máquina limpa, integração de instalador e medições pendentes. Não houve retomada de Computer Use, leitura dos áudios pessoais, distribuição ou decisão de stack.

A [triagem de segurança posterior](evidence/m0-2-security-triage.md) cruza o pacote com SPDX, configuração, fontes fixadas e advisories públicos. Ela identifica pendências de FreeType/PCRE2 e de suporte do MSVC; hashes ou testes verdes não encerram essas questões. O registro distingue fonte afetada, alcance não demonstrado, avisos fora da faixa e componentes ausentes, sem aprovar redistribuição ou alterar o preflight histórico.

### Smoke offscreen original e relocado — 2026-08-27

Após `2cd898af1cb17f67cc2938446c174bedaf951135`, a consulta de processos encontrou zero RTSS. O runner já commitado foi executado sem alterações na whitelist, ambiente, deadlines ou journeys. Nenhuma aplicação externa foi encerrada ou configurada pelo agente. O controle do desktop não foi retomado: os processos filhos usaram `offscreen`, backend software, estilo Basic, perfil/diretório de trabalho neutros e guard contra diálogos do loader.

O original é `out/m0/packages/qt-shell/tray-200b983-clean-01`. Uma nova cópia de `app/`, manifesto e sidecar foi criada em `out/m0/packages/qt-shell/relocação com espaços 200b983-01`, sem sobrescrever o stage ou a relocação anteriores. Ambos usam o manifesto `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99`, com 1.378 arquivos/82.388.347 bytes. A verificação autenticada passou antes e depois das jornadas em cada localização.

| Localização | Jornada | Saída | Primeira captura completa (ms) | Amostras vivas | Módulos distintos |
|---|---|---:|---:|---:|---:|
| original | `tray-lifecycle` | 0 | 135,5167 | 6 | 86 |
| original | `error-recovery` | 0 | 74,6409 | 9 | 90 |
| relocada | `tray-lifecycle` | 0 | 152,4874 | 10 | 90 |
| relocada | `error-recovery` | 0 | 83,0682 | 10 | 90 |

As quatro jornadas terminaram normalmente, com stdout/stderr vazios, zero erros transitórios de captura e `terminated_by_runner=false`. Os módulos exigidos foram observados vivos antes de 500 ms; todos os módulos amostrados vieram somente do pacote ou de System32. O Qt e seus plugins vieram do payload, sem paths do SDK no ambiente filho. Sete módulos CRT observados são externos: os cinco MSVC já inventariados mais `msvcrt.dll` e `ucrtbase.dll` do Windows. A consulta posterior encontrou zero players do workspace e zero RTSS.

| Relatório relativo ao respectivo stage | Localização | SHA-256 |
|---|---|---|
| `evidence/smoke-offscreen-no-rtss-20260827-01.v1.json` | original | `6c238adce3436773f0e1995d2007d81b971291895585495edf7ca13720df22c1` |
| `evidence/smoke-offscreen-no-rtss-20260827-01.v1.json` | relocada | `63134421121f7aef8718b2f1f2f8263c740bed9330c874bcddafba1781ecbd58` |

Cada relatório preserva commit do runner, data UTC, paths, ambiente e resultados completos. Os tempos acima comprovam somente o deadline de captura do smoke; não são cold start, latência da UI, medição comparativa ou cumprimento de budget de performance. Snapshots não são um trace completo do loader. A verificação local não remove o SDK da máquina, não prova isolamento de rede e não testa CRT ausente.

A fatia documental que registrou essa retomada alterou somente documentação/evidência, sem comportamento novo que exigisse Red/Green. A validação documental passou com 31 Markdown, 83 requisitos, 17 checks e 76 etapas. Os dois manifestos, hashes dos relatórios e proveniência foram reconferidos; a revisão independente do diff não apresentou achados. O commit que adicionou esta subseção registrou o fechamento dessa fatia. Os checks C++/QML anteriores não foram apresentados como novamente executados. Nesse checkpoint faltavam autorização e validações de desktop/NVDA/HC/DPI/bandeja nativos, além da resolução das pendências de segurança/toolchain. A retomada posterior foi autorizada, mas a [tentativa nativa](evidence/m0-2-native-attempt-20260827.md) permaneceu inconclusiva; a [primeira tentativa de instalação MSVC](evidence/m0-2-msvc-refresh.md) foi cancelada na confirmação de administrador. Após nova autorização, a [instalação e o novo build Release](evidence/m0-2-msvc-installation.md) passaram, sem alterar o pacote histórico; a integração posterior à receita está registrada a seguir. M0.2 e `CHK-SEC` continuam abertos. Linux, máquina limpa, instalador, redistribuição e medições também não foram aprovados. M0.3 não foi iniciado.

### Integração posterior do MSVC à receita — 2026-08-27

A [evidência da integração](evidence/m0-2-msvc-package.md) registra o Red/Green e o novo pacote diagnóstico, sem substituir os stages e resultados históricos acima. Passaram 206/206 testes PowerShell — 181 existentes, nove do build com autenticação pré/pós e 16 de pré-requisitos —, CTest Qt Release 65/65 e core Qt OFF 55/55 com 1.849 asserções. O core foi compilado em diretório novo com MSVC 14.44; seus 113 comandos, cache e imports não contêm dependências Qt. Esses checks não são medições comparativas.

A receita real produziu `out/m0/packages/qt-shell/msvc-refresh-diagnostic-20260827-01`, derivado da fonte `d5c3027` com `recipe_dirty=true`: 1.378 arquivos, 82.423.288 bytes e 69 PEs. O manifesto tem SHA-256 `3c656dc5e6200eed5c47b71d6ed848761659a69c4f9b1c58ad4cfecef6c574c1`. As duas jornadas do smoke diagnóstico offscreen passaram. Isso valida a integração em desenvolvimento, não um pacote de receita commitada nem sua relocação.

### Pacote da receita MSVC commitada e validação nativa parcial — 2026-08-27

Após o commit `63717e5b8935cfe64132f7653f5e457dfa9867f2`, a receita gerou `out/m0/packages/qt-shell/msvc-63717e5-clean-01` com worktree limpo e `recipe_dirty=false`. O payload tem 1.378 arquivos, 82.423.288 bytes e 69 PEs; o manifesto tem SHA-256 `54eba3d9809dc1eae4b9b309d09d02721a86513bcc977638dea21101320cf787`. As duas jornadas offscreen passaram no original e novamente em `out/m0/packages/qt-shell/relocação com espaços 63717e5-01`: 2/2 em cada localização. Os relatórios e hashes estão na [evidência do pacote MSVC](evidence/m0-2-msvc-package.md); esses resultados não alteram o diagnóstico ou os stages históricos acima, nem comprovam máquina limpa, integração nativa ou desempenho.

A retomada nativa confirmou visualmente abertura da fixture por `Ctrl+O`, pausa por `Space`, fila por `Ctrl+L`, Preferências por `Ctrl+,` e seleção de Aparência → Claro. A evidência UIA de foco foi inconclusiva. A ferramenta bloqueou uma tentativa de `Esc` por interação manual; os inputs foram pausados e o mantenedor foi avisado. Na consulta de `18:14:26 UTC`, o PID `26420` já não estava presente; a causa e o código de saída não foram observados. Isso não comprova saída normal nem aprova o gate de encerramento nativo. NVDA não foi iniciado, e não houve aprovação de HC/DPI ou bandeja/Explorer nativos.

Segurança, foco, NVDA, HC/DPI, bandeja/Explorer e demais gates nativos continuam pendentes. A interação nativa permanece pausada, aguardando disponibilidade para retomar os gates com captura verificável. M0.2 permanece em andamento; não há autorização de redistribuição, aprovação de stack ou início de M0.3 por esta fatia.

## 10. Regressão standalone FreeType

A [evidência da regressão standalone](evidence/m0-2-freetype-backport.md) vincula o harness que compilou o vendor FreeType da revisão Qtbase `ef55f427f2c8b410d34f8a7681020a3000cf6866` fora de QtGui. Na árvore original, três casos com coordenada excedente falharam pelo motivo esperado e os demais seis passaram. Uma cópia isolada recebeu o patch oficial `5a280ecde6f324de0d226261036e736e0cb49a71`; os mesmos nove casos passaram, Green 9/9.

A fonte original e o archive autenticado foram preservados. A árvore contém 22.856 arquivos e a comparação original/cópia encontrou somente `src/3rdparty/freetype/src/truetype/ttgxvar.c` diferente, conforme as nove adições e 12 remoções upstream. O inventário Qt declara FreeType 2.14.3, mas os headers da fonte e do SDK oficial registram 2.14.2; o harness fixa e relata a identidade real sem reescrever metadados históricos.

Esse Green demonstra a regressão e a alteração mínima somente no binário standalone. Qt6Gui e o pacote `msvc-63717e5-clean-01` continuam com os bytes anteriores; `CHK-SEC` e M0.2 permanecem abertos. Naquele checkpoint, o próximo passo técnico era reconstruir Qtbase/QtGui em uma nova identidade, contrato e pacote, resolvendo explicitamente a configuração Vulkan/SDK sem drift. Nenhum SDK ou artefato histórico seria reetiquetado como corrigido; adoção definitiva da stack e redistribuição continuavam não autorizadas. A UI nativa permanecia pausada.

## 11. Rebuild autenticado do Qtbase

A [evidência do rebuild isolado](evidence/m0-2-qtbase-refresh.md) registra a receita v2 executada em `fff4ceaa4cfb6cec687596c1babe3f68d6136b95`. O run privado `q/96bvah3m0t0t71m990olv5gpvs` concluiu configure/build/install com exit zero e publicou uma instalação de 4.289 folhas/526.177.624 bytes. O manifesto `dc26c096…afbf6b` e o receipt `8a4e3c13…28619`, ancorado por hash externo, vinculam contrato, receita, árvores, três comandos, sete relatórios, 15 PEs contratuais e o consumidor direto `qminimal`.

O verificador público foi executado em outro processo depois do runner e revalidou receipt, manifesto, evidências e cada folha física com link único. A primeira execução real permaneceu uma falha fechada sem manifesto/receipt por diferença estrita na linha de exports do `dumpbin`; a regressão Red 59/1 e o Green 60/60, além dos 25/25 testes nativos, precederam o run aprovado. O contrato v1 histórico de 15.475 bytes/SHA-256 `2023dd7621015906d86d5956474e828f4e1ef74bedda52907c5e827aa378c2c6`, o SDK oficial e os pacotes anteriores não foram alterados ou reclassificados.

Naquele checkpoint, somente o Qtbase corrigido e isolado estava autenticado. QML/Quick/Svg e ferramentas suplementares ainda precisavam ser compostos a partir das origens oficiais autenticadas, sem colisão/fallback, e o delta entre Windows SDK `10.0.26100.0` do rebuild e `10.0.22621.0` da receita histórica ainda exigia resolução explícita. O checkpoint não provava carregabilidade, equivalência ABI, novo build/pacote/smoke do shell, PCRE2 corrigido, máquina limpa, CRT, licenças, notices, SBOM ou redistribuição. `CHK-SEC` e M0.2 continuavam abertos; a interação nativa permanecia pausada, assim como foco, NVDA, HC/DPI reais, bandeja/Explorer e encerramento.

## 12. SDK Qt composto autenticado

A [composição autenticada](evidence/m0-2-qt-composite-sdk.md) publicou o run privado `f75eb9bea8280844c6d1a2b3f4` a partir do Qtbase corrigido e das origens oficiais fechadas de Qt Declarative, Qt SVG e D3D. O contrato `f75eb9b…`, o manifesto `221afa…`, o receipt externo `30399b…` e as 9.880 folhas, totalizando 2.400.301.667 bytes, passaram por verificação independente; o processo separado retornou `verified=True`. O contrato v1, o SDK oficial e os builds/pacotes históricos preservam suas identidades e alegações anteriores.

A receita autenticada `package-v2` para consumir esse SDK com Windows SDK `10.0.26100.0` está implementada e congelada; seus comandos estão no [README do candidato Qt](../../experiments/m0/candidates/qt/README.md). Sua execução foi suspensa por orientação do mantenedor e fica disponível para retomada futura. Até lá, a composição e a receita não provam carregabilidade, ABI geral, pacote, máquina limpa, CRT, licenças/notices/SBOM completos, redistribuição, segurança ou stack aceita; `CHK-SEC`, M0.2 e os gates nativos/manuais continuam abertos.

## 13. Corpus comum M0.5

Por orientação do mantenedor em 2026-08-29, a execução pesada de `package-v2` e os benchmarks ficaram suspensos para priorizar desenvolvimento funcional. A antecipação desta fundação é limitada a um input comum e neutro: não inicia M1, não aceita Qt/Tauri ou engine de áudio, não publica matriz de suporte e não reclassifica M0.2–M0.4 como concluídos.

O corpus em [`experiments/m0/shared/audio-corpus`](../../experiments/m0/shared/audio-corpus) contém:

- WAV PCM S16LE mono/48 kHz/1 s, gerado por aritmética inteira;
- cópia byte-idêntica com extensão `.mp3` para exigir identificação por conteúdo;
- prefixo truncado de 40 bytes e arquivo vazio para erro controlado;
- manifesto fechado com SHA-256/tamanhos/expectativas, receita determinística e sidecar;
- catálogo completo dos IDs canônicos `PB-001`–`PB-012`, `PB-101`–`PB-103`, `MR-001`–`MR-020` e `MR-101`–`MR-103`.

O incremento seguinte adiciona MP3 CBR/VBR e uma fixture MP3 dedicada a tags Unicode, M4A/AAC-LC, FLAC 16-bit, Ogg/Vorbis e os candidatos Opus/ALAC. Todos derivam do mesmo sinal estéreo sintético de 2 s, 48 kHz, com tags Unicode fixas. A receita exige caminhos explícitos e autentica FFmpeg/ffprobe 8.1.1 por versão, tamanho e SHA-256; as ferramentas são usadas somente para autoria/reprodução de fixtures e não entram no runtime ou pacote.

O Red inicial falhou pela ausência do módulo público. O Green passou 7/7; uma revisão independente encontrou escrita possível através de junction no gerador, reproduzida por novo Red 7/1 e corrigida para Green 8/8. A revisão final ainda demonstrou que schemas isolados aceitavam segmentos vazios e que o rewrite in-place do corpus commitado era uma capability insegura; o Red 8/2 levou à geração exclusiva em stage fresco, gramática estrita por segmento e Green final 10/10. O runner público também aprovou o corpus com quatro fixtures, duas válidas e duas hostis. O verificador cobre UTF-8/LF, schema fechado, chaves duplicadas, inventário exato, paths e links, hashes, licença, referências da receita/catálogo e estrutura RIFF/WAVE.

No segundo incremento, o Red falhou pela ausência de `flac-16-stereo`. O gerador autenticado produziu as oito fixtures de codecs em três execuções byte-idênticas; Reds posteriores demonstraram que um recibo re-hasheado ainda podia rotular MP3 como AAC e que a receita re-hasheada podia trocar argumentos do encoder. O fechamento passou a validar semântica por fixture e a identidade exata da receita. O Green final passou 14/14 e o runner público aprovou 12 fixtures, sendo 10 válidas e duas hostis. Os checks cotidianos continuam independentes de FFmpeg e também fecham os schemas/recibos de codecs, a identidade do gerador, tags Unicode, candidatos explícitos e assinaturas físicas dos contêineres.

A revisão funcional alinhou as tolerâncias versionadas usadas pelos adapters ao manifesto: WAV 0 ms, FLAC/Vorbis 20 ms, AAC 25 ms e MP3 50 ms. O corpus continuou verde em 14/14 e no runner público com 12 fixtures; o manifesto corrente tem SHA-256 `de5491e6688b7cdad739c3b0e6beda21d15c73dffa02a8c9f1cfddf92255ba1e`.

Checks:

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Invoke-M0AudioCorpusTests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0AudioCorpus.ps1
```

M0.5 continua em andamento. Os prefixos de 64 bytes derivados em runtime para regressões de M0.6 não pertencem ao manifesto e não substituem fixtures canônicas truncadas por contêiner. Ainda faltam esses artefatos versionados e casos abaixo/no/acima dos limites de tags e arte. O conteúdo atual usa `LicenseRef-MusicPlayer-M0-Private`; uma licença de redistribuição pública depende de decisão explícita do mantenedor. Nenhum áudio pessoal foi lido ou incorporado.

## 14. Primeira jornada local integrada M0.6

Por orientação do mantenedor, o desenvolvimento funcional avançou sem retomar `package-v2`, A/B, automação nativa ou benchmarks. O candidato Qt agora possui dois modos deliberadamente separados: a fixture congelada preserva o protocolo comparativo do shell, enquanto o executável combinado liga o mesmo presenter ao `QtMultimediaPlaybackBackend`, ao `SingleWorkerMetadataReader`, ao `TagLibMetadataExtractor` e a um `QFileDialog` com `.mp3/.MP3`, `.m4a/.M4A`, `.mp4/.MP4`, `.flac/.FLAC`, `.ogg/.OGG`, `.oga/.OGA` e `.wav/.WAV`. O filtro apenas permite a escolha; ele não publica suporte. Uma caracterização isolada do extrator devolve `UnsupportedFormat` para Opus/ALAC, mas playback inicia em paralelo e o Qt pode decodificar candidatos escolhidos por `.m4a` ou “Todos os arquivos”; não há rejeição ponta a ponta nem comportamento classificado para eles.

`LocalTrackSession` mantém domínio/aplicação em C++ puro: aceita o load antes de substituir a sessão, publica imediatamente o stem Unicode como fallback, cancela metadata anterior, carrega gerações nos dois boundaries, ignora evento/resultado obsoleto ou de origem divergente e solicita autoplay quando a geração atual fica pronta. O presenter enfileira callbacks para a thread Qt, usa polling de 200 ms somente enquanto necessário, para em estados terminais e limpa timer/callback/metadata pendente no teardown. O QML reutiliza abrir, play/pause e seek em modo real e permite nova abertura no estado de erro.

O teste integrado usa sete fixtures autenticadas — WAV PCM, MP3 CBR, MP3 VBR, MP3 Unicode, M4A/AAC, FLAC e Ogg/Vorbis — em nove linhas, acrescentando `.mp4` como alias de M4A/AAC e `.oga` como alias de Ogg/Vorbis. Cada linha é copiada para um path temporário `ação 日本 ☕`; a seleção é injetada somente na fronteira do diálogo, e backend, reader/TagLib e presenter reais verificam o mesmo path, fallback imediato, event loop responsivo, `Loading → Ready → Playing`, seek, pausa e metadata terminal sem erro. A duração segue a tolerância versionada de cada fixture: WAV 0 ms, FLAC/Vorbis 20 ms, AAC 25 ms e MP3 50 ms. Como volume zero e mudo antecedem o load, essa evidência não prova áudio audível, dispositivo físico, open-to-audible, underrun ou device lost. Tampouco clica/inspeciona o diálogo nativo, publica formato, atualiza o pacote histórico ou conclui FR-001–002/FR-010/FR-012/FR-014/FR-020–024.

O extrator TagLib acrescenta regressões locais aos casos canônicos aplicáveis: `MR-001`/`MR-018` cobrem propriedades e tags dos MP3 CBR/VBR autenticados por conteúdo e Unicode; `MR-003`/`MR-018`/`MR-019` cobrem stream sem ID3 sob extensão `.audio` e fallback pelo stem Unicode; `MR-103` cobre ID3 truncado/frame-sync aleatório com erro tipado. O configure autentica tamanho e SHA-256 das fixtures MP3 CBR, VBR e Unicode. O filtro `[mp3]` passou 93 asserções em quatro casos; a suíte do extrator passou 146 em nove, e o CTest de metadata passou 9/9. Esse Green não transforma MP3 em formato publicado antes dos demais gates nem amplia o catálogo canônico.

O incremento seguinte ampliou `MR-001`/`MR-018` por conteúdo sob extensão neutra `.audio` para M4A/AAC, FLAC 16-bit e Ogg/Vorbis. O configure autentica também tamanho e SHA-256 dessas três fixtures antes de compilar os testes. No primeiro Green, a suíte completa de metadata passou **221 asserções em 11 casos**. Uma caracterização separada, sem tag `MR-018`, retorna `UnsupportedFormat` para Opus/ALAC somente no extrator. A revisão acrescentou um `ftyp` ISO BMFF com `largesize`: o Red retornava `UnsupportedFormat`; o Green reconhece o contêiner e retorna `InvalidData` antes da metadata inválida. Com essa regressão, a suíte final passou **226 asserções em 12 casos**. Isso fecha a matriz básica experimental no extrator, mas não publica suporte, não altera o catálogo canônico, não aprova candidatos nem demonstra rejeição ponta a ponta.

O recorte de falha local acrescentou regressões em três níveis. O extrator deriva, em diretório temporário, os primeiros 64 bytes das fontes autenticadas MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis e devolve `InvalidData` sem crash. O adapter de playback passa por erro/recuperação com os mesmos derivados, rejeita como `InvalidMedia` o M4A truncado carregado sem faixa de áudio e, numa carga válida imediata, não atribui callbacks antigos à nova geração. Na integração real, cada derivado aberto sob nome Unicode conserva o fallback/contexto e expõe reabrir; a fonte válida seguinte chega a `Playing`, limpa o erro de metadata e projeta somente a origem/título da geração atual. Esses quatro arquivos derivados são efêmeros e não constituem fixtures canônicas, suporte de formato ou fechamento de M0.5/M0.6.

Os checks focados registrados na primeira composição foram core C++ puro com 85 casos/2.148 asserções, `PlayerPresenterTests` 53/53, Quick Test 22/22, `m0.qt.audio.local-integration` 1/1 em 0,42 s com duas linhas e build Release do shell em 122/122 passos, incluindo a cópia de `tag.dll`; `qmllint` também ficou verde. O Red integrado de duração MP3, 2.040 ms fora de ±20 ms, foi alinhado à tolerância de ±50 ms do extrator/corpus antes do Green.

No fechamento focado anterior da matriz básica, o build relevante ficou verde; CTest passou **14/14 em 6,34 s**, composto por 11 casos de metadata, playback **1/1 em 4,75 s**, startup combinado **1/1 em 0,40 s** e integração **1/1 em 0,82 s** com sete linhas. O corpus passou **14/14**, e o runner público aprovou as 12 fixtures commitadas; os checks documentais daquele recorte também ficaram verdes.

Após a revisão, o build relevante voltou a ficar verde e CTest passou **15/15 em 6,49 s**: 12 casos de metadata, playback **1/1 em 4,35 s**, startup combinado **1/1 em 0,43 s** e integração **1/1 em 1,13 s** com nove linhas sobre sete fixtures. O corpus permaneceu **14/14**, e o runner público aprovou 12 fixtures sob o manifesto `de5491e6688b7cdad739c3b0e6beda21d15c73dffa02a8c9f1cfddf92255ba1e`. `scripts/validate-docs.ps1` validou 44 arquivos Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` também ficou verde. A [evidência detalhada](evidence/m0-6-qt-audio-first-slices.md) preserva Reds, Greens e limites. M0.2, M0.5 e M0.6 continuam em andamento; ADR-0001/0002 permanecem `Proposto` e não há stack, engine ou matriz de formatos aceita.

O primeiro Green focado das regressões de falha/recuperação passou **2/2**: playback-lifecycle **1/1 em 5,21 s** e local-integration **1/1 em 1,42 s**. Após corrigir a corrida entre descoberta de faixas e autoplay, os cinco alvos relevantes compilaram e o CTest focado completo passou **17/17 em 11,87 s**; as execuções diretas registraram **2.150 asserções em 86 casos** no core, **262 asserções em 13 casos** no extrator, **35/35** no playback e **54/54 em 4,08 s** no presenter. Benchmarks, comparação A/B e empacotamento não foram executados nesse incremento.

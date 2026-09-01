# Plano de desenvolvimento executável

**Estado:** baseline para execução e acompanhamento
**Versão alvo:** MVP 0.1, seguido de incrementos opcionais 0.2 e pesquisa R1
**Última revisão:** 2026-08-31
**Marco atual:** M0.6 — em andamento funcional, com empacotamento/A-B/automação nativa/benchmarks pesados suspensos por orientação do mantenedor; a porta C++ e o adapter Qt cobrem lifecycle/eventos tipados, geração, `PB-001`/`PB-006` para WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis, `PB-004` para MP3 VBR/M4A AAC, além de `PB-002`, `PB-003`, `PB-007`–`PB-012` e `PB-101`–`PB-103` nos recortes já documentados; TagLib cobre `MR-001`, `MR-003`, `MR-005`, `MR-008`, `MR-011`–`MR-013`, `MR-018`, `MR-019` e `MR-101`–`MR-103`, com `MR-018` aplicado à matriz básica por conteúdo, limite por campo textual medido em bytes UTF-8, limite agregado de retenção provado na policy e cobertura experimental de capa derivada em runtime, e o reader assíncrono limitado cobre polling tipado, `MR-016`, `MR-017` e `MR-020`; uma caracterização separada do extrator retorna `UnsupportedFormat` para Opus/ALAC, sem constituir rejeição ponta a ponta porque playback inicia independentemente e o Qt pode decodificar candidatos. A composição aplicação/QML oferece `.mp3`, `.m4a`, `.mp4`, `.flac`, `.ogg`, `.oga` e `.wav` em variantes minúsculas/maiúsculas e percorre nove linhas válidas sobre sete fixtures autenticadas, incluindo aliases `.mp4`/`.oga`, com fallback Unicode, autoplay, play/pause, seek, metadata assíncrona e capa embutida por provider opaco isolado pela geração. Regressões adicionais exercitam, em runtime, prefixos temporários de 64 bytes derivados de MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis: metadata e playback retornam erro tipado, o M4A carregado sem faixa de áudio é rejeitado, o erro local preserva título/contexto Unicode e oferece reabrir, e cargas válidas posteriores descartam a geração antiga. Esses derivados não são fixtures canônicas; confirmação de rejeição do engine em `PB-005`, áudio audível, automação do diálogo, comportamento ponta a ponta de Opus/ALAC, fixtures hostis/capas canônicas, bordas reais abaixo/exatamente nos limites de arte, risco de pré-alocação do TagLib, stack, engine e formatos aceitos continuam pendentes

Este documento transforma o [roadmap](roadmap.md) em uma sequência executável. Ele incorpora o produto, UX, arquitetura, TDD, corpus, segurança, distribuição e a comparação C++/Qt versus Rust/Tauri já documentados. Não substitui os [requisitos](product/requirements.md), os [ADRs](architecture/decisions/README.md) nem os [gates de qualidade](quality/quality-gates.md); detalha a ordem, os checks e a evidência necessária para cumpri-los.

## 1. Regras de uso

### 1.1 Hierarquia

1. `agents.md` define como trabalhar.
2. Os requisitos definem o que o produto deve fazer.
3. Arquitetura e ADRs definem fronteiras e decisões aceitas/propostas.
4. A estratégia de testes e os gates definem como provar qualidade.
5. Este plano define a ordem executável e o checkpoint atual.
6. O roadmap resume os marcos e limites de escopo.

Conflito com requisito ou ADR aceito é resolvido antes de implementar. Nenhum checkbox deste plano altera sozinho um requisito ou o estado de um ADR.

### 1.2 Estados

| Estado | Significado |
|---|---|
| `[x]` | Entrega presente, validada no escopo indicado e commitada |
| `[ ]` | Não iniciada ou sem evidência suficiente |
| Em andamento | Existe trabalho local; não pode ser tratado como concluído |
| Bloqueado | Falta decisão, ambiente ou autoridade identificada |
| Rejeitado | Experimento realizado e não promovido, com evidência preservada |

Um item só recebe `[x]` quando o commit, os checks e os riscos residuais estão registrados. Datas não são usadas como substituto de critério de saída.

### 1.3 Unidade de trabalho

Cada incremento deve caber em um commit coerente sempre que possível e conter:

1. requisito(s) e resultado observável;
2. teste Red, quando houver comportamento;
3. implementação Green mínima;
4. refactor com testes verdes;
5. checks proporcionais ao risco;
6. rastreabilidade atualizada;
7. revisão do diff;
8. commit atômico e working tree limpa;
9. handoff com checks executados, omitidos e risco residual.

Mudança exclusivamente documental não recebe teste artificial, mas executa `CHK-DOC` e `CHK-CLOSE`.

## 2. Estado consolidado do projeto

### 2.1 Trabalho já concluído

| Entrega | Estado | Evidência |
|---|---|---|
| Contrato operacional e exigência de commits/TDD | `[x]` | `f2c3088` |
| Requisitos rastreáveis e especificação de UX | `[x]` | `4367721` |
| Análise de stack, arquitetura e ADRs propostos | `[x]` | `c708c1b` |
| Estratégia TDD e gates de qualidade | `[x]` | `1792b49` |
| Roadmap e workflow de desenvolvimento | `[x]` | `1b3bbb4` |
| Threat model e processo de release | `[x]` | `83e9bd5` |
| Hardening dos contratos e critérios | `[x]` | `5d9c847` |
| README e índice documental | `[x]` | `94735e4` |
| Qt como hipótese principal e Tauri como desafiante A/B | `[x]` | `66d179e` |
| Plano executável com checks por incremento | `[x]` | commit que introduz este documento |
| Ambiente, protocolo e layout M0.0 | `[x]` | [`M0.0`](m0/evidence/m0-0.md), `81f9f2f` |
| Fixture, harness comparativo e piloto sintético M0.1 | `[x]` | [`M0.1`](m0/evidence/m0-1.md), `24d12ea` |
| Preflight autenticado da toolchain Qt para M0.2 | `[x]` | [`M0.2-preflight`](m0/evidence/m0-2-preflight.md), contrato `ac00a565…1952` |

### 2.2 Estado que não pode ser inferido

- Não existe código do produto, scaffold na raiz ou instalador; existe apenas o candidato experimental Qt sob `experiments/m0`, agora com o shell de fixture e uma primeira composição funcional local contra o corpus autenticado.
- Nenhum formato foi comprovado.
- Nenhuma stack ou engine foi aceita.
- ADR-0001, ADR-0002 e ADR-0003 continuam `Proposto`.
- Os budgets são metas, não medições.
- Windows possui build Release e pacote local de avaliação do candidato Qt com MSVC 14.44 e receita commitada, inventário verificado e smokes offscreen original/relocado aprovados; a validação nativa é parcial. Os stages históricos permanecem identificados separadamente. Linux e o candidato Tauri ainda não possuem build.
- YouTube continua pesquisa futura sem promessa de implementação.

### 2.3 Checkpoint M0 atual

A autoria documental e a revisão do mantenedor estão concluídas. A [autorização D0.9](m0/README.md) aceita o baseline derivado e permite apenas o escopo experimental M0.0–M0.10; Qt, Tauri e os backends continuam candidatos.

M0.1 foi concluído em `24d12ea`, com evidência consolidada em [`m0-1.md`](m0/evidence/m0-1.md). O marco fechou fixture, agenda/estatística, markers, Evidence recuperável, detector visual sintético, árvore/identidade/cleanup sintéticos, summary comum puro e o piloto Windows `P`. O caminho nominal do piloto permanece `partial/etw_live_not_implemented`; o fallback pós-falha de snapshot é `unavailable/pre_snapshot_fallback`. Ambos exigem cleanup completo e zero sobreviventes. `P` usa role `synthetic_pilot`, `decision_eligible=false` e nunca entra em snapshot-set, batch ou summary comum A/B. Nenhuma coleta comparativa A/B ocorreu e nenhuma stack ou engine foi aceita.

M0.2 está em andamento. O [preflight autenticado](m0/evidence/m0-2-preflight.md) provisionou e validou Qt 6.11.2 Desktop MSVC 2022 x64, `qmlformat`, `qmllint`, deployment/módulos efetivos, `clang-format` compatível e NVDA, com origem, hash, licença, stage isolado e contrato externo. Os Reds/Greens materializaram o core C++20 puro e seus testes fora da raiz do produto, o backend manual determinístico, o loader Qt ancorado no manifesto, o modelo tipado da fila comum e o presenter com ações de transporte, preferências, metadata atual/contextual, transição recuperável para a faixa indisponível congelada e agendamento determinístico de 200 ms. Seek em voo é cancelado quando o backend deixa de ser transportável, sem timer ou estado pendente residual. O módulo QML estático registra `ShellUiContract` e `PlayerPresenter` como tipos não criáveis e expõe somente a boundary deliberada. A jornada automatizada percorre vazio → tocando/pausado → seek por clique, drag e teclado → erro com contexto → recuperação; a mesma barra persistente cobre progresso, saltos, play/pause, volume e mudo com foco explícito, ordem de Tab, targets e semântica acessível. O painel lateral usa os tokens congelados de 320–380 px, sobrepõe somente o conteúdo na largura mínima e reserva espaço em 960 px. Sua `ListView` read-only habilita reutilização, mantém uma única parada de Tab e projeta IDs, nomes, seleção e estado indisponível sem interpretar markup. Os testes percorrem os casos hostis e verificam menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação; isso não conta todo o pool de reutilização nem o total de instanciações durante a execução. `Ctrl+L` abre/fecha o painel, `Esc` fecha e o foco retorna ao acionador. O teste conta exatamente uma tentativa de commit de seek por interação e valida limites sem sobreposição em 720 × 520. O executável experimental também entra no erro por uma opção fechada do harness; seu smoke aciona “Pular” no QML e comprova recuperação sem trocar a barra ou o modelo da fila. Fixture, fila e SVGs autenticados permanecem recursos `qrc` relocáveis.

No modo de fixture, a navegação manual anterior/próxima segue a [política experimental comum aos candidatos](m0/README.md). “Anterior” em posição maior que 3.000 ms reinicia a ocorrência por seek para zero quando permitido; até 3.000 ms ou sem seek, procura o item anterior disponível. “Próxima” procura o próximo disponível; ambos ignoram indisponíveis sem circular a fila e são inertes no vazio, carregando, erro ou durante preview/ack de seek. A troca começa em zero, usa a duração da linha e preserva `Ready`/`Paused`/`Playing`, volume, mudo e identidade do modelo. EOF continua `clamp_paused`; a recuperação de erro `queue-0999` com contexto `queue-0017` continua voltando ao seed `queue-0017` em 84 s, separada da navegação. CRUD, avanço automático por `ended`, fim da fila real e persistência não pertenciam àquela fatia inicial; foram materializados em recortes posteriores, sem por isso fechar todos os gates de FR-011/FR-016/FR-017.

A navegação atualiza a seleção e a rolagem do painel sem roubar o foco do transporte. Regressões automatizadas verificam essa preservação de foco e o título longo de `queue-0512` contido na área disponível com a fila aberta.

Os atalhos locais seguem a seção 5 do [registro M0](m0/README.md): Ctrl+O abre a fixture, Space alterna reprodução, setas usam os saltos configurados e o volume, M alterna mudo, Ctrl+setas navega, Ctrl+L alterna a fila, Ctrl+, abre/refoca Preferências e Ctrl+Q solicita saída uma única vez. As guardas preservam teclas de editores, sliders e lista, além das restrições modais; botões mantêm Space próprio sem bloquear indevidamente setas. Os Reds/Greens cobrem esses contextos e volume fracionário. Não são atalhos globais ou teclas de mídia nativas e não concluem FR-037. Estão incorporados ao novo pacote MSVC 14.44, não ao stage histórico.

A fatia de preferências/temas foi fechada em `ab87803`. O painel modal oferece Geral/Reprodução/Aparência exclusivas, roles explícitos e foco contido; saltos válidos são aplicados a cada tecla, sem rollback automático de prefixos já aceitos. Tema/movimento são resolvidos antes do QML, usando os tokens e tempos congelados. A seção 6 do [registro M0](m0/README.md) preserva a política comum M0.2/M0.3 e a evidência histórica dessa fatia. Naquele fechamento as preferências ainda eram somente em memória; a persistência Windows foi materializada depois em M3.5. EOF da fixture permanece `clamp_paused`, e FR-042/FR-043/FR-044 não são declarados integralmente concluídos por causa dos demais gates.

Alto contraste está implementado no candidato e documentado separadamente na seção 7 do registro M0: prevalece temporariamente sobre os três temas guardados, projeta pares opacos nos controles e mantém movimento independente. Os checks automatizados passaram após as correções da auditoria independente, incluindo Release estrito, escalas offscreen, core sem Qt e rebuild incremental sem testes. A validação injetada não prova a leitura real de `GetSysColor`, a alternância nativa do Windows, ramos internos do Qt Basic ou Linux, e não encerra UX-006.

A bandeja está implementada sob a política comum da seção 8 do registro M0. `QtShellPlatform` isola Widgets/ícone/menu e preserva o modo da janela; fechar respeita a preferência, ocultar a ignora e sair encerra uma única vez. O aviso inicial tem confirmação/cancelamento e opt-out em memória. Ausência observada de bandeja minimiza com nota, inclusive se surgir durante a ação de ocultar. O smoke real de ciclo de vida exige saída 0 e janela QML fechada; regressões cobrem transições rápidas e foco modal. A suíte Release estrita passou 65/65. Isso não comprova bandeja/Explorer nativos nem conclui FR-030/FR-031/FR-032.

O pacote local da seção 9 já possui receita com TDD, origem/commit, inventário SHA-256, scan QML real, imports PE x64 e caminhos relativos. O stage histórico `tray-200b983-clean-01` tem 1.378 arquivos/82.388.347 bytes, sem CRT redistribuído, executáveis de teste, ferramentas ou áudio. Após a tentativa inicial bloqueada por `RTSSHooks64.dll`, uma nova verificação encontrou zero processos RTSS e permitiu o smoke offscreen sem alterar aplicações externas ou a whitelist. `tray-lifecycle` e `error-recovery` passaram nesse stage de receita commitada e na cópia com espaços/acentos: código 0, output vazio, captura de módulos exigidos antes de 500 ms e manifesto íntegro antes/depois. Isso comprova essas jornadas sem paths Qt do SDK, não máquina limpa, desktop nativo ou redistribuição. Os gates nativos/manuais e de segurança ainda impedem encerrar M0.2 e iniciar M0.3.

A [triagem de segurança do pacote](m0/evidence/m0-2-security-triage.md) identificou fontes FreeType/PCRE2 com avisos aplicáveis e correções ausentes nos trechos examinados, sem demonstração de exploração do shell. A [análise PE/PDB](m0/evidence/m0-2-qt-symbol-applicability.md) pareou os símbolos oficiais ao pacote, confirmou as funções FreeType investigadas e encontrou forte evidência de que o caminho de serialização PCRE2 não foi incorporado a essa Qt6Core; isso não declara a dependência corrigida. A [identificação de atribuições](m0/evidence/m0-2-third-party-attributions.md) fecha a localização de fontes/textos em 19 grupos e 62 recursos autenticados, com 7/7 correspondências de LicenseRef, preservando versões desconhecidas e obrigações pendentes. O MSVC 14.43 do pacote histórico está fora de suporte; o novo build com 14.44 não altera esse pacote. Pré-requisitos Microsoft, alcance/correção FreeType, notices e licenças ainda exigem tratamento. O vínculo dos 67 PEs Qt com os SPDX foi conferido considerando a transformação de assinatura, mas não aprova vulnerabilidades ou distribuição. `CHK-SEC` continua aberto; a instalação posterior de toolchain teve autorização explícita, registrada abaixo.

A [regressão standalone FreeType](m0/evidence/m0-2-freetype-backport.md) abriu Red causal em três dos nove casos contra a fonte Qtbase fixada em `ef55f427`, e os mesmos nove casos passaram após aplicar o patch upstream `5a280ec` numa cópia isolada. A árvore original de 22.856 arquivos permaneceu sem alteração; a cópia difere somente em `src/3rdparty/freetype/src/truetype/ttgxvar.c`, com nove adições e 12 remoções. O inventário/atribuição Qt declara 2.14.3, mas `freetype.h` na fonte e no SDK oficial registra 2.14.2; os checks preservam essa discrepância em vez de ajustar versão para obter Green. Esse resultado autentica a regressão e a alteração mínima na compilação standalone, não o Qt6Gui ou o pacote existentes. Portanto não encerra `CHK-SEC`, M0.2, licenças, redistribuição ou decisão de stack.

O [rebuild Qtbase autenticado](m0/evidence/m0-2-qtbase-refresh.md) fechou o passo isolado seguinte no commit de receita `fff4cea`: o run `96bvah3m0t0t71m990olv5gpvs` concluiu configure/build/install, publicou 4.289 folhas/526.177.624 bytes e vinculou três comandos, sete relatórios, 15 PEs e `qminimal` ao manifesto `dc26c096…afbf6b` e ao receipt externo `8a4e3c13…28619`. Um processo novo revalidou o receipt e todas as evidências físicas. O contrato v1, SDK oficial e pacotes históricos permaneceram imutáveis. O resultado não é um SDK composto nem prova carregabilidade/ABI, QML/Quick/Svg, build/pacote do shell, PCRE2, CRT, licenças/SBOM ou redistribuição; `CHK-SEC` e M0.2 continuam abertos.

O [SDK Qt composto autenticado](m0/evidence/m0-2-qt-composite-sdk.md) fechou a composição isolada seguinte no run `f75eb9bea8280844c6d1a2b3f4`: contrato `f75eb9b…`, manifesto `221afa…`, receipt externo `30399b…`, 9.880 arquivos e 2.400.301.667 bytes; o processo independente retornou `verified=True`. O contrato v1, o SDK oficial e os builds/pacotes históricos permanecem preservados. Reconstruir o shell com essa identidade e Windows SDK `10.0.26100.0`, produzir um pacote autenticado v2 aditivo e repetir os smokes continua pendente, mas essa execução pesada foi suspensa por orientação do mantenedor; isso ainda não aprova runtime/ABI geral, máquina limpa, CRT, licenças/notices/SBOM, redistribuição, `CHK-SEC`, M0.2 ou a stack.

A autorização posterior permitiu a [primeira tentativa nativa de 2026-08-27](m0/evidence/m0-2-native-attempt-20260827.md). A árvore acessível mudou, mas as capturas permaneceram divergentes; nenhum gate visual, NVDA, HC/DPI ou saída nativa foi aprovado naquela tentativa. O cleanup encerrou somente o processo de teste identificado e o manifesto permaneceu íntegro. A [preparação do refresh MSVC](m0/evidence/m0-2-msvc-refresh.md), commit `b59a247`, preserva a primeira tentativa cancelada. Após nova autorização explícita, a [instalação MSVC 14.44](m0/evidence/m0-2-msvc-installation.md) terminou com exit 0, instância completa e sem reboot pendente. O compilador 19.44.35228 produziu novo build Release, CTest 65/65 e receipt real, autenticado pelo contrato separado `eead467f…93f03`; as 181 regressões do empacotador também passaram nesse checkpoint. O contrato histórico, seus oito membros e o pacote anterior foram preservados.

A [integração posterior à receita](m0/evidence/m0-2-msvc-package.md) foi implementada por TDD: autenticação antes/depois do build, rejeição de falhas/drift e uso da identidade final sem fallback ao compilador antigo. Passaram 206/206 testes PowerShell (181 existentes, nove de sequência de build e 16 de pré-requisitos), CTest Qt Release 65/65 e core Qt OFF 55/55 com 1.849 asserções. A receita real gerou o stage diagnóstico `msvc-refresh-diagnostic-20260827-01`, com `recipe_dirty=true`, e seu smoke offscreen passou 2/2. Após o commit `63717e5`, o stage `msvc-63717e5-clean-01` foi gerado com worktree limpo e `recipe_dirty=false`: 1.378 arquivos/82.423.288 bytes e 69 PEs. Seus smokes passaram 2/2 no original e 2/2 na nova cópia com espaços/acentos. A evidência linkada registra manifesto, hashes e limites; os stages históricos não foram reclassificados.

A retomada nativa desse pacote confirmou visualmente abertura da fixture por `Ctrl+O`, pausa por `Space`, fila por `Ctrl+L`, Preferências por `Ctrl+,` e seleção de Aparência → Claro. A evidência UIA de foco permaneceu inconclusiva. A ferramenta bloqueou uma tentativa de `Esc` por interação manual; os inputs foram pausados e o mantenedor foi avisado. O processo de teste já não estava presente na consulta posterior, sem causa ou código de saída conhecidos: isso não aprova encerramento nativo. NVDA não foi iniciado, e HC/DPI, bandeja/Explorer, foco e demais gates nativos continuam pendentes. O build/pacote v2 contra o SDK composto permanece disponível para retomada futura, mas está suspenso enquanto o trabalho prioriza a fundação funcional de áudio. Essa identidade nova não reetiqueta nem substitui evidência histórica e não aprova adoção definitiva ou redistribuição. A UI nativa permanece pausada até a interação estar disponível.

A prova funcional de virtualização não fecha NFR-PERF-006: o stall input → foco visível de 100 ms continua reservado ao harness externo de M0.4. A entrada real por arquivos/diretórios agora usa pipeline incremental/cancelável fora da thread visual, mas ainda não foi medida com 1.000 itens, volume lento ou harness externo. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida; a abertura visual da fila na retomada parcial não comprova sua jornada completa nem o foco. Empacotamento nativo, bandeja/alto contraste nativos, acessibilidade nativa/NVDA, validação de DPI real/movimento e medições continuam pendentes. A fixture e o roteiro congelados não são alterados pela política adicional de apresentação.

O boundary C++ puro de metadata executa o extrator em um worker, aceita somente gerações crescentes, substitui o único slot pendente e torna inobserváveis resultados cancelados, antigos ou acima do deadline de 2 s. Seu polling diferencia `Pending`, `Completed`, `Cancelled`, `Expired`, geração obsoleta, shutdown e idle sem bloquear o caller. Os casos `MR-016`, `MR-017` e `MR-020`, o encerramento durante leitura ativa e a composição real `SingleWorkerMetadataReader + TagLibMetadataExtractor` estão verdes. A chamada síncrona TagLib já iniciada pode terminar antes do join; por isso o presenter cancela trabalho e retira callbacks antes da destruição, mas essa prova não autoriza shutdown bloqueante na thread visual.

A jornada local integrada está materializada no mesmo candidato. `LocalTrackSession`, ainda C++ puro, coordena load, fallback imediato do stem Unicode, geração de playback/metadata e descarte de eventos/resultados antigos. A intenção `Autoplay` tenta `play()` uma vez somente em `Ready` da geração atual; `StayReady` consome esse estado sem emitir `play()` ou `pause()`. Uma conclusão atrasada de geração/origem substituída agora é inerte: ela não transforma a leitura atual em falha, o polling permanece ativo e a conclusão atual posterior ainda pode chegar a `Ready`. `PlayerPresenter` mantém o modo de fixture e acrescenta um modo real injetável: o executável combinado usa `QFileDialog` com `.mp3/.MP3`, `.m4a/.M4A`, `.mp4/.MP4`, `.flac/.FLAC`, `.ogg/.OGG`, `.oga/.OGA` e `.wav/.WAV`, `QtMultimediaPlaybackBackend`, `SingleWorkerMetadataReader` e TagLib; eventos do backend são enfileirados para a thread Qt e o timer existente faz polling somente enquanto necessário. O backend converte paths por UTF-16 no Windows e por bytes UTF-8 nativos no POSIX. Um teste integrado usa sete fixtures autenticadas — WAV PCM, MP3 CBR, MP3 VBR, MP3 Unicode, M4A/AAC, FLAC e Ogg/Vorbis — em nove linhas, acrescentando aliases `.mp4` para M4A/AAC e `.oga` para Ogg/Vorbis, copia cada linha para um path temporário `ação 日本 ☕`, injeta a seleção no boundary do diálogo e percorre `Loading → Ready → Playing`, seek, pausa e término do polling com volume zero/mudo. Outro caso seleciona dois arquivos em um único lote e comprova a navegação até `Playing` na geração 2 do backend Qt real. As expectativas de duração vêm do manifesto: tolerância 0 ms para WAV, 20 ms para FLAC/Vorbis, 25 ms para AAC e 50 ms para MP3. Um smoke separado carrega o executável combinado, o módulo QML e o backend FFmpeg, observa o estado vazio real e encerra pelo lifecycle de produção. Isso comprova o mesmo path nativo chegando aos adapters reais e a responsividade do event loop, não a interação visual com o diálogo nem áudio audível/dispositivo físico. A caracterização `UnsupportedFormat` de Opus/ALAC pertence somente ao extrator, fora de `MR-018`; playback pode iniciar independentemente, portanto o comportamento ponta a ponta desses candidatos segue sem classificação. FR-001–002, FR-010, FR-012, FR-014 e FR-020–024 recebem evidência experimental parcial; nenhum deles, nenhum formato e nenhuma stack são declarados concluídos/aceitos por este slice.

Na tela de faixa ativa, uma ação acessível “Abrir áudio” reutiliza o mesmo caso de uso do estado vazio e permite escolher arquivos sem conhecer `Ctrl+O`. Mouse, Space, Enter, alvo mínimo, nome/ID acessíveis, ordem de Tab e contenção em 720 × 520 estão cobertos; cancelar o diálogo continua inerte pelo contrato do presenter. A abertura inicial cria a fila; `Ctrl+O` posterior anexa o lote e toca a primeira nova ocorrência, enquanto Adicionar anexa preservando a atual. IDs de duplicatas são estáveis; tocar agora, mover, remover e limpar atualizam o modelo sem reset. Remover a atual procura somente o sucessor à frente, sem circular; na ausência dele, descarrega a sessão e mantém as ocorrências anteriores na fila. Limpar toda fila não vazia exige confirmação. Botões, Delete, Alt+↑/↓, menu contextual/tecla Menu e anúncio de reordenação têm testes Quick determinísticos, inclusive com títulos hostis e bloqueio dos atalhos globais durante a confirmação, sem equivaler a NVDA/manual. Anterior/próxima e eventos terminais mantêm as políticas por ID já documentadas. Essa fatia avança FR-001/FR-011/FR-016 e UX-004/UX-007; o diálogo nativo não é automatizado, e a persistência então ausente foi materializada posteriormente em M3.5. As transições terminais com adapters Qt reais passam a ser cobertas pelo recorte posterior, sem fechar M2.1/M3.1/M3.3 por seus demais gates.

O recorte seguinte implementou a entrada de M3.2 no candidato Qt/Windows. A janela aceita URLs de arquivos e diretórios locais; sobre a fila, o QML calcula posição antes/depois/fim por indicador, e fora dela anexa. `LocalSourceDiscovery` permanece uma porta C++ pura; `SingleWorkerLocalSourceDiscovery` enumera em um worker, publica batches de 32, progresso a cada 128 inspeções, profundidade máxima 64 e mailbox de um slot com backpressure. Gerações e âncoras por ID preservam inserção mesmo com movimentos/remoções concorrentes. Lotes mistos rejeitam somente URLs remotas; arquivos explícitos preservam duplicatas e extensões desconhecidas para diagnóstico por conteúdo, enquanto arquivos encontrados em diretórios seguem a policy central das sete extensões. No Windows, handles usam `FILE_FLAG_OPEN_REPARSE_POINT`, a identidade volume/arquivo é revalidada antes/depois da enumeração por handle e `CancelSynchronousIo` é repetido no polling até o reconhecimento físico do cancelamento; o shutdown repete a interrupção até a saída confirmada antes do `join()`. A UI cobre progresso, cancelamento, resultado parcial, anúncios terminais, foco e alto contraste opaco. Falhas assíncronas de ocorrências importadas contam uma vez; `Busy`/`NotReady` permanecem recuperáveis e `InvalidArgument` pode isolar a ocorrência. Isso avança FR-003/FR-006, sem fechar M3.2: `CHK-PERF`/NFR-PERF-006, volume lento com deadline, symlink nativo quando a política do host impede sua criação, E2E empacotado e validação NVDA/manual seguem abertos.

O recorte terminal seguinte caracteriza a composição real com um único lote Unicode `[WAV válida A, WAV truncada, WAV válida B]`, usando `QtMultimediaPlaybackBackend`, reader/TagLib e presenter reais com volume zero/mudo. O primeiro `Ended` da geração 1 carrega a geração 2, observa `InvalidMedia`, marca somente a ocorrência intermediária com `invalid_media` e continua automaticamente para B em `Playing` na geração 3. O segundo `Ended` mantém B selecionada e pausada no tail, sem quarta carga, wrap ou reset do modelo. A caracterização passou na primeira execução comportamental, portanto não é apresentada como Red de produção. A opção alternativa de limpar a seleção continua comprovada deterministicamente, e M3.5 passou a persistir a preferência de fim; M3.3 permanece parcial por seus outros gates.

O recorte de erro/recuperação acrescenta quatro linhas data-driven sem ampliar o corpus: o teste deriva prefixos temporários de 64 bytes das fontes autenticadas MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis e os abre sob nome Unicode. O extrator devolve `InvalidData`; playback entra em erro, inclusive quando o M4A truncado é carregado sem faixa de áudio após uma confirmação single-shot de 100 ms; o presenter conserva fallback/contexto Unicode e troca a ação primária para reabrir. O contrato posterior tornou qualquer snapshot `Error` atual terminal para o start pendente; apenas `SourceUnavailable`, `InvalidMedia` e `AccessDenied` classificam a ocorrência local como indisponível. A seleção válida seguinte alcança `Playing`, limpa o erro de metadata e conserva somente a geração/origem atual; uma troca imediata no adapter também comprova que callbacks do player truncado não contaminam a nova geração. Os derivados existem apenas no diretório temporário dos testes e não são fixtures canônicas de M0.5. O primeiro Green CTest passou **2/2**, com playback-lifecycle em **5,21 s** e integração em **1,42 s**; após corrigir a corrida entre descoberta de faixas e autoplay, o fechamento focado completo passou **17/17 em 11,87 s** com o build dos cinco alvos relevantes. Nenhum benchmark, A/B ou empacotamento foi executado nesse incremento.

O recorte de limite por campo usa tags temporárias escritas sobre uma cópia do MP3 autenticado, com texto multibyte válido. `MR-012` retém exatamente 16.384 bytes UTF-8; `MR-013` descarta integralmente 16.385 bytes, preserva a leitura e o fallback do stem e publica diagnóstico tipado com campo, tamanho observado e limite, sem carregar o conteúdo hostil. A mesma política é aplicada a título, artista e álbum. O Red observou o campo excedente ainda retido e nenhum diagnóstico; o Green focado passou **33 asserções em dois casos**. O fechamento afetado compilou core, extrator, presenter e integração e passou **103/103 em 9,01 s**. Esses inputs derivados não são fixtures canônicas; o total de 256 KiB, arte e limites internos da alocação do TagLib continuam abertos. Nenhum benchmark, A/B, empacotamento ou automação nativa foi executado.

O recorte de capa embutida acrescenta uma policy C++ pura para 10 MiB codificados, 4.096 px por eixo, 16 MP e thumbnail de até 1.024 px, além de extração unificada `PICTURE` pelo TagLib em cópias temporárias de MP3, M4A/AAC, FLAC e Ogg/Vorbis autenticados. O Red inicial observou capa PNG válida ausente e arte corrompida sem diagnóstico; outro Red mostrou BMP aceito fora da allowlist PNG/JPEG. O Green gera RGBA8888 no worker, reduz 2.048 × 1.024 para 1.024 × 512 e descarta sem falhar a leitura arte corrompida, formato não permitido, 10 MiB + 1, 4.097 px ou 16.004.000 pixels. Na apresentação, um Red de compilação antecedeu o cache/provider; o resultado usa `image://` com ID opaco por geração, ownership compartilhado sem copiar os pixels para sinais QML, carregamento assíncrono e placeholder imediato. Abertura aceita limpa a capa anterior; abertura rejeitada a preserva; geração antiga não volta a ser servida. O fechamento recompilou core, extrator, presenter, Quick, integração e executável e passou **116/116 em 17,24 s**; o extrator passou **508 asserções em 22 casos** e a policy **13 em quatro casos**. `MR-006`, `MR-007`, `MR-009` e `MR-010` continuam apenas no nível aritmético: faltam contêiner + TagLib + decoder imediatamente abaixo e exatamente nos limites. Os derivados não mudam o manifesto nem o catálogo. Além disso, TagLib materializa a tag/capa antes do gate de 10 MiB; o limite protege decode/retenção do produto, não a pré-alocação interna do parser, portanto NFR-LIMIT-001/SEC-002 permanecem parciais. Nenhum benchmark, A/B, empacotamento ou automação nativa foi executado.

O recorte M3.4 fecha a apresentação automatizada de detalhes no candidato Qt/Windows. O extrator captura a data de modificação na mesma tarefa de metadata, sem transformar falha de `last_write_time` em falha da leitura, e aplica uma policy de retenção com 16 KiB por campo e 256 KiB agregados sem underflow/overflow. A fronteira agregada é provada na policy; com somente título/artista/álbum retidos, o caminho TagLib atual chega no máximo a 48 KiB. O presenter projeta presença e valor separadamente para nome, caminho, tamanho, modificação UTC, tags, duração, contêiner, codec, bitrate, sample rate, canais e bit depth. Diagnósticos cruzam a boundary apenas por códigos e campos tipados, nunca pela mensagem livre do adapter; strings decimais paralelas preservam exatamente valores acima de `2^53` na UI. O painel sob demanda está disponível durante reprodução e erro, usa texto plano, “Não informado” para ausências, cópia explícita de caminho/diagnóstico, foco contido e visível no scroll, retorno seguro de foco, anúncio `Polite` e pares opacos em alto contraste. Reds específicos cobriram o opener ausente, ciclo de foco, item focado fora do viewport, controle diagnóstico removido, reconciliação após modal concorrente e perda de precisão. O fechamento passou build Release completo, **154/154** testes em **30,56 s**, core sem Qt **118/118** em **1,66 s**, Quick **42/42** nas escalas padrão, 1,5× e 2×, `qmllint`, formato de **74 C++/33 QML**, validação dos **44 Markdown/83 requisitos/17 checks/76 etapas** e `git diff --check`. Nenhum benchmark foi executado. Isso torna M3.4 funcional no candidato, mas não substitui NVDA nem a inspeção nativa de escala/alto contraste; esses gates permanecem abertos e a stack continua proposta.

M3.5 está funcional no candidato Qt/Windows no recorte de armazenamento, sem encerrar os gates globais de M3 nem aceitar a stack. As dez preferências são carregadas antes da apresentação e persistidas em JSON `schemaVersion: 1`, limitado a 64 KiB, com debounce trailing de 500 ms e um único writer fora da thread visual. O adapter usa `QSaveFile` sem fallback direto, mantém no máximo um backup e impede sobrescrita automática se primário ou backup trouxer versão futura; documento inválido recupera do backup ou usa defaults com aviso. A saída suja solicita gravação imediata, bloqueia novas ações e só se confirma após sucesso; falha, versão futura ou deadline visual de 2 s restauram a janela e deixam retry/substituição acionáveis. O teste de reinício executa duas vezes o executável Release instrumentado com `BUILD_TESTING=ON`, hooks de teste, plataforma offscreen e raiz isolada com espaços/Unicode; não prova pacote, desktop nativo, NVDA, primeiro frame sem flash ou comportamento do build de produção. O timeout visual não cancela fisicamente uma chamada síncrona já ativa, e o shutdown do writer ainda faz `join()` sem deadline se o armazenamento não retornar. A injeção de falhas antes/depois do commit não equivale a perda real de energia. `restoreLastSession` e `startMinimized` têm somente persistência/round-trip neste recorte; seus efeitos pertencem a M4.1 e M4.4. Como este é o primeiro schema, nenhuma migração histórica foi exercitada. Nenhum benchmark foi executado para esta fatia.

## 3. Check obrigatório de todo incremento

### 3.1 Antes de alterar

1. Executar `git status --short --branch`.
2. Ler os requisitos, ADRs e a etapa deste plano afetados.
3. Declarar o resultado observável e os IDs cobertos.
4. Identificar fixture, fake e boundary necessários.
5. Revisar licença e manutenção antes de adicionar dependência.
6. Confirmar que a tarefa não implementa como definitiva uma decisão ainda `Proposto`. A única exceção é um spike explicitamente autorizado cujo objetivo seja produzir evidência para resolver essa proposta.

### 3.2 Red–Green–Refactor

1. **Red:** executar o teste focado e confirmar falha pelo motivo correto.
2. **Green:** executar o mesmo teste e confirmar sucesso após a menor implementação.
3. **Refactor:** executar a suíte do módulo antes/depois da melhoria.
4. Não commitar estado Red na branch principal.
5. Registrar no handoff o teste Red e o motivo relevante da falha.

### 3.3 Antes do commit

1. Executar format, análise estática e testes focados.
2. Executar a suíte do módulo e o gate proporcional.
3. Atualizar requisito, ADR, matriz de formatos e evidência quando aplicável.
4. Executar `git diff --check`.
5. Revisar o diff integral e os arquivos staged.
6. Criar commit atômico.
7. Executar `git show --check --stat HEAD`.
8. Confirmar `git status --short --branch` sem mudanças próprias.

## 4. Catálogo de checks

Os nomes abaixo são contratos lógicos. Hoje `CHK-DOC` e a parcela Git de `CHK-CLOSE` são executáveis pelo validador versionado. M0 cria runners separados por candidato; M1 cria o comando raiz `validate` da stack vencedora. Um comando futuro nunca pode ser relatado como executado antes de existir no repositório.

| ID | Check | Evidência mínima |
|---|---|---|
| `CHK-PREFLIGHT` | Estado Git, IDs/requisitos, ADRs, escopo e dependências | comando, branch/commit e decisão de escopo |
| `CHK-DOC` | Links relativos, arquivos indexados, IDs únicos, estados ADR e `git diff --check` | zero link quebrado, zero ID duplicado/desconhecido, diff limpo |
| `CHK-TRACE` | requisito → etapa → teste → evidência | linha por requisito afetado, sem P0 órfão |
| `CHK-TDD` | Red–Green–Refactor | nome/comando do teste Red, Green e suíte após refactor |
| `CHK-FAST` | format, lint/análise, unitários, contratos rápidos e build/check | runner da stack com exit code zero |
| `CHK-ARCH` | domínio/application independentes de toolkit/backend; boundaries estreitos | build/test do core isolado e revisão de dependências |
| `CHK-CORPUS` | manifesto, SHA-256, licença/receita, capacidades e tolerâncias | fixture validada e matriz por candidato/commit |
| `CHK-UI` | estados, foco, layout, temas, fila e estabilidade visual | testes de componente e roteiro visual identificado |
| `CHK-A11Y` | teclado, leitor de tela, alto contraste, escala e movimento reduzido | formulário manual com build, SO, tecnologia assistiva e resultado |
| `CHK-PERF` | Release, ambiente controlado, amostras, CPU/memória/latência e regressão | dados brutos ou hash, P50/P95, baseline e método |
| `CHK-SEC` | threat model, input hostil, boundary, advisories e licenças | controles aplicáveis SEC-001–018, inventário e exceções |
| `CHK-WIN` | single-instance, tray, SMTC, defaults, lifecycle e pacote Windows | smoke em build empacotado/ambiente descartável |
| `CHK-LINUX` | build, MPRIS, MIME, tray/fallback, Wayland/X11 e `.deb` | ambiente de referência e matriz sem extrapolação |
| `CHK-PACKAGE` | instalação, upgrade, uninstall e dependências reais | manifesto do pacote, diff de sistema escopado e dados preservados |
| `CHK-E2E` | jornadas críticas empacotadas | cenário, fixture, ambiente e resultado |
| `CHK-RELEASE` | escopo, bugs, rastreabilidade, SBOM, assinatura, hashes e tag | checklist de release completo e autorização explícita |
| `CHK-CLOSE` | diff, commit, working tree e handoff | commit, `git show --check --stat`, status limpo e riscos |

### 4.1 Check documental executável agora

```powershell
git status --short --branch
& .\scripts\validate-docs.ps1
```

O runner valida arquivos contratuais, links relativos, IDs e contagens de requisitos, matriz 83/83, catálogo de checks, etapas com `CHK-CLOSE`, estados ADR e whitespace do diff rastreado/staged. M1 deve incorporá-lo ao comando raiz `validate`, sem duplicar sua lógica.

### 4.2 Regras de performance

Os budgets dos requisitos são vigentes. Tolerâncias operacionais que ainda não aparecem nos requisitos ou em ADR aceito são **proposta inicial de protocolo**: M0.0 precisa congelá-las em commit antes da coleta, e qualquer alteração posterior invalida a comparação já iniciada.

- Build Release identificado por commit e toolchain.
- Máquina, SO, energia, display scale e dispositivos registrados.
- Primeira execução, cold start e execução aquecida separados.
- Pelo menos 20 amostras válidas por candidato e cenário curto.
- Descarte de amostra somente por regra pré-registrada, com motivo; limite inicial proposto de 10%.
- Métrica primária de RAM multiprocesso: soma de private working set; private bytes e memória compartilhada relatados separadamente.
- CPU medida por intervalo e para toda a árvore do app.
- Alerta inicial de regressão em 5%; bloqueio após confirmação em 10% ou ao violar teto absoluto.
- Proposta para NFR-PERF-007: após 30 minutos de estabilização, amostrar a soma de private bytes da árvore a cada 60 s por oito horas. Não pode haver crash/deadlock; underrun atribuível ao app tem meta zero, ou tolerância por dispositivo/backend congelada antes da coleta. “Sem crescimento sustentado” exige estimativa de slope ≤ 1 MiB/h, limite inferior do intervalo de confiança de 95% ≤ 0 **e** limite superior ≤ 1 MiB/h; intervalo mais amplo é inconclusivo/no-go, e crescimento positivo estatisticamente sustentado falha. A mediana dos 30 minutos finais deve satisfazer `final_median <= initial_median + max(initial_median * 0.10, 10 MiB)`; handles e threads não podem ter tendência monotônica. Esses números tratam ruído de medição, não autorizam leak, e só passam a valer após aprovação M0.0.

### 4.3 Evidência e exceções

Cada resumo de marco registra:

```text
step_id
requirements
commit
environment
red_test_and_expected_failure
checks_run
manual_checks
artifacts_or_hashes
checks_skipped_and_reason
residual_risk
decision
next_step
```

Uma exceção de gate possui ID, requisito afetado, aprovador, justificativa, mitigação e expiração. “Risco aceito” sem esses campos não fecha etapa. Exceção não transforma requisito funcional P0 falho em concluído; o requisito precisa passar ou ser alterado/repriorizado explicitamente antes do RC.

## 5. Dependências e pontos de decisão

| Decisão | Prazo máximo | Bloqueia |
|---|---|---|
| Aprovar requisitos derivados usados no M0 e autorizar o experimento | fim de D0 | todo M0 |
| Escolher distribuição Linux de referência e versões Wayland/X11 | M0.0 | `CHK-LINUX`, decisão da stack |
| Registrar matriz Windows 11, Windows 10 legado e máquina de benchmark | M0.0 | comparação justa e suporte declarado |
| Definir licença do repositório e modelo local/público | antes de distribuir protótipo | licenças, pacote e release |
| Tratar NSIS como baseline experimental; aceitar tecnologia final após M0 | M0.9 | associação, upgrade e uninstall |
| Decidir se versão portátil entra no 0.1 | antes de M4 | pacote Windows |
| Confirmar fila e restauração de sessão como P0 | fim de D0 | escopo M1–M4 |
| Aprovar inventário e opções de todas as preferências do MVP | D0.9 | schema M1.9 e experiência M3/M4 |
| Congelar método, tolerâncias e descarte de benchmark | M0.0, antes de M0.1 | qualquer coleta comparativa |
| Registrar resultado/vencedor nos ADR-0001 e ADR-0002 ou criar ADR final | M0.10 | bootstrap M1 |
| Nome e identidade visual | antes do acabamento M3 | ícones, strings e pacote final |

NSIS é o baseline experimental para provar o modelo de Default Apps no Windows; não é decisão final do empacotador antes do M0. O tray/pacote básico de M0.2/M0.3 comprova paridade do shell; M0.9 executa a prova nativa aprofundada apenas nos candidatos ainda viáveis.

## 6. D0 — Fechamento documental

**Objetivo:** assegurar que o experimento começa com contrato revisado, sem confundir recomendação com decisão.

| ID | Entrega | Estado | Checks | Critério de saída |
|---|---|---|---|---|
| D0.1 | `agents.md`, commits e TDD como contrato | `[x]` | `CHK-DOC`, `CHK-CLOSE` | contrato foi o primeiro commit |
| D0.2 | visão, 83 requisitos e UX | `[x]` | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | 41 FR, 10 UX e 32 NFR definidos sem duplicata |
| D0.3 | análise Qt/Tauri, arquitetura e ADRs propostos | `[x]` | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | revisão documental coerente; nenhum gate de implementação alegado e nenhuma stack apresentada como aceita |
| D0.4 | estratégia TDD, gates e corpus especificado | `[x]` | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | checks e fixtures esperados documentados |
| D0.5 | roadmap, workflow, segurança e release | `[x]` | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | marcos e gates macro presentes |
| D0.6 | README, estado e índice | `[x]` | `CHK-DOC`, `CHK-CLOSE` | navegação documental válida |
| D0.7 | Qt principal no M0; Tauri desafiante A/B | `[x]` | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | NFR-STACK-001–006 e contratos alinhados |
| D0.8 | plano executável e matriz completa | `[x]` | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | todos os requisitos possuem destino explícito |
| D0.9 | revisão dos derivados e decisões abertas pelo mantenedor | `[x]` | [registro M0](m0/README.md), `CHK-DOC`, `CHK-CLOSE` | autorização explícita apenas para M0 |

### Gate D0

**Go alcançado:** D0.9 concluído; documentos commitados; ADRs de stack continuam `Proposto`. M0.0 deve agora fechar responsáveis, ambientes e protocolo antes de qualquer coleta.

**No-go:** criar scaffold de produto ou aceitar stack por preferência antes da evidência M0.

## 7. M0 — Spikes comparativos e decisão de stack

**Objetivo:** escolher uma combinação coerente de shell, UI, áudio, metadata e distribuição com evidência reproduzível.

Os IDs abaixo são o namespace executável. No roadmap macro, `M0-A` corresponde a M0.0–M0.4, `M0-B` a M0.5–M0.8, `M0-C` a M0.9 e `M0-D` a M0.10.

Código M0 é experimental, mas comportamento observável de shell, adapter, tray ou pacote continua seguindo TDD nos dois candidatos. Somente instrumentação descartável que mede API/ambiente, sem comportamento de produto, pode trocar `CHK-TDD` por validação determinística do próprio harness. Código promovido ao produto mantém seus testes e volta a passar pelo ciclo TDD do baseline; harness de medição não vira arquitetura por acidente.

### 7.1 Sequência

| ID | Entrega | Requisitos/decisões | Checks obrigatórios | Go/No-go |
|---|---|---|---|---|
| M0.0 | Fechar Linux de referência, matriz Windows, licença experimental, protocolo e layout temporário dos spikes | `[x]` [evidência](m0/evidence/m0-0.md); NFR-STACK-001–006 | `CHK-PREFLIGHT`, `CHK-DOC`, `CHK-SEC`, `CHK-CLOSE` | decisões/protocolo em `81f9f2f`; provisionamentos ausentes bloqueiam apenas a etapa dependente |
| M0.1 | Fixture comum de estado, fila de 1.000 itens, assets, roteiro, marcadores externos, templates de evidência e harness de coleta | `[x]` [`evidência`](m0/evidence/m0-1.md); preparação parcial de NFR-STACK-002–003 | `CHK-TDD`, `CHK-TRACE`, `CHK-DOC`, `CHK-CLOSE`; `CHK-PERF` N/A/não executado por ausência de candidato/medição, contrato preparado e execução real adiada para M0.4 | concluído em `24d12ea`; piloto repetível e não decisório; nenhum candidato recebeu cenário diferente |
| M0.2 | Shell C++20/Qt Quick/QML com backend falso, temas, slider, fila, preferências e tray básico | Em andamento; shell/bandeja implementados sob a [política comum](m0/README.md); pacote histórico MSVC 14.44 com smokes offscreen original/relocado aprovados; [SDK Qt composto autenticado](m0/evidence/m0-2-qt-composite-sdk.md), ainda sem build/package v2; validação nativa parcial e segurança pendente; [preflight concluído](m0/evidence/m0-2-preflight.md); FR-012, FR-014–015, FR-030, FR-040; UX-001–010 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-UI`, `CHK-A11Y`, `CHK-SEC`, `CHK-CLOSE` | Release medível; core sem Qt; zero bloqueio P0 |
| M0.3 | Shell Rust/Tauri/Svelte equivalente, sem áudio/rede real | mesmos IDs de M0.2 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-UI`, `CHK-A11Y`, `CHK-SEC`, `CHK-CLOSE` | Paridade funcional e de roteiro com Qt |
| M0.4 | Comparação cega ao resultado dos shells | NFR-PERF-001, NFR-PERF-004–006; NFR-STACK-001–004, NFR-STACK-006 | `CHK-PERF`, `CHK-A11Y`, `CHK-LINUX`, `CHK-PACKAGE`, `CHK-TRACE`, `CHK-CLOSE` | Eliminar candidato com P0, build ausente ou budget sem exceção |
| M0.5 | Corpus licenciado, manifesto, receitas, hashes e catálogo canônico de casos `PlaybackBackend`/`MetadataReader` | Em andamento: WAV e codecs básicos autenticados; prefixos temporários de 64 bytes exercitam truncamento em quatro codecs, tags temporárias exercitam o limite por campo e capas temporárias exercitam extração/rejeição, mas não são fixtures canônicas; fixtures versionadas de arte e truncamento e bordas reais abaixo/exatamente nos limites pendem. O limite agregado de metadata está provado na policy; o adapter atual não o alcança porque retém no máximo três campos de 16 KiB. FR-002, FR-005, FR-020–024; NFR-LIMIT-001–003, NFR-REL-001, NFR-REL-006 | `CHK-CORPUS`, `CHK-SEC`, `CHK-DOC`, `CHK-CLOSE` | Expectativas e tolerâncias versionadas; nenhum arquivo pessoal/comercial |
| M0.6 | Adapter Qt Multimedia/FFmpeg + TagLib no harness comum | Em andamento: [slices funcionais](m0/evidence/m0-6-qt-audio-first-slices.md) cobrem lifecycle/eventos tipados e geração isolada; `PB-001`/`PB-006` percorrem WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis, `PB-004` cobre MP3 VBR/M4A AAC e os demais casos já documentados preservam `PB-002`, `PB-003`, `PB-007`–`PB-012` e `PB-101`–`PB-103`; `MR-001`, `MR-003`, `MR-005`, `MR-008`, `MR-011`–`MR-013`, `MR-018`, `MR-019` e `MR-101`–`MR-103` cobrem a matriz básica por conteúdo, tags Unicode, fallback, limites experimentais e hostis existentes; a retenção agregada de 256 KiB é provada por policy, mas o caminho integrado atual chega no máximo a 48 KiB; `MR-006`/`MR-007`/`MR-009`/`MR-010` ainda têm somente policy aritmética. Opus/ALAC têm apenas uma caracterização separada `UnsupportedFormat` no extrator, não rejeição ponta a ponta; o reader limitado cobre polling tipado, `MR-016`, `MR-017` e `MR-020`; a composição aplicação/QML oferece sete extensões em caixa baixa/alta, percorre nove linhas válidas sobre sete fixtures e apresenta capa por provider opaco/assíncrono isolado pela geração. Quatro regressões adicionais usam derivados temporários truncados para cobrir erro tipado, M4A sem faixa de áudio, contexto Unicode/reabertura, recuperação e descarte de geração antiga em metadata/playback/integração; tags e capas temporárias não substituem o corpus canônico. Confirmação de rejeição do engine em `PB-005`, automação do diálogo, comportamento ponta a ponta de Opus/ALAC, hostis/capas canônicos, bordas reais abaixo/exatamente nos limites, pré-alocação segura do parser e áudio audível pendentes; FR-001–002, FR-010, FR-012, FR-014, FR-020–024; NFR-STACK-005 | `CHK-TDD`, `CHK-FAST`, `CHK-CORPUS`, `CHK-PERF`, `CHK-SEC`, `CHK-CLOSE` | Pacote usa backend/plugins esperados; erros controlados |
| M0.7 | Adapter Rodio/CPAL/Symphonia + Lofty no mesmo harness | mesmos IDs de M0.6 | `CHK-TDD`, `CHK-FAST`, `CHK-CORPUS`, `CHK-PERF`, `CHK-SEC`, `CHK-CLOSE` | Uma cadeia Symphonia; sem bloqueio inaceitável do callback |
| M0.8 | Comparação de áudio, 500 trocas/seeks, armazenamento lento e soak de oito horas nos ramos viáveis | NFR-PERF-002–003, NFR-PERF-007; NFR-REL-001–002; NFR-STACK-002–003, NFR-STACK-005–006 | `CHK-CORPUS`, `CHK-PERF`, `CHK-SEC`, `CHK-TRACE`, `CHK-CLOSE` | Formatos P0, estabilidade e cancelamento passam |
| M0.9 | Integração/pacote comparável e prova aprofundada do finalista | FR-004, FR-030–038; NFR-LIMIT-004, NFR-PRIV-001 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-PACKAGE`, `CHK-A11Y`, `CHK-SEC`, `CHK-CLOSE` | Single-instance, tray, pacote e caminho nativo viáveis |
| M0.10 | Combinar UI+áudio, reexecutar budgets e decidir | NFR-PERF-001–007, NFR-STACK-001–006 | todos os checks M0, `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | ADR registra vencedor, ambiente, números, riscos e alternativas |

### 7.2 Checks específicos do shell

- Qt: CMake Release reproduzível, format/check C++/QML, Catch2/CTest, Qt Test/Quick Test aplicável, `qmllint`, plugins/DLLs inventariados, nomes/roles acessíveis e nenhuma dependência Qt em domain/application.
- Tauri: `cargo fmt`, Clippy com warnings negados, testes Rust, install congelado do frontend, format/lint/typecheck/test/build, CSP/capabilities mínimas, IPC estreito/coalescido, nenhuma navegação remota e árvore WebView2 completa nas métricas.
- Ambos: janela acionável P95 ≤ 800 ms, CPU pausada média < 0,5%, private working set ocioso < 80 MiB, fila de 1.000 itens sem bloqueio > 100 ms, jornada NVDA/teclado/contraste/100–200%/movimento reduzido e smoke Linux. Input-to-visible é registrado como diagnóstico do componente visual; NFR-PERF-003 só fecha no ensaio de áudio/combinação.

### 7.3 Checks específicos de áudio

- Mesmo manifesto, fixtures, IDs, dispositivo e método para ambos.
- MP3 CBR/VBR, M4A/AAC-LC, FLAC, WAV e Ogg/Vorbis recebem resultado explícito; Opus/ALAC permanecem candidatos.
- Open-to-audible P95 ≤ 750 ms.
- Seek-to-audible P95 ≤ 250 ms ou exceção por codec documentada antes da aceitação.
- Zero crash reproduzível com truncados, extensão enganosa, tags/capas inválidas e limites abaixo/no/acima.
- Evento `ended` uma vez, unload/shutdown idempotentes e resultado antigo descartado.
- Toda leitura síncrona não cancelável é limitada e não acumula jobs/handles.
- Underrun em reprodução estável é sempre diagnosticado. Depois de M0.0 congelar a meta zero ou uma tolerância explícita por dispositivo/backend, qualquer violação elimina o backend ou exige prefetch/ring buffer limitado e nova medição completa.

### 7.4 Decisão M0

1. Se apenas um candidato passa, combinar e medir novamente antes de aceitar.
2. Se os dois passam dentro das tolerâncias, decidir por UX comprovada, acessibilidade, manutenção pessoal, segurança, licenciamento e integração.
3. Se ambos falham por footprint do shell, solicitar autorização para spike Slint equivalente.
4. Se a falha é apenas do áudio, revisar o backend sem trocar automaticamente a UI.
5. Se um requisito é inviável, voltar ao mantenedor; não relaxar orçamento silenciosamente.
6. Atualizar ADR-0001 e ADR-0002 com seção de resultados e vencedor, ou criar ADR final que os substitua.
7. Remover o protótipo perdedor ou preservá-lo fora do produto com motivo explícito; não manter dois toolchains/engines na release.

### Gate M0

**Go:** uma combinação cumpre UX, acessibilidade, formatos P0, Windows Tier 1, smoke Linux, segurança/licenças e budgets vigentes ou exceções aprovadas; ADR final commitado.

**No-go:** iniciar M1 com ADR ainda `Proposto`, evidência incomparável ou protótipos misturados.

## 8. M1 — Fundação TDD da stack vencedora

**Objetivo:** criar a menor unidade de build/distribuição sustentável, com domínio puro, fakes e validação automatizada antes das features reais.

ADR-0003 não autoriza implementar `MediaSourceResolver` ou seu fake no MVP. M1 usa `LocalFile` tipado; a porta remota só entra após decisão específica em R1.

| ID | Incremento TDD | Requisitos principais | Checks obrigatórios | Critério de saída |
|---|---|---|---|---|
| M1.1 | Scaffold da stack vencedora, versões pinadas, lockfiles/manifests e uma unidade de build | NFR-MAINT-003 | `CHK-PREFLIGHT`, `CHK-FAST`, `CHK-SEC`, `CHK-CLOSE` | configure/build em Windows e Linux; nenhum pacote prematuro |
| M1.2 | Scripts raiz `format`, `lint`, `check`, `test`, `build`, `validate` e check documental | NFR-MAINT-001, NFR-MAINT-004–005 | `CHK-TDD` para o runner quando aplicável, `CHK-DOC`, `CHK-FAST`, `CHK-CLOSE` | um entrypoint `validate` executa checks rápidos reais |
| M1.3 | CI Windows/Linux para checks rápidos e artefatos diagnósticos | NFR-MAINT-004–005 | `CHK-FAST`, `CHK-DOC`, `CHK-CLOSE` | mesmos scripts locais/CI; commit identificável |
| M1.4 | Tipos `TrackId`, `Position`, `Duration`, `Volume`, saltos e erros tipados | FR-013–014, NFR-REL-001 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-TRACE`, `CHK-CLOSE` | invariantes/clamps/property tests verdes |
| M1.5 | Máquina de estados de playback e descarte por geração | FR-010, FR-014–015, FR-024 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-CLOSE` | transições válidas/inválidas e eventos antigos cobertos |
| M1.6 | Fila por ID, duplicatas, anterior/próxima e fim | FR-011, FR-016–017 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-TRACE`, `CHK-CLOSE` | property tests e navegação determinística verdes |
| M1.7 | Portas `PlaybackBackend`, `MetadataReader`, `SessionStore`, `PlatformIntegration`, relógio e IDs; fakes mínimos | FR-020–024, FR-030–038, FR-042–044 | `CHK-TDD`, `CHK-ARCH`, `CHK-FAST`, `CHK-CLOSE` | core compila/testa sem toolkit, SO, arquivo ou áudio real |
| M1.8 | Controller, comandos comuns, snapshots e coalescing | FR-012–016, FR-037, NFR-LIMIT-004 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-TRACE`, `CHK-CLOSE` | UI/tray/plataforma podem usar os mesmos casos de uso |
| M1.9 | Esquema versionado de preferências/sessão e política de redação, rotação e retenção de logs | FR-042–044, NFR-PRIV-002–003 | `CHK-TDD`, `CHK-SEC`, `CHK-FAST`, `CHK-CLOSE` | round-trip de todo o inventário; formatter sem conteúdo/path/URL sensível; limites de idade/bytes/arquivos declarados e testados abaixo/no/acima |
| M1.10 | Boundary/presenter e shell real consumindo somente fakes | UX-001–008, UX-010 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-ARCH`, `CHK-E2E`, `CHK-CLOSE` | jornada visual falsa; sem regra de negócio na UI |

### Gate M1

- `validate` verde localmente e na CI.
- Domain/application cobertos inicialmente por ≥ 90% lines/85% branches e ≥ 85%/80%, respectivamente; utilities de presentation atingem ≥ 85%/80% quando M1.10 as introduzir. Ferramenta e formato do relatório são registrados no bootstrap.
- Testes não usam sleeps, rede, arquivos pessoais ou dispositivo real.
- UI roda contra fake e o core roda sem toolkit/backend.
- Rastreabilidade bidirecional existe para todo requisito iniciado.
- Working tree limpa e documentação corresponde à estrutura real.

## 9. M2 — Vertical slice de reprodução local

**Objetivo:** abrir um arquivo local real e concluir a jornada abrir → tocar → seek → pausar com uma faixa.

FR-013 só fica completo quando os valores configuráveis forem persistidos em M3. M2 prova defaults, validação e comportamento. FR-022 prova extração em M2 e só fecha visualmente com o painel de detalhes em M3.

| ID | Incremento TDD | Requisitos principais | Checks obrigatórios | Critério de saída |
|---|---|---|---|---|
| M2.1 | Seletor nativo com multiseleção, filtros validados, `LocalFile`, detecção por conteúdo e erro por extensão/arquivo inválido | FR-001–002, FR-005–006 | `CHK-TDD`, `CHK-CORPUS`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE` | um e vários arquivos válidos abrem/entram na fila; inválido preserva contexto; filtros refletem matriz |
| M2.2 | Adapter real load/play/pause/unload e capacidades | FR-010, NFR-REL-001–002 | `CHK-TDD`, `CHK-FAST`, `CHK-CORPUS`, `CHK-PERF`, `CHK-CLOSE` | MP3/M4A P0 do corpus passam no pacote de preview |
| M2.3 | Posição/duração, snapshots e progresso sem polling excessivo | FR-012, NFR-PERF-003–004 | `CHK-TDD`, `CHK-UI`, `CHK-PERF`, `CHK-E2E`, `CHK-CLOSE` | UI difere do engine ≤ 250 ms sem loop desnecessário |
| M2.4 | Click/drag seek, preview, confirmação única e saltos ±5 s | FR-013–015 | `CHK-TDD`, `CHK-UI`, `CHK-CORPUS`, `CHK-PERF`, `CHK-E2E`, `CHK-CLOSE` | seek pausa/limites/non-seekable corretos; latência dentro do budget |
| M2.5 | Volume/mudo e restauração lógica do último volume | FR-010–011 | `CHK-TDD`, `CHK-UI`, `CHK-E2E`, `CHK-CLOSE` | 0–100%, mute idempotente e sem amplificação |
| M2.6 | Metadata/propriedades/capa, limites e descarte de resultado antigo | FR-020–024, NFR-LIMIT-001–003 | `CHK-TDD`, `CHK-CORPUS`, `CHK-SEC`, `CHK-PERF`, `CHK-CLOSE` | abaixo/no/acima dos limites; UI nunca bloqueia nem recebe estado antigo |
| M2.7 | Estados vazio/loading/ready/playing/paused/error e preview instalável Windows | UX-001–003, UX-008–009 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-E2E`, `CHK-PACKAGE`, `CHK-CLOSE` | jornada principal empacotada, erro acionável e geometria estável |

### Gate M2

- `CHK-E2E`: abrir → tocar → buscar → pausar em build empacotado.
- Open-to-audible e seek-to-audible dentro dos budgets no hardware de referência.
- Nenhum I/O pesado na thread visual; detector de stall sem violação.
- Matriz publica apenas capacidades comprovadas por fixture/commit.
- Nenhum item M2 declara persistência ou painel técnico concluído antes de M3.

## 10. M3 — Experiência completa do player

**Objetivo:** transformar o slice em um player agradável e completo para o uso diário local.

| ID | Incremento TDD | Requisitos principais | Checks obrigatórios | Critério de saída |
|---|---|---|---|---|
| M3.1 | Fila visual: adicionar, tocar agora, reordenar, remover, limpar e duplicatas por ID | FR-016 | `CHK-TDD`, `CHK-FAST`, `CHK-UI`, `CHK-E2E`, `CHK-CLOSE` | operações e foco após reordenação/remoção cobertos |
| M3.2 | Múltiplos arquivos, drag-and-drop e enumeração incremental de diretórios — funcional no candidato Qt/Windows; gate externo de responsividade e volume lento pendentes | FR-003, FR-006 | `CHK-TDD`, `CHK-UI`, `CHK-PERF`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE` | válidos continuam com inválidos; nenhum bloqueio > 100 ms |
| M3.3 | Anterior/próxima, regra de 3 s, ended, salto de indisponíveis e comportamento aprovado para fim da fila | FR-011, FR-017, FR-042 | `CHK-TDD`, `CHK-CORPUS`, `CHK-E2E`, `CHK-CLOSE` | limites `≤3 s`/`>3 s`, cada opção de fim e erros cobertos |
| M3.4 | Painel de detalhes e diagnóstico copiável — funcional no candidato Qt/Windows; NVDA e inspeção nativa pendentes | FR-020–023 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-CLOSE` | FR-022 completo na UI; desconhecido nunca aparece como zero falso |
| M3.5 | Preferências reais e atômicas — armazenamento funcional no candidato Qt/Windows; efeitos de sessão/início minimizado, primeiro frame nativo, E2E empacotado e `CHK-SEC` amplo ainda abertos | FR-013, FR-040, FR-042, FR-044 | `CHK-TDD`, `CHK-UI`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE` | teste parametrizado cobre todo o inventário aprovado; 7 s/20 s persistem; corrupção usa backup/default; troca de tema sem flash/restart |
| M3.6 | Strings centralizadas, pt-BR, microcopy e erros acionáveis | UX-008–010 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-DOC`, `CHK-CLOSE` | nenhuma string essencial espalhada; layout suporta texto longo |
| M3.7 | Teclado/foco, atalhos, leitor de tela, escala, contraste e movimento reduzido | FR-037, UX-004–007 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-E2E`, `CHK-CLOSE` | zero controle P0 mouse-only, sem nome ou sem foco visível |
| M3.8 | Revisão UX, desempenho de 1.000 itens e convergência visual | UX-001–010, NFR-PERF-004, NFR-PERF-006 | `CHK-UI`, `CHK-A11Y`, `CHK-PERF`, `CHK-TRACE`, `CHK-CLOSE` | jornadas 1–4, 8–9 e decisão visual registradas |

### Gate M3

- Jornadas UX 1–4, 8 e 9 passam; jornada adicional prova troca de tema sem flash e persistência.
- NVDA manual identifica build, SO, versão, executor e resultado.
- Escalas 100/150/200%, high contrast e reduced motion sem bloqueador.
- 1.000 itens respeitam NFR-PERF-006.
- FR-013, FR-022, FR-040 e FR-042 possuem teste/evidência completos.

## 11. M4 — Desktop, sessão e recuperação

**Objetivo:** tornar o player um cidadão confiável do Windows e um aplicativo honestamente suportado no Linux de referência.

| ID | Incremento TDD | Requisitos principais | Checks obrigatórios | Critério de saída |
|---|---|---|---|---|
| M4.1 | Persistência atômica de fila/item/posição e restauração opcional sempre pausada | FR-042–044, NFR-REL-003 | `CHK-TDD`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE` | restore ligado/desligado, kill/crash/corrupção/replace falho recuperam sem áudio inesperado |
| M4.2 | Posição/tamanho da janela somente em monitor visível | FR-045 | `CHK-TDD`, `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` | monitor removido/DPI alterado não deixam janela inacessível |
| M4.3 | Instância única, CLI/shell e IPC limitado com paths Unicode/longos/múltiplos | FR-004, FR-033, NFR-LIMIT-004, NFR-REL-004 | `CHK-TDD`, `CHK-SEC`, `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` | 0/1/1.000/1.001 paths; espaços/aspas/metacaracteres; payload abaixo/no/acima de 1 MiB; dividir/rejeitar sem stall e sem shell concatenado |
| M4.4 | Tray, close/start behavior, menu e saída real; fallback Linux visível | FR-030–032, FR-042 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-A11Y`, `CHK-E2E`, `CHK-CLOSE` | fechar/iniciar minimizado respeitam capacidade/preferência; Sair salva, encerra e restaura pausado; Linux nunca fica invisível |
| M4.5 | SMTC/teclas de mídia no Windows e MPRIS no Linux usando casos de uso comuns | FR-037 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-CLOSE` | UI/tray/teclado/SO produzem a mesma semântica |
| M4.6 | ProgID/capabilities/Default Apps e `.desktop`/MIME/ícones | FR-034–036 | `CHK-TDD`, `CHK-SEC`, `CHK-WIN`, `CHK-LINUX`, `CHK-PACKAGE`, `CHK-CLOSE` | apenas formatos validados; nenhuma escrita em `UserChoice` |
| M4.7 | Suspend/resume, device/drive lost, arquivo removido e Explorer restart | FR-038, NFR-REL-005 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-CLOSE` | zero zumbi/estado impossível; cada falha oferece recuperação/ação |
| M4.8 | Install, upgrade e uninstall Windows; `.deb` install/remove Linux | NFR-REL-006, NFR-PRIV-001–003 | `CHK-TDD` para templates/helpers, `CHK-PACKAGE`, `CHK-WIN`, `CHK-LINUX`, `CHK-SEC`, `CHK-CLOSE` | mídia intocada; dados preservados/removidos conforme política; zero processo, serviço, tarefa, binário ou registro do ProgID próprio residual |

### Gate M4

- Jornadas UX 5–7 passam no Windows empacotado.
- Fault injection cobre antes/depois do replace e todas as versões de schema.
- Instância única e associações são testadas após instalação real.
- Matriz Tier 1/Tier 2 registra exatamente ambiente e limitações.
- Smoke offline comprova funcionamento sem rede. Captura ou deny de egress durante as jornadas, somada à inspeção de configuração/dependências, comprova ausência de tentativa de telemetria; testes de redação/retenção verificam que logs não expõem conteúdo, path ou URL completa por padrão.

## 12. M5 — Hardening e release 0.1

**Objetivo:** transformar o aplicativo integrado em uma release local confiável e, se autorizado, distribuível.

| ID | Incremento | Requisitos principais | Checks obrigatórios | Critério de saída |
|---|---|---|---|---|
| M5.1 | Congelar escopo 0.1, zerar bugs bloqueadores e completar requirement → test → evidence | todos P0/P1 vigentes | `CHK-TRACE`, `CHK-DOC`, `CHK-CLOSE` | cada P0 passa ou foi alterado/repriorizado em commit explícito antes do RC; cada P1 incluído passa e qualquer P1 adiado saiu formalmente do escopo |
| M5.2 | Corpus/matriz final e regressões de cada crash/bug encontrado | FR-002, FR-005, NFR-REL-001, NFR-REL-006 | `CHK-TDD`, `CHK-CORPUS`, `CHK-SEC`, `CHK-CLOSE` | cada correção inicia por regressão Red; formatos publicados por plataforma/backend/commit |
| M5.3 | Property, fuzz e mutation orientados a fila/seek/parsers/boundaries | NFR-REL-001–005 | `CHK-TDD`, `CHK-FAST`, `CHK-SEC`, `CHK-CLOSE` | zero crash conhecido; mutantes relevantes mortos/justificados |
| M5.4 | 500 trocas/seeks, soak integrado de oito horas e regressão de recursos | NFR-PERF-002–007 | `CHK-PERF`, `CHK-CORPUS`, `CHK-CLOSE` | budgets/tendência/handles/threads dentro do protocolo |
| M5.5 | Performance completa de cold start, áudio, UI e fila | NFR-PERF-001–006 | `CHK-PERF`, `CHK-TRACE`, `CHK-CLOSE` | P50/P95 publicados; regressão ≥10% confirmada bloqueia |
| M5.6 | Acessibilidade final Windows e smoke Linux | UX-001–010 | `CHK-UI`, `CHK-A11Y`, `CHK-E2E`, `CHK-CLOSE` | zero bloqueador P0; formulário manual completo |
| M5.7 | Advisories, licenças, Qt/FFmpeg ou Rust/JS, notices, SBOM e política final de logs | NFR-REL-006, NFR-PRIV-003 | `CHK-FAST`, `CHK-SEC`, `CHK-PACKAGE`, `CHK-CLOSE` | zero Critical/High explorável sem waiver; zero obrigação/licença não atendida; redação e retenção abaixo/no/acima dos limites verdes |
| M5.8 | Changelog, manual, troubleshooting, versão, pacote e RC | critérios MVP 1–11 | `CHK-PACKAGE`, `CHK-RELEASE`, `CHK-DOC`, `CHK-CLOSE` | RC instalada/atualizada/removida em ambiente limpo |
| M5.9 | Tag e artefatos finais; publicação somente após autorização | todos os gates | `CHK-RELEASE`, `CHK-CLOSE` | tag aponta ao commit validado; hashes após assinatura/pacote final |

### Gate M5 / release

- Bugs P0/P1 bloqueadores abertos: zero.
- Todos os critérios de aceitação vigentes 1–11 passam. P0 não recebe exceção genérica: qualquer inviabilidade exige alterar/repriorizar o requisito em commit explícito antes do RC; P1 adiado exige ajuste formal de escopo. Somente desvios de budget expressamente permitidos pelos requisitos usam ADR prévio.
- Windows Tier 1 completo; Linux Tier 2 limitado ao testado.
- Instalação, upgrade e uninstall validados em ambiente limpo.
- SBOM/notices/licenças e manifesto de artefato completos.
- Assinatura e malware scan quando a distribuição for pública.
- Rebuild duplo compara manifesto de conteúdo; diferenças fora da allowlist falham. Não alegar bit-for-bit sem medir.
- Redownload do artefato publicado, hash/assinatura e smoke, somente se o mantenedor autorizar publicação externa.

## 13. M6 — Evoluções opcionais 0.2

M6 não é um pacote único. Cada item exige necessidade após uso real, requisito refinado, UX, TDD, checks proporcionais e commit/ADR quando alterar arquitetura.

| ID | Candidato | Requisito | Checks mínimos |
|---|---|---|---|
| M6.1 | Escolha de dispositivo e recuperação de troca | FR-018 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-CLOSE`; capabilities e device lost |
| M6.2 | Shuffle | FR-019 | `CHK-TDD`, `CHK-UI`, `CHK-E2E`, `CHK-CLOSE`; property tests de ordem/IDs |
| M6.3 | Repeat | FR-019 | `CHK-TDD`, `CHK-UI`, `CHK-E2E`, `CHK-CLOSE`; modos off/faixa/fila e fim |
| M6.4 | Velocidade de reprodução | FR-019 | `CHK-TDD`, `CHK-UI`, `CHK-PERF`, `CHK-E2E`, `CHK-CLOSE`; precisão, áudio/CPU e reset |
| M6.5 | Editor de tema limitado a tokens | FR-041 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-SEC`, `CHK-CLOSE`; contraste e recovery |
| M6.6 | Playlists M3U8 | novo requisito | `CHK-TDD`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE`; round-trip, Unicode e erro parcial |
| M6.7 | Biblioteca por pastas/busca | novo requisito/ADR | `CHK-PREFLIGHT`, `CHK-DOC`, `CHK-PERF`, `CHK-CLOSE`; medir antes de considerar SQLite |
| M6.8 | Mini-player/notificações | novo requisito | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-WIN`, `CHK-LINUX`, `CHK-CLOSE`; lifecycle e preferência |

FR-025, edição de tags/capa, permanece backlog intencional sem marco. Só entra após pedido explícito, requisito, segurança de escrita e estratégia de backup.

## 14. R1 — Fontes remotas e YouTube

**Pré-condição:** release local estável ou decisão explícita de pesquisa isolada. ADR-0003 continua `Proposto` até essa autorização.

| ID | Pesquisa/experimento | Requisitos | Checks | Saída possível |
|---|---|---|---|---|
| R1.1 | Streams HTTP/podcasts autorizados | FR-051, NFR-PRIV-004 | `CHK-TDD`, `CHK-SEC`, `CHK-PERF`, `CHK-CLOSE` | adapter isolado ou rejeição |
| R1.2 | Threat model de rede, cache, redirects, SSRF e privacidade | FR-051–053 | `CHK-SEC`, `CHK-DOC`, `CHK-CLOSE` | controles/limites aceitos |
| R1.3 | Revisão jurídica/políticas do provedor | FR-050, FR-052 | fontes oficiais, licença, `CHK-DOC`, `CHK-SEC`, `CHK-CLOSE` | implementar, usar player oficial ou rejeitar |
| R1.4 | Comparar resolver próprio, libmpv e sidecar | FR-050–053 | `CHK-TDD` para comportamento do protótipo, `CHK-SEC`, `CHK-PERF`, `CHK-PACKAGE`, `CHK-CLOSE` | um candidato ou nenhum |
| R1.5 | Harness de timeout, cancelamento, live, privado, headers, expiração e sem seek | FR-053 | `CHK-TDD`, `CHK-E2E`, `CHK-SEC`, `CHK-CLOSE` | falhas remotas controladas |
| R1.6 | ADR final e protótipo sem distribuição | FR-050–053 | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE` | feature planejada, player oficial visível ou pesquisa encerrada |

Regras invariantes:

- Nenhuma URL é concatenada em shell.
- Falha remota nunca altera a disponibilidade de arquivos locais.
- Rede é explícita e cancelável.
- URL completa não aparece em log por padrão.
- YouTube audio-only em background/tray pode ser incompatível com políticas e pode ser rejeitado.

## 15. Matriz de rastreabilidade por marco

Repetição entre marcos é intencional: M0 prova viabilidade, M1 cria contratos, M2–M4 implementam e M5 revalida o produto empacotado.

| Marco | Requisitos sob responsabilidade | Evidência dominante |
|---|---|---|
| D0 | NFR-MAINT-004–005 | IDs, links, commits e plano |
| M0 shell | FR-012, FR-014–015, FR-030, FR-040; UX-001–010; NFR-PERF-001, NFR-PERF-004–006; NFR-STACK-001–004, NFR-STACK-006 | builds equivalentes, a11y e benchmark; resposta visual de NFR-PERF-003 é apenas diagnóstico |
| M0 áudio | FR-002, FR-005, FR-010, FR-012, FR-014, FR-020–024; NFR-PERF-002–003, NFR-PERF-007; NFR-STACK-002–003, NFR-STACK-005–006; NFR-REL-001–002, NFR-REL-006; NFR-LIMIT-001–003 | contrato comum, corpus, soak e licenças |
| M0 plataforma | FR-004, FR-030–038; NFR-REL-004–006; NFR-LIMIT-004; NFR-PRIV-001 | smokes empacotados Windows/Linux |
| M1 | FR-010–017, FR-020–024, FR-042–044; NFR-REL-001–005; NFR-LIMIT-001–004; NFR-PRIV-001–003; NFR-MAINT-001–005; base de UX-010 | TDD do core/ports/fakes e CI |
| M2 | FR-001–002, FR-005–006, FR-010–015, FR-020–024; UX-001–003, UX-008–009 e NFRs locais aplicáveis | adapters reais, corpus e E2E principal |
| M3 | FR-003, FR-011, FR-013, FR-016–017, FR-020–023, FR-040, FR-042, FR-044; UX-001–010; NFR-PERF-004, NFR-PERF-006 | fila, preferências, UX e acessibilidade |
| M4 | FR-004, FR-030–038, FR-042–045; NFR-REL-003–006; NFR-LIMIT-004; NFR-PRIV-001–003 | lifecycle, persistência e pacote real |
| M5 | todos os P0/P1 e NFRs do MVP | matriz individual teste/evidência e RC |
| M6 | FR-018–019, FR-041 e novos requisitos autorizados | incrementos independentes |
| Backlog | FR-025 | exclusão intencional documentada |
| R1 | FR-050–053, NFR-PRIV-004 | threat model, política, harness e ADR |

### 15.1 Destino individual dos 83 requisitos

Esta é a matriz humana inicial exigida por `CHK-TRACE`. “Destino” indica onde o requisito é primeiro provado ou deliberadamente descartado; não significa que o teste já exista. A partir de M1.2, o runner de rastreabilidade deve usar um manifesto legível por máquina com uma linha por ID, links para testes/evidências e estado (`planejado`, `em andamento`, `comprovado`, `excepcionado` ou `fora de escopo`). `Excepcionado` vale somente para gates/desvios que o contrato permita; P0 funcional falho precisa ser corrigido ou alterado/repriorizado antes do RC.

| Requisito | Destino responsável | Prova/decisão dominante |
|---|---|---|
| `FR-001` | M2.1 | `CHK-TDD`, seletor nativo e E2E com um/vários arquivos |
| `FR-002` | M0.5–M0.8, M2.1–M2.2, M5.2 | corpus e matriz de formatos por backend/commit |
| `FR-003` | M3.2 | drag-and-drop de arquivos/diretórios, erro parcial e stall |
| `FR-004` | M0.9, M4.3 | CLI/shell empacotado com Unicode, espaços, aspas, longos e metacaracteres |
| `FR-005` | M0.5–M0.8, M2.1, M5.2 | corpus corrompido/extensão enganosa sem crash |
| `FR-006` | M2.1, M3.2 | filtros derivados da matriz validada |
| `FR-010` | M1.5, M2.2, M2.5 | contratos e E2E play/pause/volume |
| `FR-011` | M1.6, M2.5, M3.3 | unitários/property e navegação/fim de faixa |
| `FR-012` | M0.2–M0.8, M1.8, M2.3–M2.4 | slider acessível, snapshot e seek real |
| `FR-013` | M1.4, M2.4, M3.5 | defaults/clamp e persistência 7 s/20 s |
| `FR-014` | M1.4–M1.5, M2.4 | limites, pausa e mídia não seekable |
| `FR-015` | M1.5, M2.4 | preview estável e um comando no release |
| `FR-016` | M1.6, M1.8, M3.1 | property tests e fila visual por IDs |
| `FR-017` | M1.6, M3.3 | casos em 3 s e transições anterior/reinício |
| `FR-018` | M6.1 | requisito refinado, capabilities e smoke por plataforma |
| `FR-019` | M6.2–M6.4 | incrementos separados para shuffle/repeat/velocidade |
| `FR-020` | M0.5–M0.8, M1.7, M2.6, M3.4 | corpus e painel com tamanho/duração |
| `FR-021` | M0.5–M0.8, M1.7, M2.6, M3.4 | tags/fallbacks contra corpus |
| `FR-022` | M0.5–M0.8, M2.6, M3.4 | extração e painel técnico sem zeros falsos |
| `FR-023` | M0.5–M0.8, M2.6, M3.4 | tags/arte hostis, sanitização e placeholder |
| `FR-024` | M0.5–M0.8, M1.5, M2.6 | geração/cancelamento e jobs limitados |
| `FR-025` | Backlog | exclusão intencional; nova autorização e estratégia de escrita |
| `FR-030` | M0.2–M0.4, M0.9, M4.4 | tray comparativo e E2E empacotado |
| `FR-031` | M0.9, M4.4 | menu completo, salvamento e saída real sem processo residual |
| `FR-032` | M0.9, M4.4 | tray/fallback Linux nunca invisível |
| `FR-033` | M0.9, M4.3 | instância única e encaminhamento de lote |
| `FR-034` | M0.9, M4.6 | tipos validados declarados no pacote |
| `FR-035` | M0.9, M4.6 | registry diff, ProgID/capabilities e zero `UserChoice` |
| `FR-036` | M0.9, M4.6 | `.desktop`, MIME e ícones no Linux de referência |
| `FR-037` | M1.8, M3.7, M4.5 | mesmos casos de uso em teclado/SMTC/MPRIS |
| `FR-038` | M0.9, M4.7 | fault injection de lifecycle/dispositivos/Explorer |
| `FR-040` | M0.2–M0.4, M3.5 | sistema/claro/escuro, sem restart/flash e com contraste |
| `FR-041` | M6.5 | editor limitado a tokens, contraste e recovery |
| `FR-042` | M1.7–M1.9, M3.3, M3.5, M3.7, M4.1, M4.4 | round-trip parametrizado de todo inventário e efeitos integrados |
| `FR-043` | M1.7–M1.9, M4.1 | fila/item/posição restaurados em pausa |
| `FR-044` | M1.7–M1.9, M3.5, M4.1 | migração, corrupção, backup e defaults |
| `FR-045` | M4.2 | monitor removido, DPI e janela sempre visível |
| `FR-050` | R1.3–R1.6 | revisão técnica/política e ADR; implementação pode ser rejeitada |
| `FR-051` | R1.1–R1.2, R1.4–R1.6 | adapter remoto isolado e falha sem efeito local |
| `FR-052` | R1.2–R1.4, R1.6 | fontes oficiais, licença/distribuição e ADR |
| `FR-053` | R1.2, R1.4–R1.6 | harness remoto, timeout/cancelamento e zero concatenação shell |
| `UX-001` | M0.2–M0.4, M1.10, M2.7, M3.8 | estado vazio e jornada de uma interação |
| `UX-002` | M0.2–M0.4, M1.10, M2.7, M3.8 | controles principais sempre visíveis |
| `UX-003` | M0.2–M0.4, M1.10, M2.7, M3.4, M3.8 | informação essencial e detalhes sob demanda |
| `UX-004` | M0.2–M0.4, M1.10, M3.7–M3.8, M5.6 | teclado, foco e nomes acessíveis |
| `UX-005` | M0.2–M0.4, M1.10, M3.7–M3.8, M5.6 | NVDA e política de anúncios sem spam |
| `UX-006` | M0.2–M0.4, M1.10, M3.7–M3.8, M5.6 | contraste, 100–200%, high contrast e reduced motion |
| `UX-007` | M0.2–M0.4, M1.10, M3.7–M3.8, M5.6 | semântica além de cor e estados dos controles |
| `UX-008` | M0.2–M0.4, M1.10, M2.7, M3.6, M3.8 | erro preserva contexto e oferece ação |
| `UX-009` | M0.2–M0.4, M2.7, M3.6, M3.8 | geometria estável e textos longos |
| `UX-010` | M0.2–M0.4, M1.10, M3.6, M3.8 | strings centralizadas em pt-BR e estrutura traduzível |
| `NFR-PERF-001` | M0.1–M0.4, M0.10, M5.5 | cold start externo P50/P95 do produto completo |
| `NFR-PERF-002` | M0.5–M0.8, M2.2, M5.4–M5.5 | open-to-audible P95 no corpus |
| `NFR-PERF-003` | M0.8, M0.10, M2.3–M2.4, M5.4–M5.5 | seek no áudio e resposta visual no produto combinado |
| `NFR-PERF-004` | M0.1–M0.4, M2.3, M3.8, M5.5 | CPU pausada e cadence do progresso |
| `NFR-PERF-005` | M0.1–M0.4, M0.10, M5.5 | private working set da árvore em 5 s/60 s/pico |
| `NFR-PERF-006` | M0.1–M0.4, M3.2, M3.8, M5.5 | 1.000 itens e bloqueio visual máximo |
| `NFR-PERF-007` | M0.8, M0.10, M5.4 | soak de oito horas pelo protocolo proposto em 4.2 e aprovado em M0.0 |
| `NFR-STACK-001` | M0.0–M0.4, M0.10 | A/B Qt/Tauri sem aceitação por linguagem |
| `NFR-STACK-002` | M0.0–M0.8, M0.10 | fixture/roteiro iguais e áudio separado do shell |
| `NFR-STACK-003` | M0.0–M0.10 | protocolo congelado, árvore completa e combinação final |
| `NFR-STACK-004` | M0.2–M0.4, M0.9–M0.10 | a11y/tray/Linux bloqueiam candidato |
| `NFR-STACK-005` | M0.5–M0.10 | baselines reais, boundaries e licenças inventariados |
| `NFR-STACK-006` | M0.4, M0.8–M0.10 | budgets/exceções e ADR com desempate explícito |
| `NFR-REL-001` | M0.5–M0.8, M1.4–M1.7, M2.1–M2.6, M5.2–M5.3 | corpus/fuzz e zero crash reproduzível conhecido |
| `NFR-REL-002` | M0.6–M0.8, M2.2, M5.4 | auditoria do callback e underrun controlado |
| `NFR-REL-003` | M1.9, M3.5, M4.1 | fault injection e escrita atômica recuperável |
| `NFR-REL-004` | M4.3, R1.5 | argumentos tipados e zero concatenação shell |
| `NFR-REL-005` | M4.7 | falhas de arquivo/drive/dispositivo acionáveis |
| `NFR-REL-006` | M0.5–M0.9, M4.8, M5.7 | licença de fixtures/dependências/codecs e SBOM |
| `NFR-LIMIT-001` | M0.5–M0.8, M2.6 | arte abaixo/no/acima e placeholder |
| `NFR-LIMIT-002` | M0.5–M0.8, M2.6 | metadata abaixo/no/acima e diagnóstico |
| `NFR-LIMIT-003` | M0.5–M0.8, M2.6 | timeout/cancelamento e nenhum acúmulo de jobs |
| `NFR-LIMIT-004` | M1.8, M4.3 | boundary com 0/1/1.000/1.001 paths e bytes abaixo/no/acima de 1 MiB |
| `NFR-PRIV-001` | M0.9, M4.8 | smoke offline, deny/captura de egress e inspeção de dependências |
| `NFR-PRIV-002` | M1.9, M4.1, M4.8 | persistência somente local e inspeção do pacote |
| `NFR-PRIV-003` | M1.9, M4.8, M5.7 | conteúdo/path/URL redigidos e rotação/retenção abaixo/no/acima |
| `NFR-PRIV-004` | R1.1–R1.6 | rede explícita/cancelável e threat model |
| `NFR-MAINT-001` | M1.2 e todo comportamento posterior | evidência Red–Green–Refactor em `CHK-TDD` |
| `NFR-MAINT-002` | M1.4–M1.10 e revisões M2–M5 | core isolado provado por `CHK-ARCH` |
| `NFR-MAINT-003` | M1.1 | uma unidade de build/distribuição e módulos internos |
| `NFR-MAINT-004` | D0.1–D0.9 e `CHK-CLOSE` de toda etapa | commit atômico, `git show --check` e status limpo |
| `NFR-MAINT-005` | D0.8, M1.2, M5.1 | matriz bidirecional e validador de IDs/evidências |

## 16. Lacunas que o plano fecha explicitamente

- FR-006: filtros do seletor entram em M2.1.
- FR-019: velocidade de reprodução aparece em M6.
- FR-025: permanece backlog intencional.
- FR-045: monitor removido/DPI entra em M4.2.
- UX-010: strings centralizadas entram em M1.10/M3.6.
- NFR-PRIV-003: redação, rotação e retenção de logs entram em M1.9 e são inspecionadas em M4/M5.
- NFR-MAINT-003: unidade única é verificada em M1.1.
- NFR-MAINT-005: matriz requisito → etapa → teste → evidência é obrigatória desde M1.
- FR-003, FR-004, FR-005, FR-011, FR-016, FR-017, FR-032, FR-037, FR-038, FR-040 e FR-053 recebem cenários explícitos.
- NFR-LIMIT-001–004 exigem casos abaixo, exatamente no limite e acima.
- Preferências reais saem de M4 e entram em M3; M4 mantém sessão completa e recuperação.
- Soak exploratório elimina cedo; oito horas é obrigatório em cada ramo M0 ainda viável e novamente no produto integrado M5.
- Tray/pacote básico compara shells; a integração nativa aprofundada ocorre em M0.9/M4.
- NSIS é baseline experimental, não decisão final antes do M0.

## 17. Próxima sequência segura

1. O mantenedor revisa os derivados e autoriza D0.9/M0 sem aceitar stack.
2. Fechar distribuição Linux, matriz Windows, licença experimental e protocolo de medição.
3. M0.1 fechou gates e commit antes de qualquer shell, preservando um cenário comum contra benchmark enviesado.
4. O preflight Qt/QML, `clang-format`, NVDA e o SDK Qt composto foram autenticados; para concluir o shell M0.2, reconstruir/empacotar contra a nova identidade, repetir smokes e gates nativos/segurança; depois executar M0.3 com o mesmo recorte.
5. Por orientação do mantenedor, priorizar integralmente as funcionalidades do caminho C++/Qt no Windows; Linux, A/B e benchmarks ficam depois da paridade funcional, sem apagar seus gates nem apresentar a stack como aceita. Tocar linha, CRUD/reordenação, entrada incremental, detalhes/diagnóstico e o armazenamento M3.5 das preferências estão verdes deterministicamente; `Ended`, válido → inválido → válido e fim sem circular também possuem caracterização Qt real. O próximo recorte é M3.6, com strings pt-BR centralizadas e erros acionáveis, enquanto corpus, limites e alegações de formato continuam sujeitos aos gates M0.

Não iniciar M1 enquanto M0.10 e o ADR final não estiverem commitados.

# Plano de desenvolvimento executável

**Estado:** baseline para execução e acompanhamento
**Versão alvo:** MVP 0.1, seguido de incrementos opcionais 0.2 e pesquisa R1
**Última revisão:** 2026-09-06
**Marco atual:** decisão de stack aceita pelo mantenedor em 2026-09-05: C++20 + Qt Quick/QML + Qt Multimedia/FFmpeg + TagLib, conforme [ADR-0005](architecture/decisions/0005-windows-product-stack.md). M0.10 registra essa escolha explícita, sem vitória A/B ou aprovação dos budgets; a próxima frente é M1, com promoção incremental do código existente para uma unidade de produto Windows. Essa promoção ainda precisa de implementação e validação próprias; os gates remanescentes de corpus, segurança, acessibilidade, integração, distribuição e desempenho não estão concluídos.

Este documento transforma o [roadmap](roadmap.md) em uma sequência executável. Ele incorpora o produto, UX, arquitetura, TDD, corpus, segurança e distribuição, preservando a comparação C++/Qt versus Rust/Tauri como histórico experimental, não como pré-requisito do produto aceito. Não substitui os [requisitos](product/requirements.md), os [ADRs](architecture/decisions/README.md) nem os [gates de qualidade](quality/quality-gates.md); detalha a ordem, os checks e a evidência necessária para cumpri-los.

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

### 2.2 Decisão vigente e limites

- A stack Windows está aceita pelo mantenedor no [ADR-0005](architecture/decisions/0005-windows-product-stack.md); ADR-0001 e ADR-0002 ficam substituídos. ADR-0003 continua `Proposto` e ADR-0004 continua `Aceito`.
- Existe código funcional C++/Qt, runtime e instalador local experimental sob `experiments/m0`; isso não equivale à promoção concluída de M1 nem à aprovação de release.
- A matriz local validada continua limitada aos formatos, runtime e evidências declarados; aceitar o engine não aprova codecs adicionais ou distribuição pública.
- A/B não foi demonstrado. Tauri/Rust e suas ferramentas ficam fora das dependências e checks do produto; o histórico comparativo não será repetido como condição de M1.
- Budgets continuam metas não comprovadas; benchmarks e Linux ficam adiados, sem alegação de sucesso ou eliminação de seus requisitos aplicáveis.
- Segurança/licenças, corpus hostil restante, jornadas nativas/acessíveis e distribuição continuam com seus gates próprios.
- YouTube continua pesquisa futura sem promessa de implementação.

### 2.3 Checkpoint atual — entrada em M1

O mantenedor resolveu a decisão de stack e autorizou a atualização dos contratos. Após o commit documental de M0.10/[ADR-0005](architecture/decisions/0005-windows-product-stack.md), iniciar o recorte de promoção definido na seção 8: reaproveitar o domínio, as portas, os adapters e os testes existentes, com uma unidade real de produto e fronteiras verificáveis. Um wrapper na raiz que apenas delega ao experimento não satisfaz esse critério. Não reiniciar as funcionalidades já implementadas nem declarar M1 inteiro concluído por essa escolha.

Os relatos abaixo preservam o estado e as evidências de cada incremento. Afirmações antigas de “stack proposta”, A/B obrigatório ou autorizações limitadas ao M0 descrevem aquele momento; a decisão e a sequência vigentes são esta seção, M0.10 e a seção 17.

A auditoria funcional posterior encontrou uma regressão de `FR-016`: o menu contextual podia acompanhar a nova faixa após avanço automático e agir sobre a ocorrência errada. O [recorte do menu da fila](../experiments/m0/candidates/qt/README.md#queue-menu-occurrence) mantém alvo por ID e invalidação segura. O incremento seguinte corrige o [feedback de caminhos inválidos antes do IPC](../experiments/m0/candidates/qt/README.md#invalid-shell-source-feedback), com rejeição atômica e mensagem segura, preservando hooks reservados silenciosos em produção. A [identificação do executável Windows](../experiments/m0/candidates/qt/README.md#windows-executable-identity) inclui ícone em sete tamanhos e propriedades experimentais, com leitura nativa ON/OFF e nomes/raízes preservados. A [entrega local atualizada](../experiments/m0/candidates/qt/windows-installer/README.md#local-delivery-refresh) incorpora esses três incrementos: runtime `a43faa1`, matriz/receita `28c1ff2`, áudio 18/18, leitura PE 5/5 e instalação/remoção isolada 1/1. O registro centraliza hashes e limites; não houve instalação no host ou benchmark. As pendências funcionais e de recuperação Windows continuam como foco; inspeção visual nativa, ativação real e os demais gates globais permanecem abertos.

Checkpoint Windows anterior: [matriz local e evolução de formatos do instalador](../experiments/m0/candidates/qt/README.md#rc-music-local-formats). A caracterização cobre seek pausado e reprodução integral até EOF, com 18/18 no runtime privado; o builder aceita somente declaração local explícita e vinculada, ficando OFF sem argumento. O protocolo v2 acrescenta/retira formatos, migra legado OFF e conserva retry/entradas externas. Planner 18/18, lifecycle 32/32 e associações 14/14 passaram após Reds e revisão. O README do instalador centraliza comandos, hashes e limites; os registros abaixo preservam os checkpoints anteriores. M4.6/M4.8 e aprovação pública seguem abertos, sem benchmarks.

#### Histórico anterior à aceitação da stack

A autoria documental e a revisão do mantenedor fecharam D0.9. A [autorização histórica D0.9](m0/README.md) permitia apenas M0.0–M0.10, quando Qt, Tauri e os backends ainda eram candidatos. O [ADR-0005](architecture/decisions/0005-windows-product-stack.md) substitui essa limitação para o desenvolvimento Windows atual.

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

O recorte M3.4 fecha a apresentação automatizada de detalhes no candidato Qt/Windows. O extrator captura a data de modificação na mesma tarefa de metadata, sem transformar falha de `last_write_time` em falha da leitura, e aplica uma policy de retenção com 16 KiB por campo e 256 KiB agregados sem underflow/overflow. A fronteira agregada é provada na policy; naquele recorte, somente título/artista/álbum participavam do orçamento, somando no máximo 48 KiB. O presenter projeta presença e valor separadamente para nome, caminho, tamanho, modificação UTC, tags, duração, contêiner, codec, bitrate, sample rate, canais e bit depth. Diagnósticos cruzam a boundary apenas por códigos e campos tipados, nunca pela mensagem livre do adapter; strings decimais paralelas preservam exatamente valores acima de `2^53` na UI. O painel sob demanda está disponível durante reprodução e erro, usa texto plano, “Não informado” para ausências, cópia explícita de caminho/diagnóstico, foco contido e visível no scroll, retorno seguro de foco, anúncio `Polite` e pares opacos em alto contraste. Reds específicos cobriram o opener ausente, ciclo de foco, item focado fora do viewport, controle diagnóstico removido, reconciliação após modal concorrente e perda de precisão. O fechamento passou build Release completo, **154/154** testes em **30,56 s**, core sem Qt **118/118** em **1,66 s**, Quick **42/42** nas escalas padrão, 1,5× e 2×, `qmllint`, formato de **74 C++/33 QML**, validação dos **44 Markdown/83 requisitos/17 checks/76 etapas** e `git diff --check`. Nenhum benchmark foi executado. Isso torna M3.4 funcional no candidato, mas não substitui NVDA nem a inspeção nativa de escala/alto contraste; esses gates permanecem abertos e a stack continua proposta.

M3.5 está funcional no candidato Qt/Windows no recorte de armazenamento, sem encerrar os gates globais de M3 nem aceitar a stack. As dez preferências são carregadas antes da apresentação e persistidas em JSON `schemaVersion: 1`, limitado a 64 KiB, com debounce trailing de 500 ms e um único writer fora da thread visual. O adapter usa `QSaveFile` sem fallback direto, mantém no máximo um backup e impede sobrescrita automática se primário ou backup trouxer versão futura; documento inválido recupera do backup ou usa defaults com aviso. A saída suja solicita gravação imediata, bloqueia novas ações e só se confirma após sucesso; falha, versão futura ou deadline visual de 2 s restauram a janela e deixam retry/substituição acionáveis. O teste de reinício executa duas vezes o executável Release instrumentado com `BUILD_TESTING=ON`, hooks de teste, plataforma offscreen e raiz isolada com espaços/Unicode; não prova pacote, desktop nativo, NVDA, primeiro frame sem flash ou comportamento do build de produção. O timeout visual não cancela fisicamente uma chamada síncrona já ativa, e o shutdown do writer ainda faz `join()` sem deadline se o armazenamento não retornar. A injeção de falhas antes/depois do commit não equivale a perda real de energia. Naquele recorte, `restoreLastSession` e `startMinimized` tinham somente persistência/round-trip; seus efeitos locais foram integrados posteriormente em M4.1 e M4.4. Como este é o primeiro schema, nenhuma migração histórica foi exercitada. Nenhum benchmark foi executado para esta fatia.

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

Os nomes abaixo são contratos lógicos. Hoje `CHK-DOC` e a parcela Git de `CHK-CLOSE` são executáveis pelo validador versionado. M0 criou runners experimentais; M1 compõe o comando raiz `validate` da stack aceita, sem incluir Tauri/Rust. Um comando futuro nunca pode ser relatado como executado antes de existir no repositório.

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

Os budgets dos requisitos são vigentes, mas não foram aprovados como cumpridos pela escolha de stack. Benchmarks estão adiados e não bloqueiam a entrada em M1. As regras comparativas abaixo pertencem ao protocolo histórico; uma futura medição do produto deve declarar seu escopo e ambiente sem exigir o ramo Tauri. Tolerâncias operacionais que ainda não aparecem nos requisitos ou em ADR aceito são **proposta inicial de protocolo**: M0.0 precisa congelá-las em commit antes da coleta, e qualquer alteração posterior invalida a comparação já iniciada.

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
| Retomar distribuição Linux de referência e versões Wayland/X11 | quando Linux for retomado | `CHK-LINUX` e alegação de suporte Linux; não a stack Windows aceita |
| Registrar matriz Windows 11, Windows 10 legado e máquina de benchmark | M0.0 | comparação justa e suporte declarado |
| Definir licença do repositório e modelo local/público | antes de distribuir protótipo | licenças, pacote e release |
| Tratar NSIS como baseline experimental; aceitar tecnologia final após M0 | M0.9 | associação, upgrade e uninstall |
| Decidir se versão portátil entra no 0.1 | antes de M4 | pacote Windows |
| Confirmar fila e restauração de sessão como P0 | fim de D0 | escopo M1–M4 |
| Aprovar inventário e opções de todas as preferências do MVP | D0.9 | schema M1.9 e experiência M3/M4 |
| Congelar método, tolerâncias e descarte de benchmark | M0.0, antes de M0.1 | qualquer coleta comparativa |
| Registrar a escolha explícita C++/Qt/Multimedia/FFmpeg/TagLib no ADR-0005 | M0.10; decisão recebida em 2026-09-05 | commit documental habilita a promoção M1; comparação não é pré-requisito |
| Preservar RC Music visível e MusicPlayer interno conforme ADR-0004 | decisão de nome já resolvida | mudanças futuras de identidade exigem decisão própria; sem migração implícita de dados |

NSIS permanece a receita experimental local de Default Apps/instalação já implementada; a aceitação da stack não concede aprovação pública de empacotamento. A paridade entre shells pertence ao histórico M0.2/M0.3; as provas nativas restantes seguem o caminho Windows em M4/M5, sem outro candidato obrigatório.

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

**Go histórico alcançado:** D0.9 autorizou apenas o experimento e M0.0 fechou seu protocolo. Essa decisão não aprovou medidas nem stack naquele momento.

**Estado vigente:** o [ADR-0005](architecture/decisions/0005-windows-product-stack.md) aceita a stack por decisão explícita posterior do mantenedor e permite a promoção M1. Continuam proibidos resultados comparativos inventados, gates declarados sem evidência e ampliação de escopo sem autorização.

## 7. M0 — Histórico dos spikes e decisão de stack

**Estado:** escolha Windows resolvida pelo [ADR-0005](architecture/decisions/0005-windows-product-stack.md), sem concluir retrospectivamente as etapas experimentais ou medições ausentes.

**Objetivo vigente:** preservar evidências e riscos dos spikes e registrar a decisão explícita que habilita M1. A/B, benchmarks e build Linux deixaram de ser pré-requisitos da escolha; qualidade e distribuição seguem seus gates próprios.

Os IDs abaixo são o namespace executável. No roadmap macro, `M0-A` corresponde a M0.0–M0.4, `M0-B` a M0.5–M0.8, `M0-C` a M0.9 e `M0-D` a M0.10.

Código ainda sob M0 mantém sua classificação experimental até a promoção deliberada. Comportamento observável de shell, adapter, tray ou pacote continua seguindo TDD no caminho Qt aceito; o ramo Tauri não integra mais a execução obrigatória. Somente instrumentação descartável que mede API/ambiente, sem comportamento de produto, pode trocar `CHK-TDD` por validação determinística do próprio harness. Código promovido ao produto mantém seus testes e volta a passar pelo ciclo TDD do baseline; harness de medição não vira arquitetura por acidente.

### 7.1 Sequência histórica e destino atual

M0.0–M0.9 conservam seus IDs e evidências. As linhas comparativas não executadas ficam arquivadas como protocolo, não como gates verdes nem tarefas obrigatórias antes de M1; seus riscos de produto relevantes continuam em M4/M5.

| ID | Entrega | Requisitos/decisões | Checks obrigatórios | Go/No-go |
|---|---|---|---|---|
| M0.0 | Fechar Linux de referência, matriz Windows, licença experimental, protocolo e layout temporário dos spikes | `[x]` [evidência](m0/evidence/m0-0.md); NFR-STACK-001–006 | `CHK-PREFLIGHT`, `CHK-DOC`, `CHK-SEC`, `CHK-CLOSE` | decisões/protocolo em `81f9f2f`; provisionamentos ausentes bloqueiam apenas a etapa dependente |
| M0.1 | Fixture comum de estado, fila de 1.000 itens, assets, roteiro, marcadores externos, templates de evidência e harness de coleta | `[x]` [`evidência`](m0/evidence/m0-1.md); preparação parcial de NFR-STACK-002–003 | `CHK-TDD`, `CHK-TRACE`, `CHK-DOC`, `CHK-CLOSE`; `CHK-PERF` N/A/não executado por ausência de candidato/medição, contrato preparado e execução real adiada para M0.4 | concluído em `24d12ea`; piloto repetível e não decisório; nenhum candidato recebeu cenário diferente |
| M0.2 | Shell C++20/Qt Quick/QML com backend falso, temas, slider, fila, preferências e tray básico | Em andamento; shell/bandeja implementados sob a [política comum](m0/README.md); pacote histórico MSVC 14.44 com smokes offscreen original/relocado aprovados; [SDK Qt composto autenticado](m0/evidence/m0-2-qt-composite-sdk.md), ainda sem build/package v2; validação nativa parcial e segurança pendente; [preflight concluído](m0/evidence/m0-2-preflight.md); FR-012, FR-014–015, FR-030, FR-040; UX-001–010 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-UI`, `CHK-A11Y`, `CHK-SEC`, `CHK-CLOSE` | Release medível; core sem Qt; zero bloqueio P0 |
| M0.3 | Shell Rust/Tauri/Svelte — fora da sequência do produto | Histórico, não executado; mesmos IDs de M0.2 | Históricos: `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-UI`, `CHK-A11Y`, `CHK-SEC`, `CHK-CLOSE` | Paridade funcional e de roteiro com Qt |
| M0.4 | Comparação dos shells — arquivada, não demonstrada e não exigida para M1 | NFR-PERF-001, NFR-PERF-004–006; NFR-STACK-001–004, NFR-STACK-006 | `CHK-PERF`, `CHK-A11Y`, `CHK-LINUX`, `CHK-PACKAGE`, `CHK-TRACE`, `CHK-CLOSE` | Eliminar candidato com P0, build ausente ou budget sem exceção |
| M0.5 | Corpus licenciado, manifesto, receitas, hashes e catálogo canônico de casos `PlaybackBackend`/`MetadataReader` | Em andamento: WAV e codecs básicos autenticados; prefixos temporários de 64 bytes exercitam truncamento em quatro codecs, tags temporárias exercitam o limite por campo e capas temporárias exercitam extração/rejeição, mas não são fixtures canônicas; fixtures versionadas de arte e truncamento e bordas reais abaixo/exatamente nos limites pendem. O limite agregado de metadata está provado na policy; o adapter atual não o alcança porque contabiliza no máximo 48 KiB + 8 bytes para três textos e dois números. FR-002, FR-005, FR-020–024; NFR-LIMIT-001–003, NFR-REL-001, NFR-REL-006 | `CHK-CORPUS`, `CHK-SEC`, `CHK-DOC`, `CHK-CLOSE` | Expectativas e tolerâncias versionadas; nenhum arquivo pessoal/comercial |
| M0.6 | Adapter Qt Multimedia/FFmpeg + TagLib no harness comum | Em andamento: [slices funcionais](m0/evidence/m0-6-qt-audio-first-slices.md) cobrem lifecycle/eventos tipados e geração isolada; `PB-001`/`PB-006` percorrem WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis, `PB-004` cobre MP3 VBR/M4A AAC e os demais casos já documentados preservam `PB-002`, `PB-003`, `PB-007`–`PB-012` e `PB-101`–`PB-103`; `MR-001`, `MR-003`, `MR-005`, `MR-008`, `MR-011`–`MR-013`, `MR-018`, `MR-019` e `MR-101`–`MR-103` cobrem a matriz básica por conteúdo, tags Unicode, fallback, limites experimentais e hostis existentes; a retenção agregada de 256 KiB é provada por policy, mas o caminho integrado atual chega no máximo a 48 KiB + 8 bytes; `MR-006`/`MR-007`/`MR-009`/`MR-010` ainda têm somente policy aritmética. Opus/ALAC têm apenas uma caracterização separada `UnsupportedFormat` no extrator, não rejeição ponta a ponta; o reader limitado cobre polling tipado, `MR-016`, `MR-017` e `MR-020`; a composição aplicação/QML oferece sete extensões em caixa baixa/alta, percorre nove linhas válidas sobre sete fixtures e apresenta capa por provider opaco/assíncrono isolado pela geração. Quatro regressões adicionais usam derivados temporários truncados para cobrir erro tipado, M4A sem faixa de áudio, contexto Unicode/reabertura, recuperação e descarte de geração antiga em metadata/playback/integração; tags e capas temporárias não substituem o corpus canônico. Confirmação de rejeição do engine em `PB-005`, automação do diálogo, comportamento ponta a ponta de Opus/ALAC, hostis/capas canônicos, bordas reais abaixo/exatamente nos limites, pré-alocação segura do parser e áudio audível pendentes; FR-001–002, FR-010, FR-012, FR-014, FR-020–024; NFR-STACK-005 | `CHK-TDD`, `CHK-FAST`, `CHK-CORPUS`, `CHK-PERF`, `CHK-SEC`, `CHK-CLOSE` | Pacote usa backend/plugins esperados; erros controlados |
| M0.7 | Adapter Rodio/CPAL/Symphonia + Lofty — fora da sequência do produto | Histórico, não executado; mesmos IDs de M0.6 | Históricos: `CHK-TDD`, `CHK-FAST`, `CHK-CORPUS`, `CHK-PERF`, `CHK-SEC`, `CHK-CLOSE` | Uma cadeia Symphonia; sem bloqueio inaceitável do callback |
| M0.8 | Protocolo histórico de comparação/soak — não demonstrado; estabilidade e budgets do produto seguem em M5 | NFR-PERF-002–003, NFR-PERF-007; NFR-REL-001–002; NFR-STACK-002–003, NFR-STACK-005–006 | `CHK-CORPUS`, `CHK-PERF`, `CHK-SEC`, `CHK-TRACE`, `CHK-CLOSE` | Formatos P0, estabilidade e cancelamento passam |
| M0.9 | Integração/pacote comparável históricos; provas Windows restantes seguem em M4/M5, Linux adiado | FR-004, FR-030–038; NFR-LIMIT-004, NFR-PRIV-001 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-PACKAGE`, `CHK-A11Y`, `CHK-SEC`, `CHK-CLOSE` | Single-instance, tray, pacote e caminho nativo viáveis |
| M0.10 | Registrar a stack Windows aceita pelo mantenedor e a entrada em M1 | Decisão explícita em 2026-09-05; [ADR-0005](architecture/decisions/0005-windows-product-stack.md); NFR-STACK-001–006 | `CHK-DOC`, `CHK-TRACE`, `CHK-CLOSE`; sem teste artificial ou nova coleta | ADR aceito e contratos commitados; sem vencedor A/B, budget aprovado ou fechamento automático de M0/M1 |

### 7.2 Checks específicos do shell

Referência histórica do experimento: somente os checks Qt proporcionais ao recorte promovido integram o produto atual. Tauri/A/B não são dependências; benchmarks e Linux continuam adiados, não aprovados.

- Qt: CMake Release reproduzível, format/check C++/QML, Catch2/CTest, Qt Test/Quick Test aplicável, `qmllint`, plugins/DLLs inventariados, nomes/roles acessíveis e nenhuma dependência Qt em domain/application.
- Tauri: `cargo fmt`, Clippy com warnings negados, testes Rust, install congelado do frontend, format/lint/typecheck/test/build, CSP/capabilities mínimas, IPC estreito/coalescido, nenhuma navegação remota e árvore WebView2 completa nas métricas.
- Ambos: janela acionável P95 ≤ 800 ms, CPU pausada média < 0,5%, private working set ocioso < 80 MiB, fila de 1.000 itens sem bloqueio > 100 ms, jornada NVDA/teclado/contraste/100–200%/movimento reduzido e smoke Linux. Input-to-visible é registrado como diagnóstico do componente visual; NFR-PERF-003 só fecha no ensaio de áudio/combinação.

### 7.3 Checks específicos de áudio

O protocolo comparativo abaixo não é mais condição de seleção. Os contratos funcionais, corpus e limites continuam aplicáveis a Qt Multimedia/FFmpeg e TagLib; desempenho e soak do produto permanecem metas futuras, sem execução alegada.

- Mesmo manifesto, fixtures, IDs, dispositivo e método para ambos.
- MP3 CBR/VBR, M4A/AAC-LC, FLAC, WAV e Ogg/Vorbis recebem resultado explícito; Opus/ALAC permanecem candidatos.
- Open-to-audible P95 ≤ 750 ms.
- Seek-to-audible P95 ≤ 250 ms ou exceção por codec documentada antes da aceitação.
- Zero crash reproduzível com truncados, extensão enganosa, tags/capas inválidas e limites abaixo/no/acima.
- Evento `ended` uma vez, unload/shutdown idempotentes e resultado antigo descartado.
- Toda leitura síncrona não cancelável é limitada e não acumula jobs/handles.
- Underrun em reprodução estável é sempre diagnosticado. Depois de M0.0 congelar a meta zero ou uma tolerância explícita por dispositivo/backend, qualquer violação elimina o backend ou exige prefetch/ring buffer limitado e nova medição completa.

### 7.4 Decisão M0

1. O mantenedor aceitou C++20 + Qt Quick/QML + Qt Multimedia/FFmpeg + TagLib no [ADR-0005](architecture/decisions/0005-windows-product-stack.md); ADR-0001/0002 ficam substituídos.
2. A escolha é uma decisão de produto, não vitória, empate ou inferioridade comprovada em A/B. Não altera os budgets para fazê-los passar.
3. Tauri/Rust e seus checks ficam fora das dependências do produto; arquivos históricos permanecem separados, sem apagar evidências.
4. Promover o código Qt existente em incrementos testados, preservando domínio puro, portas, comportamento, privacidade e identidade local.
5. Tratar riscos remanescentes de corpus, segurança/licenças, acessibilidade, Windows e distribuição no marco responsável; benchmarks e Linux ficam adiados.

### Gate de decisão M0.10 / entrada M1

**Go:** decisão explícita registrada no ADR aceito, contratos alinhados, checks documentais/revisão e commit; o primeiro incremento M1 tem unidade real e critérios próprios definidos.

**No-go:** apresentar A/B ou budgets como aprovados, declarar todos os spikes concluídos, promover código sem validar suas fronteiras ou tratar um wrapper de build como toda a fundação.

## 8. M1 — Promoção incremental e fundação da stack aceita

**Estado:** promoção em andamento; ainda não concluída. Núcleo e unidade gráfica já possuem build raiz, sem duplicação. O código existente implementa comportamentos de M1–M4, com parte dos testes ainda no consumidor histórico; cada promoção deve inventariar o que reutiliza, o que falta e os checks que realmente executa.

**Objetivo:** extrair/compor a menor unidade real de produto Windows, preservando domínio puro, portas, adapters e testes existentes. M1.1 começa por uma fatia identificada de código e sua unidade de build/test, não por reimplementar features nem por adicionar somente comandos que encaminham ao CMake experimental. O modo de fixture/harness permanece distinguível da composição real; wrappers úteis de M1.2 não substituem essa separação.

O [primeiro recorte de M1.1](core-build.md) extrai o core C++ puro como implementação única, mantendo o shell Qt existente como consumidor. O build raiz OFF não depende de Qt, Catch2, fixture/corpus/protocolo ou testes; ON reutiliza os 224 casos com Catch2 explícito. Workers e fake estão em alvos separados do domínio. `WindowsRuntimePlan` já exige limpas as fontes promovidas.

O [segundo recorte](bootstrap-separation.md) remove `FixtureBundle` dos construtores reais e os dois JSONs de demonstração do executável combinado, preservando defaults, traduções, cenários históricos e algoritmos. Red/Green, Release ON/OFF, CTest integrado 10/10, compatibilidade histórica 2/2 e lint/formato passaram. O terceiro recorte, no mesmo registro, exclui também tipos/fontes de cenário e fake da compilação combinada OFF, mantendo o ON histórico e a API QML. A [promoção gráfica seguinte](desktop-build.md) move fontes, recursos e oito arquivos de testes para a raiz e separa o configure dos inputs M0: Release ON/OFF compilam, e a raiz ON passa 232/232 testes sem ligar cenários na aplicação. Isso não fecha automaticamente M1.1/M1.2/M1.10, segurança/licenças ou os gates globais.

ADR-0003 não autoriza implementar `MediaSourceResolver` ou seu fake no MVP. M1 usa `LocalFile` tipado; a porta remota só entra após decisão específica em R1.

| ID | Incremento TDD | Requisitos principais | Checks obrigatórios | Critério de saída |
|---|---|---|---|---|
| M1.1 | Promover uma unidade real C++/Qt existente para a estrutura de produto, com versões pinadas e build próprio | NFR-MAINT-003, NFR-STACK-002 | `CHK-PREFLIGHT`, `CHK-FAST`, `CHK-ARCH`, `CHK-SEC`, `CHK-CLOSE` | fontes/fronteiras e testes promovidos identificados; configure/build/test Windows e core sem Qt nessa unidade; não apenas wrapper sobre o experimento; Linux adiado explicitamente |
| M1.2 | Scripts raiz `format`, `lint`, `check`, `test`, `build`, `validate` e check documental | NFR-MAINT-001, NFR-MAINT-004–005 | `CHK-TDD` para o runner quando aplicável, `CHK-DOC`, `CHK-FAST`, `CHK-CLOSE` | um entrypoint `validate` executa checks rápidos reais |
| M1.3 | CI Windows para checks rápidos e artefatos diagnósticos; extensão Linux adiada | NFR-MAINT-004–005 | `CHK-FAST`, `CHK-DOC`, `CHK-CLOSE` | mesmos scripts locais/CI Windows e commit identificável; nenhum resultado Linux inferido |
| M1.4 | Tipos `TrackId`, `Position`, `Duration`, `Volume`, saltos e erros tipados | FR-013–014, NFR-REL-001 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-TRACE`, `CHK-CLOSE` | invariantes/clamps/property tests verdes |
| M1.5 | Máquina de estados de playback e descarte por geração | FR-010, FR-014–015, FR-024 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-CLOSE` | transições válidas/inválidas e eventos antigos cobertos |
| M1.6 | Fila por ID, duplicatas, anterior/próxima e fim | FR-011, FR-016–017 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-TRACE`, `CHK-CLOSE` | property tests e navegação determinística verdes |
| M1.7 | Portas `PlaybackBackend`, `MetadataReader`, `SessionStore`, `PlatformIntegration`, relógio e IDs; fakes mínimos | FR-020–024, FR-030–038, FR-042–044 | `CHK-TDD`, `CHK-ARCH`, `CHK-FAST`, `CHK-CLOSE` | core compila/testa sem toolkit, SO, arquivo ou áudio real |
| M1.8 | Controller, comandos comuns, snapshots e coalescing | FR-012–016, FR-037, NFR-LIMIT-004 | `CHK-TDD`, `CHK-FAST`, `CHK-ARCH`, `CHK-TRACE`, `CHK-CLOSE` | UI/tray/plataforma podem usar os mesmos casos de uso |
| M1.9 | Esquema versionado de preferências/sessão e política de redação, rotação e retenção de logs | FR-042–044, NFR-PRIV-002–003 | `CHK-TDD`, `CHK-SEC`, `CHK-FAST`, `CHK-CLOSE` | round-trip de todo o inventário; formatter sem conteúdo/path/URL sensível; limites de idade/bytes/arquivos declarados e testados abaixo/no/acima |
| M1.10 | Promover boundary/presenter e validar shell contra fakes, mantendo a composição real separada | UX-001–008, UX-010 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-ARCH`, `CHK-E2E`, `CHK-CLOSE` | jornada visual falsa; sem regra de negócio na UI |

### Gate M1

- Unidade de produto real identificada, com fontes e dependências promovidas; `validate` verde localmente e na CI Windows. Linux permanece adiado, sem alegação de cobertura.
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
| M2.1 | Seletor nativo com multiseleção, filtros validados, `LocalFile`, detecção por conteúdo e erro por extensão/arquivo inválido — seleção principal assíncrona funcional localmente no candidato Qt/Windows, com checks automatizados consolidados e gates nativos pendentes | FR-001–002, FR-005–006 | `CHK-TDD`, `CHK-CORPUS`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE` | um e vários arquivos válidos abrem/entram na fila; inválido preserva contexto; filtros refletem matriz |
| M2.2 | Adapter real load/play/pause/unload e capacidades | FR-010, NFR-REL-001–002 | `CHK-TDD`, `CHK-FAST`, `CHK-CORPUS`, `CHK-PERF`, `CHK-CLOSE` | MP3/M4A P0 do corpus passam no pacote de preview |
| M2.3 | Posição/duração, snapshots e progresso sem polling excessivo | FR-012, NFR-PERF-003–004 | `CHK-TDD`, `CHK-UI`, `CHK-PERF`, `CHK-E2E`, `CHK-CLOSE` | UI difere do engine ≤ 250 ms sem loop desnecessário |
| M2.4 | Click/drag seek, preview, confirmação única e saltos ±5 s | FR-013–015 | `CHK-TDD`, `CHK-UI`, `CHK-CORPUS`, `CHK-PERF`, `CHK-E2E`, `CHK-CLOSE` | seek pausa/limites/non-seekable corretos; latência dentro do budget |
| M2.5 | Volume/mudo e restauração lógica do último volume | FR-010–011 | `CHK-TDD`, `CHK-UI`, `CHK-E2E`, `CHK-CLOSE` | 0–100%, mute idempotente e sem amplificação |
| M2.6 | Metadata/propriedades/capa, limites e descarte de resultado antigo; sanitização textual/numérica funcional e checks automatizados consolidados no candidato Qt/Windows | FR-020–024, NFR-LIMIT-001–003 | `CHK-TDD`, `CHK-CORPUS`, `CHK-SEC`, `CHK-PERF`, `CHK-CLOSE` | abaixo/no/acima dos limites; UI nunca bloqueia nem recebe estado antigo |
| M2.7 | Estados vazio/loading/ready/playing/paused/error e preview instalável Windows | UX-001–003, UX-008–009 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-E2E`, `CHK-PACKAGE`, `CHK-CLOSE` | jornada principal empacotada, erro acionável e geometria estável |

O recorte de seleção principal assíncrona de M2.1/M3.1 substitui o seletor estático por `AudioFilesPicker`/`QtAudioFilesPicker` e compartilha o helper de diálogo com Localizar. Abrir/Adicionar aplicam paths validados à fila viva somente na conclusão, preservando sua semântica e mantendo engine/SMTC/IPC/polling operantes. Estado ocupado, falha e conclusão efetiva são separados na apresentação, com cancelamento externo e foco diferido protegidos. O [registro central](../experiments/m0/candidates/qt/README.md#async-audio-picker) concentra Reds, checks automatizados consolidados e riscos residuais, incluindo intermitência de IPC cuja causa não foi demonstrada; não conclui M2 ou M3.

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
| M3.5 | Preferências reais e atômicas — armazenamento funcional no candidato Qt/Windows; efeitos locais de sessão/início minimizado integrados depois em M4.1/M4.4, mas primeiro frame nativo, E2E empacotado e `CHK-SEC` amplo seguem abertos | FR-013, FR-040, FR-042, FR-044 | `CHK-TDD`, `CHK-UI`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE` | teste parametrizado cobre todo o inventário aprovado; 7 s/20 s persistem; corrupção usa backup/default; troca de tema sem flash/restart |
| M3.6 | Strings centralizadas, pt-BR, microcopy e erros acionáveis — funcional no candidato Qt/Windows; gates nativos permanecem pendentes | UX-008–010 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-DOC`, `CHK-CLOSE` | nenhuma string essencial espalhada; layout suporta texto longo |
| M3.7 | Teclado/foco, atalhos, leitor de tela, escala, contraste e movimento reduzido — funcional no candidato Qt/Windows no recorte automatizável; gates nativos pendentes | FR-037, UX-004–007 | `CHK-TDD`, `CHK-UI`, `CHK-A11Y`, `CHK-E2E`, `CHK-CLOSE` | zero controle P0 mouse-only, sem nome ou sem foco visível |
| M3.8 | Revisão UX, desempenho de 1.000 itens e convergência visual | UX-001–010, NFR-PERF-004, NFR-PERF-006 | `CHK-UI`, `CHK-A11Y`, `CHK-PERF`, `CHK-TRACE`, `CHK-CLOSE` | jornadas 1–4, 8–9 e decisão visual registradas |

O fechamento funcional de M3.6 no candidato Qt/Windows usa catálogos QML `*Text.qml`, catálogo C++ para o filtro nativo e pt-BR compilado por `qt_add_translations` e instalado em runtime por `QTranslator`, com `numerus` cobrindo “1 segundo”. Erros locais possuem causas tipadas, texto seguro e ação de recuperação; seleção não vazia rejeitada de modo síncrono mantém fila/contexto e publica recuperação. A fila acessível omite IDs/códigos técnicos, resultados parciais ou rejeitados de importação oferecem nova escolha e a UI mantém ação/texto longo dentro de 720 × 520. O fechamento automatizado passou 174/174 no Release combinado, 135/135 no core Qt OFF, Quick 64/64 nas escalas padrão, 1,5× e 2×, `qmllint` silencioso, formato de 90 C++/39 QML e a composição `BUILD_TESTING=OFF` com zero testes e rejeição dos argumentos reservados em exit 1. Isso não substitui NVDA, DPI, alto contraste ou movimento reduzido nativos, nem pacote/E2E; um target SDK Qt enxuto exige `QT_HOST_PATH` para as ferramentas host de tradução. Nenhum benchmark foi executado.

O recorte automatizável de M3.7 fecha no candidato Qt/Windows a jornada integral por teclado até a confirmação de limpeza, ToolTip alcançável para fonte ativa sem seek e causas acessíveis nos demais estados, continuidade de foco entre estados, transporte e modais, e inspeção direta de role, nome, descrição, foco e estado desabilitado na árvore `QAccessibleInterface`. Anúncios `Polite`/`Assertive` cobrem loading, nova faixa, erro e seek rejeitado uma vez por ocorrência, sem ticks; durante descoberta, a região de importação mantém autoridade exclusiva, o erro persistente fica deferido até seu resumo terminal e uma rejeição de seek obsoleta é suprimida. O aro de foco primário usa par semântico com contraste ≥ 3:1, e movimento reduzido interrompe a animação do progresso sem esconder texto/status. Regressões adicionais impedem que a raiz passiva seja restaurada após avisos sobrepostos ou que uma callback antiga retire foco de um aviso reaberto. O fechamento passou 174/174 no CTest Release combinado, Quick 90/90 nas escalas padrão, 1,5× e 2×, `qmllint` silencioso, formato de 90 C++/46 QML e a composição `BUILD_TESTING=OFF` com zero testes e argumento reservado rejeitado em exit 1. O core Qt OFF não foi repetido porque nenhum código de domínio/aplicação mudou. NVDA, DPI, alto contraste e movimento reduzido nativos, pacote/E2E e os gates globais de M3 continuam abertos. Nenhum benchmark foi executado; M3.8 permanece adiado pela prioridade funcional Windows e o próximo incremento de código é M4.1.

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
| M4.1 | Persistência atômica de fila/item/posição e restauração opcional sempre pausada — em andamento; snapshot, codec, store Windows, writer/autosave, bootstrap, presenter, restart e recuperação destrutiva locais e lock multiprocesso cobertos; jornada empacotada/nativa pendente | FR-042–044, NFR-REL-003 | `CHK-TDD`, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE` | restore ligado/desligado, kill/crash/corrupção/replace falho recuperam sem áudio inesperado |
| M4.2 | Posição/tamanho da janela somente em monitor visível — funcional localmente no candidato Qt/Windows com Preferences JSON v2, migração v1, policy segura e restart em dois processos; pacote/nativo, `CHK-WIN`, `CHK-E2E` e `CHK-CLOSE` pendentes | FR-045 | `CHK-TDD`, `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` | monitor removido/DPI alterado não deixam janela inacessível |
| M4.3 | Instância única, CLI/shell e IPC limitado com paths Unicode/longos/múltiplos — funcional localmente no candidato Qt/Windows; pacote/Explorer e gates nativos/segurança amplos pendentes | FR-004, FR-033, NFR-LIMIT-004, NFR-REL-004 | `CHK-TDD`, `CHK-SEC`, `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` | 0/1/1.000/1.001 paths; espaços/aspas/metacaracteres; payload abaixo/no/acima de 1 MiB; dividir/rejeitar sem stall e sem shell concatenado |
| M4.4 | Tray, close/start behavior, menu e saída real — funcional no recorte automatizável Qt/Windows; pacote/tray/desktop nativos, Linux real e gates amplos pendentes | FR-030–032, FR-042 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-A11Y`, `CHK-E2E`, `CHK-CLOSE` | fechar/iniciar minimizado respeitam capacidade/preferência; Sair drena, salva, encerra áudio e só então o processo; Linux nunca fica invisível |
| M4.5 | SMTC/teclas de mídia no Windows e MPRIS no Linux usando casos de uso comuns — SMTC funcional localmente no candidato Qt/Windows; teclas físicas, pacote, MPRIS e gates amplos pendentes | FR-037 | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-CLOSE` | UI/tray/teclado/SO produzem a mesma semântica |
| M4.6 | ProgID/capabilities/Default Apps e `.desktop`/MIME/ícones — primeiro recorte funcional local no Qt/Windows abre a página oficial por launcher tipado e UI acessível; registro, formatos publicados, pacote, Linux e gates amplos pendentes | FR-034–036 | `CHK-TDD`, `CHK-SEC`, `CHK-WIN`, `CHK-LINUX`, `CHK-PACKAGE`, `CHK-CLOSE` | apenas formatos validados; nenhuma escrita em `UserChoice` |
| M4.7 | Suspend/resume, device/drive lost, arquivo removido e Explorer restart — recortes locais Qt/Windows de suspend/resume, retry da mesma ocorrência, recuperação explícita da saída padrão, recuperação da janela após `TaskbarCreated` e localização de arquivo em outro path com checks automatizados verdes; seletor nativo, dispositivo/drive físicos, restart real do Explorer, pacote, Linux e gates amplos pendentes | FR-038; UX-008/NFR-REL-005 parciais, sem fechamento global | `CHK-TDD`, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-CLOSE` | zero zumbi/estado impossível; cada falha oferece recuperação/ação |
| M4.8 | Install, upgrade e uninstall Windows; `.deb` install/remove Linux | NFR-REL-006, NFR-PRIV-001–003 | `CHK-TDD` para templates/helpers, `CHK-PACKAGE`, `CHK-WIN`, `CHK-LINUX`, `CHK-SEC`, `CHK-CLOSE` | mídia intocada; dados preservados/removidos conforme política; zero processo, serviço, tarefa, binário ou registro do ProgID próprio residual |

O primeiro incremento de M4.1 mantém o contrato no domínio C++ puro: `SessionSnapshot` contém somente fila, ocorrência atual e posição confirmada, enquanto `LocalFileQueueSnapshot` preserva ordem, IDs e paths, mas exclui disponibilidade transitória. Os validadores rejeitam IDs não positivos/duplicados, paths, seleção e posição inválidos; a hidratação da fila valida seu snapshot antes do commit, sem alterar fila ou cursor em falha. O allocator circular pula IDs ocupados após wrap. O Red ligou com quatro símbolos ausentes; o Green focado passou 118 asserções em sete casos, o core Qt OFF passou 142/142 e o Release combinado passou 181/181. Formato cobriu 93 C++/46 QML; a composição `BUILD_TESTING=OFF` listou zero testes e rejeitou argumento reservado em exit 1.

O segundo incremento separa `SessionStore` de `PreferencesStore` e materializa o codec JSON v1 ainda sem I/O. O Red compilou e falhou no link somente por `SessionJsonCodec::encode/decode`; o Green focado passou 1/1. `queue` agrupa itens e seleção, IDs/posição usam strings decimais canônicas exatas até `uint64_t`/`int64_t`, paths Windows Unicode permanecem literais e versões futuras são detectadas antes dos campos atuais. O limite inclusivo de 16 MiB é aplicado antes do parse e depois da serialização. A porta também é compilada no core Qt OFF. O fechamento passou 183/183 no Release combinado e 143/143 no core sem Qt; a composição `BUILD_TESTING=OFF` compilou o codec, listou zero testes e rejeitou argumento reservado em exit 1. `clang-format` validou 98 arquivos C++, e o validador documental passou 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum QML mudou e nenhum benchmark foi executado. Store Windows, backup/replace atômico, writer/autosave, bootstrap pausado e E2E de corrupção/restart/kill continuam necessários para o critério de saída de M4.1.

O terceiro incremento materializa `QtJsonWindowsSessionStore` isolado em `session.json`/`.bak`. O Red inicial compilou e falhou no link pelos três métodos ausentes; regressões posteriores expuseram concorrência entre stores e a diferença entre JSON inválido e arquivo ilegível antes do Green final. O adapter usa `QSaveFile` sem fallback direto, lock cooperativo durante a transação e cópia byte a byte do primário válido. Load não muta arquivos; save bloqueia qualquer versão futura sem confirmação e falha fechado em I/O ilegível. Backup futuro só é substituído depois do commit primário. Quatro fault points, sharing Windows e falhas reais de commit cobrem estados antes/depois de backup e primário, inclusive o resultado ambíguo em que `Failed` sucede um commit. O teste focado passou 1/1, o Release combinado 184/184 e o core Qt OFF 143/143; `BUILD_TESTING=OFF` compilou o store, listou zero testes e rejeitou argumento reservado em exit 1. `clang-format` validou 101 arquivos C++; nenhum QML mudou e nenhum benchmark foi executado. O lock ainda precisa de prova multiprocesso, e fault injection não prova kill/crash/perda de energia. Writer/autosave, bootstrap pausado e E2E de corrupção/restart/kill mantêm FR-043/FR-044, NFR-REL-003 e M4.1 abertos.

O quarto incremento permaneceu no core C++ puro e materializou `SessionWriter`/`SingleWorkerSessionWriter` e `SessionAutosave`, sem bootstrap. O writer mantém uma única chamada de store fora do caller, recusa `start()` enquanto há trabalho, não cria backlog ilimitado e coordena `flush()`/shutdown; shutdown é idempotente e seguro sob callers concorrentes, mas seu `join()` pode bloquear indefinidamente se o store não retornar. Após `Failed`, o mesmo worker chama `load()` e anexa uma observação tri-state — snapshot pedido exatamente carregado/recuperado, divergente ou readback indisponível — sem promover o status a `Saved`. O autosave valida snapshot/opções transacionalmente, aplica debounce estrutural trailing de 500 ms limitado a 5 s de idade suja e atualiza posição confirmada em O(1), armando no primeiro tick um checkpoint fixo de 10 s que os próximos não adiam. Deadlines estrutural e posicional são independentes. Naquele recorte, o checkpoint imediato existia no core, mas a ligação a pausa, seek confirmado e eventos terminais pertencia ao incremento de integração. `Failed` e `BlockedByFutureVersion` suspendem retries automáticos; somente ação explícita rearma. Confirmação de substituição futura muda a revisão completa do request e permanece autorizada depois de falha ambígua. O Red compilou e falhou no link pelos 22 símbolos ausentes. Durante o Green, regressões P1 impediram que `flush()` republicasse completion já consumida ou que uma autorização nova reutilizasse a revisão ativa. Aquele fechamento passou 169/169 no core puro e 210/210 no Release combinado; `clang-format` validou 107 arquivos C++, `BUILD_TESTING=OFF` listou zero testes e rejeitou argumento reservado em exit 1. Nenhum QML mudou e nenhum benchmark foi executado. Naquele ponto, o executável ainda não capturava, salvava nem restaurava a sessão; bootstrap/restauração sempre pausada, integração dos gatilhos urgentes e E2E de corrupção/restart/kill continuavam necessários. Assim, `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE`, FR-043/FR-044, NFR-REL-003 e M4.1 permaneceram abertos.

O quinto incremento integra a sessão ao executável Qt/Windows. Depois de carregar preferências, o startup consulta `session.json` somente quando `restore_last_session=true`; com a opção desligada, usa snapshot vazio sem tocar no store. Antes da exposição ao QML, o presenter hidrata a fila e abre a ocorrência selecionada com intenção `RestorePaused`, nunca com autoplay. A posição restaurada pertence à geração aberta, aguarda duração conhecida quando o destino é positivo e só é considerada confirmada dentro de tolerância de 50 ms; substituição da geração descarta o seek pendente, e a marca de EOF impede que o `Ended` associado avance ou toque a próxima faixa. Mudanças estruturais entram no autosave coalescido, enquanto posição vem somente do backend confirmado e checkpoints imediatos cobrem seek, pausa, eventos terminais e saída. Retry, substituição explícita de versão futura, aviso/recuperação após timeout e a transição `false` → `true` que repara ou substitui sessão antiga permanecem acionáveis. O restart instrumentado do executável Release comprovou path Unicode e restauração pausada em 750 ms; o cenário com restauração desabilitada ignorou a sessão sem lê-la. O fechamento funcional passou 214/214 no CTest Release combinado e 172/172 no core Qt OFF; `all_qmllint` ficou verde, e os checks de formato cobriram 109 C++ e 46 QML. A composição de produção `BUILD_TESTING=OFF` compilou, listou zero testes e rejeitou o hook reservado com exit 1. Nenhum benchmark foi executado. Ainda faltam E2E de encerramento forçado/crash, corrupção e reparo, falha de replace, lock multiprocesso e validação nativa/pacote; portanto `CHK-SEC`, `CHK-E2E`, `CHK-CLOSE`, FR-043/FR-044, NFR-REL-003 e M4.1 permanecem abertos.

O sexto incremento cobre os cenários destrutivos locais de M4.1 sem extrapolá-los para pacote ou perda física de energia. Um processo Release instrumentado restaura a sessão pausada em 750 ms, solicita uma mudança estrutural e para em `BeforePrimaryCommit`; nesse ponto o primário anterior permanece intacto, o backup contém os mesmos bytes e o owner PID do `QLockFile` corresponde ao processo. O processo é encerrado por `QProcess::kill()`, termina em `CrashExit`, deixa um lock obsoleto que o save seguinte reaproveita e remove, e o relançamento volta à mesma sessão pausada. No fluxo de corrupção, primário inválido e backup válido produzem `RecoveredFromBackup`; um handle Windows que nega delete sharing causa uma falha real do replace primário, o aviso permanece acionável e o botão QML de retry repara o arquivo depois da liberação. Com os dois documentos inválidos, o executável inicia vazio por defaults seguros, permite dispensar o aviso pela QML e não reescreve silenciosamente nenhum deles. O contrato de lock também roda entre PIDs em duas linhas, `BeforeBackupCommit` e `AfterPrimaryCommit`: o proprietário é confirmado por PID, o concorrente retorna `Failed` sem alterar primário/backup e, depois da liberação, o holder e uma nova gravação concluem com o lock removido. Os alvos focados são `m0.qt.session.restart` e `m0.qt.session.windows-store`; a consolidação dos gates amplos pertence ao fechamento do incremento. As jornadas correspondentes ainda precisam passar no executável empacotado/nativo para `CHK-E2E`, e M4.1, FR-043/FR-044, NFR-REL-003, `CHK-SEC` e `CHK-CLOSE` permanecem formalmente abertos.

O fechamento automatizado final do sexto incremento passou 214/214 no CTest Release combinado; o core fresco, sem Qt e com MSVC 19.44/toolset 14.44, passou 172/172; e o alvo Quick final passou 1/1 com 99 casos internos. `all_qmllint` ficou verde. O formato cobriu 109 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não possui `--check`, a conferência QML comparou sua saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, registrou zero testes e rejeitou o hook reservado de crash com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Esses checks não fecham pacote/nativo, `CHK-E2E`, M4.1, FR-043/FR-044, NFR-REL-003, `CHK-SEC`, `CHK-CLOSE` nem qualquer outro gate ainda indicado como pendente.

M4.2 está funcional localmente no candidato Qt/Windows. `Preferences` ganhou `std::optional<WindowPlacement>` e o codec passou a emitir JSON schema v2; documentos v1 conhecidos são lidos como válidos com placement ausente, sem promoção ou reescrita no load, enquanto v3 ou superior permanece protegido como versão futura. A policy pura recebe cada `availableGeometry` conectado em DIPs e o mínimo `720 × 520`, normaliza o tamanho, aceita coordenadas negativas, rejeita dados ou cálculos com overflow e só restaura quando o retângulo inteiro cabe em uma única área individual, nunca na união virtual. Assim, monitor removido ou área alterada por DPI produzem fallback seguro. O root QML nasce oculto, recebe `setScreen()` e `setGeometry()` antes de ser revelado; a captura normal-visível é coalescida e drenada antes de ocultar, minimizar ou sair pelos casos de uso. Uma transição nativa não normal que vence o debounce descarta o candidato ambíguo, preserva o último normal estabilizado e agenda nova captura ao retornar a normal. Um restart instrumentado entre dois processos persiste a geometria no primeiro e verifica a restauração no segundo.

O fechamento automatizado de M4.2 passou 225/225 no CTest Release combinado, 183/183 num core fresco sem Qt com MSVC 19.44/toolset 14.44 e 1/1 no alvo Quick, com 100 casos internos. `all_qmllint` ficou verde. O formato cobriu 112 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não possui `--check`, a conferência QML comparou sua saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, registrou zero testes e rejeitou o hook reservado de restart de preferências com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

Esse recorte não substitui pacote, desktop nativo ou validação física de DPI/múltiplos monitores. `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` e M4.2 permanecem abertos globalmente.

M4.3 está funcional localmente no candidato Qt/Windows. O bootstrap adquire cedo um lock na raiz de configuração do usuário e solicita `UserAccessOption` no canal local; somente a principal inicializa o backend, enquanto cada secundária resolve seus argumentos nativos contra o próprio CWD, envia um `openSources` v1 tipado por `QLocalSocket`, tenta ceder permissão de foreground ao PID proprietário e aguarda ACK/NACK limitado. A principal restaura a janela e serializa os lotes pelo mesmo pipeline de importação da UI. Paths de filesystem precisam estar plenamente qualificados no wire; Qt resources, NUL, namespaces Win32 de dispositivo, prefixos extended não canônicos e formas dependentes do drive da principal são rejeitados.

Os limites são inclusivos em 1.000 paths e 1 MiB de JSON, excluindo o prefixo de frame de quatro bytes. A política aprovada neste recorte é rejeitar o lote inteiro, não dividir. Fila/bytes de bootstrap, conexões ativas, buffers e timeouts são limitados; excesso e frame grande recebem NACK sem encerrar a principal. Durante a saída, novos ingressos são pausados e todo comando já ACKado é drenado antes de persistir/encerrar; o deadline cancela a intenção visual em vez de perder trabalho. As mensagens visíveis ficam em `ApplicationText` e o build sem testes não aceita o hook do harness.

Os Reds focados falharam pelas expectativas novas de argumentos/paths/limites e, no fechamento da drenagem, pela ausência de `OpenSourcesDispatcher::hasDrainedAcceptedWork` e `PlayerPresenter::setSourceExitDrainedProbe`. Uma revisão adversarial adicional deixou parser e codec vermelhos 2/2 para namespaces Win32 de dispositivo/prefixos pseudo-extended antes do Green correspondente. O Green final de dispatcher/presenter/processo passou 3/3; a consolidação Release passou 229/229. O teste de processo Release instrumentado segurou A ativo, ACKou B num path Windows comum acima de 260 caracteres, recusou uma terceira invocação durante a saída e confirmou A+B no `session.json` com um único backend. `m0_qt_shell_ui_qmllint`, formato de 122 C++/46 QML e validação documental ficaram verdes. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes, rejeitou o hook reservado com exit 1 sob o runtime autenticado e preservou `longPathAware=true` no manifesto. A checagem QML comparou a saída normalizada porque `qmlformat` 6.11.2 não oferece `--check`; nenhum benchmark foi executado.

As provas locais de parser, codec, dispatcher, coordinator e processo não equivalem a associação instalada/double-click pelo Explorer, quoting produzido pelo shell, foreground real, ACL entre usuários ou pacote nativo. Fuzz/property do framing/schema e o fechamento de advisories/licenças também permanecem pendentes. Portanto `CHK-WIN`, `CHK-E2E`, `CHK-SEC`, `CHK-CLOSE` e M4.3 continuam abertos globalmente.

M4.4 está funcional localmente no candidato Qt/Windows. A disposição inicial só é decidida depois do placement: preferência desligada mostra; ligada com tray disponível mantém o root oculto antes do event loop; sem tray mostra e publica aviso. Uma fonte inicial explícita prevalece e mostra a janela, enquanto um lote posterior por IPC restaura/ativa uma principal oculta. O teste de processo Release instrumentado também aciona as `QAction` reais da tray em mostrar/ocultar/mostrar. O toggle permite desligar `startMinimized` mesmo depois que a capacidade de tray é perdida.

`QtShellPlatform` separa a invisibilidade do bootstrap da intenção `hidden_to_tray`. Somente `hideWindow()` arma a recuperação e a vigilância; mostrar/rebind limpam a intenção. Assim, perder tray durante o bootstrap não altera a janela, mas perder a capacidade durante uma ocultação explícita restaura a janela visível/minimizada e encerra o polling.

O commit da saída agora exige, nesta ordem, drenagem de fontes já ACKadas, sucesso da persistência de preferências e sessão, `PlaybackBackend::shutdown()` e só então `PlatformIntegration::requestExit()`. O controller torna shutdown/exit idempotentes. Falha ou timeout anterior ao commit cancela a saída, mantém o backend ativo e devolve uma janela alcançável.

Os Reds focados falharam pelas expectativas novas de disposição, ownership de ocultação, toggle, precedência de fonte e ordem de shutdown; os Greens correspondentes passaram antes da consolidação. O fechamento automatizado passou 229/229 no CTest Release combinado e 183/183 no core Qt OFF; `m0_qt_shell_ui_qmllint` e o formato de 122 C++/46 QML ficaram verdes, com o QML comparado à saída padrão normalizada porque `qmlformat` 6.11.2 não possui `--check`. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e recusou os dois hooks M4.4 com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

Essa evidência não encerra M4.4. Pacote/desktop e tray nativos, primeiro frame perceptível sem flash, fallback Linux real, restart do Explorer, NVDA/acessibilidade nativa e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y` e `CHK-CLOSE` permanecem pendentes.

M4.5 está funcional localmente no candidato Qt/Windows. A porta C++ pura `SystemMediaControls` publica metadata, estado, capacidades e timeline e recebe Play, Pause, Previous, Next e Seek. `SystemMediaControlsBinding` projeta o presenter com deduplicação, desabilita seek durante preview ou confirmação pendente, encaminha cada comando aos mesmos casos de uso de UI, atalhos e tray e desliga a integração durante a saída. `PlayerController` separa a intenção desejada do comando em voo: reversões antes do ACK ficam serializadas, um ACK obsoleto dispara a compensação necessária e geração nova, terminal ou EOF descartam a intenção.

No Windows, `WindowsSmtcAdapter` usa C++/WinRT e `ISystemMediaTransportControlsInterop::GetForWindow` sobre o `HWND` real. O boundary publica status, botões, metadata e timeline, desabilita operações não suportadas, entrega callbacks na thread da UI, mantém cache/deduplicação e falha fechado. Eventos `QEvent::PlatformSurface` liberam o SMTC antes da destruição da surface e refazem o vínculo/republicação após a criação; geração e presença da surface impedem rebind enfileirado obsoleto. `Closed` limpa metadata e desabilita os controles; shutdown revoga handlers e torna callbacks tardios inertes.

Os Reds/Greens cobriram link ausente, comandos explícitos, intenção invertida antes do ACK, `Ready` obsoleto, navegação preservando a intenção, capacidade de seek, surface destruída/recriada e rebind obsoleto. O filtro `[async]` passou cinco casos e 42 asserções. O CTest Release combinado passou 239/239 e o core Qt OFF 190/190; `m0.qt.system-media-controls.binding`, `m0.qt.windows.smtc` e `m0.qt.windows.smtc-native` ficaram verdes. O smoke nativo QPA Windows comprovou `HWND`/SMTC reais, status, botões, metadata e limpeza em `Closed`, mas não sintetiza teclas físicas. `m0_qt_shell_ui_qmllint` passou, `clang-format` validou 131 C++ e nenhum QML mudou. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado com exit 1 sem criar a raiz solicitada. Nenhum benchmark foi executado.

M4.5 permanece aberta por teclas de mídia físicas, pacote, MPRIS/Linux e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E` e `CHK-CLOSE`.

O primeiro recorte funcional local de M4.6 acrescenta a `PlatformIntegration` suporte da plataforma, disponibilidade momentânea e a operação fechada `openDefaultAppsSettings`, com resultado `Dispatched`, `Unavailable` ou `Failed`. O presenter expõe `Idle`, `Opening`, `Dispatched`, `Unavailable` e `Failed` por `defaultAppsSettingsChanged`, impede duplicação em voo e usa geração/lifetime para descartar conclusão obsoleta. Uma saída ainda reversível por drenagem ou persistência conserva a conclusão; somente o commit efetivo da saída a invalida.

No Windows, `QtShellPlatform` entrega exclusivamente a URI interna fixa `ms-settings:defaultapps` a `QDesktopServices::openUrl`. `Dispatched` comprova apenas o handoff ao handler, não visibilidade nem mudança de padrão. A QML oferece botão e feedback acessíveis somente quando a plataforma suporta a operação; indisponibilidade/falha são localizadas e anunciadas apenas enquanto o painel está aberto e visível.

Os Reds focados exigiram as fronteiras, propriedades/estados e contratos de adapter/UI. Os Greens passaram individualmente em `m0.qt.shell-platform` 1/1, `m0.qt.presenter.initial-load` 1/1 e `m0.qt.quick.shell-states` 1/1. A consolidação passou 239/239 no CTest Release combinado, 190/190 no core Qt OFF e 1/1 no Quick com 100 casos internos; `m0_qt_shell_ui_qmllint`, formato de 131 C++/46 QML, composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado, e validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas ficaram verdes. Nenhum benchmark foi executado.

Esse recorte não escreve `UserChoice`, não cria ProgID, `Capabilities` ou `RegisteredApplications`, não publica formatos, não instala associações/pacote e não implementa Linux. M4.6 permanece aberta; identidade, matriz publicada e integração de pacote precisam ser definidas antes do registro, enquanto o próximo recorte Windows implementável segue por M4.7.

O primeiro recorte funcional local de M4.7 materializa a porta C++ pura `SystemPowerEvents`, separada de `PlatformIntegration`, e o binding que entrega `Suspending`/`Resumed` ao presenter. No Windows, `WindowsPowerEvents::start()` cria na thread da UI um `QWindow` privado e invisível, obtém seu `HWND` estável e o registra com `RegisterSuspendResumeNotification`/`DEVICE_NOTIFY_WINDOW_HANDLE`. Esse receptor é diferente da janela visual e permanece inscrito até shutdown; criar, destruir ou recriar a surface visual não interfere nele. Somente `PBT_APMSUSPEND` e `PBT_APMRESUMEAUTOMATIC` do `WM_POWERBROADCAST` dirigido ao receptor viram eventos, entregues sincronamente. Essas mensagens reconhecidas são consumidas com resultado `TRUE`, inclusive quando a transição consecutiva não é republicada; mensagem, janela ou tipo incorreto retornam `false` e são inertes. Eventos nativos consecutivos iguais são deduplicados no registro contínuo, sem descartar um `Resumed` nativo inicial.

Se o registro inicial falha, o receptor privado é preservado e o adapter agenda tentativas coarse single-shot com backoff finito de 1 s, 5 s e 30 s. Falhando também depois dos 30 s, o timer permanece parado; apenas outro `start()` explícito ou `applicationStateChanged` enquanto indisponível reinicia o orçamento. Shutdown cancela o timer, remove o filtro, desfaz a inscrição quando existente e destrói o receptor; timeout e callback posteriores ficam inertes.

No primeiro `Suspending`, o presenter registra a última posição confirmada da geração corrente e chama `requestSaveNow()`. A escrita continua assíncrona: não há `flush()` no caminho da notificação, nenhum bloqueio deliberado da thread da UI e nenhum comando de pause, play ou autoplay. O polling funcional fica quiescente até a retomada, preservando apenas o polling necessário à persistência já ativa. Em `Resumed`, capabilities da plataforma são atualizadas e o snapshot do backend é consultado; se ainda pertence à geração ativa, estado, posição e erro são reconciliados pelo fluxo normal de playback, e o timer aplicável é rearmado. Duplicatas e eventos após o commit da saída não alteram estado.

Os testes focados cobrem presenter, binding e adapter Win32, inclusive um registro real do `HWND` por QPA Windows sem suspender a máquina. Resultado final dos alvos, consolidação Release/core, formato, composição `BUILD_TESTING=OFF` e validação documental: build Release aprovado, 241/241 testes no CTest combinado sequencial, 190/190 no core Qt OFF, `qmllint` e formato de 139 C++/46 QML verdes; `BUILD_TESTING=OFF` recompilado com zero testes e hook reservado rejeitado em exit 1 sem criar a raiz solicitada; validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas aprovada. Nenhum QML foi alterado neste recorte e nenhum benchmark foi executado.

O recorte atende somente à parte de suspend/resume de `FR-038` e não encerra M4.7. Sleep físico, Modern Standby, hibernação e timing real de congelamento não foram ensaiados; como o save é assíncrono, não há garantia de durabilidade antes do freeze. Perda de dispositivo, drive desconectado, arquivo removido, restart do Explorer, pacote/E2E nativo e Linux permanecem pendentes. `NFR-REL-005` não recebe evidência deste recorte.

O segundo recorte funcional local de M4.7 acrescenta `retryQueueOccurrence(ID)` para a ocorrência indisponível original. A UI fornece somente o ID; o presenter reutiliza o path interno, preservando ordem, duplicatas e identidade do modelo. A aceitação do load precede a seleção para retry, e a ocorrência continua indisponível até a geração nova confirmar `Ready`, `Playing` ou `Paused`. A confirmação limpa o diagnóstico por atualização restrita no modelo, conservando metadata. Rejeição síncrona preserva seleção e backend anteriores; falha da tentativa aceita conserva diagnóstico acionável, permite novo retry explícito e não cria loop nem avanço automático dessa tentativa.

“Tentar reproduzir novamente” aparece na fila e no erro com teclado e semântica acessível; loading é vinculado ao ID da ocorrência, inclusive em duplicatas, e “Abrir outro áudio” permanece disponível. A intenção explícita de retry prevalece sobre lotes incrementais posteriores, e a contagem de problemas da importação mantém o histórico por ID mesmo quando a ocorrência volta a disponível. O recorte possui testes de domínio/modelo, presenter, Quick e uma jornada de integração que restaura uma WAV autenticada no mesmo path temporário antes ausente. A [evidência central do segundo recorte](../experiments/m0/candidates/qt/README.md#retry-m47) distingue Reds, checks intermediários e consolidação final.

Esse segundo recorte não encerra M4.7 nem `NFR-REL-005`. Perda de dispositivo de saída, drive físico, localização do arquivo em outro path, restart do Explorer, sleep físico, pacote/E2E nativo, acessibilidade manual e Linux seguem pendentes. Nenhum benchmark integra o recorte.

O terceiro recorte funcional local de M4.7 detecta ausência ou mudança da saída padrão no adapter Qt e produz `AudioOutputUnavailable`/`AudioOutputChanged`. A interrupção retira o player anterior, conserva a posição confirmada e não marca o arquivo indisponível nem avança a fila. Reconexão é inerte até uma ação explícita; “Tentar novamente” reutiliza a ocorrência corrente em `RestorePaused`, restaura a posição quando há seek e preserva “Abrir outro áudio”. Identidade da fila, geração, rejeição do load, repetição da falha, mudança posterior de seleção e UI acessível pertencem aos contratos de teste do recorte.

O [registro central do terceiro recorte](../experiments/m0/candidates/qt/README.md#audio-output-m47) reúne Reds, checks e a integração com decoder real/saída mutada e topologia injetada. Essa evidência não comprova reconexão física ou áudio audível; drive físico, arquivo relocalizado, Explorer, sleep físico, pacote/E2E nativo, acessibilidade manual e Linux permanecem pendentes. M4.7, `FR-038` e `NFR-REL-005` continuam parciais, e a seleção manual de dispositivo dentro do app permanece em `FR-018`/M6.1. Nenhum benchmark integra o recorte.

O quarto recorte funcional local de M4.7 acrescenta `WindowsTaskbarEvents` e a recuperação da janela após `TaskbarCreated`. Um receptor top-level privado recebe a mensagem dirigida a seu próprio `HWND` e a entrega enfileirada; monitor não pronto impede ocultação e permite retry por refresh explícito. O Qt continua responsável por registrar novamente seu ícone, enquanto o adapter torna visível/minimizada, sem ativação ou comandos de áudio, somente a janela ocultada por ele para a bandeja. Saída pendente adia essa recuperação; bootstrap, rebind e retorno do shell não reaplicam `startMinimized`.

O [registro central de taskbar](../experiments/m0/candidates/qt/README.md#taskbar-m47) concentra Reds, checks e a caracterização nativa que remove e recria somente o ícone do próprio teste. Presença de shell e intenção de visibilidade não confirmam `NIM_ADD`; overflow legítimo impede usar geometria vazia como ausência de suporte. O teste não encerra Explorer nem comprova seu reinício real. M4.7 e `FR-038` continuam parciais; dispositivos/drive/sleep físicos, arquivo relocalizado, pacote/E2E nativo, acessibilidade manual e Linux permanecem pendentes. Nenhum benchmark integra o recorte.

O quinto recorte funcional local de M4.7 acrescenta `locateQueueOccurrence(ID)` com a porta pura `AudioFileLocator` e um seletor Qt assíncrono. Apenas o path da ocorrência indisponível é substituído, preservando IDs, ordem, duplicatas e seleção; a metadata anterior é limpa e a linha fica em `source_relocated`, ainda indisponível. Localizar uma ocorrência não atual não altera a reprodução corrente; seu retry posterior confirma a nova origem. A ocorrência atual em erro é recarregada em `RestorePaused`, com nova geração e restauração de posição quando possível. Geração/ID/path anterior, cancelamento e lifetime protegem a conclusão, e o autosave estrutural conserva schema v1.

O [registro central de localização](../experiments/m0/candidates/qt/README.md#file-location-m47) concentra Reds, checks automatizados verdes de core/modelo/presenter/picker/UI/decoder, composição de produção e gates restantes, sem reutilizar checks históricos como prova nova. Também preserva o registro da falha intermitente do coordinator, cuja causa não foi demonstrada apesar da consolidação final verde. M4.7 não está concluída: seletor nativo, drive físico, NVDA, pacote/E2E Windows e Linux continuam sem fechamento. Nenhum benchmark pertence ao recorte.

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
| M5.7 | Advisories, licenças C++/Qt/FFmpeg/TagLib, notices, SBOM e política final de logs | NFR-REL-006, NFR-PRIV-003 | `CHK-FAST`, `CHK-SEC`, `CHK-PACKAGE`, `CHK-CLOSE` | zero Critical/High explorável sem waiver; zero obrigação/licença não atendida; redação e retenção abaixo/no/acima dos limites verdes |
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

Esta é a matriz humana inicial exigida por `CHK-TRACE`. “Destino” indica onde o requisito é primeiro provado ou deliberadamente descartado; não significa que o teste já exista. O [manifesto de M1.2](quality/traceability.json) mantém uma linha por ID, links para testes/evidências, lacunas e estado (`planejado`, `em andamento`, `comprovado`, `excepcionado` ou `fora de escopo`). O [validador integrado ao runner](quality/traceability.md) confere inventário, destinos e referências literais, com consulta inversa por teste/evidência; não executa os testes citados nem comprova automaticamente seus critérios. `Excepcionado` vale somente para gates/desvios que o contrato permita; P0 funcional falho precisa ser corrigido ou alterado/repriorizado antes do RC.

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
| `NFR-STACK-001` | M0.10 | escolha explícita pelo mantenedor no ADR-0005; sem alegação de vitória A/B |
| `NFR-STACK-002` | M1.1–M1.2 | unidade real de produto e implementação única; experimentos/toolchains não escolhidas fora das dependências |
| `NFR-STACK-003` | M5.4–M5.5 | protocolo, ambiente/commit, árvore de processos e medições futuras separados da escolha |
| `NFR-STACK-004` | M3.7, M4.4–M4.5, M5.6 | gates de UX/acessibilidade/Windows preservados; Linux adiado |
| `NFR-STACK-005` | M1.1, M1.7, M4.8, M5.7 | domínio puro, um engine deliberado, versões/configuração e gates de segurança/licenças |
| `NFR-STACK-006` | M0.10, M5.1, M5.4–M5.9 | decisão aceita não comprova budgets nem dispensa gates de produto |
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
- FR-023: [sanitização textual/numérica](../experiments/m0/candidates/qt/README.md#metadata-sanitization) funcional no candidato, com Red/Green da policy, extrator e apresentação e checks automatizados completos consolidados, incluindo core sem Qt, integração real e produção; não confundir número da faixa/ano já disponíveis com campos ausentes. Corpus hostil canônico, limites internos do TagLib e gates globais não são fechados por esse recorte, que não afirma vulnerabilidade demonstrada.
- FR-019: velocidade de reprodução aparece em M6.
- FR-025: permanece backlog intencional.
- FR-045: policy e restauração seguras estão funcionais localmente em M4.2; pacote/nativo e os gates Windows/E2E/fechamento permanecem pendentes.
- UX-010: strings centralizadas entram em M1.10/M3.6.
- NFR-PRIV-003: diagnóstico opt-in, formato fechado, proteção Qt/stderr e retenção estão [integrados ao candidato Windows](../experiments/m0/candidates/qt/README.md#local-diagnostics), com TDD e checks focados; pacote, inspeção nativa, symlinks não autorizados no host e gates M1/M4/M5 continuam abertos.
- NFR-MAINT-003: unidade única é verificada em M1.1.
- NFR-MAINT-005: matriz requisito → etapa → teste → evidência é obrigatória desde M1.
- FR-003, FR-004, FR-005, FR-011, FR-016, FR-017, FR-032, FR-037, FR-038, FR-040 e FR-053 recebem cenários explícitos.
- NFR-LIMIT-001–004 exigem casos abaixo, exatamente no limite e acima.
- Preferências reais saem de M4 e entram em M3; M4 mantém sessão completa e recuperação.
- Soak de oito horas e budgets continuam gates futuros do produto em M5; comparação entre ramos não é pré-requisito de M1.
- Comparação de tray/pacote entre shells permanece histórica; integração Windows aprofundada e pacote seguem em M4/M5.
- NSIS mantém o escopo experimental local; aceitar a stack não autoriza distribuição pública.

## 17. Próxima sequência segura

**Decisão atual:** [ADR-0005](architecture/decisions/0005-windows-product-stack.md) aceito pelo mantenedor em 2026-09-05. A/B não bloqueia mais o desenvolvimento do produto Windows; benchmarks e Linux permanecem adiados.

Checkpoint de jornada Windows: o [isolamento manual do build ON](../experiments/m0/candidates/qt/README.md#isolated-native-journey) passou 101/101 testes e recusa real em produção. A janela nativa mostrou transporte, detalhes, volume e gravação da sessão com WAV sintética; seletor completo e restauração observada continuam sem validação. O usuário interrompeu Computer Use, portanto não retomá-lo sem confirmação. O instalador permanece inalterado e os gates amplos continuam abertos; não repetir suites verdes ou benchmarks sem mudança relevante. A decisão explícita de stack agora foi recebida; a promoção continua sujeita aos critérios próprios de M1 e à validação do código efetivamente promovido.

Checkpoint funcional mais recente: os [sliders acessíveis](../experiments/m0/candidates/qt/README.md#accessible-slider-input) agora aplicam progresso/volume ao player por ações e valor direto, com sincronização que não transforma ticks ou mudança de duração em comandos. Red/Green, Quick 187/187, startup e builds ON/OFF constam do registro central. O instalador a43faa1 permanece anterior a essa correção; não foi recompilado nem instalado. Prosseguir somente por lacunas funcionais comprovadas ou gates nativos autorizados; este incremento não encerra M3.7/M5.6 ou os gates amplos, não retoma benchmarks e não introduz protocolo de recuperação geral do instalador.

1. Preservar a decisão aceita já registrada no commit `c324114` e a evidência do [core promovido](core-build.md): fonte única, builds ON/OFF, testes reutilizados e proveniência atualizada. Não reabrir a escolha de stack ou repetir checks sem mudança relevante.
2. Preservar a [unidade gráfica promovida](desktop-build.md): fontes e configure na raiz, sem dependência dos inputs M0, com implementação única compartilhada pelo consumidor histórico. O ON da raiz não ativa cenários de demonstração na aplicação; uma [variante exclusiva de UI para testes](ui-tests.md) reutiliza as mesmas fontes sem misturar objetos, macro de cenário ou registros QML com a UI real. Não repetir essa separação nem duplicar lógica.
3. Validar a próxima unidade promovida com checks Windows proporcionais e preservação de comportamento/identidade. Alteração comportamental exige Red; movimentação mecânica exige equivalência verificada. O harness autenticado pré-M0.10 fica no snapshot histórico, não deve ser relaxado/reancorado para aceitar diretórios de produto.
4. Preservar os [comandos locais de M1.2](development-commands.md), já implementados com `validate` real, build de produção separado e testes dos runners. A [cobertura automática de integrações Windows](windows-integration-tests.md) e a [promoção dos cinco drivers de processo](process-tests.md) têm gates de build/testes validados, sem habilitar hooks/ABI de cenário no executável real. Não repetir essas promoções; manter os quatro smokes QPA Windows separados. A [rastreabilidade executável dos 83 IDs](quality/traceability.md) está integrada a `check`/`validate`, com evidências e lacunas explícitas; preservá-la a cada mudança. Prosseguir com medição de cobertura e validação/CI Windows de M1.3, sem manter Rust/Tauri como dependência ou presumir provedor/runner remoto. A [consulta CTest corrigida](audio-tests.md#preservacao-do-log-ctest) preserva o log da suíte e as assertions de registro de áudio; não reconstruir evidências históricas truncadas. Não marcar M1 completo antes de seus critérios de saída nem retomar Computer Use.
5. Continuar lacunas funcionais Windows comprovadas e hardening dos componentes promovidos. Preservar gates de corpus, segurança/licenças, acessibilidade, recuperação e distribuição; benchmarks/Linux ficam adiados. Não retomar Computer Use sem confirmação do usuário.

A [caracterização de geometria com metadata/capa](ui-tests.md#geometria-durante-metadata-e-capa) cobre a lacuna automatizada de UX-009 em Loading/Playing/troca de faixa, com provider real e dados sintéticos. O layout existente passou; não foi necessário alterar a UI de produção. Preservar essa regressão sem confundir geometria assentada offscreen com observação visual nativa ou fechamento de M3/M5.

O [diagnóstico local opt-in de M1.3](development-commands.md#diagnostico-local-opt-in) já preserva recibo, ferramentas selecionadas e logs separados por execução, mais JUnit novo quando executa CTest. `test -Diagnostics` passou **297/297** e `check -Diagnostics` passou separadamente; os quatro skips internos de symlink permanecem explícitos. Não repetir esses gates sem mudança relevante. CI Windows ainda depende da escolha de repositório/provedor/runner pelo mantenedor, solicitada sem presumir uma resposta; cobertura, CI remota e demais critérios de M1 não foram concluídos por essa capacidade local.

O [recorte de contraste UX-006](ui-tests.md#contraste-de-indicadores-e-textos) corrige o trilho/contorno claro de progresso/volume e a opacidade final dos indicadores de rolagem da fila/detalhes, com Red nos controles reais antes da correção. Tokens e fixtures históricas não mudam; a fonte QML continua única para produção e teste. Os resultados e limites ficam no guia central, sem declarar todos os pares da UI, inspeção nativa ou M3/M5 concluídos.

### Checkpoints históricos de evolução Windows

Incremento M1.2 de 2026-09-06: `scripts/develop.ps1` executa `format`, `check`, `build`, `test`, `lint` e `validate` sobre o CMake raiz, com ferramentas locais explícitas. A [evidência central](development-commands.md#evidencia-do-incremento) registra os ciclos Red/Green dos scripts, build Release OFF, `validate` ON e os testes promovidos. Cache de outra configuração/toolchain é recusado antes do build; check QML compara stdout sem `--check`; ausência de testes e falhas de processos não geram sucesso. Nenhum benchmark, instalação, interação nativa, SDK novo ou CI remoto foi executado. Isso não encerra rastreabilidade, a promoção dos testes históricos ou os demais gates M1.

Promoção de cobertura áudio de 2026-09-06: [playback, integração e TagLib](audio-tests.md) passam a compilar/testar na raiz com as mesmas fontes e assertions, usando 12 entradas sintéticas próprias copiadas sob a licença privada existente. Corpus e cenários comparativos não entram na aplicação, e os originais M0 permanecem intactos. O registro central distingue baseline, validação na raiz, produção OFF e compatibilidade histórica. A auditoria de CI não encontrou configuração versionada nem remote Git: não presumir provedor, runner registrado ou execução remota verde. Esses limites não impedem continuar a promoção local nem encerram M1.

Promoção de cobertura UI de 2026-09-06: a [composição exclusiva de testes](ui-tests.md) incorpora repositório sintético, presenter, contrato visual e Quick ao build raiz; a fábrica comum preserva a implementação C++/QML, com isolamento de ABI, plugins e recursos. O `validate` passou **276/276**, com build Release, ambos os lints QML, formato **154 C++/52 QML** e scripts **33/33 + 18/18**. A produção OFF continua sem Qt Test, cenários ou JSONs de demonstração. Os originais históricos e as assertions foram preservados; o único ajuste C++ do runner Quick escolhe o símbolo de tradução correspondente à variante. O registro central separa baseline, equivalência, guards e compatibilidade histórica. Prosseguir com cobertura restante de bindings/plataforma e processos, rastreabilidade e CI Windows; não tratar offscreen como validação nativa nem fechar M1.

Promoção de integrações Windows de 2026-09-06: [12 suites existentes](windows-integration-tests.md) agora integram o runner diário, com fontes/assertions byte-idênticas e consumidor histórico apontando para os novos paths. O app real permanece sem cenários e a variante de UI compila os quatro bindings com ABI coerente, inclusive a mesma fonte de diagnóstico sem ligar a biblioteca de produção. `validate` passou **288/288**, com quatro skips internos de symlink por política do host; formato **166 C++/52 QML**, scripts **33/33 + 18/18**, ambos os lints e a guarda de seis grafos passaram. Após limitar labels ao Windows, reconfigure/build/lint e seleção **13/13** passaram; compatibilidade histórica **12/12** e produção OFF sem testes/fixtures também foram verificadas. Não houve nova interação nativa, instalação, benchmark ou alteração de código funcional. Naquele checkpoint, os cinco drivers do app instrumentado e quatro smokes nativos permaneciam fora do runner diário; esse recorte não encerra M1/M4.

Promoção de processos de 2026-09-06: os [cinco drivers e seu helper](process-tests.md) mantêm bytes/assertions e registram sete jornadas sobre a main compartilhada, com dois shells instrumentados de ABI única. O combinado não incorpora JSONs; o shell de cenário incorpora somente os dois inputs privados autenticados. `test` passou **296/296**; build OFF, lint e formato passaram como gates separados, sem nova execução do wrapper `validate`. A guarda inspeciona comandos e recursos realmente alcançados, mantendo o produto ON/OFF sem hooks, e cobre os falsos positivos corrigidos para nomes de diretórios de build. A compatibilidade histórica passou em ambos os shells; tempos, Reds/Greens e limites do log global ficam no guia central, sem reatribuir o total histórico de 288. Os quatro smokes nativos continuam separados; nenhum gate M1/M4, CI, segurança/licenças ou release é concluído pela promoção.

Os registros seguintes documentam a ordem dos recortes anteriores; suas antigas “próximas tarefas” e decisões pendentes não substituem a sequência vigente acima.

O [recorte de recuperação do transporte](../experiments/m0/candidates/qt/README.md#transport-recovery) está corrigido e seus checks estão consolidados; não repetir esses checks sem mudança relevante nem encerrar fases amplas. O [aviso acessível de faixa pulada automaticamente](../experiments/m0/candidates/qt/README.md#automatic-skip-announcements), cujo Red Quick confirmou perda de contexto e de aviso deferido durante importação, está corrigido; Presenter e Quick passaram, mas a suíte global ficou não verde (271/273): contrato de texto corrigido e falha de IPC ainda sem causa confirmada ou correção. A validação proporcional está concluída: contrato UI 22/22 e Quick 153/153 no CTest focado 2/2, formato final e composição de produção conforme registro central. Não apresentar Green global nem repetir IPC/full para buscar aprovação. A próxima lacuna Windows já auditada é logging com retenção de `NFR-PRIV-003`, em incremento próprio com TDD, sem incluir logger neste commit nem afirmar vulnerabilidade ou silêncio de bibliotecas nativas. O nome definitivo foi solicitado e ainda não confirmado; registro de associações continua dependente de identidade e matriz, sem inventar essas decisões. Validações nativas/físicas, pacote e Linux mantêm seus limites, sem retomar benchmarks ou ensaios disruptivos.

Atualização do checkpoint — diagnóstico local: o incremento de `NFR-PRIV-003` citado acima está implementado no candidato combinado Windows, não apenas em helpers. `--diagnostics` opt-in cria writer limitado e slots locais; o bootstrap protege Qt/stderr antes dos plugins e o binding registra avisos reais sem textos livres. O [registro central](../experiments/m0/candidates/qt/README.md#local-diagnostics) reúne os Reds/Greens, core sem Qt 224/224, Release/lint/formato, testes de decoder real e produção sem hooks. A primeira regressão focada foi 11/12 por um contrato de mensagem bruta no harness da instância única; marcadores fechados preservaram as assertions e o alvo voltou a passar com diagnóstico em 2/2. Isso não corrige nem reexecuta a falha antiga do coordinator e não é Green global. Quatro casos de symlink foram pulados, sem elevar privilégios. Continuar a revisão funcional Windows restante a partir deste checkpoint, sem repetir checks verdes sem mudança, sem benchmarks e sem inventar nome/identidade/registro de associações. M4.6/M4.7 e gates nativos/empacotados seguem pendentes onde indicado.

Atualização do checkpoint — consentimento de recuperação: o candidato Windows agora [vincula a substituição futura à revisão do aviso e às fontes apresentadas](../experiments/m0/candidates/qt/README.md#replacement-consent), com rejeição de eventos antigos/reentrantes em QML e C++, sem alterar schemas ou stores. Os checks finais estão no registro central. As próximas lacunas funcionais já auditadas são a validação lexical dos caminhos recebidos por drag-and-drop (NUL decodificado/relativo) e o foco deferido da fila por identidade de ocorrência após inserção incremental; tratar em incrementos próprios com Red comportamental, sem refazer uma auditoria geral. A hipótese separada de ACK já armazenado quando a espera de escrita falha no IPC ainda requer reprodução determinística; o teste nativo antigo não comprova essa hipótese nem foi relaxado/repetido. Nome/associações e gates físicos/empacotados continuam pendentes, sem benchmarks.

Atualização do checkpoint — paths de entrada: a [boundary lexical compartilhada](../experiments/m0/candidates/qt/README.md#filesystem-url-boundary) agora protege drop e seleção antes da descoberta, preservando os caminhos de filesystem Windows suportados e o contrato IPC. Os Reds/Greens e CTest proporcional 6/6 estão no registro central; não repetir a auditoria dessa lacuna. O próximo incremento é o foco deferido da fila já identificado, ainda separado desta correção C++.

Atualização do checkpoint — foco incremental: o [foco da fila por identidade e revisão](../experiments/m0/candidates/qt/README.md#queue-deferred-focus) agora preserva seleção/Delete após inserções, modais e remoção da faixa atual no tail. Os Reds, a regressão do CRUD preservada e o fechamento CTest 3/3/Quick 165/165 estão registrados centralmente; produção sem hooks, lint e formato passaram. Não reabrir os dois incrementos já tratados sem evidência nova. O próximo problema técnico já delimitado é a troca IPC: criar regressão determinística para ACK completo já recebido quando a espera de escrita retorna falha, depois corrigir se a hipótese for confirmada, sem retransmitir comandos nem afrouxar o teste nativo antigo. Nome definitivo/associações, pacote, validações físicas, Linux e benchmarks mantêm seus limites anteriores; não se declara conclusão global.

Atualização do checkpoint — confirmação IPC: a [regressão determinística da resposta já recebida](../experiments/m0/candidates/qt/README.md#ipc-buffered-reply) confirmou o descarte de ACK/NACK quando a espera de escrita falhava. A correção mantém framing/schema/deadline e proíbe retransmissão; os Reds, Green focal 18/18, CTest proporcional 5/5 e produção sem hooks estão no registro central. O teste antigo não foi alterado e passou dentro do coordinator, sem transformar isso em causa histórica demonstrada ou Green global. Não reabrir esse recorte sem nova evidência; identidade/associações, pacote, cenários físicos, Linux e benchmarks continuam nos limites anteriores.

Atualização de identidade — o mantenedor confirmou **RC Music** em 2026-09-05. O [ADR-0004](architecture/decisions/0004-rc-music-identity.md) registra o nome definitivo e preserva os identificadores internos que localizam preferências, sessão e lock. A decisão textual deixou de ser bloqueio; não pedir novamente o nome. O próximo incremento de M4.6 é o template próprio de associações por usuário, integrado ao instalador e limitado à matriz validada, sem `UserChoice`. Registro real, formatos publicados, pacote e gates físicos/acessíveis continuam exigindo suas provas; não antecipar M6/P2, Linux ou benchmarks.

O [incremento de identidade](../experiments/m0/candidates/qt/README.md#rc-music-identity) aplica RC Music ao nome visível sem mover dados locais. Red/Green, CTest proporcional 3/3 (contrato 23/23, Quick 168/168, startup 3/3), build/lint/formato e composição de produção estão registrados centralmente; não repetir os checks sem mudança.

O recorte seguinte materializa o [template NSIS de associações](../experiments/m0/candidates/qt/windows-installer/README.md): funções de cadastro/remoção por usuário e wrappers de seções, com conflitos não mutantes, propriedade, preservação de padrões e teste compilado isolado. A evidência e os limites ficam no [checkpoint do candidato](../experiments/m0/candidates/qt/README.md#rc-music-associations). O módulo ainda não é um instalador completo e não registra o player no host. O próximo trabalho de integração depende de payload e matriz validados; não republicar o pacote histórico de fixture como app atual, não mudar `UserChoice` e não retomar `package-v2`/benchmarks. Evolução do manifesto entre versões e recuperação geral do instalador permanecem contratos posteriores explícitos.

A [montagem local do runtime real](../experiments/m0/candidates/qt/README.md#rc-music-local-runtime) materializa a base seguinte de M4.6/M4.8: executável OFF, QML, plugins, Multimedia/FFmpeg e TagLib separados do SDK, com inventário/hash e probes de runtime fora do payload. Usa build incremental do aplicativo, não rebuild/empacotamento comparativo de M0. O template de associações continua separado até a integração de instalação, atualização e remoção do payload; registro real e publicação de formatos não são inferidos do manifesto experimental. Não encerrar M4.6/M4.8 ou substituir os gates pendentes pelo smoke local.

O [instalador local experimental](../experiments/m0/candidates/qt/README.md#rc-music-local-installer) integra o payload real ao ciclo Windows de instalação/atualização/remoção de M4.8, com versões separadas e preservação de dados. A composição de associações é testada exclusivamente em Registry isolado, enquanto o artefato local mantém essa publicação desabilitada até existir matriz aprovada. Continuar pelos contratos residuais de formatos, atalhos, recuperação, instalação nativa e segurança; não retomar o empacotamento comparativo/benchmarks nem encerrar M4.6/M4.8 por esse incremento.

O recorte seguinte entrega o [atalho do menu Iniciar](../experiments/m0/candidates/qt/windows-installer/README.md#contrato-do-atalho-windows), atualizado junto à versão ativa, removido apenas quando próprio e preservado diante de desinstalador obsoleto ou conflito externo. A migração de instalação sem atalho também pertence ao contrato. Os checks compilam o mesmo NSIS com menu e Registry isolados; não alteram o menu real nem lançam o player. Prosseguir pelos contratos residuais de formatos, recuperação e jornadas Windows nativas; M4.6/M4.8 e gates amplos continuam abertos.

O recorte de [formatos locais](../experiments/m0/candidates/qt/README.md#rc-music-local-formats) fecha a ligação explícita entre a matriz caracterizada e o builder, além da evolução controlada da lista entre versões. O instalador pode ser compilado ON somente com `-FormatMatrix`; compilar não instala no host. Prosseguir pelas lacunas funcionais Windows ainda comprovadamente abertas, sem repetir estes checks sem mudança relevante; registro/ativação nativos, recuperação geral, máquina limpa e segurança/distribuição mantêm seus gates separados.

Após o commit documental de M0.10/ADR-0005, iniciar a promoção M1 delimitada acima. Isso não declara medições históricas, demais etapas M0 ou gates de release concluídos.

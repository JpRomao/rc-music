# Estado e decisões do projeto

**Atualizado em:** 2026-09-06

**Estado vigente:** C++20 + Qt Quick/QML, Qt Multimedia/FFmpeg e TagLib foram aceitos pelo mantenedor no [ADR-0005](architecture/decisions/0005-windows-product-stack.md). A/B e checks Rust/Tauri deixam de ser pré-requisitos para desenvolver o produto Windows. A decisão não é uma vitória comparativa medida; Linux e benchmarks ficam adiados, com requisitos e budgets preservados.

**Promoção técnica existente, M1 ainda aberto:** o [núcleo extraído](core-build.md) e a [aplicação gráfica](desktop-build.md) compilam na raiz, sem encaminhar o configure ao experimento. O produto e o consumidor histórico compartilham uma implementação. As suites de [áudio](audio-tests.md), [UI](ui-tests.md), [integrações Windows](windows-integration-tests.md) e [processo](process-tests.md) estão no build raiz; cenários, fakes e hooks ficam nos alvos exclusivos de testes, não na aplicação real ON/OFF. Os [comandos locais](development-commands.md) e a [rastreabilidade executável](quality/traceability.md) também estão implementados. Não foram alterados dados/identificadores `MusicPlayer` ou evidências históricas.

**Entrega local existente:** o [runtime Release/OFF sem terminal adicional](windows-runtime.md#runtime-sem-terminal-adicional), incluindo atalhos e modos de fila, foi gerado do commit `bfeb90f`, com 554 arquivos/hashes conferidos e identidade/subsistema GUI 6/6. Os runtimes anteriores e os [dois instaladores antigos](windows-installer.md) estão preservados; esses setups não contêm as mudanças novas. A autorização foi **“Sim, gerar sem instalar”**; não houve instalação. Permanecem privados/experimentais, sem assinatura ou aprovação de distribuição. O [manual de uso e solução de problemas](user-guide.md) descreve a operação implementada, sem substituir a validação nativa.

**Lacunas de fechamento:** cobertura ainda não apurada; CI Windows sem provedor/runner definido; jornadas nativas e de instalação/upgrade/uninstall sem fechamento; segurança/licenças/notices/SBOM, CRT/máquina limpa e acessibilidade final ainda abertos. Linux e benchmarks seguem adiados. Esses gates não significam que reprodução, fila, preferências e sessão precisem ser reimplementadas, nem permitem declarar M1/M4/M5 concluídos.

O incremento de 2026-09-06 refina os atalhos e antecipa o recorte autorizado de FR-019: Espaço controla reprodução sem clicar o botão focado, setas controlam saltos/volume, Shift+setas navega, F1 alterna aleatório e F2 repetição. Os dois modos têm estado visível e persistido; repetição roda a ocorrência concluída para o fim, e aleatório percorre a rodada sem perder pendentes. Consulte o [manual vigente](user-guide.md); as regras históricas de foco e de fila sem repetição abaixo descrevem os baselines anteriores, não substituem esse contrato.

**Próximo escopo aceito, não implementado:** o [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md) registra Brave/Chromium, Lite/Completa e extensões principal/YouTube independentes. A Lite preserva o player sem navegador interno; a Completa acrescenta navegação isolada. Ambas integrarão a extensão externa opcional, com lista/seleção múltipla e download/conversão MP3. Não há engine/downloader aprovados ou extensões disponíveis ainda; YouTube não bloqueia a principal e conserva revisão própria.

## Histórico funcional anterior à aceitação

Os marcos e pendências abaixo registram os respectivos recortes históricos, não uma auditoria atual de todas as funcionalidades. Referências a stack ainda proposta ou A/B suspenso descrevem aquela fase e foram substituídas pelo ADR-0005; a evidência por incremento e o próximo checkpoint vigente ficam no [registro Qt](../experiments/m0/candidates/qt/README.md) e no [plano](development-plan.md).

**Fase:** M0.6 em andamento, com empacotamento/A-B/automação nativa/benchmarks pesados suspensos; lifecycle/eventos tipados e geração isolada estão verdes, assim como `PB-001`/`PB-006` para WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis e `PB-004` para MP3 VBR/M4A AAC via Qt Multimedia; `MR-001`, `MR-003`, `MR-018`, `MR-019` e `MR-101`–`MR-103` no TagLib cobrem a matriz básica por conteúdo, enquanto polling/`MR-016`/`MR-017`/`MR-020` permanecem verdes no reader assíncrono limitado. Uma caracterização separada do extrator retorna `UnsupportedFormat` para Opus/ALAC, mas playback inicia independentemente e o Qt pode decodificar candidatos, logo não há rejeição ponta a ponta nem classificação de suporte. A composição UI oferece `.mp3`, `.m4a`, `.mp4`, `.flac`, `.ogg`, `.oga` e `.wav` em variantes minúsculas/maiúsculas e percorre nove linhas válidas sobre sete fixtures autenticadas com fallback Unicode, autoplay, play/pause, seek e metadata assíncrona com adapters reais e saída mutada. O erro local agora conserva título/contexto Unicode e oferece reabrir; regressões de metadata/playback/integração usam prefixos temporários de 64 bytes derivados de MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis para cobrir erro tipado, M4A sem faixa de áudio, recuperação e descarte de geração antiga. Esses derivados não são fixtures canônicas; M0.5/M0.6, áudio audível, comportamento ponta a ponta dos candidatos, hostis canônicos restantes, arte/limites, gates nativos e decisão de stack permanecem pendentes

No modo real, a fila local também coordena eventos terminais por geração e ID: `Ended` avança sem circular com autoplay; no tail, as duas opções mantêm ou limpam a seleção e sua escolha agora é persistida pelo recorte M3.5. Erros de origem marcam somente a ocorrência corrente e tentam continuar preservando reprodução/pausa; `Busy` mantém recuperação manual e snapshots terminais repetidos/antigos não alteram a intenção nem a nova faixa. Uma caracterização Qt Multimedia real comprova válida A → `InvalidMedia` → válida B e o segundo `Ended` pausado no tail, sem wrap, quarta carga ou reset do modelo. Os demais gates ainda impedem concluir FR-011/FR-016/M3.3.

A entrada por drag-and-drop de arquivos e diretórios locais está funcional no candidato Qt/Windows. A descoberta recursiva usa worker único, batches limitados, mailbox com backpressure, cancelamento por geração e profundidade máxima; resultados são inseridos incrementalmente por âncoras de ocorrência. A UI cobre posição antes/depois/fim sobre a fila, append fora dela, lote misto, erro parcial, progresso, cancelamento, foco e anúncios acessíveis. Handles Windows são abertos sem seguir reparse points, a identidade do diretório é revalidada e a interrupção de I/O é repetida até o reconhecimento do cancelamento ou a saída confirmada no teardown. M3.2 permanece parcial porque não houve `CHK-PERF` de 1.000 itens/stall de 100 ms, volume lento com deadline, validação nativa de symlink/NVDA nem E2E empacotado.

M3.6 está funcional somente no candidato Qt/Windows. Strings essenciais são centralizadas em catálogos QML `*Text.qml` e no catálogo C++ do filtro nativo; o pt-BR é gerado por `qt_add_translations` e instalado em runtime por `QTranslator`, com plural correto para “1 segundo”. Erros locais possuem causas tipadas, texto seguro e ação de recuperação. Uma seleção não vazia rejeitada sincronamente preserva fila/contexto e oferece nova escolha; importações parciais/rejeitadas também são acionáveis. A fila acessível omite IDs de ocorrência e códigos técnicos, e texto longo permanece contido em 720 × 520 nos testes automatizados. Isso não aceita a stack nem fecha M3: NVDA, DPI, alto contraste e movimento reduzido nativos, pacote/E2E e os demais gates indicados continuam abertos. O recorte M3.7 abaixo é posterior; Linux, A/B e benchmarks seguem suspensos até a paridade funcional Windows.

M3.7 está funcional no recorte automatizável do candidato Qt/Windows. Teclado, foco entre estados/transporte/modais, ToolTip do progresso ativo sem seek e motivos acessíveis de indisponibilidade, inspeção `QAccessibleInterface`, anúncios sem spam ou competição com importação, contraste do foco e movimento reduzido estão cobertos. O fechamento passou 174/174 no CTest combinado e Quick 90/90 em 100%, 150% e 200%, além de lint/formato e composição sem testes. A validação nativa com NVDA, DPI, alto contraste e movimento reduzido continua pendente e impede encerrar os gates globais de M3. M3.8 permanece adiado; o desenvolvimento Windows avançou para a sessão real de M4.1 descrita abaixo.

M4.1 está em andamento no candidato C++/Qt/Windows. Snapshot/hidratação puros preservam IDs, ordem, paths Unicode e duplicatas; disponibilidade permanece transitória. A porta separada, o codec JSON v1 exato e o store Windows em `session.json`/`.bak` estão verdes. O store usa replace atômico sem fallback direto, lock cooperativo, backup byte a byte, recuperação sem reescrita, proteção de versão futura e falha fechada para arquivo ilegível. Writer e autosave coordenam debounce estrutural, checkpoint fixo de posição, gatilhos urgentes, retry explícito e shutdown seguro para callers concorrentes, embora o `join()` ainda não tenha deadline se o store síncrono não retornar.

O quinto recorte integrou essa infraestrutura ao executável. O bootstrap consulta a sessão somente quando `restoreLastSession` está ligado; o presenter hidrata fila/item antes do QML, restaura sempre pausado e aplica os 750 ms somente à geração correta, sem autoplay nem avanço de EOF. Mudanças estruturais e posição confirmada alimentam o autosave; seek, pausa, eventos terminais e saída disparam checkpoint imediato, e retry, versão futura, timeout e reparo ao reativar a preferência permanecem acionáveis. Naquele fechamento, o Release combinado passou 214/214, o core Qt OFF 172/172 e lint/formato/composição sem testes ficaram verdes.

O sexto recorte cobre localmente os cenários destrutivos que faltavam. Um shell Release instrumentado é encerrado por `QProcess::kill()` em `BeforePrimaryCommit`, depois de confirmar primário intacto, backup byte a byte e owner PID do lock; o save seguinte reaproveita e remove o lock obsoleto, e o relançamento restaura a faixa pausada. Primário corrompido recupera o backup; uma negação real de delete sharing força falha de replace e o retry QML repara o arquivo quando o handle é liberado. Dois documentos inválidos usam defaults vazios e aviso dispensável sem reescrita. Em duas linhas multiprocesso, `BeforeBackupCommit` e `AfterPrimaryCommit`, o PID do holder é confirmado, o concorrente falha sem mutação e uma gravação posterior sucede depois da liberação. Isso não prova perda física de energia nem as jornadas empacotadas/nativas correspondentes; `CHK-E2E`, os gates de fechamento, FR-043/FR-044, NFR-REL-003 e M4.1 permanecem formalmente abertos.

O fechamento automatizado final desse recorte passou 214/214 no CTest Release combinado, 172/172 num core fresco sem Qt com MSVC 19.44/toolset 14.44 e 1/1 no alvo Quick final, que cobriu 99 casos. `all_qmllint` ficou verde. O formato cobriu 109 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não oferece `--check`, a conferência QML usou sua saída padrão normalizada em LF. O rebuild `BUILD_TESTING=OFF` listou zero testes e rejeitou o hook reservado de crash com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Isso não fecha pacote/nativo, `CHK-E2E`, M4.1 nem qualquer dos demais gates ainda abertos.

M4.2 está funcional localmente no candidato Qt/Windows. Preferências agora usam JSON schema v2 com `WindowPlacement` opcional; v1 é migrado em leitura para placement ausente sem promoção/regravação, e versões futuras continuam protegidas. A policy pura restaura apenas quando o retângulo, normalizado ao mínimo `720 × 520`, cabe integralmente em uma `availableGeometry` individual em DIPs, com coordenadas negativas aceitas e overflow rejeitado; monitor removido ou DPI alterado levam ao fallback seguro. O root QML nasce oculto e recebe tela/geometria antes da revelação, enquanto a captura normal-visível é coalescida e drenada antes de ocultar, minimizar ou sair pelos casos de uso. Transições nativas não normais descartam qualquer candidato ainda ambíguo, preservam o último normal estabilizado e recapturam ao retornar a normal. Um restart instrumentado entre dois processos persiste e restaura a geometria. Isso acrescenta agora a migração que o marco histórico M3.5 ainda não exercitava, mas não prova pacote, desktop nativo ou DPI físico: `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` e M4.2 permanecem abertos globalmente.

Os checks finais de M4.2 passaram 225/225 no CTest Release combinado, 183/183 num core fresco sem Qt e 1/1 no alvo Quick com 100 casos. `all_qmllint`, formato de 112 C++/46 QML e a composição `BUILD_TESTING=OFF` também ficaram verdes; esta listou zero testes e rejeitou o hook reservado com exit 1 sem criar sua raiz. O QML foi comparado à saída padrão normalizada em LF porque `qmlformat` 6.11.2 não possui `--check`. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

M4.3 está funcional localmente no candidato Qt/Windows. O shell adquire o lock e o `QLocalServer::UserAccessOption` antes de construir preferências, sessão, backend e QML. Secundárias resolvem argumentos/paths contra o próprio CWD, enviam somente `openSources` v1 dentro dos limites e saem após ACK/NACK, sem disputar o dispositivo. A principal ativa a janela e serializa os lotes no importador real. O contrato rejeita o lote inteiro acima de 1.000 paths ou de 1 MiB de JSON, limita fila/bytes/conexões/buffers/timeouts e drena comandos já ACKados antes de persistir e sair. Paths relativos, rooted-only, drive/UNC/extended canônico, Unicode e CWDs diferentes têm cobertura; formas ambíguas, recursos Qt, NUL, namespaces Win32 de dispositivo e grafias pseudo-extended são recusados.

Essa prova ainda é Release instrumentada/local. Explorer/associação instalada, quoting nativo, foreground efetivo, ACL entre usuários, fuzz/property do protocolo e pacote continuam pendentes; logo `CHK-WIN`, `CHK-E2E`, `CHK-SEC`, `CHK-CLOSE` e o fechamento global de M4.3 permanecem abertos. Nenhum benchmark foi executado.

Os checks finais de M4.3 passaram 229/229 no CTest Release combinado. O teste de processo Release instrumentado confirmou dois comandos ACKados — B em path Windows comum acima de 260 caracteres —, NACK para novo ingresso durante a drenagem, A+B persistidos e backend único. `m0_qt_shell_ui_qmllint`, formato de 122 C++/46 QML e validação documental ficaram verdes. O build `BUILD_TESTING=OFF` foi recompilado, listou zero testes, rejeitou o hook reservado com exit 1 usando o runtime autenticado e reteve `longPathAware=true` no manifesto. A conferência QML comparou a saída normalizada, pois `qmlformat` 6.11.2 não possui `--check`.

M4.4 está funcional no recorte local automatizável do candidato Qt/Windows. A aplicação decide a disposição inicial depois do placement: preferência desligada mostra; ligada com tray disponível entra no event loop oculta; sem tray permanece visível e apresenta aviso. Fonte inicial explícita ou fonte posterior via IPC prevalece e restaura/ativa a janela. O teste de processo Release instrumentado também percorre mostrar/ocultar/mostrar pelas `QAction` reais da tray. A opção pode ser desligada mesmo depois de a tray desaparecer; a ajuda explica que ativá-la exige uma bandeja funcional, e a nota de sessão é condicional à preferência de restauração.

O adapter agora distingue a invisibilidade do bootstrap da intenção `hidden_to_tray`; apenas ocultação explícita arma a recuperação, e perda da tray nesse estado devolve uma janela visível/minimizada. O commit de saída ocorre somente após drenagem de fontes ACKadas e sucesso de preferências/sessão; depois chama `PlaybackBackend::shutdown()` uma vez e só então solicita a saída da plataforma. Falha ou timeout cancelam a intenção sem desligar o backend.

Os Reds focados cobriram disposição, ownership da ocultação, toggle, precedência de fonte e ordem de shutdown; os Greens correspondentes antecederam a consolidação. O Release combinado passou 229/229 e o core Qt OFF 183/183. `m0_qt_shell_ui_qmllint` ficou verde, e o formato cobriu 122 C++/46 QML com comparação da saída padrão normalizada do `qmlformat`. O build `BUILD_TESTING=OFF` foi recompilado, listou zero testes e recusou os dois hooks M4.4 com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

M4.4 continua aberta globalmente por pacote/desktop e tray nativos, primeiro frame perceptível sem flash, fallback Linux real, restart do Explorer, NVDA/acessibilidade nativa e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y` e `CHK-CLOSE`.

M4.5 está funcional localmente no candidato Qt/Windows. A porta pura `SystemMediaControls` mantém metadata, estado, capacidades e timeline fora da plataforma; o binding deduplica a projeção do presenter e encaminha Play, Pause, Previous, Next e Seek aos mesmos casos de uso de UI, atalhos e tray. O controller separa intenção desejada de comando em voo para compensar confirmações assíncronas obsoletas sem duplicação.

O adapter C++/WinRT vincula SMTC ao `HWND` real por `ISystemMediaTransportControlsInterop::GetForWindow`, publica status/botões/metadata/timeline, faz marshalling dos callbacks para a thread da UI, falha fechado e refaz o vínculo quando a surface nativa é recriada. O smoke QPA Windows comprovou SMTC real, publicação e limpeza em `Closed`; não sintetizou tecla física. O fechamento passou 239/239 no Release combinado, 190/190 no core Qt OFF, os três alvos SMTC, `qmllint`, `clang-format` em 131 C++ e a composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado. Nenhum QML mudou e nenhum benchmark foi executado.

M4.5 continua aberta por teclas de mídia físicas, pacote, MPRIS/Linux e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E` e `CHK-CLOSE`.

M4.6 possui agora um primeiro recorte funcional local no candidato Qt/Windows. `PlatformIntegration` separa suporte da plataforma, disponibilidade momentânea e conclusão tipada; o presenter publica `Idle`, `Opening`, `Dispatched`, `Unavailable` e `Failed` por sinal dedicado. `QtShellPlatform` abre somente a URI interna fixa `ms-settings:defaultapps` via `QDesktopServices`; `Dispatched` comprova apenas o handoff ao handler. O botão e o feedback acessíveis aparecem somente em plataforma suportada, e anúncios de indisponibilidade/falha ocorrem apenas com o painel aberto e visível. Uma saída ainda reversível não invalida a conclusão antes do commit efetivo.

Os três alvos focados ficaram verdes individualmente: `m0.qt.shell-platform` 1/1, `m0.qt.presenter.initial-load` 1/1 e `m0.qt.quick.shell-states` 1/1. A consolidação passou 239/239 no CTest Release combinado e 190/190 no core Qt OFF; o Quick passou 1/1 com 100 casos internos, `m0_qt_shell_ui_qmllint` ficou verde e o formato cobriu 131 C++/46 QML. A composição `BUILD_TESTING=OFF` recompilou, listou zero testes e rejeitou o hook reservado com exit 1 sem criar a raiz solicitada; o validador documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. M4.6 continua parcial: não há escrita em `UserChoice`, ProgID, `Capabilities`, `RegisteredApplications`, matriz publicada de formatos, associação, pacote/instalador ou implementação Linux.

M4.7 possui agora um primeiro recorte funcional local limitado a suspend/resume no candidato Qt/Windows. A porta C++ pura `SystemPowerEvents` fica separada de `PlatformIntegration`; `SystemPowerEventsBinding` entrega os eventos ao presenter. O adapter `WindowsPowerEvents` cria na thread da UI um `QWindow` privado e invisível, registra seu `HWND` estável por `RegisterSuspendResumeNotification`/`DEVICE_NOTIFY_WINDOW_HANDLE` e mantém esse receptor independente da janela/surface visual até shutdown. Somente `PBT_APMSUSPEND` e `PBT_APMRESUMEAUTOMATIC` nativos dirigidos ao receptor viram callbacks síncronos; o filtro consome os broadcasts reconhecidos com resultado `TRUE`, inclusive duplicados, enquanto entradas alheias retornam `false`. Duplicatas nativas consecutivas são suprimidas, mas uma retomada nativa inicial ainda é aceita. Falha inicial preserva o receptor e usa backoff coarse single-shot finito de 1 s, 5 s e 30 s; depois de esgotado, o orçamento só reinicia por `start()` explícito ou `applicationStateChanged`. Shutdown cancela o timer, desfaz o registro e torna retry/callback tardio inerte. Destruir ou recriar a surface visual não altera o receptor nem a inscrição.

Na suspensão, o presenter captura a última posição confirmada e solicita save imediato assíncrono, sem `flush()`, pause, play ou autoplay; seu polling funcional fica quiescente enquanto persistência pendente ainda pode ser acompanhada. Na retomada, capabilities e snapshot do backend são relidos, e estado, posição e erro da geração corrente são reconciliados antes de rearmar o polling aplicável. Resultado dos alvos focados e dos checks amplos: build Release aprovado, 241/241 testes no CTest combinado sequencial, 190/190 no core Qt OFF, `qmllint` e formato de 139 C++/46 QML verdes; `BUILD_TESTING=OFF` recompilado com zero testes e hook reservado rejeitado em exit 1 sem criar a raiz solicitada; validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas aprovada. Nenhum QML mudou e nenhum benchmark foi executado.

Este recorte cobre somente suspend/resume de `FR-038` e não encerra M4.7. Não houve sleep físico, Modern Standby ou hibernação; o save assíncrono pode ainda não estar durável quando o processo for congelado. Perda de device/drive, arquivo removido, restart do Explorer, pacote/E2E nativo, Linux e `NFR-REL-005` continuam pendentes.

O segundo recorte de M4.7 introduz a ação acessível “Tentar reproduzir novamente” na fila e no erro. `retryQueueOccurrence(ID)` reutiliza somente o path interno da ocorrência original, preserva ID, ordem, duplicatas e modelo, e mantém a indisponibilidade durante o loading até `Ready`, `Playing` ou `Paused` da nova geração. Falha conserva diagnóstico e permite nova tentativa explícita, sem loop ou avanço automático; load rejeitado conserva seleção e backend anteriores. A intenção explícita prevalece sobre lotes posteriores da importação, e a contagem de problemas mantém o histórico por ID mesmo depois de recuperar um item. “Abrir outro áudio” continua disponível. O [registro central do segundo recorte](../experiments/m0/candidates/qt/README.md#retry-m47) distingue contrato, evidência intermediária e checks finais. Dispositivo, drive físico, localizar arquivo em outro path, Explorer, pacote/E2E nativo, Linux e os gates globais de M4.7/`NFR-REL-005` permanecem pendentes.

**Promoção do código para o produto:** autorizada pelo ADR-0005, mas ainda não concluída; o diretório experimental não passa automaticamente a ser uma unidade de produto independente.

## 1. O que está confirmado

Os itens de produto/processo abaixo vieram diretamente do mantenedor:

- player desktop pessoal, leve e rápido;
- Windows é mais importante que Linux, mas ambos devem ser contemplados;
- C++20 + Qt Quick/QML, Qt Multimedia/FFmpeg e TagLib são a stack aceita para o produto Windows;
- UI/UX moderna, leve e minimalista é a prioridade mais alta;
- MP3, M4A e outros formatos comuns devem ser investigados/suportados;
- bandeja no Windows;
- metadados incluindo tamanho e duração;
- declaração/fluxo para escolha como aplicativo padrão;
- barra de progresso arrastável e seek;
- saltos padrão de −5 s/+5 s configuráveis;
- temas claro e escuro;
- edição de tema é desejável, mas não prioritária;
- áudio de YouTube por URL é possibilidade futura, se viável;
- desenvolvimento seguirá TDD;
- `agents.md` deveria ser o primeiro arquivo;
- mudanças devem ser sempre commitadas;
- o M0 foi autorizado originalmente com Qt como hipótese principal e Tauri como desafiante, com shell/UI e áudio comparados separadamente; esse processo seletivo é histórico, substituído pelo ADR-0005;
- A/B não é mais pré-requisito da stack ou de M1; benchmarks e Linux permanecem adiados, sem resultados presumidos;
- o baseline derivado, fila/sessão P0 e o experimento M0.0–M0.10 foram autorizados em 2026-08-23.

Os contratos experimentais derivados sob a autorização do M0 são:

- Windows 11 x64 é Tier 1; Ubuntu 24.04 LTS amd64/GNOME é Tier 2; Windows 10 é somente legado/best effort;
- ambiente `M0-E1`, protocolo `M0-P1` e layout `M0-L1` ficam congelados antes da coleta.
- M0.1 foi concluído em `24d12ea` com fixture, harness, summary comum e piloto sintético `P` não decisório; nenhuma coleta A/B foi executada.

Esses contratos preservam o método e a evidência do experimento. A aceitação posterior da stack decorre da decisão explícita do mantenedor no ADR-0005, não de um resultado A/B.

## 2. Decisões vigentes e propostas remanescentes

| Decisão | Recomendação | Estado |
|---|---|---|
| Shell/UI | C++20 + Qt 6 Quick/QML Basic customizado, com domínio e aplicação em C++ puro | Aceito no ADR-0005; ADR-0001 substituído |
| Áudio | Qt Multimedia/FFmpeg atrás de `PlaybackBackend` | Aceito no ADR-0005; ADR-0002 substituído |
| Metadata | TagLib atrás da porta de leitura | Aceito no ADR-0005 |
| Alternativas M0 | Rust/Tauri/Svelte, Rodio/Symphonia/Lofty | Não escolhidas; artefatos históricos preservados, sem checks obrigatórios para o produto |
| Estrutura | uma unidade de build e distribuição com módulos internos; fonte única | Aplicação, testes e receitas promovidos; cobertura, CI e gates M1 pendentes |
| Persistência | JSON versionado e atômico; sem banco no MVP | Implementada localmente; preservar schemas e raízes MusicPlayer |
| Integração | portas e adapters Windows da implementação existente; Linux posterior | Aceitação da stack não conclui gates nativos/funcionais |
| Windows package | runtime/instalador NSIS local experimental por usuário | Evidência local separada; release, licenças e máquina limpa pendentes |
| Linux package | `.deb` primeiro, AppImage depois | Proposto |
| Remoto | porta isolada, desativada no MVP | Proposto em ADR-0003 |

Alternativas preservadas:

- Rust + Slint somente se a evidência justificar um experimento adicional de footprint;
- GStreamer/libmpv apenas como substituição de engine quando requisitos futuros justificarem.

## 3. Baseline derivado aprovado para execução

Estes comportamentos não foram todos pedidos literalmente, mas foram autorizados em D0.9 como baseline. Alteração posterior exige atualização explícita dos requisitos/plano:

- fila básica;
- volume/mudo e anterior/próxima;
- CLI e instância única;
- atalhos/SMTC/MPRIS;
- restauração pausada de sessão;
- preferências atômicas e versionadas;
- contraste, teclado, leitor de tela e reduced motion;
- valores de salto independentes entre 1 e 300 s;
- “anterior” reinicia após 3 s;
- pt-BR inicial com strings preparadas para tradução;
- metas de startup, memória, CPU e latência;
- limites iniciais de arte/metadata/IPC, a validar no M0;
- NSIS como instalador local experimental Windows por usuário e `.deb` como proposta de primeiro pacote Linux; distribuição e validações nativas mantêm gates próprios.

Os detalhes executáveis permanecem nos [requisitos](product/requirements.md), na [UX](product/ux-specification.md) e no inventário de preferências do [registro M0](m0/README.md).

## 4. Decisões em aberto

1. definir repositório/provedor/runner para CI Windows e obter evidência de cobertura e dos gates residuais M1 da unidade já promovida;
2. definir a identidade visual final, preservando o nome RC Music já aceito no ADR-0004;
3. licença para eventual distribuição pública;
4. exigir versão portátil no 0.1 ou depois;
5. aprovar/recalibrar budgets após medições;
6. decidir no futuro se YouTube será pesquisado, oferecido via player oficial ou rejeitado.

Nenhuma dessas perguntas reabre automaticamente a seleção de stack. ADR-0001/0002 estão `Substituído` pelo ADR-0005 `Aceito`; ADR-0003 permanece `Proposto`. M1 não depende de concluir A/B, benchmarks ou Linux, mas sua promoção técnica precisa de implementação e evidência próprias.

## 5. Alegações que ainda não podem ser feitas

- “o app já oferece suporte publicado a WAV/MP3/M4A/FLAC/Ogg/Opus”: a matriz explícita da entrega local e suas evidências não aprovam release ou todos os perfis desses contêineres/codecs. Os testes de integração usam saída mutada, sem prova audível/dispositivo físico; Opus/ALAC não são declarados por essa matriz. A aceitação do engine não amplia automaticamente os formatos suportados;
- “é leve”: os budgets são metas, não medidas;
- “funciona em Linux”: arquitetura e plano existem, build não;
- “já está validado como app padrão no Windows”: launcher, instalador e cadastro de ProgID/`Capabilities`/`RegisteredApplications` têm recortes locais e testes isolados; isso não comprova instalação/associação no host, escolha confirmada pelo usuário no Windows ou distribuição aprovada. O ADR-0005 não autoriza essas mutações nem escrita em `UserChoice`;
- “a recuperação de sessão está concluída”: `restoreLastSession` já controla a leitura e a restauração local de fila/item/posição sempre pausada no candidato Qt/Windows, inclusive nos cenários destrutivos descritos acima; `startMinimized` também possui efeito local condicionado à tray em M4.4. As jornadas empacotadas/nativas de M4.1/M4.4 continuam pendentes, portanto FR-042/FR-043/FR-044 não estão integralmente concluídos;
- “a instância única está concluída no Windows”: M4.3 já possui lock na raiz de configuração do usuário, `QLocalServer::UserAccessOption`, argumentos nativos, IPC `openSources` limitado, uma única inicialização do backend e drenagem de comandos ACKados no executável Release instrumentado; associação instalada/double-click pelo Explorer, foreground real, quoting do shell, ACL entre usuários, fuzz/property e o pacote nativo ainda não foram aprovados;
- “alto contraste está resolvido”: existe implementação e evidência automatizada com paletas injetadas, mas isso não comprova Windows real, ramos internos do Qt Basic, Linux, NVDA ou DPI nativo nem fecha UX-006;
- “a bandeja está validada no Windows”: menu, avisos, fallback, start behavior e saída ordenada passaram em testes automatizados e de processo Release instrumentado, mas isso não comprova ícone/tray/Explorer reais, primeiro frame sem flash, ativação nativa, NVDA ou Linux;
- “o pacote está validado em máquina sem SDK ou pronto para distribuir”: inventário, imports PE e smokes offscreen original/relocado passaram sem paths Qt do SDK no ambiente filho, mas isso não remove o SDK do host nem resolve CRT externo, licenças, segurança ou ambiente limpo;
- “a aceitação da stack aprova o SDK e todos os riscos transitivos”: builds/runtimes e correções possuem evidências específicas, sem aprovação global de segurança, ABI, CRT, licenças/notices/SBOM ou redistribuição. A receita histórica `package-v2` não vira dependência de M1 nem ganha resultado por este ADR;
- “YouTube é suportado”: é pesquisa futura com possível incompatibilidade de políticas;
- “a stack venceu o A/B” ou “a aceitação concluiu M1/release”: o ADR-0005 aceita a direção técnica por decisão explícita; não há vitória comparativa medida, promoção automática do diretório experimental nem dispensa dos gates restantes.

## 6. Prontidão documental

| Área | Documento | Estado |
|---|---|---|
| Contrato de trabalho | [agents.md](../agents.md) | Concluído |
| Visão | `product/vision.md` | Baseline |
| Requisitos | [Requisitos](product/requirements.md) | Para revisão |
| UX | [UX](product/ux-specification.md) | Proposta para protótipo |
| Stack | [Análise](architecture/stack-analysis.md) | Stack aceita no ADR-0005; comparação histórica preservada |
| Arquitetura | [Arquitetura](architecture/architecture.md) | Unidade real promovida; critérios de saída M1 ainda abertos |
| ADRs | [Índice](architecture/decisions/README.md) | ADR-0004/0005 aceitos; ADR-0001/0002 substituídos; ADR-0003 proposto |
| TDD | [Estratégia](quality/testing-strategy.md) | Contrato inicial |
| Gates | [Gates](quality/quality-gates.md) | Contrato inicial |
| Corpus | [Corpus](quality/test-corpus.md) | M0.5 em andamento; WAV e matriz básica de codecs autenticados, sem suporte publicado; os prefixos truncados atuais são derivados temporários, não fixtures canônicas; arte/limites e truncados versionados pendentes |
| Plano executável | [Plano](development-plan.md) | Baseline de execução |
| Roadmap | [Roadmap](roadmap.md) | Proposto |
| Workflow | [Workflow](development-workflow.md) | Contrato inicial |
| Segurança | [Threat model](security/threat-model.md) | Baseline local |
| Manual | [Uso e solução de problemas](user-guide.md) | Operação local documentada; não é aceite nativo ou RC |
| Release | [Processo](delivery/release-process.md) | Gates atuais separados dos artefatos privados já gerados |
| M0 | [Ambiente, protocolo e evidências](m0/README.md) | Histórico preservado; decisão de stack atendida pelo ADR-0005; ensaios não executados permanecem sem resultado e não bloqueiam M1 |

## 7. Próximo passo recomendado

Com a stack aceita, a sequência atual é:

1. manter M4.1–M4.7 formalmente abertas até os gates residuais correspondentes, registrando separadamente a evidência funcional local já verde;
2. preservar a unidade C++/Qt, testes e receitas promovidos e obter as evidências residuais de M1, especialmente cobertura e CI Windows, sem repetir a separação já verificada;
3. continuar pelas lacunas funcionais Windows do checkpoint vigente, sem impor A/B, toolchains do ramo não escolhido, `package-v2`, benchmarks ou Linux como pré-requisitos; preservar requisitos residuais e não retomar interação nativa interrompida ou instalar/registrar no host sem autorização própria.

Os parágrafos seguintes preservam o histórico de implementação e validação; menções a decisão pendente não descrevem o estado vigente após o ADR-0005.

No modo de fixture, a navegação manual segue a [política experimental comum](m0/README.md): anterior reinicia após 3 s quando há seek ou busca o anterior disponível; próxima pula indisponíveis sem circular a fila. Trocas começam em zero e preservam estado de reprodução, volume, mudo e modelo; vazio, carregando, erro e seek ocupado não navegam. EOF continua `clamp_paused`, e a recuperação do cenário de erro permanece separada. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

No modo real, `Ended` e erro de origem já coordenam a fila conforme o parágrafo de fase acima, inclusive as duas opções de fim cuja escolha passou a ser persistida em M3.5. Tocar linha e CRUD/reordenação estão ligados por IDs estáveis. Adicionar preserva a atual; `Ctrl+O` anexa e toca a primeira nova ocorrência. Mover, remover e limpar têm botões, teclado e menu; toda limpeza não vazia pede confirmação, e a reordenação emite anúncio coberto por Quick. Remover a atual tenta apenas o sucessor à frente, sem circular. Drag-and-drop/diretórios já usam descoberta incremental no worker e projeção em lotes; a transição terminal padrão também está coberta com adapters reais. `CHK-PERF`, E2E empacotado e NVDA/manual continuam pendentes, então M3.1/M3.2/M3.3 permanecem parciais pelos gates residuais.

A seção 5 também registra atalhos locais com TDD para abertura, transporte, Preferências e saída, incluindo guardas de foco/modal, autorrepetição e volume fracionário. Não são atalhos globais nem aprovação de FR-037; o stage histórico continua anterior a essa correção.

A fatia de preferências/temas foi fechada em `ab87803`; naquele marco, as preferências ainda eram somente em memória. A [política comum e evidência](m0/README.md) preserva esse histórico na seção 6 e registra separadamente HC na seção 7. Alto contraste está implementado com precedência temporária sobre os três temas, pares opacos e movimento independente; os checks automatizados passaram após as correções da auditoria independente. Fixture/protocolo permanecem inalterados. O recorte M3.5 posterior tornou funcional no candidato Qt/Windows o armazenamento atômico e recuperável das dez preferências, sem concluir M3 nem aceitar a stack. Naquele recorte, `restoreLastSession` e `startMinimized` apenas persistiam; os quinto e sexto recortes M4.1 implementaram a restauração pausada, e M4.4 integrou localmente o início minimizado depois do placement, condicionado à tray e superado por fontes explícitas. O restart automatizado é Release instrumentado/offscreen, e não há prova de pacote, primeiro frame nativo sem flash, NVDA, migração histórica ou perda física de energia. O deadline visual de saída não cancela fisicamente uma gravação síncrona de armazenamento já ativa, e o shutdown do writer ainda pode aguardar `join()` sem limite. Paletas injetadas e escalas offscreen tampouco comprovam alto contraste nativo, DPI real ou aprovação de UX-006.

O recorte M3.6 posterior separa texto essencial de estado/ações em catálogos traduzíveis, inclui o filtro do diálogo nativo e instala o catálogo pt-BR compilado em runtime. O contrato cobre plural, causas de erro seguras, recuperação sem perda de fila/contexto, fila sem identificadores técnicos verbalizados, importação terminal acionável e cópia longa no mínimo 720 × 520. O fechamento automatizado passou 174/174 no Release combinado, 135/135 no core Qt OFF, Quick 64/64 nas escalas padrão, 1,5× e 2×, `qmllint` silencioso, formato de 90 C++/39 QML e a composição `BUILD_TESTING=OFF` com zero testes e argumentos reservados rejeitados em exit 1. Nenhum benchmark foi executado; os gates nativos e de pacote/E2E permanecem abertos, e o target SDK Qt enxuto requer `QT_HOST_PATH` para localizar as ferramentas host de tradução.

A seção 8 registra a política comum de bandeja e sua implementação Qt: menu compartilhando o transporte, intenções separadas de fechar/ocultar/sair, confirmação inicial por execução e fallback explicado. O smoke do executável fecha a janela QML e encerra normalmente, sem kill como evidência de sucesso. Os checks automatizados passaram após regressões de animação/foco e perda de disponibilidade durante a ocultação. Isso não conclui FR-030/FR-031/FR-032, o pacote ou os gates nativos de M0.2.

A seção 9 registra a receita e os checks do pacote local, sem alterar o preflight histórico. Inventário SHA-256, imports PE e relocação com Unicode passaram; o stage tem 1.378 arquivos/82.388.347 bytes e usa o CRT externo do host. O bloqueio inicial por `RTSSHooks64.dll` foi superado após uma leitura encontrar zero processos RTSS. As duas jornadas offscreen passaram no stage de receita commitada e na cópia relocada, com saída normal, módulos amostrados somente do pacote/System32 e manifesto íntegro. Nenhuma aplicação externa, whitelist ou configuração do sistema foi alterada. Isso não equivale a validar desktop nativo, máquina limpa, licenças de redistribuição ou instalação.

A [triagem de segurança](m0/evidence/m0-2-security-triage.md) acrescenta achados nas fontes FreeType/PCRE2, sem exploração do shell demonstrada, e confirma MSVC 14.43 fora de suporte no pacote histórico. O vínculo PE/SPDX foi conferido, mas advisories, servicing e licenças não receberam aprovação global. A triagem preservou dependências, toolchain e contrato histórico; a instalação posterior autorizada é registrada abaixo. `CHK-SEC` permanece em aberto.

A fila tem validação funcional: menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação. Isso não conta o pool completo nem as instanciações ao longo da execução e não fecha NFR-PERF-006. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida. A [tentativa posterior autorizada](m0/evidence/m0-2-native-attempt-20260827.md) permaneceu visualmente inconclusiva e não aprovou gates nativos/NVDA. Após a preparação `b59a247` e o primeiro cancelamento, a [nova instalação autorizada](m0/evidence/m0-2-msvc-installation.md) terminou com sucesso, sem reboot, e MSVC 19.44.35228 passou no novo build/CTest 65/65 e receipt real. A integração histórica ao pacote passou nos smokes offscreen; depois, o [Qtbase corrigido](m0/evidence/m0-2-qtbase-refresh.md) foi reconstruído e o [SDK composto autenticado](m0/evidence/m0-2-qt-composite-sdk.md) publicou 9.880 folhas/2.400.301.667 bytes no run `f75eb9bea8280844c6d1a2b3f4`, aprovado pelo verificador independente. A receita `package-v2` está implementada e congelada, sem alegação de execução real. Ainda faltam build/runtime/ABI contra essa identidade, pacote v2 e seus smokes; CRT, PCRE2, licenças/notices/SBOM, redistribuição, acessibilidade nativa/NVDA, `CHK-SEC`, desempenho, M0.2 e decisão de stack permanecem pendentes.

A implementação do produto Windows está autorizada conforme [agents.md](../agents.md) e ADR-0005. Isso não declara M1 concluída nem transforma qualquer ensaio histórico não executado em aprovado.

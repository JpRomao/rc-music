# Jornadas de processo — composições exclusivas de testes

**Estado:** promoção validada nos gates locais de build, testes, scripts e documentação; M1 e os gates nativos/release permanecem abertos.
**Data:** 2026-09-06.
**Revisão dos opcionais:** 2026-10-02.

Os cinco drivers históricos e seu helper de lifecycle vivem em [tests/process](../tests/process), sem alterar suas assertions. [tests/process/CMakeLists.txt](../tests/process/CMakeLists.txt) preserva sete jornadas de baseline somente com Windows, desktop e `BUILD_TESTING=ON`; os opcionais de downloads abaixo acrescentam jornadas próprias. O consumidor histórico aponta para as mesmas fontes; configurar o experimento não é pré-requisito da suite raiz.

## Baseline: sete registros, dois executáveis instrumentados

Os dois executáveis exclusivos de teste compilam a mesma [main.cpp](../src/app/main.cpp) do produto. A composição comum fica em [RcMusicShell.cmake](../cmake/RcMusicShell.cmake); não há cópia da implementação do bootstrap. Ambos recebem `M0_QT_SHELL_TEST_HOOKS`, `RC_MUSIC_TEST_UI_VARIANT` e a ABI da [UI de testes](ui-tests.md), nunca a UI/plugin de produção.

| Executável | Composição |
|---|---|
| `rc_music_test_shell` | Áudio Qt Multimedia/FFmpeg, metadata TagLib e fluxos reais de persistência/IPC, com hooks para dirigir e observar as jornadas. Não incorpora JSONs de cenário. |
| `rc_music_fixture_test_shell` | Sem áudio combinado; usa backend determinístico e os dois JSONs privados autenticados para os cenários históricos de erro/bandeja. |

| Registro CTest | Driver | Executável e jornada |
|---|---|---|
| `rc_music.process.preferences-restart` | `PreferencesRestartTests` | Combinado; gravação/leitura entre processos, placement, início minimizado e fallback sem bandeja. |
| `rc_music.process.session-restart` | `SessionRestartTests` | Combinado; restauração Unicode pausada, término forçado antes do commit, recuperação e retry após falha real de replace. |
| `rc_music.process.single-instance` | `SingleInstanceExecutableTests` | Combinado; primário/secundários, IPC, limites, drenagem e feedback seguro de entrada. |
| `rc_music.process.diagnostics` | `DiagnosticExecutableTests` | Combinado; diagnóstico opt-in/default, decoder e preservação de colisão com arquivo não pertencente ao logger. |
| `rc_music.process.combined-startup` | `ShellExecutableSmokeTests` | Combinado; bootstrap vazio e lifecycle. |
| `rc_music.process.error-recovery` | `ShellExecutableSmokeTests` | Fixture; recuperação do cenário `unavailable`. |
| `rc_music.process.tray-lifecycle` | `ShellExecutableSmokeTests` | Fixture; menu, ocultação/restauração e persistência com disponibilidade de bandeja injetada. |

Os sete recebem os labels `windows;desktop;qt;process`. A guarda `rc_music.process.build-boundary`, com labels `process;boundary`, foi o oitavo registro da seleção `process`, não uma oitava jornada. Com as [12 integrações anteriores](windows-integration-tests.md), o label `windows` selecionava 19 registros naquele baseline; esse total histórico não inclui os opcionais posteriores. Os quatro smokes com QPA Windows continuam separados.

## Isolamento, ABI e recursos

A aplicação real `m0_qt_shell` conserva a UI de produção, sem hooks ou macro de cenário, tanto em ON quanto em OFF. Ligar testes não a torna instrumentada. Os dois shells de teste ligam exclusivamente `rc_music_test_ui`/`rc_music_test_uiplugin` e `rc_music_ui_scenario_support`; cada um compila a mesma fonte `ShellDiagnosticBinding.cpp` com a ABI de teste, sem trazer a biblioteca de diagnóstico ligada à UI de produção. A única seleção nova no bootstrap escolhe o símbolo de tradução correspondente à variante.

A [guarda de processo](../tests/cmake/ProcessBuildBoundaryTests.cmake) examina os comandos alcançáveis dos três executáveis: exige uma compilação coerente de main, presenter, moc, registro QML e binding de diagnóstico, recusando mistura de variantes. Também lê os QRCs efetivamente compilados: produto e combinado de teste não podem conter JSON; o shell de fixture deve incorporar exatamente `fixture.v1.json` e `queue.v1.json` de `tests/ui/fixtures`. A autenticação desses inputs permanece no [contrato da UI](ui-tests.md#fixtures-privadas-preservadas). OFF não inclui `tests/process`, drivers ou shells de teste.

Todos os filhos recebem raiz de configuração própria em `QTemporaryDir` por `--m0-test-config-root`, junto dos argumentos reservados de smoke/cenário. O isolamento decorre desse contrato, não de redefinir `APPDATA` ou `USERPROFILE`. Os drivers não consultam a configuração pessoal `MusicPlayer`, não escrevem no Registry nem iniciam o executável de produção. Os programas são recebidos por argumento, sem depender de um basename fixo. O registro CTest preserva a interface exata de argumentos dos drivers, sem acrescentar opções QtTest que seus `main` não aceitam.

As jornadas usam `offscreen`, renderização software e estilo Basic. CTest fornece fontes Windows, PATH de Qt Core e, no grupo combinado, runtime/plugins Multimedia e backend FFmpeg; as DLLs TagLib são copiadas para junto do shell combinado. O driver de diagnóstico ajusta o logging somente no filho para observar a proteção dos canais. Esses ambientes não equivalem a um runtime empacotado independente do SDK.

Os timeouts CTest são 40 s para preferências, 120 s para sessão, 45 s para IPC/diagnóstico e 25 s para cada smoke; a guarda tem 15 s. Os drivers mantêm seus watchdogs e ownership de `QProcess`; a terminação deliberada da jornada de sessão atinge somente o filho cujo PID possui o lock temporário, depois da barreira de commit. Locks, corrupção, retries e logs exercitados ficam nos diretórios próprios dos testes.

## Inputs e equivalência preservados

Sessão e IPC recebem a WAV sintética de [tests/audio/corpus](audio-tests.md): `fixtures/valid/wav-pcm-mono.wav`, **96.044 bytes**, SHA-256 `03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f`. O configure autentica o corpus antes de registrar os processos; os drivers copiam a WAV para paths temporários Unicode. Diagnóstico gera sua própria WAV silenciosa; preferências apenas serializa um path de áudio que não precisa existir. Não há leitura de áudio pessoal.

As seis fontes movidas preservam os bytes das originais históricas:

| Fonte em `tests/process` | Bytes | SHA-256 |
|---|---:|---|
| [qtest/PreferencesRestartTests.cpp](../tests/process/qtest/PreferencesRestartTests.cpp) | 5.158 | `575c27c9ed58e13ba0d3f4f5aa6dede23222acfd0e6d64a40235cd85b15ec2c5` |
| [qtest/SessionRestartTests.cpp](../tests/process/qtest/SessionRestartTests.cpp) | 16.447 | `1347ca9b886d6b4701c90ac3b66f1b580872ad9e50d6e2bda5def463ac5d4366` |
| [qtest/SingleInstanceExecutableTests.cpp](../tests/process/qtest/SingleInstanceExecutableTests.cpp) | 28.734 | `4e8239377cfb974558f7f0f080aebddf47c8fb80747a99293aad0fc8d75f59b8` |
| [qtest/DiagnosticExecutableTests.cpp](../tests/process/qtest/DiagnosticExecutableTests.cpp) | 6.605 | `1060cba1442bb206400ecdcea975176f9428876a1bab59c330b903efa13a9482` |
| [qtest/ShellExecutableSmokeTests.cpp](../tests/process/qtest/ShellExecutableSmokeTests.cpp) | 2.401 | `97df355c080420a6772cf369d146704db93b20b79ea8169acb2cafd4a5024c49` |
| [support/ShellLifecycleSmoke.hpp](../tests/process/support/ShellLifecycleSmoke.hpp) | 6.213 | `0231d071ffd6665dadaa00258cf14a61a027831d8d90bfdbb8a938062a3f808f` |

Os nomes de cenário e markers fechados continuam internos aos testes. A licença privada das fixtures não autoriza redistribuição pública; nenhum recurso sintético entra no produto para viabilizar esses checks.

## Composição instrumentada dos downloads

Com `RC_MUSIC_BUILD_BROWSER_INTEGRATION` e persistência de downloads ligados, `rc_music.process.downloads-composition` acrescenta dois boots do `rc_music_test_shell` com `--m0-smoke=downloads`. Só esse executável recebe `DownloadsShellSmoke` e a macro privada; produção e outras composições recusam o hook antes de criar configuração. O parser exige raiz absoluta, cenário vazio e nenhum path de abertura. Não há executor falso, arquivo de teste ou hook ativado no produto.

O driver copia o MP3 CBR autenticado do corpus para TEMP Unicode próprio e cria um novo filho de checkpoint com DACL protegida usuário atual+SYSTEM, sem reparar ACL existente. A main preserva o `SessionLoadResult` antes de mover o snapshot para o presenter. Serviço, controller, binding/model, importador durável, worker/store de checkpoint e worker/store de sessão são reais; somente `Published` vem do executor sintético, sem transporte/conversão/ownership de mídia real. QML recebe o binding opcional verdadeiro. O primeiro boot comprova Saved de admissão antes da execução, fila durável sem load/autoplay e reprodução após seleção explícita; o segundo, com restauração desligada, reconhece Imported sem executar, hidratar ou duplicar.

Saída normal exige `exitCommitted`, parada física do controller/IO/executor e `Saved` final antes de destruir os owners. Dois leitores novos após cada `NormalExit` verificam os documentos, IDs e fila/recibo; isso não é readback promovido a ACK nem transação global dos documentos. O driver tem watchdogs e guarda somente seus filhos, timeout CTest de 60 s e labels próprios. Não consulta sites/perfis/áudios pessoais, instala extensão ou altera Registry. Este ensaio não aprova release, sandbox, perda de energia, som físico, engine da Completa ou integração instalada.

## Processo do host Native Messaging

Com `RC_MUSIC_BUILD_NATIVE_MESSAGING=ON`, Windows e `BUILD_TESTING=ON`, [NativeHostProcessTests](../tests/qt/NativeHostProcessTests.cpp) registra `rc_music.native.host-process`, separado das jornadas de shell. O alvo de produção `rc_music_native_host` e os dois alvos de teste compilam a mesma [main real](../src/app/native_host_main.cpp). Produção recebe configuração confiável de build, vazia por padrão e sem overrides de argv/ambiente/JSON; `rc_music_native_host_test` recebe identidade sintética própria do cache e `rc_music_native_host_disabled_test` recebe configuração sempre vazia, inclusive quando o build de produção estiver explicitamente configurado. Sintético não vira fallback no produto.

O driver atua como browser peer por stdin/stdout binários e como dono falso por `QLocalServer` manual, independente do servidor de produção. Exige um único preâmbulo/frame exato, resposta estrita correlacionada, stdout sem bytes extras e stderr vazio. Mantém stdin aberto durante Pending e cobre prefixo/corpo fragmentados, origem/flags/spoof sem IPC, frames excessivos/zero/incompletos, `NotAvailable` por dono ausente e resposta hostil/mismatch/double/oversized sem ACK ou reconnect. EOF Pending fecha somente o observador e não reenvia o pedido. A composição seguinte usa dono real, mas não injeta EOF durante admissão durável pendente; a continuidade nesse instante exige ensaio próprio, não inferência do peer falso ou do caminho normal.

Ausência de entrada expira pelo prazo total de montagem de 5 s. Helpers próprios do watchdog verificam finish→0, stall→2 e stall durante teardown→2; o watchdog do host real tem limite independente de 15 s e pode terminar somente o próprio host. `ChildGuard` encerra e aguarda exclusivamente filhos do driver. O registro tem timeout CTest de 90 s e compartilha o resource lock do endpoint sintético com a composição seguinte. Deadlines/watchdogs verificam protocolo e cleanup, não orçamento de desempenho. Pipes, diretórios e configuração ficam próprios do teste; não há instalação, Registry, browser ou rede.

## Composição instrumentada do ingresso Native Messaging

Com os opcionais de integração/persistência e Native Messaging ligados, `rc_music.process.native-downloads-composition` acrescenta [NativeDownloadsCompositionTests](../tests/process/qtest/NativeDownloadsCompositionTests.cpp). Usa `rc_music_test_shell --m0-smoke=native-downloads`, a main compartilhada e `DownloadsShellSmoke` com capacidade nativa exclusiva de teste. Parser/guards recusam o hook em builds sem a capacidade, antes de configuração, com raiz absoluta, cenário vazio e nenhum path de abertura. O player real ON/OFF não alcança macros, identidade sintética, executor de fixture ou hooks de teste.

O TEMP Unicode próprio contém uma cópia do MP3 CBR autenticado e um diretório de checkpoint novo com DACL protegida usuário atual+SYSTEM. Servidor/client/host Native Messaging, controller/serviço, binding/model/QML, importador durável e ambos stores/workers são reais. **Somente publicação `Published` é sintética**: não há transferência, rede, conversão ou ownership físico da mídia neste fluxo. O modo nativo não chama `controller.begin` diretamente; cada admissão/replay chega por um host real ao endpoint separado de `OpenSources`.

São quatro processos de host, dois por boot em dois boots, sempre com o mesmo UUID. Cada resposta exige NormalExit/exit 0, frame único e ACK estrito com os mesmos IDs de job. Um leitor independente do checkpoint deve carregar exatamente um job e um recibo após cada resposta; não se mede o instante físico do ACK. Depois de ambos ACKs e do marker esperado, o driver cria exclusivamente o sentinel fechado `m0-native-downloads-exit` no TEMP. Ele dirige somente lifecycle instrumentado, nunca acrescenta um comando ao protocolo de produção.

No primeiro boot, a execução só começa após Saved de admissão; importação conserva fila+recibo sem load/autoplay e a reprodução exige seleção explícita. No segundo, `restore_last_session=false` preserva o ledger e reconhece o único job como Imported, sem executar, hidratar ou duplicar. Após cada NormalExit, leitores novos de sessão/checkpoint conferem fila/recibo/estado: Completed com uma ocorrência no primeiro boot, Imported e fila vazia no segundo. A saída exige `exitCommitted`, Saved final e parada reconhecida do servidor/controller/workers/executor antes de destruir owners; parada física não é promovida a gravação bem-sucedida.

O driver limita a captura de saída do app, mantém watchdogs e guards apenas de seus filhos, e usa timeout CTest de 150 s e resource lock do endpoint sintético. Os labels acrescentam `native;downloads;session;FR-051;FR-053`. `RC_MUSIC_BUILD_NATIVE_MESSAGING` permanece OFF por padrão, Qt Network é um componente opcional separado, e produção continua sem ingresso/serviço remoto ativo. Origem em argv/pipe do mesmo usuário não autentica navegador; primeira instância/colisão do pipe Windows, identidade instalada, representação signed HWND/Brave, registro/upgrade e instalação continuam pendentes. Sem ferramenta/sandbox, mídia da internet, perda de energia, transação global entre documentos, release ou engine da Completa aprovados por este ensaio.

### Evidência focada do ingresso de 2026-10-02

- Processo real/peer independente/watchdog: Red **19 passagens/13 falhas**, Green **32/32 QtTest**, CTest focado **1/1 em 9,01 s**.
- Composição anterior de downloads e composição com ingresso nativo: **3/3 QtTest cada**, com publicação sintética nos dois ensaios.
- Entrada da main: regressão causal Red **3 passagens/1 falha**, Green **4/4**, relendo o clock após o read potencialmente bloqueante.
- Codec **188/188**, launch **54/54**, I/O Win32 **13/13** e transporte **27/27** têm gates próprios; os três achados de reentrada/drain do transporte tiveram Red **24 passagens/3 falhas** antes do Green.
- Consolidação Release ON/build/`all_qmllint`: **470/470 CTest em 138,98 s**. Esse tempo de execução, inclusive com builds concorrentes, não é benchmark. O fechamento dos probes OFF de produção é separado e não é inferido dessa suíte: desktop OFF/lint/zero testes/grafo de 253 comandos e host standalone OFF/zero testes passaram; três probes reais recusaram hook/spoof sem saída, IPC ou configuração. Perfil com identidade de produção explicitamente configurada passou **2/2 CTest**, incluindo **32/32 QtTest** contra o fixture sempre vazio separado. Não instala ou ativa essa identidade no app.

As contagens incluem setup/cleanup QtTest e não se somam aos registros CTest. Não houve runtime/setup novo, instalação/registro, Brave/Chromium real, download remoto, ferramenta nova, Computer Use ou benchmark. Os estados dos requisitos FR-051–053 permanecem em andamento.

## Exclusividade cooperativa do dono Native Messaging

`rc_music.native.owner-process` executa [NativeDownloadOwnerProcessTests](../tests/qt/NativeDownloadOwnerProcessTests.cpp) com o helper exclusivo de testes [NativeDownloadOwnerHelper](../tests/qt/NativeDownloadOwnerHelper.cpp). O helper instancia o servidor real com porta falsa de admissão, sem Main/UI, stores, mídia, rede ou instalação. Endpoints UUID próprios e comandos stdin fechados permitem observar READY/REFUSED/STOPPED/RELEASED; entrada acumulada até 256 bytes e captura de 1.024 bytes por canal conservam limites. Watchdog independente termina somente o próprio helper; cleanup/kill nunca usa PID recebido nem processos externos.

O segundo processo é recusado para o mesmo endpoint e para alias de caixa com origem diferente. Ele permanece vivo com seu servidor recusado: a prova de liberação não depende de terminar o contender. Um pedido real pelo pipe recebe um único recibo do primeiro dono, sem admissão no segundo. STOPPED fecha ingresso, mas o servidor vivo conserva a reserva; RELEASED destrói esse objeto e permite sucessor enquanto o processo antigo continua vivo. Outro caso mata somente o helper dono, aguarda `NotRunning`/CrashExit e aceita sucessor com dois contenders recusados ainda vivos. Configuração inválida não ocupa o endpoint e endpoints distintos coexistem.

Red real: **3 passagens/4 falhas**, por contendores indevidamente READY, sem falha de compilação/harness. Green focado: **7/7 QtTest**, juntamente com transporte **31/31** e lease/DACL **23/23**, CTest **3/3 em 4,82 s**. A reserva usa criação atômica de marcador privado por SID/endpoint, não mutex adquirido por thread, portanto não permite recursão na mesma thread. Retenção até o último HANDLE é conservadora; observadores externos podem prolongar a indisponibilidade. O teste low-level verifica DACL e colisões preexistentes, sem API extra de inspeção no produto.

Exclusividade é cooperativa, não autenticação do browser, `FILE_FLAG_FIRST_PIPE_INSTANCE`, resistência a pipe/processo hostil do mesmo usuário, prova de sessões Windows diferentes ou disponibilidade simultânea entre usuários. O marcador `Global\\` tem SID, mas o pipe configurado atual continua global e sem derivação per-SID. A eleição/ACK de `OpenSources` não muda. Produção continua desativada; identidade/namespace instalados, runtime/ownership, ferramentas/sandbox, Completa e opcionais do instalador permanecem gates separados.

Fechamento do código final: build Release ON, `all_qmllint` e CTest **472/472**, com 472 entradas/472 Test Passed no log completo atual e os oito skips internos históricos declarados no plano. A correção de revisão separa falha real de listen de fechamento reentrante após listen que já teve êxito, retendo a reserva no segundo ramo; não se alega injeção determinística dessa corrida de startup. Native Release BUILD_TESTING OFF foi recompilado após a correção: zero testes, guarda de grafo verde, dois probes do host desativado exit 2/silenciosos/sem timeout kills. Grafo desktop default OFF foi inspecionado, sem novo build alegado. Nenhum helper/driver de owner chega ao grafo de produção e nenhum desses processos permaneceu ativo.

## Comandos

Os [comandos existentes](development-commands.md) incluem os novos alvos no build ON, sem downloads ou novo wrapper:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 test -ToolchainFile tools/windows.example.psd1
pwsh -NoProfile -File scripts/develop.ps1 validate -ToolchainFile tools/windows.example.psd1
```

Depois do configure/build, a seleção focada, incluindo a guarda, é:

```powershell
ctest --test-dir out/product/desktop-tests --output-on-failure --no-tests=error -L process
```

Use o CTest do arquivo de ferramentas se não estiver no PATH e o diretório efetivamente configurado; `.tools/windows.psd1` continua sendo o padrão do runner. Não execute configure/builds concorrentes no mesmo diretório. As contagens de registros CTest não são somadas às funções/linhas internas de QtTest.

## Evidência e limites

- Baseline histórico confirmado antes da promoção: **5/5** registros do combinado em **9,50 s** e **2/2** dos cenários históricos em **1,29 s**. Esses resultados não são Green da nova composição.
- Os hashes acima registram a equivalência das seis fontes; a movimentação mecânica preserva o comportamento e não exige Red artificial das jornadas.
- A guarda arquitetural teve Red inicial pelo target `rc_music_test_shell` ainda ausente e Reds posteriores por falsos positivos nos diretórios de build `desktop-tests` e `tests/desktop`; a validação de comandos e recursos normaliza somente o prefixo absoluto do build antes de procurar inputs de teste, preserva referências reais e mantém ambas as regressões, com Green final **1/1** em **0,80 s** e sem relaxar a exclusão de macros ou a ABI.
- `scripts/develop.ps1 test` passou **296/296 registros CTest** em **104,34 s**, com exit 0 e resultado observado no console. O total anterior de **288/288** permanece como evidência da promoção das 12 integrações Windows.
- Os gates separados de formato **172 C++/52 QML**, build de produção OFF e `lint` com reconfigure/build e os dois alvos QML passaram. Não houve execução de `scripts/develop.ps1 validate` neste incremento; não apresentar esses comandos como uma execução desse wrapper.
- Depois do ajuste de escopo do import QuickLayouts, a seleção dos oito registros `process` mais o store de diagnóstico passou **9/9** em **12,21 s**. O log focado confirmou os totais QtTest das sete jornadas, na ordem da tabela: **3, 6, 9, 8, 3, 3 e 3**, sem falhas ou skips. O store passou 29 casos e registrou quatro skips de symlink: `config-root`, `ancestor`, `diagnostics-child` e `reserved-leaf`, pela política do host.
- Produção OFF: **246 comandos**, **zero testes**, sem Qt Test/QuickTest/Catch ou suporte de teste; os quatro QRCs alcançáveis não contêm inputs de teste/JSON. A leitura PE passou **5/5** sem iniciar o aplicativo.
- Compatibilidade histórica: combinado **5/5** em **10,48 s** e cenários **2/2** em **1,14 s**.
- Check final da raiz: formato **172 C++/52 QML**, documentos **55 Markdown/83 requisitos/17 checks/76 etapas**, runner **33/33**, formatter **18/18** e `git diff --check` aprovados.

Limite do log global nesse checkpoint: o probe CTest aninhado de `AudioRegistrationTests.cmake`, preexistente a esta promoção, truncou `LastTest.log` durante a execução completa. O resultado 296/296 vem do exit/console; os totais internos e skips acima foram conferidos no log da seleção focada posterior, não extrapolados do log global incompleto. A [correção posterior da consulta](audio-tests.md#preservacao-do-log-ctest) preserva os próximos logs, sem reconstruir esta evidência histórica.

São processos reais com instrumentação de teste e UI offscreen, não o executável distribuído nem interação nativa. Crash no checkpoint controlado não representa perda física de energia; tray injetada não comprova Explorer/SMTC reais; decoder e mídia sintética não comprovam áudio audível ou dispositivos. Não há Computer Use, instalação/associação no host, atualização de runtime/instalador, benchmark, Linux ou CI remoto neste recorte. Os quatro smokes nativos e os gates de M1/M4, acessibilidade, segurança, licenças e release permanecem separados no [plano](development-plan.md).

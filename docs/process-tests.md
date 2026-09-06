# Jornadas de processo — composições exclusivas de testes

**Estado:** promoção validada nos gates locais de build, testes, scripts e documentação; M1 e os gates nativos/release permanecem abertos.
**Data:** 2026-09-06.

Os cinco drivers históricos e seu helper de lifecycle passam a viver em [tests/process](../tests/process), sem alterar suas assertions. [tests/process/CMakeLists.txt](../tests/process/CMakeLists.txt) registra sete jornadas somente com Windows, desktop e `BUILD_TESTING=ON`. O consumidor histórico aponta para as mesmas fontes; configurar o experimento não é pré-requisito da suite raiz.

## Sete registros, dois executáveis instrumentados

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

Os sete recebem os labels `windows;desktop;qt;process`. A guarda `rc_music.process.build-boundary`, com labels `process;boundary`, é o oitavo registro da seleção `process`, não uma oitava jornada. Com as [12 integrações anteriores](windows-integration-tests.md), o label `windows` passa a selecionar 19 registros; os quatro smokes com QPA Windows continuam separados.

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

Limite do log global: o probe CTest aninhado de `AudioRegistrationTests.cmake`, preexistente a esta promoção, truncou `LastTest.log` durante a execução completa. O resultado 296/296 vem do exit/console; os totais internos e skips acima foram conferidos no log da seleção focada posterior, não extrapolados do log global incompleto.

São processos reais com instrumentação de teste e UI offscreen, não o executável distribuído nem interação nativa. Crash no checkpoint controlado não representa perda física de energia; tray injetada não comprova Explorer/SMTC reais; decoder e mídia sintética não comprovam áudio audível ou dispositivos. Não há Computer Use, instalação/associação no host, atualização de runtime/instalador, benchmark, Linux ou CI remoto neste recorte. Os quatro smokes nativos e os gates de M1/M4, acessibilidade, segurança, licenças e release permanecem separados no [plano](development-plan.md).

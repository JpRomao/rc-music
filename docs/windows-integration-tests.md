# Integrações Windows — testes automáticos da raiz

**Escopo:** promoção de 12 suites de integração; não encerra M1 nem os gates nativos ou de release.
**Data:** 2026-09-06.

A raiz passa a executar a cobertura de bindings, plataforma, persistência, privacidade e identidade do executável que não exige interação com o desktop. As 12 fontes foram movidas sem alteração de bytes; o consumidor histórico usa os mesmos arquivos. Os quatro smokes nativos permanecem separados do runner diário. Os cinco drivers instrumentados, ainda separados naquele incremento, foram incluídos pela [promoção posterior de processos](process-tests.md), com composição exclusiva de testes e gates de build/testes validados.

## Os 12 alvos promovidos

[RcMusicWindowsTests.cmake](../cmake/RcMusicWindowsTests.cmake) registra oito suites de adapters/identidade somente com Windows, desktop e `BUILD_TESTING=ON`. Os quatro bindings são registrados em [tests/ui/CMakeLists.txt](../tests/ui/CMakeLists.txt), usando a [variante exclusiva de testes da UI](ui-tests.md). Todos recebem o label CTest `windows`.

| Registro CTest | Fonte | Composição e efeito exercitado |
|---|---|---|
| `rc_music.ui.shell-platform-binding` | [ShellPlatformBindingTests.cpp](../tests/ui/qtest/ShellPlatformBindingTests.cpp) | Presenter de cenário, adapter de plataforma, janelas/menus próprios em `offscreen` e disponibilidade/saída injetadas. |
| `rc_music.ui.system-media-controls-binding` | [SystemMediaControlsBindingTests.cpp](../tests/ui/qtest/SystemMediaControlsBindingTests.cpp) | Presenter de cenário e portas falsas de plataforma/mídia; não publica SMTC real. |
| `rc_music.ui.system-power-events-binding` | [SystemPowerEventsBindingTests.cpp](../tests/ui/qtest/SystemPowerEventsBindingTests.cpp) | Presenter de cenário e eventos falsos de energia; não suspende o sistema. |
| `rc_music.ui.diagnostic-binding` | [ShellDiagnosticBindingTests.cpp](../tests/ui/qtest/ShellDiagnosticBindingTests.cpp) | Presenter e sink falso; verifica eventos fechados e deduplicação, sem gravar logs reais. |
| `rc_music.windows.shell-platform` | [QtShellPlatformTests.cpp](../tests/qt/QtShellPlatformTests.cpp) | Adapter real com `offscreen` obrigatório; ação de aplicativos padrão usa abridor de URI injetado, sem abrir Configurações. |
| `rc_music.windows.system-appearance` | [QtSystemAppearanceTests.cpp](../tests/qt/QtSystemAppearanceTests.cpp) | Adapter real com snapshots e mensagens sintéticos; não altera tema ou alto contraste do Windows. |
| `rc_music.windows.smtc-adapter` | [WindowsSmtcAdapterTests.cpp](../tests/qt/WindowsSmtcAdapterTests.cpp) | Adapter real com fronteira nativa falsa; não publica controles de mídia no host. |
| `rc_music.windows.preferences-store` | [QtJsonWindowsPreferencesStoreTests.cpp](../tests/qt/QtJsonWindowsPreferencesStoreTests.cpp) | Store real, JSON sintético e falhas injetadas em `QTemporaryDir`. |
| `rc_music.windows.session-store` | [QtJsonWindowsSessionStoreTests.cpp](../tests/qt/QtJsonWindowsSessionStoreTests.cpp) | Store real e locks em diretório temporário; um filho do próprio teste mantém o lock. |
| `rc_music.windows.diagnostic-privacy` | [QtDiagnosticPrivacyTests.cpp](../tests/qt/QtDiagnosticPrivacyTests.cpp) | Mensagens sintéticas e captura de saída de filhos do próprio teste, incluindo término fatal deliberado de um filho. |
| `rc_music.windows.diagnostic-store` | [QtWindowsDiagnosticLogStoreTests.cpp](../tests/qt/QtWindowsDiagnosticLogStoreTests.cpp) | Store real, relógio falso, retenção e fronteiras de arquivos/links somente em diretórios temporários. |
| `rc_music.windows.executable-identity` | [WindowsExecutableIdentityTests.cpp](../tests/qt/WindowsExecutableIdentityTests.cpp) | Lê ícones e VERSIONINFO do executável produzido, carregado como dados/recursos; não inicia a aplicação. |

Os stores não abrem mídia pessoal nem configurações existentes. Caminhos Unicode/UNC serializados nos testes de sessão são dados sintéticos, não fontes de áudio. O teste de identidade depende do build de `m0_qt_shell`, mas inspeciona o PE com `LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE`; os ícones temporários são liberados pelo próprio teste.

As suites que precisam de GUI usam `offscreen`; as demais usam Qt Core/Qt Test. O CTest preserva ambiente e timeout por alvo, incluindo renderização software/estilo Basic onde aplicável. Nenhum desses testes exige Computer Use, entrada nativa, reinício do Explorer, escrita no Registry ou instalação.

## Fronteira de UI e instrumentação

Os quatro bindings ligam `rc_music_test_ui` e `rc_music_ui_scenario_support`, com os mesmos JSONs privados autenticados de `tests/ui/fixtures`. `RC_MUSIC_ENABLE_FIXTURE_SCENARIOS` acompanha a variante e seus consumidores; não é habilitada na aplicação real, nem com testes ON.

O binding de diagnóstico merece uma distinção: o seu executável de teste compila diretamente a mesma fonte [ShellDiagnosticBinding.cpp](../src/presentation/ShellDiagnosticBinding.cpp), sob a configuração da UI de cenário. Ele não liga `m0_qt_diagnostic_binding`, pois essa biblioteca traz a UI de produção. Isso evita misturar dois layouts C++ de `PlayerPresenter` no mesmo processo, sem duplicar a implementação do binding.

A guarda [rc_music.ui.build-boundary](../tests/cmake/UiBuildBoundaryTests.cmake) agora examina os comandos alcançáveis da aplicação, do Quick e dos quatro bindings. Exige a variante correta em cada consumidor, incluindo presenter, moc/registro QML e a compilação única do binding de diagnóstico. Recusa a UI de produção nos testes de cenário e suporte/macros/recursos de cenário na aplicação. A guarda tem label `ui;boundary`: entra no `validate` completo, mas não na seleção pelo label `windows`. O grafo de comandos não substitui a revisão do conteúdo dos QRCs alcançáveis.

Os oito testes de adapters/identidade não precisam de `FixtureRepository`, cenário ou hooks de CLI do app. Filhos usados por sessão/privacidade são executáveis dos próprios testes, não uma aplicação de produto instrumentada. Os guards de timeout encerram somente esses filhos próprios.

## Smokes separados e promoção posterior de processos

### Quatro smokes com QPA Windows

As fontes abaixo permanecem em [tests/qtest do consumidor histórico](../experiments/m0/candidates/qt/shell/tests/qtest). Elas não exigem Computer Use ou instalação, mas exercitam fronteiras reais do host e não foram promovidas ao `validate` diário neste recorte.

| Suite | Efeito nativo e limite |
|---|---|
| `WindowsSmtcNativeSmokeTests` | Cria janela própria oculta e publica/limpa metadados e capacidades sintéticos no SMTC real; pode aparecer temporariamente na superfície de mídia do sistema. Não testa teclas físicas. |
| `WindowsPowerEventsTests` | Registra a fronteira real de energia em receptor próprio e envia mensagens somente à sua janela; não suspende ou hiberna o host. |
| `WindowsTaskbarEventsTests` | Usa receptor próprio de `TaskbarCreated` e consulta presença da shell; não envia broadcast nem reinicia o Explorer. |
| `WindowsTrayRecoveryTests` | Remove e recria somente seu próprio ícone de bandeja e envia `TaskbarCreated` somente ao receptor correspondente. Depende da bandeja disponível e de identificação privada documentada do Qt 6.11.2 exclusivamente no teste; não altera ícones alheios. |

Os quatro conservam sua configuração histórica serial e QPA `windows`. A separação não transforma esses testes em ensaios físicos nem declara seus gates concluídos por equivalência com `offscreen`.

### Cinco drivers promovidos em composição própria

O [guia de processos](process-tests.md) concentra o mapa das sete jornadas de preferências, sessão, IPC, diagnóstico e lifecycle, registradas e validadas na raiz com dois executáveis instrumentados exclusivos de testes. Ambos usam a mesma main e ABI da UI de teste, sem habilitar hooks na aplicação real ON/OFF. Fontes/assertions e consumo histórico foram preservados. Essa promoção não inclui os quatro smokes QPA Windows acima.

## Comandos existentes

Os [comandos de desenvolvimento da raiz](development-commands.md) já incluem a nova cobertura, sem wrapper adicional:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 test
pwsh -NoProfile -File scripts/develop.ps1 validate
```

Por padrão, usam `.tools/windows.psd1` e `out/product/desktop-tests`. Para selecionar ferramentas locais explicitamente, acrescente `-ToolchainFile tools/windows.example.psd1`; `-BuildDirectory` seleciona outro build ON da raiz. Depois do configure/build, a seleção focada é:

```powershell
ctest --test-dir out/product/desktop-tests --output-on-failure --no-tests=error -L windows
```

Use o `CTest` do arquivo de ferramentas se ele não estiver no `PATH`, e ajuste o diretório ao build efetivamente configurado. Com a [promoção de processos](process-tests.md), essa seleção passa de 12 para 19 registros CTest: as 12 suites deste guia mais sete jornadas instrumentadas. A seleção `-L process` contém essas sete jornadas e sua guarda de fronteira, totalizando oito. Não são contagens de funções/linhas internas de QtTest. Não execute simultaneamente configure/builds no mesmo diretório.

## Evidência do incremento

O registro abaixo preserva a promoção original das 12 suites, anterior à inclusão das jornadas de processo. Seus totais não são a evidência da composição posterior.

- Baseline histórico antes dos movimentos: **8/8** de plataforma, bindings e identidade em **3,34 s**; **4/4** de stores/privacidade em **1,78 s**.
- As **12 fontes movidas têm SHA-256 byte-idêntico** às originais. A promoção mecânica preserva as assertions e não exige Red artificial de comportamento.
- A extensão da guarda arquitetural teve Red antes da composição porque o novo target ainda não existia; passou com os consumidores e variantes corretamente ligados.
- `validate` da raiz passou **288/288 registros CTest** em **93,81 s**, incluindo build, os dois alvos `qmllint`, formato **166 C++/52 QML**, runner **33/33** e formatter **18/18**.
- Depois dessa execução, somente o label de requisito Linux `FR-032` foi retirado de dois testes Windows, sem alteração de comportamento. Reconfigure/lint e a seleção dos 12 registros mais a guarda de fronteira passaram **13/13** em **7,33 s**. O consumidor histórico foi recompilado e sua seleção correspondente passou **12/12** em **4,91 s**.
- Produção OFF passou configure/build e a guarda de **246 comandos**: **zero testes**, sem Qt Test/QuickTest ou inputs do novo módulo de testes Windows. A promoção não habilita cenário nem instrumentação no executável real.
- O inventário JSON do CTest confirmou exatamente os 12 nomes esperados no label `windows`, todos com `offscreen`, timeout e PATH Qt, sem labels nativos ou Linux. A leitura PE também passou **5/5** contra o executável OFF, sem executá-lo.
- Revisão independente sem achados P0–P2; documentos **54 Markdown/83 requisitos/17 checks/76 etapas** e `git diff --check` aprovados. Nenhuma fonte C++/QML de produção foi alterada neste incremento.

Contagens internas confirmadas no `LastTest.log` focado de `out/product/m1-desktop-release-on-20260906`:

| Suite | Passagens | Falhas | Skips |
|---|---:|---:|---:|
| Plataforma | 51 | 0 | 0 |
| Aparência | 33 | 0 | 0 |
| Adapter SMTC | 8 | 0 | 0 |
| Store de preferências | 15 | 0 | 0 |
| Store de sessão | 34 | 0 | 0 |
| Privacidade de diagnóstico | 8 | 0 | 0 |
| Store de diagnóstico | 29 | 0 | 4 |
| Identidade PE | 5 | 0 | 0 |
| Binding de plataforma | 16 | 0 | 0 |
| Binding de mídia | 5 | 0 | 0 |
| Binding de energia | 3 | 0 | 0 |
| Binding de diagnóstico | 20 | 0 | 0 |

Esses totais QtTest não são somados aos registros CTest. Os quatro skips correspondem somente aos casos de symlink `config-root`, `ancestor`, `diagnostics-child` e `reserved-leaf`, recusados pela política do host; não houve outra falha ou skip nessa seleção.

## Limites e riscos residuais

Há **quatro linhas de symlink** no teste de store de diagnóstico que podem usar `QSKIP` quando a política do host não permite criá-los; o harness não solicita elevação. A prova de hardlink também exige um filesystem compatível. Um CTest aprovado não significa automaticamente ausência desses skips.

O teste de privacidade provoca `qFatal` deliberadamente em um filho próprio, com a interface de erro Windows/CRT desabilitada nesse filho. Isso não representa controle sobre políticas externas de dump do host. Locks e arquivos temporários são reais, mas não constituem sandbox completa contra um ator concorrente do mesmo usuário.

Não houve nova interação nativa, teste físico de áudio/teclas/energia, benchmark, Linux, instalação, associação de arquivos ou atualização de runtime/instalador neste incremento. `offscreen` e fronteiras falsas não comprovam integração visual nativa, NVDA ou comportamento de dispositivos. A [promoção dos drivers instrumentados](process-tests.md) tem evidência e validação próprias; CI Windows, os quatro smokes e os demais gates de segurança/licenças/release continuam separados no [plano](development-plan.md).

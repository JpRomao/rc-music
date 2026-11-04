# Runtime Windows — receita de produto

**Estado:** runtimes privados Lite e Completa experimental montados e hashes conferidos em 2026-11-04, com setups gerados sem instalar. O smoke de startup/codecs abaixo pertence ao runtime anterior. Uso nativo, lifecycle e gates de distribuição continuam abertos.
**Data:** 2026-11-04.

A [entrega Lite de `4125f46`](local-delivery-4125f46-20261104.md) contém 554 arquivos / 96.347.255 bytes, sem opcionais remotos ou engine interna. O runtime permaneceu íntegro após a limpeza inventariada. A receita adicional da Completa experimental está descrita abaixo, com evidência de montagem própria exigida para cada artefato.

A [Completa experimental de `51cd067`](local-delivery-complete-51cd067-20261104.md) contém **557 arquivos / 96.573.561 bytes**, incluindo os três notices adicionais, com inventário e hashes conferidos. A engine não foi iniciada; Evergreen é pré-requisito externo e os serviços remotos continuam OFF.

O [inventário de componentes e rascunho de notices](delivery/runtime-3f22fda-notices.md) trata especificamente o runtime local `3f22fda`. Documentos/textos ficam separados do payload já gerado; identificações por origem e relações do fornecedor não substituem correspondência binária, SBOM completo, revisão de segurança ou autorização de distribuição.

## Implementação única

[`packaging/windows`](../packaging/windows/New-RcMusicLocalRuntime.ps1) contém a receita local, o planejador de cópia, o parser PE, `qt.conf`, o aviso de uso privado e os testes. O produto não importa módulos, protocolos ou fixtures de `experiments`. Os caminhos antigos de runtime são encaminhadores; o builder comparativo também reutiliza o parser PE puro.

O build é explícito na entrada de produto: somente Release, `BUILD_TESTING=OFF` e entrypoint reconhecido, raiz com desktop habilitado ou consumidor histórico com áudio/shell habilitados. O executável real continua `m0_qt_shell.exe`, renomeado para `RCMusic.exe` no payload; marca RC Music e dados/locks MusicPlayer não mudam.

O snapshot usa o entrypoint validado. Produto verifica `CMakeLists.txt`, `cmake`, `include`, `src`, `tests`, `tools`, `assets` e `packaging/windows`; histórico conserva também seus três inputs M0. Alterações staged, unstaged ou untracked nesses inputs impedem atribuir um runtime ao commit limpo. Não há autenticação de publicador ou proteção contra escritor hostil concorrente.

O manifesto mantém schema 1 e campos de compatibilidade, acrescentando `parser_sha256`, `recipe_path_base=repository`, `edition` e `browser_runtime_requirement`. Há cinco paths/hashes de receita na Lite e seis na Completa experimental, que inclui seu aviso próprio; são revalidados antes de publicar o manifesto. O instalador histórico inclui o hash do planejador promovido na identidade/recibo, além das oito chaves anteriores. O builder comparativo identifica seus sete inputs de receita relativamente à raiz. Nenhum manifesto, protocolo, corpus, matriz ou pacote histórico é reescrito ou aprovado retroativamente.

## Edições e montagem da Completa experimental

`-Edition Lite` é o padrão e continua recusando um cache com navegador ON. `-Edition CompleteExperimental` exige o entrypoint raiz, navegador ON, SDK local explícito e integração/downloads/Native Messaging OFF, conforme o [ADR-0012 Proposto](architecture/decisions/0012-experimental-edition-packaging.md). A receita sempre exige `BUILD_TESTING=OFF`; permitir ON explicitamente na API de inspeção não autoriza empacotar testes.

Com o build OFF da [navegação](embedded-browser.md#inputs-e-composição) preparado, fontes commitadas e destino inexistente:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicLocalRuntime.ps1 -Edition CompleteExperimental -BuildDirectory out/product/complete-experimental-release-20261104 -OutputDirectory out/product/rc-music-local/nova-completa-experimental
```

Além do mesmo núcleo Qt/Multimedia/TagLib, são copiados `WEBVIEW2-DEVELOPMENT.txt`, `notices/WebView2-SDK-LICENSE.txt` e `notices/WebView2-SDK-NOTICE.txt`. Bytes, origem e hashes entram no inventário. O loader WebView2 é estático; o runtime Evergreen do host não é copiado. O pré-requisito registrado é `existing-webview2-evergreen-x64`; o pacote não baixa/instala o runtime. Navegação continua opt-in e downloads permanecem OFF. O setup correspondente também precisa receber a edição explicitamente.

O grafo de produção é inspecionado com a edição correspondente. O helper confere também os inputs do link, pois os comandos podem usar RSP e ocultar bibliotecas:

```powershell
pwsh -NoProfile -File scripts/Test-ProductionBoundary.ps1 -Edition CompleteExperimental -BuildDirectory out/product/complete-experimental-release-20261104
```

Na Lite, o mesmo helper sem `-Edition` recusa fontes e loader de navegação. TDD do boundary passou 8/8 → Red 10 passagens/17 falhas → Green 27/27; os grafos reais aprovados têm 255 comandos Lite e 289 Completa. Configuração/runtime planner passou 115/115; receita 7/7. O build Completa/OFF passou após declarar Qt Widgets no escopo raiz, com lint e zero testes registrados. São checks de construção e fronteira, separados do smoke da engine e da instalação.

## Checks sintéticos

Na raiz, PowerShell 7 x64 com Git local:

```powershell
pwsh -NoProfile -File packaging/windows/tests/WindowsRuntimePlan.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/PeDependencyReport.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/RuntimeRecipe.Tests.ps1
pwsh -NoProfile -File scripts/tests/ProductionBoundary.Tests.ps1
```

Esses checks usam arquivos de texto, relatórios PE e repositórios Git sintéticos. Não executam os `.exe`/`.dll` fictícios. Exportações sem `experiments` verificam independência a partir de diretório corrente alheio. Cobrem confinamento, nomes Windows, reparse points, colisões, destinos novos, hashes, snapshots e seleção de probes. As raízes GUID são verificadas antes de criar/limpar; Git sintético não usa hooks/templates/assinatura pessoais.

Esses quatro checks padrão integram agora `develop.ps1 check` e `validate`, junto aos quatro checks puros do instalador. Ordem, isolamento e evidência ficam no [guia do runner](development-commands.md#checks-das-receitas-windows); também podem ser executados individualmente quando houver mudança relevante na receita.

Compatibilidade histórica separada, sem NSIS/Registry:

```powershell
pwsh -NoProfile -File packaging/windows/tests/PeDependencyReport.Tests.ps1 -IncludeLegacy
pwsh -NoProfile -File packaging/windows/tests/WindowsRuntimePlan.Tests.ps1 -IncludeLegacy
pwsh -NoProfile -File experiments/m0/candidates/qt/windows-installer/tests/WindowsInstallerPlan.Tests.ps1
```

Somente os comandos de compatibilidade acima permanecem fora do runner diário. Não confundir os checks sintéticos com os comandos reais abaixo.

## Montagem e smoke reais, separados

Somente ao atualizar o runtime local, com fontes commitadas e toolchain já provisionada, use builds preparados conforme o [guia desktop](desktop-build.md). Substitua o destino por um nome inexistente:

```powershell
pwsh -NoProfile -File packaging/windows/tests/LocalRuntime.Tests.ps1 -BuildDirectory out/product/desktop-release -OutputDirectory out/product/rc-music-local/nova-revisao
pwsh -NoProfile -File packaging/windows/tests/LocalRuntime.Smoke.ps1 -RuntimeDirectory out/product/rc-music-local/nova-revisao/app -InstrumentedBuildDirectory out/product/desktop-tests
```

O primeiro compila incrementalmente o app OFF e monta arquivos explícitos de Qt/Multimedia/TagLib. Não baixa dependências, reconstrói SDK, copia CRT/System32 ou instala. Recusa saída existente; falha tardia pode deixar diretório parcial, sem rollback ou reutilização automática. O runtime permanece privado/experimental, não uma release.

O smoke usa ambiente reconstruído/offscreen. Build raiz ON seleciona `tests/process/rc_music_test_shell.exe` e `rc_music_local_audio_integration_tests.exe`; histórico mantém nomes anteriores. Nunca trata o app real como instrumentado. O app do payload deve recusar hooks; probes e Qt Test ficam fora dele. A cópia inicial reutiliza o planejador de paths/hashes antes da primeira escrita. Áudio mutado não comprova audibilidade, Explorer, máquina limpa ou inspeção nativa.

## Runtime atualizado de bb826b4

Em 2026-11-03, o commit `bb826b4b00b55ddfa9a3516770ed6a2d15d42ed7` foi compilado em worktree limpa e isolada, preservando os 17 arquivos de desenvolvimento pendentes no checkout principal. CMake Release/x64, `BUILD_TESTING=OFF` e opcionais remotos OFF; SDKs existentes reutilizados. Configure/build, `all_qmllint`, grafo de produção de **253 comandos** e listagem CTest com **zero testes** passaram.

`New-RcMusicLocalRuntime.ps1` montou o runtime dentro da worktree; a cópia final para `out/product/rc-music-local/rc-music-windows-dev-bb826b4-20261103/app` preservou todos os bytes. Conferência independente antes e depois da limpeza:

- **554 arquivos / 96.338.039 bytes**, inventário físico, tamanhos e SHA-256 coincidentes; cinco fontes da receita conferidas e `recipe_dirty=false`.
- `RCMusic.exe`: **2.104.320 bytes**, SHA-256 `37437fbadb29e5fd75a0aa754e1d1f1f3a712bac9ad7d55f48868dc4272b8d5b`; PE x64, Windows GUI (2), sem console automático.
- Manifesto: SHA-256 `62ced215b47b980a3cfb8970166d0fe71b267bca0e7539be5ab30b5df784dc9d`.

O runtime completo e o [setup correspondente](windows-installer.md#instalador-atualizado-de-bb826b4) foram preservados. Worktree/cache temporários e binários substituídos foram removidos somente após verificação. Evidência em `out/product/installer-refresh-bb826b4-20261103/`; `runtime-relocation.json` registra a cópia e seus hashes. Não houve execução do player/setup, smoke novo de áudio, instalação, Registry ou benchmark. Produção remota e distribuição continuam em seus gates próprios.

## Runtime atualizado de c2d7c88

Em 2026-11-03, o commit `c2d7c88ce32d5931e5128c64ab83c456fc06d48b` foi compilado em worktree limpa e isolada, preservando os dois arquivos de testes da extensão ainda pendentes no checkout principal. CMake Release/x64, `BUILD_TESTING=OFF` e todos os opcionais remotos OFF; SDKs existentes reutilizados, sem download ou reconstrução do Qt.

Configure/build, `all_qmllint`, grafo de produção **253 comandos** e CTest com **zero testes** passaram. A receita `LocalRuntime.Tests.ps1` montou e verificou o runtime; após a montagem ele foi movido para `out/product/rc-music-local/rc-music-windows-dev-c2d7c88-20261103/app`. A conferência independente passou antes e depois da limpeza:

- **554 arquivos / 96.338.039 bytes**, tamanhos e SHA-256 coincidentes com o manifesto; cinco fontes da receita conferidas, `recipe_dirty=false`.
- `RCMusic.exe`: **2.104.320 bytes**, SHA-256 `046ab0b1e8064cc083e2278d88fa7196f91b84c8a6f411d2ec1ffbb77b2f1a51`; PE x64, subsistema Windows GUI (2), sem console automático.
- Manifesto: SHA-256 `f580686e6cd0cb566ef2cdfe6db03aedb7d0f811cd52a8476ca70cf0c05b34a4`.

O [setup correspondente](windows-installer.md#instalador-atualizado-de-c2d7c88) e o novo runtime permanecem íntegros. A worktree/cache temporários e os binários do runtime anterior foram removidos após a verificação, conservando evidências e instaladores históricos. Não houve execução do player/setup, smoke de áudio, instalação, Registry ou benchmark; produção remota e gates de distribuição continuam separados. Evidência ignorada em `out/product/installer-refresh-c2d7c88-20261103/`.

## Runtime atualizado de f589040

Em 2026-11-03, o checkout limpo `f5890405e103d29591c5aca871318446ec6b0395` gerou o runtime `out/product/rc-music-local/rc-music-windows-dev-f589040-20261103/app`, pelo comando `LocalRuntime.Tests.ps1` acima, com o cache `out/product/m1-desktop-release-off-20260906`. Release/x64, `BUILD_TESTING=OFF`, opcionais remotos OFF e `recipe_dirty=false`; foram reutilizados os SDKs existentes.

Build incremental, `all_qmllint`, grafo de produção **253 comandos** e listagem CTest com **zero testes** passaram. Conferência independente do inventário físico: **554 arquivos / 96.338.551 bytes**, todos os tamanhos/hashes e cinco fontes da receita conferidos, **71 PEs x64**. `RCMusic.exe` tem subsistema Windows GUI (2), sem console automático.

- Executável: **2.104.832 bytes**, SHA-256 `7999bd0889d6da9b5f5447ab9cdd237c841e8a2369f3711f64d912f4ef52933d`.
- Manifesto: SHA-256 `9c617b256a90169f7885e509d5e57df24d78896ef237e97d838ecc179830ed1f`.
- [Setup correspondente](windows-installer.md#instalador-atualizado-de-f589040) compilado e conferido antes da limpeza; evidência em `out/product/installer-refresh-f589040-20261103/`.

O novo runtime foi preservado e reconferido após a limpeza. Os binários do payload anterior `531c5cc` foram removidos, conservando seu manifesto, QML, configurações e setup. Não houve execução do player/setup, novo smoke de áudio, instalação, Registry ou benchmark; os gates nativos e de distribuição continuam separados.

## Runtime local gerado em 2026-11-03

Atendendo ao pedido de gerar o instalador antes da limpeza, o commit concluído `531c5cc579bd29593e2002a867ddcc1c494e04ed` foi compilado em worktree Git limpa e isolada. O checkout principal continha 14 arquivos de desenvolvimento pendente, incluindo integração ainda incompleta; seus bytes foram preservados e não entraram no pacote. Foram usados os SDKs e compiladores existentes, sem reconstruir Qt ou instalar dependências.

O configure CMake Release/x64 com desktop ON, `BUILD_TESTING=OFF` e opcionais remotos OFF passou, assim como o build de `m0_qt_shell`, `all_qmllint` e a inspeção do grafo de produção de 253 comandos. CTest listou zero testes no produto. O runtime foi montado pela receita da worktree e movido para `out/product/rc-music-local/rc-music-windows-dev-531c5cc-20261103/app` antes da compilação do setup.

- Inventário físico: **554 arquivos / 96.300.663 bytes**, tamanhos/SHA-256 e cinco fontes da receita conferidos.
- `RCMusic.exe`: **2.066.944 bytes**, SHA-256 `6c48b02c821e3922588e70e28ab962f033dde199391124a32762012e6c2a530e`.
- `runtime-manifest.json`: SHA-256 `861e761e0a00067f3c462acde37311426ef8f82ee45317bc7935ace8ca141858`, `recipe_dirty=false`.
- Leitura de identidade/recursos do executável copiado: **6/6 Qt Test**, zero falhas/skips, incluindo subsistema Windows GUI (2).
- Recusa isolada dos hooks reservados no executável real: exit 1, stdout/stderr vazios e nenhuma configuração criada; sem abrir a interface ou a sessão pessoal.

Checks puros proporcionais: parser PE **5/5**, fontes NSIS **5/5** e receita do instalador **3/3**. A verificação independente e os resultados ficam em `out/product/cleanup-20261103/`. O setup correspondente está no [registro do instalador](windows-installer.md#instalador-local-gerado-em-2026-11-03).

A worktree foi removida após a conferência. O comando oficial `ninja -t clean` limpou os intermediários do build isolado; restaram 9.385.583 bytes de configuração/metadados após bloqueio da remoção direta pela revisão automática. Os manifests conservam origens absolutas dessa geração como proveniência histórica. Não houve execução do setup, instalação, Registry, novo smoke de áudio, Brave, benchmark ou validação de release. A limpeza removeu os binários dos runtimes antigos, preservando manifests e evidências conforme o [registro da operação](development-commands.md#limpeza-após-build-do-instalador--2026-11-03).

## Runtime local gerado em 2026-10-24

O runtime `out/product/rc-music-local/rc-music-windows-dev-1c79d81-20261024/app` foi montado do commit limpo `1c79d8187e8acd0cb9d0d295640f22f1e21ea25f`, em Release/x64, com `BUILD_TESTING=OFF` e `recipe_dirty=false`. O build via `develop.ps1 build -Diagnostics` passou; o recibo `out/product/validation-runs/run-a2ef9574017f42149fff249228814118/run.json` registra preflight, configure e build com exit 0 e checkout limpo no início/fim.

Comandos no mesmo PowerShell de desenvolvimento MSVC 14.44 / Windows SDK 10.0.22621.0, usando o cache Release/OFF existente e um destino de runtime novo:

```powershell
& ./scripts/develop.ps1 build -ToolchainFile tools/windows.example.psd1 -BuildDirectory out/product/m1-desktop-release-off-20260906 -Diagnostics
& ./packaging/windows/tests/LocalRuntime.Tests.ps1 -BuildDirectory out/product/m1-desktop-release-off-20260906 -OutputDirectory out/product/rc-music-local/rc-music-windows-dev-1c79d81-20261024
```

Repetir a montagem exige outro `OutputDirectory`. A conferência independente confirmou **554 arquivos / 96.301.175 bytes**, inventário exato e todos os tamanhos/SHA-256, além dos cinco hashes da receita. O executável tem subsistema PE **Windows GUI (2)**, sem console automático.

- `RCMusic.exe`: **2.067.456 bytes**, SHA-256 `c1c21010b884f8136f824560cc29d75b1969f93d8d8bbaec2e213ee97648cb48`.
- `runtime-manifest.json`: SHA-256 `d09940046752a96968d291976243bac050b5c62c3113a937cee103ca88caf113`.
- O runtime alimentou o [setup local de 2026-10-24](windows-installer.md#instalador-local-gerado-em-2026-10-24), com associações OFF.

Checks puros proporcionais: WindowsRuntimePlan **60 passagens/4 falhas**, PeDependencyReport **5/5**, WindowsInstallerPlan **33 passagens/2 falhas** e InstallerRecipe **3/3**. Os seis casos falharam ao criar junctions, com `Acesso negado`; as fixtures próprias foram removidas, sem alterar testes ou elevar privilégios. Isso não equivale a uma suíte inteiramente verde.

Não houve nova execução do player/setup, instalação, Registry, smoke de áudio ou validação de Brave/extensão. A cópia preexistente `bfeb90f` permaneceu fora da limpeza. A montagem e os hashes não comprovam playback, máquina limpa, acessibilidade nativa, lifecycle, segurança/licenças ou release. A [limpeza posterior](development-commands.md) preservou este runtime e removeu os payloads antigos `3f22fda`/`fc11d40`, mantendo seus manifests e registros históricos.

## Runtime sem terminal adicional

Atualização de 2026-09-06, do commit limpo **`bfeb90f1f6a3489a2868204d66b928765c9738a5`**: o executável usa o subsistema Windows GUI e não cria console automático. Inclui os atalhos e modos de fila de `fc11d40`. A [regressão PE](windows-integration-tests.md#abertura-sem-terminal-adicional) passou também sobre o `RCMusic.exe` efetivamente copiado: **6/6**, sem executar o app.

```powershell
& ./packaging/windows/tests/LocalRuntime.Tests.ps1 -BuildDirectory out/product/m1-desktop-release-off-20260906 -OutputDirectory out/product/rc-music-local/rc-music-windows-dev-bfeb90f
```

Montagem e conferência independente passaram: **554 arquivos / 96.112.759 bytes**, inventário exato, tamanhos/hashes conferidos, **5/5 hashes** da receita, 71 relatórios PE, `build_testing=false` e `recipe_dirty=false`.

- Executável: `out/product/rc-music-local/rc-music-windows-dev-bfeb90f/app/RCMusic.exe`, **1.879.040 bytes**, SHA-256 `44fed6b91d7e92a4e0620db7670c3067db35684b2e367a7444af5778f718358d`.
- Manifesto: `out/product/rc-music-local/rc-music-windows-dev-bfeb90f/runtime-manifest.json`, SHA-256 `a9b73ec53a21e03a5a84d6c9130909c0a686d7718491b423e0751dd5d0d28073`.

Encerre a versão anterior por **Sair** antes de abrir esta cópia; uma segunda abertura encaminha para a instância que já estiver rodando. Não houve instalação, Registry, alteração de dados pessoais, nova execução de codecs/benchmarks ou substituição dos runtimes/setups anteriores. Setups antigos ainda abrem o executável anterior. O smoke de áudio e a declaração de formatos antigos não são transferidos para esse artefato; permanece uma versão local privada/experimental.

## Runtime com atalhos e modos de fila

Em 2026-09-06, após implementar o [pedido de atalhos e modos persistentes](ui-tests.md#atalhos-principais-e-modos-de-fila), foi gerada uma nova cópia local do commit limpo **`fc11d405bd88b61b2635e50cf98915fda72dc7e6`**, sem instalar. A receita e as dependências não mudaram; foram usados o mesmo MSVC 14.44/SDK Windows 10.0.22621.0 e cache Release/OFF existentes.

```powershell
& ./packaging/windows/tests/LocalRuntime.Tests.ps1 -BuildDirectory out/product/m1-desktop-release-off-20260906 -OutputDirectory out/product/rc-music-local/rc-music-windows-dev-fc11d40
```

Montagem e conferência independente somente leitura passaram: **554 arquivos / 96.111.223 bytes**, inventário exato, todos os hashes/tamanhos iguais ao manifesto, **5/5 hashes** da receita conferidos e **71 relatórios de imports PE**. O manifesto registra `build_testing=false`, x64 e `recipe_dirty=false`; o executável copiado coincide com o build de produção.

- Executável: `out/product/rc-music-local/rc-music-windows-dev-fc11d40/app/RCMusic.exe`, **1.877.504 bytes**, SHA-256 `5296c1ed01847ec6f6efb38679f0fa7ab49ade6e5aa7a0415a21dcb246491e71`.
- Manifesto: `out/product/rc-music-local/rc-music-windows-dev-fc11d40/runtime-manifest.json`, SHA-256 `c466dd6ffe18770da1c1ae60048aad38ab57d2ee467b5d02b8b10c76ebbf2f60`.

Use esse executável para experimentar os novos atalhos. O runtime `3f22fda` e os dois setups anteriores foram preservados; **os setups não foram recompilados e não contêm este incremento**. Não houve execução do app/setup, Registry, downloads, smoke de codecs do pacote ou benchmarks nesta montagem. Os testes funcionais/build/lint estão no registro do incremento; não se transfere o smoke 18/18 nem a declaração de formatos vinculados ao runtime anterior. A versão continua local privada/experimental, sem assinatura ou aprovação de distribuição.

## Runtime local gerado em 2026-09-06

Após autorização explícita **“Sim, gerar sem instalar”**, o runtime foi montado com as ferramentas locais existentes e o cache desktop raiz Release/OFF `out/product/m1-desktop-release-off-20260906`, conferido contra `tools/windows.example.psd1`. O processo usou o ambiente MSVC 14.44/SDK Windows 10.0.22621.0; não houve download nem rebuild de SDK.

Comando desta execução, dentro desse ambiente de desenvolvimento (o destino era novo; uma repetição exige outro destino):

```powershell
& ./packaging/windows/tests/LocalRuntime.Tests.ps1 -BuildDirectory out/product/m1-desktop-release-off-20260906 -OutputDirectory out/product/rc-music-local/rc-music-windows-dev-3f22fda
```

O gate terminou com exit 0 após build incremental, scanner QML, leitura de imports PE, cópia e verificação de hashes. Uma conferência independente somente leitura confirmou:

- commit de origem **`3f22fda7ae58e6ca2a2bf0587c8f0a620ad44cb9`**, checkout limpo, `recipe_dirty=false`, `BUILD_TESTING=OFF`, arquitetura x64;
- **554 arquivos / 96.076.407 bytes**, sem extras, ausentes, artefatos de teste ou mídia; **71 PEs x64** pelos headers; o `RCMusic.exe` copiado coincide com o executável do build;
- **5/5 hashes** da receita promovida iguais aos fontes, e manifesto SHA-256 **`c3f43d26601fdafd5d8ebe7e779f2c7f34827361681b729cecb182d83f1b742c`**.

O payload está em `out/product/rc-music-local/rc-music-windows-dev-3f22fda/app`; manifesto e `qml-imports.json` ficam no diretório pai, ignorados pelo Git. Esse runtime alimentou a [compilação do setup privado](windows-installer.md#instalador-local-gerado-em-2026-09-06). O identificador de versão do executável ainda é `M0 experimental`; não foi inventada versão de release.

Na montagem original não houve execução do player, `LocalRuntime.Smoke.ps1`, probes, setup ou nova declaração de formatos. A conferência de bytes/headers/imports isoladamente não demonstra carregabilidade, playback no pacote, máquina limpa ou lifecycle. O smoke posterior está registrado abaixo; CRT/pré-requisitos externos, segurança, licenças/notices/SBOM, assinatura e gates nativos continuam pendentes. Nenhum pacote histórico foi substituído.

## Smoke automatizado do runtime

Em 2026-09-06, com HEAD limpo `89cd08c`, foi executado o smoke promovido sobre o runtime `3f22fda` já gerado. Antes de lançar processos, os 554 arquivos e o manifesto foram reconferidos; o cache ON foi validado contra a toolchain selecionada e o build dos dois probes retornou `ninja: no work to do`.

```powershell
pwsh -NoProfile -File packaging/windows/tests/LocalRuntime.Smoke.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-3f22fda/app -InstrumentedBuildDirectory out/product/m1-desktop-release-on-20260906
```

Resultado observado, exit 0 do runner:

- **Produção OFF:** `RCMusic.exe` recusou os hooks antes de abrir configuração/IPC/stores; exit 1, stdout/stderr 0 B e raiz solicitada ausente. QApplication já é inicializada; isso não comprova a jornada normal do executável instalado;
- **Startup ON:** cópia do shell exclusivo de teste, fora do payload, carregou a UI vazia e concluiu `requestExit`/shutdown; exit 0 e stdout/stderr 0 B, com configuração GUID própria;
- **Codecs:** **16/16 linhas e 18/18 testes Qt**, zero falhas/skips. Cobrem sete fixtures básicas, aliases `.mp4`/`.oga` e sete cópias com extensão neutra; cada jornada verifica load, pause, seek até a metade, metadata, retorno a zero e reprodução completa até EOF. Volume zero e mute são verificados antes do load;
- os três processos próprios terminaram; inventário/tamanhos/hashes dos 554 arquivos e ausência de extras foram reconferidos após o smoke. Setup e runtime original não foram modificados.

Evidência local ignorada pelo Git: `out/product/test-runs/rc-music-runtime-smoke/278db7a6503d4b0fa3a31297015f062a/smoke-result.json`, SHA-256 **`c8bfcb96ae72f2636bbef2b558d097e1d2bc02d862aebc91b25045ef96f8f299`**. As 16 linhas PASS foram conferidas individualmente, além do agregado. O resultado preserva diagnósticos FFmpeg em stderr; o contrato dessa jornada não exige streams vazios, ao contrário dos dois processos de shell.

Proveniência dos probes usados:

| Input | SHA-256 |
|---|---|
| Script de smoke | `c5f6137e1774ce7e6bd37edea8ed18ea67181a86de6aa47704cd212da8dab7ee` |
| Shell instrumentado | `8b3d7ab523f0f5b5efb7ffcdc134b0b6ab5bfa4aa8ee51c77ad8d3d18eeba3d9` |
| Teste de áudio | `868d4b37139c935bb279e3c1b38c810f53f0c16dfb193fc70e0ef0f5f3ad848d` |
| Qt6Test, somente no probe | `8cee7db0c3e75e8f32da032c31893638207caa25f81f28083baa9afa0dcd0612` |

O ambiente foi reconstruído com `offscreen/software`, PATH de runtime/Windows e configuração explícita. Isso isola os dados próprios do teste, não todas as APIs Known Folder ou efeitos do Qt/Windows: o ON tenta SMTC/eventos de energia, e o áudio real pode abrir sessão de dispositivo, embora silenciado. Não houve Computer Use, janela nativa interativa, instalação, registro de associações ou alteração de volume mestre. Não se mediu desempenho nem se repetiu CTest global.

A prova de codecs é do adapter/presenter instrumentado usando as bibliotecas declaradas do runtime, não da abertura via Explorer ou do setup instalado. O resultado sustenta somente a [declaração local v2 dos perfis básicos](windows-installer.md#declaracao-local-v2-validada); não aprova outros perfis, Opus/ALAC, audibilidade, máquina limpa, acessibilidade nativa ou release.

## Evidência e limites da promoção original

- Guarda de produção: baseline **7/7**, Red **7 passes/1 falha** pelo import histórico ausente na exportação, Green **8/8** com import raiz. A inspeção read-only do build OFF existente confirmou **248 comandos**, fontes reais presentes e suporte experimental ausente, sem rebuild.
- Planner de runtime: baseline **56/56**, Red **50 passes/14 falhas**, Green **64/64**, preservando planejamento/cópia. Com `-IncludeLegacy`, **68/68** inclui quatro caracterizações de propagação de falhas pelos encaminhadores. Alternar import direto/histórico na mesma sessão passou **4/4**. A seleção de probes ON e os snapshots produto/histórico têm regressões específicas.
- Parser: corpo original preservado; **5/5** produto e **8/8** com compatibilidade histórica. O teste PE original passou antes/depois.
- Planner do instalador: baseline **18/18**, Red **18 passes/2 falhas** pela proveniência omitida, Green **20/20**, sem NSIS.
- Receita exportada: **5/5**; inventário de cinco hashes reais, mudança isolada do parser refletida no hash, build obrigatório e rejeição de cache Debug antes de ferramentas ou saída. Esta é caracterização da receita promovida, não montagem de runtime.
- Rastreabilidade: o novo diretório durável `packaging` foi aceito após Red **43 passes/1 falha** e Green **44/44**, mantendo rejeição de output/traversal. Os **83 requisitos** continuam com vínculos válidos, sem promover estados de conclusão. `qt.conf` e o aviso privado mantêm SHA-256 idêntico aos originais históricos.

Não foi gerado runtime/instalador nem executado `LocalRuntime.Tests.ps1` ou `LocalRuntime.Smoke.ps1`. Áudio/UI/CTest global, builds, benchmarks, instalação, Registry e Computer Use não foram repetidos. A seleção de probes tem teste de contrato; o smoke promovido permanece sem nova execução.

Incremento posterior: a [receita do instalador e o leitor de formatos](windows-installer.md) também foram promovidos, com catálogo factual e declaração de produto v2. A matriz v1 continua vinculada ao corpus M0 e ao runtime que a originou, acessível somente pela compatibilidade explícita. Não transferir essa aprovação para artefato novo. Compilação/lifecycle da receita promovida, segurança, licenças/notices/SBOM, CRT, assinatura, máquina limpa e jornadas nativas permanecem pendentes. M1, M4.6/M4.8 e release não estão concluídos.

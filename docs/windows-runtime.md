# Runtime Windows — receita de produto

**Estado:** runtime local atualizado montado e inventário verificado; execução do payload, lifecycle e gates de distribuição continuam abertos.
**Data:** 2026-09-06.

## Implementação única

[`packaging/windows`](../packaging/windows/New-RcMusicLocalRuntime.ps1) contém a receita local, o planejador de cópia, o parser PE, `qt.conf`, o aviso de uso privado e os testes. O produto não importa módulos, protocolos ou fixtures de `experiments`. Os caminhos antigos de runtime são encaminhadores; o builder comparativo também reutiliza o parser PE puro.

O build é explícito na entrada de produto: somente Release, `BUILD_TESTING=OFF` e entrypoint reconhecido, raiz com desktop habilitado ou consumidor histórico com áudio/shell habilitados. O executável real continua `m0_qt_shell.exe`, renomeado para `RCMusic.exe` no payload; marca RC Music e dados/locks MusicPlayer não mudam.

O snapshot usa o entrypoint validado. Produto verifica `CMakeLists.txt`, `cmake`, `include`, `src`, `tests`, `tools`, `assets` e `packaging/windows`; histórico conserva também seus três inputs M0. Alterações staged, unstaged ou untracked nesses inputs impedem atribuir um runtime ao commit limpo. Não há autenticação de publicador ou proteção contra escritor hostil concorrente.

O manifesto mantém schema 1 e campos de compatibilidade, acrescentando `parser_sha256`, `recipe_path_base=repository` e cinco paths/hashes reais da receita, módulos e arquivos de configuração, revalidados antes de publicar o manifesto. O instalador histórico inclui o hash do planejador promovido na identidade/recibo, além das oito chaves anteriores. O builder comparativo identifica seus sete inputs de receita relativamente à raiz. Nenhum manifesto, protocolo, corpus, matriz ou pacote histórico é reescrito ou aprovado retroativamente.

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

Não houve execução do player, `LocalRuntime.Smoke.ps1`, probes, setup ou nova declaração de formatos. A conferência de bytes/headers/imports não demonstra carregabilidade, playback no pacote, máquina limpa ou lifecycle. CRT/pré-requisitos externos, segurança, licenças/notices/SBOM, assinatura e gates nativos continuam pendentes. Nenhum pacote histórico foi substituído.

## Evidência e limites da promoção original

- Guarda de produção: baseline **7/7**, Red **7 passes/1 falha** pelo import histórico ausente na exportação, Green **8/8** com import raiz. A inspeção read-only do build OFF existente confirmou **248 comandos**, fontes reais presentes e suporte experimental ausente, sem rebuild.
- Planner de runtime: baseline **56/56**, Red **50 passes/14 falhas**, Green **64/64**, preservando planejamento/cópia. Com `-IncludeLegacy`, **68/68** inclui quatro caracterizações de propagação de falhas pelos encaminhadores. Alternar import direto/histórico na mesma sessão passou **4/4**. A seleção de probes ON e os snapshots produto/histórico têm regressões específicas.
- Parser: corpo original preservado; **5/5** produto e **8/8** com compatibilidade histórica. O teste PE original passou antes/depois.
- Planner do instalador: baseline **18/18**, Red **18 passes/2 falhas** pela proveniência omitida, Green **20/20**, sem NSIS.
- Receita exportada: **5/5**; inventário de cinco hashes reais, mudança isolada do parser refletida no hash, build obrigatório e rejeição de cache Debug antes de ferramentas ou saída. Esta é caracterização da receita promovida, não montagem de runtime.
- Rastreabilidade: o novo diretório durável `packaging` foi aceito após Red **43 passes/1 falha** e Green **44/44**, mantendo rejeição de output/traversal. Os **83 requisitos** continuam com vínculos válidos, sem promover estados de conclusão. `qt.conf` e o aviso privado mantêm SHA-256 idêntico aos originais históricos.

Não foi gerado runtime/instalador nem executado `LocalRuntime.Tests.ps1` ou `LocalRuntime.Smoke.ps1`. Áudio/UI/CTest global, builds, benchmarks, instalação, Registry e Computer Use não foram repetidos. A seleção de probes tem teste de contrato; o smoke promovido permanece sem nova execução.

Incremento posterior: a [receita do instalador e o leitor de formatos](windows-installer.md) também foram promovidos, com catálogo factual e declaração de produto v2. A matriz v1 continua vinculada ao corpus M0 e ao runtime que a originou, acessível somente pela compatibilidade explícita. Não transferir essa aprovação para artefato novo. Compilação/lifecycle da receita promovida, segurança, licenças/notices/SBOM, CRT, assinatura, máquina limpa e jornadas nativas permanecem pendentes. M1, M4.6/M4.8 e release não estão concluídos.

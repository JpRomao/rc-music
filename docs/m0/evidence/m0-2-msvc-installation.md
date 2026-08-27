# M0.2 — MSVC instalado e build de verificação

**Data:** 2026-08-27
**Estado deste checkpoint:** instalação e build Release verificados; a receita ainda não estava integrada ao refresh. A [integração posterior](m0-2-msvc-package.md) tem evidência própria; M0.2 permanece aberto.

## Resultado

Após a nova autorização explícita do mantenedor, a segunda solicitação de elevação foi aceita e a instalação terminou com código **0**, sem reinicialização pendente. O MSVC **19.44.35228.0** compilou e linkou o shell Qt em uma pasta nova; CTest passou **65/65**. A [primeira tentativa cancelada](m0-2-msvc-refresh.md) permanece como histórico, não como estado atual da instalação.

O escopo foi a nova instância Build Tools e o componente MSVC v143 14.44 com suas dependências obrigatórias, conforme o procedimento previamente documentado. Nenhuma confirmação de segurança foi automatizada. Não foram solicitados reparo/atualização de outras instâncias, remoção de toolsets ou reinício. O instalador compartilhado atualizou seus próprios arquivos; não se afirma isolamento absoluto das dependências compartilhadas.

| Campo | Evidência observada |
|---|---|
| Solicitação / processo iniciado, UTC | `2026-08-27T17:00:02.3841702Z` / `2026-08-27T17:00:04.1924749Z` |
| Término, UTC | `2026-08-27T17:01:30.8557672Z`, exit 0 |
| Bootstrapper | `18.9.12120.119`, 5.690.848 bytes; assinatura Microsoft `Valid` |
| Bootstrapper SHA-256 | `352ac5776d592a4870c15756f9e140507b9ee64de62b401152e5929f7c0c52a6` |
| Instância | `016ce806`, `Microsoft.VisualStudio.Product.BuildTools` |
| Instalação / versão | `C:\Program Files\Microsoft Visual Studio\18\BuildTools` / `18.9.12120.119` |
| Inventário `vswhere` | `isComplete=true`, `isLaunchable=true`, `isRebootRequired=false` |
| Componente requerido | `Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64`, também encontrado por `vswhere -requires` |

O `state.json` da instância registra seis seleções: o produto BuildTools, MSVC 14.44, CoreBuildTools, Roslyn.Compiler, MSBuild e Workload.MSBuildTools. São seleções, não contagem de todos os pacotes transitivos. Os dois logs de erros da operação têm zero bytes; o log principal registra `Completed install operation`.

## Contrato real, separado do histórico

O [contrato de refresh](../../../experiments/m0/provisioning/qt-shell/msvc-refresh-contract.v1.json) tem **2.062 bytes**, SHA-256 **`eead467f5a597a72b6ff45812b40040db629232bc089d0c61c0dd64c73693f03`**. Seus pins são observações reais, não os valores sintéticos dos testes. As três ferramentas estão em `VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64` sob a instância acima; o nome do diretório `35207` e a revisão dos executáveis `35228` são campos distintos preservados como encontrados.

| Ferramenta | FileVersion / ProductVersion | Bytes | SHA-256 |
|---|---|---:|---|
| `cl.exe` | `19.44.35228.0` / `14.44.35228.0` | 677736 | `88c8344236a27a6e727e0a8edc49aaa2690bdc7a9464b9d18cc7abe70a9f1c0d` |
| `link.exe` | `14.44.35228.0` / `14.44.35228.0` | 3252576 | `ca11e6c45debd34bf652dfe984c5360a531a005ed78bf72852330c9c2590cf0d` |
| `dumpbin.exe` | `14.44.35228.0` / `14.44.35228.0` | 23416 | `12a1cd87238bd66dfdb788b4fcdcb91ce4b3f81236aab12a7a29e5ae1d85af50` |

Todas têm Authenticode `Valid`, assinante Microsoft Corporation, e certificados pinados no contrato. A revisão independente conferiu paths sem reparse, versões, tamanhos, hashes, assinatura e aceitação pelo helper existente. O linker novo supera o piso `14.44.35227.0` correspondente ao build Qt. Essa comparação não é aprovação genérica de ABI, segurança ou redistribuição.

O contrato histórico `ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952` e seus oito membros continuam byte a byte iguais. Qt 6.11.2, Catch2, fixture e protocolo não foram atualizados. CMake 3.31.5, CTest 3.31.5 e Ninja 1.12.1 foram reconferidos contra os hashes do descriptor histórico.

## Build e receipt real

Fonte compilada: `ea5411c`, incluindo os atalhos de `707b031`, sem alteração C++/QML nesta fatia. Pasta nova: `out/m0/builds/qt-shell/msvc-14.44-install-check-20260827-01/`. Nenhum build ou pacote anterior foi sobrescrito.

- Developer PowerShell da nova instância com `-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0`; o SDK foi escolhido explicitamente, pois o default observado era 10.0.26100.0.
- Configure Ninja Release com `BUILD_TESTING=ON`, `M0_QT_SHELL_SUPPORT=ON`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, paths explícitos de `cl`/`link` e Ninja histórico. Qt/Catch2 vieram dos diretórios já provisionados, sem download durante o build.
- Configure aprovado e build do zero concluído em 277 passos; `qmllint` sem diagnósticos. O configure informou ausência de `WrapVulkanHeaders`; não se alega validação de Vulkan.
- `New-QtShellMsvcRefreshReceipt` autenticou o contrato, as ferramentas e os arquivos **reais** `CMakeCXXCompiler.cmake`, `CMakeCache.txt` e `qconfig.pri`. O probe do linker usou grafia 8.3, enquanto os paths efetivos permaneceram coerentes com os pins.

O receipt real `msvc-refresh-receipt.v1.json` tem 3.131 bytes e SHA-256 `86986ec2c2095840e63ce1acafbc81f37aa74a81785e28177795223925443773`. Ele registra compiler info `6ccdc4cc7d2c15c8f4daebb1ac44c82e73c89f7c0601feafe4cca7a0598c6df4`, cache `df89f1df232cb6b6e83cf99091e9007e7d1a50e4706fd30fe4dac2a80560997d` e qconfig `709a167ec64027dfeba7ddbd3429dc39a497a3b84cb6861460df3990616991e1`. É autenticação de arquivos quiescentes, não proteção contra escritor concorrente ou prova do novo pacote.

A revisão independente reconferiu os cinco hashes referenciados pelo receipt e as 176 entradas de `compile_commands.json`: todas usam o compilador 14.44, sem referências ao compilador antigo. Headers MSVC, `rc` e `mt` também correspondem ao toolset e ao SDK 10.0.22621.0 selecionados. Os logs confirmam 277 passos concluídos, `qmllint` sem diagnósticos e CTest 65/65 sem skips. Os probes pthread negativos do configure são esperados neste Windows e não impediram encontrar Threads.

Artefatos locais, relativos à pasta do build acima, fora do Git:

| Artefato | SHA-256 |
|---|---|
| `configure.log` | `43da7f80b9997e523843bb5ab415317cdfbe95b26cf2ef826530614fef733778` |
| `build.log` | `9b87ffc889571ebd79d9b020bd4b6403654ac9132b31c9c7b8f52861d4c3d4a0` |
| `qmllint.log` | `996717ad0d8f3849892205bd313e1b30f1f88fee529fb1d4f573c5e47b9b4d28` |
| `ctest-release.log` / `.xml` | `4a5c6f9b1011b52b67be226e4ab997a4920900ada678d0efa8e88cae3c82e799` / `25640282478382eb72edd620e44b51520692650de5aeced0da9cf36a41ece63c` |
| `m0_qt_shell.exe` | `0db36e5c2b1a63e8568d864041192a0b1707b68ac40a2cc9e09b539dc5a524bd` |
| `installation-logs/dd_bootstrapper_20260827140006.log` | `929e05c22f5f8787642ea199f87cf17df2581f05519ec08bce39bef3a637645c` |
| `installation-logs/dd_setup_20260827140035.log` | `1215cb6f79f29b44d8dca07cd417cd4a08dfd511ee07cd463b4a175108044e49` |
| `installation-logs/dd_setup_20260827140127.log` | `cb556308bd63232dd50cbc9ab87edd4a6f3810aca02d57b82f3f9a831900e617` |

Os dois arquivos `*_errors.log` foram preservados na mesma subpasta, vazios, SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

## Checks e fronteira de conclusão

- CTest Release: **65/65**, 20,65 s, incluindo as jornadas Qt Quick e os smokes de erro/ciclo de vida, todos offscreen. Nenhuma nova jornada nativa, NVDA, DPI/HC real ou áudio pessoal foi executada.
- Empacotador PowerShell: **181/181** — Builder 14, refresh 16, manifesto 113 e smoke 38. Parser de nove scripts: zero erros; PSScriptAnalyzer continua não instalado.
- Validação documental: **36 Markdown, 83 requisitos, 17 checks e 76 etapas**, aprovada; `git diff --check` sem erros.
- Revisão independente documental/contratual: sem achados acionáveis, preservando a distinção entre instalação/build e pacote/gates ainda pendentes.
- O manifesto histórico `a227bb2b…97dc99` foi revalidado integralmente: 1.378 arquivos preservados. Os cinco arquivos CRT externos permanecem com os hashes/versões históricos `14.51.36247.0`.
- Não restaram processos do novo build. A mudança versiona dados de identidade e documentação, sem comportamento novo que exija Red/Green artificial; os helpers já tinham TDD em `b59a247`.

A instalação resolve a ausência do MSVC 14.44, e o build prova seu uso com este shell. A [integração posterior do contrato/receipt à receita](m0-2-msvc-package.md) foi implementada por TDD e validada com o pacote diagnóstico offscreen; falta gerar o pacote de receita commitada e repetir seus smokes original/relocado. Os stages antigos não passam a usar o novo compilador. A [triagem de segurança](m0-2-security-triage.md), as obrigações de licença e os gates nativos continuam pendentes. Esta entrega não encerra M0.2, não libera M0.3 e não aceita uma stack.

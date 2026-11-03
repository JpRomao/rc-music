# Instalador Windows — receita de produto

**Estado:** setup privado atualizado em 2026-11-03 compilado com associações OFF; os setups históricos OFF/v2 foram preservados. Execução/lifecycle e gates de distribuição ainda pendentes.
**Data:** 2026-11-03.

## Fonte única e proveniência

[`packaging/windows`](../packaging/windows/New-RcMusicInstaller.ps1) reúne o builder, o planejador, o leitor de formatos e os quatro templates/helpers NSIS. A geração da configuração e os quatro arquivos NSIS preservam a lógica anterior; os NSIS foram comparados byte a byte à revisão `3f539d6`. LF é fixado por `.gitattributes` para conservar esses bytes em outro checkout.

O produto não importa o experimento. Os caminhos antigos são encaminhadores, e a compatibilidade com a declaração v1 é explícita. O template corrente do harness histórico aponta diretamente para a fonte promovida; os snapshots de versões antigas continuam vindo de seus commits fixos. Os includes dos encaminhadores são ancorados em `__FILEDIR__`, com diretório de busca explícito, conforme a [documentação do pré-processador NSIS](https://nsis.sourceforge.io/Docs/Chapter5.html). Os encaminhadores foram inspecionados estaticamente; sua compilação não foi exercitada. A compilação direta da receita de produto está registrada abaixo.

`Get-RcMusicInstallerRecipeHashes` é o inventário único de nove fontes reais, com caminhos relativos à raiz e SHA-256. Planejamento, identidade do payload, builder e revalidação pós-compilação usam essa mesma lista, não hashes dos encaminhadores. O recibo mantém schema 1 e acrescenta `recipe_path_base=repository`. Corrigir somente um helper muda a identidade; runtime e recibos antigos não são reatribuídos a um commit novo.

O builder exige `-MakeNsis` explícito e confere seu hash contra a cópia byte-idêntica de `nsis-toolchain.json`. Esse recibo conserva a observação histórica, não autentica publicador nem autoriza instalação/download. PowerShell/NSIS são ferramentas de build, não dependências do player entregue.

## Catálogo factual e declaração por runtime

[`tests/audio/corpus/catalog.v1.json`](../tests/audio/corpus/catalog.v1.json) descreve sete fixtures básicas já promovidas: três MP3, AAC em M4A, FLAC, WAV PCM e Ogg/Vorbis. Registra tamanho/hash, contêiner/perfil, classificação `baseline` e capacidades exigidas. Seus bytes concordam com os pins de `RcMusicAudioFixtures.cmake`; a licença privada é preservada. Não contém resultado de execução, aprovação de runtime ou promessa de suporte público. Opus, ALAC, entradas hostis e extensão disfarçada não entram nessa elegibilidade.

Uma declaração de produto é **schema 2**, scope `windows-x64-product-local-development`, backend `QtMultimedia/FFmpeg`. Ela vincula explicitamente:

- `runtime_manifest_sha256`: o runtime exato;
- `fixture_catalog_sha256`: o catálogo de entradas;
- `test_source_sha256`: a fonte atual de `LocalAudioIntegrationTests.cpp`;
- `result_sha256`: o resultado revisado daquela execução;
- `formats`: extensões, descrições e IDs das fixtures correspondentes.

O leitor exige `validation_status=passed-local`, mas esse texto e `result_sha256` são referências de evidência revisada, não assinatura ou prova automática de aprovação. Conferir o resultado real continua obrigatório. O leitor rejeita tipos inválidos, hashes divergentes, candidatos, aliases indevidos, duplicatas e nomes inseguros; revalida também os bytes das fixtures referenciadas. Catálogo e declaração são reconferidos ao terminar o planejamento.

Na promoção original, nenhuma declaração de produto aprovada foi criada; os testes constroem declarações fictícias exclusivamente em diretórios GUID. A declaração real posterior está registrada abaixo. A matriz v1 histórica permanece inalterada, vinculada ao seu corpus/runtime. Só o entrypoint antigo ou `-LegacyBaseline` explícito escolhe essa baseline; a entrada padrão recusa v1 antes de consultar o histórico. Não basta trocar o número de schema ou recalcular hashes para aprovar outro runtime.

### Declaração local v2 validada

A [declaração `rc-music-windows-dev-3f22fda.v2.json`](../packaging/windows/formats/rc-music-windows-dev-3f22fda.v2.json) registra o resultado revisado do [smoke de codecs do runtime](windows-runtime.md#smoke-automatizado-do-runtime): 16 linhas/18 testes sem falhas ou skips. Seu SHA-256 é **`d066ac79e1f21d2d0769d1cad3ad64ef9c910e579c193197cb31d0a042a8a851`**. Ela vincula o manifesto do runtime `3f22fda`, catálogo factual, fonte do teste e `smoke-result.json` pelos quatro hashes reais, sem transferir aprovação da matriz v1.

Declara somente os perfis básicos das sete fixtures: MP3 CBR/VBR e tags Unicode, AAC LC em M4A/MP4, FLAC 16-bit, WAV PCM 16-bit e Vorbis em Ogg/OGA. As sete extensões são `flac`, `m4a`, `mp3`, `mp4`, `oga`, `ogg`, `wav`; `.audio` foi usada apenas nas cópias de teste, não declarada como associação. Isso não promete todos os codecs/perfis desses contêineres.

Leitura e planejamento reais passaram sem escrita: sete formatos/callbacks exatos, associações ON somente com argumento explícito, assinatura **`10e0c7a4087c3a1fab787b0a91d071234bf19c26d43dea71a75100d01070d6ef`** e payload ID **`15343435b378183f2ecd1fb5c46c4af61d285034cefb7dfc6c4c15d3a6104fae`**. Uma chamada com hash de outro runtime foi recusada antes de saída. Esses valores descrevem dados/configuração revisados, não execução de instalação ou alteração do Windows; o setup OFF já entregue permanece inalterado.

## Checks sem compilação ou instalação

Na raiz, PowerShell 7 x64:

```powershell
pwsh -NoProfile -File packaging/windows/tests/FormatMatrix.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/WindowsInstallerPlan.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerSources.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerRecipe.Tests.ps1
```

`-IncludeLegacy` nos três primeiros acrescenta compatibilidade histórica. Esses checks não compilam nem executam NSIS. Usam arquivos sintéticos, cópias próprias das fixtures licenciadas e exportações sem `experiments`, a partir de diretório corrente alheio. OFF não precisa de catálogo/corpus/fonte de teste. O preflight do builder recebe um arquivo de texto como compilador e deve rejeitá-lo pelo hash antes de tentar executá-lo ou criar saída.

As quatro entradas padrão integram agora `develop.ps1 check` e `validate`, junto aos quatro checks puros do runtime. Ordem, isolamento e evidência ficam no [guia do runner](development-commands.md#checks-das-receitas-windows). `-IncludeLegacy` e os harnesses nativos históricos de arquivos, associações e lifecycle continuam separados; esta integração não executa esses gates nativos.

## Montagem real, somente quando necessária

Depois de montar/verificar um [runtime local](windows-runtime.md), com fontes commitadas e compilador já provisionado, o comando abaixo compila um setup privado sem executá-lo. Escolha uma saída nova; o exemplo não habilita associações:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/nova-revisao/app -OutputDirectory out/product/rc-music-installer/nova-revisao -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

O caminho do compilador no exemplo é o provisionamento local existente, não dependência de código/fixtures M0. Outro ambiente fornece explicitamente seu caminho verificado. `-FormatMatrix` somente deve apontar para uma declaração revisada para aquele runtime; sem argumento, associações ficam OFF. O diretório de saída deve ser novo sob `out/product` ou `out/m0` para compatibilidade. Falhas podem deixar saída parcial sem receipt aprovado; não há rollback, reutilização ou limpeza automática.

## Instalador atualizado de f589040

Em 2026-11-03, o [runtime atualizado](windows-runtime.md#runtime-atualizado-de-f589040) e a receita do checkout limpo `f5890405e103d29591c5aca871318446ec6b0395` geraram o novo setup, com as ferramentas provisionadas:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-f589040-20261103/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-f589040-20261103 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

Compilação exit 0, associações OFF, `recipe_dirty=false`, sem matriz de formatos antiga. Conferência independente de inventário, cinco hashes da receita do runtime, nove do instalador, compilador, configuração e manifesto passou. Replanejamento sem escrita reproduziu o inventário, a configuração e o payload ID `8abc0d1a46b3303dd4fc1826a269ce5623aeafb8a6ccd76f7bfffaaa6464fa4e`.

- `RCMusic-Setup.exe`: **38.061.983 bytes**, SHA-256 `0b24d602a084f20373e44a8a49656fcb52940a96a5233263ab9e935af105ae9f`, Authenticode **NotSigned**.
- `installer-receipt.json`: SHA-256 `4d123b946ad60a46365297d5d9fad136596561cbc32a3ceba2816830ed011421`.
- `installer-config.nsh`: SHA-256 `a95a95fa65bfde1ec8c5b3542eeaa29f54ec752f1bd19e46fbe675a5dc2c5a4f`.

Checks puros proporcionais: parser PE **5/5**, fontes NSIS **5/5**, receita do instalador **3/3**. Evidência em `out/product/installer-refresh-f589040-20261103/independent-verification.json`; build/lint e [limpeza posterior](development-commands.md#limpeza-após-atualização-de-f589040) têm registros próprios. Setup e runtime novos permaneceram íntegros após a limpeza. Nenhum setup/player foi executado, instalado ou publicado, nem houve mudança de Registry/associações. O pacote contém o player local; módulos remotos continuam OFF. Lifecycle, assinatura e demais gates de distribuição permanecem pendentes.

## Instalador local gerado em 2026-11-03

O [runtime Release/OFF do commit limpo `531c5cc`](windows-runtime.md#runtime-local-gerado-em-2026-11-03) alimentou o novo setup privado. A geração isolada preservou integralmente os 14 arquivos pendentes do checkout principal. A receita do instalador permaneceu limpa, no mesmo commit, e o NSIS existente foi conferido pelo pin de `nsis-toolchain.json`.

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-531c5cc-20261103/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-531c5cc-20261103 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

A compilação terminou com exit 0, sem `-FormatMatrix`, execução de setup ou instalação. O diretório de saída era novo. A verificação independente conferiu:

- `RCMusic-Setup.exe`: **38.052.800 bytes**, SHA-256 `6c762e7267218fa8d82cc5b7bd7783c4c3bb6f2fe1ef9227a2fdec0a93ff4f73`, Authenticode **NotSigned**.
- `installer-receipt.json`: SHA-256 `2d93f4f236591e093e67263b266ad54ac79f9f76b5b501b52613a88eb3069097`.
- `installer-config.nsh`: SHA-256 `f89b7102090aa55b6e31dce09a4f4d6c851ddba92c424b05b60b23d7661d0bdb`.
- Inventário físico dos **554 arquivos**, manifesto, cinco fontes de receita do runtime, nove do instalador e hash do compilador correspondentes ao receipt.
- Replanejamento sem escrita reproduziu o payload ID `dd660783590b02c19de9f78043da1a0cb672f8b46293e5755a0bec4e35b81974`, associações OFF, formatos/callbacks vazios e `format_matrix=null`.

Evidência local ignorada: `out/product/cleanup-20261103/installer-verification.json`. O novo setup e runtime foram preservados na [limpeza subsequente](development-commands.md#limpeza-após-build-do-instalador--2026-11-03). Nenhum setup foi executado, Registry/associação alterado ou componente de navegador instalado. O pacote contém o player local do commit `531c5cc`; a integração remota pendente não foi ativada. Instalação/lifecycle, máquina limpa, assinatura, segurança/licenças e distribuição continuam gates separados.

## Instalador local gerado em 2026-10-24

A receita de produto compilou, sem executar o setup, o [runtime atualizado](windows-runtime.md#runtime-local-gerado-em-2026-10-24) em `out/product/rc-music-installer/rc-music-windows-dev-1c79d81-20261024/RCMusic-Setup.exe`. Receita e runtime vinculam o commit limpo `1c79d8187e8acd0cb9d0d295640f22f1e21ea25f`; `recipe_dirty=false`, runtime Release/x64 com `BUILD_TESTING=OFF` e associações OFF. Não foi fornecida matriz de formatos nem transferida a declaração do runtime antigo.

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-1c79d81-20261024/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-1c79d81-20261024 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

O destino era novo e o comando terminou com exit 0. A revisão independente somente leitura confirmou:

- Setup: **38.052.419 bytes**, SHA-256 `ff809cde4ffd6ad608e455ea6f9ed0155f120c8411f97ec2e0a52ef4b8756009`.
- `installer-receipt.json`: SHA-256 `f8a23026510da3ec06ab4b0d64219b563dc4ac9f9f530cfd84fdb8ed8322da07`.
- `installer-config.nsh`: SHA-256 `9862fd803b5eeafebd7aa98c833edbf0636cc247fed6907d208549958f1ff519`.
- Manifesto do runtime: SHA-256 `d09940046752a96968d291976243bac050b5c62c3113a937cee103ca88caf113`.
- Inventário físico de **554 arquivos / 96.301.175 bytes**, coincidente com manifesto/receipt; cinco hashes de receita do runtime, nove da receita do instalador e compilador NSIS conferidos.
- Replanejamento sem escrita reproduziu o payload ID `8ea215cfd0e5824b1f6e655033c2c40dcddaa67aff1b3ec5e767a9d249e46c5c`, formatos vazios, assinatura do conjunto vazio e `format_matrix=null`.
- Authenticode **`NotSigned`**; executável do player com subsistema Windows GUI (2).

O [registro do runtime](windows-runtime.md#runtime-local-gerado-em-2026-10-24) documenta o build aprovado e as seis falhas de criação de junction por `Acesso negado` nos checks puros. Não houve execução do novo player/setup, instalação, Registry, áudio, Brave/extensão ou publicação. O artefato permanece privado/experimental; lifecycle, máquina limpa, assinatura, segurança/licenças e distribuição continuam pendentes. A limpeza posterior preservou o novo setup, os setups históricos e os respectivos receipts.

## Instalador local gerado em 2026-09-06

Após autorização explícita para **gerar sem instalar**, a entrada de produto compilou o [runtime atualizado](windows-runtime.md#runtime-local-gerado-em-2026-09-06) do commit limpo **`3f22fda7ae58e6ca2a2bf0587c8f0a620ad44cb9`**, usando NSIS 3.12 já provisionado e conferido pelo recibo. Comando executado com exit 0, sem `-FormatMatrix` e em destino então inexistente:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-3f22fda/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-3f22fda -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

Conferência posterior somente leitura do artefato, configuração e recibo:

- `RCMusic-Setup.exe`: **37.980.884 bytes**, SHA-256 **`83166f6cb8d056ceb283ec781046cda45c2b77253672e5d3eee053c356e12ea2`**;
- `installer-receipt.json`: SHA-256 **`33f20a4c55ba4e1082884299decd13d1eabaff83660a9a2b3dbdd73a46778339`**, receita/runtime vinculados ao commit acima e `recipe_dirty=false`;
- inventário de **554 arquivos** coincide com o manifesto do runtime; nove hashes de fontes e hash do compilador coincidem com os inputs atuais. Hashes do setup, configuração e manifesto foram reconferidos;
- associações **OFF**, lista/callbacks de formatos vazios e assinatura do conjunto vazio conferida independentemente; nenhum corpus/matriz histórico foi usado para inferir formatos;
- replanejamento sem escrita reproduziu o payload ID **`ca04ad5303fd47c690b5a6cffa1075ffad59266e21fb30d8383498a8cfd762e9`**;
- Authenticode: **`NotSigned`**. Artefatos privados e ignorados pelo Git, sem publicação.

Essa execução comprova compilação da receita direta e consistência dos artefatos, não instalação, atualização, remoção, launch ou playback. Na geração OFF nenhum setup/player/probe foi executado, nenhum Registry/menu/associação foi alterado e nenhum runtime/pacote anterior foi sobrescrito. `InstallerBuild.Tests.ps1` histórico não foi executado: o builder de produto foi chamado diretamente, seguido das verificações acima. A validação local posterior dos perfis e o setup separado constam abaixo; lifecycle, CRT/máquina limpa, segurança/licenças e validações nativas seguem abertos, sem release aprovada.

## Instalador com formatos v2

Após o smoke e o commit da declaração **`cc1de1c4145af1c81c840d4f4689ffa4955d69db`**, a mesma receita de produto compilou um setup separado com `-FormatMatrix` explícito. Não foi remontado o runtime nem substituído o setup OFF. Comando executado em fonte limpa, exit 0:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-3f22fda/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-3f22fda-formats-v2 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe -FormatMatrix packaging/windows/formats/rc-music-windows-dev-3f22fda.v2.json
```

Conferência posterior somente leitura aprovada:

- setup **37.989.715 bytes**, SHA-256 **`bcdd56db5fc3a0c71c90357f5e5a4017d09cb89a730c48c73a9c275040f60f2a`**;
- recibo SHA-256 **`6f67139c7b168b08e26aca49ae3515ba06356bf347d77194518986966ca939d4`**; receita `cc1de1c`, runtime `3f22fda`, `recipe_dirty=false`;
- nove hashes de fontes, compilador, manifesto, declaração/resultado e 554 arquivos do runtime coincidem com os inputs; configuração compilada contém exatamente os sete callbacks, associações ON e a assinatura da declaração;
- replanejamento sem escrita reproduziu inventário, declaração completa, formatos, assinatura e payload ID registrados; nenhuma saída de verificação foi criada;
- Authenticode **`NotSigned`**; o setup OFF conserva SHA-256 `83166f6cb8d056ceb283ec781046cda45c2b77253672e5d3eee053c356e12ea2`.

ON aqui significa código/configuração de cadastro incluídos no setup, **não cadastro executado ou app escolhido como padrão no Windows**. Nenhum setup foi iniciado; não houve install/upgrade/uninstall, mudança de Registry/associações, Computer Use ou publicação. A versão continua privada/experimental, restrita aos perfis locais testados, e exige seus gates de lifecycle, integração nativa e distribuição.

## Evidência e limites da promoção original

- Planner: baseline **20/20**; Red arquitetural por entrada promovida ausente; Green **25/25** produto e **26/26** com legado. Após integrar o leitor final, **26/26** novamente. Exportações ON/OFF não dependem do experimento; modificar somente um helper copiado muda seu hash e o ID, sem modificar a origem.
- Leitor: Red inicial **2 passes/11 falhas**, Green **14/14** produto e **15/15** com legado. Red escalar **13 passes/2 falhas** reproduziu aceitação de schema textual; ambos os modos agora exigem inteiro. Catálogo, aliases, tipos e hashes têm regressões próprias.
- Fontes NSIS: Red **0/5**, Green **5/5** produto e **9/9** com encaminhadores, apenas leitura/hash/exportação. Recibo da toolchain preservado por SHA-256.
- Builder: caracterização **3/3**, cobrindo export OFF, compilador explícito, rejeição antes de execução/saída e inventário compartilhado antes/depois da compilação.

Não houve runtime/instalador novo, execução de NSIS, install/uninstall, Registry, atalhos reais, áudio/UI/CTest global, build C++, benchmark ou Computer Use. A compilação/preprocessamento e o lifecycle da receita promovida ainda precisam de execução própria; os resultados históricos não foram transferidos. Segurança, licenças/notices/SBOM, assinatura, CRT/máquina limpa, declaração de formatos do runtime novo e integração nativa permanecem gates abertos. Esta promoção não conclui M1, M4.6/M4.8 ou release.

# Instalador Windows — receita de produto

**Estado:** setup privado da receita promovida compilado com associações OFF; execução/lifecycle e gates de distribuição ainda pendentes.
**Data:** 2026-09-06.

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

Essa execução comprova compilação da receita direta e consistência dos artefatos, não instalação, atualização, remoção, launch ou playback. Nenhum setup/player/probe foi executado, nenhum Registry/menu/associação foi alterado e nenhum runtime/pacote anterior foi sobrescrito. `InstallerBuild.Tests.ps1` histórico não foi executado: o builder de produto foi chamado diretamente, seguido das verificações acima. Lifecycle, formatos do runtime novo, CRT/máquina limpa, segurança/licenças e validações nativas seguem abertos; não se trata de release aprovada.

## Evidência e limites da promoção original

- Planner: baseline **20/20**; Red arquitetural por entrada promovida ausente; Green **25/25** produto e **26/26** com legado. Após integrar o leitor final, **26/26** novamente. Exportações ON/OFF não dependem do experimento; modificar somente um helper copiado muda seu hash e o ID, sem modificar a origem.
- Leitor: Red inicial **2 passes/11 falhas**, Green **14/14** produto e **15/15** com legado. Red escalar **13 passes/2 falhas** reproduziu aceitação de schema textual; ambos os modos agora exigem inteiro. Catálogo, aliases, tipos e hashes têm regressões próprias.
- Fontes NSIS: Red **0/5**, Green **5/5** produto e **9/9** com encaminhadores, apenas leitura/hash/exportação. Recibo da toolchain preservado por SHA-256.
- Builder: caracterização **3/3**, cobrindo export OFF, compilador explícito, rejeição antes de execução/saída e inventário compartilhado antes/depois da compilação.

Não houve runtime/instalador novo, execução de NSIS, install/uninstall, Registry, atalhos reais, áudio/UI/CTest global, build C++, benchmark ou Computer Use. A compilação/preprocessamento e o lifecycle da receita promovida ainda precisam de execução própria; os resultados históricos não foram transferidos. Segurança, licenças/notices/SBOM, assinatura, CRT/máquina limpa, declaração de formatos do runtime novo e integração nativa permanecem gates abertos. Esta promoção não conclui M1, M4.6/M4.8 ou release.

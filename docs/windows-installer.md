# Instalador Windows — receita de produto

**Estado:** implementação e planejamento promovidos; compilação/lifecycle desta receita e gates de distribuição ainda pendentes.
**Data:** 2026-09-06.

## Fonte única e proveniência

[`packaging/windows`](../packaging/windows/New-RcMusicInstaller.ps1) reúne o builder, o planejador, o leitor de formatos e os quatro templates/helpers NSIS. A geração da configuração e os quatro arquivos NSIS preservam a lógica anterior; os NSIS foram comparados byte a byte à revisão `3f539d6`. LF é fixado por `.gitattributes` para conservar esses bytes em outro checkout.

O produto não importa o experimento. Os caminhos antigos são encaminhadores, e a compatibilidade com a declaração v1 é explícita. O template corrente do harness histórico aponta diretamente para a fonte promovida; os snapshots de versões antigas continuam vindo de seus commits fixos. Os includes dos encaminhadores são ancorados em `__FILEDIR__`, com diretório de busca explícito, conforme a [documentação do pré-processador NSIS](https://nsis.sourceforge.io/Docs/Chapter5.html). A resolução foi inspecionada estaticamente; não houve nova compilação NSIS.

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

**Nenhuma declaração de produto aprovada foi criada neste incremento.** Os testes constroem declarações fictícias exclusivamente em diretórios GUID. A matriz v1 histórica permanece inalterada, vinculada ao seu corpus/runtime. Só o entrypoint antigo ou `-LegacyBaseline` explícito escolhe essa baseline; a entrada padrão recusa v1 antes de consultar o histórico. Não basta trocar o número de schema ou recalcular hashes para aprovar outro runtime.

## Checks sem compilação ou instalação

Na raiz, PowerShell 7 x64:

```powershell
pwsh -NoProfile -File packaging/windows/tests/FormatMatrix.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/WindowsInstallerPlan.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerSources.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerRecipe.Tests.ps1
```

`-IncludeLegacy` nos três primeiros acrescenta compatibilidade histórica. Esses checks não compilam nem executam NSIS. Usam arquivos sintéticos, cópias próprias das fixtures licenciadas e exportações sem `experiments`, a partir de diretório corrente alheio. OFF não precisa de catálogo/corpus/fonte de teste. O preflight do builder recebe um arquivo de texto como compilador e deve rejeitá-lo pelo hash antes de tentar executá-lo ou criar saída.

Esses comandos permanecem separados de `develop.ps1 validate`. Os harnesses nativos históricos de arquivos, associações e lifecycle continuam separados e não foram executados nesta promoção.

## Montagem real, somente quando necessária

Depois de montar/verificar um [runtime local](windows-runtime.md), com fontes commitadas e compilador já provisionado, o comando abaixo compila um setup privado sem executá-lo. Escolha uma saída nova; o exemplo não habilita associações:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/nova-revisao/app -OutputDirectory out/product/rc-music-installer/nova-revisao -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

O caminho do compilador no exemplo é o provisionamento local existente, não dependência de código/fixtures M0. Outro ambiente fornece explicitamente seu caminho verificado. `-FormatMatrix` somente deve apontar para uma declaração revisada para aquele runtime; sem argumento, associações ficam OFF. O diretório de saída deve ser novo sob `out/product` ou `out/m0` para compatibilidade. Falhas podem deixar saída parcial sem receipt aprovado; não há rollback, reutilização ou limpeza automática.

## Evidência e limites

- Planner: baseline **20/20**; Red arquitetural por entrada promovida ausente; Green **25/25** produto e **26/26** com legado. Após integrar o leitor final, **26/26** novamente. Exportações ON/OFF não dependem do experimento; modificar somente um helper copiado muda seu hash e o ID, sem modificar a origem.
- Leitor: Red inicial **2 passes/11 falhas**, Green **14/14** produto e **15/15** com legado. Red escalar **13 passes/2 falhas** reproduziu aceitação de schema textual; ambos os modos agora exigem inteiro. Catálogo, aliases, tipos e hashes têm regressões próprias.
- Fontes NSIS: Red **0/5**, Green **5/5** produto e **9/9** com encaminhadores, apenas leitura/hash/exportação. Recibo da toolchain preservado por SHA-256.
- Builder: caracterização **3/3**, cobrindo export OFF, compilador explícito, rejeição antes de execução/saída e inventário compartilhado antes/depois da compilação.

Não houve runtime/instalador novo, execução de NSIS, install/uninstall, Registry, atalhos reais, áudio/UI/CTest global, build C++, benchmark ou Computer Use. A compilação/preprocessamento e o lifecycle da receita promovida ainda precisam de execução própria; os resultados históricos não foram transferidos. Segurança, licenças/notices/SBOM, assinatura, CRT/máquina limpa, declaração de formatos do runtime novo e integração nativa permanecem gates abertos. Esta promoção não conclui M1, M4.6/M4.8 ou release.

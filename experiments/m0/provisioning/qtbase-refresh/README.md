# Rebuild isolado do Qtbase para M0.2

Esta pasta define uma identidade nova e reproduzível para reconstruir o Qtbase 6.11.2 com o backport FreeType `5a280ec`. Ela não modifica, reetiqueta ou substitui o SDK oficial, o contrato histórico `ac00a565…1952`, o pacote `msvc-63717e5-clean-01` ou qualquer evidência anterior.

Não há instalação global nesta etapa. A receita usa os pré-requisitos já autenticados no host: MSVC 14.44, Windows SDK 10.0.26100.0, CMake 3.31.5 e Ninja 1.12.1. Fontes, headers Vulkan, build, instalação e evidências ficam no `RunRoot` privado sob `q`.

## Identidades do contrato

- `qtbase-refresh-contract.v1.json` é histórico e somente para verificação. Seus 15.475 bytes e SHA-256 `2023dd7621015906d86d5956474e828f4e1ef74bedda52907c5e827aa378c2c6` são imutáveis. A compatibilidade do formato v1 é coberta por fixture; nenhum replay de receipt v1 real é alegado.
- `qtbase-refresh-contract.v2.json` é a receita de produção. Seus 20.464 bytes e SHA-256 `3a58cc3bd599f24d2af77ceb8f797fcbc16c60e749e66c5975043ac953371bbb` são autenticados pelo sidecar canônico adjacente.
- O contrato v2 fixa tamanho e hash do orquestrador, do runner nativo e do launcher. O launcher não aceita parâmetros, overrides de path, callbacks ou hashes fornecidos pelo chamador.

O sidecar colocalizado não substitui a âncora externa: a execução real exige receita commitada e working tree limpa. Um adversário local concorrente com a mesma conta ou privilégios administrativos está fora do threat model; o workspace deve permanecer exclusivo durante o rebuild.

## Receita v2

A entrada fixa combina:

- archive Qtbase da revisão `ef55f427f2c8b410d34f8a7681020a3000cf6866`;
- patch upstream de alvo único `5a280ecde6f324de0d226261036e736e0cb49a71`;
- Vulkan-Headers `37164a5726f7e6113810f9557903a117498421cf`;
- 36 ferramentas, headers e bibliotecas do host autenticados por tamanho e SHA-256.

A preparação produz inventários físicos canônicos UTF-8 sem BOM/LF e ordem ordinal. O contrato fixa estas árvores de entrada:

| Árvore | Arquivos | Bytes | SHA-256 do inventário |
| --- | ---: | ---: | --- |
| Qtbase original | 22.856 | 276.454.784 | `1d994ab02aaa31b0ee3d96143fb3abeef3fc41818968b48f44ac82faf13338a7` |
| Qtbase corrigido | 22.856 | 276.459.640 | `04f8cfd9fc0cbc581be485941d178b91ef0c6f0ffb9f95d1758bc1fb6c5a04d4` |
| Vulkan-Headers | 53 | 12.968.619 | `06d27b1e3d9d14479ec50ea583d9d42d24ad67e86dc7f9ca51c608f18a76d25e` |

O runner executa, nessa ordem, configure, build `RelWithDebInfo` e install. A configuração usa Qt compartilhado, `Debug;RelWithDebInfo`, bibliotecas 3rd-party embutidas, OpenGL dinâmico, Vulkan, DirectWrite/Direct2D, acessibilidade, bandeja, CET e informação de debug. LTCG, PCH, OpenSSL, PostgreSQL e o pipeline SBOM oficial ficam explicitamente fora da receita.

Sucesso requer três comandos e sete relatórios autenticados: summary, cache CMake, vínculo Ninja direto de `Qt6Gui.dll` e `qminimal.dll` a `Qt6BundledFreetype.lib`, além de headers/imports/exports obtidos por `dumpbin`. O orquestrador revalida todas as folhas e árvores físicas, publica atomicamente `qtbase-refresh-manifest.v2.json` e somente então `qtbase-refresh-receipt.v2.json`, ambos sem overwrite.

## Checks e execução

Execute os checks rápidos antes do rebuild:

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qtbase-refresh\tests\QtBaseRefresh.Tests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qtbase-refresh\tests\QtBaseRefreshNative.Tests.ps1
```

Com a receita commitada e o working tree limpo, o launcher de produção inicia um `RunRoot` novo sob o diretório dedicado `q`. O nome codifica todos os 128 bits de um GUID em 26 caracteres base32 minúsculos; o build usa o leaf `b`. Esse layout projeta em 244 caracteres o pior objeto observado neste checkout, abaixo do limite CMake de 250. O runner também procura `CMAKE_OBJECT_PATH_MAX` e `cannot be safely placed under this directory` nos dois logs completos do configure e proíbe o build se qualquer aviso reaparecer:

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qtbase-refresh\Invoke-M0QtBaseRefresh.ps1
```

O install oficial pode criar aliases de ferramentas Qt como hardlinks NTFS. Antes de executar relatórios ou inventariar a árvore, o runner materializa qualquer folha com mais de um link como uma cópia privada de bytes idênticos, verifica tamanho, SHA-256 e contagem física igual a um e remove o scratch `n`. Reparse point, ADS, cópia divergente, substituição bloqueada ou scratch preexistente falham antes do inventário e do receipt; o gate geral de arquivos físicos continua proibindo hardlinks.

O rebuild pode levar horas. O resultado só é aceito após verificar em outro processo o hash externo do receipt, o manifesto e todas as evidências físicas. Falha, timeout, orçamento de path inseguro, drift, reparse point, hardlink, ADS, path fora da raiz, arquivo preexistente ou divergência semântica impedem a publicação final.

## Limites da evidência

O receipt v2 comprova a receita autenticada, execução sequencial com exit zero, configuração observada, árvores, artefatos PE x64 e nomes estruturais de imports/exports. O verificador não carrega DLLs não confiáveis e, isoladamente, não prova carregabilidade em runtime nem identidade funcional completa do Qt. Essas propriedades pertencem aos smokes do novo pacote em processo isolado, original e relocado.

Qtbase corrigido também não é, por si só, o pacote do shell: QML/Quick/Svg e ferramentas suplementares ainda precisam ser combinados com o SDK oficial autenticado sem permitir fallback ou overwrite dos binários base. O delta entre SDK 10.0.26100.0 desta receita e o SDK 10.0.22621.0 historicamente fixado pelo shell deve ser resolvido explicitamente nessa integração.

Esta receita não encerra `CHK-SEC`, M0.2, licenças, notices, redistribuição ou adoção definitiva da stack. Ela também não produz SBOM oficial. Validação desktop nativa, NVDA, alto contraste/DPI reais, bandeja/Explorer, foco e encerramento permanecem pausados até a interação estar disponível.

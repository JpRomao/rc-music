# M0.2 — Regressão e backport isolado do FreeType

**Data:** 2026-08-27
**Estado:** regressão pública Red/Green validada no vendor isolado; QtGui e pacote ainda não corrigidos; `CHK-SEC` e M0.2 permanecem abertos

## Resultado

Foi preparado o harness local [freetype-regression](../../../experiments/m0/security/freetype-regression/README.md), sem Qt runtime, GUI, áudio, instalação ou rede durante os testes. Ele compila o vendor FreeType da revisão Qtbase `ef55f427f2c8b410d34f8a7681020a3000cf6866` e chama somente APIs públicas sobre `testfont_variable.ttf`, uma fonte válida da suíte Qt. Nenhum áudio ou arquivo pessoal foi usado.

A API exige zero para cada coordenada solicitada além do número de eixos. A fixture possui um eixo `wght`; os nove casos combinam estado inicial, default explícito e peso 650 com pedidos de zero, uma e duas coordenadas. Assim, há casos abaixo, exatamente no limite e acima do número de eixos, além de guardas antes/depois do output.

- **Red comportamental:** seis casos dentro do limite passaram; os três pedidos de duas coordenadas falharam com `Excess coordinate is not zero`. CTest **6/9**, exit **8**, 0,21 s.
- **Green:** o mesmo código de teste contra a cópia com o patch upstream `5a280ec` passou **9/9**, exit **0**, 0,23 s.
- **Refactor:** sem enfraquecer a expectativa, nova execução manteve o original em **6/9** e o corrigido em **9/9**.
- **Negativo de entrada:** outra fonte foi recusada na configuração pelo hash fixado, exit **1**, antes de build.

O primeiro ensaio, anterior ao Red acima, falhou **9/9** na checagem de versão: a atribuição Qt dizia 2.14.3, mas headers/runtime dessa fonte dizem 2.14.2. Ele é preservado como correção de setup e **não** é contado como Red do comportamento.

## Fonte e identidade

O arquivo fonte Qtbase foi obtido do codeload oficial no commit exato: 67.782.484 bytes, SHA-256 `12b3a9ba00613180caea4da77a1ae6fc1ca054b8822b316ba3a05fb4414fba56`. Antes de extrair, os 26.850 membros foram aceitos somente sob o prefixo fixado, como arquivo/diretório, sem path absoluto, `..`, barra invertida ou link. A árvore tem **22.856 arquivos**, zero reparse point.

A segunda árvore nasceu como cópia completa: mesma contagem, nenhum arquivo ausente e zero reparse point. A comparação de path/tamanho/timestamp registrou mudança somente em `src/3rdparty/freetype/src/truetype/ttgxvar.c`. O patch oficial preservado tem 1.705 bytes, SHA-256 `fae1c96cd94b9ed5aafc7424f4b8894f2e235b4869a72b3cdbb684dedcea9d1a`; aplicou limpo após `git apply --check` e produziu exatamente nove adições/12 remoções.

| Âncora | SHA-256 |
|---|---|
| `ttgxvar.c` original | `b5ca576f480437e59f84a31ba2b569c7aadd31871f608034f6ea576340b88e10` |
| `ttgxvar.c` corrigido | `9126fee413fabeb2f9f78b60548065dda2a8365ef95da43cb2a0edc55813e346` |
| Header FreeType na fonte | `28519f6bdbda31718a5ac2715ca0d3765b5594cad6ef20d0a817d780d6a4ae79` |
| Header FreeType no SDK oficial | `b117b5a3cf49b857bddba22b861c3e5d2d8b50530866bb3dfb0313782025d09a` |
| `qt_attribution.json` | `628b68bf60eb3bc1616342bc3545b63be2392e78bbebbc4c19f49675dd2ac8a7` |
| Fixture Qt (28.528 bytes) | `8ce9e27075d78bfc284afaa052e133166b5e89bd90498c12effe238c35a8c7a7` |

`qt_attribution.json` declara FreeType **2.14.3**, mas tanto o header dessa fonte quanto o header do SDK oficial definem **2.14.2**. O executável standalone validou seu runtime contra o header. Isso é divergência de inventário confirmada, não permissão para renomear a fonte, inferência de qual tag upstream foi usada ou prova direta da versão reportada dentro da Qt6Gui empacotada.

## Build e vínculo causal

O build Release isolado usou MSVC `19.44.35228.0`, toolset `14.44.35207`, SDK `10.0.22621.0`, CMake `3.31.5` e Ninja `1.12.1`, sem `CL`/`_CL_`/`LINK`/`_LINK_`. Foram **45 comandos**: 44 unidades C do vendor e o teste C++ com `/W4 /WX`. O vendor usa `/W0`, como a receita Qt, e seu gzip interno; não usa Qt Core, PNG ou HarfBuzz. Portanto não reproduz a configuração binária de QtGui.

O deps log do Ninja vincula o `ttgxvar.c` da árvore corrigida à compilação de `truetype.c`. As identidades mudaram em todas as fronteiras relevantes:

| Artefato | Original | Corrigido |
|---|---|---|
| `truetype.c.obj` | `d5fa1e…e85295` | `99388f…d46e4` |
| `m0_freetype.lib` | `710ece…6149` | `ab6e43…ada11` |
| executável da regressão | `eeb983…a93a3` | `2c4e80…11a84` |

O primeiro auditor do deps log procurou barra invertida; Ninja gravou `/`, e o resultado inicial foi falso negativo. A releitura normalizada encontrou exatamente `../qtbase-patched/src/3rdparty/freetype/src/truetype/ttgxvar.c`; não houve rebuild ou reteste entre as leituras. O executável corrigido tem zero dependência `Qt6*.dll`, recusou argumentos inválidos com exit 2 e não deixou processo residual.

Evidência local: `out/m0/ft50811-01/evidence/summary.json`, 8.272 bytes, SHA-256 `9bb5190b98c6629f4f7a4f49a3359b0d39291bd89a9e5dbf2f13278063be74ae`. O summary vincula hashes dos 13 JUnit/logs essenciais e dos seis arquivos do harness, incluindo a repetição final original/corrigido. O log ruidoso de uma tentativa interrompida de `git diff --no-index` não integra a evidência: o Git comparou árvores fora do índice usando regras globais de EOL e inundou a saída com warnings; o check quieto de path/tamanho/timestamp o substituiu.

## Licenças e limites

A revisão independente confirmou o uso privado da fixture pela alternativa GPL-3.0-only de `tests/**` no `REUSE.toml`, sem presumir licença comercial. FreeType usa FTL neste ensaio; BDF, PCF e gzip/zlib mantêm textos próprios. A lista adaptada de unidades C conserva o notice BSD-3-Clause da receita Qt. Isso não decide a licença global do projeto ou autoriza redistribuição.

Este Green prova que o patch corrige o contrato público no vendor compilado isoladamente. Não prova que `Qt6Gui.dll`, plugins, SDK ou pacote estão corrigidos. FreeType é estático em QtGui e também alcança ao menos o plugin `qminimal`; trocar DLL ou recompilar somente o player não basta.

A reconstrução Qt precisa de identidade/configuração separadas. O SDK oficial foi construído com `shared`, Debug+RelWithDebInfo, `/MD`, `/Z7` e sem LTCG. Seus PDBs apontam para Windows SDK `10.0.26100.0` e Vulkan headers `1.2.182.0`; os headers Vulkan não existem no ambiente atual, enquanto o shell usa SDK `10.0.22621.0`. Não configurar por autodetecção e perder Vulkan silenciosamente. O próximo incremento deve fixar esses inputs ou declarar/provar qualquer delta, reconstruir/relinkar QtGui e consumidores diretos necessários, conferir features/ABI/exports/imports, SBOM/notices e então repetir build, CTest, pacote e smokes. O contrato `ac00…1952`, o SDK oficial e todos os pacotes históricos permanecem intactos.

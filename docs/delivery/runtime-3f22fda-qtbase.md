# Auditoria parcial de atribuições Qtbase do runtime 3f22fda

Data da inspeção: **2026-09-06**. Escopo: evidência técnica local para M5.7, não revisão jurídica, SBOM completo ou aprovação de distribuição.

O runtime examinado é `out/product/rc-music-local/rc-music-windows-dev-3f22fda/app`, cujo manifesto externo registra `source_commit=3f22fda7ae58e6ca2a2bf0587c8f0a620ad44cb9`, arquitetura `x64`, `build_testing=false` e distribuição `private-local-development-only`. O recorte abaixo identifica dez grupos prioritários do Qtbase reconstruído, incluindo os subgrupos SLJIT, BDF, PCF e dados PSL. Não representa todos os componentes do Qt ou do runtime.

## Origem e método

A instalação Qtbase vem do run `q/96bvah3m0t0t71m990olv5gpvs`. Sua fonte `source/qtbase-patched` deriva da revisão Qtbase `ef55f427f2c8b410d34f8a7681020a3000cf6866`, com o backport FreeType `5a280ecde6f324de0d226261036e736e0cb49a71`. A [regressão FreeType](../m0/evidence/m0-2-freetype-backport.md) e o [rebuild autenticado](../m0/evidence/m0-2-qtbase-refresh.md) mantêm a evidência histórica e seus próprios limites; não foram reexecutados nesta inspeção.

Foram lidos o manifesto do runtime, o manifesto/evidências do rebuild, o cache, o grafo Ninja, fontes e metadados selecionados. Foram calculados hashes locais dos arquivos indicados. **Ninja não foi executado**: não houve configure, build, teste, execução de DLL/aplicação, download, Registry ou UI. A presença de um diretório de terceiros, isoladamente, não foi considerada prova de incorporação.

Convenções dos caminhos nas tabelas:

- `R` = `q/96bvah3m0t0t71m990olv5gpvs`;
- `V` = `R/source/qtbase-patched/src/3rdparty`;
- os caminhos de atribuições e textos são relativos a `V`, dentro da fonte reconstruída, não ao SDK histórico.

## Vínculo dos binários

Para cada um dos seis PEs abaixo, o SHA-256 físico do payload foi igual ao registrado em `runtime-manifest.json` e ao arquivo correspondente em `R/install`. O manifesto do runtime aponta a origem intermediária no SDK composto `q/f75eb9bea8280844c6d1a2b3f4/sdk`; a igualdade foi conferida diretamente com a instalação reconstruída, sem atribuir ao rebuild o SPDX do SDK oficial.

| PE no payload | SHA-256 comum ao payload, manifesto e instalação reconstruída |
|---|---|
| `Qt6Core.dll` | `3d472b9249aa3a91dabf453c7e84b2f172d2a26ec4266b0919d6bb985438497a` |
| `Qt6Gui.dll` | `2327a7314ab8e2dea6a9f753594e6aa2723dcec7f46763ef030cd1057f550b25` |
| `Qt6Network.dll` | `5b98c10688dd00791d43b518404c3c3378f3996a67350516b5a939ecd4e4982a` |
| `Qt6Widgets.dll` | `b7575bc98c91ad4aa372ffc85ccd08ee2fe7a8ed921d9a86937516a1a4033662` |
| `Qt6OpenGL.dll` | `d780d2404d898bf96171a6b7943fbb68e1ae66c8d58841428c9957b24c5dd8be` |
| `plugins/imageformats/qjpeg.dll` | `aaaa0f615a08cd91340b3573683706f2a8ae997ac296ab9881732e85d534b350` |

O cache confirma as opções `FEATURE_system_freetype`, `system_harfbuzz`, `system_pcre2`, `system_zlib`, `system_png` e `system_jpeg` em `OFF`. O sumário confirma as funcionalidades correspondentes habilitadas. As linhas de link em `R/b/CMakeFiles/impl-RelWithDebInfo.ninja` distinguem a configuração efetiva:

| Linha | Alvo | Dependências relevantes |
|---|---|---|
| 6620 | Core | `Qt6BundledZLIB.lib`, `Qt6BundledPcre2.lib` e objetos double-conversion |
| 19489 | Network | Core e `lookup_string_in_fixed_set.c.obj` |
| 28417 | Gui | `Qt6BundledHarfbuzz.lib`, `Qt6BundledFreetype.lib`, `Qt6BundledLibpng.lib`, objetos MD4C e D3D12 Memory Allocator |
| 32879 | OpenGL | Gui e Core por bibliotecas de importação |
| 35686 | Widgets | Gui e Core por bibliotecas de importação |
| 46501 | qjpeg | `Qt6BundledLibjpeg.lib`, Gui e Core |

Widgets/OpenGL não possuem uma cópia estática adicional dos seis componentes prioritários nas linhas examinadas. As bibliotecas estáticas e fontes selecionadas integram o build; isso não comprova a retenção de toda função após otimização/link nem seu alcance por entradas da aplicação.

## Dez grupos prioritários

As versões e expressões de licença abaixo reproduzem os campos locais de atribuição; não são uma nova classificação jurídica. Onde há versão de header divergente, ambas permanecem explícitas.

| Grupo | Versão literal | Licença declarada | Evidência específica |
|---|---|---|---|
| FreeType | atribuição `2.14.3`; header `2.14.2`, com backport acima | `FTL OR GPL-2.0-only`; BDF `MIT`; PCF `MIT AND MIT-open-group` | Gui liga BundledFreetype; BDF/PCF têm compilações próprias no Ninja |
| PCRE2 e SLJIT | `10.47` em ambos os registros | principal `LicenseRef-BSD-3-Clause-with-PCRE2-Binary-Like-Packages-Exception`; SLJIT `BSD-2-Clause` | Core liga BundledPcre2; `src/pcre2_jit_compile.c` compilado inclui `deps/sljit/sljit_src/sljitLir.c`; `SUPPORT_JIT` é habilitado para x64 e não há `PCRE2_DISABLE_JIT` nessa compilação |
| zlib | atribuição `1.3.2`; header `1.3.2 (Qt)` | `Zlib` | Core liga BundledZLIB |
| libpng | `1.6.58` | `Libpng AND libpng-2.0` | Gui liga BundledLibpng; o texto genérico de `QtUsage` sobre qpng não substitui esse grafo real |
| libjpeg-turbo | `3.2.0` | `IJG AND BSD-3-Clause` | plugin qjpeg liga BundledLibjpeg |
| HarfBuzz-NG | `14.3.0` | `MIT` | Gui liga BundledHarfbuzz |
| double-conversion | `3.4.0` | `BSD-3-Clause` | oito objetos próprios aparecem na linha de link de Core |
| libpsl e dados PSL | código `664f3dc85259ec65e30248a61fa1c45b7b0e4c3f`; dados `2026-05-14_08-35-31_UTC` | código `BSD-3-Clause`; dados `MPL-2.0` | objeto de lookup no Network; `src/network/kernel/qtldurl.cpp:19` inclui `psl_data.cpp`, com banco interno habilitado |
| MD4C | `0.5.3` | `MIT` | objeto `md4c.c.obj` no Gui |
| D3D12 Memory Allocator | `f128d39b7a95b4235bd228d231646278dc6c24b2` | `MIT` | objeto `D3D12MemAlloc.cpp.obj` no Gui |

O FreeType foi compilado com `FT_CONFIG_OPTION_SYSTEM_ZLIB`: seu `src/gzip/ftgzip.c` seleciona a zlib externa ao diretório FreeType, fornecida pelo QtCore neste build. Portanto, o registro adicional `freetype-zlib` não prova incorporação de outra cópia de zlib. Seu campo `Version` é ausente e não foi preenchido por inferência. `freetype/ZLIB-LICENSE.txt` foi conferido como metadado local, mas fica **fora da seleção de 16 textos** abaixo.

### Atribuições autenticadas

| Caminho relativo a `V` | SHA-256 |
|---|---|
| `freetype/qt_attribution.json` | `628b68bf60eb3bc1616342bc3545b63be2392e78bbebbc4c19f49675dd2ac8a7` |
| `pcre2/qt_attribution.json` | `e009f73100ef4aed400257a095cdb25ee31980c4a290f1aca52d147cb3067abc` |
| `zlib/qt_attribution.json` | `0346805e9c4048316dbc7c58d560a20c12482ea0218de264282212e9619b6d64` |
| `libpng/qt_attribution.json` | `a43ff8e146c949f7cd14e00bd681f4957e27def06ecafec55ccc7231c451d3fa` |
| `libjpeg/qt_attribution.json` | `ed7cb7abdd9124f664acd26cf552ba821c8cf236c4cd34a6737f202215bd41bc` |
| `harfbuzz-ng/qt_attribution.json` | `15fb72e213bc590be310de8b453792a0b0a42cfd302b97387abf49252b9dd63f` |
| `double-conversion/qt_attribution.json` | `4d9c7505a7c12e4e6b57bbee63af926c861abb5515cdf802b8f3491e321146fe` |
| `libpsl/qt_attribution.json` | `ae227b5f2cf40511fe1a84d4c752f4005917b7eeb44e7c4495b1403c27cc3647` |
| `md4c/qt_attribution.json` | `7b2bc7be22e4be2a2984119ce3d0bfc210ddd0d8ef6193d66e8479d4c22af4ba` |
| `D3D12MemoryAllocator/qt_attribution.json` | `182d7e5ed90dc592b01a50635c85f4039ca6f25f88ec4700b08faf6f07fda09e` |

### Textos locais selecionados

Estes 16 arquivos são a seleção técnica para materialização de notices deste recorte. `freetype/LICENSE.txt` contém os textos completos FTL e GPLv2; isso não registra escolha de licença para distribuição. O texto `pcre2/LICENCE.md` usa `BSD-3-Clause WITH PCRE2-exception`, enquanto o JSON conserva o `LicenseRef` acima; a exceção não foi interpretada como dispensa automática de obrigações do produto. Copyrights e textos não devem ser resumidos ou normalizados durante a cópia.

As [cópias versionadas no rascunho de notices](runtime-3f22fda-notices.md#textos-preservados) já preservam os bytes dos dezesseis textos abaixo. Ficam separadas do runtime e não mudam o alcance parcial desta auditoria.

| Caminho relativo a `V` | SHA-256 |
|---|---|
| `freetype/LICENSE.txt` | `bf543cdda11864477b8d2ca2aa9afc2aaa8f9133461b4d962ed6ec73646256a1` |
| `freetype/BDF-LICENSE.txt` | `e4e792f29e66750f2e410fd7b50a567652c180f6ee15e1a4dfa0bb5cde524537` |
| `freetype/PCF-LICENSE.txt` | `6938597bfdeca0db191a8f9ede52eaf2482c2c71a0094c084747a1235f9958fe` |
| `pcre2/LICENCE.md` | `197d8a73ffee0d6b09adba2f9c677b5f5aede24edf89258a68e48248d010d811` |
| `pcre2/LICENSE-SLJIT` | `5f216505c0f6ea3273caec89e766eef93cdeb7bbb0c429f9360116d7c938feeb` |
| `zlib/LICENSE` | `d7e3139972874dc8abdbbfce27ec1e6f942b28279dd7d27453630a8b4610e080` |
| `libpng/LICENSE` | `bdb0a645ea18c60507d0368379b1ac5474b92255fcc2d115e07486a7672ba526` |
| `libjpeg/LICENSE` | `ba6bceebcba0fdd35488477c2cca8c4632ce82c74dbfbc87d886ce6fc4433579` |
| `libjpeg/ijg-license.txt` | `1cb448f5e2e6d3aca60943f4eee1a04675628dc0266a074a42bc1560ebc28d61` |
| `libjpeg/COPYRIGHT.txt` | `e5b349c6a98d997a8d0f5fcfe5cc900b1f76bbc32743674c52a70981498748c0` |
| `harfbuzz-ng/COPYING` | `ba8f810f2455c2f08e2d56bb49b72f37fcf68f1f4fade38977cfd7372050ad64` |
| `double-conversion/LICENSE` | `4af93c12062c58058378de2397dc1c92bbff9ddfb1d583a01c84127557ce97ca` |
| `libpsl/PSL-LICENSE.txt` | `3f3d9e0024b1921b067d6f7f88deb4a60cbe7a78e76c64e3f1d7fc3b779b9d04` |
| `libpsl/src/LICENSE.chromium` | `1d7f52747a9169751cdf2641a8299c0098e9ecdf9429296ffd55bdb14c9ed5b3` |
| `md4c/LICENSE.md` | `d30937367d5413e7eaa218b1640b8946ff76fd34d97152f6979fd96169d5d0fc` |
| `D3D12MemoryAllocator/LICENSE.txt` | `1b3941c52b26d42c510afbdf205a8b96a6d64f5a7b8ebff0287979fd91dfd52f` |

## Conferências e lacunas

Foram conferidos **28/28 arquivos** contra `R/evidence/inventories/qtbase-patched.tsv`: dez atribuições, 17 textos/copyright — incluindo o texto FreeType-zlib excluído da seleção — e `ttgxvar.c`. O próprio TSV, o cache e o Ninja continuam com os hashes registrados em `R/evidence/qtbase-refresh-manifest.v2.json`.

| Evidência | SHA-256 |
|---|---|
| `R/evidence/inventories/qtbase-patched.tsv` | `04f8cfd9fc0cbc581be485941d178b91ef0c6f0ffb9f95d1758bc1fb6c5a04d4` |
| `R/b/CMakeCache.txt` e cópia `R/evidence/reports/cmake-cache.txt` | `19e5d91d4acaca5792434e6e5ae781f25af9638cc73f7e5ff6e3a4f44619dcb8` |
| `R/b/CMakeFiles/impl-RelWithDebInfo.ninja` | `12b96ef622e8b4bf81c0555598aa09c3953a6d67180af38bc95af6e5b6f0cff6` |
| `V/freetype/src/truetype/ttgxvar.c`, com backport | `9126fee413fabeb2f9f78b60548065dda2a8365ef95da43cb2a0edc55813e346` |
| `V/freetype/ZLIB-LICENSE.txt`, conferido e excluído da seleção | `2b1f5d615382a151af3eb71462ad17d89e2efee8fa7664058d35d3d9237ebb19` |

Lacunas preservadas:

- Esta tabela não é um inventário completo de Qtbase: inclusões em fontes do Core alcançam SHA/MD/blake2/siphash/tinycbor/easing; Gui contém ainda caminhos de emoji, Vulkan Memory Allocator e perfil ICC. A lista de objetos de terceiros na linha de link, sozinha, não cobre código incluído por `#include`, headers ou recursos.
- Declarative/Quick, Svg, Multimedia/FFmpeg, TagLib, D3D e demais arquivos do runtime precisam de suas próprias atribuições e vínculos. Bibliotecas do Windows/CRT listadas no link não foram convertidas automaticamente em componentes redistribuídos.
- A divergência de versão FreeType não foi corrigida documentalmente por inferência. O patch específico e a igualdade de hashes não fecham advisories, segurança de fontes/decoders ou revisão de obrigações.
- O rebuild desabilitou a geração de SBOM; o SPDX histórico do SDK oficial **não representa automaticamente os binários reconstruídos**. Os vínculos deste documento são a evidência local descrita, não um substituto para esse gate.
- Materializar textos é um passo de notices, não aprovação de licença pública, cumprimento integral de LGPL/GPL, oferta de fontes/relink, distribuição, M5.7 ou release. Nenhum runtime ou setup foi gerado ou instalado nesta auditoria.

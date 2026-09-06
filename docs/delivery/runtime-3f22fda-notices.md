# RC Music — terceiros do runtime local 3f22fda

**Estado:** inventário técnico e rascunho de notices, ainda incompletos para distribuição.
**Data:** 2026-09-06.

Este documento acompanha o [inventário de componentes](runtime-3f22fda-components.json) do runtime privado Windows x64. Os textos em [licenses](licenses) são cópias de documentos de terceiros, não uma licença para o código do RC Music. A licença pública do projeto não foi escolhida; este trabalho não autoriza distribuição nem conclui M5.7, NFR-REL-006 ou CHK-SEC.

## Objeto e correspondências

Objeto: `out/product/rc-music-local/rc-music-windows-dev-3f22fda`, produzido de `3f22fda7ae58e6ca2a2bf0587c8f0a620ad44cb9`. SHA-256 de `runtime-manifest.json`: `c3f43d26601fdafd5d8ebe7e779f2c7f34827361681b729cecb182d83f1b742c`. Nesta revisão, tamanhos e SHA-256 de **554/554 arquivos, 96.076.407 bytes**, foram conferidos contra o manifesto; não havia arquivos extras. Os [runtime e setups existentes](../windows-installer.md) não foram modificados.

| Grupo técnico | Arquivos no payload | Identificação e limite |
|---|---:|---|
| RC Music | 1 | Executável próprio; sem licença pública definida |
| Qtbase reconstruído | 9 | Qt 6.11.2, fonte corrigida e configuração específicas; sem SPDX gerado para o rebuild |
| Qt Declarative | 530 | Qt 6.11.2; inclui QML e plugins realmente copiados, não só o estilo usado na tela |
| Qt SVG | 3 | Qt 6.11.2 e suas atribuições transitivas |
| Qt Multimedia | 2 | Qt 6.11.2; plugin FFmpeg e biblioteca |
| FFmpeg | 5 | DLLs; versão declarada divergente entre SPDX e configuração/diagnóstico |
| TagLib | 1 | `tag.dll` 2.3.1, build compartilhado com utfcpp incorporado |
| Microsoft D3D compiler | 1 | `d3dcompiler_47.dll`; tratamento separado do Qt |
| Configuração própria | 2 | `qt.conf` e aviso de desenvolvimento privado |

O inventário registra a origem técnica dos arquivos e, separadamente, as relações declaradas nos SPDX locais. Nome/caminho correspondente não é prova de identidade binária: os hashes brutos divergentes permanecem explícitos, sem presumir que assinatura explique a diferença. Componentes de build/teste não são acrescentados ao runtime só por aparecerem no SDK. Pré-requisitos do Windows/CRT ficam fora da contagem de arquivos empacotados.

## Textos preservados

As cópias abaixo mantêm todos os bytes dos documentos locais selecionados, inclusive cabeçalhos, créditos e finais de linha. A regra específica em `.gitattributes` impede conversão automática nesses arquivos. Somente os textos FreeType principal, zlib e TagLib MPL preservam linhas vazias finais do original com exceção pontual a `blank-at-eof`; os demais checks de whitespace permanecem ativos. A seleção é parcial: não substitui atribuições individuais, avisos de alterações, fontes correspondentes ou revisão das condições aplicáveis ao artefato final.

Fontes relativas à raiz do repositório:

- **Q:** `q/96bvah3m0t0t71m990olv5gpvs/source/qtbase-patched/LICENSES/`;
- **T:** `out/m0/taglib-2.3.1-msvc144-probe-20260829-01/extract/taglib-2.3.1/`.

| Cópia versionada | Fonte | Bytes | SHA-256 da fonte e da cópia |
|---|---|---:|---|
| [LGPLv3](licenses/LGPL-3.0-only.txt) | Q + `LGPL-3.0-only.txt` | 7651 | `da7eabb7bafdf7d3ae5e9f223aa5bdc1eece45ac569dc21b3b037520b4464768` |
| [GPLv3](licenses/GPL-3.0-only.txt) | Q + `GPL-3.0-only.txt` | 35147 | `8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903` |
| [TagLib LGPL](licenses/TagLib-COPYING.LGPL.txt) | T + `COPYING.LGPL` | 26530 | `dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551` |
| [TagLib MPL](licenses/TagLib-COPYING.MPL.txt) | T + `COPYING.MPL` | 25755 | `53692a2ed6c6a2c6ec9b32dd0b820dfae91e0a1fcdf625ca9ed0bdf8705fcc4f` |
| [Autores TagLib](licenses/TagLib-AUTHORS.txt) | T + `AUTHORS` | 889 | `047780f6bded3712fc24edc77b9187bfa49ea3826ed090933cb6342458eace85` |
| [utfcpp](licenses/utfcpp-LICENSE.txt) | T + `3rdparty/utfcpp/LICENSE` | 1338 | `c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566` |

A [auditoria dos componentes incorporados ao Qtbase](runtime-3f22fda-qtbase.md) identifica um recorte adicional pelo grafo e pelos binários do rebuild. As dezesseis cópias abaixo vêm de `q/96bvah3m0t0t71m990olv5gpvs/source/qtbase-patched/src/3rdparty/`, usando o caminho da primeira coluna. O hash é o mesmo na origem e na cópia. O texto da zlib interna do FreeType não foi incluído, pois o build seleciona a zlib do QtCore; isso não elimina o texto próprio da zlib copiado abaixo.

| Texto relativo à fonte Qtbase | Bytes | SHA-256 |
|---|---:|---|
| [freetype/LICENSE.txt](licenses/Qtbase-freetype-LICENSE.txt) | 26234 | `bf543cdda11864477b8d2ca2aa9afc2aaa8f9133461b4d962ed6ec73646256a1` |
| [freetype/BDF-LICENSE.txt](licenses/Qtbase-freetype-BDF-LICENSE.txt) | 2305 | `e4e792f29e66750f2e410fd7b50a567652c180f6ee15e1a4dfa0bb5cde524537` |
| [freetype/PCF-LICENSE.txt](licenses/Qtbase-freetype-PCF-LICENSE.txt) | 2218 | `6938597bfdeca0db191a8f9ede52eaf2482c2c71a0094c084747a1235f9958fe` |
| [pcre2/LICENCE.md](licenses/Qtbase-pcre2-LICENCE.txt) | 4011 | `197d8a73ffee0d6b09adba2f9c677b5f5aede24edf89258a68e48248d010d811` |
| [pcre2/LICENSE-SLJIT](licenses/Qtbase-pcre2-LICENSE-SLJIT.txt) | 1444 | `5f216505c0f6ea3273caec89e766eef93cdeb7bbb0c429f9360116d7c938feeb` |
| [zlib/LICENSE](licenses/Qtbase-zlib-LICENSE.txt) | 1465 | `d7e3139972874dc8abdbbfce27ec1e6f942b28279dd7d27453630a8b4610e080` |
| [libpng/LICENSE](licenses/Qtbase-libpng-LICENSE.txt) | 5345 | `bdb0a645ea18c60507d0368379b1ac5474b92255fcc2d115e07486a7672ba526` |
| [libjpeg/LICENSE](licenses/Qtbase-libjpeg-LICENSE.txt) | 6077 | `ba6bceebcba0fdd35488477c2cca8c4632ce82c74dbfbc87d886ce6fc4433579` |
| [libjpeg/ijg-license.txt](licenses/Qtbase-libjpeg-ijg-license.txt) | 1815 | `1cb448f5e2e6d3aca60943f4eee1a04675628dc0266a074a42bc1560ebc28d61` |
| [libjpeg/COPYRIGHT.txt](licenses/Qtbase-libjpeg-COPYRIGHT.txt) | 606 | `e5b349c6a98d997a8d0f5fcfe5cc900b1f76bbc32743674c52a70981498748c0` |
| [harfbuzz-ng/COPYING](licenses/Qtbase-harfbuzz-ng-COPYING.txt) | 1971 | `ba8f810f2455c2f08e2d56bb49b72f37fcf68f1f4fade38977cfd7372050ad64` |
| [double-conversion/LICENSE](licenses/Qtbase-double-conversion-LICENSE.txt) | 1527 | `4af93c12062c58058378de2397dc1c92bbff9ddfb1d583a01c84127557ce97ca` |
| [libpsl/PSL-LICENSE.txt](licenses/Qtbase-libpsl-PSL-LICENSE.txt) | 16726 | `3f3d9e0024b1921b067d6f7f88deb4a60cbe7a78e76c64e3f1d7fc3b779b9d04` |
| [libpsl/src/LICENSE.chromium](licenses/Qtbase-libpsl-LICENSE.chromium.txt) | 1664 | `1d7f52747a9169751cdf2641a8299c0098e9ecdf9429296ffd55bdb14c9ed5b3` |
| [md4c/LICENSE.md](licenses/Qtbase-md4c-LICENSE.txt) | 1096 | `d30937367d5413e7eaa218b1640b8946ff76fd34d97152f6979fd96169d5d0fc` |
| [D3D12MemoryAllocator/LICENSE.txt](licenses/Qtbase-D3D12MemoryAllocator-LICENSE.txt) | 1099 | `1b3941c52b26d42c510afbdf205a8b96a6d64f5a7b8ebff0287979fd91dfd52f` |

TagLib apresenta LGPL 2.1 ou MPL **1.1**, não MPL 2.0. O build usa utfcpp 4.1.0, cujo header registra `Copyright 2006 Nemanja Trifunovic`; o texto preservado é Boost Software License 1.0. O arquivo AUTHORS conserva os créditos publicados por TagLib; não é uma licença substituta.

Qt oferece alternativas nos metadados do fornecedor. Copiar LGPLv3/GPLv3 não seleciona uma licença comercial nem altera a licença de outros componentes. Os copyrights e expressões de licença dos pacotes alcançados ficam preservados literalmente no inventário, inclusive `AND`, `OR`, `unknown` e `NOASSERTION`.

## Divergências e partes ainda faltantes

- **Qtbase:** o runtime usa o [rebuild com backport FreeType](../m0/evidence/m0-2-qtbase-refresh.md), não os binários do SPDX oficial antigo. É necessário completar o inventário dos componentes incorporados e a fonte/configuração correspondente; a identificação histórica de [19 grupos/62 recursos](../m0/evidence/m0-2-third-party-attributions.md) serve de referência, não de aprovação transferida. A divergência FreeType entre atribuição 2.14.3 e headers 2.14.2, com patch específico, não deve ser apagada.
- **Qt Declarative/SVG:** conferir atribuições e textos dos módulos efetivamente embarcados, inclusive terceiros embutidos nos plugins e arquivos QML. Um SPDX do SDK não é, sozinho, um conjunto de notices do pacote.
- **FFmpeg:** o SPDX de Qt Multimedia declara 7.1.3, enquanto `config_qtmultimedia.opt` menciona `FFmpeg-n7.1.5` e o diagnóstico do [smoke atual](../windows-runtime.md#smoke-automatizado-do-runtime) informa 7.1.5. A expressão composta de licenças e as atribuições do SPDX não podem ser reduzidas ao texto LGPL do diagnóstico. Falta reconciliar a origem/configuração das cinco DLLs e obter os textos/fontes correspondentes.
- **Terceiros de Multimedia:** o fornecedor declara Signalsmith Stretch 1.0.0, MIT, `Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.`, e dr_libs 0.14.5, `Unlicense OR MIT-0`. Isso exige revisão das relações e dos textos originais; um MIT genérico com placeholder não substitui o aviso original. Textos próprios desses componentes não foram localizados nesta revisão local.
- **Microsoft:** separar os termos e a origem de `d3dcompiler_47.dll` dos pré-requisitos CRT externos. Não há redistribuível CRT dentro deste payload nem prova de máquina limpa.
- **Entrega final:** consolidar as demais atribuições, notices e fontes, resolver as divergências, revisar segurança e definir licença própria antes de distribuição. Os textos desta pasta não foram incorporados aos instaladores existentes; uma futura entrega completa exige montagem e validação próprias.

## Validação e continuidade

Este incremento contém dados e cópias documentais, não comportamento do app; não exige Red artificial. A verificação proporcional é: vínculo com o manifesto exato, cobertura dos paths, coerência das relações/campos transcritos, comparação byte a byte dos textos, links/rastreabilidade e revisão do diff. Não foram executados app, setup, benchmark ou nova compilação; não houve download de dependências, alteração de associações ou aceitação de risco/licença.

Conferências executadas: 554 paths únicos e somas dos nove componentes; 12 hashes de fontes do inventário; 22/22 textos iguais às fontes; dez atribuições Qtbase e seis pares de PEs iguais à instalação reconstruída. A revisão independente recompôs os 54 vínculos por caminho, 63 pacotes e 16 referências externas dos três SPDX, preservando os checksums divergentes e Signalsmith fora da closure comprovada. Check documental aprovado com 61 Markdown/83 requisitos/17 checks/76 etapas; rastreabilidade válida para os 83 IDs, sem alterar seus estados. Os documentos originais não foram normalizados para satisfazer estilo; as três exceções de fim de arquivo estão restritas aos textos identificados acima.

Próximo trabalho: completar as atribuições e fontes faltantes a partir destas lacunas concretas. Não repetir o smoke/empacotamento já verde para preencher uma ausência de licença ou de correspondência de fornecedor. Este documento é complementar ao [processo de release](release-process.md), não substitui seus gates.

# M0.2 — Triagem de segurança do pacote Qt

**Data da consulta:** 2026-08-27
**Estado:** triagem parcial com achados e pendências; `CHK-SEC`/`SEC-014` não encerrados

## Escopo e resultado

Esta revisão atende à triagem de advisories exigida pelo [gate de dependências](../../quality/quality-gates.md) e pelo controle `SEC-014` do [threat model](../../security/threat-model.md). Prepara `NFR-REL-006` e `NFR-STACK-006`, sem declarar esses requisitos atendidos. Não é decisão de stack, scanner completo, certificação de segurança ou autorização de redistribuição.

O objeto é o pacote privado `out/m0/packages/qt-shell/tray-200b983-clean-01`, gerado em `200b9838dc9e86427e083771ef71bc9f1ee49f4b`, com `source_paths_clean=true` e `recipe_dirty=false`. Seu manifesto autenticado tem SHA-256 `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99`: 1.378 arquivos, 82.388.347 bytes e 69 PEs x64. A [seção 9 do registro M0](../README.md) mantém a receita, a regeneração, o bloqueio histórico por RTSS e os smokes posteriores aprovados; estes não resolvem os achados desta triagem.

Resultados que exigem acompanhamento:

1. FreeType 2.14.3 e PCRE2 10.47 têm avisos aplicáveis às fontes vendorizadas examinadas; os trechos corretivos não estão nessas fontes. A [análise posterior de símbolos](m0-2-qt-symbol-applicability.md) confirmou as funções FreeType investigadas no PE e encontrou forte evidência de ausência do caminho de serialização PCRE2 nessa DLL específica, sem demonstração de exploração do shell ou aprovação global.
2. O toolset MSVC 14.43 usado pelo shell do pacote histórico está fora de suporte. O runtime mais novo do host não resolve o suporte do compilador nem a ressalva de linker 19.43 com Qt compilado por 19.44. A instalação e o build posteriores com 14.44 não alteram esse pacote.
3. D3DCompiler, servicing do CRT e obrigações completas de licença/notices continuam sem fechamento. O [complemento de atribuições](m0-2-third-party-attributions.md) identifica fontes e textos de licença, mas preserva versões upstream desconhecidas e pendências de distribuição.

Durante a triagem, não houve atualização de dependência, instalação, modificação da toolchain, execução de PoC, execução do player, retomada de desktop ou acesso aos áudios pessoais. As consultas externas enviaram somente identificadores públicos de componentes, versões e revisões; o SBOM e os paths locais não foram enviados a um scanner externo.

## Identidade e limites do inventário

Os SPDX agregados incluem módulos e ferramentas que não foram empacotados. A inclusão abaixo usa as relações entre pacote, dependência e arquivo, confrontadas com o manifesto e a configuração da toolchain; não apenas uma busca pelo nome no SDK. Atribuição a um módulo não prova uso de todos os seus caminhos pelo aplicativo.

| SPDX | SHA-256 | Revisão Qt declarada |
|---|---|---|
| `qtbase-6.11.2.spdx` | `5acdc05bb79c66e31da70c8de4e9d37ba2f1d94199a21f8d430b888a76fe88c0` | `ef55f427f2c8b410d34f8a7681020a3000cf6866` |
| `qtdeclarative-6.11.2.spdx` | `0753ffa69fa629b4f52ebfe41cb5ea3a142160a1cd4d34aae9ff3f8a052c76e0` | `4e3399c26ec57246c08de019cfcbda8d23604cfa` |
| `qtsvg-6.11.2.spdx` | `c95254b9349aa562ec1f64b8a214a7f4f532cffccf586cedd9f2055a749377f7` | `17ca512f903f935282ebeca496aac5d11ba4199a` |

Os hashes SHA-256 do manifesto identificam os PEs finais. A conferência dos `FileChecksum` SHA-1 dos SPDX passou para 67/67 arquivos Qt: 15 de qtbase, 49 de qtdeclarative e três de qtsvg. A reconstrução somente em memória removeu a CertificateTable terminal e zerou CheckSum/CertificateDirectory; todos os certificados começavam no fim da última seção, sem padding adicional a remover. O SHA-1 integral desses bytes correspondeu ao SPDX. Quatro controles negativos no Core divergiram corretamente; os SHA-256 dos originais permaneceram iguais ao manifesto e à toolchain na releitura final. Nenhuma DLL foi escrita.

Esse método segue os campos do [formato PE](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format), mas não é cálculo do digest Authenticode nem nova validação de assinatura. Demonstra o vínculo desses PEs com os SPDX, não recompilação reproduzível ou ausência de vulnerabilidades. `PackageVerificationCode` não é SHA-256 de tarball upstream; essas entradas não fornecem tal checksum.

### Bibliotecas e licenças declaradas no inventário

Nas entradas seguintes, `PackageLicenseDeclared` é `NOASSERTION`. A última coluna transcreve `PackageLicenseConcluded` do fornecedor; não é uma conclusão jurídica desta revisão.

| Componente | Versão | Consumidor indicado | LicenseConcluded |
|---|---|---|---|
| PCRE2 / SLJIT | 10.47 | Qt6Core | `LicenseRef-BSD-3-Clause-with-PCRE2-Binary-Like-Packages-Exception` / `BSD-2-Clause` |
| zlib | 1.3.2 | Qt6Core; atribuições em Gui/Network | `Zlib` |
| TinyCBOR | 7.0 | Qt6Core | `MIT` |
| double-conversion | 3.4.0 | Qt6Core | `BSD-3-Clause` |
| libpng | 1.6.58 | Qt6Gui | `Libpng AND libpng-2.0` |
| libjpeg-turbo | 3.2.0, variantes 8/12/16 bits | qjpeg | `IJG AND BSD-3-Clause` |
| FreeType | 2.14.3 | Qt6Gui | `FTL OR GPL-2.0-only` |
| HarfBuzz | 14.3.0 | Qt6Gui | `MIT` |
| md4c | 0.5.3 | Qt6Gui | `MIT` |
| emoji-segmenter | 0.4.0 | Qt6Gui | `Apache-2.0` |
| VulkanMemoryAllocator | 3.2.1 | Qt6Gui | `MIT` |
| Yoga | 2.0.1 | Qt6QuickLayouts | `MIT` |

FreeType também atribui licenças separadas a gzip/zlib (`Zlib`), BDF (`MIT`) e PCF (`MIT AND MIT-open-group`). A tabela não substitui essas atribuições ou os textos completos.

A configuração confirma variantes bundled de PCRE2, double-conversion, FreeType, HarfBuzz, PNG e JPEG. `vulkan`, `emojisegmenter`, `textmarkdownreader`, `qml_jit` e `publicsuffix_qt` estão habilitados. Não foram tratados como bibliotecas incorporadas: SQLite/QtSql, DBus, Qt Test/Catch2, ferramentas QML, ferramentas de build, OpenSSL, Brotli, Zstd ou ICU externo. O payload contém Schannel, não `qopensslbackend`/`libssl`/`libcrypto`; `winsdkicu` é integração do Windows, não cópia de ICU. A presença de QtNetwork e de JIT QML não é prova de egress ou de sua ausência.

## Achados que não podem receber Green genérico

### FreeType — CVE-2026-50811

O [registro CNA](https://cveawg.mitre.org/api/cve/CVE-2026-50811), publicado em 2026-07-07, descreve leitura fora dos limites em FreeType 2.14.3, corrigida no [commit upstream `5a280ec`](https://github.com/freetype/freetype/commit/5a280ecde6f324de0d226261036e736e0cb49a71). Os campos estruturados de produto/versão são `n/a`; a análise usou a descrição e a correção, não um resultado vazio de matching por versão.

Na [fonte Qt fixada pelo SPDX](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/3rdparty/freetype/src/truetype/ttgxvar.c#L3449), `TT_Get_Var_Design` ainda percorre entradas além do número de eixos ao preencher coordenadas extras; não há a correção que zera essas posições e limita a cópia. SHA-256 do texto consultado: `b5ca576f480437e59f84a31ba2b569c7aadd31871f608034f6ea576340b88e10`.

O [callsite Qt examinado](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/gui/text/freetype/qfontengine_ft.cpp#L366) usa `var->num_axis` e um buffer desse tamanho. Isso é evidência de um chamador específico, não prova de ausência de outros caminhos ou de exploração impossível. Não foi reproduzido um gatilho no shell. A [análise PE/PDB posterior](m0-2-qt-symbol-applicability.md) confirmou `FT_Get_Var_Design_Coordinates` e `TT_Get_Var_Design` no binário, sem comprovar correção ou alcance do caminho afetado.

**Disposição:** fonte vendorizada afetada e funções presentes, alcance no produto não demonstrado. Resolver correção/proveniência do binário ou uma análise de aplicabilidade suficiente antes de encerrar essa pendência; não atualizar DLL isolada ou alterar o contrato congelado silenciosamente.

### PCRE2 — GHSA-q7rw-r7qq-2hx6

O [advisory upstream](https://github.com/PCRE2Project/pcre2/security/advisories/GHSA-q7rw-r7qq-2hx6), publicado em 2025-10-25, inclui 10.47 e descreve dois bytes não inicializados potencialmente expostos pela serialização de uma expressão compilada. A correção é o [commit `edc111a`](https://github.com/PCRE2Project/pcre2/commit/edc111a6831591f68b5355a08cc9df8be8f35304); o aviso indica 10.48 como versão corrigida, sem que esta triagem afirme a disponibilidade dessa release.

A [fonte vendorizada](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/3rdparty/pcre2/src/pcre2_compile_class.c#L1805) mantém a inicialização condicionada a Debug/Valgrind. O serializador copia o bloco sem corrigir esse padding. O [target Qt](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/3rdparty/pcre2/CMakeLists.txt#L37) inclui o serializador numa biblioteca estática de 16 bits e símbolos ocultos. Isso não prova que o objeto foi extraído para o DLL final; o exemplo público usa 8 bits e não foi executado.

O [stream operator de QRegularExpression](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/corelib/text/qregularexpression.cpp#L2756) grava padrão/opções, não a representação serializada PCRE2. Uso de regex não demonstra exposição por essa API, e ausência de chamada no código próprio não encerra a análise do payload.

**Disposição atualizada:** fonte afetada e correção ausente no trecho examinado. A [análise posterior do PDB correspondente ao PE](m0-2-qt-symbol-applicability.md) encontrou 24 compilands PCRE2 e as funções de compilação/matching, mas não `pcre2_serialize.c` ou símbolos `pcre2_serialize*`. É forte evidência de que o caminho específico desse advisory não foi incorporado à Qt6Core examinada, delimitada pelos limites de enumeração documentados. Não classifica PCRE2 10.47 como corrigido nem fecha `CHK-SEC`; reavaliar em novo payload/configuração.

### Compilador e pré-requisitos Microsoft

A [tabela Microsoft](https://learn.microsoft.com/en-us/cpp/overview/compiler-versions?view=msvc-170) identifica MSVC 14.43/`_MSC_VER=1943` como EOL. O shell do pacote histórico usa 19.43.34808.0. A [política de ciclo de suporte](https://devblogs.microsoft.com/cppblog/new-release-cadence-and-support-lifecycle-for-msvc-build-tools/) não transforma essa versão antiga em uma das linhas novas suportadas. Trata-se de risco de suporte confirmado, não de CVE ou exploração comprovada.

No VS 2022 inspecionado na triagem havia toolsets 14.16, 14.29, 14.34, 14.39 e 14.43, sem 14.44. Na triagem inicial nenhum instalador foi acionado. A [primeira tentativa posterior autorizada](m0-2-msvc-refresh.md) foi cancelada na confirmação de administrador, antes de instalar. Após nova autorização, a [instalação MSVC 14.44](m0-2-msvc-installation.md) terminou com sucesso em uma nova instância Build Tools; o build Release com 19.44.35228 passou 65/65 testes e gerou receipt autenticado, sem alterar o contrato histórico. Ainda falta integrar esse refresh à receita e gerar/validar novo pacote. A ressalva de suporte e linker antigo permanece no pacote histórico objeto desta triagem; o novo build não fecha as demais pendências de segurança/licença.

| Componente externo ou adicional | Observação | Disposição |
|---|---|---|
| D3DCompiler 6.3.9600.16384, copiado da toolchain Qt | A [KB4019990](https://support.microsoft.com/en-us/topic/update-for-the-d3dcompiler-47-dll-component-on-windows-server-2012-windows-7-and-windows-server-2008-r2-769c6690-ed30-4dee-8bf8-dfa30e2f8088) registra outra revisão; não define uma faixa de CVE aplicável a esta cópia | indeterminado; versão menor não é prova de vulnerabilidade nem de licença para redistribuir |
| Cinco componentes CRT 14.51.36247.0 de System32 | Hashes/versões conferem, mas a consulta [MSRC de agosto](https://api.msrc.microsoft.com/cvrf/v3.0/cvrf/2026-Aug) não forneceu associação inequívoca do servicing desses arquivos | indeterminado; compatibilidade numérica não é certificado de atualização contra CVEs |

## Avisos Qt e demais bibliotecas consultados

“Fora da faixa” refere-se somente ao aviso identificado. “Correção declarada” depende da informação do fornecedor e não substitui auditoria binária. Um canal sem avisos não prova ausência de vulnerabilidades.

### Qt 6.11.2

| Aviso | Resultado limitado |
|---|---|
| [CVE-2026-6210 — Qt SVG](https://www.qt.io/blog/security-advisory-type-confusion-and-heap-buffer-overflow-vulnerability-in-qt-svg-marker-handling) | corrigido em 6.8.8/6.11.1; 6.11.2 está fora das faixas publicadas |
| [CVE-2025-10728/10729 — Qt SVG](https://www.qt.io/blog/security-advisory-uncontrolled-recursion-and-use-after-free-vulnerabilities-in-qt-svg-module-impact-qt) | corrigidos em 6.8.5/6.9.3; fora das faixas publicadas |
| [CVE-2025-14576 — VectorImage](https://www.qt.io/blog/security-advisory-qml-code-injection-in-vectorimage) | corrigido em 6.8.7/6.10.2; fora das faixas publicadas |
| [CVE-2025-12385 — Quick Text](https://www.qt.io/blog/security-advisory-improper-validation-of-img-tag-size-in-text-component-parser) | corrigido em 6.5.11/6.8.6/6.10.1; fora das faixas publicadas |
| [CVE-2025-14575 — Network/OpenSSL](https://www.qt.io/blog/security-advisory-untrusted-search-path-vulnerability-in-openssl) | corrigido em 6.5.10/6.8.4/6.9.2; fora das faixas, e o aviso trata Unix/Linux |
| CVE-2026-16762/19248/8168 | as [notas oficiais de 6.11.2](https://code.qt.io/cgit/qt/qtreleasenotes.git/about/qt/6.11.2/release-note.md) declaram correção; registros CVE públicos consultados retornaram 404, sem confirmação independente das faixas completas |
| [CVE-2026-15037 — QtXml/QDom](https://www.qt.io/blog/security-advisory-cve-2026-15037-xml-injection) | a faixa anterior a 6.12 inclui 6.11.2; não aplicável a este payload por ausência de QtXml/QDom, não por versão corrigida; reavaliar se o módulo entrar |

### Bibliotecas upstream

| Componente | Triagem |
|---|---|
| PCRE2 10.47 | Além do achado aberto acima, [GHSA-43gg-c3xx-7w7g](https://github.com/PCRE2Project/pcre2/security/advisories/GHSA-43gg-c3xx-7w7g) é corrigido em 10.47 e trata JIT AArch64; [CVE-2025-58050](https://github.com/PCRE2Project/pcre2/security/advisories/GHSA-c2gv-xgf5-5cc2) foi corrigido em 10.46. |
| zlib 1.3.2 | [CVE-2026-27171](https://raw.githubusercontent.com/CVEProject/cvelistV5/main/cves/2026/27xxx/CVE-2026-27171.json) afeta versões anteriores a 1.3.2; a [release](https://zlib.net/) declarada já contém a correção. |
| TinyCBOR 7.0 | Fora das faixas `<0.6.1` de CVE-2025-24302/20025 no [Intel-SA-01326](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-01326.html). A [release 7.0](https://github.com/intel/tinycbor/releases/tag/v7.0) removeu deliberadamente o prefixo `0.`; não é erro de transcrição do SPDX. |
| libpng 1.6.58 | Foram consultados os 12 advisories públicos do [projeto](https://github.com/pnggroup/libpng/security/advisories), sem próxima página. As faixas/correções de 11 deles ficam antes de 1.6.58; o caso APNG exige distinção de componente, abaixo. |
| HarfBuzz 14.3.0 | Foram consultados os cinco advisories públicos do [projeto](https://github.com/harfbuzz/harfbuzz/security/advisories). A versão fica fora das faixas publicadas de GHSA-q4gc-p4hh-3765, GHSA-xvjr-f2r9-c7ww, GHSA-p965-5rr7-9mhq e GHSA-qmp9-xqm5-jh6m. GHSA-f6q6-4vjq-m6j7 é o relato do fuzzer, sem faixa de produto preenchida; esse executável não está no pacote. |
| libjpeg-turbo 3.2.0 | O [canal oficial](https://github.com/libjpeg-turbo/libjpeg-turbo/security/advisories) retornou zero avisos publicados; não equivale a zero vulnerabilidades. As [notas 3.2.0](https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.2.0) incluem hardening de APIs e reparo de jpegtran; os utilitários não são o plugin qjpeg empacotado. |
| double-conversion 3.4.0 | Nenhum aviso publicado localizado no [canal oficial](https://github.com/google/double-conversion/security/advisories); ausência de vulnerabilidades indeterminada. A [release](https://github.com/google/double-conversion/releases/tag/v3.4.0) inclui inicialização preventiva, não classificada aqui como correção de CVE. |
| md4c 0.5.3 | [CVE-2021-30027](https://raw.githubusercontent.com/CVEProject/cvelistV5/main/cves/2021/30xxx/CVE-2021-30027.json) é anterior à correção 0.4.8 registrada no [changelog](https://github.com/mity/md4c/blob/release-0.5.3/CHANGELOG.md). O [PR330](https://github.com/mity/md4c/pull/330), posterior, trata o utilitário md2html ausente do pacote. A issue338 foi analisada por configuração, conforme abaixo. |
| Yoga 2.0.1 | Nenhum aviso publicado localizado no [canal oficial](https://github.com/react/yoga/security/advisories); ausência de vulnerabilidades indeterminada. A [release](https://github.com/react/yoga/releases/tag/v2.0.1) trata exports JavaScript, não demonstra correção de segurança do núcleo C++ vendorizado. |

No libpng, [CVE-2026-40930](https://github.com/pnggroup/libpng/security/advisories/GHSA-c4v6-gxrq-6g2x) distingue o patch externo libpng-apng e o trunk 1.8.0 do upstream 1.6.x sem APNG. A [fonte Qt fixada](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/3rdparty/libpng/png.h#L750) trata esses chunks como desconhecidos e o `pnglibconf.h` examinado não habilita APNG. A classificação é por componente/configuração, não apenas por comparar 1.6.58 com 1.8.0. Os outros IDs consultados são CVE-2026-34757/33636/33416/25646/22801/22695 e CVE-2025-66293/65018/64720/64506/64505; faixas e links individuais estão no extrato de evidência.

O aviso HarfBuzz CVE-2026-22693 tem divergência entre a faixa estruturada `<12.3.0` e a descrição de 12.3.0. A [fonte Qt examinada](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/3rdparty/harfbuzz-ng/src/hb-ot-cmap-table.hh#L1701) já testa falha de alocação antes de construir o cache; não se inferiu segurança global do módulo a partir disso. GHSA-q4gc-p4hh-3765, publicado em 2026-08-18, foi recuperado pela API oficial apesar de a página HTML retornar 404 na consulta; a API indica correção em 14.0.0.

No md4c, a [correção `5add6a3`](https://github.com/mity/md4c/commit/5add6a3b2fa30b7fdea0203927b1d1ef4e228b60) da [issue338](https://github.com/mity/md4c/issues/338) multiplica três comprimentos de cópia/comparação por `sizeof(CHAR)` na variante UTF-16. O [vendor Qt](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/3rdparty/md4c/md4c.c#L1302) ainda não contém esse patch. Entretanto, o [target QtGui](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/gui/CMakeLists.txt#L763) define `MD4C_USE_UTF8`, e o [importador](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/gui/text/qtextmarkdownimporter.cpp#L198) usa `toUtf8()` antes de `md_parse()`. Na configuração oficial examinada, `CHAR` é `char` e seu tamanho é um byte: a condição UTF-16 desse defeito não se aplica. Isso não equivale a incorporar o patch, auditar o binário inteiro ou aprovar genericamente md4c.

### Outros trechos e dados identificados

As APIs públicas dos seis repositórios abaixo retornaram HTTP 200, zero advisories publicados e nenhuma página adicional na data da consulta. A triagem incluiu os canais oficiais e, quando disponíveis, notas/revisões; não transforma um catálogo vazio em ausência de CVEs.

| Componente | Resultado limitado |
|---|---|
| emoji-segmenter 0.4.0 | Nenhum aviso localizado no [canal oficial](https://github.com/google/emoji-segmenter/security) ou nas [notas consultadas](https://raw.githubusercontent.com/google/emoji-segmenter/0.4.0/NEWS); o projeto aponta o canal de reporte Google. |
| VulkanMemoryAllocator 3.2.1 | Nenhum aviso localizado no [canal oficial](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/security) e nas notas; não havia política `SECURITY.md` detectada. |
| D3D12MemoryAllocator `f128d39b7a95b4235bd228d231646278dc6c24b2` | [Revisão pública](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator/commit/f128d39b7a95b4235bd228d231646278dc6c24b2) de 2023-01-09, não reclassificada como release atual; nenhum aviso localizado no [canal oficial](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator/security) e nas notas. |
| MiniEngine/mipmap `0aa79bad78992da0b6a8279ddb9002c1753cb849` | Nenhum aviso localizado para o trecho no [canal oficial](https://github.com/microsoft/DirectX-Graphics-Samples/security), que encaminha reportes ao MSRC; não é conclusão sobre todos os componentes DirectX. |
| Código derivado de libpsl `664f3dc85259ec65e30248a61fa1c45b7b0e4c3f` | O [catálogo](https://github.com/rockdaboot/libpsl/security) vazio não revelou o aviso relacionado CVE-2026-8924 encontrado em outro canal; aplicabilidade limitada abaixo. |
| PSL de 2026-05-14 | A [política](https://raw.githubusercontent.com/publicsuffix/list/master/SECURITY.md) não trata a correção das entradas de domínio como advisory; adições/remoções seguem PRs. Não houve validação de toda a lista. |

O [advisory CVE-2026-8924 do curl](https://curl.se/docs/CVE-2026-8924.html), publicado em 2026-06-24, descreve supercookies, afeta curl 7.46.0–8.20.0 e indica correção em 8.21.0. O NEWS de libpsl 0.23.0 remete ao caso, mas seu [patch `98d5261`](https://github.com/rockdaboot/libpsl/commit/98d5261796ea878c0eaae1acbfbf2b417b60d630) modifica `src/psl.c` e um teste. A atribuição QtNetwork examinada incorpora `lookup_string_in_fixed_set.c` e coordenação própria em [qtldurl.cpp](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/network/kernel/qtldurl.cpp), não `psl.c` nem libcurl. Classificação: aviso relacionado encontrado, sem correspondência direta demonstrada no subconjunto incorporado; não aplicar automaticamente a faixa de curl ao Qt nem declarar segurança global. Não houve teste dinâmico de cookies.

## Atribuições e pendências restantes

As consultas delimitadas acima não esgotam as demais atribuições de código/dados ou canais de divulgação. A origem das entradas está inventariada; nenhuma recebeu status genérico “sem vulnerabilidades”.

MASM/JavaScriptCore, XSVG, Wintab, sombras Angular Material e várias atribuições de hash/rasterização/dados têm versão upstream `unknown`; o [complemento de atribuições](m0-2-third-party-attributions.md) agora as vincula às revisões Qt fixadas, preservando essa incerteza para matching de advisories. O complemento cobre 19 grupos, 62 recursos oficiais autenticados e as sete definições de cinco `LicenseRef`, com controles negativos. A versão `36` de UCD foi preservada e distinguida da declaração Unicode 17.0 na fonte; não é nova prova binária. Fontes correspondentes completas, notices aplicáveis e cumprimento das licenças continuam pendentes. O uso de dados derivados de Tika não é inclusão automática da biblioteca Java inteira.

Os 67 PEs Qt são 6.11.2.0; os dois restantes são o executável próprio e D3DCompiler. Os avisos do Qt não substituem a revisão das bibliotecas embutidas. Assinatura válida e hashes corretos demonstram identidade no escopo verificado, não ausência de bugs ou aprovação de licença.

## Evidência, checks e retomada

Os extratos locais ficam em `evidence/security-20260827/` do stage identificado. São projeções de metadados públicos e localizações de fonte, sem PoCs ou dados pessoais:

| Artefato | SHA-256 |
|---|---|
| `pnggroup-libpng-advisories.json` | `f686d3c52ea864648e60d42df76dd1eb4dcba0474d865b5aac77cf869b01c263` |
| `harfbuzz-harfbuzz-advisories.json` | `05616eebac6be7829810ec476c2b8ece4455c4aed1264472b72ca46c9aa86872` |
| `libjpeg-turbo-libjpeg-turbo-advisories.json` | `0a6f0810e46f43bea581e392e9d6e40f6684d4c60ec3eee73fade1d2ba523066` |
| `CVE-2026-50811-cna.json` | `9e360f4065036e396057268b9a098e6c9a14ddf7e4625be99c79ffd63f615111` |
| `pinned-qt-source-review.json` | `a667b54cebdd3f06283f9a8e513876ff87e79a5e2e755522d268152e1f4e9f3f` |
| `pe-spdx-presign-check.json` | `5d8763081820ce2f4f403110c60c8c2cc57a78a01e5a86e736cdfe953bac91fd` |

As três consultas de advisories via API retornaram HTTP 200 e nenhuma próxima página. Isso fecha somente aquelas listas públicas na data indicada, não todos os registros de segurança existentes. Os extratos preservam URL, data, identificadores e hash do texto da resposta; as fontes linkadas continuam sendo a referência para revisões posteriores.

Esta é uma mudança documental, sem comportamento novo: TDD não se aplica. A validação documental passou com 31 Markdown, 83 requisitos, 17 checks e 76 etapas; o manifesto autenticado e os seis hashes de evidência acima foram reconferidos, e a revisão do diff não apresentou achados. As suítes C++/QML já verdes não foram repetidas como se fossem um scanner de vulnerabilidades. O commit que introduz este documento é a evidência de `CHK-CLOSE` desta revisão, não o encerramento de M0.2.

Os complementos de atribuições e símbolos têm evidências e limites próprios nos documentos linkados; não reescrevem os extratos históricos ou o pacote. Seus checks documentais e commits fecham apenas essas análises adicionais.

Para retomar: integrar à receita o refresh MSVC já instalado/validado, produzir novo pacote e resolver as demais pendências de segurança com evidência e decisões proporcionais. A autorização posterior de desktop permitiu uma [tentativa nativa inconclusiva](m0-2-native-attempt-20260827.md); a repetição depende de captura/interação consistentes. O smoke offscreen original/relocado passou no pacote histórico, sem RTSS ou relaxamento da whitelist, conforme o registro M0; esse resultado não substitui `CHK-SEC`. Redistribuição, M0.3 e fechamento de M0.2 não foram liberados por esta triagem.

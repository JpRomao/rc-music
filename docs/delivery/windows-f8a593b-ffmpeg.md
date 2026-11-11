# FFmpeg nos instaladores Windows de f8a593b

## Origem dos binários e das fontes

A leitura direta dos dois setups `f8a593b` confirmou por SHA-256 que as cinco DLLs do FFmpeg são idênticas às dos runtimes, do estágio do Qt e do arquivo oficial Qt Multimedia 6.11.2 para MSVC 2022 x64. O SHA-1 publicado pelo Qt coincide com o do arquivo local. As cinco DLLs são parte da instalação básica de ambas as edições. [Arquivo do Qt](../m0/evidence/m0-6-qt-audio-first-slices.md#dependências-isoladas) · [Inventários dos setups](windows-f8a593b-sbom.md)

| DLL | Versão do arquivo PE e dos headers da fonte | SHA-256 |
| --- | --- | --- |
| `avcodec-61.dll` | `61.19.101` | `ed5b2789171ba8bf603b3cdb55c9ad3e711873c9ccdf1e6a89958bc24dc2807b` |
| `avformat-61.dll` | `61.7.103` | `4140360861d785a68f39927481f42cd61a48a77eaeabadfa291e118bfc01f796` |
| `avutil-59.dll` | `59.39.100` | `530fb1c691e27341d058059070cfa399b7ae5fbc827d0e4928e6d022d6676df2` |
| `swresample-5.dll` | `5.3.100` | `d9b6978b3edec2a6a3e7c5dd55acca2f0a9e198fd4cc81682e3f64973d8b2a15` |
| `swscale-8.dll` | `8.3.100` | `3f5a8f18f683e0c52cb8b8f0695f84b97d5c2e5d45d944f2a15a40e45be79a9a` |

As cinco DLLs informam `ProductVersion` `7.1.5` e a mesma configuração MSVC de bibliotecas compartilhadas. A configuração embutida coincide com a receita Coin do Qt, que fixa a tag `n7.1.5` e compila seu arquivo fonte diretamente. O tarball conferido corresponde ao commit `3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587`. `config_qtmultimedia.opt` aponta para a saída dessa receita. A atribuição do Qt Multimedia `v6.11.2` ainda declarava `7.1.3` e foi atualizada no commit `884fc4a`. O SPDX antigo permanece registrado como dado do fornecedor. [Receita FFmpeg para Windows](https://github.com/qt/qt5/blob/v6.11.2/coin/provisioning/common/windows/install-ffmpeg.ps1) · [Opções comuns](https://github.com/qt/qt5/blob/v6.11.2/coin/provisioning/common/shared/ffmpeg_config_options.txt) · [Atribuição em 6.11.2](https://github.com/qt/qtmultimedia/blob/v6.11.2/src/3rdparty/ffmpeg/qt_attribution.json) · [Correção da atribuição](https://github.com/qt/qtmultimedia/commit/884fc4a59bb96b6dc18a0f17ccbb5bd650db2176)

As opções do FFmpeg habilitam zlib e apontam para a saída x64 da receita Qt para zlib **1.3.1**. Essa receita ajusta `zconf.h` e `win32/Makefile.msc` para MSVC. A inspeção confirmou os marcadores de `inflate` e `deflate` 1.3.1 em `avcodec-61.dll`, e de `inflate` 1.3.1 em `avformat-61.dll`. Os imports dos PEs não incluem uma DLL zlib. A evidência vincula a cópia estática a esses dois arquivos. [Receita zlib do Qt](https://github.com/qt/qt5/blob/v6.11.2/coin/provisioning/common/windows/zlib.ps1) · [Makefile MSVC do zlib](https://github.com/madler/zlib/blob/v1.3.1/win32/Makefile.msc)

| Arquivo fonte fixado na receita | SHA-1 do Qt | SHA-256 conferido | Textos na fonte |
| --- | --- | --- | --- |
| [FFmpeg n7.1.5](https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n7.1.5) | `2322a4cef2d607c7f0048953662ec5e27c587ae8` | `e3963a50831c985933e1a625ed566ec4c7adb5c012c34fa9f84438e1d61bdacc` | `COPYING.LGPLv2.1`, `LICENSE.md`, `CREDITS` |
| [zlib 1.3.1](https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz) | `f535367b1a11e2f9ac3bec723fb007fbc0d189e5` | `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` | `LICENSE` |

As DLLs registram LGPL 2.1 ou posterior e as opções observadas excluem `--enable-gpl`, `--enable-version3` e `--enable-nonfree`. A atribuição do Qt inclui termos adicionais e referencia código de terceiros como IJG e Boost. A composição dos notices e a conferência das obrigações de distribuição continuam em M5.7. [Atribuições do Qt para FFmpeg 7.1.5](https://doc.qt.io/qt-6.11/qtmultimedia-attribution-ffmpeg.html)

## Avisos aplicáveis à revisão de M5.7

A inspeção estática dos PEs confirmou os registros dos decodificadores e demuxers abaixo. O filtro local do RC Music verifica o sufixo antes de passar a URL ao Qt. O backend FFmpeg do Qt identifica o formato pelo conteúdo e analisa os streams ao abrir o arquivo. O AAX e o VobSub têm funções de detecção por conteúdo, por isso a lista de extensões e a ausência de vídeo na interface são insuficientes para dispensar esses caminhos. [Admissão de arquivo local](../../src/domain/LocalAudioFilePolicy.cpp) · [Backend do RC Music](../../src/adapters/QtMultimediaPlaybackBackend.cpp) · [Abertura no Qt](https://github.com/qt/qtmultimedia/blob/v6.11.2/src/plugins/multimedia/ffmpeg/playbackengine/qffmpegmediadataholder.cpp#L260-L310) · [Detecção AAX](https://github.com/FFmpeg/FFmpeg/blob/n7.1.5/libavformat/aaxdec.c#L58-L69) · [Detecção VobSub](https://github.com/FFmpeg/FFmpeg/blob/n7.1.5/libavformat/mpeg.c#L687-L744)

| Aviso | Evidência no candidato | Pendência |
| --- | --- | --- |
| [CVE-2026-64835](https://www.vulncheck.com/advisories/ffmpeg-out-of-bounds-memory-access-in-adx-audio-decoder), High | `adpcm_adx`, `adx` e `aax` estão registrados. O callback ADX mantém o estado de canais anterior no caminho de novos dados do stream. | Incluir e validar a [correção ADX](https://github.com/FFmpeg/FFmpeg/commit/1836ef96846937a6cc2443698a693104f5c0b21e) na substituição do runtime. |
| [CVE-2026-64830](https://www.vulncheck.com/advisories/ffmpeg-heap-buffer-overflow-via-vobsub-subtitle-demuxer), High | `vobsub` está registrado. O parser cria streams sem o limite corrigido e usa a contagem para indexar a fila de legendas. O demuxer procura um `.sub` irmão. | Incluir e validar a [correção VobSub](https://github.com/FFmpeg/FFmpeg/commit/dbd495f066a85ba96b17433f4306582aa37c3951) na substituição do runtime. |
| [TOB-ZLIB-004](https://zlib.net/), `inflateSync` | `avcodec-61.dll` inclui `flashsv` e o marcador da recuperação zlib. O código do decoder chama `inflateSync` nesse caminho. | Confirmar a aplicabilidade e validar a [correção zlib](https://github.com/madler/zlib/commit/8179479c6274c0041189882b8746a33e2376eaf0) junto da substituição. [Decoder Flash Screen Video](https://github.com/FFmpeg/FFmpeg/blob/n7.1.5/libavcodec/flashsv.c#L210-L235) |

Os três patches upstream passaram em `git apply --check` nas fontes das tags `n7.1.5` e `v1.3.1`. A validação foi restrita à aplicação dos patches. A receita publicada para Qt 6.11.3 também fixa `n7.1.5`. A substituição ainda exige DLLs Windows x64 com as ABIs acima e verificação dos formatos nas duas edições. [Receita Qt 6.11.3](https://github.com/qt/qt5/blob/v6.11.3/coin/provisioning/common/windows/install-ffmpeg.ps1) · [Compatibilidade do FFmpeg no Qt](https://doc.qt.io/qt-6/qtmultimedia-building-ffmpeg.html)

A auditoria leu os arquivos sem executá-los. Os scripts, recibos JSON e arquivos fonte autenticados estão em `out/product/test-runs/windows-ffmpeg-f8a593b-20261111/`. A análise de alcance usou a implementação do RC Music e as fontes upstream. A exploração no player e a cobertura dos demais avisos permanecem sem verificação. Os candidatos atuais seguem privados e M5.7 fica em andamento.

## Build Windows x64 com os três patches

O build partiu das duas fontes autenticadas acima e aplicou os patches ADX, VobSub e `inflateSync` antes da compilação. Usou MSVC 19.44.35228, Windows SDK 10.0.22621.0, GNU Make 4.4.1 e NASM 3.02. O FFmpeg preservou as cinco ABIs, os módulos compartilhados e as opções da receita Qt. O zlib foi compilado com CRT estático (`/MT`) para eliminar o conflito de CRT detectado na primeira ligação. A receita fixa `REVISION=7.1.5` e `EXTRA_VERSION=rcmusic1`, pois a compilação dentro da árvore do RC Music havia incorporado o commit do player como versão. Os cinco PEs finais declaram `7.1.5-rcmusic1`. O recibo `out/product/fffix/build-receipt.json` fixa as fontes, os patches, as ferramentas, a configuração e o SHA-256 de cada DLL. [Receita de referência do Qt](https://github.com/qt/qt5/blob/v6.11.2/coin/provisioning/common/windows/install-ffmpeg.ps1)

Os checks de regressão usaram uma transição legal estéreo → mono no ADX, três entradas com um ID repetido no VobSub e uma retomada `inflateSync` com alocador de teste. O ADX passou de rejeição para 32 amostras mono. O VobSub passou de três streams para dois. O zlib corrigido produziu `ab` e solicitou a janela de 32.768 bytes, que na versão original era de 1 byte. Os PEs finais preservaram os exports nomeados e resolveram 882 símbolos em 16 relações de importação, incluindo os plugins Qt das duas edições. Os relatórios `patch-regression-*.json` estão na pasta de evidências acima.

## Validação dos runtimes corrigidos

Cada cópia isolada de `f8a593b` substituiu as cinco DLLs e vinculou seus hashes ao recibo do build. A conferência cobriu 603 arquivos na Lite e 606 na Completa. Cada edição passou nos 17 cenários de áudio, com 19 casos Qt aprovados e zero skips. O Windows confirmou que o probe de cada edição carregou as cinco DLLs corrigidas e o plugin Qt a partir da cópia isolada. O diagnóstico informou `7.1.5-rcmusic1`. Os manifestos, resultados e declarações dos sete formatos estão em `out/product/fffix/qa/v2/`.

## Setups privados com FFmpeg corrigido

Os setups em `out/product/fffix/setups/` foram montados com os runtimes corrigidos e as declarações de formatos validadas para os novos hashes. A leitura direta dos dois instaladores conferiu os hashes do player e das cinco DLLs. Os recibos confirmam as cinco combinações de opções, as associações e que apenas as cinco DLLs diferem no inventário dos arquivos empacotados. Os dois SBOMs em `out/product/fffix/sbom/` registram os patches e o recibo do build. A verificação do instalador foi estática. O resultado está em `out/product/fffix/setup-verification.json`.

| Edição | Bytes do setup | SHA-256 |
| --- | ---: | --- |
| Lite | 90.019.051 | `3cde05503a5bfe4b52005559421e23cb1f83e2c42c4329403b83e54ae7d882e4` |
| Completa experimental | 90.090.411 | `f56f85baec26279d033b4cdab9e7f4d223ae329850b6a0b7e9658c164dd70605` |

## Uso no fluxo Windows

O SDK preparado pelo projeto usa as cinco DLLs `7.1.5-rcmusic1` e o plugin Qt que passou na qualificação. O runner Windows e `New-RcMusicLocalRuntime.ps1` conferem os seis arquivos com os hashes versionados antes do build. A montagem repete a conferência sobre os arquivos selecionados e grava a versão e o SHA-256 do recibo de qualificação no manifesto do runtime. Os binários originais do SDK do fornecedor permanecem disponíveis para as evidências históricas. [Comandos de preparação](../development-commands.md#qt-multimedia-com-ffmpeg-corrigido) · [Hashes dos arquivos qualificados](../../packaging/windows/ffmpeg-runtime.v1.json)

Na validação de `3c85479`, a comparação dos 560 arquivos do SDK preparado confirmou somente as cinco substituições. A receita padrão passou 25 testes, o planejamento de runtime passou 150 e o runner passou 37. O build Release em worktree limpa gerou um runtime Lite de 554 arquivos, com manifesto SHA-256 `325059224a523920669655ac0a7a339a6a8eb8652d38057680e19120cf960b45`. O smoke offscreen com os probes existentes carregou `7.1.5-rcmusic1` e passou 17 cenários de áudio, 19 casos Qt e zero skips. Logs e runtime em `out/product/r9/out/product/`. Recibo do smoke em `out/product/test-runs/rc-music-runtime-smoke/c90f5da96bf04ca790df195f6e3dac94/`.

Os hashes fixam a build qualificada existente. Uma nova compilação precisa de qualificação e de novos hashes. Os PEs atuais incluem caminhos absolutos na configuração embutida. O preparo do SDK e a montagem padrão são para desenvolvimento privado. O empacotador de instaladores ainda aceita manifestos históricos escolhidos explicitamente.

## Avisos incluídos no runtime

A receita padrão das edições Lite e Completa experimental inclui 13 arquivos em `notices/ffmpeg/` e `notices/qt-multimedia/`. O SHA-256 de cada aviso aparece no inventário da receita e no inventário do payload. Os instaladores gerados a partir desse runtime recebem os avisos como arquivos obrigatórios. Os setups históricos acima conservam os manifestos e conteúdos originais.

- FFmpeg `7.1.5-rcmusic1` leva o texto LGPL 2.1, os créditos e os avisos do FFmpeg, IJG, Boost.Math e zlib 1.3.1. O aviso registra as fontes autenticadas, os patches ADX/VobSub/`inflateSync` e os ajustes do build Windows. [Aviso do FFmpeg](../../packaging/windows/notices/ffmpeg/NOTICE.txt)
- Qt Multimedia 6.11.2 leva os textos de Signalsmith Stretch e Signalsmith DSP, presentes no conversor de áudio do plugin FFmpeg, e os de dr_wav e TLSF, compilados em `Qt6Multimedia.dll`. O aviso identifica as duas DLLs auditadas e os caminhos dos textos no arquivo-fonte oficial. [Aviso do Qt Multimedia](../../packaging/windows/notices/qt-multimedia/NOTICE.txt) · [Conversor de áudio](https://github.com/qt/qtmultimedia/blob/v6.11.2/src/plugins/multimedia/ffmpeg/playbackengine/qffmpegaudioframeconverter.cpp#L74-L145) · [Decoder WAV](https://github.com/qt/qtmultimedia/blob/v6.11.2/src/multimedia/audio/qsamplecache_p.cpp#L101-L123)

M5.7 ainda exige o inventário das demais licenças incorporadas ao FFmpeg, os termos aplicáveis ao próprio Qt, as fontes correspondentes e a revisão de distribuição e segurança. A troca das DLLs ou da configuração precisa de nova conferência dos avisos. [Checklist de distribuição do FFmpeg](https://ffmpeg.org/legal.html)

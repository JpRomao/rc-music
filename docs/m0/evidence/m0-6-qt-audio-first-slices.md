# M0.6 — primeiros slices Qt Multimedia e TagLib

**Data:** 2026-08-29

**Estado:** M0.6 em andamento; load/probe WAV e extração síncrona inicial verdes, sem suporte de formato publicado

## Escopo

Este incremento prioriza comportamento funcional do candidato C++ e mantém `package-v2`, A/B e benchmarks suspensos. Ele não integra áudio ao shell/QML e não decide a stack.

Dois recortes independentes foram implementados:

- `QtMultimediaPlaybackBackend` carrega por path o WAV sintético autenticado, publica `Loading` antes de `Ready` e expõe duração/seekability observadas;
- `TagLibMetadataExtractor` lê propriedades WAV, preserva tags ausentes, aplica fallback de título pelo nome, identifica RIFF/WAVE por conteúdo sob extensão `.mp3` e devolve erro tipado para vazio/truncado.

O nome `MetadataExtractor` é intencional. A chamada TagLib é síncrona e será envolvida por uma porta `MetadataReader` assíncrona, limitada, cancelável e identificada por geração antes de integrar a UI. A extensão concreta de load do adapter Qt também não representa ainda o contrato completo de `PlaybackBackend`.

## Dependências isoladas

O [archive oficial Qt Multimedia 6.11.2 para MSVC 2022 x64](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64/qt.qt6.6112.addons.qtmultimedia.win64_msvc2022_64/6.11.2-0-202608131017qtmultimedia-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z) foi baixado e autenticado em `out/m0/qt-multimedia-archive-probe-20260829-01`, sem alterar o SDK Qt existente:

| Campo | Valor |
|---|---|
| Bytes | `43.228.011` |
| SHA-1 | `b074034da581a838892a54051a2469c0a2c38e21` |
| SHA-256 | `24d73c45fefe5e260aa9a99eea1aea48adcad54f93bddaa3ec5b0574b55f57c7` |
| Inventário extraído | 560 arquivos, 61 diretórios, 485.224.087 bytes, zero reparse point |

O prefixo contém `Qt6Multimedia.dll`, os plugins `ffmpegmediaplugin`/`windowsmediaplugin` e as DLLs `avcodec-61`, `avformat-61`, `avutil-59`, `swresample-5` e `swscale-8`. As DLLs não registram `--enable-gpl` ou `--enable-nonfree` e declaram LGPL 2.1-or-later.

Há uma divergência que permanece aberta: o SBOM do archive cita FFmpeg 7.1.3, mas `config_qtmultimedia.opt` e `av_version_info()` apontam 7.1.5. O archive também não traz os textos de licença/notice. Por isso o runtime não é autenticado aqui como FFmpeg 7.1.3 e não há autorização de redistribuição.

[TagLib 2.3.1](https://github.com/taglib/taglib/releases/tag/v2.3.1) foi baixado do asset oficial, autenticado e compilado dinamicamente em `out/m0/taglib-2.3.1-msvc144-probe-20260829-01`:

| Campo | Valor |
|---|---|
| Source bytes | `1.638.729` |
| Source SHA-256 | `a19d90e6fd41d09a0281ec0fe762d51491d7a6ccffc923c4f7868c5e647ca230` |
| Toolchain | MSVC 19.44.35228, toolset 14.44.35207, Windows SDK 10.0.26100.0 |
| Build | Release, x64, DLL, `/MD`, sem zlib |
| Instalação | 150 arquivos, 2.762.008 bytes |

O consumer independente encontrou `TagLib::TagLib`, compilou, vinculou e imprimiu `2.3.1`. As licenças de origem são `LGPL-2.1-only OR MPL-1.1`, com `utf8cpp` sob BSL-1.0; os textos não são copiados automaticamente para o prefixo instalado e continuam obrigatórios em eventual pacote.

## TDD e checks

O Red do playback configurou o build Release e falhou em C++ com `C1083` porque `QtMultimediaPlaybackBackend.hpp` ainda não existia. O Green implementou somente o necessário para o recorte WAV. A falha anterior de descoberta do componente Multimedia foi uma precondição de composição, não foi apresentada como Red comportamental; o configure correto usa o SDK base em `CMAKE_PREFIX_PATH` e o overlay em `QT_ADDITIONAL_PACKAGES_PREFIX_PATH`.

O Red de metadata falhou em C++ pela ausência de `TagLibMetadataExtractor.hpp`. O Green inicial cobriu `MR-101`–`MR-103`. Uma revisão encontrou o risco de manter referência ao valor de um `variant` temporário; o teste passou a preservar o resultado antes de obter a referência e foi recompilado/reexecutado. Um Red adicional então falhou ao tentar observar ausência em propriedades numéricas ainda representadas por zero; o Green tornou duração, sample rate e canais opcionais.

No build integrado, novo e Release:

- configure: MSVC `19.44.35228.0`, sucesso;
- build: 145 passos, sucesso com `/W4 /WX /permissive-` nos alvos próprios;
- CTest: **60/60**;
- corpus autenticado: **14/14**;
- teste Qt `m0.qt.audio.pb-001-wav-load`: `Loading → Ready`, 1.000 ms e seekable;
- metadata: **45 asserções em quatro casos**, incluindo `MR-101`, `MR-102`, `MR-103` e ausência sem zero falso;
- configure/build separado com `M0_QT_AUDIO_SUPPORT=OFF`: `m0_qt_core` verde e cache sem entradas Qt6/TagLib;
- nenhum processo de teste permaneceu aberto.

O timeout de dois segundos no teste é um limite funcional para impedir espera indefinida, não uma coleta de benchmark nem prova de `open-to-audible`.

## Limites e próximo slice

Este resultado não prova áudio audível, play/pause reais, seek, evento `ended`, unload/shutdown, descarte por geração, recuperação após erro, codecs comprimidos, UI, dispositivo, pacote ou Linux. `PB-101` permanece aberto porque exige seek e término reais; `PB-001` possui apenas a linha WAV coberta. Nenhum formato foi publicado e M0.6 não está concluído.

O próximo incremento funcional deve materializar erros/eventos/geração e a lifecycle da porta de playback, completar seek/fim do WAV e adicionar o boundary assíncrono/cancelável de metadata. Depois disso, o shell pode receber a primeira jornada local abrir → tocar → seek → pausar, ainda sem benchmarks ou empacotamento.

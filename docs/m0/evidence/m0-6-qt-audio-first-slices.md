# M0.6 — slices funcionais Qt Multimedia e TagLib

**Data:** 2026-08-29

**Estado:** M0.6 em andamento; lifecycle e ciclo WAV no Qt, extração inicial no TagLib e boundary assíncrono limitado de metadata verdes sem suporte de formato publicado

## Escopo

Este incremento prioriza comportamento funcional do candidato C++ e mantém `package-v2`, A/B e benchmarks suspensos. Ele não integra áudio ao shell/QML e não decide a stack.

Os recortes independentes implementados são:

- `PlaybackBackend` expõe snapshot, load/unload/shutdown, play/pause/seek, volume/mudo, callback de eventos e resultados tipados; o fake continua inteiramente C++ e sem Qt;
- `QtMultimediaPlaybackBackend` cria uma sessão `QMediaPlayer` por geração, publica `Loading` antes de `Ready`, executa play/pause/seek/fim/replay do WAV com saída mutada nos testes, emite `ended` uma vez por término e recupera após vazio/truncado;
- `TagLibMetadataExtractor` lê propriedades WAV, preserva tags ausentes, aplica fallback de título pelo nome, identifica RIFF/WAVE por conteúdo sob extensão `.mp3` e devolve erro tipado para vazio/truncado;
- `MetadataReader` e `SingleWorkerMetadataReader` permanecem C++ puro, executam o extrator em um worker, mantêm no máximo um trabalho ativo e o pendente mais recente e descartam cancelamento, geração obsoleta e resultado fora do budget de 2 s.

O nome `MetadataExtractor` continua intencional: a chamada TagLib permanece síncrona dentro do worker e, se já começou, pode terminar fisicamente mesmo após cancelamento ou deadline. O boundary torna esse resultado inobservável e não acumula uma fila de novas leituras. Snapshot, comandos e eventos de playback são puros; o bridge de aplicação poderá consumir o mesmo `PlaybackEventCallback` do fake e do adapter Qt.

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

O Red do boundary assíncrono, no Developer PowerShell válido, falhou em C++ com `C1083` porque `SingleWorkerMetadataReader.hpp` ainda não existia. O Green adicionou a porta pura, ownership exclusivo do extractor, um `std::jthread`, um slot pendente substituível, geração crescente, cancelamento observável, deadline injetável e conversão segura de exceções. Os testes determinísticos cobrem `MR-016`, `MR-017`, `MR-020`, cancelamento atrasado, concorrência máxima um, exceção e shutdown durante leitura ativa; outro teste compõe o reader com o `TagLibMetadataExtractor` real e a fixture WAV autenticada.

O Red do lifecycle, executado dentro do Developer PowerShell válido, falhou porque a porta ainda não possuía `load`, `unload`, `shutdown`, `PlaybackCommandResult`, erro/generation no snapshot nem `playbackEnded`. Uma tentativa anterior fora desse ambiente falhou ao localizar o header padrão `cstdint` e foi tratada apenas como precondição de toolchain, não como Red comportamental. O Green adicionou os resultados tipados e os testes do fake e do adapter real.

Na revisão, reutilizar um `QMediaPlayer` foi considerado insuficiente para separar atualizações internas atrasadas. O adapter passou a aposentar a instância anterior, remover sua saída e criar outra por geração; cada callback captura também a identidade física do player. Um teste troca imediatamente o WAV truncado pelo válido e comprova que o erro antigo não contamina a nova geração. As fixtures vazia, truncada e de extensão enganosa também são autenticadas no configure antes de chegarem ao teste.

Reds adicionais fecharam defeitos reais encontrados em revisão. O primeiro reproduziu um `0xc0000005` em `QMediaPlayer::setSource` quando o callback de `Loading` fazia `unload()` de forma síncrona; comandos e callbacks passaram a revalidar player e geração após qualquer publicação reentrante. Outro mostrou que buscar para trás após EOF emitia `ended` novamente antes de reproduzir, porque o FFmpeg publica `positionChanged` ainda sob `EndOfMedia`; uma guarda de reposicionamento agora mantém o latch até a mudança terminar. Por fim, um Red cobriu a busca reentrante no snapshot final antes do `EndOfMedia`: o estado `Stopped` intermediário deixou de publicar um fim prematuro. A ordem pura também é testada no fake e no adapter: snapshot final pausado antes de `ended`, sem `Ready` transitório no término, e nenhum evento antigo atribuído à nova geração ou à posição substituída.

No build integrado, novo e Release:

- configure: MSVC `19.44.35228.0`, sucesso;
- build Release integrado: sucesso com `/W4 /WX /permissive-` nos alvos próprios;
- CTest integral: **71/71**;
- corpus autenticado: **14/14**;
- teste Qt `m0.qt.audio.playback-lifecycle`: `Loading → Ready`, 1.000 ms, seekable, play/pause/seek/fim/replay, `ended` ordenado e único, reentrada, lifecycle, troca rápida, conteúdo sob extensão enganosa e erro/recuperação;
- metadata TagLib: **53 asserções em cinco casos**, incluindo `MR-101`, `MR-102`, `MR-103`, ausência sem zero falso e composição assíncrona real;
- reader puro: **66 asserções em sete casos** para `MR-016`, `MR-017`, `MR-020`, validação, exceção e shutdown ativo;
- configure/build/test novo com `M0_QT_AUDIO_SUPPORT=OFF`: `m0_qt_core` verde em **65/65**, 1.943 asserções, sem localizar Qt6/TagLib;
- `clang-format --dry-run --Werror`, corpus 14/14, layout M0 e validação dos 44 Markdown/83 requisitos verdes;
- nenhum processo de teste permaneceu aberto.

O budget de dois segundos é um limite funcional determinístico, não uma coleta de benchmark nem prova de `open-to-audible`. Ele descarta o resultado e impede acúmulo além de um ativo e um pendente substituível; não interrompe fisicamente uma chamada TagLib já iniciada. Como `shutdown()` aguarda esse worker, ele não deve ser chamado na thread visual enquanto esse risco não for coordenado fora dela.

## Limites e próximo slice

Este resultado observa play/pause/seek/fim no `QMediaPlayer`, com a saída deliberadamente mutada, mas não prova áudio audível, dispositivo físico, open-to-audible, underrun ou device lost. Também não cobre codecs comprimidos, integração de metadata na UI, arte/limites, pacote ou Linux. `PB-101` está verde somente para o WAV autenticado; `PB-001` continua incompleto na matriz de formatos. `PB-005` continua pendente para uma rejeição posterior à aceitação pelo adapter, pois `QMediaPlayer::setPosition` não devolve confirmação/erro; a rejeição síncrona por estado inválido já retorna erro tipado sem alterar o snapshot. Nenhum formato foi publicado e M0.6 não está concluído.

O próximo incremento funcional é integrar o callback puro de playback e esse reader na primeira jornada local abrir → tocar → seek → pausar do shell, com polling/teardown fora de bloqueios da GUI. Benchmarks e empacotamento continuam suspensos.

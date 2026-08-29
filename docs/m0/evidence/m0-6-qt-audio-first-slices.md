# M0.6 — slices funcionais Qt Multimedia e TagLib

**Data:** 2026-08-29

**Estado:** M0.6 em andamento; lifecycle e ciclo WAV no Qt, extração WAV/MP3 CBR/VBR/Unicode no TagLib, polling assíncrono limitado e primeira jornada aplicação/QML com MP3/WAV verdes sem suporte de formato publicado

## Escopo

Este incremento prioriza comportamento funcional do candidato C++ e mantém `package-v2`, A/B, automação nativa e benchmarks suspensos. Ele integra a primeira jornada local ao shell/QML, mas não decide a stack nem conclui M0.6.

Os recortes independentes implementados são:

- `PlaybackBackend` expõe snapshot, load/unload/shutdown, play/pause/seek, volume/mudo, callback de eventos e resultados tipados; o fake continua inteiramente C++ e sem Qt;
- `QtMultimediaPlaybackBackend` cria uma sessão `QMediaPlayer` por geração, publica `Loading` antes de `Ready`, executa play/pause/seek/fim/replay do WAV com saída mutada nos testes, emite `ended` uma vez por término, recupera após vazio/truncado e converte paths por UTF-16 no Windows ou UTF-8 nativo no POSIX;
- `TagLibMetadataExtractor` lê WAV e MP3 CBR/VBR por conteúdo, preserva tags ausentes e ID3v2 Unicode, aplica fallback de título pelo nome, reconhece stream MP3 sem ID3 sob extensão `.audio` e devolve erro tipado para vazio, truncado e frame-sync aleatório;
- `MetadataReader` e `SingleWorkerMetadataReader` permanecem C++ puro, executam o extrator em um worker, mantêm no máximo um trabalho ativo e o pendente mais recente e expõem polling tipado para pendente, conclusão, cancelamento, expiração, geração obsoleta, shutdown e idle;
- `LocalTrackSession` permanece C++ puro e coordena load, fallback imediato pelo stem Unicode, gerações independentes, cancelamento da leitura anterior, validação da origem retornada e autoplay somente para `Ready` atual;
- o modo real de `PlayerPresenter` recebe eventos do backend pela fila da thread Qt, usa o timer existente para polling sem bloquear o caller e compartilha play/pause/seek com o QML sem exigir a fila congelada;
- o executável combinado compõe Qt Multimedia, `SingleWorkerMetadataReader`, TagLib e `QFileDialog` com filtros MP3/WAV, preservando o modo de fixture quando áudio não é ligado;
- `m0.qt.audio.local-integration` percorre WAV PCM e MP3 CBR autenticados por essa composição real, em path Unicode temporário e com saída mutada;
- `m0.qt.shell-executable.combined-startup` carrega o binário combinado, QML e FFmpeg, observa o estado vazio real e encerra pela saída de produção.

O nome `MetadataExtractor` continua intencional: a chamada TagLib permanece síncrona dentro do worker e, se já começou, pode terminar fisicamente mesmo após cancelamento ou deadline. O boundary torna esse resultado inobservável e não acumula uma fila de novas leituras. Snapshot, comandos e eventos de playback são puros; `LocalTrackSession` consome o mesmo `PlaybackEventCallback` do fake e do adapter Qt, e o presenter apenas traduz essa sessão para a boundary visual.

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

O Red MP3 abriu três falhas funcionais, enquanto o caso hostil inicial já permanecia verde. O Green adicionou regressões locais aos casos canônicos existentes: CBR/VBR e Unicode cobrem `MR-001`/`MR-018`; stream sem ID3, extensão `.audio` e fallback pelo stem cobrem `MR-003`/`MR-018`/`MR-019`; hostis permanecem em `MR-103`. A implementação seleciona `TagLib::MPEG::File` somente após reconhecer ID3v2 válido ou frame MPEG Layer III por conteúdo; CBR/VBR autenticados retornam duração, 48 kHz, dois canais, bitrate, contêiner/codec e tags esperados. A fixture dedicada preserva título/artista/álbum Unicode; ID3 truncado e bytes aleatórios que apenas imitam frame-sync retornam erro tipado sem crash. O configure autentica tamanho e SHA-256 das fixtures MP3 CBR, VBR e Unicode antes de disponibilizá-las aos alvos de teste.

O Red do boundary assíncrono, no Developer PowerShell válido, falhou em C++ com `C1083` porque `SingleWorkerMetadataReader.hpp` ainda não existia. O Green adicionou a porta pura, ownership exclusivo do extractor, um `std::jthread`, um slot pendente substituível, geração crescente, cancelamento observável, deadline injetável e conversão segura de exceções. Os testes determinísticos cobrem `MR-016`, `MR-017`, `MR-020`, cancelamento atrasado, concorrência máxima um, exceção e shutdown durante leitura ativa; outro teste compõe o reader com o `TagLibMetadataExtractor` real e a fixture WAV autenticada.

Um Red posterior exigiu que os estados terminais do reader fossem observáveis sem confundir “ainda trabalhando” com “sem resultado”. O Green adicionou `MetadataPollStatus`/`MetadataPollResult`: cancelamento e expiração permanecem observáveis para a geração atual, conclusão é consumida uma vez e deadline é publicado mesmo se o extrator síncrono ainda estiver bloqueado. Os testes também preservam coalescimento e descarte de gerações antigas.

O Red da coordenação local falhou pela ausência de `LocalTrackSession.hpp`. O Green separou da UI a abertura, o fallback, a identidade da origem e as gerações. Regressões cobrem load rejeitado sem iniciar metadata, troca que cancela a geração anterior, evento de playback antigo, envelope ou `source_path` divergente, estados terminais que preservam fallback e autoplay apenas uma vez para a geração atual.

O Red do presenter falhou porque não existiam o construtor real nem `openAudio()`. O Green manteve a API de fixture e acrescentou injeção estreita de backend, reader e seletor. Regressões verificam cancelamento do diálogo inerte, fallback Unicode imediato, evento enfileirado, transporte/seek sem fila, polling terminal e teardown. Revisões adicionais encontraram dois defeitos de coordenação: uma nova geração ainda conservava preview/ack de seek da faixa anterior, e a restauração de volume ao desmutar não tentava rollback quando o comando de mudo era rejeitado. Ambos receberam testes Red antes das correções.

O teste integrado usa os adapters de produção e injeta somente a função que fornece o path, sem automatizar o `QFileDialog`. As duas linhas copiam a fixture para `ação 日本 ☕`, exigem que playback e metadata recebam o mesmo path nativo, observam o event loop durante a leitura, aguardam `Loading → Ready → Playing`, fazem seek e pausa e esperam metadata terminal sem erro. No MP3, o título Unicode embutido substitui o fallback. O primeiro Red dessa expectativa observou duração de 2.040 ms fora de ±20 ms; a tolerância integrada foi alinhada a ±50 ms, igual ao contrato do extrator/corpus, e então ficou verde. O volume é zero e mudo é ativado antes do load; a execução não constitui prova audível.

A revisão final encontrou que os smokes de fixture não poderiam validar a composição real e que `path.wstring()` seria dependente de locale no POSIX. O Red do novo argumento falhou em compilação enquanto `ShellLaunchOptions` ainda não o expunha; o primeiro smoke do binário falhou ao não localizar o objeto QML pelo nome contratual. O Green adicionou um smoke dedicado ao combinado, sem carregar faixa nem abrir diálogo, e a conversão explícita UTF-16/UTF-8. Os smokes de cenário congelado permanecem somente no configure sem áudio.

O Red do lifecycle, executado dentro do Developer PowerShell válido, falhou porque a porta ainda não possuía `load`, `unload`, `shutdown`, `PlaybackCommandResult`, erro/generation no snapshot nem `playbackEnded`. Uma tentativa anterior fora desse ambiente falhou ao localizar o header padrão `cstdint` e foi tratada apenas como precondição de toolchain, não como Red comportamental. O Green adicionou os resultados tipados e os testes do fake e do adapter real.

Na revisão, reutilizar um `QMediaPlayer` foi considerado insuficiente para separar atualizações internas atrasadas. O adapter passou a aposentar a instância anterior, remover sua saída e criar outra por geração; cada callback captura também a identidade física do player. Um teste troca imediatamente o WAV truncado pelo válido e comprova que o erro antigo não contamina a nova geração. As fixtures vazia, truncada e de extensão enganosa também são autenticadas no configure antes de chegarem ao teste.

Reds adicionais fecharam defeitos reais encontrados em revisão. O primeiro reproduziu um `0xc0000005` em `QMediaPlayer::setSource` quando o callback de `Loading` fazia `unload()` de forma síncrona; comandos e callbacks passaram a revalidar player e geração após qualquer publicação reentrante. Outro mostrou que buscar para trás após EOF emitia `ended` novamente antes de reproduzir, porque o FFmpeg publica `positionChanged` ainda sob `EndOfMedia`; uma guarda de reposicionamento agora mantém o latch até a mudança terminar. Por fim, um Red cobriu a busca reentrante no snapshot final antes do `EndOfMedia`: o estado `Stopped` intermediário deixou de publicar um fim prematuro. A ordem pura também é testada no fake e no adapter: snapshot final pausado antes de `ended`, sem `Ready` transitório no término, e nenhum evento antigo atribuído à nova geração ou à posição substituída.

No build Release inicial dos adapters, antes da ligação ao shell:

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

Após o slice MP3, o filtro `[mp3]` passou **93 asserções em quatro casos**, a suíte completa do extrator passou **146 asserções em nove casos** e o CTest focado `^m0\.qt\.metadata\.` passou **9/9**. `clang-format` e `git diff --check` também passaram nesse recorte.

Na composição funcional posterior, os checks focados registraram:

- core C++ puro: **85 casos e 2.148 asserções**, todos verdes;
- `PlayerPresenterTests`: **53/53**;
- Quick Test da jornada do shell: **22/22**;
- `m0.qt.audio.local-integration`: **1/1** no CTest, com duas linhas de dados (WAV PCM e MP3 CBR Unicode) em 0,42 s; metadata MP3 terminou sem erro e publicou o título embutido;
- `m0.qt.shell-executable.combined-startup`: **1/1** em 0,39 s no fechamento final, com QML/backend carregados e saída normal pelo lifecycle real;
- build Release de `m0_qt_shell`: **122/122 passos**, com `tag.dll` copiada ao lado do executável;
- formato C++/QML, `qmllint` e `git diff --check` verdes no recorte correspondente.

O budget de dois segundos é um limite funcional determinístico, não uma coleta de benchmark nem prova de `open-to-audible`. Ele descarta o resultado e impede acúmulo além de um ativo e um pendente substituível; não interrompe fisicamente uma chamada TagLib já iniciada. Como `shutdown()` aguarda esse worker, ele não deve ser chamado na thread visual enquanto esse risco não for coordenado fora dela.

## Limites e próximo slice

Este resultado observa play/pause/seek/fim no `QMediaPlayer`, integra abertura/polling/apresentação para WAV e MP3 CBR e extrai separadamente propriedades/tags de MP3 CBR/VBR/Unicode, com a saída deliberadamente mutada, mas não prova áudio audível, dispositivo físico, open-to-audible, underrun ou device lost. A escolha nativa está ligada no executável, porém o teste injeta o path e não automatiza ou inspeciona o diálogo. A jornada ainda não cobre M4A/AAC, FLAC, Ogg/Vorbis, Opus/ALAC, arte/limites, pacote ou Linux. `PB-101` está verde somente para o WAV autenticado; `PB-001` continua incompleto na matriz de formatos. `PB-005` continua pendente para uma rejeição posterior à aceitação pelo adapter, pois `QMediaPlayer::setPosition` não devolve confirmação/erro; a rejeição síncrona por estado inválido já retorna erro tipado sem alterar o snapshot. Nenhum formato foi publicado e M0.6 não está concluído.

O próximo incremento funcional é ampliar essa mesma jornada aos codecs e entradas hostis de maior valor e aplicar os limites restantes de metadata/arte sem bloquear a GUI. Benchmarks, automação nativa e empacotamento continuam suspensos.

# M0.6 — slices funcionais Qt Multimedia e TagLib

**Data:** 2026-08-29

**Estado:** M0.6 em andamento; lifecycle e geração isolada no Qt, `PB-001`/`PB-006` na matriz básica experimental de sete fixtures, `PB-004` em MP3 VBR/M4A AAC, extração por conteúdo no TagLib, polling assíncrono limitado e jornada aplicação/QML de nove linhas válidas com aliases `.mp4`/`.oga` verdes sem suporte de formato publicado. Regressões adicionais cobrem erro local, reabertura e descarte de geração com quatro prefixos truncados temporários, que não são fixtures canônicas; Opus/ALAC têm somente caracterização `UnsupportedFormat` no extrator, sem rejeição ponta a ponta

## Escopo

Este incremento prioriza comportamento funcional do candidato C++ e mantém `package-v2`, A/B, automação nativa e benchmarks suspensos. Ele integra a primeira jornada local ao shell/QML, mas não decide a stack nem conclui M0.6.

Os recortes independentes implementados são:

- `PlaybackBackend` expõe snapshot, load/unload/shutdown, play/pause/seek, volume/mudo, callback de eventos e resultados tipados; o fake continua inteiramente C++ e sem Qt;
- `QtMultimediaPlaybackBackend` cria uma sessão `QMediaPlayer` por geração, publica `Loading` antes de `Ready`, executa `PB-001`/`PB-006` para WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis e `PB-004` para MP3 VBR/M4A AAC com saída mutada nos testes, emite `ended` uma vez por término, recupera após vazio/truncado e converte paths por UTF-16 no Windows ou UTF-8 nativo no POSIX;
- `TagLibMetadataExtractor` lê WAV, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis por conteúdo, preserva tags ausentes e Unicode, aplica fallback de título pelo nome, reconhece stream MP3 sem ID3 sob extensão `.audio` e devolve erro tipado para vazio, truncado e frame-sync aleatório; uma caracterização separada, fora de `MR-018`, retorna `UnsupportedFormat` para Opus/ALAC somente nessa porta;
- `MetadataReader` e `SingleWorkerMetadataReader` permanecem C++ puro, executam o extrator em um worker, mantêm no máximo um trabalho ativo e o pendente mais recente e expõem polling tipado para pendente, conclusão, cancelamento, expiração, geração obsoleta, shutdown e idle;
- `LocalTrackSession` permanece C++ puro e coordena load, fallback imediato pelo stem Unicode, gerações independentes, cancelamento da leitura anterior, validação da origem retornada e autoplay somente para `Ready` atual;
- o modo real de `PlayerPresenter` recebe eventos do backend pela fila da thread Qt, usa o timer existente para polling sem bloquear o caller, compartilha play/pause/seek com o QML sem exigir a fila congelada e, em erro local, preserva título/contexto Unicode e oferece reabrir;
- o executável combinado compõe Qt Multimedia, `SingleWorkerMetadataReader`, TagLib e `QFileDialog` com `.mp3/.MP3`, `.m4a/.M4A`, `.mp4/.MP4`, `.flac/.FLAC`, `.ogg/.OGG`, `.oga/.OGA` e `.wav/.WAV`, preservando o modo de fixture quando áudio não é ligado; filtro não equivale a suporte;
- `m0.qt.audio.local-integration` percorre nove linhas sobre sete fixtures autenticadas — WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC e Ogg/Vorbis, mais aliases `.mp4`/`.oga` — por essa composição real, em path Unicode temporário e com saída mutada;
- regressões de metadata, playback e integração derivam em runtime prefixos temporários de 64 bytes das fontes autenticadas MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis para cobrir erro tipado, recuperação e descarte de geração; esses derivados não pertencem ao corpus canônico;
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

O Red da matriz básica falhou ao encontrar `UnsupportedFormat` na primeira linha M4A/AAC antes de os novos leitores existirem. O Green passou a reconhecer por conteúdo — inclusive sob extensão neutra `.audio` — ISO MP4/M4A com AAC não criptografado, FLAC e Ogg/Vorbis, projetando duração, sample rate, canais, bitrate, bit depth quando aplicável e tags Unicode sem zeros falsos. O configure autentica tamanho e SHA-256 das três fixtures básicas novas antes de compilar os testes; no primeiro Green, a suíte completa do extrator passou **221 asserções em 11 casos**. Opus e ALAC retornam `UnsupportedFormat` numa caracterização separada sem tag `MR-018`; isso descreve apenas o extrator, não o playback ou a jornada integrada.

A revisão posterior cobriu o tamanho estendido de box ISO BMFF: uma entrada com `ftyp` e `largesize` válido retornava `UnsupportedFormat` no Red, mostrando que o contêiner ainda não era reconhecido. O Green passou a aceitar o cabeçalho estendido e então retornar `InvalidData` antes da leitura de metadata inválida. A suíte final do extrator passou **226 asserções em 12 casos**. Esse resultado não classifica candidatos: playback inicia independentemente do extrator e o Qt pode decodificar Opus/ALAC escolhidos por `.m4a` ou “Todos os arquivos”.

O Red do boundary assíncrono, no Developer PowerShell válido, falhou em C++ com `C1083` porque `SingleWorkerMetadataReader.hpp` ainda não existia. O Green adicionou a porta pura, ownership exclusivo do extractor, um `std::jthread`, um slot pendente substituível, geração crescente, cancelamento observável, deadline injetável e conversão segura de exceções. Os testes determinísticos cobrem `MR-016`, `MR-017`, `MR-020`, cancelamento atrasado, concorrência máxima um, exceção e shutdown durante leitura ativa; outro teste compõe o reader com o `TagLibMetadataExtractor` real e a fixture WAV autenticada.

Um Red posterior exigiu que os estados terminais do reader fossem observáveis sem confundir “ainda trabalhando” com “sem resultado”. O Green adicionou `MetadataPollStatus`/`MetadataPollResult`: cancelamento e expiração permanecem observáveis para a geração atual, conclusão é consumida uma vez e deadline é publicado mesmo se o extrator síncrono ainda estiver bloqueado. Os testes também preservam coalescimento e descarte de gerações antigas.

O Red da coordenação local falhou pela ausência de `LocalTrackSession.hpp`. O Green separou da UI a abertura, o fallback, a identidade da origem e as gerações. Regressões cobrem load rejeitado sem iniciar metadata, troca que cancela a geração anterior, evento de playback antigo, envelope ou `source_path` divergente, estados terminais que preservam fallback e autoplay apenas uma vez para a geração atual.

O Red do presenter falhou porque não existiam o construtor real nem `openAudio()`. O Green manteve a API de fixture e acrescentou injeção estreita de backend, reader e seletor. Regressões verificam cancelamento do diálogo inerte, fallback Unicode imediato, evento enfileirado, transporte/seek sem fila, polling terminal e teardown. Revisões adicionais encontraram dois defeitos de coordenação: uma nova geração ainda conservava preview/ack de seek da faixa anterior, e a restauração de volume ao desmutar não tentava rollback quando o comando de mudo era rejeitado. Ambos receberam testes Red antes das correções.

O teste integrado usa os adapters de produção e injeta somente a função que fornece o path, sem automatizar o `QFileDialog`. No primeiro slice, as duas linhas WAV PCM/MP3 CBR copiaram a fixture para `ação 日本 ☕`, exigiram que playback e metadata recebessem o mesmo path nativo, observaram o event loop durante a leitura, aguardaram `Loading → Ready → Playing`, fizeram seek e pausa e esperaram metadata terminal sem erro. No MP3, o título Unicode embutido substituiu o fallback. O primeiro Red dessa expectativa observou duração de 2.040 ms fora de ±20 ms; a tolerância integrada foi alinhada a ±50 ms, igual ao contrato do extrator/corpus, e então ficou verde. O volume é zero e mudo é ativado antes do load; a execução não constitui prova audível.

O incremento da matriz parametrizou a mesma jornada para nove linhas sobre sete fixtures: WAV PCM, MP3 CBR, MP3 VBR, MP3 Unicode, M4A/AAC, FLAC e Ogg/Vorbis, acrescentando `.mp4` como alias de M4A/AAC e `.oga` como alias de Ogg/Vorbis. Todas preservam o path Unicode, a responsividade, a sequência até `Playing`, seek/pausa e metadata terminal; as oito linhas de codecs com tags publicam o título Unicode embutido. As durações usam as tolerâncias do manifesto: WAV 0 ms, FLAC/Vorbis 20 ms, AAC 25 ms e MP3 50 ms. O CTest integrado final passou **1/1 em 1,13 s** com saída mutada.

A revisão final encontrou que os smokes de fixture não poderiam validar a composição real e que `path.wstring()` seria dependente de locale no POSIX. O Red do novo argumento falhou em compilação enquanto `ShellLaunchOptions` ainda não o expunha; o primeiro smoke do binário falhou ao não localizar o objeto QML pelo nome contratual. O Green adicionou um smoke dedicado ao combinado, sem carregar faixa nem abrir diálogo, e a conversão explícita UTF-16/UTF-8. Os smokes de cenário congelado permanecem somente no configure sem áudio.

O Red do lifecycle, executado dentro do Developer PowerShell válido, falhou porque a porta ainda não possuía `load`, `unload`, `shutdown`, `PlaybackCommandResult`, erro/generation no snapshot nem `playbackEnded`. Uma tentativa anterior fora desse ambiente falhou ao localizar o header padrão `cstdint` e foi tratada apenas como precondição de toolchain, não como Red comportamental. O Green adicionou os resultados tipados e os testes do fake e do adapter real.

Na revisão, reutilizar um `QMediaPlayer` foi considerado insuficiente para separar atualizações internas atrasadas. O adapter passou a aposentar a instância anterior, remover sua saída e criar outra por geração; cada callback captura também a identidade física do player. Um teste troca imediatamente o WAV truncado pelo válido e comprova que o erro antigo não contamina a nova geração. As fixtures vazia, truncada e de extensão enganosa também são autenticadas no configure antes de chegarem ao teste.

O recorte atual ampliou essa defesa sem alterar o catálogo canônico. Os testes copiam somente os primeiros 64 bytes de MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis autenticados para arquivos temporários; no extrator, as quatro entradas retornam `MetadataReadErrorCode::InvalidData` sem crash. No backend, cada entrada alcança erro e pode ser seguida pela fonte válida, e uma carga válida imediata conserva apenas eventos da nova geração. A regressão M4A expôs um estado carregado do Qt sem faixa de áudio; o adapter passou a observar essa capacidade e publicar `PlaybackErrorCode::InvalidMedia`, em vez de anunciar `Ready`.

No presenter, a regressão abre `Falha 日本語.mp3`, injeta o erro da geração atual e exige que título e contexto permaneçam `Falha 日本語`, com `open-audio` como ação primária. A reabertura de `Recuperada 日本語.mp3` cancela metadata anterior, limpa o erro/contexto e volta a `Playing`; uma notificação atrasada da primeira geração não altera o estado recuperado. A jornada integrada parametriza o mesmo fluxo para os quatro prefixos temporários, confirma `InvalidData` na metadata, reabre uma cópia válida e termina com playback e metadata vinculados somente à segunda seleção. Esses casos são regressões locais derivadas, não novas fixtures do manifesto ou evidência de suporte publicado.

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

Na primeira composição funcional posterior, os checks focados registraram:

- core C++ puro: **85 casos e 2.148 asserções**, todos verdes;
- `PlayerPresenterTests`: **53/53**;
- Quick Test da jornada do shell: **22/22**;
- `m0.qt.audio.local-integration`: **1/1** no CTest, com duas linhas de dados (WAV PCM e MP3 CBR Unicode) em 0,42 s; metadata MP3 terminou sem erro e publicou o título embutido;
- `m0.qt.shell-executable.combined-startup`: **1/1** em 0,39 s no fechamento final, com QML/backend carregados e saída normal pelo lifecycle real;
- build Release de `m0_qt_shell`: **122/122 passos**, com `tag.dll` copiada ao lado do executável;
- formato C++/QML, `qmllint` e `git diff --check` verdes no recorte correspondente.

No fechamento focado anterior da matriz básica, o build relevante ficou verde e CTest passou **14/14 em 6,34 s**: 11 casos de metadata, `m0.qt.audio.playback-lifecycle` **1/1 em 4,75 s**, `m0.qt.shell-executable.combined-startup` **1/1 em 0,40 s** e `m0.qt.audio.local-integration` **1/1 em 0,82 s**. Playback cobriu `PB-001`/`PB-006` nas sete linhas básicas e `PB-004` em MP3 VBR/M4A AAC. O corpus passou **14/14**, o runner público aprovou 12 fixtures e os checks documentais daquele recorte ficaram verdes.

Após a revisão, o build relevante voltou a ficar verde e CTest passou **15/15 em 6,49 s**: 12 casos de metadata, `m0.qt.audio.playback-lifecycle` **1/1 em 4,35 s**, `m0.qt.shell-executable.combined-startup` **1/1 em 0,43 s** e `m0.qt.audio.local-integration` **1/1 em 1,13 s** com nove linhas sobre sete fixtures. Playback preservou `PB-001`/`PB-006` nas sete fixtures e `PB-004` em MP3 VBR/M4A AAC sob as tolerâncias versionadas. O corpus permaneceu **14/14**, e o runner público aprovou 12 fixtures; o manifesto atual tem SHA-256 `de5491e6688b7cdad739c3b0e6beda21d15c73dffa02a8c9f1cfddf92255ba1e`. `scripts/validate-docs.ps1` validou 44 arquivos Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` também ficou verde.

O primeiro Green focado do recorte atual passou **2/2**: `m0.qt.audio.playback-lifecycle` **1/1 em 5,21 s** e `m0.qt.audio.local-integration` **1/1 em 1,42 s**. Após corrigir a corrida entre descoberta de faixas e autoplay, os cinco alvos relevantes compilaram e o CTest focado completo passou **17/17 em 11,87 s**: 13 testes de metadata, presenter, playback-lifecycle, startup combinado e integração. As execuções diretas registraram **2.150 asserções em 86 casos** no core, **262 asserções em 13 casos** no extrator, **35/35** no playback e **54/54 em 4,08 s** no presenter. Nenhum benchmark, A/B ou empacotamento foi executado nesse incremento.

O budget de dois segundos é um limite funcional determinístico, não uma coleta de benchmark nem prova de `open-to-audible`. Ele descarta o resultado e impede acúmulo além de um ativo e um pendente substituível; não interrompe fisicamente uma chamada TagLib já iniciada. Como `shutdown()` aguarda esse worker, ele não deve ser chamado na thread visual enquanto esse risco não for coordenado fora dela.

## Limites e próximo slice

Este resultado observa play/pause/seek/fim no `QMediaPlayer`, integra abertura/polling/apresentação e extrai propriedades/tags para a matriz básica experimental de WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis, com aliases `.mp4`/`.oga` e saída deliberadamente mutada, mas não prova áudio audível, dispositivo físico, open-to-audible, underrun ou device lost. A escolha nativa está ligada no executável, porém o teste injeta o path e não automatiza ou inspeciona o diálogo. Os quatro prefixos truncados são entradas temporárias derivadas e não satisfazem a pendência de fixtures canônicas truncadas de M0.5; arte/limites, hostis canônicos restantes, pacote e Linux continuam fora dessa jornada. Opus/ALAC permanecem candidatos sem comportamento ponta a ponta classificado: apenas o extrator retorna `UnsupportedFormat`; playback inicia separadamente e o Qt pode decodificá-los. `PB-101` continua verde somente para o WAV autenticado por definição do caso, enquanto `PB-001`/`PB-006` estão verdes nas sete fixtures básicas e `PB-004` em MP3 VBR/M4A AAC. `PB-005` continua pendente para uma rejeição posterior à aceitação pelo adapter, pois `QMediaPlayer::setPosition` não devolve confirmação/erro; a rejeição síncrona por estado inválido já retorna erro tipado sem alterar o snapshot. Nenhum formato foi publicado e M0.6 não está concluído.

O próximo incremento funcional é transformar truncados de maior valor em fixtures canônicas versionadas, ampliar os hostis além dos prefixos temporários já exercitados e aplicar os limites restantes de metadata/arte sem bloquear a GUI. Opus/ALAC permanecem candidatos até decisão e evidência próprias. Benchmarks, A/B, automação nativa e empacotamento continuam suspensos.

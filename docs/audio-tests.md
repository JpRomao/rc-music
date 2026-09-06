# Testes de áudio na unidade de produto

**Estado:** suites de áudio promovidas e verificadas na raiz; M1 e release permanecem abertos.
**Data:** 2026-09-06.

Este incremento promove três suites existentes, sem duplicar a implementação nem alterar suas assertions: [QtMultimediaPlaybackBackendTests.cpp](../tests/qt/QtMultimediaPlaybackBackendTests.cpp), [LocalAudioIntegrationTests.cpp](../tests/qt/LocalAudioIntegrationTests.cpp) e [TagLibMetadataExtractorTests.cpp](../tests/audio/TagLibMetadataExtractorTests.cpp). As fontes são movidas byte a byte; o consumidor histórico passa a apontar para os mesmos arquivos do produto.

## Corpus sintético e proveniência

[tests/audio/corpus](../tests/audio/corpus) contém cópias byte-idênticas dos 12 binários sintéticos e da licença do [corpus histórico](../experiments/m0/shared/audio-corpus). A comparação dos 13 pares confirmou tamanho e SHA-256 iguais:

- binários: **485.998 bytes**;
- `LICENSE.assets.txt`: **663 bytes**;
- total: **486.661 bytes**, aproximadamente **475,25 KiB**.

O conjunto inclui WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC e Ogg/Vorbis; casos hostis vazio/truncado; WAV com extensão MP3; e candidatos Opus/ALAC. Nenhum áudio pessoal, gravação comercial ou mídia de terceiros entra no conjunto.

Os originais, seus manifests e o protocolo comparativo permanecem intactos. A cópia permite que os testes do produto usem entradas locais próprias, sem configurar o shell M0 nem depender do harness ou da autenticação do protocolo comparativo.

A [licença preservada](../tests/audio/corpus/LICENSE.assets.txt), `LicenseRef-MusicPlayer-M0-Private`, permite uso, cópia e modificação privados para desenvolvimento/testes do repositório; não concede redistribuição pública. O FFmpeg registrado na receita histórica foi ferramenta de geração dos sinais, não parte deste corpus nem aprovação da configuração redistribuível do runtime.

## Composição e autenticação

[RcMusicAudioFixtures.cmake](../cmake/RcMusicAudioFixtures.cmake) fixa explicitamente tamanho e SHA-256 dos 13 arquivos. A validação é somente leitura: recusa arquivo ausente, diretório, tamanho ou hash divergente, e aceita o arquivo vazio autenticado. Não gera, repara ou substitui entradas para fazer o teste passar. No configure, os arquivos validados entram em `CMAKE_CONFIGURE_DEPENDS`.

[RcMusicAudioTests.cmake](../cmake/RcMusicAudioTests.cmake) fornece `rc_music_add_audio_tests(corpus_root)`. A integração raiz autentica e registra o corpus somente com desktop e `BUILD_TESTING=ON`; produção OFF não lê esse módulo de fixtures, não compila essas suites nem adquire uma dependência do corpus. Os nomes técnicos `M0_TEST_AUDIO_*` permanecem para preservar as fontes dos testes, sem tornar o experimento um pré-requisito.

Os alvos novos são:

| Alvo | Registro CTest |
|---|---|
| `rc_music_audio_adapter_tests` | `rc_music.audio.playback-lifecycle` |
| `rc_music_local_audio_integration_tests` | `rc_music.audio.local-integration` |
| `rc_music_taglib_metadata_extractor_tests` | casos descobertos com prefixo `rc_music.metadata.` |

Playback e integração usam Qt Multimedia/FFmpeg com saída mutada; a integração usa apresentação offscreen. Metadata usa TagLib real e cópias temporárias quando precisa modificar tags. Isso verifica o backend local, não som audível ou dispositivos físicos. O módulo conserva os ambientes de áudio e limites de playback/integração por alvo, sem aplicar `QT_FATAL_WARNINGS` global aos testes de áudio. Metadata recebe limite de 30 s por caso.

## Evidência e checks

- Baseline histórico antes dos movimentos: presenter, contrato UI, Quick e os dois alvos Qt de áudio passaram **5/5**, em **74,91 s**; metadata passou **35/35**, em **2,19 s**. Isso preserva a evidência anterior à promoção, não comprova por si só o build raiz.
- A guarda do corpus teve Red pelo helper CMake ausente e Green em **seis casos** de [AudioCorpusTests.cmake](../tests/cmake/AudioCorpusTests.cmake): corpus válido, arquivo vazio autenticado, tamanho divergente, hash divergente, ausência e diretório. Os probes são somente leitura e não adulteram corpus original ou promovido.
- Primeiro `validate` da raiz: configure/build Release e lint aprovados; CTest **270/270**, em **52,38 s**, incluindo os 35 casos TagLib, dois alvos Qt de áudio e a guarda do corpus. Formato **143 C++/28 QML** e scripts **33/33 + 17/17** verdes.
- A inspeção do inventário CTest encontrou uma lista de labels achatada pelo helper Catch. A regressão [AudioRegistrationTests.cmake](../tests/cmake/AudioRegistrationTests.cmake) ficou vermelha porque metadata não entrava na seleção `audio`; a configuração passou a usar um label único e o prefixo `rc_music.metadata.` para a subcategoria. Green: os **35 casos** pertencem à seleção `audio`, sem propriedades espúrias.
- `validate` final, após corrigir o registro: **271/271** no CTest raiz em **54,58 s**, incluindo os dois checks CMake, com configure/build/lint, formato **143 C++/28 QML** e scripts **33/33 + 17/17** aprovados. Validação documental final: **52 Markdown, 83 requisitos, 17 checks e 76 etapas**. Revisão independente sem achados P0–P2 remanescentes.
- Produção OFF recompilada e com **zero testes**. Inspeção dos grafos ON/OFF: **246 comandos da aplicação** em cada um, sem fontes de teste, macro de cenário, suporte fake ou experimento. O `build.ninja` OFF não referencia os módulos de teste áudio nem seu corpus.
- Compatibilidade do planner histórico de instalador, após atualizar somente o path de proveniência da suite movida: **18/18**. Não houve recompilação do instalador, execução de instalação ou alteração do Registry.
- Consumidor histórico reconfigurado e os três alvos recompilados com as fontes movidas: **37/37** testes de áudio/metadata passaram em **41,13 s**, conservando seus inputs originais. Os três movimentos são byte-idênticos, sem alterações nas assertions.

Os [comandos de desenvolvimento](development-commands.md) mantêm o fluxo de configure/build antes de CTest. Para uma seleção proporcional no build ON já atualizado, pode-se usar `ctest --test-dir out/product/desktop-tests --output-on-failure --no-tests=error -R '^rc_music\.(audio|metadata)\.'` ou o label `audio`. A seleção não substitui os demais checks relevantes à mudança.

## Limites

Esta promoção não publica uma nova matriz de formatos, não reclassifica Opus/ALAC nem substitui a evidência do runtime/instalador. Não houve benchmark, reconstrução de SDK, instalação no host, áudio audível/dispositivo físico ou nova interação nativa.

A promoção posterior de [presenter/UI](ui-tests.md) introduz uma variante exclusiva de testes, preservando os cenários históricos sem habilitá-los na aplicação raiz ON/OFF. CI Windows permanece um incremento separado, sem execução remota declarada. A transferência destas suites não encerra M1 nem os gates de acessibilidade, segurança, licenças e release.

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

Incremento posterior: o [catálogo factual](../tests/audio/corpus/catalog.v1.json) descreve somente sete fixtures básicas elegíveis para a [declaração de formatos do instalador](windows-installer.md). Seus tamanhos/hashes são conferidos contra os pins abaixo; não substitui o corpus, a licença ou um resultado de execução e não promove Opus/ALAC/entradas edge a suporte aprovado.

[RcMusicAudioFixtures.cmake](../cmake/RcMusicAudioFixtures.cmake) fixa explicitamente tamanho e SHA-256 dos arquivos originais e dos geradores, manifestos e extensões canônicas descritos abaixo. A validação é somente leitura: recusa arquivo ausente, diretório, tamanho ou hash divergente, e aceita o arquivo vazio autenticado. Não gera, repara ou substitui entradas para fazer o teste passar. No configure, os arquivos validados entram em `CMAKE_CONFIGURE_DEPENDS`.

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

## Preservação do log CTest

Em 2026-09-06, a consulta `--show-only=json-v1` usada pela guarda de labels foi corrigida: ela abria `LastTest.log` no build da suíte em execução, truncando a evidência anterior. O CTest continua lendo o registro real, seus includes e propriedades finais por `--test-dir`; `--overwrite BuildDirectory` direciona os arquivos auxiliares para um temporário exclusivo, removido ao terminar. Não há cópia/restauração do log original nem interpretação manual dos arquivos internos do Catch2.

A separação corresponde ao comportamento do [CTest 3.31.5](https://github.com/Kitware/CMake/blob/v3.31.5/Source/cmCTest.cxx): o override muda também a busca de customizações. Por isso a guarda recusa explicitamente `CTestCustom.cmake`/`CTestCustom.ctest` no build, em vez de ignorá-los silenciosamente; esses arquivos não existem na configuração validada.

[AudioRegistrationRegressionTests.cmake](../tests/cmake/AudioRegistrationRegressionTests.cmake), registrado como `rc_music.audio.registration-regression`, usa sete casos pequenos sem Qt: registro válido com label aplicada após include, label ausente/incorreta, `audio-extra`, registro vazio, include inválido e os dois arquivos de customização. Todos preservam o SHA-256 de um log sentinela e comprovam que nenhuma entrada de teste foi executada; paths incluem espaços e Unicode. A guarda original continua exigindo que todos os casos de metadata tenham exatamente o label `audio` entre seus labels.

- **Red:** a inspeção válida alterou o log sentinela antes da correção.
- **Green:** os sete casos de regressão passaram; reconfigure e seleção integrada passaram **4/4 em 1,11 s**, incluindo a consulta dos **35 casos reais** de metadata e testes antes/depois dela. `LastTest.log` conservou os quatro registros, sem regiões NUL de truncamento.
- A raiz registra agora **297 entradas CTest**. Esta alteração somente nos checks CMake não repetiu a suíte completa de áudio/UI, build de produção ou benchmarks; o total completo **296/296** do [incremento anterior](process-tests.md) continua sendo evidência daquele checkpoint.

Esta correção não recupera logs históricos truncados nem amplia a validação funcional/nativa do produto.

## Prefixos truncados canônicos

Em 2026-11-05, quatro prefixos de 64 bytes passaram a integrar o corpus do produto: MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis. O [manifesto e receita](../tests/audio/corpus/hostile-truncation.v1.json) registra fontes, tamanhos, hashes, licença privada existente e o resultado esperado `MetadataReadError::InvalidData`. A reprodução consiste em copiar os primeiros 64 bytes da fonte indicada; não exige encoder, rede ou áudio pessoal.

A regressão `MR-103` lê essas fixtures diretamente, verifica sua igualdade com os prefixos das fontes autenticadas e preserva as exigências de erro tipado, mensagem não vazia e ausência de exceção. O configure autentica também o novo manifesto. O consumidor histórico usa somente essa extensão do corpus do produto, mantendo seus arquivos, manifesto e caminhos básicos originais. Os testes de playback continuam gerando seus prefixos temporários; este incremento não lhes atribui uma nova execução nem publica capacidades de áudio.

Validação proporcional: build Release do alvo de metadados, **36/36 CTest** (35 casos de metadados e integridade do corpus, com seis probes), formato do C++ alterado, validação documental e rastreabilidade passaram. A conferência independente confirmou receita/hash dos quatro prefixos e preservação dos três probes congelados e do recibo do ensaio do instalador. Evidências em `out/product/test-runs/canonical-truncation-20261105/`. Não foi recompilado o consumidor histórico, o player ou o instalador; o código funcional permanece igual.

Este incremento resolveu o versionamento dos quatro truncados de `M0.5`/`M5.2`, sem encerrar esses marcos ou os gates de release. O recorte posterior abaixo acrescenta arte canônica.

## Capas canônicas e bordas reais

O [manifesto de arte](../tests/audio/corpus/artwork.v1.json) registra 14 MP3 derivados do sinal sintético existente, com **31.971.521 bytes** no total. Há um controle PNG de 1 × 1, arte inválida e casos abaixo/no/acima de 10 MiB codificados, 4.096 pixels em cada eixo e 16.000.000 pixels. O caso abaixo do limite agregado usa 3.999 × 4.001 = 15.999.999 pixels. Os sinais e tags originais são preservados; somente um frame APIC ID3v2.3 é acrescentado.

O [gerador](../scripts/fixtures/generate_artwork_corpus.py) usa apenas Python stdlib, autentica a fonte e exige um stage novo sob `out/m0`. PNG usa grayscale8 preto, filter 0, zlib nível 9 e CRC por chunk; padding privado `raNd` em chunks de até 1 MiB ajusta o tamanho codificado exato. A receita registra versões Python/zlib, hash do script e da fonte. Duas gerações produziram bytes e manifestos idênticos; saídas existentes ou fora do stage foram recusadas sem escrita. Os testes cotidianos usam os binários versionados, sem executar o gerador.

`MR-006`, `MR-007`, `MR-009` e `MR-010` agora percorrem contêiner, TagLib e decoder reais. Nove entradas aceitas e cinco rejeitadas verificam thumbnail/diagnóstico e preservação das propriedades de áudio e título. O Red encontrou quatro falhas: capas de 4.095/4.096 × 1 e 1 × 4.095/4.096 eram descartadas após o arredondamento de um eixo para zero. A correção conserva pelo menos um pixel por eixo no cálculo do thumbnail.

Green: **39/39 CTest** (37 casos de metadados, integração local e integridade), build Release do player sem testes e formato dos dois C++ alterados aprovados. A inspeção independente confirmou os 14 hashes, frames APIC, tags/áudio originais, CRCs e dimensões físicas dos PNG. Evidências em `out/product/test-runs/artwork-boundaries-20261105/`. O consumidor histórico recebeu os caminhos/autenticação novos, sem ser recompilado; seus assets originais permanecem intactos.

O incremento seguinte acrescenta capas nos demais contêineres básicos e bordas textuais. TagLib ainda materializa a tag antes do gate de tamanho do produto; a proteção de pré-alocação do parser, `NFR-LIMIT-001`/`SEC-002` e os gates amplos continuam parciais. Não houve execução do player, atualização dos instaladores ou aceitação nativa.

## Capas e textos no corpus dos codecs

Os novos manifestos de [capas por contêiner](../tests/audio/corpus/codec-artwork.v1.json) e [limites de texto](../tests/audio/corpus/text-limits.v1.json) acrescentam 13 arquivos, **636.248 bytes**: M4A/AAC, FLAC e Ogg/Vorbis com PNG de 1 × 1; título, artista e álbum MP3 com 16 KiB UTF-8 − 1, exatamente 16 KiB e + 1; e três textos no limite junto de faixa/ano válidos. A matriz de capas e os casos `MR-012`/`MR-013` passam a ler as fixtures diretamente, com as mesmas assertions. Duas regressões adicionais verificam cada campo independentemente e o agregado realmente alcançável de **49.160 bytes**. O limite geral de 256 KiB continua separado na policy.

Os geradores de [capas](../scripts/fixtures/generate_codec_artwork_corpus.py) e [texto](../scripts/fixtures/generate_tag_corpus.py) reutilizam o helper existente, autenticam fontes e escrevem somente em stage novo. A autoria de capas usa o mesmo FFmpeg 8.1.1 pinado da receita histórica, apenas com stream copy e mídia sintética local; o executável não é copiado ou exigido pelos testes cotidianos. O texto usa Python stdlib, frames ID3v2.3 UTF-16LE e contagem explícita dos bytes UTF-8 esperados. Scripts, fontes, ferramentas e argumentos ficam nas receitas, sob a licença privada existente.

Duas gerações reproduziram os 13 binários e dois manifestos; cinco tentativas com saída existente/fora do stage ou ferramenta divergente foram recusadas. A inspeção independente confirmou os hashes, os três streams de áudio codificado e, nos dez MP3, áudio, padding e frames não alterados. O fechamento passou **40/40 CTest** (39 casos de metadados e integridade), build Release do alvo, formato, documentação e rastreabilidade. Evidências em `out/product/test-runs/canonical-metadata-20261105/`.

Este incremento altera corpus e testes. O código do player e os instaladores permanecem nos seus commits anteriores; seus checks não foram repetidos. Parser/isolamento, outros perfis hostis, budgets, catálogo global e aceitação nativa mantêm seus próprios gates; `M0.5`/`M2.6`/`M5.2` não são encerrados por esta cobertura.

## FLAC precedido por ID3

O extrator agora procura o marcador FLAC no fim indicado por uma tag ID3v2 inicial, mesmo quando a extensão é `.mp3`. A leitura usa quatro bytes no deslocamento calculado e inclui o rodapé quando sinalizado pelo cabeçalho ID3v2.4. Um marcador dentro da tag não identifica o áudio. O leitor usa os metadados Vorbis e as propriedades do FLAC no arquivo original. [Estrutura do ID3v2.4](https://id3.org/id3v2.4.0-structure)

O [manifesto do corpus](../tests/audio/corpus/id3-prefixed-flac.v1.json) fixa a receita e os hashes de três arquivos sintéticos. Todos preservam o FLAC canônico byte a byte. O caso válido começa o áudio no byte 610 e inclui um marcador enganoso dentro da tag. As duas variantes adulteram o comprimento declarado para ultrapassar o arquivo ou usar um bit reservado. Ambas retornam `InvalidData` na leitura de metadados.

O Red reproduziu a perda dos metadados em dois testes do extrator e na jornada Qt com caminho Unicode. O Green passou **45/45 CTest**, incluindo o catálogo de metadados, a integridade do corpus e a reprodução Qt com busca de posição e decodificação até o fim. O build Release do player passou. Logs em `out/product/test-runs/id3-prefixed-flac-20261111/`. Os limites de alocação internos do TagLib e a aceitação instalada permanecem pendentes.

## Limite por leitura de metadados

A identificação do contêiner e o TagLib usam a mesma abertura somente leitura para WAV, MP3, FLAC, M4A e Ogg/Vorbis. O adaptador limita cada leitura a **32 MiB** antes da alocação do buffer de entrada. Perto do fim do arquivo, um pedido maior recebe apenas os bytes disponíveis se couberem no limite. A recusa encerra a extração com `InvalidData`. O player continua usando o nome do arquivo como título quando a reprodução está disponível.

O teto acomoda tags com várias capas no limite individual de 10 MiB. Os limites de arte e de retenção de texto continuam aplicados depois da leitura. O teto cobre o buffer de cada leitura e deixa em aberto a memória cumulativa, as alocações internas dos parsers e o isolamento do decoder.

Os testes expandem com preenchimento a tag de um MP3 sintético autenticado e preservam seus frames de áudio. Uma tag de 32 MiB continua legível. O Red aceitou uma tag um byte maior e a jornada real recebeu os metadados em vez do erro. No Green, o extrator devolve o erro, lê um MP3 válido em seguida e, na jornada Qt, o mesmo áudio toca até o fim com o nome do arquivo como título. Passaram o build Release do player e **47/47 CTest**, incluindo a matriz dos cinco leitores, a jornada Qt e a integridade do corpus. Os arquivos de teste são gerados em diretórios temporários. Logs em `out/product/test-runs/bounded-metadata-read-20261111/`.

## Limites

Esta promoção não publica uma nova matriz de formatos, não reclassifica Opus/ALAC nem substitui a evidência do runtime/instalador. Não houve benchmark, reconstrução de SDK, instalação no host, áudio audível/dispositivo físico ou nova interação nativa.

A promoção posterior de [presenter/UI](ui-tests.md) introduz uma variante exclusiva de testes, preservando os cenários históricos sem habilitá-los na aplicação raiz ON/OFF. CI Windows permanece um incremento separado, sem execução remota declarada. A transferência destas suites não encerra M1 nem os gates de acessibilidade, segurança, licenças e release.

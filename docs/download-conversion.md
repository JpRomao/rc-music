# Conversão local opcional de downloads

**Estado:** incremento local de R1.2/R1.8 validado; fora do player e sem backend completo de execução de downloads.

O [ADR-0009](architecture/decisions/0009-download-execution-backends.md) permanece **Proposto**. Este recorte acrescenta área temporária Windows, subprocesso limitado e conversão de bytes locais para MP3. Não conecta o [transporte](download-network.md) à porta `DownloadExecution`, à [coordenação de jobs](download-jobs.md) ou ao importador do player.

**Restrição do ensaio:** o FFmpeg/ffprobe Gyan 8.1.1 já disponível é usado somente com fixtures sintéticas confiáveis, autenticadas pelo teste. O gate de correções de segurança registrado no ADR não foi fechado; disponibilidade local e testes verdes não aprovam processamento de mídia remota ou não confiável, instalação ou redistribuição do CLI.

## APIs e responsabilidades

| Componente | Contrato deste incremento |
| --- | --- |
| [WindowsDownloadWorkspace](../src/adapters/WindowsDownloadWorkspace.hpp) | `create()` abre uma área nova privada; `inputSink()`/`outputSink()` compartilham a cota; `sealInput()`/`sealOutput()` fecham escrita e tornam os paths internos disponíveis. `validateAndPublish()` exige validação confiável e publica sem sobrescrever. `cleanup()` remove somente identidades pertencentes ao job. |
| [WindowsDownloadProcess](../src/adapters/WindowsDownloadProcess.hpp) | `start()` recebe executável, argumentos, diretório e payload stdin limitado da composição confiável; `poll()` retorna dados limitados e estados; `cancel()` solicita parada do processo próprio. O estado terminal reconhece término do processo e conclusão dos pipes. |
| [FfmpegDownloadCodec](../src/adapters/FfmpegDownloadCodec.hpp) | Constrói vetores de argumentos locais, decodifica JSON de inspeção com campos limitados e interpreta progresso incremental. Não lança processos nem aceita URLs. |
| [WindowsLocalMediaConverter](../src/adapters/WindowsLocalMediaConverter.hpp) | `start(workspace, now_ms)` exige entrada selada e saída vazia; `poll(now_ms)` coordena inspeção, conversão, inspeção de saída e decodificação integral; `cancel()` aguarda parada reconhecida. `Validated` não publica nem importa. |

Construir, usar e destruir estes objetos em uma mesma thread worker com event loop Qt, nunca na thread visual. O workspace emprestado deve sobreviver ao conversor. Escritas, flush e operações nativas de arquivos podem bloquear; `poll()` não cria timer, worker ou event loop. O proprietário fornece relógio monotônico e mantém o lifecycle até o reconhecimento terminal. Relógio regressivo é erro, não reinício de deadline.

Os pais de staging/publicação e executáveis são configuração da composição confiável. URLs, títulos, nomes de arquivo, argumentos e caminhos fornecidos por extensão/página não são parâmetros destas APIs. A construção lexical de argumentos locais não constitui autorização para qualquer arquivo do host.

## Área privada, cota e publicação

`WindowsDownloadWorkspace` exige pais locais existentes e confiáveis; aplica DACL privada somente ao filho novo do job, sem alterar permissões dos pais. Nomes do job, parciais e MP3 final são gerados internamente. Pais e arquivos são inspecionados por handles/identidade; reparse points e substituições incompatíveis são recusados. A proteção não torna confiável outro processo do mesmo usuário, nem autoriza ignorar o gate de sandbox.

O limite padrão e máximo é **512 MiB agregados de bytes lógicos da entrada mais saída**, não 512 MiB para cada arquivo. Uma entrada que consome a cota inteira não deixa espaço para converter. O orçamento controla bytes escritos nestes sinks; não promete medir clusters alocados, metadados NTFS, todos os arquivos do host ou memória total do processo proprietário.

`inputPath()` e `outputPath()` são caminhos internos de parciais selados, não mídia concluída. `plannedPublicationPath()` é apenas um nome interno antecipado, nunca confirmação de sucesso. O contrato de publicação exige entrada/saída seladas, validação confiável, flush e rename nativo com `ReplaceIfExists = FALSE`, verificando a identidade e o caminho final antes de devolver um path publicado. Colisão ou publicação incerta falham sem substituir um arquivo existente.

Os ancestrais e staging negam compartilhamento de escrita/remoção. A folha de publicação compartilha escrita para permitir a operação nativa, mas continua negando remoção/rename do diretório. Depois do rename absoluto, o path normalizado com GUID obtido do handle do arquivo precisa corresponder exatamente ao parent canônico pinado mais o UUID; resultado incerto é removido pelo handle DELETE já adquirido, sem reabrir o nome antigo. O teste introduz uma junction temporária durante a validação e exige recusa sem saída externa. Isso não é uma sandbox contra todo processo local hostil. Contratos nativos: [rename](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info), [path final por handle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew).

`validateAndPublish()` recebe uma função `bool(const QString&)` **confiável**. Esse `bool` não codifica prova criptográfica, capacidade de validação ou autoridade de um chamador não confiável. A composição futura deve vincular a validação concluída ao mesmo workspace e aos mesmos bytes, sem aceitar um `true`, path ou estado informado pela página. Neste recorte, o conversor e o workspace são exercitados por composição confiável de teste; ainda não existe essa composição no app.

`cleanup()` verifica as identidades dos arquivos e diretório próprios; não é remoção recursiva de um path arbitrário. Preserva o MP3 cuja publicação já foi reconhecida e pode informar falha de limpeza. Cancelamento/erro não significam que qualquer parcial já foi removido: o proprietário deve primeiro aguardar a parada reconhecida do subprocesso e depois verificar a limpeza.

## Subprocesso e limites

Não há concatenação de shell ou busca implícita de executáveis em `PATH`. A composição fornece caminhos absolutos existentes e argumentos separados. O processo usa `CREATE_NO_WINDOW`, sem abrir terminal, herda apenas os handles de stdin/stdout/stderr previstos e pertence a Job Object próprio. O fechamento do Job encerra seus filhos; não são encerrados processos alheios.

O Job limita a memória de processo/job a **512 MiB por padrão** e restringe descendentes. Esse limite nativo não limita a memória do player/worker proprietário e **não restringe permissões de rede ou sistema de arquivos**. Não constitui sandbox de conteúdo hostil. O deadline padrão é **30 minutos**, compartilhado entre todas as etapas do conversor, sem renovar o total a cada processo.

O Job permanece limitado a **um processo**, sem breakaway. Não usar este supervisor para um worker integral que depois lança FFmpeg: o limite ancestral impediria a criação da ferramenta. O controlador deve manter o workspace e supervisionar separadamente rede e conversão; ampliar a árvore exige ownership/handles e reconhecimento próprios, não apenas observar `ActiveProcesses == 0`. Esta ampliação não faz parte do incremento.

`standard_input` permite uma única mensagem opaca de até **256 KiB**, enviada por pipe nativo com buffer fixo e backpressure, seguida de EOF. Entrada vazia conserva stdin em NUL. O supervisor não interpreta JSON/framing, não coloca esse payload em argumentos ou ambiente e não constitui um host Native Messaging. O proprietário futuro ainda precisa limitar e validar seu protocolo e fornecer ambiente/artefatos revisados para o worker TLS; o ambiente herdado atual não é uma allowlist de segurança.

Pipes nativos têm buffers fixos/backpressure; `poll()` entrega no máximo **64 KiB somando ambos os canais**. A conversão limita stdout a 512 MiB e stderr a 4 MiB, além da cota agregada do workspace. As inspeções limitam JSON a 16 KiB; a validação final não espera stdout útil. Limite/deadline/cancelamento solicitam encerramento do Job e continuam em `Stopping` até reconhecer processo encerrado e pipes concluídos.

Saídas raw de ferramentas não vão para UI/log. O CLI executa em modo quiet; a conversão usa stderr apenas para o protocolo limitado `-progress pipe:2`, sem transformar diagnósticos, tags, URLs ou paths em mensagens públicas. A projeção futura deve expor somente estados, contadores e erros tipados/redigidos; não há integração de UI/log neste recorte.

## Inspeção, conversão e conclusão

O fluxo local possui quatro etapas obrigatórias:

1. `ffprobe` inspeciona a entrada selada, selecionando `a:0` e apenas campos de formato/codec/duração/taxa/canais. JSON inválido, codec/contêiner fora do contrato ou ausência de áudio são erros tipados.
2. FFmpeg transforma o primeiro áudio em MP3 `libmp3lame` 192 kbit/s, uma thread de áudio, sem vídeo, legendas, data streams, tags ou capítulos. Produz bytes em `pipe:1`, escritos pelo proprietário no sink limitado; não recebe o path final de publicação.
3. `ffprobe` exige que a saída selada tenha contêiner **e codec MP3**, com perfil/taxa/canais aceitos.
4. FFmpeg decodifica integralmente o áudio MP3 local em saída nula, com erros fatais. Somente código de saída zero, pipes concluídos e validação de todas as etapas permitem `Validated`.

Os vetores restringem protocolo de entrada a `file`, formatos de entrada ao conjunto explícito, probe a 1 MiB, análise a 5 s e streams a 16. Para ISO BMFF/MP4, referências externas/paths absolutos do demuxer são desabilitados e capítulos ignorados. Esses controles reduzem superfície; não provam ausência de todos os acessos locais possíveis em demuxers e não substituem o CLI corrigido nem sandbox. **Nenhuma URL remota é entregue a FFmpeg/ffprobe.**

O decoder `DownloadProgressCodec` limita total a 4 MiB, bloco a 16 KiB e linha a 1.024 bytes, recusa chaves duplicadas/linhas inválidas e exige contador monotônico. `progress=end` não é prova de êxito do processo, conteúdo correto ou publicação. Da mesma forma, `duration_us` é **estimativa opcional obtida dos metadados**, não um total garantido: o contador pode ultrapassá-la. Não converter esse valor em 100% concluído antes das validações e publicação reconhecida.

Os formatos/codec aceitos por esta inspeção não ampliam automaticamente a matriz pública de playback, a lista de fontes de download ou a compatibilidade com sites. MP4 sem áudio é recusado, não convertido em faixa vazia.

## Build opcional e ensaio local

[RcMusicDownloadNetwork.cmake](../cmake/RcMusicDownloadNetwork.cmake) acrescenta o codec à biblioteca opcional `rc_music_download_network` e os três adapters Windows somente em Windows. `RC_MUSIC_BUILD_DOWNLOAD_NETWORK` permanece OFF por padrão e não liga esta biblioteca ao executável do player. Não introduz download de dependências, instalação, host ou pacote.

Com `BUILD_TESTING=ON`, entram os testes de codec/workspace/processo e helpers de processo exclusivos dos testes. O alvo `rc_music_WindowsLocalMediaConverter_tests` só existe quando **ambos** os caches `RC_MUSIC_DOWNLOAD_TEST_FFMPEG` e `RC_MUSIC_DOWNLOAD_TEST_FFPROBE` são informados, absolutos e existentes; configuração inválida falha. Os caches não selecionam um conversor de produção. Com `BUILD_TESTING=OFF`, helpers, fixtures e testes não fazem parte do grafo.

Na sessão MSVC de [core-build](core-build.md#build-windows), com a árvore de [integração opcional](browser-extension.md#biblioteca-nativa-windows) já configurada para Qt/Catch2, a receita local é:

```powershell
$converterCliRoot = 'C:/Users/Joao/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.1.1-full_build/bin'
$converterFfmpeg = Join-Path $converterCliRoot 'ffmpeg.exe'
$converterFfprobe = Join-Path $converterCliRoot 'ffprobe.exe'
if (!(Test-Path -LiteralPath $converterFfmpeg -PathType Leaf) -or !(Test-Path -LiteralPath $converterFfprobe -PathType Leaf)) {
    throw 'Exige os dois executáveis locais explícitos, revisados somente para fixtures confiáveis'
}
cmake -S . -B out/product/browser-integration-tests -DBUILD_TESTING=ON -DRC_MUSIC_BUILD_DOWNLOAD_NETWORK=ON "-DRC_MUSIC_DOWNLOAD_TEST_FFMPEG=$converterFfmpeg" "-DRC_MUSIC_DOWNLOAD_TEST_FFPROBE=$converterFfprobe"
if ($LASTEXITCODE -ne 0) { throw 'Configure de conversão falhou' }
cmake --build out/product/browser-integration-tests --target rc_music_FfmpegDownloadCodec_tests rc_music_WindowsDownloadWorkspace_tests rc_music_WindowsDownloadProcess_tests rc_music_WindowsLocalMediaConverter_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de conversão falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.download-network\.(FfmpegDownloadCodec|WindowsDownloadWorkspace|WindowsDownloadProcess|WindowsLocalMediaConverter)$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes de conversão falharam' }
```

Esta receita não procura em `PATH`, instala ou atualiza o CLI. O path acima descreve apenas a ferramenta existente neste host; outro ambiente deve fornecer paths absolutos de artefatos revisados explicitamente, nunca uma substituição ambiental silenciosa. Não executar estes testes com mídia pessoal ou downloads. O CLI GPL full-build não é copiado ao runtime/instalador Lite ou Completo.

O [teste do conversor](../tests/qt/WindowsLocalMediaConverterTests.cpp) verifica tamanho/SHA-256 antes de copiar as duas fixtures sintéticas canônicas do [catálogo](../tests/audio/corpus/catalog.v1.json):

| Fixture | Bytes | SHA-256 |
| --- | --- | --- |
| `m4a-aac-lc.m4a` | 33.400 | `aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558` |
| `mp3-unicode-tags.mp3` | 24.966 | `65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000` |

As cópias ficam em raízes temporárias com espaços/Unicode. O caso sem áudio altera somente uma cópia temporária autenticada; os inputs canônicos permanecem intactos. Helpers sintéticos exercitam falha de validação/deadline sem depender de conteúdo remoto. A licença `LicenseRef-MusicPlayer-M0-Private` destes sinais permite o ensaio privado do projeto, não redistribuição pública; ver [corpus](quality/test-corpus.md). Nenhuma fixture é entrada de produção.

## Checkpoint de validação

TDD de 2026-10-01: codec começou com oito falhas e chegou a 12 passagens; a regressão de parâmetros MP3 impossíveis reproduziu outra falha antes da restrição de taxas/canais. Workspace começou com 20 falhas; processo com 18; conversor com seis. A publicação nativa permaneceu vermelha por sharing/rename até a correção por handles e path final, sem enfraquecer os testes.

Fechamento Release MSVC 19.44/Qt 6.11.2 com `/W4 /WX`: codec **13/13**, workspace **27 passagens/zero falhas/dois skips** de symlink pela política do host, processo **21/21** e conversor **13/13**. O teste de junction durante validação passou sem privilégio. Conversão real M4A-AAC/MP3 com Unicode, derivado sem áudio, quota, cancelamento e clocks passaram. Helper exclusivo de teste confirma falha após produzir MP3 válido quando o decoder final retorna erro, e deadline total após encerrar o primeiro probe.

A seleção `^rc_music\.(download-network\.|downloads\.|browser\.)` passou **41/41 CTest** em 10,36 s, incluindo suites anteriores efetivamente reexecutadas. `BUILD_TESTING=OFF` recompilou a biblioteca, listou zero testes e seu grafo excluiu Qt Test, Catch2, helpers, fixtures, paths do CLI, Multimedia e WebEngine. Formato, documentos/rastreabilidade e diff foram verificados separadamente. O core independente e JavaScript não foram reexecutados neste incremento C++ sem alteração dessas camadas; evidências anteriores não são execução atual. Nenhum benchmark, mídia pessoal, download de mídia da internet, instalação ou runtime/instalador novo foi executado.

### Incremento de stdin privado

Em 2026-10-01, o teste com stdin ainda conectado a NUL reproduziu perda do payload, e o teste acima de 256 KiB reproduziu admissão indevida. O supervisor com envio assíncrono passou **27/27 casos Qt, zero falhas/skips**, incluindo os 21 anteriores: bytes binários/Unicode, limite exato, EOF, rejeição acima do teto, cancelamento com escrita pendente e reuso. Build Release MSVC 19.44/Qt 6.11.2, CTest focado **1/1** em 2,84 s, formato e revisão do diff passaram.

A avaliação de um worker com descendentes foi retirada deste incremento: accounting vazio não é prova suficiente de todos os handles encerrados. O contrato original de um processo foi preservado, não se alegou validação multiprocesso e nenhum teste preexistente foi removido. Rede diskless e conversão separadas, com workspace no controlador, permanecem a direção de composição; não há host/worker de rede executável neste checkpoint.

## Gates que continuam abertos

1. CLI corrigido, artefato/proveniência/hashes e revisão dos avisos antes de aceitar mídia não confiável; licenças, notices, fontes correspondentes e distribuição do conjunto exato.
2. Sandbox efetivo de rede/sistema de arquivos para parser/conversor; Job Object e `-protocol_whitelist file` não fecham esse gate. Avaliar todos os acessos indiretos e limites nativos, sem diminuir verificação.
3. Composição integral em worker com rede/TLS restrito, workspace, conversor, publicação, cancelamento, espaço em disco e porta `DownloadExecution`; subprocessos ensaiados separadamente não comprovam o fluxo completo.
4. Importador local, persistência/recuperação e projeção de jobs, host Native Messaging e UI do player. Não há abertura automática de MP3 parcial nem integração com playback.
5. Fluxo Brave/Chromium, navegação interna opcional, edições Lite/Completa, extensão e instaladores/ownership; YouTube permanece separado. Sem instalação, Registry, perfis, mídia pública ou aprovação de release neste incremento.

# Conversão local opcional de downloads

**Estado:** componentes locais, worker de rede diskless e execução assíncrona de R1.2/R1.8 implementados no alvo opcional. Ensaios offline distinguem socket real de transferência falsa; continuam fora do player e sem workflow completo de downloads.

O [ADR-0009](architecture/decisions/0009-download-execution-backends.md) permanece **Proposto**. A sessão compõe [transferência](download-network.md), conversão e publicação; `WindowsDownloadExecution` a mantém em thread privada e já tem ensaio com a [coordenação de jobs](download-jobs.md), ainda com importador falso e fora do player. É uma implementação comum às futuras edições Lite/Completa, sem engine de navegação.

**Restrição do ensaio:** o FFmpeg/ffprobe Gyan 8.1.1 já disponível é usado somente com fixtures sintéticas confiáveis, autenticadas pelo teste. O gate de correções de segurança registrado no ADR não foi fechado; disponibilidade local e testes verdes não aprovam processamento de mídia remota ou não confiável, instalação ou redistribuição do CLI.

## APIs e responsabilidades

| Componente | Contrato deste incremento |
| --- | --- |
| [WindowsDownloadWorkspace](../src/adapters/WindowsDownloadWorkspace.hpp) | `create()` abre uma área nova privada; `inputSink()`/`outputSink()` compartilham a cota; `sealInput()`/`sealOutput()` fecham escrita e tornam os paths internos disponíveis. `validateAndPublish()` exige validação confiável e publica sem sobrescrever. `cleanup()` remove somente identidades pertencentes ao job. |
| [WindowsDownloadProcess](../src/adapters/WindowsDownloadProcess.hpp) | `start()` recebe executável, argumentos, diretório e payload stdin limitado da composição confiável; `poll()` retorna dados limitados e estados; `cancel()` solicita parada do processo próprio. O estado terminal reconhece término do processo e conclusão dos pipes. |
| [FfmpegDownloadCodec](../src/adapters/FfmpegDownloadCodec.hpp) | Constrói vetores de argumentos locais, decodifica JSON de inspeção com campos limitados e interpreta progresso incremental. Não lança processos nem aceita URLs. |
| [WindowsLocalMediaConverter](../src/adapters/WindowsLocalMediaConverter.hpp) | `start(workspace, now_ms)` exige entrada selada e saída vazia; `poll(now_ms)` coordena inspeção, conversão, inspeção de saída e decodificação integral; `cancel()` aguarda parada reconhecida. `Validated` não publica nem importa. |
| [WindowsDownloadJobSession](../src/adapters/WindowsDownloadJobSession.hpp) | Sessão de um único job no controlador worker; exige reconhecimento nativo de parada do I/O antes de selagem, conversão, cleanup ou terminal. Projeta `DownloadExecutionUpdate`, mas não implementa a porta de UI. |
| [WindowsDownloadExecution](../src/adapters/WindowsDownloadExecution.hpp) | Porta rápida com thread/event loop Qt privado; admissão/snapshot limitados e cancelamento/shutdown sem aguardar I/O. `stopped()` reconhece teardown; importação e journal são separados. |
| [WindowsDownloadTlsRuntime](../src/adapters/WindowsDownloadTlsRuntime.hpp) | Ativação one-shot, fail-closed, do backend TLS restrito em processo novo de rede; verifica artefatos/runtime e conserva pins somente leitura. Não troca TLS no player. |
| [DownloadWorkerProtocol](../src/adapters/DownloadWorkerProtocol.hpp) | Pedido binário privado de uma operação e frames limitados; valida campos, versão, truncamento e EOF explícito. Não substitui policy de URLs/IP nem autenticação pré-launch. |
| [WindowsProcessDownloadIo](../src/adapters/WindowsProcessDownloadIo.hpp) | Proxy no controlador worker; supervisiona um filho diskless por resolução/conexão, backpressure e parada reconhecida. Redirect aguarda o filho anterior antes de iniciar o sucessor. |
| [Worker de rede](../src/adapters/WindowsDownloadNetworkWorkerMain.cpp) | Executável opcional real de DNS/socket, sem mídia no filesystem ou dados do pedido em argv/ambiente. Exige runtime TLS restrito inclusive para DNS/HTTP, sem fallback. |

Construir, usar e destruir os componentes lentos em uma mesma thread worker com event loop Qt, nunca na thread visual. O workspace emprestado deve sobreviver ao conversor. Escritas, flush e operações nativas de arquivos podem bloquear; o `poll()` da sessão não cria timer, worker ou event loop. A porta rápida cria a thread privada, fornece relógio monotônico e mantém lifecycle até terminal. Relógio regressivo é erro, não reinício de deadline.

Os pais de staging/publicação e executáveis são configuração da composição confiável. `DownloadItem` recebe somente URL/título/tipo validados pela boundary; conteúdo de página/extensão nunca escolhe paths locais, argumentos, ferramentas, quotas ou configuração de runtime. A construção lexical de argumentos locais não constitui autorização para qualquer arquivo do host.

## Área privada, cota e publicação

`WindowsDownloadWorkspace` exige pais locais existentes e confiáveis; aplica DACL privada somente ao filho novo do job, sem alterar permissões dos pais. Nomes do job, parciais e MP3 final são gerados internamente. Pais e arquivos são inspecionados por handles/identidade; reparse points e substituições incompatíveis são recusados. A proteção não torna confiável outro processo do mesmo usuário, nem autoriza ignorar o gate de sandbox.

O limite padrão e máximo é **512 MiB agregados de bytes lógicos da entrada mais saída**, não 512 MiB para cada arquivo. Uma entrada que consome a cota inteira não deixa espaço para converter. O orçamento controla bytes escritos nestes sinks; não promete medir clusters alocados, metadados NTFS, todos os arquivos do host ou memória total do processo proprietário.

`inputPath()` e `outputPath()` são caminhos internos de parciais selados, não mídia concluída. `plannedPublicationPath()` é apenas um nome interno antecipado, nunca confirmação de sucesso. O contrato de publicação exige entrada/saída seladas, validação confiável, flush e rename nativo com `ReplaceIfExists = FALSE`, verificando a identidade e o caminho final antes de devolver um path publicado. Colisão ou publicação incerta falham sem substituir um arquivo existente.

Os ancestrais e staging negam compartilhamento de escrita/remoção. A folha de publicação compartilha escrita para permitir a operação nativa, mas continua negando remoção/rename do diretório. Depois do rename absoluto, o path normalizado com GUID obtido do handle do arquivo precisa corresponder exatamente ao parent canônico pinado mais o UUID; resultado incerto é removido pelo handle DELETE já adquirido, sem reabrir o nome antigo. O teste introduz uma junction temporária durante a validação e exige recusa sem saída externa. Isso não é uma sandbox contra todo processo local hostil. Contratos nativos: [rename](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info), [path final por handle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew).

`validateAndPublish()` recebe uma função `bool(const QString&)` **confiável**. Esse `bool` não codifica prova criptográfica, capacidade de validação ou autoridade de um chamador não confiável. `WindowsDownloadJobSession` vincula o path selado ao seu próprio conversor em `Validated`, sem aceitar um `true`, path ou estado informado pela página. Essa composição existe no módulo opcional, ainda não no app.

`cleanup()` verifica as identidades dos arquivos e diretório próprios; não é remoção recursiva de um path arbitrário. Preserva o MP3 cuja publicação já foi reconhecida e pode informar falha de limpeza. Cancelamento/erro não significam que qualquer parcial já foi removido: o proprietário deve primeiro aguardar a parada reconhecida do subprocesso e depois verificar a limpeza.

## Subprocesso e limites

Não há concatenação de shell ou busca implícita de executáveis em `PATH`. A composição fornece caminhos absolutos existentes e argumentos separados. O processo usa `CREATE_NO_WINDOW`, sem abrir terminal, herda apenas os handles de stdin/stdout/stderr previstos e pertence a Job Object próprio. O fechamento do Job encerra seus filhos; não são encerrados processos alheios.

O Job limita a memória de processo/job a **512 MiB por padrão** e restringe descendentes. Esse limite nativo não limita a memória do player/worker proprietário e **não restringe permissões de rede ou sistema de arquivos**. Não constitui sandbox de conteúdo hostil. O deadline padrão é **30 minutos**, compartilhado entre todas as etapas do conversor, sem renovar o total a cada processo.

O Job permanece limitado a **um processo**, sem breakaway. Não usar este supervisor para um worker integral que depois lança FFmpeg: o limite ancestral impediria a criação da ferramenta. O controlador deve manter o workspace e supervisionar separadamente rede e conversão; ampliar a árvore exige ownership/handles e reconhecimento próprios, não apenas observar `ActiveProcesses == 0`. Esta ampliação não faz parte do incremento.

`standard_input` permite uma única mensagem opaca de até **256 KiB**, enviada por pipe nativo com buffer fixo e backpressure, seguida de EOF. Entrada vazia conserva stdin em NUL. O supervisor não interpreta JSON/framing, não coloca esse payload em argumentos ou ambiente e não constitui um host Native Messaging. O proxy abaixo valida seu próprio protocolo e exige ambiente explícito; a composição de artefatos/ambiente revisados de produção continua gate, não uma allowlist aplicada ao player.

`environment` opcional substitui a herança por valores de composição confiável. Exige `PATH` explícito (pode ser vazio) e `SystemRoot` não vazio: Qt/Windows herdaria essas chaves do pai se faltassem. Aceita até 128 entradas e 32.760 unidades UTF-16 no bloco lógico, incluindo separadores/terminadores; recusa NUL e `=` em nomes antes de criar recursos. Não é limite de todas as alocações Qt. `FFREPORT` e overrides de cor continuam removidos em ambos os modos. `nullopt` preserva a herança filtrada anterior. Valores, diretório de trabalho, DLLs e executável ainda precisam de composição autenticada; não há allowlist de produção aplicada ao player por existir esta opção.

Pipes nativos têm buffers fixos/backpressure; `poll()` entrega no máximo **64 KiB somando ambos os canais**. A conversão limita stdout a 512 MiB e stderr a 4 MiB, além da cota agregada do workspace. As inspeções limitam JSON a 16 KiB; a validação final não espera stdout útil. Limite/deadline/cancelamento solicitam encerramento do Job e continuam em `Stopping` até reconhecer processo encerrado e pipes concluídos.

Saídas raw de ferramentas não vão para UI/log. O CLI executa em modo quiet; a conversão usa stderr apenas para o protocolo limitado `-progress pipe:2`, sem transformar diagnósticos, tags, URLs ou paths em mensagens públicas. A projeção futura deve expor somente estados, contadores e erros tipados/redigidos; não há integração de UI/log neste recorte.

## Runtime TLS no futuro processo de rede

`WindowsDownloadTlsRuntime::activate(config)` deve ser chamado após criar `QCoreApplication`, antes de qualquer uso TLS e somente no novo processo de rede. A configuração é de composição confiável: paths locais absolutos de Core/Network/plugin e seus SHA-256, nunca campos da extensão, página ou ambiente. Recusa namespaces, UNC, ADS, componentes ambíguos, reparse/ancestrais reparse e artefatos vazios/acima de 128 MiB. Hash usa leitura limitada; handles somente leitura/share-read de arquivos e ancestrais permanecem retidos durante a vida do objeto.

Depois de esvaziar `libraryPaths`, valida Qt 6.11.2 e relaciona endereços de código/metaObject dos módulos carregados aos arquivos pinados por volume/ID/tamanho/path final. Recusa backend já instanciado, carrega o plugin absoluto e exige exclusivamente `rcmusic-schannel-offline`, selecionado e com suporte TLS. Falha deve encerrar o worker, sem retry/fallback; o loader não altera confiança/CA padrão, não oferece opções de relaxamento e não recebe URLs.

Import DLLs entram antes de `main`: verificar paths/arquivos após o carregamento **não prova sozinho os bytes originalmente mapeados**, nem previne um carregamento inicial indevido. O futuro launcher/runtime deve autenticar e conservar os artefatos antes de criar o processo, controlar DLL search, ambiente/WD e manter o loader vivo. O executável diskless abaixo existe no alvo opcional, mas não implementa essa autenticação pré-launch nem composição no player. Testes de ativação não reexecutam handshake, CRL/OCSP/CTL, sandbox ou gates de distribuição do [ADR-0009](architecture/decisions/0009-download-execution-backends.md).

## Rede diskless e protocolo privado

`rc_music_download_network_worker` recebe exatamente um pedido binário versionado, de até 256 KiB, pelo stdin privado seguido de EOF; recusa argumentos adicionais. O pedido contém pins confiáveis do runtime e uma operação de resolução ou um endpoint numérico/Host/request-target. Os campos têm comprimentos limitados, UTF-8 exato e enums fechados; não aceita comandos, paths de mídia ou execução arbitrária enviados por página. O loader permanece vivo até a saída. Resolução numérica não chama `QHostInfo`, evitando PTR implícito; nomes usam lookup assíncrono. Respostas DNS preservam todos os endereços para a policy comum recusar conjuntos mistos, sem aprovar o primeiro isoladamente.

Saída é apenas framing binário: comprimento little-endian de quatro bytes, tipo e corpo de até **32.769 bytes**; cada Data contém no máximo **32 KiB**. O decoder retém um frame, até 32.769 bytes de corpo mais quatro de header lógicos, sem contar overhead de alocação. DNS entrega Addresses e Terminal; stream entrega Data e Terminal. `authenticated_eof` é uma observação explícita do transporte, nunca inferida de exit code zero. Diagnósticos livres Qt são suprimidos; qualquer stderr observado pelo proxy é erro, não texto de UI/log.

`WindowsProcessDownloadIo` exige ambiente explícito, argv/stdin inicialmente vazios e configurações confiáveis. O controlador retém no máximo um bloco raw de 64 KiB e um slot legível de 32 KiB, além do decoder; não continua drenando o filho quando o consumidor segura esse slot. Pipe bloqueado aplica backpressure ao processo diskless, que o Job/deadline pode encerrar. Cota separada limita bytes de mídia no fio; stdout admite a razão máxima de seis bytes de framing/payload por byte útil mais 4.096 de controle, com aritmética verificada. Isso limita retenção/contagem nessas fronteiras, não todas as alocações Qt/Windows.

Sucesso de DNS/EOF exige sequência válida, Terminal sem erro, exit zero e encerramento nativo com todos os pipes concluídos. Truncamento, controles duplicados, dados fora da ordem, excesso e stderr falham fechados. Redirect permite um único sucessor pendente, só iniciado após reconhecer a parada do anterior; cancelar também remove esse sucessor. `failure()` não avança I/O e conserva a primeira causa; `pollStop()` avança a drenagem, sem confundir erro com reconhecimento. O worker não recebe workspace ou arquivo de publicação; o controlador conserva ownership do disco e só inicia conversão depois dessa parada.

Cancelamento não apaga uma saída natural já ocorrida: supervisor observa o handle sem bloquear e conserva exit code/bytes pendentes; proxy valida Terminal/exit mesmo depois de descartar o slot legível. Antes de admitir o próximo destino, também avança e verifica a saída anterior, sem substituir essa prova ao iniciar outro filho. Um filho ainda vivo encerrado pelo cancelamento pode ter IPC truncado por essa ação própria; continua exigindo ACK nativo, mas não é falsamente classificado como uma saída natural inválida. Nenhuma dessas distinções permite liberar processo/pipes ainda ativos.

São componentes worker-thread, não uma porta rápida ou um launcher autenticado de produção. O endpoint de baixo nível admite loopback apenas para testes controlados; `DirectDownloadSession` mantém a recusa de redes privadas em produção. Não há imports pré-main autenticados, sandbox completa, host ou downloads habilitados no app.

## Inspeção, conversão e conclusão

O fluxo local possui quatro etapas obrigatórias:

1. `ffprobe` inspeciona a entrada selada, selecionando `a:0` e apenas campos de formato/codec/duração/taxa/canais. JSON inválido, codec/contêiner fora do contrato ou ausência de áudio são erros tipados.
2. FFmpeg transforma o primeiro áudio em MP3 `libmp3lame` 192 kbit/s, uma thread de áudio, sem vídeo, legendas, data streams, tags ou capítulos. Produz bytes em `pipe:1`, escritos pelo proprietário no sink limitado; não recebe o path final de publicação.
3. `ffprobe` exige que a saída selada tenha contêiner **e codec MP3**, com perfil/taxa/canais aceitos.
4. FFmpeg decodifica integralmente o áudio MP3 local em saída nula, com erros fatais. Somente código de saída zero, pipes concluídos e validação de todas as etapas permitem `Validated`.

Os vetores restringem protocolo de entrada a `file`, formatos de entrada ao conjunto explícito, probe a 1 MiB, análise a 5 s e streams a 16. Para ISO BMFF/MP4, referências externas/paths absolutos do demuxer são desabilitados e capítulos ignorados. Esses controles reduzem superfície; não provam ausência de todos os acessos locais possíveis em demuxers e não substituem o CLI corrigido nem sandbox. **Nenhuma URL remota é entregue a FFmpeg/ffprobe.**

O decoder `DownloadProgressCodec` limita total a 4 MiB, bloco a 16 KiB e linha a 1.024 bytes, recusa chaves duplicadas/linhas inválidas e exige contador monotônico. `progress=end` não é prova de êxito do processo, conteúdo correto ou publicação. Da mesma forma, `duration_us` é **estimativa opcional obtida dos metadados**, não um total garantido: o contador pode ultrapassá-la. Não converter esse valor em 100% concluído antes das validações e publicação reconhecida.

Os formatos/codec aceitos por esta inspeção não ampliam automaticamente a matriz pública de playback, a lista de fontes de download ou a compatibilidade com sites. MP4 sem áudio é recusado, não convertido em faixa vazia.

## Composição do job no controlador

`WindowsDownloadJobSession` é one-shot: `start(job_id, item, now_ms)`, `poll(now_ms)` e `cancel()`, na mesma thread worker. Configuração confiável fornece pais, executáveis e limites; ID é ASCII limitado e URL exige UTF-8 exato/policy compartilhada. Título/tipo não escolhem paths, comandos ou codec. A sessão mantém o workspace enquanto seus subprocessos de conversão são encerrados; não transfere ownership para um filho que pode ser morto.

A fase de conversão não usa duração estimada como total. `Publishing` fica visível em um poll como 0/1, sem path, antes do commit; somente publicação validada e cleanup bem-sucedido devolvem `Published` 1/1. Cancelamento reentrante é revalidado após callbacks de I/O e antes de selagem/conversão/publicação. A cota agregada preserva `LimitExceeded`, mesmo quando o transporte observa apenas escrita recusada. Estados terminais são estáveis e a sessão não reinicia outro job.

Há um deadline observado único; o conversor recebe somente o tempo restante, sem renovar o total. Relógio regressivo falha, inclusive durante drenagem. Flush/rename nativos podem bloquear entre polls: isso não é um hard deadline dentro dessas chamadas. `DirectDownloadIo::cancel()` solicita parada; `pollStop()` precisa reconhecer todo o trabalho nativo/pipes com `stopped=true` antes de selagem/conversão, cleanup ou terminal. Erro/exception sem esse reconhecimento conserva ownership e `Running`, sem path ou erro público prematuro. A primeira causa permanece; código desconhecido vira `InvalidResult`. `QtDownloadIo` in-process retorna `false/Unavailable`: invalidar callback não encerra comprovadamente a thread DNS. O proprietário deve fazer polling até terminal antes de destruir a sessão; RAII best-effort não é reconhecimento. Não adaptar esta sessão diretamente como uma porta rápida.

`DirectDownloadIo::failure()` é um getter puro da falha tipada já observada, sem iniciar I/O nem avançar processos. O transporte o consulta antes de cancelar, para não apagar `Timeout`, `LimitExceeded`, `InvalidResult` ou `Unavailable` do adapter sob um `Network` genérico. `None`/getter com exception conserva fallback `Network`; enum desconhecido vira `InvalidResult`. Falhas de policy, sink, HTTP e deadline próprios conservam suas causas, sem liberar parada por observar um erro.

Rename é o commit. Cancelamento/clock observado depois dele não deve esconder um arquivo final já criado. Se o cleanup posterior falhar, a sessão retorna `Failed/Publication`, sem path importável, preserva arquivos desconhecidos e disponibiliza `retainedPublicationPath()` apenas para recuperação confiável. A [coordenação](download-jobs.md#regressão-de-cancelamento-de-2026-10-01) já preserva falha tipada durante cancelamento, sem importar publicação tardia. Persistência dessa identidade continua gate; não há recuperação automática nem promessa de apagar todos os parciais em falha.

O [teste da sessão](../tests/qt/WindowsDownloadJobSessionTests.cpp) usa `DirectDownloadIo` falso, DNS público aprovado e framing HTTP sintético com as fixtures autenticadas abaixo. Filesystem, FFmpeg/ffprobe, inspeção/decodificação e publicação são reais. Não há socket, DNS/TLS real ou exceção de localhost na policy de produção neste teste; a composição completa de rede isolada e importação continua pendente.

O [teste de composição com subprocesso](../tests/qt/WindowsProcessDownloadJobTests.cpp) acrescenta o proxy/supervisor reais e um helper exclusivo de teste que simula DNS/HTTP com as mesmas fixtures autenticadas. Comprova bytes recebidos, MP3 e MP4→MP3 reais, parada antes de conversão/publicação, limpeza e preservação de um arquivo existente. Não é o worker de socket real nessa jornada. O [teste do worker real](../tests/qt/WindowsDownloadNetworkWorkerTests.cpp) verifica separadamente DNS literal offline, HTTP em loopback, request-target assinado, runtime exigido e falhas; não comprova download/conversão com DNS público, handshake TLS real ou mídia da internet.

## Execução assíncrona Windows

`WindowsDownloadExecution` implementa `DownloadExecution` sem aguardar filesystem, processo, tarefa ou destruição no caller. Factory confiável cria **somente objetos inertes**, sem I/O, processo ou arquivo de mídia ativo antes de `WindowsDownloadTask::start`; failure/null não podem reter recursos. A composição concreta possui I/O antes de sessão, destruindo sessão antes de I/O no worker. Nenhuma chamada lenta ocorre sob mutex.

Há um pedido pendente, um snapshot coalescido e um wake enfileirado no máximo. ID ASCII tem até 64 bytes; URL/título exigem UTF-8 exato, até 8.192/800 bytes e policy lexical sem DNS. Paths de snapshot/recuperação são limitados a 30.000 unidades nativas Windows antes da cópia ao caller, não limitando alocações internas da tarefa confiável. Terminal é estável; somente `poll` do ID correspondente observa ACK e libera novo start. Poll/cancel antigos são inertes.

`start` da porta devolve `false` apenas por recusa local sem trabalho/recursos para o pedido. Factory falha após admissão gera `Failed/Unavailable`, sob o contrato inerte. `start` falso da tarefa exige cancelamento/drenagem: sem malfunction, conserva `Failed.error` reconhecido; terminal sem falha explicativa gera `InvalidResult`. Exception/DTO inválido mantém causa técnica `InvalidResult`, sem liberar ownership antes de ACK válido e destruição da tarefa. Progresso intermediário pode ser coalescido; serviço observa `Publishing` antes do callback de importação.

`shutdown()` fecha admissão prontamente; `stopped()` exige término da thread após drenagem/destruição. `DownloadService::stopped()` reconhece somente seu job: composição deve fechar **ambos** e observar ambos antes de destruir na UI. Join do destrutor é fallback fora da UI e pode bloquear se a tarefa violar o contrato, sem detach/término forçado ou timeout inventado como ACK. Guard impede passos reentrantes em event loops aninhados de tarefas/callbacks.

Publicação que vence cancelamento permanece `Published` no executor, sem apagar/importar o final; serviço cancelado não importa. Cleanup após rename conserva `Failed/Publication` e path privado. Evidência limitada do job atual não é autoridade de importação, journal durável ou recuperação após restart: próximo start aceito a substitui. Ownership/persistência dos finais ainda precisam de composição antes de habilitar entradas externas.

## Build opcional e ensaio local

[RcMusicDownloadNetwork.cmake](../cmake/RcMusicDownloadNetwork.cmake) acrescenta o codec à biblioteca opcional `rc_music_download_network` e os adapters/protocolo Windows, além do executável `rc_music_download_network_worker` somente em Windows. `RC_MUSIC_BUILD_DOWNLOAD_NETWORK` permanece OFF por padrão e não liga esta biblioteca ou worker ao player. Não introduz download de dependências, instalação, host ou pacote.

Com `BUILD_TESTING=ON`, entram os testes de codec/workspace/processo/protocolo/worker/proxy e helpers exclusivos dos testes. Os alvos `rc_music_WindowsLocalMediaConverter_tests`, `rc_music_WindowsDownloadJobSession_tests` e `rc_music_WindowsProcessDownloadJob_tests` só existem quando **ambos** os caches `RC_MUSIC_DOWNLOAD_TEST_FFMPEG` e `RC_MUSIC_DOWNLOAD_TEST_FFPROBE` são informados, absolutos e existentes; configuração inválida falha. Os caches não selecionam um conversor de produção. Com `BUILD_TESTING=OFF`, helpers, fixtures e testes não fazem parte do grafo, mas o worker real permanece alvo de produção opcional.

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
cmake --build out/product/browser-integration-tests --target rc_music_FfmpegDownloadCodec_tests rc_music_WindowsDownloadWorkspace_tests rc_music_WindowsDownloadProcess_tests rc_music_WindowsLocalMediaConverter_tests rc_music_WindowsDownloadJobSession_tests rc_music_WindowsProcessDownloadJob_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de conversão falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.download-network\.(FfmpegDownloadCodec|WindowsDownloadWorkspace|WindowsDownloadProcess|WindowsLocalMediaConverter|WindowsDownloadJobSession|WindowsProcessDownloadJob)$' --output-on-failure
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

### Incremento de composição worker-only

Em 2026-10-01, teste/stub confirmou Red **16 passagens/13 falhas**; regressões adicionais de cleanup/cancelamento, estabilidade e exceção de I/O confirmaram **16 passagens/18 falhas**. Green após refactor passou **34/34 casos Qt, zero falhas/skips**, com MP4-AAC e MP3 sintéticos autenticados, conversor/decoder reais e publicação Windows. Revisão independente não deixou achados acionáveis neste escopo.

O fechamento Release executou efetivamente **42/42 CTest** de admissão/jobs/transporte/conversão em **13,43 s**, incluindo o supervisor 27/27 e a sessão 34/34. O workspace conserva dois skips internos de symlink pela política do host; o teste de junction passou. Build `BUILD_TESTING=OFF` compilou a nova sessão, registrou zero testes e seu grafo excluiu helpers, fixtures, paths do CLI, Qt Test/Catch2, Multimedia e WebEngine. Core local foi recompilado, mas seus testes, JavaScript e UI não foram reexecutados por este incremento; não se trata de validação de release ou das duas edições instaladas.

### Incremento de ambiente explícito

Em 2026-10-01, Red **28 passagens/10 falhas** confirmou herança de sentinel sintético e admissão de ambientes inválidos. Green final **42/42 casos Qt, zero falhas/skips** inclui os 27 anteriores e 15 novos: ausência de herança de segredo/plugin-path, filtros, limites exatos, nomes case-insensitive, `PATH` vazio com DLL de teste adjacente e reuso explícito→legado. Seleção proporcional do serviço/admissão/supervisor passou **42/42 CTest** em 4,58 s. Build Release, formato, documentos/rastreabilidade e revisão independente passaram. Testes não alteram o ambiente persistente, SDK ou runtime do player; somente filhos e temporários próprios são usados.

### Incremento de loader TLS

Em 2026-10-01, o stub recusou corretamente as entradas inválidas, mas falhou nos contratos de pins/identidade/ativação: Red válido **23 passagens/17 falhas**; Green **40/40 casos Qt, zero falhas/skips**. Helper usa processo novo e JSON privado por stdin; plugin real é opt-in por `RC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN`, com paths/hashes de fixtures de runtime somente nos testes. Sem essa fixture, os casos dependentes ficam explicitamente skipped, não comprovados. `BUILD_TESTING=OFF` não recebe esses paths/hashes/helper.

Após o configure opcional descrito acima, para incluir a fixture restrita já construída localmente:

```powershell
$rcTlsPlugin = "$PWD/out/product/download-tls/rcmusic-schannel-offline-20261001-03/build/plugins/tls/qrcmusicschanneloffline.dll"
cmake -S . -B out/product/browser-integration-tests "-DRC_MUSIC_DOWNLOAD_TEST_TLS_PLUGIN=$rcTlsPlugin"
if ($LASTEXITCODE -ne 0) { throw 'Configure da fixture TLS falhou.' }
cmake --build out/product/browser-integration-tests --target rc_music_WindowsDownloadTlsRuntime_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build do loader TLS falhou.' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.download-network\.WindowsDownloadTlsRuntime$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest do loader TLS falhou.' }
```

Fechamento Release MSVC 19.44/Qt 6.11.2: seleção `^rc_music\.(download-network\.|downloads\.|browser\.)` passou **51/51 CTest** em 20,80 s, incluindo 36 casos do serviço e 15 alvos Qt; os dois skips de symlink do workspace permanecem. Build OFF passou, registrou zero testes e seu grafo excluiu Qt Test/Catch2/helpers/fixtures/paths CLI/Multimedia/WebEngine. Formato, documentos/rastreabilidade e revisão independente foram checks separados. Core, JavaScript e UI não foram reexecutados; o core foi compilado no build ON, sem alegar teste atual. Nenhum novo runtime/instalador, benchmark, instalação ou mídia da internet.

**Incidente e recuperação do fixture:** na tentativa inicial, um junction de teste apontou para `sdk/bin` e a limpeza Qt seguiu o link, removendo 191 arquivos desse SDK local. A execução foi interrompida; as cópias originais de cada proprietário foram verificadas e somente os arquivos ausentes foram restaurados, sem overwrite. Conferência integral confirmou **9.880/9.880 arquivos**, todos os SHA-256 originais, **2.400.301.667 bytes** e inventário `4e90550253cb4e07a579bf7706d1b1d68736e4999ab454da7171a2c73d428933`. O primeiro resultado parcial não serve como evidência TDD válida. O fixture corrigido só aponta para outro diretório temporário próprio, desabilita cleanup recursivo até unlink nativo reconhecido e verifica sobreviventes no alvo; nunca aponta SDK ou arquivos pessoais. Receipts/evidências históricos não foram reescritos.

### Incremento de rede diskless

Em 2026-10-01, Reds válidos reproduziram protocolo ausente (**3 passagens/8 falhas**), worker stub (**2/17**) e proxy stub (**2/25**). Drenagem começou com sessão de transferência **18/1** e job **34/16**; erro enum desconhecido teve **51/1**. A composição com subprocesso/conversor revelou perda da causa tipada (**6/2**): timeout e framing inválido viravam `Network`. Regressões específicas confirmaram transferência **21/37** e job **52/8** antes do getter puro/captura pré-cancelamento. Um caso adicional de cancelamento após saída natural sem Terminal já era verde, portanto não é apresentado como Red ou correção nova.

A revisão final demonstrou que a reavaliação de `read()` por QTRY naquele caso podia ocultar a corrida real. A sequência poll→leitura exata→cancel reproduziu Red **28 passagens/2 falhas** por Terminal ausente/exit não zero; a sucessão imediata do redirect reproduziu **31/2**, mantendo o caso válido verde. O fixture aguarda bytes disponíveis antes da espera de saída, pois stdin só é enviado pelo polling. No supervisor, evento/handle testemunham saída nativa 0/17 antes de cancelar, sem polling terminal: Red **42/2**, Green **44/44**. A correção conserva saída natural e a valida antes de aceitar sucessor, sem remover a possibilidade de cancelamento de filho vivo.

Green final: protocolo **11/11**, worker **19/19**, proxy **33/33**, supervisor **44/44**, transferência **58/58**, job **60/60** e composição com subprocesso **8/8**, sem skips nessas suites. O worker real usa DNS literal/HTTP loopback; os oito casos do job usam transferência simulada no filho e filesystem/conversor reais, não internet. Publicação não sobrescreveu o arquivo existente do fixture e staging foi limpo nos terminais exercitados. Estes testes não fecham autenticação pré-launch, sandbox, TLS completo, importação ou edição Completa.

Build Release MSVC 19.44/Qt 6.11.2 e seleção `^rc_music\.(download-network\.|downloads\.|browser\.)` passaram **55/55 CTest** em **35,01 s**: 36 casos do serviço e 19 alvos Qt. Dois skips internos de symlink do workspace persistem pela política do host; não são falhas nem casos comprovados. Build OFF compilou biblioteca e worker real e registrou zero testes; helpers, fixtures, paths do CLI, Qt Test/Catch2, Multimedia e WebEngine não compõem esse grafo. Formato dos 20 C++ afetados, documentos/rastreabilidade e revisão são verificações separadas. Core foi compilado, mas seus testes, JavaScript e UI não foram reexecutados. Nenhum benchmark, runtime/instalador novo, instalação/Registry, perfil pessoal ou download público foi executado.

### Incremento de execução assíncrona

Em 2026-10-01, a porta vazia reproduziu Red **8 passagens/20 falhas**; Green inicial **28/28**. Regressão adicional demonstrou `start` falso ocultando falha tipada de configuração/publicação/rede: Red **28 passagens/3 falhas**, depois separação de recusa e malfunction. Com progresso/terminal e watchdog que não mascara bloqueio do caller, Green final **32/32** casos Qt. Gates controlados bloqueiam factory/start/poll/cancel/destruição e exigem que chamadas UI retornem enquanto o gate continua fechado, sem benchmark. Um guard protege passos contra eventos Qt aninhados; refactor manteve a suíte verde.

Composição assíncrona passou **10/10** casos Qt, preservando oito anteriores e acrescentando lote MP3+MP4→serviço/importação única no replay e shutdown com workspace ativo sem executar o sucessor/importar parcial. DNS/transferência em helper e importador são falsos; supervisor/processos, arquivos, FFmpeg/ffprobe, inspeção/decodificação integral e publicação são reais. Não é importação no player nem ensaio de socket/TLS público.

Build Release MSVC 19.44/Qt 6.11.2 e CTest focado passaram **41/41 em 18,69 s**: serviço **36/36** e cinco alvos Qt (execução **32**, composição **10**, supervisor **44**, proxy **33**, sessão **60** casos), sem skips. OFF compilou biblioteca/worker real, listou zero testes e excluiu Qt Test/Catch2/helpers/fixtures/CLI/Multimedia/WebEngine do grafo. C++ formatado, documentos/rastreabilidade, diff e revisão são checks separados. Não reexecutar worker/socket/TLS, JavaScript, core independente ou UI sem mudança relevante; seu histórico não é validação atual. Não houve runtime/instalador novo, benchmark, instalação/Registry, perfil ou mídia pessoal/pública.

Na árvore ON já configurada com ferramentas sintéticas explícitas, e na sessão MSVC documentada:

```powershell
cmake --build out/product/browser-integration-tests --target rc_music_WindowsDownloadExecution_tests rc_music_WindowsProcessDownloadJob_tests rc_music_WindowsDownloadJobSession_tests rc_music_WindowsDownloadProcess_tests rc_music_WindowsProcessDownloadIo_tests rc_music_downloads_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de execução assíncrona falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.(downloads\.|download-network\.(WindowsDownloadExecution|WindowsProcessDownloadJob|WindowsDownloadJobSession|WindowsDownloadProcess|WindowsProcessDownloadIo)$)' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Checks de execução assíncrona falharam' }
cmake --build out/product/download-network-release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build opcional OFF falhou' }
ctest --test-dir out/product/download-network-release -N
```

## Gates que continuam abertos

1. CLI corrigido, artefato/proveniência/hashes e revisão dos avisos antes de aceitar mídia não confiável; licenças, notices, fontes correspondentes e distribuição do conjunto exato.
2. Sandbox efetivo de rede/sistema de arquivos para parser/conversor; Job Object e `-protocol_whitelist file` não fecham esse gate. Avaliar todos os acessos indiretos e limites nativos, sem diminuir verificação.
3. Autenticar e conservar artefatos antes de criar o worker e controlar busca de DLLs/ambiente/WD. Worker/proxy e porta rápida existem fora do app; ensaios separados não comprovam jornada inteira DNS/TLS real→publicação/importação ou todos os caminhos auxiliares de cadeia.
4. Importador local, persistência/recuperação e projeção de jobs, host Native Messaging e UI do player. Não há abertura automática de MP3 parcial nem integração com playback.
5. Fluxo Brave/Chromium, navegação interna opcional, edições Lite/Completa, extensão e instaladores/ownership; YouTube permanece separado. Sem instalação, Registry, perfis, mídia pública ou aprovação de release neste incremento.

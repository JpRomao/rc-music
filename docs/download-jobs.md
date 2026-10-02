# Coordenação opcional de downloads

**Estado:** incremento R1.8 em desenvolvimento. Coordenação, execução Windows opcional e importação local têm componentes próprios; ainda sem workflow de download ligado ao player, tela QML de jobs ou host instalado.

Contratos: [requisitos FR-051–053](product/requirements.md#26-extensões-navegação-e-fontes-remotas-planejadas), [ADR-0008](architecture/decisions/0008-download-job-service.md), [avaliação de execução ADR-0009](architecture/decisions/0009-download-execution-backends.md). O pedido completo continua incluindo downloads reais/conversão/importação, ambas as edições e instaladores; esta camada não os substitui.

## Responsabilidades implementadas

[DownloadService](../include/m0/application/DownloadService.hpp) é C++ puro, separado de reprodução e Qt. Implementa a porta de admissão e mantém uma fila compartilhada por canais de navegador que usam a mesma instância. Fechar o canal não apaga o pedido nem gera outro job. Repetição do mesmo UUID/lote devolve IDs anteriores; lote diferente sob o mesmo ID é recusado. UUIDs não distinguem caixa. A composição deve gerar um prefixo de IDs confiável novo por execução.

Admissão copia o lote inteiro e não inicia I/O. O serviço conserva recibos, sem expulsão, até ser destruído; limita inicialmente 128 jobs, 64 pedidos e 1 MiB de strings de pedidos (ID/URLs/títulos, não o overhead de containers/snapshots). Ao atingir um teto, recusa novas admissões sem perder as anteriores. Esses limites são ajustáveis por composição confiável, não por mensagem da página; a admissão staged também respeita os tetos fixos do checkpoint. Recuperação histórica e admissão durável têm componentes abaixo, mas sua composição instalada/limpeza acionável continuam pendentes; não anunciar deduplicação entre processos independentes.

[DownloadExecution e DownloadImport](../include/m0/ports/DownloadExecution.hpp) separam execução e importação. [WindowsDownloadExecution](../src/adapters/WindowsDownloadExecution.hpp) já implementa a porta rápida fora do app, mantendo tarefa, I/O e destruição em thread Qt privada. O [ensaio assíncrono MP3/MP4](download-conversion.md#incremento-de-execução-assíncrona) liga execução real ao serviço com transferência sintética e importador falso; filesystem, processos, conversor, validação e publicação são reais. O importador local descrito abaixo é outro componente: esses ensaios não comprovam a composição completa com player/host ou downloads disponíveis ao usuário.

- Uma execução ativa, fila FIFO. Fases de download, conversão e publicação têm progresso próprio; total desconhecido mantém indicador indeterminado.
- Cancelamento de item na fila é imediato; em execução aguarda confirmação terminal. Timeout total inicial de 30 minutos também pede cancelamento, sem presumir que o worker parou. Nenhum próximo trabalho começa antes de confirmar a saída do ativo.
- Cancelamento/shutdown não ocultam falha de cleanup: `Failed/Publication` permanece falha acionável. A primeira causa observada durante a drenagem (inclusive timeout, relógio regressivo ou exceção de polling) é preservada. ACK terminal não precisa repetir contadores anteriores; payload malformado falha como `InvalidResult`, estado desconhecido não libera o slot e publicação tardia nunca importa arquivo.
- Resultado obsoleto, inválido ou contraditório não importa arquivo; exceção não implica sucesso. Shutdown recusa novos pedidos e cancela/drena os próprios jobs, sem tocar na reprodução local. A composição deve aguardar `stopped()` antes de destruir adapters emprestados; o destrutor não encerra processos externos por conta própria.
- Apenas resultado publicado com path absoluto normalizado `.mp3` é encaminhado à importação. Essa checagem do core não valida bytes: o executor deve provar formato/áudio e publicação sem sobrescrita. Falha de importação preserva o arquivo e permite retry explícito pelo mesmo job, sem repetir download; [PlayerDownloadImport](../src/presentation/PlayerDownloadImport.hpp) faz o handoff append-only à fila local, sem autoplay.

[DownloadJobsModel](../src/adapters/DownloadJobsModel.hpp) expõe títulos, estados, progresso, erro tipado e ações por ID para a futura UI Qt/QML. Não oferece papel de URL/query. `refresh()` não executa trabalho ou avança relógio; inserções/atualizações preservam índices existentes. Texto de página continua não confiável: a futura QML deve usar texto simples, não RichText/HTML. A projeção não é uma tela já disponível no RC Music.

## Importação local de resultado publicado

`PlayerDownloadImport::accept(job_id, mp3_path)` é um handoff C++ confiável, executado na thread da aplicação sobre o [PlayerPresenter](../src/presentation/PlayerPresenter.hpp). Não é comando de página nem método invocável por QML. A composição entrega somente um `Published` confiável, com arquivo final já validado/publicado pelo executor. O importador faz verificações lexicais, mas não abre arquivos, consulta filesystem, decodifica áudio, descobre fontes ou inicia leitura de metadata; tampouco autentica bytes, sandbox ou ownership do arquivo.

O append cria uma ocorrência no fim da fila existente, sem limpar/reordenar itens anteriores, alterar seleção/contexto/posição ou chamar `load`, `play` ou outros comandos do backend. Mesmo numa fila vazia, não escolhe automaticamente a nova faixa. Reprodução e metadata seguem o fluxo local normal somente quando uma ação posterior seleciona a ocorrência; eventos de playback já em curso continuam sob suas próprias regras após a transação.

O recibo liga o ID do job ao path aceito durante a vida deste importador. Mesmo ID/path retorna sucesso sem criar outra ocorrência, inclusive depois de remoção manual ou limpeza da fila. Trocar o path de um ID já comprometido é recusado. Jobs distintos podem criar ocorrências distintas do mesmo path: não há deduplicação global por arquivo. Os recibos não são expulsos para admitir novos jobs; replay comprometido continua aceito em capacidade cheia e enquanto o presenter vivo está busy/em saída, sem nova mutação. Presenter já destruído recusa todas as chamadas, inclusive replay.

Os limites iniciais de [PlayerDownloadImportLimits](../src/presentation/PlayerDownloadImport.hpp) são **128 recibos** e **1 MiB lógico** de IDs/paths nativos retidos: `id.size() + path.native().size() * sizeof(std::filesystem::path::value_type)`, sem overhead de containers/alocadores. O ID aceita de 1 a 64 caracteres ASCII `[A-Za-z0-9_-]`; no Windows, o path local absoluto/normalizado aceita até 30.000 unidades de código nativas UTF-16, Unicode válido e extensão `.mp3` sem distinguir caixa. NUL, URL, path relativo, UNC, namespaces de dispositivo e alternate streams são recusados. Tetos são inclusivos e ajustáveis só por composição confiável; capacidade zero recusa novos recibos. São quotas independentes dos limites de jobs/pedidos retidos do serviço, não cotas de transferência/disco, journal ou prova de existência do arquivo.

Nova importação é recusada antes do commit enquanto há descoberta/seleção/localização de arquivo ou outra transação ativa, durante saída, no presenter de cenário congelado ou sem owner válido. Caller de outra thread é recusado antes de ler/mutar o presenter. Recusa anterior ao commit não altera a fila, não retém reserva nem consome quota do importador; não registra uma importação concluída e deixa capacidade para retry ou outro job. O serviço conserva `ImportFailed` e permite retry explícito pelo mesmo ID/path, sem baixar novamente.

O [LocalQueueListModel](../include/m0/presentation/LocalQueueListModel.hpp) prepara/valida itens e reserva a projeção antes de executar o callback de commit do owner, que deve ser não lançante. Fila e marca de compromisso do recibo ficam estabelecidas antes das notificações Qt de inserção/estado. Assim, replay reentrante já comprometido não duplica a ocorrência; novo append, mutações da fila/transporte e saída são bloqueados durante a transação. Polling de metadata e eventos de playback ficam adiados até a projeção estar consistente; um `Ended` da geração atual tem prioridade sobre snapshots posteriores dessa geração e não deve ser apagado.

Owner, importador, model e adapters emprestados devem permanecer vivos durante a chamada. Callbacks Qt de linhas não podem lançar exceções nem destruir owner/model durante a transação; não há promessa de rollback para violação dessa precondição. Os recibos são apenas em memória: autosave da fila não é um journal atômico de fila+importação. Persistência/recuperação de jobs, propriedade durável dos arquivos e retry idempotente após restart continuam gates separados. Ter este componente não significa que host, model de jobs e UI de download já estejam ligados ao player.

## Checks locais Windows

### Checkpoint privado e recuperação conservadora

[DownloadCheckpoint](../include/m0/application/DownloadCheckpoint.hpp) é histórico privado limitado, separado da projeção pública. Exporta UUIDs canônicos, fingerprint, IDs, títulos/estados/progresso/erro e paths candidatos locais; não tem campos para URLs/query, cookies, credenciais, histórico de navegação ou deadlines monotônicos. Títulos e paths privados não são criptografados; um título recebido pode conter texto escolhido pela origem. `DownloadRequestIdentity` rápida/emprestada calcula SHA-256 na admissão; o adapter Qt usa o domínio ASCII `RCMusic.DownloadRequest.v1` seguido de NUL, quantidade de itens uint64 big-endian e, por item, tipo em um byte (áudio 0/vídeo 1), URL e título UTF-8 com comprimento uint64 big-endian. UUID é chave separada, não entra no hash. SHA é identidade de replay, não assinatura, criptografia ou proteção contra guessing/correlação offline de URLs conhecidas.

`checkpoint()` não chama ports nem I/O e recusa durante chamada externa, sem identidade ou quando a representação não cabe no schema. `restore()` só aceita owner prístino/aberto, uma vez (inclusive vazio), com identidade e prefixo que não sobrepõe os IDs recuperados. Valida/prepara tudo antes de swap; falha conserva estado anterior. Recibos restaurados contam integralmente nos limites originais, sem expulsão; replay compara hash e conserva os IDs, lote conflitante é recusado. Novos pedidos mantêm o fluxo normal sob namespace novo.

Queued/Downloading/Converting/Publishing/Cancelling/ImportFailed recuperados viram `Interrupted/Unavailable`, não Queued ou ImportFailed executáveis. Não há start/poll/cancel/import nem retry automático. Failed/Cancelled/Completed/Interrupted históricos preservam identidade/causa; Completed não prova que a fila local tenha sido salva, não restaura recibo comprometido do importador e não importa novamente. Todos os paths recuperados são ocultados da projeção pública; candidatos ficam apenas no checkpoint privado, sem autoridade de leitura/importação/cleanup. Recuperação acionável de mídia exige revisão assíncrona de ownership/bytes e escolha explícita futura.

Schema v1: 128 jobs, 64 recibos, 20 IDs por lote; bijeção/ordem de recibos→jobs, IDs/UUID/hash canônicos e enums/progresso/path lexicais válidos. Há teto inclusivo de 1 MiB lógico para strings/paths nativos do checkpoint, separado de 1 MiB para contagem original dos pedidos. Não inclui overhead de containers. Codec JSON tem teto absoluto de 2 MiB, profundidade máxima 64, UTF-8/Unicode válidos, membros exatos sem duplicatas/aliases e inteiros uint64 como strings decimais canônicas. Versão futura é distinguida antes dos campos v1, após limites/sintaxe; não é permissão para interpretar esse schema.

[WindowsDownloadCheckpointStore](../src/adapters/WindowsDownloadCheckpointStore.hpp) é síncrono e **somente para worker**, em diretório já existente/privado de composição confiável; não cria/repara config real. Pins de ancestrais/diretório, lock nativo exclusivo, arquivos regulares sem reparse/hardlinks e DACL protegida delimitam a transação. Novo temporário privado é escrito/flushed e publicado por rename atômico; nenhum backup automático ou truncamento de documento existente. Dados corrompidos/desconhecidos/futuros/ilegíveis bloqueiam save e são preservados. Falha antes do commit conserva documento anterior; falha após commit continua `Failed`, mesmo se readback encontrar o novo documento. Readback não vira ACK de sucesso/rollback. Diretório app-owned privado e ausência de host/same-user malicioso são precondições, não prova de CAS universal, flush físico ou perda de energia.

O diretório leaf permite sharing de escrita/delete exigido pelo rename NTFS no mesmo pai; os ancestrais permanecem pinados, e identidade, path final, reparse e DACL do leaf são revalidados. Isso não impede toda mutação concorrente feita por processo malicioso do mesmo usuário nem transforma um path no JSON em autoridade sobre mídia.

Os handles de inspeção do journal anterior são fechados depois da última comparação e antes do rename, mantendo lock/pins do diretório. O rename normal não substitui destino com handles ainda abertos; não são usadas semânticas POSIX para ignorar essa condição. Não é compare-and-swap contra um escritor que não respeite o lock. [Regras nativas de rename da Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information).

O opcional `RC_MUSIC_BUILD_DOWNLOAD_PERSISTENCE=ON` compila codec/identidade/store com Qt Core, sem desktop, browser integration, Qt Network ou WebEngine obrigatórios. Permanece OFF por padrão e não é instanciado no player. O incremento assíncrono abaixo conecta admissão e lifecycle dos componentes, não o host/UI instalado. Journal de ownership/temporários/residuais e checkpoint conjunto de fila+importação continuam gates separados; não há promessa exactly-once após crash.

Na sessão MSVC/SDK explícita do build desktop, configure o cache de testes existente:

```powershell
cmake -S . -B out/product/desktop-tests -DRC_MUSIC_BUILD_DOWNLOAD_PERSISTENCE=ON -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure de checkpoint falhou' }
cmake --build out/product/desktop-tests --target rc_music_downloads_tests rc_music_DownloadCheckpointJson_tests rc_music_WindowsDownloadCheckpointStore_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de checkpoint falhou' }
ctest --test-dir out/product/desktop-tests -R '^rc_music\.(downloads\.|download-checkpoint\.)' --output-on-failure --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Testes de checkpoint falharam' }
```

Receita não é evidência de execução. Tests usam TEMP privado e candidatos lexicais sintéticos, sem abrir mídia, rede, perfil pessoal, Registry ou benchmarks. Produção opcional com `BUILD_TESTING=OFF` deve registrar zero testes; o build default não ganha dependências de persistência/downloads.

Incremento de 2026-10-02: Red precedeu implementação no core, model, codec e store. Green final **54/54 casos Catch2** do serviço, **23/23** do model, **80/80** do codec e **23 passagens/0 falhas/2 skips** do store. A regressão de profundidade teve Red **79/1**; os Greens parciais do store falharam em sharing/handles próprios antes das correções, sem relaxar expectativas. Dois fixtures de symlink próprio não foram executados pela política do host. Sem diag temporário no código final.

Fechamento passou no cache desktop com downloads/browser/persistência ON e rede OFF: build Release completo, **362/362 CTest em 35,77 s** e `all_qmllint`. Seis skips internos no total incluem os quatro históricos do diagnóstico. Build separado de persistência com desktop/browser/rede/testing OFF passou, listou zero testes e não incorporou Qt Test/Network/WebEngine, helpers ou fixtures. Core de downloads sem Qt e default desktop/testing OFF também compilaram. Formato dos 14 C++ alterados e checks documentais/rastreabilidade/diff são próprios; este recorte não reexecutou JavaScript nem os componentes de rede/TLS/conversão, não ligou host/UI no player e não criou runtime/instalador.

### Admissão durável assíncrona

[DownloadAdmissionController](../include/m0/application/DownloadAdmissionController.hpp) é o dono puro da admissão/restauração/checkpoints, na thread da aplicação. Recebe `DownloadService` e a porta rápida [DownloadCheckpointIo](../include/m0/ports/DownloadCheckpointIo.hpp). `start()` agenda Load, sem ler arquivos no caller. Missing estabelece histórico vazio prístino sem salvar defaults; Loaded válido restaura recibos e histórico inerte. Invalid/Unreadable/FutureVersion, payload incoerente ou restore recusado bloqueiam novos pedidos, sem apagar/substituir dados. `retryStartup()` só repete a leitura por ação explícita após reparo externo.

`submitStaged()` prepara e valida o checkpoint prospectivo completo **antes** de comprometer IDs/quotas/recibo. Candidatos privados recuperados contam no teto lógico separado; caps configuráveis do serviço não relaxam schema v1. Jobs novos aguardam confirmação na ordem FIFO, enquanto o job ativo continua progresso, timeout e cancelamento. O `submit()` legado continua disponível para composição volátil/testes, mas replay de um estágio não confirmado devolve Busy; composição durável não pode usar esse caminho para novas admissões.

`begin()` admite no máximo um lote novo aguardando escrita. Pending não contém IDs nem ACK; mesmo UUID/lote repete a espera, conflito é InvalidRequest e outro lote novo é Busy. Só a conclusão **Saved do ID/tipo correspondente** libera o estágio e torna o recibo observável. Já confirmado pode ser consultado repetidamente por sessões independentes. Failed/Blocked, exceção, operação/tipo incorreto ou enqueue recusado não iniciam o estágio nem inventam ACK. Recibo/quotas permanecem retidos; `retryAdmission()` agenda escrita nova do mesmo pedido, sem IDs novos/re-download/retry automático. Failed após commit nunca é promovido por readback no mesmo processo. Em novo startup, Loaded válido é histórico durável e permite replay dos mesmos IDs sem reexecutar/importar, não prova de que a sessão anterior recebeu resposta.

[SingleWorkerDownloadCheckpointIo](../include/m0/adapters/SingleWorkerDownloadCheckpointIo.hpp) mantém uma operação, incluindo completion ainda não observada. Factory confiável, store síncrono, leitura/escrita e destruição ficam em thread privada C++ sem Qt. IDs aceitos são positivos/crescentes; poll obsoleto não consome outro resultado. Métodos rápidos só admitem/copiam estado limitado ou consultam completion; não aguardam I/O nativo. Shutdown fecha ingressos imediatamente, pode cancelar pedido antes do I/O e drena chamada já ativa. Store e recursos da factory são destruídos antes de `stopped`; destructor faz join apenas como fallback, não protocolo normal de fechamento da UI.

O overload [AsyncDownloadSubmission](../include/m0/ports/AsyncDownloadSubmission.hpp) da sessão nativa conserva framing/origem/schema/resposta v1. Depois da montagem completa, observa o recibo por UUID com prazo próprio total de 5 s inclusivos, sem renovar por poll e com recusa de clock regressivo/overflow. Não envia mensagem Pending no wire. EOF, nova entrada, timeout ou exceção encerram somente o observador: não cancelam, repetem ou executam o trabalho pertencente ao dono. O host futuro deve usar essa composição, não o overload síncrono, para afirmar admissão durável.

Controles confiáveis de job podem usar o serviço na mesma thread; ninguém pode submeter/restaurar/checkpointar por fora do dono. Reentrância de portas impede novo begin/poll/tick e adia shutdown até o retorno; saída pedida ao observar Saved fecha o serviço antes de iniciar o estágio. Shutdown cancela fila e drena execução/escrita pendentes, depois salva uma vez o estado final se startup/escrita estavam saudáveis, e fecha/drena IO. Startup bloqueado ou escrita de admissão falha não provocam save automático de substituição. Falha final continua visível em `saveStatus()`, sem auto-retry. `stopped()` significa **parada física**, não sucesso de persistência; a composição do app precisa conferir status e tratar erro antes de aprovar sua própria saída. Dependências emprestadas sobrevivem ao drain; destructor do controller não chama portas.

Este incremento persiste admissão e encerramento final, **não autosave periódico de progresso**, journal de ownership/mídia/residuais, fila+recibo atômicos ou UI/host instalados. Crash no meio pode recuperar observação anterior como Interrupted, nunca continuar/importar automaticamente. Lite/Completa usarão os mesmos componentes sem engine obrigatória na Lite.

Checks usam o configure de checkpoint acima com `RC_MUSIC_BUILD_BROWSER_INTEGRATION=ON`, build dos alvos `rc_music_downloads_tests`, `rc_music_NativeDownloadSession_tests` e `rc_music_WindowsDownloadCheckpointStore_tests`, seguido de CTest com filtro `^rc_music\.(downloads\.|browser\.NativeDownloadSession$|download-checkpoint\.)`. Respeitar PATH Qt configurado pelo CTest. Gates/fakes/relógios observam comportamento, não benchmarks; o ensaio real Service→controller→worker→store→novo owner usa TEMP privado e mídia apenas lexical, sem rede/perfis/Registry.

Evidência de 2026-10-02: Red válido dos stubs **1 passagem/40 falhas** nos 41 casos Catch2 focados, sessão **21/31** e store composto **23/2/2 skips**. Correção mecânica de sintaxe Catch2 antecedeu o Red comportamental. Green inicial core **95/95**; revisão teve dois Reds de shutdown reentrante com um caso falho cada, antes da correção. Green final core **97/97 (2.354 assertions)**, sessão **52/52**, store **25/0/2 skips**. Fechamento Release ON completo **405/405 CTest em 39,52 s** e `all_qmllint` passaram; seis skips internos são os quatro históricos do diagnóstico e dois do store pela política do host. Builds Release sem testes (serviço puro, persistência Qt Core e desktop default/opcionais OFF) passaram/zero testes; grafos preservam as fronteiras. Formato C++, docs/rastreabilidade e diff foram conferidos. Não reexecutou JS, componentes de rede/TLS/conversão ou gerou runtime/instalador; histórico não é nova evidência. Nenhum requisito amplo foi promovido.

### Coordenação e recepção

Na sessão MSVC local descrita em [build do core](core-build.md#build-windows), reutilizando Qt/Catch2 já provisionados, configure conforme [biblioteca nativa](browser-extension.md#biblioteca-nativa-windows). Compile os alvos e filtre os testes desse componente:

```powershell
cmake --build out/product/browser-integration-tests --target rc_music_downloads_tests rc_music_NativeDownloadCodec_tests rc_music_NativeMessageFrame_tests rc_music_NativeDownloadSession_tests rc_music_NativeDownloadService_tests rc_music_DownloadJobsModel_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de downloads falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.(downloads\.|browser\.)' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes de downloads falharam' }
```

`RC_MUSIC_BUILD_DOWNLOADS=ON` também permite compilar/testar o serviço puro sem `RC_MUSIC_BUILD_BROWSER_INTEGRATION` ou `RC_MUSIC_BUILD_DESKTOP`. Com `BUILD_TESTING=ON`, fornecer o source de Catch2 como no core. Para provar produção sem Qt/testes:

```powershell
$rcNinja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
cmake -S . -B out/product/download-service-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DRC_MUSIC_BUILD_DOWNLOADS=ON -DRC_MUSIC_BUILD_BROWSER_INTEGRATION=OFF -DRC_MUSIC_BUILD_DESKTOP=OFF -DBUILD_TESTING=OFF "-DCMAKE_MAKE_PROGRAM=$rcNinja"
if ($LASTEXITCODE -ne 0) { throw 'Configure puro falhou' }
cmake --build out/product/download-service-release --target rc_music_downloads --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build puro falhou' }
ctest --test-dir out/product/download-service-release -N
```

### Importador local

Na árvore desktop de testes configurada conforme [build Windows](desktop-build.md#configure-build-e-testes), habilite também `RC_MUSIC_BUILD_DOWNLOADS=ON`. O alvo requer `RC_MUSIC_BUILD_DESKTOP=ON` e `BUILD_TESTING=ON`, com Qt/TagLib/Catch2 explícitos daquela receita; não exige ativar transporte de rede, browser integration ou conectar serviços ao executável do player. Reutilizando esse cache:

```powershell
cmake -S . -B out/product/desktop-tests -DRC_MUSIC_BUILD_DESKTOP=ON -DRC_MUSIC_BUILD_DOWNLOADS=ON -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure de importação local falhou' }
cmake --build out/product/desktop-tests --target rc_music_ui_download_import_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de importação local falhou' }
ctest --test-dir out/product/desktop-tests -R '^rc_music\.ui\.download-import$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes de importação local falharam' }
```

[PlayerDownloadImportTests](../tests/ui/qtest/PlayerDownloadImportTests.cpp) exercita presenter/model/serviço reais com backend, metadata, descoberta e execução falsos, usando paths lexicais sintéticos sem criar/abrir mídia. Os contratos cobrem append sem autoplay, preservação do playback, replay/conflito/remoção/capacidade, limites, busy/retry, callbacks reentrantes, evento `Ended`, polling adiado, thread/lifetime e superfície QML não privilegiada. Esta receita não é evidência de execução dos checks nem valida conteúdo/publicação, sandbox ou UI instalada.

Não solicitar benchmark, mídia pessoal, DLLs novas, download/instalação de dependências ou Registry para estes testes. Os opcionais da raiz ficam OFF por padrão; o player permanece sem os novos serviços de rede/engine. O [transporte HTTP/TLS](download-network.md) tem testes próprios; execução Windows e importador local não equivalem à composição completa com o player.

## Evidência do incremento de 2026-09-08

- TDD da coordenação: baseline com implementação vazia **1 passagem/26 falhas**; Green inicial **27/27**. Regressão adicional observou `Downloading` durante o callback de importação quando o contrato exigia `Publishing`; após corrigir a transição anterior ao handoff, **28/28**.
- TDD da projeção: model vazio falhou com zero papéis em vez dos 12 públicos. Regressão de `refresh()` reentrante durante inserção produzia três linhas para dois jobs; após coalescer atualizações, **22/22** casos Qt, sem skips. A ação reentrante em `dataChanged` também conserva o estado mais recente.
- Build Release MSVC 19.44/Qt 6.11.2 com `/W4 /WX` e seleção CTest **33/33**: 28 casos Catch2 do serviço e cinco alvos Qt. Os alvos Qt somam **222 passagens**, sem falhas/skips: codec 137, frame 35, sessão 21, sessão com serviço real 7 e model 22. São suites de componente, não testes de download na internet ou de UI instalada.
- Regressões locais: core independente **227/227**; cliente JavaScript **110/110**, sem rede ou navegador real. Nenhuma suíte de benchmark foi executada.
- Produção opcional: `download-service-release` compilou `rc_music_downloads` sem Qt/Catch2/adapters nativos; `browser-integration-release` compilou a biblioteca com Qt Core, sem Qt Test/Catch2/Qt Network/WebEngine. Ambos usam `BUILD_TESTING=OFF` e registram zero testes. Não geram host, runtime ou instalador atualizado.
- Formato dos oito C++ novos, validação documental **68 Markdown, 83 requisitos, 17 checks e 80 etapas**, rastreabilidade e `git diff --check` aprovados. Revisão independente da implementação e dos contratos não deixou achados materiais neste escopo.

Nenhum requisito amplo foi promovido neste checkpoint: 15 planejados, 66 em andamento e 2 comprovados. Seus fakes não entregaram execução concreta nem edições completas; os componentes posteriores de execução Windows e importação local são descritos separadamente acima. A composição final com o player e as duas edições continua pendente.

## Regressão de cancelamento de 2026-10-01

O teste reproduziu falha de cleanup tratada como cancelamento bem-sucedido: Red **28/34** casos Catch2, seguido de Green **34/34**. Duas regressões adicionais reproduziram exceção/deadline/clock ignorados durante a drenagem (Red **34/36**); Green **36/36**, zero skips. Os 28 casos anteriores foram preservados, corrigindo somente a expectativa antiga que também ocultava `Failed/Network` tardio. Build Release MSVC 19.44, formato e diff passaram; testes continuam com portas falsas, sem rede, filesystem ou mídia pessoal. Isso corrige a coordenação; evidência de término físico da execução Windows pertence aos seus testes separados, não a estes fakes.

## Evidência da importação local de 2026-10-01

Red corrigido dos stubs de importador/model: **8 passagens/45 falhas**. Green inicial **55/56** identificou expectativa incorreta de remoção no EOF, corrigida conforme avanço normal que preserva histórico. Teste adicional de falha de clock após commit teve setup inválido de preferências; corrigido só o harness. Green final **57/57** casos Qt, sem skips: fila/seleção/transporte preservados, idempotência e limites, busy/retry/liberação de quota, commit antes de notificações, EOF/metadata/power reentrantes, afinidade/lifetime e porta ausente do meta-object. Relógio C++ falso após commit prova retry sem reinserção e guard liberado, sem lançar em callback Qt. Power cobre estado final/refresh, não checkpoint completo da sessão.

Build Release ON completo e **337/337 CTest** passaram após a correção do único alvo da primeira execução **336/337**. Presenter **292/292**, contrato **35/35**, Quick **234/234**, core **227** e serviço **36**, além de adapters/áudio/processos existentes. Quatro casos internos de symlink foram skipped pela política do host. `all_qmllint`, formato C++, docs/rastreabilidade/diff passaram. Release OFF compila app com zero testes e sem ativar serviço/host/rede/engine de downloads; não foi gerado runtime/instalador. Componentes de rede/TLS/conversão e JavaScript não foram reexecutados; o handoff usa paths lexicais e portas falsas, não mídia baixada de fato.

## Pendências funcionais

1. Executável Native Messaging com transporte binário e encaminhamento para o serviço dono no app; composição/prefixo/identidade por instalação.
2. Download real protegido contra SSRF/redirects e conversão isolada, com ferramentas corrigidas e verificadas conforme ADR-0009; fakes não provam esses controles.
3. Persistência limitada de jobs/recibos, recuperação/cancelamento no restart e tratamento acionável de capacidade/histórico sem duplicação silenciosa.
4. Compor serviço, execução e importador local com timer/ciclo de vida e tela QML no player; validar MP3 publicado escutável na fila existente, sem handoff de parciais ou autoplay.
5. Navegação interna da Completa, integração comum instalada nas duas edições, opcionais do setup e testes reais autorizados do Brave. YouTube continua separado.

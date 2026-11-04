# Ciclo de vida privado do perfil de mídia Windows

**Estado:** backend nativo e owner compostos no probe autorizado; prova física parcial, sem ativação no player.

**Evidência:** 2026-11-04. A decisão de isolamento permanece [Proposta no ADR-0011](architecture/decisions/0011-media-worker-appcontainer.md).

O incremento separa propriedade do perfil, persistência e confirmação de uso nativo. O [owner](../src/adapters/WindowsMediaProfileOwner.hpp), o [store Windows](../src/adapters/WindowsDownloadMediaProfileStore.hpp), a [fence](../src/adapters/WindowsDownloadNativeUseFence.hpp) e o [backend nativo](../src/adapters/WindowsNativeMediaProfile.hpp) estão implementados. O probe manual compõe esses componentes, mas o owner ainda não está composto em `src/app/main.cpp`. Downloads remotos continuam OFF por padrão. Os testes automáticos não criam perfis; o ensaio físico recebeu autorização específica e não comprova integração instalada.

## Documento separado e limites

`DownloadMediaProfileJson` aceita um objeto fechado de schema v1, owner `rc-music-download-media-profile`, com exatamente seis campos: `schemaVersion`, `owner`, `jobId`, `operationId`, `stage` e `sid`. O documento é `download-media-profile.json`, limitado inclusivamente a 8 KiB e a uma operação. Checkpoint e journal de ownership preservam seus schemas v1, nomes e limites de 2 MiB e 256 KiB.

O job contém de 1 a 64 caracteres ASCII em `[A-Za-z0-9_-]`; a operação usa UUID v4 canônico em minúsculas. O nome `RCMusic.Media.<UUID>` é derivado internamente. Não há nome livre de perfil, path, URL ou dados do navegador no registro. O SID é nulo em Planned/Abandoned e obrigatório nas demais fases: 40 bytes canônicos de SID_PROFILE, codificados em 80 caracteres hexadecimais minúsculos. Validar esse formato não comprova existência, propriedade ou autorização para usar o perfil.

O codec reutiliza o preflight lexical estrito dos documentos de download antes do parse Qt, recusando duplicatas, inclusive escapadas, Unicode inválido, profundidade excessiva e conteúdo após o JSON. Owner conhecido com versão futura válida retorna `FutureVersion`; documentos desconhecidos, inválidos, futuros ou maiores que o limite são preservados. Não há migração ou substituição automática.

O diretório é existente e fornecido pela composição confiável. Diretório, documento, locks e temporários exigem DACL protegida com somente usuário atual e SYSTEM, ambos com controle total e ACEs sem herança. Reparse points e arquivos hardlinked são recusados; ancestrais e identidades permanecem pinados durante a operação. O adapter não cria ou repara o diretório configurado.

## Locks e atualização reconhecida

O lock curto `download-media-profile.lock` protege leitura, decisão de transição e substituição do documento. `WindowsPrivateDownloadDocument::update()` reinspeciona o documento sob esse mesmo lock exclusivo, valida os novos bytes, grava um temporário privado novo, faz flush e substituição atômica, revalidando diretório e identidades. A limpeza de falha alcança somente o temporário recém-criado por handle; temporários antigos ou desconhecidos são preservados.

Uma lease distinta mantém `download-media-profile-owner.lock` aberto com compartilhamento zero durante a operação viva. Ela é adquirida antes da leitura de startup do owner e impede dois owners cooperativos de atravessarem juntos a janela entre leitura e criação, inclusive usando a mesma identidade. Dados desconhecidos são inspecionados antes de tentar criar a lease. Os locks são arquivos vazios e privados; não são removidos por PID ou tratados como prova de perfil existente.

O lock do documento não substitui a lease, e nenhum deles comprova parada nativa. Não existe transação entre esse documento e os journals de mídia. A substituição cooperativa e os pins não constituem CAS contra escritores maliciosos do mesmo usuário.

`Saved` é o único reconhecimento de escrita. Uma falha `AfterCommit` pode deixar bytes novos válidos e ainda retorna `Failed`; `load()` nunca promove esse resultado. Replay idêntico faz outra gravação. O owner conserva o registro pendente e sua continuação em memória; `retryPersistence()` tenta explicitamente essa escrita, sem repetir Create/Delete já executados. Dados conflitantes retornam `BlockedByExistingData`, sem retry automático.

## Transições e autoridade viva

| Transição | Condição do owner vivo |
|---|---|
| Ausente/terminal → Planned | Lease exclusiva, startup saudável e UUID novo; Planned precisa de Saved antes de Create |
| Planned → Created | Esta chamada recebeu criação bem-sucedida e SID válido; não adota colisão ou perfil existente |
| Created → InUse | Created Saved; somente InUse Saved permite emitir a capability |
| Planned → Abandoned | O owner sabe que nunca chamou Create; Abandoned Saved libera a lease |
| Created → CleanupPending | Aborto antes do uso, com admissão fechada e fence quiescente |
| InUse → CleanupPending | Todos os usos da fence reconheceram parada; CleanupPending precisa de Saved antes de remover |
| CleanupPending → Removed | Porta reconhece remoção e confirma ausência; Removed Saved libera a lease |
| Removed/Abandoned → novo Planned | UUID diferente; operação ativa nunca é expulsa |

O mesmo UUID não pode trocar job ou SID. Regressões, saltos de fase e rebind de evidência são recusados. O owner conserva em memória a prova de sua própria criação bem-sucedida; SID ou registro carregado não recriam essa autoridade.

`Loaded` não terminal bloqueia o startup sem criar, adotar ou remover perfil. Crash entre Create e Created Saved deixa Planned incerto. Colisão, chamada falha, exceção ou SID inválido preservam a operação como criação não confirmada; `Abandoned` não serve para esses casos. Remoção falha, ausência desconhecida ou persistência ambígua também não liberam a operação por readback.

Os métodos pertencem a um único worker. A proteção de reentrada impede duplicar preparação, retry ou emissão de capability dentro de callbacks. `close()` reentrante é latched e fecha a admissão imediatamente; a continuação respeita o fechamento antes de criar, emitir uso ou remover. Construtor e destrutor não chamam as portas de perfil ou gravam journals. O destrutor revoga a admissão e libera a lease, conservando o registro ativo que bloqueará outro startup; não fabrica ACK nem cleanup.

## Fence e supervisor

A capability emitida pelo owner compartilha uma `WindowsDownloadNativeUseFence` própria. Ela admite somente um ticket pendente. O supervisor adquire o ticket antes da possibilidade de criar filho; confirma ausência de filho somente quando conhecida, ou confirma parada após término nativo e conclusão dos três pipes. O fechamento é irreversível e sincronizado com aquisição: OpenBusy torna-se ClosingBusy, e somente o ACK explícito permite ClosedQuiescent.

Destruir ou sobrescrever um ticket pendente torna a incerteza permanente. Cancelamento, timeout, destrutor, contagem de referências ou uma capability ainda viva não equivalem a ACK. `close()` pode permanecer em Draining; nova chamada observa a fence após a confirmação. Sem ClosedQuiescent, não há CleanupPending/remoção. A fence cobre sua instância e os tickets participantes, não qualquer uso do mesmo SID por outro código no host.

O supervisor e o runtime conservam as verificações AppContainer, SID, integridade Low, capabilities vazias, Job e herança explícita. No caminho AppContainer, o runtime obtém `LOCALAPPDATA` por `SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY)`, sem criar pasta ou herdar o valor do ambiente pai. PATH vazio e SystemRoot explícito permanecem; esse ajuste não amplia capabilities ou ACLs.

## Backend nativo de perfil

`WindowsNativeMediaProfile` admite uma única tentativa de criação e conserva somente o par nome/SID de uma criação própria com S_OK, SID canônico e liberação reconhecida de `FreeSid`. Colisão, exceção, outro HRESULT ou falha posterior à criação não autorizam adoção, remoção ou retry da API. O relatório conserva o evento S_OK mesmo quando a captura do SID/locator falha. Nome/SID divergentes não consomem a tentativa legítima de remoção. Construtor, factory e destrutor são inertes.

O backend do sistema exige processo desktop do usuário atual, fora da sessão zero e sem impersonação. Antes de emitir o SID ao owner, captura KnownFolder e o path de `GetAppContainerFolderPath`, confere a relação esperada e retém a grafia real retornada pelo Windows para a observação posterior. A consulta de arquivos recusa ancestrais reparse e erros desconhecidos. As chaves SID exatas Mappings/Storage são consultadas por componentes, recusando links abaixo da âncora predefinida `HKEY_CURRENT_USER_LOCAL_SETTINGS`. Essa âncora evita atravessar o alias de sistema `HKCU\Software\Classes`; não concede permissão para seguir links arbitrários. [Chaves predefinidas](https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys), [aliases do Registro](https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys).

Somente Delete S_OK inicia a observação de ausência nos locais capturados. Qualquer artefato presente ou erro de consulta impede ausência confirmada. A consulta não enumera todo o host e não reconstrói autoridade a partir de dados carregados. A falha continua incerta; não há segundo Delete, limpeza recursiva de perfil ou remoção direta de Registro.

## Evidência automática anterior

Capturas locais em `out/product/test-runs/media-profile-owner-20261104/`; os números incluem init/cleanup do Qt Test.

| Contrato | Red observado | Green observado | Logs principais |
|---|---|---|---|
| Modelo/JSON | 92 passagens / 22 falhas | 114/114 | `model-fence-red.log`, `final-regression.log` |
| Fence | 3 passagens / 12 falhas | 15/15 | `model-fence-red.log`, `final-regression.log` |
| Store privado | 6 passagens / 18 falhas | 24/24 antes da lease | `dependencies-green-store-process-red.log`, `store-green-owner-red.log` |
| Lease de operação | 29 passagens / 2 falhas | 31/31 com store | `lease-owner-red.log`, `final-regression.log` |
| Owner | 12 passagens / 37 falhas | 49/49 | `corrected-owner-red.log`, `corrected-owner-green.log` |
| Recusa da fence no supervisor | 54 passagens / 4 falhas | 58/58 | `dependencies-green-store-process-red.log`, `final-regression.log` |
| Ambiente AppContainer MF | 32 passagens / 1 falha | 33/33 | `sandbox-environment-red.log`, `sandbox-environment-green.log` |

A primeira integração com store real ficou em 48 passagens/1 falha porque a fixture criava DACL OICI, incompatível com o contrato privado sem herança. O setup foi corrigido; o Red comportamental foi recapturado antes do Green 49/49, preservando os oráculos de persistência e ausência de repetição de API.

O fechamento focado `final-regression.log` passou **18/18 CTest**, com **707 passagens Qt e dois skips históricos** de symlink no checkpoint, impostos pela política do host. Inclui codecs anteriores, ownership, store, owner, fence, supervisor, conversão MF, preparação/validação de recovery, tarefa preparada e sessão. O parser puro do probe também passou **1/1**, inclusive no fechamento. Esses testes usam porta de perfil falsa e, quando aplicável, filesystem privado real e helpers locais; não criam perfis ou alteram o Registro. O resultado cobre essa seleção focada, sem equivaler à suíte inteira do produto.

Os builds Release de `rc_music_download_documents`, da biblioteca opcional `rc_music_player_downloads` e do executável `m0_qt_shell` terminaram com exit 0. Os dois caches de produção registram zero testes (`documents-release-build.log`, `documents-release-tests.log`, `optional-release-build.log`, `optional-release-tests.log` e `optional-player-release-build.log`). O formato de 410 C++/62 QML e a fronteira do grafo Lite, com 255 comandos, passaram. A compilação e o link não ativam o owner nem aprovam a integração instalada.

## Ensaio físico autorizado: resultado parcial

Os três ensaios manuais autorizados de 2026-11-04 estão em `out/product/test-runs/media-appcontainer-native-authorized-20261104-{01,02,03}/`. Cada um possui recibos anteriores à criação, resultado da criação, helper, resultados de mídia, cleanup por API e conferência externa somente leitura. O host registrou kernel Windows `10.0.26200`, AppContainer comum e capabilities vazias; LPAC não foi ensaiado.

| Ensaio | Helper e mídia | Resultado global |
|---|---|---|
| 01 | Helper não iniciou, erro Isolation; três conversões falharam | `passed=false` |
| 02 | Mesmo bloqueio antes da execução; três conversões falharam | `passed=false` |
| 03, após correção de LOCALAPPDATA | Helper iniciou; MP3, AAC em M4A e WAV converteram e passaram por segunda validação independente | `media_passed=true`, `helper_passed=false`, `passed=false` |

No terceiro ensaio, `02-helper-result.json` registra máscara **29695/32767**, ACK físico, SID/token AppContainer, Low, zero capabilities e os canais autorizados. O hash do sentinela permaneceu igual. As tentativas de escrita pela entrada, acesso ao sentinela e escrita/alteração de DACL, owner e exclusão no runtime receberam os erros estritos esperados. O probe confirma os handles autorizados; não enumera todos os handles do filho para provar ausência de quaisquer extras.

Duas provas permaneceram abertas, sem alteração dos oráculos:

- `JobChildDenied`: a tentativa de descendente retornou **1816, ERROR_NOT_ENOUGH_QUOTA**, em vez do erro estrito de acesso esperado. Isso não foi reclassificado como aprovação do check, nem como prova de negação pelo AppContainer. [Código Win32](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1700-3999-).
- `LoopbackDenied`: a conexão retornou **10060, WSAETIMEDOUT**; o critério exige negação explícita **10013, WSAEACCES**, com controle positivo fora do sandbox. Timeout permanece inconclusivo para a política de rede. [Códigos Winsock](https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2).

Os resultados de mídia de `03-results.json` preservam hashes das três fixtures e saídas, conversão sem erro, segunda validação e ACKs nativos. Isso comprova essas três fixtures neste host e nessa configuração; não certifica todos os codecs, containers, versões do Windows, conteúdo externo ou limites de escrita no armazenamento próprio do perfil.

## Cleanup e resíduos observados

Nos três ensaios, `01-profile-result.json` registra Create S_OK, perfil criado pela própria execução e SID liberado. `04-profile-cleanup.json` registra um Delete S_OK somente após todos os ACKs nativos. O retorno da API não foi tratado como prova universal de ausência: a documentação exige fechar handles para remoção completa e informa estado indeterminado em falhas. [DeleteAppContainerProfile](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deleteappcontainerprofile).

Cada `06-external-residue-check.json` posterior registra ausência do path de pacote exato do perfil e das chaves SID exatas Mappings/Storage consultadas. Essa conferência somente leitura se limita aos nomes/SIDs dos três recibos; não afirma ausência universal de resíduos no host. Os três diretórios de fixture permaneciam presentes e foram preservados:

- `RCMusic-AppContainer-Probe-CBkphS` — ensaio 01;
- `RCMusic-AppContainer-Probe-WhCiDX` — ensaio 02;
- `RCMusic-AppContainer-Probe-SvkojM` — ensaio 03.

Os recibos contêm seus paths completos. `runtime_cleanup` e `workspace_cleanup` internos não equivalem à ausência dessas árvores TEMP, confirmada como `present` pela observação externa. Não houve remoção recursiva substituindo a API de perfil.

Os três ensaios anteriores criavam diretamente `RCMusic.Test.<UUID>` e usavam capability sem a fence do owner. Seus recibos permanecem históricos: não são reinterpretados pela nova composição nem pelo protocolo v2.

## Ensaio composto autorizado

O novo [probe manual](../tests/windows/sandbox/README.md) compõe backend nativo, owner, store privado, lease e a mesma capability/fence para helper, conversão e segunda validação. A criação usa `RCMusic.Media.<UUID>` e o journal permanece em `profile-journal` junto dos recibos. A nova cobertura automática usa chamadas de perfil injetadas e preserva os Reds em `out/product/test-runs/native-profile-composition-20261104/`:

| Contrato | Red observado | Green observado |
|---|---|---|
| Backend nativo: propriedade, chamada única, colisão, SID, exceções e ausência | 14 passagens / 25 falhas | 39/39 |
| Protocolo v2: porta UDP, tamanho/versão e erros brutos independentes | 21 passagens / 11 falhas | 32/32 |
| Notificação específica de limite de processos no Job | 58 passagens / 1 falha | 59/59 com supervisor |

O fechamento `final-captured-path-regression.log` passou **20/20 CTest**, com **779 passagens Qt e dois skips históricos** de symlink do checkpoint. São 19 suítes Qt e o parser puro; não é a suíte inteira. `final-captured-path-build.log`, `final-documents-release-build.log` e `final-optional-player-release-build.log` registram builds Release com exit 0; os caches de produção registram zero testes. `final-format-checked.log` passou **414 C++/62 QML** e `lite-boundary.log` conferiu **255 comandos**. A validação documental passou **85 Markdown/83 requisitos/17 checks/80 etapas**, sem elevar requisitos a comprovados por esses testes. Não houve novo instalador, instalação ou benchmark.

O supervisor associa uma completion port privada ao Job antes do launch e conserva os eventos observados até o próximo start. A notificação `JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT` identifica o limite de processos; ausência de mensagem é inconclusiva. Ela nunca promove estado terminal, ACK ou cleanup. No probe, erro bruto 1816 acompanhado desse evento e sem eventos de limite de memória confirma somente a restrição de descendentes pelo Job; `checks` bruto permanece preservado, ao lado de `effective_checks`. [Notificações de Job](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_associate_completion_port).

O ensaio `media-owner-native-authorized-20261104-01` recebeu Create S_OK, mas a captura do locator encontrou o alias REG_LINK em `HKCU\Software\Classes`. Antes de qualquer worker, o owner recusou a preparação e preservou Planned com SID nulo. Não houve Delete. O diagnóstico somente leitura motivou a correção para a âncora predefinida, mantendo a recusa de links abaixo dela. A conferência externa confirmou presentes o pacote, a chave Mappings e o TEMP `RCMusic-AppContainer-Probe-mXZJRp`; Storage estava ausente. O perfil **`RCMusic.Media.6a06e082-f85d-4d0d-97f0-f70f8bfd7c24` permanece preservado para revisão**. O diagnóstico de SID não autorizou adoção ou cleanup por Loaded.

Os ensaios compostos 02 e 03, após essa correção, confirmaram os seguintes critérios pelos recibos. O 03 repetiu a jornada após conservar a grafia real do path retornado pelo Windows:

- `05-no-child.json`: uma imagem própria existente e inválida falhou antes de admitir filho, com fence OpenIdle, journal InUse e runtime limpo;
- `03-results.json`: MP3, AAC/M4A e WAV converteram e passaram por segunda validação independente com ACKs;
- `07-cancel-and-drain.json`: helper vivo com stdin não lido, término nativo testemunhado antes do poll, owner Draining e journal InUse antes do ACK, novo launch recusado e fence ClosedQuiescent somente após drenagem;
- `04-profile-cleanup.json`: Delete S_OK após ACKs, ausência confirmada e Removed Saved. A conferência externa posterior registrou pacote e chaves SID exatas ausentes.

O helper conservou `checks=29695`, com `effective_checks=30719/32767` após a testemunha específica do Job. TCP retornou **10060** e permaneceu inconclusivo; o diagnóstico UDP retornou **0** em `sendto`, o que também não comprova negação nem entrega. Os controles positivos locais passaram. O check de rede não foi promovido, portanto `helper_passed=false` e `passed=false`, apesar dos critérios do owner e de mídia aprovados. Os TEMP `RCMusic-AppContainer-Probe-bfUghi` e `RCMusic-AppContainer-Probe-bxaqxi` foram preservados pela regra de falha global. [Contrato de sendto](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-sendto).

## Observador de eventos de rede do ensaio

O [observador WFP do probe](../tests/windows/sandbox/README.md#observação-de-bloqueio-de-rede-pelo-windows) acrescenta uma fonte de evidência separada dos erros Winsock. Somente um `CAPABILITY_DROP` correlacionado ao app ID canônico do executável privado, SID do usuário/perfil, TCP, destino/porta e janela de até 15 segundos pode complementar o timeout 10060. Exige flags completos, capability conhecida, filter ID não nulo e indicação explícita de loopback. `CLASSIFY_DROP`, eventos de allow, ausência de evento e conexão bem-sucedida não são promovidos. A estrutura não identifica direção, PID ou layer; a alegação se limita ao tráfego correlacionado que foi bloqueado por capability ausente. [Semântica do evento](https://learn.microsoft.com/en-us/windows/win32/api/fwpmtypes/ns-fwpmtypes-fwpm_net_event_capability_drop0).

O contexto e os filtros são preparados antes da assinatura. Somente start/stop reconhecidos permitem usar a primeira correspondência; o receipt registra executável, destino/porta, janela e metadados mínimos da correspondência, sem gravar tráfego alheio. Falha de unsubscribe/close fecha admissão e conserva recursos/contexto até a saída do driver, sem retry destrutivo. Essas notificações não confirmam ACK de processo/pipes ou ausência de perfil.

`--network-preflight` consulta as opções existentes de coleta e a máscara `CAPABILITY_DROP`, sem criar perfil ou assinatura e sem alterar firewall, ACL, auditoria ou configurações. Leitura negada significa diagnóstico indisponível; não demonstra permissão ou bloqueio de rede. O Windows exige direitos próprios de leitura/assinatura além de abrir a sessão. O observador é compilado somente no probe e em seu teste com chamadas injetadas, fora do produto/worker/instaladores. Nenhum recibo histórico é reinterpretado por esse código. [Direitos WFP](https://learn.microsoft.com/en-us/windows/win32/fwp/access-control), [assinatura](https://learn.microsoft.com/en-us/windows/win32/api/fwpmu/nf-fwpmu-fwpmneteventsubscribe1).

As capturas de `out/product/test-runs/media-isolation-network-20261104/` registram o Red inicial **3 passagens/71 falhas**, Green **74/74** e a regressão da revisão **73/2 → 75/75**. O segundo Red reproduziu descarte prematuro do contexto depois de Subscribe falho sem handle. A correção retém também esse contexto/bridge/engine, inclusive em falha RPC; Close não substitui a barreira de Unsubscribe. Um callback tardio após destruir o observer permanece seguro e não produz aprovação. O parser puro registra separadamente os Reds do modo preflight e da admissão da testemunha de rede.

`final-regression.log` passou **7/7 CTest**, com **269 passagens Qt e nenhum skip**: observador, protocolo do probe, supervisor, backend nativo, owner, fence e parser puro. É a seleção afetada, não a suíte inteira. `optional-release-build.log` e `lite-release-build.log` passaram; o cache de produção opcional registrou zero testes. Formato **417 C++/62 QML** e fronteira Lite **255 comandos** passaram. A revisão não alterou código do player ou worker de produção.

O `compiled-native-preflight-run.json` registra a execução inicial sem elevação do executável SHA-256 **`151abba1a1f8d36801db7ae966f4bd0478d05877667b8b6192f606676be81a51`**, exit 1: open **0**, query **5 (AccessDenied)**, close **0**, sem contexto retido, assinatura, perfil ou alteração de política. A consulta da máscara não chegou a ser executada nessa chamada.

Após autorização explícita de uma única leitura elevada, o launcher revisado conferiu esse mesmo hash e executou somente `--network-preflight`. `elevated-preflight-01/run.json` e `stdout.json` registram open, ambas as queries e close **0**, valores U32 de coleta **1** e máscara **0**, exit 1/`KeywordDisabled`, sem contexto retido ou timeout. O launcher PID 46628 e o preflight PID 50780 terminaram. Não houve assinatura de eventos, worker, perfil, instalação ou alteração de firewall/auditoria. A autorização de leitura foi consumida nessa execução; não autoriza habilitar a máscara global.

O evento real correlacionado permanece sem aceitação. O próximo caminho a investigar é uma prova de CLASSIFY_DROP já coletado, com correlação completa e consulta da regra concreta de isolamento, preservando a rejeição atual enquanto o novo contrato não estiver provado. Não substituir a evidência por timeout, ausência de pacote ou inferência estática. A produção remota continua OFF.

## Restrição de armazenamento ainda pendente

O Job atual não impõe cota física de disco ao armazenamento do perfil. `IoWriteBytesLimit` em limites de notificação não impede que o processo continue escrevendo; observar ou matar depois do excesso não equivale a impedir a alocação. Quotas NTFS são cobradas ao owner por volume e não constituem um limite separado para esse perfil ou para o Registro. Não alterar a quota do usuário para resolver o gate do worker. [Limites de notificação](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_notification_limit_information), [cobrança de quota NTFS](https://learn.microsoft.com/en-us/windows/win32/fileio/disk-quota-limits).

A próxima alternativa a ensaiar é impedir escrita nos recursos próprios de um perfil novo, após inventariar owner, DACL, herança, pastas e armazenamento de Registro. Uma ACL somente na folha `AC` ou o modelo RX do runtime não comprova toda essa superfície: filhos protegidos ou ACEs explícitas exigem avaliação, assim como os direitos implícitos do owner. A interação de `OWNER_RIGHTS` com o token real requer prova física e precisa preservar a capacidade de cleanup pelo host. Essa é uma investigação pendente, não uma alteração já aplicada ao perfil. O perfil 01 preservado não é fixture reutilizável.

O ensaio deve começar com controles positivos em um perfil descartável próprio, depois exigir negação explícita de criação, append/truncate/escrita em arquivo existente, subdiretório, exclusão e operações de escrita/segurança no Registro. Abertura inesperada com WRITE_DAC/WRITE_OWNER/DELETE deve reprovar e fechar o handle, sem usar a autoridade obtida para alterar ACLs ou excluir objetos. Após os ACKs, comparar dados/inventário/segurança e executar Delete pela API com conferência de ausência. As três fixtures MF e a segunda validação precisam continuar funcionais. Falha de compatibilidade ou cobertura incompleta conserva o gate aberto; não autoriza ampliar acesso ou substituir a cota por watchdog.

## Limites e próxima integração

O owner/fence já foi exercitado com o backend nativo no probe; permanece ausente da composição normal do player/ingresso instalado. O cenário físico não ensaiou destruição com ticket abandonado, crash, escrita ambígua ou falha de Delete: esses casos conservam cobertura determinística com portas falsas, sem alegar aceitação física. A prova específica do Job não é prova de negação de descendentes pelo AppContainer.

Permanecem pendentes a conclusão da prova de rede, política de escrita/cotas/resíduos do armazenamento próprio do perfil, revisão do perfil preservado, composição instalada e os demais gates do ADR-0009. Os resultados não autorizam ampliação de capabilities, exceção loopback, ACL de ancestrais ou fallback legado. O ADR-0011 permanece Proposto e a produção remota OFF.

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

O evento real correlacionado permanece sem aceitação. Esse resultado motivou o oráculo separado de CLASSIFY_DROP abaixo. Não substituir a evidência por timeout, ausência de pacote ou inferência estática. A produção remota continua OFF.

### Bloqueio por regra de isolamento

O modo explícito `ClassifyIsolation` preserva o oráculo CAPABILITY_DROP como padrão. `--network-preflight-classify` consulta coleta e máscara sem exigir o bit 4, mas continua recusando erro, tipo desconhecido ou coleta desligada. O ensaio manual seleciona esse oráculo somente com `--classify-isolation` após os argumentos de perfil/evidência; não há fallback automático entre provas.

Um CLASSIFY_DROP precisa dos mesmos flags, app ID canônico, SID de usuário/perfil, IPv4/TCP, destino/porta e janela. Exige também `filterId != 0`, `layerId` não nulo de 16 bits, `isLoopback == TRUE` e `msFwpDirection == 0x3901`; `FWP_DIRECTION_OUTBOUND == 0` não representa esse campo. O callback guarda somente um candidato escalar e não consulta o sistema. Candidato não preenche `matched` ou `first_match`. [Campo de direção e IDs](https://learn.microsoft.com/en-us/windows/win32/api/fwpmtypes/ns-fwpmtypes-fwpm_net_event_classify_drop2).

Após Unsubscribe 0, antes de Close e na mesma engine, consultas exatas ao filtro e à camada exigem IDs retornados iguais aos do evento, filtro habilitado e ação exatamente BLOCK. Provider e sublayer precisam ser os GUIDs públicos `FWPM_PROVIDER_MPSSVC_APP_ISOLATION` e `FWPM_SUBLAYER_MPSSVC_APP_ISOLATION`; tanto a camada do filtro quanto a camada resolvida pelo ID do evento precisam ser `FWPM_LAYER_ALE_AUTH_CONNECT_V4`. Windows Firewall genérico, callout, objeto ausente, GUID desconhecido, divergência, exceção ou erro não aprovam. Não há reabertura/reconexão/retry. Somente regra verificada e encerramento reconhecido permitem `matched`; Close incerto revoga a prova e conserva contexto. [Subcamada de isolamento](https://learn.microsoft.com/en-us/windows/win32/fwp/management-filtering-sublayer-identifiers), [filtro por ID](https://learn.microsoft.com/en-us/windows/win32/api/fwpmu/nf-fwpmu-fwpmfiltergetbyid0), [camada por ID](https://learn.microsoft.com/en-us/windows/win32/api/fwpmu/nf-fwpmu-fwpmlayergetbyid0).

O receipt conserva candidato/prova separados, IDs, direção, GUIDs e filterKey em bytes brutos SDK, flags/action e horários de consulta em strings decimais. Runtime e perfil permanecem vivos até o retorno das consultas. GetById observa objetos atuais, não um snapshot retroativo: não há alegação de imutabilidade histórica ou garantia de não-reuso de IDs após mudanças/reinício. A afirmação limitada é bloqueio da tentativa correlacionada por filtro dessa subcamada, sem identificar capability ausente. O campo `capability_id` permanece nulo nesse modo.

Evidência em `out/product/test-runs/media-isolation-classify-20261104/`: Red **106 passagens/42 falhas → Green 148/148** no observador, preservando os 75 casos CAP anteriores. O parser reproduziu cinco falhas de seleção/verificação/rótulo/versão de prova antes do Green. O fechamento `final-regression.log` passou **7/7 CTest / 342 passagens Qt, sem skips**, incluindo init/cleanup, no build Release do probe e dos alvos focados; não é a suíte inteira. Formato **417 C++/62 QML**, fronteira Lite **255 comandos** e validação **85 Markdown/83 requisitos/17 checks/80 etapas** passaram. O incremento não altera código/link do produto, worker ou instaladores.

`native-preflight-classify.json` registra a execução normal do binário SHA-256 `cd7a94bdb8e915a4f15325eac27fd68dfa24c1c1f9b9856fd028a1cda233efb9`: seleção correta do oráculo, open/close 0, query 5, exit 1 e nenhuma assinatura/perfil/política alterados. Nenhuma nova elevação foi solicitada ou executada. Rodar o driver inteiro elevado diagnostica somente esse contexto e não comprova equivalência com o app normal. O próximo ensaio deve manter driver/owner/worker não elevados, com observador elevado separado e temporário, após composição, testes e autorização próprios. Low/AppContainer não substituem conferência de usuário, sessão, elevação e privilégios. A prova física continua pendente.

O ensaio separado deve usar somente request/ready/result novos e limitados, ligados por nonce e SHA dos bytes exatos do request. A identidade esperada é o usuário do driver, não o usuário da elevação; PID/creation time, imagens autenticadas, raiz privada/pins e encerramento real precisam ser verificados fora do codec. Ready só pode ser publicado após Subscribe e informar a janela efetiva, sem consumir seu prazo durante UAC. Resultado falho não inventa Ready/Observing; timeout não confirma ACK. A composição ainda não foi implementada, e nenhum serviço, registro persistente ou alteração WFP é necessário por esse desenho de teste.

### Preparação do transporte separado

Três componentes restritos aos testes preparam essa composição, sem acrescentar modos executáveis ao driver. `AppContainerObserverProtocol` codifica os três documentos fechados, com limite de 16 KiB, nonce, SHA-256 dos bytes exatos do request e identidades PID/creation time. SID e hashes são binários nos tipos; valores de 64 bits e GUIDs usam representações textuais exatas no wire. Request fixa IPv4/TCP/loopback e ClassifyIsolation. Result conserva o relatório tipado completo, inclusive erros e ausência de prova; rejeição de admissão não fabrica Ready ou relatório. Schema, hash ou nonce válidos não autenticam um processo, imagem ou arquivo.

`AppContainerObserverFiles` abre um diretório existente e exige owner do driver e DACL protegida com exatamente três ACEs: usuário do driver, SYSTEM e Administrators. Essa permissão pertence somente ao futuro diretório de troca; o journal de produção continua com sua DACL própria. Arquivos têm nomes fixos e são criados uma vez com a segurança final, sem sobrescrever, reparar ACL ou remover objetos. Leitura limitada mantém um lease com handles e pins de ancestrais; recusa reparse, hardlink, tipo divergente e mudança de identidade/segurança. Sharing violation significa Busy, não um frame vazio. O lease impede escrita e troca do arquivo enquanto seus bytes são usados. A classe não cria a raiz nem transforma diretório ou documento em autoridade sobre o perfil.

`AppContainerObserverIdentity` captura somente o token primário do processo por um handle emprestado, sem impersonação ou ajuste de privilégios. Driver normal exige usuário/sessão válidos, integridade Medium, elevação falsa e ausência de AppContainer/UIAccess; um grupo Administrators habilitado é recusado. Observador exige elevação e integridade High ou superior, mas não impõe que o usuário do UAC seja o usuário do driver. Correspondência SID/sessão é uma verificação separada do papel. A captura registra grupos restritos e privilégios habilitados; consultas sucessivas pelo mesmo token não constituem snapshot atômico nem prova de imutabilidade posterior. PID, criação, imagem e término do processo continuam responsabilidade da composição.

O observador aceita opcionalmente uma janela de exatamente 15 segundos calculada após as consultas e imediatamente antes de Subscribe. O relógio é chamado uma vez, e o mesmo intervalo fica no filtro, callback e relatório. Relógio ausente, zero, overflow ou exceção impedem assinatura; falha de Close continua incerta. A configuração anterior com intervalo explícito não lê esse relógio. Nenhum teste automático assina WFP ou cria perfil por esses componentes.

Evidência em `out/product/test-runs/media-observer-handoff-20261104/`:

| Contrato | Red observado | Green observado |
|---|---|---|
| Janela deferida, ordem, limites, reentrada e falhas do relógio | 154 passagens / 27 falhas | 181/181 |
| Codec dos três documentos e binding | 105 passagens / 8 falhas | 113/113 |
| Captura do token, papéis e SID/sessão | 37 passagens / 11 falhas | 48/48 |
| Arquivos, ACL/pins, limites e lease | 15 passagens / 19 falhas | 34/34 |

O Green inicial dos arquivos foi **31/34** por três problemas de fixture: CreateDirectory/SetSecurityInfo normalizam ou desdobram ACEs, e o teste de revalidação precisava conservar previamente seu handle de mutação. As fixtures finais conferem trustee, máscara, flags, proteção e contagem reais antes do oráculo público. Duplicata usa ACEs do mesmo trustee com escopos distintos para não desaparecer por coalescência. Extra/revalidação exigem mais de três ACEs e World presente, sem presumir quatro quando o Windows produz cinco. `files-fixture-v3.log` registra **34/34**; o código de arquivos não foi alterado para acomodar essas diferenças.

`final-build.log` e `final-regression.log` registram **10/10 CTest / 570 passagens Qt, sem skips, em 6,40 s**, com init/cleanup, no Release do probe e dos alvos afetados. É a seleção proporcional, não a suíte inteira. `final-format.log` passou **426 C++/62 QML**; `lite-build.log` e `lite-boundary.log` passaram com **255 comandos** no grafo. Revisões independentes do codec, token e arquivos não deixaram bloqueador material. Nenhum componente novo foi ligado ao player/worker/instaladores ou aos modos executáveis do probe; houve somente leitura do token atual e operações em fixtures TEMP próprias, sem elevação, assinatura WFP, perfil ou política alterada. Outro usuário de UAC, writers SYSTEM/Administrators e falhas nativas raras de Write/Flush/Close permanecem fora dessa evidência. Composição, autorização e ensaio físico continuam pendentes.

`final-docs.log` validou **85 Markdown/83 requisitos/17 checks/80 etapas**. `final-traceability.json` conserva 83 registros e `allRequirementsProven=false`; este incremento não promove estados de requisitos ou aprovação de release. A conferência final não encontrou processos do workspace ainda em execução.

### Composição separada do ensaio

O incremento posterior liga esses componentes somente ao probe manual. O driver usa `--allow-profile-changes --evidence-dir <raiz nova> --external-classify-observer`; o ator separado usa `--allow-wfp-subscription --observer-request <raiz nova>/observer-handoff/wfp-request.json`. Nenhum modo eleva ou lança o outro processo automaticamente. Parser recusa mistura de flags, request com outro nome e escapes do diretório permitido. Os modos de preflight continuam isolados e não criam perfil ou assinatura.

Antes de criar o perfil, o driver exige snapshot completo do próprio processo, sessão interativa, token primário Medium não elevado e os demais predicados de driver normal. O diretório de evidência do modo externo e seu filho de troca são novos, com DACL protegida usuário/SYSTEM/Administrators; o journal continua privado com sua própria DACL de duas ACEs. O driver prepara o runtime privado do helper e sela os bytes exatos do request com uma lease. Espera Ready por até 120 s, sem executar o helper durante UAC. Só concede uma permissão de launch, após conferir o processo do observador ainda vivo, PID/criação, token elevado na mesma sessão, imagem com o mesmo FILE_ID/volume e SHA do próprio probe e pelo menos 10 s restantes na janela anunciada.

O ator confirma por handles que o diretório do request está sob o `out/product` confiável; igualdade textual sem diferenciar maiúsculas não basta em diretórios Windows case sensitive. Valida owner/DACL e request/nonce/hash, driver normal vivo e sua imagem, além do usuário e sessão reais do driver. O SID do usuário do UAC pode ser diferente, mas AccessDenied ao consultar token ou runtime permanece falha, sem ajuste de privilégios ou reparo de ACL. Não há garantia de compatibilidade com toda elevação para outra conta.

A imagem do helper e seu diretório privado imediato exigem owner do driver e exatamente as ACEs usuário/SYSTEM com FA e pacote com RX, sem herança; a validação precede a leitura/hash do path recebido. Ancestrais e objetos ficam pinados e sem reparse; arquivo exige link único. SHA precisa corresponder ao helper de origem fixado no build. O runtime pode ficar no TEMP próprio fora da raiz de evidência, como nos ensaios anteriores. Ator não executa esse helper, cria perfil ou aceita um conversor indicado pelo request.

Somente após Subscribe reconhecido publica Ready com a janela efetiva. O loop é limitado a 15 s e termina no primeiro candidato ou em falha de driver/relógio; chamadas nativas de início/encerramento continuam exigindo seus retornos, sem transformar prazo em ACK. Stop precede Result. Erro do relógio durante Stop conserva o relatório nativo fiel, inclusive match, mas impede exit 0. O driver exige também esse exit 0 para elegibilidade da prova. O gate Winsock anterior continua separado: evento/regra/teardown só complementam 10060, sem aprovar conexão bem-sucedida ou alterar erro bruto.

O driver observa Result e término por até 20 s desde Ready, sem renovar o prazo ao receber um frame. Resultado com peer já parado ainda precisa da etapa explícita de ACK pelo mesmo handle. Result não substitui Ready nunca aceito; Unavailable anterior a Subscribe não pode suceder Ready. Cleanup exige resultado terminal saudável, sem contexto retido, e sinal nativo com exit code conhecido. Código não zero pode confirmar parada/cleanup, mas não prova de rede. Ausência, frame inválido, PID/identidade divergente, prazo ou estado desconhecido mantêm Uncertain e preservam perfil/runtime para revisão.

`AppContainerObserverProcess` abre uma única testemunha não herdável, conserva o mesmo objeto mesmo após falha parcial e não reabre por PID. Snapshot de criação/imagem/token é imutável, sem alegar imutabilidade posterior do token. Poll só reconhece Stopped após sinal do objeto; o valor 259 de exit code não significa Running nesse caso. Seu destrutor fecha somente a testemunha e nunca mata o peer. Os recibos registram SID/sessão/elevação/IL/UIAccess/AppContainer, grupo Administrators, grupos restritos e privilégios habilitados, além de paths, FILE_ID/volume e hashes efetivamente pinados.

O agregado de ACKs do helper e do observador controla a saída do runtime e o fechamento do owner. Se ficar incerto, os proprietários nativos são conservados até o término do driver, sem Delete ou cleanup por Loaded. A composição ainda precisa de execução física autorizada: a única elevação anterior consultou opções e já foi consumida. Nenhuma nova assinatura ou perfil foi executado para implementar esse fluxo, que permanece fora do player/worker/instaladores e não habilita downloads de produção.

Evidência em `out/product/test-runs/media-observer-composition-20261104/`:

| Contrato | Red observado | Green observado |
|---|---|---|
| Testemunha de processo, snapshot/handle e término com exit 259 | 2 passagens / 7 falhas | 9/9 |
| Coordenador puro de Ready, Result, janela e ACK | 3 passagens / 89 falhas | 92/92 |
| Unavailable anterior a Subscribe após Ready já aceito | 92 passagens / 1 falha | 93/93 |
| Ator com Files/pins reais e WFP/identidades injetados | 4 passagens / 42 falhas | 46/46 |
| Root `product`/`PRODUCT` e relógio falho durante Stop | 46 passagens / 4 falhas | 50/50 |

`process-parser-red.log` registra também dois Reds do parser, sem operações de perfil. O Green inicial do ator foi 43/46: três fixtures tentavam alterar ACLs depois dos pins e foram corrigidas retendo previamente o handle de mutação e inspecionando o descritor físico, sem alterar os oráculos de recusa. `actor-fixture-green.log` passou 46/46 antes da revisão. O Windows permitiu case mode somente no TEMP próprio do novo teste; IDs distintos comprovaram o problema real, sem skip ou alteração de diretório do projeto.

`final-build.log` e `final-regression.log` passaram **13/13 CTest / 722 passagens Qt, sem skips, em 7,51 s**, com init/cleanup, no probe Release e alvos afetados. É a seleção proporcional, não a suíte inteira nem a execução WFP física. `final-format.log` passou **435 C++/62 QML**, `lite-build.log` passou e `lite-boundary.log` conferiu **255 comandos**. Revisões independentes do Actor e do Main não deixaram bloqueadores de lifecycle/ACK. As cinco fixtures de retenção criadas nas capturas foram removidas somente depois do término dos processos Qt, por folhas inspecionadas e diretórios vazios sem remoção recursiva; os recibos `retained-fixture-cleanup-01.json` e `retained-fixture-cleanup-final.json` confirmam ausência. Não houve cleanup do perfil preservado anterior.

O probe compilado tem SHA-256 `d008cafab583c48464e98618514077baa925ae14bcdc3a570f85d37476023c07`; o helper suspenso/confiável tem SHA-256 `ab2ee3d1b0d8cabc3002f0ad6636d9b1da25bd7516d2e16ca0c88d5f12235f2d`. Esses hashes identificam a preparação compilada, sem autorizar sua elevação ou aprovar rede/produção. Nenhum ensaio privilegiado foi executado por esse fechamento.

## Restrição de armazenamento ainda pendente

O Job atual não impõe cota física de disco ao armazenamento do perfil. `IoWriteBytesLimit` em limites de notificação não impede que o processo continue escrevendo; observar ou matar depois do excesso não equivale a impedir a alocação. Quotas NTFS são cobradas ao owner por volume e não constituem um limite separado para esse perfil ou para o Registro. Não alterar a quota do usuário para resolver o gate do worker. [Limites de notificação](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_notification_limit_information), [cobrança de quota NTFS](https://learn.microsoft.com/en-us/windows/win32/fileio/disk-quota-limits).

A próxima alternativa a ensaiar é impedir escrita nos recursos próprios de um perfil novo, após inventariar owner, DACL, herança, pastas e armazenamento de Registro. Uma ACL somente na folha `AC` ou o modelo RX do runtime não comprova toda essa superfície: filhos protegidos ou ACEs explícitas exigem avaliação, assim como os direitos implícitos do owner. A interação de `OWNER_RIGHTS` com o token real requer prova física e precisa preservar a capacidade de cleanup pelo host. Essa é uma investigação pendente, não uma alteração já aplicada ao perfil. O perfil 01 preservado não é fixture reutilizável.

O ensaio deve começar com controles positivos em um perfil descartável próprio, depois exigir negação explícita de criação, append/truncate/escrita em arquivo existente, subdiretório, exclusão e operações de escrita/segurança no Registro. Abertura inesperada com WRITE_DAC/WRITE_OWNER/DELETE deve reprovar e fechar o handle, sem usar a autoridade obtida para alterar ACLs ou excluir objetos. Após os ACKs, comparar dados/inventário/segurança e executar Delete pela API com conferência de ausência. As três fixtures MF e a segunda validação precisam continuar funcionais. Falha de compatibilidade ou cobertura incompleta conserva o gate aberto; não autoriza ampliar acesso ou substituir a cota por watchdog.

### Inventário antes de propor uma DACL

As quatro raízes conhecidas são pacote, `AC`, Mappings/SID e Storage/SID. O backend conserva os locators privados da criação para observar ausência; o getter passivo e a fonte ligados ao owner foram acrescentados no [incremento de leitura](#bindings-parciais-de-leitura-win32). Uma cópia de Report, SID canônico, record Loaded ou a capability de lançamento não reconstrói a prova de criação. A fonte exige o owner vivo, a mesma instância do backend e uma reserva real da fence, além do locator S_OK. O owner avança automaticamente de Created Saved para InUse; o incremento não cria outro estágio ou interpreta uma fence apenas observada Idle como reserva exclusiva. A sessão nativa completa das quatro raízes e sua execução em perfil ainda precisam de composição própria.

A hipótese de proteção preserva owner, ACEs e grants explícitos do host/SYSTEM. Um ALLOW `OWNER_RIGHTS` de `READ_CONTROL` pode suprimir os grants implícitos de owner; um deny de WRITE_DAC ao próprio OWNER_RIGHTS atingiria também o host quando ambos compartilham o owner. O deny específico do Package SID precisaria respeitar a ordem da DACL e cobrir direitos de escrita, DELETE, WRITE_DAC e WRITE_OWNER, além de FILE_DELETE_CHILD nos diretórios e KEY_CREATE_LINK nas chaves. Não usar generic write como atalho: suas máscaras incluem direitos também necessários para leitura. Essa é uma candidata para ensaio, sem transformador de ACL implementado ou garantia de compatibilidade MF. [OWNER_RIGHTS](https://learn.microsoft.com/en-us/windows-server/identity/ad-ds/manage/understand-security-identifiers), [acesso AppContainer](https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer), [direitos de arquivo](https://learn.microsoft.com/en-us/windows/win32/fileio/file-security-and-access-rights), [direitos de Registro](https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights).

Herança sozinha não cobre filhos protegidos ou ACEs explícitas. Storage/SID ausente é lacuna, sem criação automática pelo inventário ou reparo do ancestral compartilhado. Controles anteriores e posteriores devem conservar token, objetos, pins e share modes, e distinguir negação já existente por MIC de efeito da nova DACL. A captura de owner/DACL não consulta SACL nem comprova a mandatory label; essa observação permanece uma exigência do ensaio físico. [MIC](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control).

O futuro helper de storage deve ter alvo/protocolo separados do helper WFP congelado. No Registro, obter erro 5 ao escrever por um handle aberto somente para leitura provaria apenas falta de direitos desse handle: solicitar KEY_SET_VALUE explicitamente. A criação de subchave exige tentativa própria. Correlacionar GetAppContainerRegistryLocation com o Storage/SID capturado pelo host por canário aleatório, sem presumir aliases HKCU. Conservar HRESULTs e erros Win32 sem reinterpretar NOT_FOUND, sharing violation ou timeout como negação. Uma operação descartável que inesperadamente crie, trunque ou exclua dados reprova e precisa aparecer no inventário posterior, sem reparação silenciosa. [RegOpenKeyEx](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw), [Registry do AppContainer](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-getappcontainerregistrylocation).

### Modelo limitado de captura de metadados

`AppContainerStorageInventory` implementa somente o consumidor de uma porta C++ confiável, em alvo de teste independente. Source.begin devolve proveniência e sessão de leitura juntas; Loaded, Revoked, Unknown, erro ou sessão ausente impedem a captura. Não existe factory nativa implícita, nome/path/SID livre fornecido pelo caller ou método de criação, SetACL, adoção ou Delete. A ligação com o criador S_OK/lease/fence fica fora desse modelo, nos bindings parciais descritos abaixo.

A captura percorre quatro raízes fixas e seus objetos, com respostas individuais limitadas antes da alocação na futura porta. Limites inclusivos: 512 objetos, profundidade 16, SD bruto de até 64 KiB, nomes de até 255 unidades UTF-16 e 4 MiB de payload variável. O manifesto de raízes referencia os objetos sem duplicar SD/owner; `AC` é correlacionado ao filho direto do pacote por identidade e snapshot completo, contando uma única vez. Missing/Unknown, links/reparse/hardlink, aliases não esperados, ciclos, nomes inseguros, identidade/ACL divergente, erro ou limite excedido produzem relatório parcial Incomplete.

Ausência de DACL, DACL NULL e DACL presente são dados distintos, não aprovação de segurança. Owner SID, SD e nomes são copiados para armazenamento próprio, mesmo quando a porta usa buffers Qt emprestados. Identidades são locais à sessão: não são FileIDs persistentes ou HKEYs serializados. O SD é opaco e supõe uma porta confiável que o capturou; este consumidor não comprova estrutura Win32, MIC ou negação de escrita. SACL não é consultada. Observed significa captura aceita dessa porta, sem snapshot atômico ou autoridade para hardening/cleanup.

ReadLease conserva a sessão e admite revalidação somente de leitura. Close é explícito e faz no máximo uma tentativa na porta; só Closed com erro 0 confirma fechamento dessa sessão, sem confirmar worker/parada/perfil. Erro, contradição ou exceção retêm a sessão sem retry automático ou ACK por destrutor. O contrato usa uma thread confiável; reentrância retorna BUSY sem nova chamada. Close durante validação não destrói a sessão ativa, e NotAttempted/BUSY permite uma tentativa explícita depois de a validação retornar. Não há promessa de thread safety.

Evidência em `out/product/test-runs/media-storage-inventory-20261104/`: Red **2 passagens/102 falhas → Green 104/104**. A revisão reproduziu três regressões, **104/3 → 107/107**: metadados parciais do root descartados antes de recusar um payload excessivo, validação reentrante e Close que destruía a sessão durante validação. Oráculos foram mantidos; o fake preserva seu estado durante os callbacks para que o Red não dependa de UAF. `final-build.log` e `final-regression.log` passaram **1/1 CTest / 107 casos Qt, sem skips**, incluindo init/cleanup. São testes do novo consumidor com fonte falsa, sem execução de perfil, Registro, ACL ou API de inventário nativa.

`inventory-graph.json` registra oito comandos, somente o consumidor/teste e Qt Core/Test, sem targets/fontes do probe, mídia ou rede. `userenv.lib` é transitiva do Qt, não uma ligação com o backend de perfil. Os binários WFP `d008…`/`ab2…` e o launcher `fb29…`, preparados no incremento `48d8542`, permanecem congelados enquanto sua autorização específica está pendente. O inventário nativo, mandatory labels, política de escrita, controles físicos, compatibilidade MF e remoção reconhecida continuam abertos; este modelo não os substitui.

`final-format.log` passou **438 C++/62 QML** e `final-docs.log` validou **85 Markdown/83 requisitos/17 checks/80 etapas**. `final-traceability.json` mantém 83 registros e `allRequirementsProven=false`, acrescentando somente referências de testes/evidência a FR-051/FR-053. A revisão independente não deixou bloqueador material no consumidor. `frozen-wfp-artifacts.json` confirma os três hashes preservados e a conferência confirmou que os diretórios do ensaio físico/elevado ainda não foram criados.

### Bindings parciais de leitura Win32

O incremento separa a coordenação da origem, a leitura de permissões e o filesystem em alvos de teste próprios. Implementa o binding de pacote/`AC`, sem compor uma sessão Win32 das quatro raízes ou executar inventário em perfil AppContainer. Nenhum desses componentes cria perfil, aplica política de escrita, instala recursos ou habilita downloads no player.

`WindowsNativeMediaProfile::capturedStorageLocation()` expõe somente uma cópia própria dos metadados capturados pela criação S_OK dessa instância: nome/SID, usuário criador, pacote/`AC` e chaves SID exatas. Falta do callback, criação incerta, colisão, campos contraditórios ou tentativa de remoção impedem o retorno. Não há descoberta ou fallback por nome, e o DTO não concede autoridade sobre storage ou cleanup. A consulta é passiva; a captura dos paths pertence à chamada de criação já existente.

`AppContainerStorageLiveSource` exige owner Ready e confirma, por `usesPlatform`, a mesma instância de backend usada por ele. Reserva um ticket da fence exata antes de consultar o locator ou a factory e confere capability, par S_OK e estado vivo antes/depois das chamadas de leitura. Fechar o owner revoga as leituras e mantém a remoção em Draining até o fechamento explícito da sessão. Somente Closed com erro 0 permite reconhecer essa reserva de leitura, sem confirmar worker ou parada de processo. Erro, exceção ou abandono conservam a sessão e deixam a fence incerta, sem ACK por destrutor. A factory confiável apenas aloca/vincula a sessão, sem I/O nativo; só por esse contrato falha anterior ao retorno pode reconhecer NoChild. Owner e backend emprestados devem sobreviver à fonte e às leases; uso é de um único worker, sem promessa de thread safety.

`AppContainerStorageSecurity` recebe handles emprestados e não os fecha. Liga `GetKernelObjectSecurity` para arquivo/diretório e `RegGetKeySecurity` para chave, somente com OWNER/GROUP/DACL. Faz uma consulta de tamanho e no máximo uma leitura, sem retry, fallback por path, consulta SACL ou ajuste de privilégios. Confere o limite de SD bruto de 64 KiB antes de alocar o buffer e o orçamento conjunto antes de copiar o owner; parsing estrutural limitado de header self-relative, offsets, SIDs e extensões de ACL/ACE precede a publicação dos dados. Owner tem cópia independente; DACL ausente, NULL e presente permanecem distintas. Estrutura capturada não aprova a política da ACL, payloads de ACE ou MIC.

`AppContainerStorageFiles` implementa somente a parte pacote/`AC` da futura sessão, a partir de paths C++ confiáveis capturados pelo criador. Chaves Mappings/Storage não usam fallback de filesystem. Abre sem seguir reparse, confere identidades por volume/FILE_ID e a correlação de `AC`, limita nomes/respostas, revalida bindings/metadados e exige fechamento explícito com erro bruto. Solicita compartilhamento de leitura/escrita/delete; isso não qualifica renomeação ou exclusão de diretórios enquanto houver arquivos filhos abertos, nem transforma erro 5 em prova de DACL. A [documentação de renomeação](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information) prevê restrições quando arquivos do diretório ou de seus descendentes têm handles abertos. A revalidação não é snapshot atômico; mudança ou incerteza observada permanece registrada. Mantém uma testemunha por binding, fecha e contabiliza explicitamente a anterior ao substituí-la e reutiliza slots fechados, com limite de 4.608 handles retidos, sem contagem cumulativa de aberturas. Os 512 objetos permitidos pelo consumidor não comprovam que o binding suporte toda árvore desse tamanho; essa escala nativa não foi validada. Falha de abertura ou exaustão conserva o inventário incompleto, sem ampliar direitos ou descartar o erro.

A revisão reproduziu troca de ancestral por junction entre a conferência do binding e uma abertura por path absoluto. Revalidar depois da consulta de segurança seria tardio: o oráculo exige zero leituras do objeto estranho. A correção validada abre um componente por vez relativo ao handle retido do pai, usando `NtCreateFile` e `OBJECT_ATTRIBUTES.RootDirectory`, sem reconstruir um path absoluto para atravessar o ancestral mutável. A API distingue abertura de objeto existente de criação, e o contrato continua somente de leitura. A ligação `ntdll` é privada ao alvo de teste Files. [Abertura relativa](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile), [atributos e RootDirectory](https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_object_attributes).

Evidência em `out/product/test-runs/media-storage-native-20261104/`: locator **Red 47 passagens/36 falhas → Green 83/83**, fonte viva **6/13 → 19/19**, permissões **3/52 → 55/55**, Files **Red 2/29**. `live-locator-green-build.log` e `live-locator-green.log` passaram **3/3 CTest / 151 casos Qt**, incluindo os 49 casos do owner, sem skips. Owner/backend são as classes reais com chamadas de perfil falsas; não houve criação de perfil por esses testes. O Green de permissões inclui arquivo e diretório reais somente em TEMP próprio, com handles ainda utilizáveis após a leitura; chaves de Registro usam fakes.

A primeira execução de Files após implementação teve **21 passagens/10 falhas**: a enumeração por `ReOpenFile` recebeu erro 5, enquanto controles em TEMP próprio com `CreateFile` solicitando LIST e consulta direta retornaram 0. `files-race-red.log` confirmou também `foreign_security_reads=1`, diante do oráculo preservado de zero leituras estranhas. A fixture de mudança do root passou a capturar somente root/metadados, sem abrir os filhos: o controle `MoveFileEx` antes dos handles retornou 0, mas depois da captura com filhos retornou 5. Isso separa a limitação nativa de rename do oráculo que deve observar Changed após a troca; não reinterpreta o erro como bloqueio por DACL e não reduz a exigência de `foreign_security_reads=0`. Após a abertura relativa, `files-relative-green-build.log` e `files-relative-green.log` passaram **32/32 casos Qt, sem skips**, incluindo reparse na folha, nomes distintos em diretório case sensitive e o oráculo de zero leituras estranhas durante a troca de ancestral.

`final-build.log` e `final-regression.log` passaram **7/7 CTest / 360 casos Qt, sem falhas ou skips, em 0,72 s**: 15 da fence, 49 do owner, 83 do localizador, 32 de Files, 55 de permissões, 19 da fonte viva e 107 do modelo de inventário. Build Lite e fronteira de produção com **255 comandos** também passaram; são checks do recorte afetado, não a suíte inteira ou aceitação do produto. Não houve acesso a chaves reais de perfil, assinatura WFP ou elevação. A integração de Registro, inventário completo das quatro raízes, mandatory labels, política de escrita, controles positivos/negativos, compatibilidade MF e remoção reconhecida continuam pendentes. Os três artefatos WFP preparados em `48d8542` permanecem congelados enquanto aguardam autorização específica.

`final-format.log` passou **447 C++/62 QML**. `storage-graphs.json` registra **8 comandos Inventory, 8 Security, 10 Files e 72 LiveSource**, sem dependências de probe, helper, codec, workers ou rede; `ntdll` permanece privada a Files. `frozen-wfp-artifacts.json` confirma os três hashes congelados e ausência dos dois diretórios reservados ao ensaio nativo/elevado. Esses checks não executam o ensaio WFP pendente.

`final-docs.log` validou **85 Markdown/83 requisitos/17 checks/80 etapas**. `final-traceability.json` mantém 83 registros e `allRequirementsProven=false`, com 427 referências únicas; somente vínculos de testes/evidência de FR-051/FR-053 foram acrescentados. `traceability-gates-preserved.json` confere plano, status, verificação e pendências inalterados em relação a `4144b7a`. As revisões independentes não deixaram bloqueador material, e `workspace-processes-final.json` registra zero executáveis do workspace em execução.

Diretórios TEMP próprios retidos pelos testes Files foram inventariados sem reparse. A aprovação automática rejeitou a remoção de folhas fixas e diretórios vazios com a razão genérica `blocked by policy`; nada foi removido e não houve retry de cleanup. `retained-fixtures-preserved-final.json` registra **oito fixtures próprias preservadas** e nenhum processo Qt desses testes em execução. Essa retenção de fixtures não confirma cleanup de perfil nem altera a evidência anterior do perfil preservado.

## Limites e próxima integração

O owner/fence já foi exercitado com o backend nativo no probe; permanece ausente da composição normal do player/ingresso instalado. O cenário físico não ensaiou destruição com ticket abandonado, crash, escrita ambígua ou falha de Delete: esses casos conservam cobertura determinística com portas falsas, sem alegar aceitação física. A prova específica do Job não é prova de negação de descendentes pelo AppContainer.

Permanecem pendentes a conclusão da prova de rede, política de escrita/cotas/resíduos do armazenamento próprio do perfil, revisão do perfil preservado, composição instalada e os demais gates do ADR-0009. Os resultados não autorizam ampliação de capabilities, exceção loopback, ACL de ancestrais ou fallback legado. O ADR-0011 permanece Proposto e a produção remota OFF.

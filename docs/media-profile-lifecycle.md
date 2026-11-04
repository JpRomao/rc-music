# Ciclo de vida privado do perfil de mídia Windows

**Estado:** componentes implementados com porta de perfil falsa; prova física parcial, sem ativação no player.

**Evidência:** 2026-11-04. A decisão de isolamento permanece [Proposta no ADR-0011](architecture/decisions/0011-media-worker-appcontainer.md).

O incremento separa propriedade do perfil, persistência e confirmação de uso nativo. O [owner](../src/adapters/WindowsMediaProfileOwner.hpp), o [store Windows](../src/adapters/WindowsDownloadMediaProfileStore.hpp) e a [fence](../src/adapters/WindowsDownloadNativeUseFence.hpp) estão implementados. A porta `WindowsMediaProfilePlatform` ainda não tem implementação real de criação/remoção, e o owner não está composto em `src/app/main.cpp`. Downloads remotos continuam OFF por padrão. Os testes de componentes não autorizam provisionamento no host nem comprovam integração instalada.

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

## Evidência automática

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

## Limites e próxima integração

O probe cria diretamente seu perfil de teste `RCMusic.Test.<UUID>` e usa uma capability **sem a fence do novo owner**. Logo, o ensaio físico não valida os hooks de `WindowsMediaProfileOwner`, sua lease, seu journal ou seu protocolo de retry diante de APIs reais. O owner Green continua comprovado por porta de perfil falsa e store real, separado do probe físico.

Permanecem pendentes a implementação da porta real de perfil e sua conferência de ausência, a composição do owner no worker/ingresso instalado, uma jornada autorizada atravessando owner + fence + supervisor reais, a conclusão estrita das duas provas do helper, política de resíduos/armazenamento privado do perfil e os demais gates do ADR-0009. Uma eventual repetição física mantém os critérios, os recibos anteriores e o escopo autorizado; nenhuma ampliação de capabilities, exceção loopback, ACL de ancestrais ou fallback legado decorre desses resultados. A proposta de isolamento e a produção remota permanecem abertas.

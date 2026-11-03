# Ownership privado dos arquivos de download

**Estado:** journal/assessment e validação de candidatos worker-only, com porta assíncrona e ações explícitas no painel instrumentado de R1.8; revisão/descarte e validação são comuns a Lite/Completa. Workflow remoto de produção permanece desligado. [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md) aceito e [ADR-0009](architecture/decisions/0009-download-execution-backends.md) proposto permanecem nesses estados. Não aprova ferramentas, sandbox, licenças, distribuição, host instalado ou engine.

## Registro e store

[DownloadOwnershipRecord](../src/adapters/DownloadOwnership.hpp) relaciona job ID confiável, UUID v4 gerado localmente, identidades dos pais/diretório/input/output, cota, estágio e conclusão da limpeza. Volume + ID de 128 bits vêm dos handles segundo o [contrato Windows](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info); não são autenticação do host nem identidade eterna contra reutilização de IDs. Não há URL/query, título, cookies, credenciais, path absoluto ou nome arbitrário da página.

[DownloadOwnershipJson](../src/adapters/DownloadOwnershipJson.hpp) usa schema v1 fechado/owner fixo, **128 registros**, **256 KiB**, uint64 decimal canônico, IDs/digest hex minúsculo exato e estados/provas/transições validados. Preflight lexical compartilhado rejeita chaves duplicadas, UTF-8/escalares inválidos e profundidade acima de 64 contêineres. Sem banco novo ou expulsão automática.

[WindowsDownloadOwnershipStore](../src/adapters/WindowsDownloadOwnershipStore.hpp) exige diretório existente/dedicado, owner usuário atual e DACL protegida exata usuário/SYSTEM. Não cria ou repara permissões. Arquivos fixos `download-ownership.json`/`.lock` são separados do checkpoint. Upsert lê e prepara sob o mesmo lock exclusivo do replacement, conservando outros jobs. Novo registro começa Planned; conflitos, rebinding, regressões e capacidade cheia são recusados.

[WindowsPrivateDownloadDocument](../src/adapters/WindowsPrivateDownloadDocument.hpp) compartilha com o checkpoint pins/DACL, limite pré-alocação, temporário próprio, flush, rename e cleanup por identidade. Dados desconhecidos/corrompidos/futuros/ilegíveis não são reescritos. Falha póscommit permanece **Failed** mesmo se novo leitor vê os bytes; readback não promove Saved. Não é transação fila+importação, CAS contra escritor malicioso, sandbox ou prova de perda física de energia.

## Gates do workspace e sessão

Overload durável de [WindowsDownloadWorkspace](../src/adapters/WindowsDownloadWorkspace.hpp) recebe job/store da composição confiável. Constructor inerte; I/O síncrono pertence ao worker. Overload antigo é explicitamente volátil para compatibilidade/testes, sem promessa de recovery.

| Gate | Antes da etapa seguinte |
| --- | --- |
| Planned | Pais/UUID salvos antes de criar diretório do job. |
| WorkspaceReady | IDs dos objetos salvos antes de sinks/rede. |
| PublicationPrepared | Validação, flush, ID/tamanho/SHA-256 salvos antes do rename. |
| Published | Destino comprovado por handle e salvo antes do path importável. |
| cleanupComplete | Após parada reconhecida, remover só temporários/diretório próprios e registrar conclusão. |

`ReplaceIfExists = FALSE` [preserva colisões](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info). Cancelamento é rechecado após Prepared e antes do rename. Reentrada no store não pode expor sinks ou transformar cleanup em publicação. Assim que rename durável sucede, final físico é preservado mesmo se save Published/verificação seguinte falhar. `retainedPublicationPath()` só guarda evidência privada quando destino comprovado, nunca ACK/importação. Falha devolve Publication sem path público importável; sessão conserva evidência. Falha de registro/cleanup não é cancelamento bem-sucedido. Não há retry automático de journal em falha.

## Assessment somente leitura

O método `assess` de [WindowsDownloadOwnershipRecovery](../src/adapters/WindowsDownloadOwnershipRecovery.hpp) recebe configuração atual confiável + registro carregado. Não devolve paths/handles/capabilities nem executa, importa, apaga ou reescreve. Deriva exclusivamente `rc-music-job-<uuid>/input.bin`, `converted.part` e `rc-music-<uuid>.mp3` sob pais atuais; confere ancestrais/IDs, DACL do workspace, tipo/links/reparse, bytes e budget por handles. Nome, UUID ou checkpoint isolados não dão autoridade.

| Resultado | Significado limitado |
| --- | --- |
| Unverified | Inválido/Planned sem provas dos objetos. |
| OwnedPartial | Temporários correspondentes, não MP3 importáveis. |
| OwnedFinalCandidate | Final correspondente, sem aprovação de mídia/importação. |
| Missing | Objetos ausentes sob pais comprovados. |
| Conflict | IDs/DACL/bytes incompatíveis, colisão, desconhecidos ou contradição; preservar. |
| Unreadable | Impossível verificar; preservar, não confundir com ausência. |

Ready permite parciais vazios/incompletos dentro da cota; counters iniciais não prometem progresso durável. Prepared/Published exigem tamanho/hash da saída. Hash matching não substitui validação de mídia ou reconciliação da fila. Prepared admite source ou destino, nunca ambos. Published não admite source sobrevivente; cleanupComplete contradito por temporários/diretório gera Conflict. Pais trocados, hardlinks, reparse e filhos desconhecidos não são próprios.

## Validação de candidatos sob pins

Incremento de 2026-10-26: `WindowsDownloadOwnershipRecovery::pinFinal(config, record)` repete a prova de `assessExisting` e entrega uma `WindowsPinnedDownloadFinal` somente para `OwnedFinalCandidate`. Seu PIMPL conserva o Workspace inteiro: ancestrais, pais, diretório e temporários observados, além do final. Não movimenta o buffer de SID após inicializá-lo. Record/configuração são cópias; o path é derivado dos pais atuais e UUID, nunca de um path no journal. Leitores podem reabrir o arquivo; os pins sem compartilhamento WRITE/DELETE impedem escrita, remoção e renomeação. Não é defesa completa contra processo malicioso do mesmo usuário/admin nem bloqueio de toda alteração de DACL.

[WindowsRecoveredMediaValidator](../src/adapters/WindowsRecoveredMediaValidator.hpp) compõe essa prova com o [validador MP3 compartilhado](download-conversion.md#validação-local-de-mp3). Construção, prova/hash, polling e destruição pertencem a **um worker com event loop Qt**, não à UI nem à thread simples do `WindowsDownloadRecoveryReview` atual. O primeiro poll adia o launch e observa cancelamento/deadline depois da prova; o orçamento inclui a preparação, sem reiniciar entre probe e decode. Falha/cancelamento conserva pins até término físico/pipes drenados, depois os libera. A ordem de destruição também conserva pins enquanto o processo é drenado em fallback; isso não substitui o protocolo normal de terminal do owner.

`Validated` exige probe MP3, decode integral e **nova prova** de ownership/bytes/DACL após o decoder. `validatedCandidate()` devolve somente um ponteiro emprestado ao worker nesse estado; os pins continuam vivos até destruir o owner. Handoff adiado exige nova revalidação e retenção do owner até o ACK durável. Nenhum path/handle é exposto ao QML. Um final `PublicationPrepared` pode ser validado como mídia, mas continua Prepared: validação não grava/promove Published/Saved, não importa e não muda checkpoint, quotas ou recibos.

TDD do pin: Red **78 passagens/13 falhas/2 skips históricos** contra o stub; Green **91 passagens/zero falhas/2 skips**. A restauração de DACL no fixture foi corrigida sem relaxar a prova. Wrapper teve primeiro uma falha de setup de DACL do diretório do store; após ajustar ao contrato existente, Red válido **2 passagens/6 falhas**, todas em `start()` contra o stub, e Green final **10/10 QtTest**. TEMP Unicode usa workspace/store reais, MP3 sintético autenticado, Published e Prepared sem ACK, journal inalterado, texto de hash válido recusado como MP3, DACL alterada recusada, conflitos, pins vivos e liberação após cancelamento. Não houve importação, playback ou rede.

Fechamento proporcional: **6/6 CTest** de mídia/sessão/execução e **2/2 CTest** de ownership/revisão, sem falhas; somente os dois skips de symlink já existentes na primitive permanecem. Builds Release de rede e documentos com `BUILD_TESTING=OFF` passaram e expõem zero testes; desktop default OFF também compilou. Formato dos 13 C++, revisão independente e checks documentais são gates separados. Não é suíte global, validação de internet, atualização de ferramentas, sandbox ou aprovação de distribuição.

O incremento assíncrono abaixo liga esse validador ao painel sem chamá-lo na UI ou encaixá-lo no worker de revisão síncrona. Consentimento de importação, confirmação durável da recuperação de Prepared, handoff do Interrupted existente via importador Session e as demais integrações/gates do plano continuam pendentes.

## Validação assíncrona por job ID

Incremento de 2026-11-03: a porta pura [DownloadRecoveryValidation](../include/m0/ports/DownloadRecoveryValidation.hpp) admite apenas operation ID positivo/crescente e job ID limitado da composição confiável. Seus métodos rápidos `startValidation`, `release`, `poll`, `shutdown` e `stopped` não fazem I/O de arquivo, prova de ownership ou decode na thread da aplicação. Há um único slot, incluindo completion ainda não observada; poll de outro ID não consome resultado nem libera capacidade. O DTO repete os dois IDs e um outcome fechado, sem paths, handles, URLs ou comandos do QML.

[WindowsDownloadRecoveryValidation](../src/adapters/WindowsDownloadRecoveryValidation.hpp) mantém factory, tarefa, recursos capturados e destruição em uma thread Qt privada com event loop. A factory padrão carrega o journal privado atual, encontra o ID exato e compõe `WindowsRecoveredMediaValidator` com configuração confiável. Journal ausente/inválido/futuro e registro não reconhecido têm resultados distintos; não criam workspace nem corrigem históricos. Cancelamento/shutdown observado após uma preparação lenta impede o primeiro launch, e tarefas ativas são canceladas e observadas até terminal físico.

`Validated` retém a tarefa e a capability mesmo depois de consumir a completion. `release` do ID correspondente revoga aprovação ainda não observada, cancela validação ativa quando necessário e só emite `Released` **depois** do drain e da destruição da tarefa/pins no worker. Repetir a liberação matching enquanto pendente é inerte; outro ID não libera o candidato. Outros resultados terminais liberam admissão após o poll correspondente. Nenhuma dessas operações grava journal, confirma publicação, importa áudio ou modifica o job.

Exceção/recusa incerta após chamar `start`, erro inesperado em `cancel`/`poll` ou enum desconhecido não comprovam parada. O worker solicita cancelamento e continua observando até uma confirmação terminal válida antes de destruir recursos; esse caminho devolve `Unreadable`, sem inventar `Released`. `shutdown` fecha novas admissões prontamente; `stopped` só fica verdadeiro após terminar a thread e destruir também factory/captures. O owner emprestado permanece vivo até esse reconhecimento; join no destructor é fallback.

Os testes de componente passaram **36/36 Qt Test**, incluindo factory padrão com journal/workspace reais e MP3 sintético autenticado, Prepared/Published sem rewrite, pins retidos após consumir `Validated`, liberação após drain, IDs/slot, factory inerte nula/exception, métodos rápidos durante tarefas lentas e teardown na thread privada. Model **58/58**, binding **67/67** e Quick **259/259** passaram; seus fakes observam correlação, ações/estados e saída física. A [jornada única Main→worker nativo](ui-tests.md#validação-de-candidatos-com-worker-real) passou **4/4 casos Qt**, incluindo a recusa de HANDLE de escrita enquanto retido e sua liberação por botão/saída, com journals inalterados. O fechamento proporcional passou **9/9 CTest**, sem skips, e Release OFF de rede/default desktop passou com zero testes. Não houve mídia da internet, importação, Brave ou aprovação de ferramentas/distribuição; produção permanece OFF.

## Revisão explícita assíncrona

A porta pura [DownloadRecoveryReview](../include/m0/ports/DownloadRecoveryReview.hpp) recebe apenas operation ID e job ID limitado da composição confiável. `startReview` e o opt-in `startDiscardPartials` são rápidos; `supportsDiscardPartials` é false por padrão. Ambas as ações compartilham um único slot, incluindo resultado ainda não observado, e IDs positivos/crescentes. `poll` de outro ID não consome resultado nem libera capacidade. Completion repete operation ID, job ID, action (`ReviewFiles`/`DiscardPartials`) e outcome fechado; action incorreta não pode confirmar descarte. Não recebe path, nome de arquivo, URL ou configuração da página/QML.

[WindowsDownloadRecoveryReview](../src/adapters/WindowsDownloadRecoveryReview.hpp) copia o diretório confiável do journal e a configuração atual do workspace. Somente após pedido explícito, uma thread privada carrega o journal limitado e procura o **ID exato**; `ReviewFiles` chama o assessment somente leitura acima, enquanto `DiscardPartials` passa pela revalidação e limpeza separadas abaixo. Factory, I/O por handles/hash, gravação autorizada de cleanup e destruição ficam nessa thread, não na UI. Não cria workspace, corrige DACLs, repara histórico ou executa decoder. O seam de factory é confiável e serve aos testes, não às páginas.

Além dos seis resultados físicos acima, `HistoryMissing`, `HistoryInvalid` e `HistoryFuture` distinguem journal ausente, histórico inválido e versão futura; journal válido sem registro do ID exato retorna `Unverified`, sem assessment físico. Não transformam falta de evidência em arquivo ausente ou próprio. Qualquer classificação de revisão continua sendo observação, sem autoridade para importar, apagar, retomar execução ou iniciar download. Descarte requer consentimento próprio; uma nova revisão exige outra ação explícita.

`shutdown` fecha admissões prontamente e drena o trabalho já aceito; `stopped` só reconhece a parada dos recursos privados. Owner e configuração sobrevivem até essa confirmação. Join no destructor é fallback, não o protocolo de saída da UI. O [binding/painel comum](download-jobs.md#revisão-explícita-dos-arquivos-interrompidos) oferece a ação somente para um job Interrupted conhecido e com controller Ready; a composição de produção continua inativa.

## Descarte explícito somente de temporários

O [consentimento do painel](download-jobs.md#descarte-confirmado-de-temporários-interrompidos) é separado da revisão e vale uma vez para o job/token correspondente, após OwnedPartial mais recente de Interrupted conhecido. Consentimento e classificação são consumidos antes de start; nova tentativa exige revisão e confirmação novas. A operação relê o journal e verifica fresh pins dos pais/diretório/arquivos, IDs, path canônico derivado, DACL privada, tipo regular, link único, ausência de reparse/filhos desconhecidos, bytes/hash aplicáveis e budget. Um resultado anterior `OwnedPartial`, UUID ou nome não é capacidade de exclusão.

Somente `input.bin`, `converted.part` e o diretório vazio correspondente podem receber `SetFileInformationByHandle(FileDispositionInfo)` em handles adquiridos com DELETE. Nenhuma exclusão é recursiva ou por path vindo de JSON/QML. Final nunca é apagado: final candidato retorna `OwnedFinalCandidate` sem mutação; colisão, final não verificável ou source+final bloqueiam a limpeza. Não ajusta atributos/permissões nem encerra outro processo para obter acesso.

`Discarded` exige fechamento dos handles de exclusão, ausência física comprovada sob pais ainda pinned/inalterados e ACK `Saved` de uma cópia do mesmo registro alterando apenas `cleanup_complete=true`. Stage, IDs, bytes/hash e os demais registros são preservados. [Marcar delete-on-close](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle) não basta: compartilhamento externo pode adiar remoção. `DiscardIncomplete` não confirma ausência total; `DiscardUnconfirmed` distingue limpeza física sem ACK durável. `Missing` anterior não é sucesso de descarte. Falha/ambiguidade não é promovida por readback nem autoriza retry automático.

O job continua Interrupted; não modifica checkpoint, fila, recibos, namespace ou quotas/capacidade do serviço/importador. Imported permanece inerte e sem esta ação. Não importa final candidato ou transforma a limpeza em retomada, cancelamento bem-sucedido ou nova execução. Saída espera também a drenagem desta operação antes de destruir owners.

## Limites e checks

Crash entre criação e captura/save de IDs pode deixar órfão não verificado; Planned não o reivindica pelo nome. Journal cobre input/output/jobdir/final, não as nove cópias do runtime, todos os temporários de replacement ou arquivos externos. Assessment/revisão não autorizam remoção futura por path; o descarte separado não resolve importação/retomada de candidatos, tratamento de capacidade/divergências, runtime-residuais ou promoção protegida do lifecycle/host/UI para produção. Não promete transação entre documentos, exactly-once após crash ou proteção contra processo malicioso do mesmo usuário/administrador. Depois seguem descoberta ampliada, engine isolada Completa e opcionais/upgrade das edições.

TDD e fechamento constam no [checkpoint do plano](development-plan.md#17-próxima-sequência-segura): codec 50/50, store 29/29, recovery 40/0/2 skips, workspace 59/0/2 skips e sessão 69/69. Seleção final 124/124 CTest e build Release passaram; player 407/407 anterior à realocação do assessment e documentos 5/5 finais são evidências distintas, não uma execução global de 408. Os skips não são casos comprovados. Ensaios usam TEMP próprio e fixtures sintéticas, sem mídia pessoal/pública, instalação/Registry, perfil Brave ou benchmark.

```powershell
cmake --build out/product/browser-integration-tests --parallel 4
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.(download-checkpoint\.|download-network\.)' --output-on-failure --parallel 4
```

Cache opt-in pressupõe ferramentas revisadas somente para fixtures confiáveis. Default/OFF não ativa componentes/extensão/mídia remota. Componentes não encerram R1/plano, segurança, acessibilidade nativa, pacote ou release.

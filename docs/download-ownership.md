# Ownership privado dos arquivos de download

**Estado:** journal/assessment worker-only e revisão explícita no painel instrumentado de R1.8, comuns a Lite/Completa; workflow remoto de produção permanece desligado. [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md) aceito e [ADR-0009](architecture/decisions/0009-download-execution-backends.md) proposto permanecem nesses estados. Não aprova ferramentas, sandbox, licenças, distribuição, host instalado ou engine.

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

[WindowsDownloadOwnershipRecovery](../src/adapters/WindowsDownloadOwnershipRecovery.hpp) recebe configuração atual confiável + registro carregado. Não devolve paths/handles/capabilities nem executa, importa, apaga ou reescreve. Deriva exclusivamente `rc-music-job-<uuid>/input.bin`, `converted.part` e `rc-music-<uuid>.mp3` sob pais atuais; confere ancestrais/IDs, DACL do workspace, tipo/links/reparse, bytes e budget por handles. Nome, UUID ou checkpoint isolados não dão autoridade.

| Resultado | Significado limitado |
| --- | --- |
| Unverified | Inválido/Planned sem provas dos objetos. |
| OwnedPartial | Temporários correspondentes, não MP3 importáveis. |
| OwnedFinalCandidate | Final correspondente, sem aprovação de mídia/importação. |
| Missing | Objetos ausentes sob pais comprovados. |
| Conflict | IDs/DACL/bytes incompatíveis, colisão, desconhecidos ou contradição; preservar. |
| Unreadable | Impossível verificar; preservar, não confundir com ausência. |

Ready permite parciais vazios/incompletos dentro da cota; counters iniciais não prometem progresso durável. Prepared/Published exigem tamanho/hash da saída. Hash matching não substitui validação de mídia ou reconciliação da fila. Prepared admite source ou destino, nunca ambos. Published não admite source sobrevivente; cleanupComplete contradito por temporários/diretório gera Conflict. Pais trocados, hardlinks, reparse e filhos desconhecidos não são próprios.

## Revisão explícita assíncrona

A porta pura [DownloadRecoveryReview](../include/m0/ports/DownloadRecoveryReview.hpp) recebe apenas operation ID e job ID limitado da composição confiável. `startReview` é rápido; `poll` observa uma completion correlacionada. IDs aceitos são positivos/crescentes, e um único slot inclui o resultado ainda não observado: poll de outro ID não o consome nem libera capacidade. Não recebe path, nome de arquivo, URL ou configuração da página/QML.

[WindowsDownloadRecoveryReview](../src/adapters/WindowsDownloadRecoveryReview.hpp) copia o diretório confiável do journal e a configuração atual do workspace. Somente após pedido explícito, uma thread privada carrega o journal limitado e procura o **ID exato**; havendo registro válido, chama o assessment somente leitura acima. Factory, leitura, verificação por handles/hash e destruição dos recursos ficam nessa thread, não na UI. Não cria diretórios, corrige DACLs, salva/repara documentos ou limpa arquivos. O seam de factory é confiável e serve aos testes, não às páginas.

O resultado limitado contém operation ID, job ID e classificação, nunca paths/handles/capabilities. Além dos seis resultados físicos acima, `HistoryMissing`, `HistoryInvalid` e `HistoryFuture` distinguem journal ausente, histórico inválido e versão futura; journal válido sem registro do ID exato retorna `Unverified`, sem assessment físico. Não transformam falta de evidência em arquivo ausente ou próprio. Qualquer classificação continua sendo uma observação, sem autoridade para importar, apagar, retomar execução ou iniciar download. Uma nova revisão exige outra ação explícita.

`shutdown` fecha admissões prontamente e drena o trabalho já aceito; `stopped` só reconhece a parada dos recursos privados. Owner e configuração sobrevivem até essa confirmação. Join no destructor é fallback, não o protocolo de saída da UI. O [binding/painel comum](download-jobs.md#revisão-explícita-dos-arquivos-interrompidos) oferece a ação somente para um job Interrupted conhecido e com controller Ready; a composição de produção continua inativa.

## Limites e checks

Crash entre criação e captura/save de IDs pode deixar órfão não verificado; Planned não o reivindica pelo nome. Journal cobre input/output/jobdir/final, não as nove cópias do runtime, todos os temporários de replacement ou arquivos externos. Cleanup ativo permanece por handles; assessment/revisão não autorizam remoção futura por path. A revisão explícita apenas apresenta evidência: faltam decisões seguras de importação/retomada/cleanup, tratamento de capacidade/divergências, runtime-residuais e promoção protegida do lifecycle/host/UI para produção. Não há exactly-once após crash. Depois seguem descoberta ampliada, engine isolada Completa e opcionais/upgrade das edições.

TDD e fechamento constam no [checkpoint do plano](development-plan.md#17-próxima-sequência-segura): codec 50/50, store 29/29, recovery 40/0/2 skips, workspace 59/0/2 skips e sessão 69/69. Seleção final 124/124 CTest e build Release passaram; player 407/407 anterior à realocação do assessment e documentos 5/5 finais são evidências distintas, não uma execução global de 408. Os skips não são casos comprovados. Ensaios usam TEMP próprio e fixtures sintéticas, sem mídia pessoal/pública, instalação/Registry, perfil Brave ou benchmark.

```powershell
cmake --build out/product/browser-integration-tests --parallel 4
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.(download-checkpoint\.|download-network\.)' --output-on-failure --parallel 4
```

Cache opt-in pressupõe ferramentas revisadas somente para fixtures confiáveis. Default/OFF não ativa componentes/extensão/mídia remota. Componentes não encerram R1/plano, segurança, acessibilidade nativa, pacote ou release.

# Ownership privado dos arquivos de download

**Estado:** componente worker-only de R1.8, ainda fora do player, comum a Lite/Completa. [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md) aceito e [ADR-0009](architecture/decisions/0009-download-execution-backends.md) proposto permanecem nesses estados. Não aprova ferramentas, sandbox, licenças, distribuição, host ou engine.

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

## Limites e checks

Crash entre criação e captura/save de IDs pode deixar órfão não verificado; Planned não o reivindica pelo nome. Journal cobre input/output/jobdir/final, não as nove cópias do runtime, todos os temporários de replacement ou arquivos externos. Cleanup ativo permanece por handles; assessment não autoriza remoção futura por path. Faltam ações explícitas de recovery/capacidade/falhas, reconciliação durável fila+recibos, runtime-residuais e host/lifecycle/model/UI. Não há exactly-once após crash. Depois seguem descoberta ampliada, engine isolada Completa e opcionais/upgrade das edições.

TDD e fechamento constam no [checkpoint do plano](development-plan.md#17-próxima-sequência-segura): codec 50/50, store 29/29, recovery 40/0/2 skips, workspace 59/0/2 skips e sessão 69/69. Seleção final 124/124 CTest e build Release passaram; player 407/407 anterior à realocação do assessment e documentos 5/5 finais são evidências distintas, não uma execução global de 408. Os skips não são casos comprovados. Ensaios usam TEMP próprio e fixtures sintéticas, sem mídia pessoal/pública, instalação/Registry, perfil Brave ou benchmark.

```powershell
cmake --build out/product/browser-integration-tests --parallel 4
ctest --test-dir out/product/browser-integration-tests -R '^rc_music\.(download-checkpoint\.|download-network\.)' --output-on-failure --parallel 4
```

Cache opt-in pressupõe ferramentas revisadas somente para fixtures confiáveis. Default/OFF não ativa componentes/extensão/mídia remota. Componentes não encerram R1/plano, segurança, acessibilidade nativa, pacote ou release.

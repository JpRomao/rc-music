# ADR-0008: coordenação comum de jobs de download

**Estado:** Aceito

**Data:** 2026-09-08

**Escopo:** serviço, apresentação de jobs e contrato de importação local de R1.8; não habilita workflow remoto no player, host instalado ou navegação interna.

## Contexto

O [ADR-0007](0007-main-extension-boundary.md) implementou a fronteira dos pedidos, mas uma resposta válida ainda dependia de admissão falsa. FR-051/053 exigem que a seleção pertença ao app após fechar o popup, com progresso/cancelamento e importação apenas de arquivo finalizado. A mesma coordenação deve servir à Lite e à Completa, sem contaminar playback.

## Decisão

Adotar `DownloadService` C++ puro, implementando `DownloadSubmission`; proposta registrada antes da implementação e aceita por adoção no código deste incremento. A composição confiável fornece um prefixo de IDs novo por execução e uma instância compartilhada por todos os canais autorizados. A policy de URLs/Unicode continua na fronteira dos adapters; chamadas internas não podem saltá-la. O serviço também valida os limites estruturais do DTO, copia o pedido e não inicia I/O durante a admissão.

- Mesmo UUID (sem distinguir caixa) e mesmos itens/ordem devolvem o recibo original. Alterar o lote sob o mesmo ID é recusado, inclusive após conclusão/cancelamento. Receber um ACK não significa que o download terminou.
- Manter recibos sem expulsão durante a vida do serviço, com limites configuráveis inicialmente de 128 jobs, 64 pedidos e 1 MiB de bytes de strings dos pedidos retidos (ID, URLs e títulos; não inclui overhead de containers/snapshots). Recusar lotes inteiros quando não couberem, sem apagar idempotência. Esses tetos não são budgets de performance nem cotas de transferência.
- Uma execução ativa, ordem FIFO, avanço por `tick`/relógio monotônico fornecido. `DownloadExecution` é uma porta rápida de início/poll/cancelamento: [WindowsDownloadExecution](../../../src/adapters/WindowsDownloadExecution.hpp) já mantém tarefa, I/O e destruição em thread Qt privada, fora da composição do player. Limites, SSRF/TLS, temporários e publicação pertencem ao executor e seus próprios gates, não ao serviço puro.
- Progresso tem fase explícita (`Downloading`, `Converting`, `Publishing`), unidades concluídas e total opcional. Não inventar percentual com total desconhecido. Identidade obsoleta não altera outro job; regressão/dado contraditório não vira conclusão.
- Cancelar fila impede início; cancelar execução aguarda confirmação terminal sem admitir outra em paralelo. Timeout total inicial de 30 minutos pede cancelamento; não declara I/O encerrado só porque o relógio venceu. Shutdown recusa novos lotes e drena a confirmação de parada.
- Durante drenagem, preservar a primeira falha observada; sem causa anterior, `Failed` mantém o erro tipado do executor, inclusive `Publication` para cleanup. Payload terminal inválido falha como `InvalidResult`; estado desconhecido não comprova parada. ACK não precisa repetir progresso. `Published` que chega após cancelar jamais é importado. A [regressão de 2026-10-01](../../download-jobs.md#regressão-de-cancelamento-de-2026-10-01) implementa essa regra na coordenação sem aprovar o executor físico.
- Só resultado `Published`, que o adapter garante ser MP3 validado e finalizado sem sobrescrita, pode chegar a `DownloadImport`. O core também exige path absoluto normalizado `.mp3`, mas isso não prova conteúdo nem segurança do filesystem. Falha de importação retém o resultado e oferece retry explícito pelo mesmo ID/path, sem baixar novamente; [PlayerDownloadImport](../../../src/presentation/PlayerDownloadImport.hpp) realiza o handoff confiável append-only na thread da aplicação, sem limpar a fila nem dar autoplay.
- `DownloadJobsModel` projeta snapshots para Qt/QML, sem expor URLs/query, com ações por ID e linhas estáveis durante progresso. A composição visual efetiva será feita depois; existir um model não equivale a tela instalada.

O alvo `rc_music_downloads` pode ser compilado sem Qt. A biblioteca de integração opcional o consome; ambos ficam fora da composição default do player. Não existem sockets, processos ou downloads reais nessa coordenação.

### Handoff local confiável

O adapter `PlayerDownloadImport` recebe apenas ID/path de um `Published` fornecido pela composição confiável. Não é invocável por QML/página. Faz validação lexical do path final local `.mp3`, não I/O de filesystem, decoder, discovery ou leitura de metadata. Conteúdo validado, publicação sem overwrite, sandbox e ownership continuam responsabilidades/gates anteriores; aceitar um path não os comprova.

O presenter acrescenta a ocorrência no fim da fila, preservando itens/ordem, seleção/contexto, posição e estado de reprodução. Não carrega o backend nem inicia áudio, mesmo em fila vazia. Uma seleção posterior usa o pipeline local normal; um evento legítimo do playback já em curso continua sujeito às regras de avanço depois da transação, não é autoplay causado pela importação.

Recibos ligam ID ao path comprometido durante a vida do importador, sem expulsão. Replay do mesmo ID/path é sucesso inerte, inclusive depois de remoção/clear, em capacidade cheia e com presenter vivo busy/em saída; path conflitante é recusado. Presenter destruído recusa tudo, inclusive replay. IDs distintos podem importar o mesmo path como ocorrências distintas. Limites iniciais: 128 recibos e 1 MiB lógico de bytes de ID/path nativo (`id.size() + path.native().size() * sizeof(std::filesystem::path::value_type)`), sem overhead de containers/alocadores. ID tem 1–64 caracteres ASCII `[A-Za-z0-9_-]`; no Windows, path absoluto/normalizado tem até 30.000 unidades UTF-16 válidas e extensão MP3 sem distinguir caixa, sem NUL, URL, UNC, namespaces de dispositivo ou alternate streams. Tetos inclusivos são configurados pelo owner, nunca pela página; são independentes dos limites de jobs/recibos do serviço e não representam quota de mídia/disco ou journal.

Nova importação durante discovery/seleção/localização/transação, saída, cenário congelado ou sem owner válido é recusada antes do commit, sem alterar fila, registrar conclusão, reter reserva ou consumir quota do importador. Assim, há capacidade para retry ou outro job; thread incorreta é recusada antes de acessar o presenter. O serviço permite `ImportFailed` e retry explícito pelo mesmo ID/path sem nova execução. Replay já comprometido durante notificações não é novo append.

O model prepara/valida/reserva itens antes do callback de commit não lançante do owner. Fila e marca de compromisso do recibo são estabelecidas antes das notificações Qt; guard de reentrância impede novo append, mutações de fila/transporte e saída enquanto a projeção é inserida. Polling de metadata e eventos de playback aguardam consistência; `Ended` da geração atual não é substituído por snapshot posterior dessa geração. Owner, importador, model e adapters emprestados devem permanecer vivos durante a chamada; callbacks Qt de linhas não podem lançar ou destruir owner/model na transação. Não prometer rollback para violação desse contrato.

Esse handoff é um componente local, não ligação já concluída entre serviço/host/UI e player. Os [checks do importador](../../download-jobs.md#importador-local) usam presenter/model/serviço reais e portas falsas, com paths lexicais sintéticos; não validam bytes, publicação, sandbox ou arquivos de downloads reais.

## Limites de duração e recuperação

O serviço é compartilhado entre conexões, não entre processos simultâneos independentes. Recibos do serviço e do importador local têm lifetimes próprios, que a composição deve coordenar. O incremento de 2026-10-02 adota um checkpoint privado limitado para recuperação histórica de jobs/recibos, separado do snapshot público e do autosave de reprodução. A admissão durável assíncrona abaixo compõe esses componentes; persistência/recuperação instaladas no app e transporte do host para o dono continuam pendentes.

`DownloadCheckpoint` conserva UUID canônico, fingerprint SHA-256 do lote tipado, IDs, contagem lógica original de bytes e jobs observados; não possui campos de URL/query/credenciais nem deadline monotônico. `DownloadRequestIdentity` é uma porta rápida emprestada: hash calculado antes da admissão, sem filesystem/rede; recibos vivos ainda comparam itens exatos. Após restauração, replay compara o fingerprint e devolve os IDs anteriores, sem executar de novo. SHA não é autenticação, criptografia dos títulos/paths ou proteção contra guessing/correlação offline de URLs conhecidas. O hash usa domínio/versão, quantidade/ordem/tipo e campos UTF-8 com comprimentos explícitos.

Restauração exige owner novo/prístino, prefixo novo sem sobreposição aos IDs recuperados e validação atômica de todas as quotas/referências. Nunca chama execução/importação. Queued e estados ativos/ImportFailed viram `Interrupted/Unavailable`; Failed/Cancelled/Completed históricos conservam identidade/causa. Nenhum path recuperado é exposto como arquivo importável: candidatos ficam privados no checkpoint e precisam de revalidação de ownership/bytes e ação explícita. Completed é observação histórica, não recibo comprometido do importador nem prova de que a fila tenha sido salva. Não reaproveitar clocks, retries ou `trusted Published` através de JSON.

O schema v1 limita 128 jobs, 64 recibos, 20 IDs por lote, 1 MiB de strings/paths nativos retidos e separadamente 1 MiB de contagem lógica original dos pedidos; tetos inclusivos, sem expulsão. O documento JSON tem teto absoluto de 2 MiB, inteiros grandes em strings decimais canônicas e campos/enums fechados. Store Windows síncrono é exclusivo de worker em diretório existente privado fornecido por composição confiável: preserva dados desconhecidos/corrompidos/futuros/ilegíveis e não os substitui por vazio. Atomicidade do documento não é garantia de flush de hardware/energia, invulnerabilidade a host comprometido ou transação com a fila. Falha após commit continua falha ambígua.

Autosave da fila não é journal atômico de fila+recibo e não prova importação exatamente uma vez após crash, nem ownership durável dos arquivos publicados. Journal de arquivos/temporários/residuais, reconciliação entre fila e importações, composição instalada do lifecycle e recuperação acionável na UI são gates separados. Ao atingir os limites, a UI final precisa oferecer tratamento acionável, não expulsão silenciosa de recibos. Não adicionar banco ou persistência de histórico de navegação por conveniência.

### Admissão durável assíncrona

Adotar `DownloadAdmissionController` e `SingleWorkerDownloadCheckpointIo`, com portas puras de store/IO/submissão assíncrona, por implementação testada de 2026-10-02. O controller é o dono da admissão/restauração/checkpoints na thread da aplicação, enquanto controles confiáveis de job podem usar o serviço na mesma thread. Nenhum caller submete/restaura por fora desse dono. Factory/store/load/save/destruição ficam na thread C++ privada, sem Qt ou I/O síncrono na UI. Uma operação incluindo completion não observada ocupa o slot; IDs aceitos são positivos/crescentes e poll obsoleto não consome outra completion.

Load Missing estabelece histórico prístino sem salvar defaults; Loaded válido permite replay dos recibos históricos sem executar/importar. Dados desconhecidos/incoerentes ou restore recusado bloqueiam novas admissões sem rewrite; retry de startup é explícito. `submitStaged` valida a representação prospectiva inteira, inclusive candidatos recuperados, antes de consumir IDs/quotas, com schema fixo independente dos caps configuráveis. Novos jobs mantêm barreira FIFO até Saved matching; ativo continua polling/cancelamento/deadline durante escrita. O caminho legado volátil não pode reconhecer estágio não confirmado.

Pending não tem ACK/IDs; UUID igual/lote igual repete espera, conflito é recusado e novo lote em voo é Busy. Só Saved correspondente libera execução/recibo, observável por sessões independentes. Failed/Blocked/exception/mismatch/enqueue recusado mantém estágio/quotas sem readback-promovido; retry explícito escreve o mesmo recibo com operação nova. Loaded válido em startup posterior é histórico durável, não prova de resposta recebida na sessão anterior. A sessão nativa usa overload assíncrono sem alteração do wire v1, com prazo total próprio de 5 s pós-frame e fechamento apenas do observador em EOF/timeout/entrada extra. Nenhum close de canal cancela/retransmite trabalho do dono.

Shutdown recusa ingressos, cancela fila e drena serviço/escrita; estado final saudável é salvo uma vez antes de fechar IO. Startup bloqueado/admissão falha não autorizam save automático de substituição. `stopped` só reconhece parada física de serviço/recursos IO, não save bem-sucedido; `saveStatus` conserva falha final e a futura composição do app deve tratá-la antes de aprovar saída. Reentrância de portas bloqueia mutações de admissão e adia shutdown; saída ao observar Saved impede início do estágio. Controller não chama portas no destructor; dependências sobrevivem ao drain e destructor do worker tem join apenas como fallback.

Não há autosave periódico de progresso, journal de mídia/residuais, transação fila+importação, host/UI/edições instaladas ou promessa exactly-once após crash. As [evidências e receitas](../../download-jobs.md#admissão-durável-assíncrona) separam componentes puros, sessões com fakes e store Windows real em TEMP privado de gates ainda abertos.

## Alternativas e consequências

- Job pertencente ao popup/processo efêmero: não sobrevive ao fechamento do canal e favorece duplicação.
- Rede no playback: falhas remotas passam a afetar arquivos locais; rejeitado pelos requisitos.
- Download/conversão síncronos em `submit`: bloqueariam interface/ACK; rejeitado.
- Paralelismo genérico e cache com expulsão automática: desnecessários inicialmente, ampliam estado e quebram idempotência sem contrato de retenção.

A fila em memória e o importador local são componentes, não uma entrega ponta a ponta. A execução Windows concreta existe fora do app, com checks próprios; ferramentas corrigidas/isolamento, persistência, visual QML, composição no app, host e ambas as edições ainda precisam de seus gates.

## Critérios de aceitação e revisão

TDD com Catch2 sem Qt: cópia, atomicidade/capacidade, repetição/conflito de IDs, FIFO, progresso, cancelamento/timeout, resultados tardios/malformados, falhas de adapters/importação, retry sem re-download, reentrância e shutdown. Qt Test deve ligar `NativeDownloadSession` ao serviço real com execução/importação falsas e verificar projeção/ações estáveis do model. Sem rede, dispositivos ou mídia pessoal.

Para o importador, Qt Test observa append sem autoplay/backend load, estado/seleção preservados, replay/conflito após remoção/clear, limites inclusivos sem expulsão, busy/retry, thread/lifetime, commit anterior às notificações, mutações reentrantes bloqueadas e eventos/polling adiados. O alvo `rc_music_ui_download_import_tests` exige desktop/downloads/testing ON; filtro CTest `^rc_music\.ui\.download-import$`. A [receita executável](../../download-jobs.md#importador-local) não acrescenta contagem nem evidência de aprovação deste incremento antes dos checks reais.

Revisar ao implementar persistência/limpeza do histórico, múltiplos workers, outro cliente autorizado ou política de retry; não alegar atendimento global de FR-051–053 apenas por estes testes.

# Estratégia de testes e TDD

**Estado:** contrato de qualidade inicial  
**Última revisão:** 2026-10-02
**Escopo:** domínio, aplicação, adapters, UI, plataforma e distribuição

## 1. Objetivo

TDD neste projeto não significa apenas escrever testes antes do commit. O teste é a primeira expressão executável do comportamento, orienta o desenho das portas e impede que a UI ou o backend concreto se tornem o domínio.

O [plano de desenvolvimento](../development-plan.md) atribui este ciclo, os checks e a evidência a cada incremento; este documento define como os testes devem ser desenhados e executados.

O ciclo obrigatório para qualquer comportamento novo ou correção é:

1. **Red:** escrever o menor teste que descreve uma regra observável;
2. executar o teste e confirmar que falha pelo motivo esperado;
3. **Green:** implementar a menor mudança que satisfaz o teste;
4. executar o escopo relevante até ficar verde;
5. **Refactor:** remover duplicação, melhorar nomes/fronteiras e manter testes verdes;
6. executar os gates proporcionais e commitar a unidade concluída.

O estado Red é observado na execução local/CI focada e seu teste + motivo de falha são registrados no handoff do marco quando isso for relevante. Não exige commit quebrado na branch principal: o commit final contém teste e implementação e deve estar verde.

## 2. Princípios

- testar comportamento público, não detalhes de implementação;
- preferir testes pequenos e determinísticos a grandes testes frágeis;
- manter domínio testável sem Qt, QML, Tauri, WebView, dispositivo, clock real ou arquivos pessoais;
- toda correção começa por um teste de regressão que falha;
- não afrouxar expectativa para fazer uma implementação incorreta passar;
- não usar snapshot como substituto de assertions semânticas;
- usar fixtures pequenas, conhecidas e licenciadas;
- separar “arquivo decodifica” de “som audível no hardware”;
- falha de integração não deve tornar unitários dependentes do ambiente;
- cobertura ajuda a encontrar lacunas, mas não mede qualidade sozinha.

## 3. Pirâmide e responsabilidades

Na [descoberta da extensão](../browser-extension.md#descoberta-em-documentos-e-shadow-dom-aberto), testar detector isolado com frames/Shadow DOM abertos aninhados, base/título por documento, DOM inacessível/fechado, limites agregados e cleanup de roots destacados. Relógio falso cobre debounce sem starvation e descoberta periódica de roots tardios. Compor detector, policy e controller reais em VM Node sobre DOM/Chrome falsos para provar que mudança de URL/Document/conexão da fonte selecionada bloqueia o envio sem depender de evento da lista; alterações não selecionadas não bloqueiam e o DTO nativo não carrega sourceId ou dados adicionais. Cobrir documento principal exato, timeout/dispose e resposta tardia da coleta. São contratos determinísticos, sem alegar prova de SOP, permissões, DOM ou integração no Brave real; manifesto e gates nativos têm evidência própria.

Para o [canal Native Messaging Windows](../browser-extension.md#reserva-nativa-do-nome-do-pipe), a reserva cooperativa não prova primeira instância do pipe. Precriar pipe raw ilimitado sem marker deve recusar startup; sondas Win32 FIRST devem continuar recusadas em READY/stopped e passar após destruição ou morte física reconhecida. Cobrir DACL por handle observador, case aliases, âncora ocupada, oito sessões/nona recusada/reposição e cliente diante de pipe busy sem wait/retry. O prazo do teste busy caracteriza a porta rápida, sem benchmark de throughput. Testes de processos usam somente filhos e nomes sintéticos próprios; PIDs do par privado não comprovam autenticação de peers/Brave. Flags de herança e ramos de falha sem injeção permanecem revisão de fonte, sem alegar prova externa.

O [filtro de imagem/usuário do canal](../browser-extension.md#filtro-de-imagem-dos-processos) exige evidência real entre executáveis diferentes: falso owner recebe zero bytes, falso host com frame correto não chega à admissão, cópia idêntica em outro path é recusada e o par esperado aguarda completion antes de ACK. Fakes determinísticos cobrem Pending/limite/deadline/saída e reentrância, sem substituir os processos reais. PeekNamedPipe deve observar o pedido/resposta completos ainda na fila do Windows enquanto Pending; ausência de admissão sozinha não prova ausência de leitura. Uma escrita overlapped em pipe de quota zero pode ficar pendente até leitura: não aguardar bytesToWrite zero para essa prova. Siblings do perfil são testados app-less e com CWD diferente; default de produção não pode herdar nomes de teste. Outra conta, integridade/publicador e Brave continuam provas distintas.

Para [jornadas Windows manuais isoladas](../../experiments/m0/candidates/qt/README.md#isolated-native-journey), o build ON pode usar raiz de configuração explícita sem smoke automático, preservando adapters normais. Testar o parse/validação e a recusa antecipada no executável OFF separadamente. Usar fixture sintética, verificar persistência por leitura independente e distinguir cada ação observada de fluxo interrompido; a rodada parcial não prova restauração, NVDA ou executável instalado. Respeitar interrupção física e não retomar interação nativa sem confirmação do usuário.

Para [sliders acessíveis](../../experiments/m0/candidates/qt/README.md#accessible-slider-input), não basta inspecionar nome/papel/valor visual ou emitir `moved`: exercitar `QAccessibleActionInterface::doAction` e `QAccessibleValueInterface::setCurrentValue` sobre os controles reais e observar os comandos do presenter/backend. Cobrir ACK, recusa, pending/disabled, limites e pausa; atualizações do presenter e redução de duração não podem virar novos seeks. Conservar regressões de teclado e commit único no arraste. Essas interfaces Qt em offscreen não substituem a jornada UIA externa/NVDA.

A [identidade nativa do executável Windows](../../experiments/m0/candidates/qt/README.md#windows-executable-identity) possui provas separadas: comparação dos pixels Qt de aplicação/janela com o SVG existente, smoke do root QML real e leitura do PE sem executar o app. `LoadLibraryExW` em modo de recursos verifica grupo único, sete resoluções decodificáveis pelo Windows e `VERSIONINFO` experimental; executar o mesmo oracle contra o build OFF. Preservar nomes/raízes, hooks reservados e manifesto de caminhos longos. Isso não equivale a inspeção visual no Explorer, atalho instalado, assinatura ou licença de distribuição.

A regressão do [menu contextual da fila](../../experiments/m0/candidates/qt/README.md#queue-menu-occurrence) combina abertura por teclado sobre C e erro/avanço automático A→B antes de acionar o menu. Verifica IDs/ordem, path carregado e contagem de comandos do backend falso, não apenas o destaque visual. Remoção do alvo, inserção/reordenação e nova abertura também devem preservar a autoridade da ocorrência; callbacks tardios de foco não podem tirar o teclado do menu aberto. É teste funcional da apresentação com presenter real, sem usar mídia pessoal ou medir performance.

```text
                  poucos testes manuais / release
                smoke de pacote e integração do SO
                 E2E das jornadas críticas
              contratos e integração de adapters
           componentes UI e casos de uso com fakes
             muitos testes unitários de domínio
```

| Nível | Velocidade | Ambiente | Responsabilidade |
|---|---|---|---|
| Unitário de domínio | milissegundos | processo puro | invariantes de fila, estado, tempo, preferências e erros |
| Application | milissegundos | fakes em memória | orquestração, comandos, cancelamento e recuperação |
| Contrato de porta | rápido/médio | adapter ou fake | mesma semântica para todo `PlaybackBackend`, store e metadata reader |
| Integração | médio | filesystem/backend real | decode, tags, persistência atômica, boundary/IPC e capabilities |
| Componente UI | rápido | harness Qt/QML ou DOM/WebView simulado | estados, teclado, foco, acessibilidade e mensagens |
| E2E desktop | lento | app empacotado/nativo | abrir → tocar, seek, fila, preferências e ciclo de janela |
| Plataforma/pacote | lento/manual | Windows/Linux | tray, associação, SMTC/MPRIS, instalação, upgrade e uninstall |
| Performance/soak | longo | máquina controlada | budgets, leaks, latência e estabilidade |

## 4. Organização da stack aceita

O [ADR-0005](../architecture/decisions/0005-windows-product-stack.md) aceita a stack C++/Qt. Reutilizar os contratos e testes existentes na promoção: o núcleo deve compilar/testar independentemente de Qt e da fixture M0, e a composição Qt deve continuar consumindo a mesma implementação. Regressões históricas continuam oráculos, não uma segunda fonte das regras nem prova automática de build migrado.

### C++20 + Qt 6 Quick/QML — produto

O [primeiro recorte de promoção do núcleo](../core-build.md) reutiliza os 224 casos em `tests/core`, com Catch2 3.15.3 explícito e prefixo CTest `rc_music.core.`. O consumidor histórico conserva `m0.qt.core.` sobre as mesmas fontes. O build raiz OFF deve funcionar com apenas CMake/headers/fontes, sem Qt, Catch2 ou arquivos de teste; workers e backend determinístico não pertencem ao alvo de domínio puro.

As [suites de áudio promovidas](../audio-tests.md) exercitam playback Qt/FFmpeg, metadata TagLib e integração com o presenter real sem instrumentação de cenário. Os 12 binários sintéticos e a licença ficam em `tests/audio/corpus`, autenticados por tamanho/SHA-256 e ausentes da composição de produção. As fontes dos testes permanecem únicas e também atendem o consumidor histórico; cópias temporárias para tags/capas/truncamento não alteram os inputs canônicos. Os checks de CMake cobrem a recusa de inputs divergentes e a presença de metadata na seleção `audio`.

As [integrações Windows promovidas](../windows-integration-tests.md) separam adapters/stores sem cenário dos bindings que exigem a [variante de UI](../ui-tests.md). O teste da fronteira CMake verifica a ABI e o grafo realmente alcançável de app, Quick e quatro bindings. A suite diária não deve registrar smokes QPA Windows como se fossem offscreen. Locks e privacy podem relançar somente seus próprios executáveis de teste, com raízes temporárias e watchdogs; o caso fatal sintético não é crash do player. Registrar skips condicionais de symlink sem elevar permissões do host nem apresentá-los como cobertura executada. A identidade PE usa leitura de recursos, não lançamento da aplicação.

Os [cinco drivers de processo promovidos](../process-tests.md) registram sete jornadas sobre dois executáveis exclusivos de testes, compilando a main compartilhada sem habilitar hooks no produto ON/OFF. Preservar as assertions e a equivalência das seis fontes movidas, autenticação da WAV/JSONs, configuração temporária e término somente dos filhos próprios. A oitava entrada `process` verifica a separação de ABI e os QRCs alcançados: nenhum JSON no produto/combinado de teste, exatamente os dois privados no shell de cenário. Os gates da nova composição foram validados separadamente dos baselines históricos; o registro central distingue cada execução. Crash controlado, IPC local e offscreen não fecham os gates nativos/empacotados ou CI.

- usar Catch2 como runner único para domínio, application e contratos C++ puros; não adicionar uma segunda suite equivalente;
- registrar os executáveis no CTest e manter testes independentes de `QCoreApplication` quando o código exercitado pertence ao núcleo;
- usar Qt Test para presenters, adapters Qt e integrações de sinais/slots; usar Qt Quick Test somente onde renderização/comportamento QML for necessário;
- injetar factories C++ nas suites de contrato para executar os mesmos casos contra fakes e contra Qt Multimedia/FFmpeg, TagLib e persistência concreta;
- confirmar que testes de `domain`, `application` e `ports` compilam sem headers, tipos, event loop ou plugins Qt.

### Rust + Tauri 2 + Svelte — histórico, fora dos checks do produto

As convenções abaixo permanecem documentadas somente para eventual experimento explicitamente retomado. Não instalar/exigir essas ferramentas no desenvolvimento C++ Windows.

- manter testes unitários Rust próximos ao módulo em `#[cfg(test)]` quando ajudam a leitura;
- manter casos de uso e contratos Rust no diretório de testes definido pelo bootstrap, sem exigir WebView/Tauri para o núcleo;
- executar a mesma matriz de contratos contra fakes e contra Rodio/CPAL/Symphonia, Lofty e persistência concreta;
- testar componentes Svelte com Vitest + Testing Library, consultando por role/nome como o usuário ou leitor de tela;
- evitar selecionar classes, estrutura de DOM ou `data-testid` quando existe semântica; testar funções puras sem render e limitar snapshots a estruturas pequenas e revisáveis.

### Convenções comuns

O [cliente de extensão opcional](../browser-extension.md#checks-locais) usa `node:test`/assert/VM com DOM, Chrome e relógio falsos, sem runtime npm no produto. Exercitar scripts de produção, validação/schema, escolha explícita, navegação/SPA, coalescência sem starvation e rejeição/timeout sem retry; teste estático de manifesto/CSP não substitui carregamento no Brave. O runner é separado do C++/Qt e não adiciona Node à compilação/execução da Lite. A biblioteca nativa opcional tem Qt Test para codec estrito, framing parcial/limites/deadline e sessão com admissão falsa: nenhuma URL é aberta. Vetores JSON compartilhados conferem o contrato JavaScript/C++; entradas byte-level cobrem tolerâncias do parser que reserialização esconderia.

A [coordenação de jobs](../download-jobs.md) acrescenta Catch2 puro para retenção atômica/limitada, deduplicação entre conexões, FIFO, progresso, cancelamento/timeout, resultados tardios/inválidos, importação idempotente e shutdown. Fakes de execução/importação e relógio explícito mantêm esses testes sem rede, mídia ou Qt. Qt Test verifica a sessão ligada ao serviço real e o model de apresentação: ações por ID, índices estáveis, reentrância de sinais e ausência de URL/query nos papéis públicos. A porta rápida Windows abaixo acrescenta execução concreta fora do player; o host/transporte opcional e a composição instrumentada abaixo não ativam execução remota no produto. Ainda faltam perfil exato revisado de distribuição, ferramenta corrigida/isolada, reconciliação física/recuperação acionável e composição protegida de produção/instalada. Cada fronteira exige testes próprios, sem fixtures de sites ou perfis pessoais.

- fixtures são compartilhadas por helper explícito e pelo mesmo manifesto versionado;
- nomes em inglês descrevem resultado, por exemplo `skip_forward_clamps_at_track_end`;
- usar Arrange–Act–Assert ou Given–When–Then, sem comentários redundantes;
- diferenças inevitáveis de harness ficam documentadas, mas resultado, tolerância e semântica esperados permanecem iguais.

### Conversão nativa Windows com Media Foundation

O [backend nativo opcional](../download-conversion.md#conversão-nativa-com-media-foundation) tem testes independentes do protocolo binário, main real, codec nativo e composição com supervisor/workspace. Fixtures autenticadas e cópias temporárias próprias cobrem MP3 CBR/VBR/Unicode, AAC/MP4 e WAV/resampling; a saída exige framing MP3 e decodificação integral até EOF em outro processo. Testar ausência de áudio, conteúdo disfarçado/truncado, limites, callback recusado, timeout/cancelamento com drenagem e ausência de fallback quando o worker falta. A recuperação deve conservar pins e a composição assíncrona deve publicar antes do handoff idempotente ao serviço. Regressões legadas e UI são evidências separadas; transferência helper e importador falso não provam mídia da internet ou jornada integral no app.

Fechamento de 2026-11-03: **11/11 CTest, 299 casos Qt**, sem skips; compatibilidade da recuperação na UI **1/1 CTest, 8 casos Qt**. Release opcional com `BUILD_TESTING=OFF` lista zero testes; desktop padrão continua sem backend remoto. Inspeção PE confere worker x64 com imports diretos OS-only, sem imports diretos de MF anteriores à main. Ambiente sem componentes/encoder MF, fixture DRM e clones de IStream intercalados não foram exercitados diretamente. Job/processo não é sandbox e os testes não aprovam produção remota ou distribuição.

### Host Native Messaging e transporte privado Windows

O [host opcional separado](../browser-extension.md#host-e-transporte-native-messaging-windows) exige Red→Green por fronteira, sem confundir seu alvo real com instalação. `RC_MUSIC_BUILD_NATIVE_MESSAGING` é OFF por padrão; a configuração confiável de produção é vazia, e o processo deve recusar antes de abrir I/O/conexão. Testar origem exata e endpoint limitado de build/composição, sem adotar argv, ambiente ou JSON como configuração. `--parent-window` aceita somente uint64 decimal canônico como metadata inerte: os testes não provam a representação signed intptr_t emitida pelo Chromium, HWND ou Brave reais. O componente Qt Network é separado da biblioteca Qt Core, não acrescenta engine à Lite nem muda `OpenSources` local.

Codec deve exercitar encoder tipado sem renormalizar URL/query assinada e decoder de resposta estrito/correlacionado: schema/chaves/version/type/status fechados, UUID esperado, erros tipados, count e IDs distintos limitados. Transportes usam `QLocalSocket` real e admissão falsa/controlada, com relógio injetado, para provar Pending sem wire, preâmbulo/frame fragmentados, limites antes da alocação, oito sessões no máximo, indisponibilidade, reentrada de clock/porta e shutdown sem declarar `stopped` antes de callbacks/poll desenrolarem. Uma conexão apenas, sem retry/autolaunch; falha pós-escrita/EOF/resposta inválida é incerta, nunca rejeição ou ACK inventado, e fechar observador não cancela o lote do dono.

A [reserva cooperativa Windows](../browser-extension.md#exclusividade-cooperativa-do-dono) exige teste público com dois servidores na mesma thread, sem depender de recursão de mutex: mesma chave/case e origem distinta recusam antes de admitir, endpoint diferente permanece independente. Observar falha de listen sem retenção e reserva ainda ativa depois de shutdown/stopped até destruição do servidor. Testes de baixo nível verificam handle não herdável, DACL protegida usuário+SYSTEM, objeto precriado/erro recusados sem espera e indisponibilidade enquanto um observador conserva o último handle; driver independente com filhos próprios exercita concorrência entre processos, CWD/perfil/edição e retomada após término normal ou morte física reconhecida. Nenhum cenário exige Registry, instalação, browser ou arquivos pessoais. Esses testes não comprovam outra sessão/usuário, primeiro pipe ou defesa contra processo hostil que ignore a reserva; estados/documentos do serviço permanecem outra fronteira.

O adapter Win32 usa pipes reais próprios e handles emprestados, sem console, arquivos pessoais ou rede. Provar snapshot limitado antes de alocar, ausência de tradução textual, bytes extras no antigo limite de chunk, escrita binária única, buffer do worker, prazo de 2 s, clock regressivo, pipe quebrado e cancelamento repetido até parada física sem fechar handles emprestados. O seam de entrada da main relê o clock **depois** do read; read que retorna na fronteira de 5 s deve falhar sem admissão. Watchdog independente de 15 s termina somente o próprio host em stall/teardown, não o app dono.

`NativeHostProcessTests` dirige a main real configurada com identidade sintética exclusiva do alvo de teste, o host desabilitado separado e helpers de watchdog. O peer falso é independente do servidor de produção: bytes exatos, stdin aberto durante Pending, stdout com um único frame estrito/zero stderr, flags/origem/spoof recusados sem IPC, framing/EOF excessivos e replies malformados/mismatch/double/oversized sem ACK/retry. Ausência de input expira pelo prazo total; finish/stall/late-teardown têm saídas distintas. Esse ensaio prova observador e I/O, não continuidade do owner real: essa responsabilidade pertence à composição abaixo. Watchdogs e limites de prazo são oráculos de protocolo/cleanup, não benchmarks.

A [composição instrumentada do ingresso](../process-tests.md#composição-instrumentada-do-ingresso-native-messaging) acrescenta servidor, host, controller/serviço, UI/importador e ambos stores/workers reais. Quatro hosts repetem o mesmo UUID duas vezes por boot, em dois boots; leitores independentes conferem IDs/recibos após cada resposta e checkpoint/sessão após NormalExit. Primeiro boot exige Saved antes da execução, importação sem load/autoplay e reprodução só por seleção explícita. Segundo boot reconhece Imported com restore=false, sem executar/hidratar/duplicar. Somente publicação MP3 é falsa; não há rede, conversão ou ownership físico nesse fluxo. Sentinel de saída pertence somente ao TEMP/hook instrumentado e surge depois dos dois ACKs, nunca é comando do wire. Saída normal exige Saved final e drenagem de servidor, controller e workers antes de destruir owners.

Evidência focada do incremento anterior de host/IPC em 2026-10-02, sem somar funções/linhas QtTest a registros CTest:

| Fronteira | Resultado QtTest observado |
|---|---:|
| Codec tipado/legado/respostas correlacionadas | 188/188 |
| Configuração confiável e launch | 54/54 |
| I/O Win32 | 13/13 |
| Entrada da main e clock pós-read | Red 3 passagens/1 falha; Green 4/4 |
| Cliente/servidor IPC | 27/27, incluindo Red 24 passagens/3 falhas das regressões de reentrada/drain |
| Processo real e watchdog | 32/32; CTest focado 1/1 em 9,01 s |
| Composição instrumentada anterior / com ingresso nativo | 3/3 cada |

A consolidação Release ON/build/`all_qmllint` passou **470/470 CTest em 138,98 s**; os resultados focados acima e o tempo da suíte não são benchmarks. Gates de grafo/default OFF e separação de identidade sintética precisam de execução própria, não aprovação transferida da suíte ON. Origem em argv e pipe do mesmo usuário não autenticam navegador; primeira instância/colisão do pipe Windows, identidade instalada, registro/upgrade, Brave real, egress, ferramentas/sandbox e distribuição permanecem pendentes. Não instalar/carregar extensão, alterar Registry/políticas, baixar ferramentas ou retomar interação nativa para executar esses checks.

### Importação local de resultados publicados

O [importador da apresentação](../download-jobs.md#importação-local-de-resultado-publicado) tem Qt Test com `DownloadService` e `PlayerPresenter` reais, mas executor, backend de áudio, metadata e discovery falsos. Paths são stand-ins lexicais, sem ler/decodificar mídia; isso prova o handoff append-only, não o conteúdo/ownership de um download real. Cobrir fila vazia sem seleção/autoplay, preservação confirmada de Playing/Paused/Ready/Loading/Error, load somente após escolha explícita, replay após remoção/limpeza, conflito e tetos inclusivos sem expulsão. Rejeição antes do commit deve devolver capacidade para retry; cancelamento tardio não importa.

O método privilegiado não pode ser invocável por QML/página. Recusar thread incorreta antes de tocar em QObject/recibos, presenter destruído/frozen/exiting e descoberta/diálogo ativos. Provar preparo/validação do model antes do callback de commit não lançável e antes das notificações, reentrância em `rowsAboutToBeInserted`/`rowsInserted`/`stateChanged`, e polling/eventos de EOF em loop aninhado: nenhuma mutação dentro da inserção; `Ended` da geração atual prevalece sobre snapshots até replay enfileirado após liberar a guarda. Callbacks de transação Qt não podem lançar nem destruir o model/owner; importador e adapters emprestados devem sobreviver à chamada. Não alegar lifetime geral por `QPointer`; os testes do modo volátil não comprovam deduplicação durável. A composição real executor→importador, journal, host/lifecycle/model/UI e engine da Completa mantêm seus gates.

O modo `Session` exige Red→Green independente: fila+recibo comprometidos antes dos sinais, `Importing` enquanto `begin`/`pollImport` aguardam Saved matching, confirmação de revisão antiga sem limpar snapshot mais novo, Failed/readback sem ACK, polling sem retry e retry explícito pelo mesmo ID/path sem novo append/download. Exercitar múltiplos importadores no mesmo owner, reserva recusada, tetos inclusivos de 128 recibos/1 MiB nativo sem expulsão, reentrada do writer/sinais, shutdown com reconhecimento pendente e backup/defaults inválidos/versão futura bloqueando novas importações. Recibos carregados continuam históricos depois de remoção/clear/arquivo ausente; testes não podem abrir/importar paths recuperados nem usá-los para cleanup.

Core Qt-free cobre o vetor/equality de `DownloadImportReceipt`, IDs ASCII únicos, paths MP3 absolutos/normalizados, NUL/scalars inválidos e aritmética de quota sem overflow. Qt codec prova encoder v2, migração v1 com ledger vazio, membro `downloadImports` obrigatório/fechado, duplicatas de chave inclusive escapes equivalentes, UTF-8/UTF-16 inválido, fronteiras de count/bytes e preservação de versões >2. Store real em TEMP próprio + novo owner devem observar fila e recibo no mesmo documento em Saved, BeforeCommit e Failed AfterCommit, sem readback-promovido ou mídia real. Bootstrap de processo instrumentado deve conservar ledger com `restore_last_session=false` e opcionais de downloads OFF, sem hidratação/backend load/autoplay; o teste não equivale a serviço de downloads instanciado. Registrar RED de comportamento separadamente de setup/build incorreto e centralizar contagens/evidências no [plano](../development-plan.md) e em [download-jobs](../download-jobs.md), não inferir exactly-once universal, hardware ou distribuição.

### Checkpoint privado de jobs

O [checkpoint privado de jobs](../download-jobs.md#checkpoint-privado-e-recuperação-conservadora) usa Catch2 puro para export/restore all-or-nothing, quotas inclusivas, bijeção/ordem dos recibos, namespace novo, replay/conflito e nenhuma execução/importação no restart. Qt Test cobre JSON estrito/UTF-8/duplicatas/profundidade, números decimais sem perda e vetores SHA independentes. Store Windows usa TEMP privado: arquivos reais, ACL/pins/lock, substituição/compartilhamento, falha antes/depois do commit e preservação de dados corrompidos/futuros/ilegíveis/hardlinks/reparse. Ensaio Service→identidade→store→novo Service compõe os componentes reais com execução/importação falsas. Não abrir candidatos de mídia, perfis/config reais ou rede. Documento recuperável sozinho não comprova ACK durável, transação de fila+recibo, propriedade de mídia/residuais, perda de energia ou UI instalada.

O restore estrito com recibos de sessão exige Red→Green para o crash após Session Saved com checkpoint ainda Queued/parcial/sem path: ID exato deve reconhecer Imported sem inventar métricas e sem chamar begin/accept/pollImport/executor/FS. Testar candidato diferente e Failed/Cancelled/Cancelling com recibo como `ReceiptConflict` all-or-nothing, estados de conclusão sem recibo como Unconfirmed/Interrupted, órfãos sem jobs/fingerprint/quotas e namespace contra todo o ledger inclusive com checkpoint Missing. Relatório bounded não expõe URL/path e legado sem reconciliação mantém testes próprios. Controller prova cópia detached de Loaded primário/Missing exatamente vazio, recusa de DTO/backup/default/futuro e retry de downloads que não fabrica saúde nova. Codec/model cobrem Imported histórico com contadores/total antigos, “Já importado na fila”, percentual final 1 sem afirmar bytes e zero path/ações públicas. Não confundir esses fakes/policies com ownership físico, resolução acionável de conflitos ou serviços/QML já compostos no app; evidências ficam no [plano](../development-plan.md) e em [download-jobs](../download-jobs.md).

A [admissão durável assíncrona](../download-jobs.md#admissão-durável-assíncrona) exige Red→Green em estágio/prospectiva/controller/worker/sessão. Catch2 observa quotas de representação com candidatos recuperados, nenhum start/ACK antes de Saved matching, replay/conflito, Load desconhecido sem rewrite, enqueue/IO falhos, retry explícito sem novos IDs, cancel/timeout do ativo durante escrita e shutdown reentrante. Gates de worker provam que factory/store/destruição ocorrem fora do caller e que completion não observada permanece Busy; são relógios/watchdogs de falha, não benchmarks. Testar fechamento antes do I/O e drain ativo/recursos antes de stopped, sem join no protocolo da UI. Qt Test mantém legado/v1 e exige espera limitada por UUID, EOF/clock/entrada extra sem cancel/retry do dono; compõe controller/serviço reais e porta IO falsa. O ensaio Windows adicional usa controller→worker→store reais em TEMP privado, barreira BeforeCommit e Failed AfterCommit sem readback-promovido, final save e novo startup sem execução/importação. Parada física não comprova save bem-sucedido; assertar `saveStatus` separadamente. A composição instrumentada de app/host/UI não fecha promoção protegida de produção, autosave periódico ou reconciliação acionável com journal de mídia.

### UI opcional e saída com downloads

O [binding/painel comuns](../download-jobs.md#painel-compartilhado-e-preparação-de-saída) exigem TDD independente. Controller Qt-free cobre opt-in Hold, save falho/Busy/exception/malformed/IDs tardios, operação incerta limitada, retry explícito sem ACK de admissão falha e close com Failed preservado. Presenter Qt Test cobre congelamento antes do begin, source-drain separado, Pending/Ready/Failed, timeout, enum inválido, callbacks reentrantes/thread/lifetime e aprovação final revogada. Binding compõe controller/serviço/model reais com execução/IO falsos, não cria executor de produto e distingue save de parada física de todos os participantes.

Quick usa Main/painel reais com proxy/ListModel somente no harness: `downloads:null` não altera o player, capacidades/IDs são estáveis, strings são PlainText, dados privados não aparecem, confirmação é separada/revogável, recusa não sai, foco/Tab/Escape e atalhos respeitam o modal e 720×520 funciona nos temas. Fatal warnings continuam ligados; um warning de foco não pode ser ocultado. O [processo instrumentado](../process-tests.md#composição-instrumentada-dos-downloads) compõe ambos stores/workers reais em TEMP próprio e publicação sintética de MP3 autenticado, observa handoff sem autoplay/reprodução após ação/restart sem duplicação/exit Saved+drain e leitores novos após NormalExit. Não equivale a transporte/conversão/sandbox real, Brave instalado ou transação global entre documentos. Produção OFF/default continua sem engine, test hooks ou executor sintético; validar grafo, zero testes e recusa antecipada do hook separadamente.

### Revisão explícita de arquivos interrompidos

A porta/binding/model exigem Red→Green para pedido explícito somente de Interrupted conhecido com controller Ready, checking/resultado/recheck correlacionados e ausência de paths/ações executáveis. Testar zero revisão automática, Busy enquanto houver operação/completion, IDs incorretos/obsoletos, falhas/exceções/enums inválidos e reentrância sem consumir resultado de outro pedido. Fake com barreiras deve provar start/poll/shutdown prontos e saída Pending até stopped físico, preservando owners; join no destructor não é oráculo de protocolo rápido.

O fake precisa respeitar backpressure real: se start admite e lança, ou poll lança antes de consumir a completion, o slot antigo permanece ocupado. Binding deve fechar somente revisão, conservar correlação/drain até stopped e recusar recheck/sucesso tardio, sem fechar os demais serviços. Separadamente, completion normal `Unreadable` libera slot e permite nova ação explícita. Fake que aceita sempre um operation ID novo não prova esse contrato.

O worker Windows exige testes de factory/work/destruição na thread privada e um ensaio com store/workspace/assessment reais em TEMP próprio. Classificar ID exato com journal Missing/Invalid/Future, registro ausente, candidatos físicos e conflito/Unreadable; preservar bytes/documentos, sem import/delete/download ou reparo de permissões. Oráculos de Quick devem usar também painel real ligado ao binding/model/controller/service reais, não somente um proxy de ListModel: escolha explícita, checking, resultado/recheck e saída drenada, sem autoplay ou mudança de fila. Gates e watchdogs são limites de falha, não benchmarks. Esses ensaios não fecham promoção de produção, sandbox, mídia da internet ou resolução de candidatos.

### Descarte confirmado somente de temporários

Red→Green separado cobre opt-in ausente/inativo, request sem I/O, consentimento exato job/token one-shot, cancelamento/Escape/replay, troca de intenção e elegibilidade revalidada na confirmação. OwnedPartial mais recente é necessário e consumido antes da admissão; nova tentativa exige revisão/consentimento novos. Testar signals/port reentrantes, fechamento durante confirmação/start/poll, slot compartilhado com revisão/completion não observada e correlação op/job/action; ReviewFiles não pode confirmar outcome de descarte. Model só projeta Interrupted, nunca expõe paths e deixa Imported inerte. Nenhuma ação muda checkpoint/job/recibos/quota, fila, importador ou playback; exceção incerta mantém slot para drain, desativa só a porta e projeta DiscardUnconfirmed sem alegar preservação dos parciais.

Primitive Windows e worker real usam somente TEMP próprio e bytes sintéticos: fresh IDs/canonical/DACL/tipo/link único/reparse/filhos conhecidos/budget/hash, Ready parcial/vazio e Prepared. Conferir final candidato byte a byte sem mutação, colisão/source+final, unknown child, substituição/sharing denial, hardlinks e reparse. Sucesso exige fechar DELETE handles, provar ausência física e Saved de cleanup_complete; testar remoção parcial/delete-pending como Incomplete, save Before/AfterCommit ou throw como Unconfirmed sem readback-promovido, e Missing anterior sem falso Discarded. Não reparar ACL/atributos, limpar recursivamente ou encerrar processos externos.

O painel QML real composto deve provar diálogo/cancelamento/confirmar por teclado, resultado correlacionado, arquivo temporário removido/final preservado, documentos e histórico não relacionados intactos e saída Pending até worker/stores drenados. Factory/I/O/save/destruição pertencem à thread privada; shutdown não bloqueia o caller. Esses checks não aprovam mídia da internet, importação de candidatos, limpeza de runtime-residuais, tratamento de capacidade, sandbox, distribuição ou workflow instalado. Produção remota permanece OFF; Lite/Completa compartilham o painel sem engine na Lite.

### Execução rápida Windows e reconhecimento de parada

O [journal de ownership](../download-ownership.md) exige Red→Green para codec/transições, upsert sob lock, workspace e sessão: Saved antes de sinks/rename/path público, Failed pósrename preservando final, cancel/reentrada e ACK nativo antes de cleanup. Recovery só classifica TEMP próprio: IDs/DACL/bytes trocados, hardlinks/reparse/unknown children, ausência versus sharing denial e quota agregada. Ensaio store+workspace+novo leitor+assessment reais usa bytes sintéticos/validator de teste sem afirmar mídia válida; sessão usa MP3/MP4 autenticados e transferência falsa. Injetar Before/AfterCommit, registrar symlink skips/setup RED incorreto, preservar testes anteriores do checkpoint e ambiguidade de Failed sem readback-promovido. Não cobre exactly-once/fila+recibo, runtime-residuais, host/app/UI ou sandbox.

A porta opcional [WindowsDownloadExecution](../../src/adapters/WindowsDownloadExecution.hpp) implementa `DownloadExecution` com um pedido pendente, snapshot coalescido e no máximo um wake enfileirado. Factory, `WindowsDownloadTask`, I/O e destruição ficam no event loop Qt de uma thread privada; `start`/`cancel`/`poll`/`shutdown` apenas admitem comandos ou leem o estado limitado. Sua factory é de composição confiável e só cria objetos inertes: falha/null/exception não podem deixar processos, arquivos ou trabalho fora da tarefa retornada. Isso não autentica imports pré-main nem transforma o seam em entrada de página/extensão.

Testes de contrato devem observar:

- retorno do caller enquanto factory/start/poll/cancel/destruição estão bloqueados por gates controlados, sem sleeps ou benchmark como prova; métodos lentos e destruição permanecem fora da thread do caller;
- admissão e memória limitadas, progresso coalescido sem regressão, IDs/DTOs inválidos, terminal estável e recusa de novo start até `poll` observar o terminal do ID correspondente; polling antigo não libera a admissão;
- `WindowsDownloadTask::start` falso ou com exception, poll/cancel com exception e resultados inválidos sem ACK prematuro: cancelar e drenar até terminal válido. Start falso sem malfunction conserva somente o erro tipado de `Failed` do ID correspondente; terminal sem essa falha explicativa gera `InvalidResult` após drenagem. Exceptions/DTOs inválidos mantêm `InvalidResult` como causa técnica mesmo se depois houver outro erro, publicado somente após ACK válido. Não confundir recusa da tarefa com recusa da porta rápida;
- shutdown pronto para o caller, com admissão fechada e ownership preservado enquanto há trabalho; `stopped` somente após teardown da thread, inclusive destruição da tarefa. O owner deve observar esse estado antes de destruir a porta na UI; join do destrutor é fallback fora da UI, não confirmação rápida de parada;
- publicação concorrente com cancelamento e falha de cleanup após rename: final retido é evidência privada limitada, nunca autoridade de importação ou journal durável; serviço cancelado não importa e o sucessor aguarda drenagem reconhecida.

A composição de ensaio MP3/MP4→MP3→`DownloadService` deve passar pela porta rápida e pelos adapters de produção de workspace, supervisor/processos, conversor, inspeção/decodificação integral e publicação sem overwrite. Apenas DNS/transferência do helper e importador são falsos; fixtures são sintéticas/autenticadas e caminhos temporários pertencem ao teste. Exercitar sucesso, cancelamento, falha tipada, não importação de parcial e teardown; não ligar essa composição ao player nem alegar importação local real. Perfil revisado do runtime pré-launch, CLI corrigido/proveniência/licenças, sandbox, journal/recuperação e host/UI continuam gates próprios.

### Runtime autenticado pré-launch

`WindowsDownloadNetworkLaunchTests` separa copiar/pinar bytes sintéticos da jornada de nove PEs reais → bootstrap/payload → DNS literal/HTTP loopback → ACK nativo. Fixture TLS/CRT opt-in ausente gera skips explícitos, não prova funcional. Usar fontes SDK/CRT apenas para leitura; jamais junction de teste para SDK/arquivos pessoais ou cleanup recursivo de aliases. Testar SHA errado em todos os papéis antes de criar pasta, ambiente mínimo/overrides ignorados, headroom de path, write/rename/delete recusados enquanto há lease inclusive após cancel pedido, payload malformado sem frame/stderr e configs/`.local`/manifest externos recusados antes do start. Extras desconhecidos sobrevivem ao cleanup. Gate PE/imports revisa separadamente o perfil conhecido; SHA verde não significa revisão de PE, sandbox, todos os imports dinâmicos ou distribuição aprovada.

`WindowsPreparedDownloadTaskTests` observa preparo real de bytes sintéticos após `start`, nenhum arquivo na construção/default factory e liberação da lease após destruição. Gate de preparer confiável comprova caller livre durante preparo, cancel/shutdown sem sessão posterior, terminal depois de cleanup e teardown reconhecido. Deadline usa clock original, incluindo preparo; clocks regressivos/negativos, limite inclusivo e one-shot são determinísticos. Os bytes sintéticos não são PEs nem podem ser executados. `WindowsPreparedDownloadExecutionTests` usa nove fontes reais somente para leitura e falha antes de DNS por pins/configuração; preserva sentinelas e reautentica cada job após mutar apenas bootstrap copiado para TEMP próprio. Não confundir esses testes com jornada inteira de rede/conversão; job inválido e pasta limpa não provam sozinhos que houve preparo. Fixtures TLS/CRT ausentes geram skips explícitos.

### E2E e plataforma

O [transporte opcional](../download-network.md) separa testes de policy/HTTP/session com I/O falso, worker/proxy reais com DNS literal/HTTP loopback e testes de socket TLS com certificado sintético não secreto e confiança por instância. Exercitar bytes assinados, DNS misto/redirects, fronteiras/quota, EOF/TLS, corpo segmentado, cancelamento reentrante e backpressure, sem benchmarks. Não abrir exceção localhost na policy de produção. Sucesso DNS/EOF e parada exigem saída nativa e drenagem dos pipes; testes separados não provam a jornada completa DNS/TLS→conversão→importação, launcher, instalação, sandbox ou mídia da internet.

- poucos cenários centrados em requisitos P0;
- fixture e estado de configuração isolados por execução;
- nunca usar arquivos da biblioteca pessoal do usuário;
- teste pode usar backend falso para fluxo visual e backend real para um conjunto menor;
- teste que requer sessão desktop, leitor de tela ou áudio real é rotulado e documentado.

## 5. Fakes e seams obrigatórios

| Fake | Controla |
|---|---|
| `FakePlaybackBackend` | ready, duração, posição, ended, device lost, erro e confirmação de seek |
| `FakeMetadataReader` | resultado imediato, atraso, cancelamento, tag hostil e arte acima dos limites NFR-LIMIT |
| `InMemorySessionStore` | load/save, versão, corrupção e falhas programáveis |
| `FakePlatformIntegration` | tray presente/ausente, close, visibilidade e abertura tipada da página de apps padrão com suporte, disponibilidade, resultado e conclusão controláveis |
| `FakeSystemMediaControls` | disponibilidade, snapshots publicados e comandos Play/Pause/Previous/Next/Seek |
| `FakeSystemPowerEvents` | disponibilidade, handler substituível e transições síncronas `Suspending`/`Resumed` controláveis |
| `FakeMediaSourceResolver` | local válido, não suportado e resultado remoto futuro |
| `ManualClock` | ticks, debounce, timeout, interpolação e autosave |
| `DeterministicIdGenerator` | IDs estáveis sem acoplar testes a UUID aleatório |

Fakes pertencem ao código de teste ou módulo de suporte, não viram abstração pública genérica. Mock framework só entra se reduzir código sem esconder expectativas.

## 6. Exemplo de TDD: salto para frente

Requisito: FR-013/FR-014.

### Red

Escrever primeiro casos como:

```text
given position 58s, duration 60s and forward skip 5s
when SkipForward is handled
then the playback backend receives SeekTo(60s)
and playback state remains paused if it was paused
```

Confirmar que falha porque o caso de uso ainda não existe ou não limita a duração.

### Green

Implementar `SkipBy` usando valores de preferência e a operação comum `SeekTo(clamp(...))`, sem criar regra na UI.

### Refactor

Unificar avanço, retrocesso, botão e teclado na mesma função; executar casos:

- limite no início;
- limite no fim;
- duração desconhecida/não seekable;
- valor de preferência mínimo/máximo;
- estado pausado e tocando;
- erro do backend sem posição falsa.

### Regressões do transporte local em erro e seek recusado

Para preservar `FR-011`/`FR-014`/`UX-008`, a cobertura combina presenter com backend falso e Quick: anterior/próxima habilitados por capacidade mesmo em `Error`, botões/atalhos equivalentes, foco reparado sem habilitar indevidamente os outros controles; commit/backward/forward recusados por `NotSeekable`, `Busy` ou `NotReady`, com preview reconciliado à posição confirmada e feedback acessível. Cobrir também arraste visual, estado disabled efetivo dos filhos e reentrada/destruição durante sinais, sem rejeição antiga vazando para uma seleção nova. A perda de capacidade deve ser observada pelo presenter, sem assumir que `item.enabled` já atualizou; navegação ainda disponível conserva foco, e `seekPending` transitório não provoca deslocamento.

Os Reds, Greens focados, a primeira consolidação que expôs a regressão de foco e o CTest final 273/273 ficam no [registro central de recuperação do transporte](../../experiments/m0/candidates/qt/README.md#transport-recovery), junto de Release, `all_qmllint` e composição de produção revalidados após o último QML. Os testes existentes foram preservados; a expectativa nova incorreta de ausência de autoplay foi alinhada ao contrato de retry explícito, sem ajuste de produção. Core Qt OFF não foi repetido neste recorte sem mudança de núcleo. São regressões de contratos existentes, não novo escopo; offscreen/software não comprova pacote, integração física nativa ou NVDA. Nenhum benchmark pertence ao recorte.

### Aviso de faixa pulada automaticamente

Para `UX-005`/`FR-011`, testar falha de origem seguida de próxima carga aceita, ordem do sinal específico antes do notify geral, motivo tipado e ausência de path bruto/mensagem livre. Quick deve conferir a fala da faixa pulada, supressão do Loading genérico, anúncio posterior da próxima faixa em Ready e deduplicação de eventos da mesma geração, sem fundir ocorrências de IDs distintos com o mesmo título. Durante importação, preservar um aviso agrupado sem dados de faixa e entregá-lo após o resumo, inclusive para falhas de ocorrências que não pertencem ao lote. Rejeição da próxima carga e conclusão obsoleta não devem anunciar avanço aceito.

Cobrir também reentrada que troca seleção/lote durante sinais sem perder nem duplicar `sourceImportChanged`, e títulos com `%2` literal sem substituição recursiva. O [registro central](../../experiments/m0/candidates/qt/README.md#automatic-skip-announcements) distingue Reds/Greens de Presenter/Quick, correção do contrato de texto e suíte global não verde (271/273) com falha de IPC ainda sem causa demonstrada ou correção. A revalidação focada passou CTest 2/2, com contrato UI 22/22 e Quick 153/153 sem falhas/skips; isso não substitui aprovação global nem autoriza relaxar ou repetir o teste de IPC até passar. Não confundir inspeção de sinais/texto QML com audição em NVDA, nem incluir o logger pendente de `NFR-PRIV-003` nesta correção.

## 7. Testes do domínio

### Playback state

- todas as transições válidas e inválidas;
- play sem faixa não altera estado;
- pause/seek preservam invariantes;
- evento atrasado de faixa anterior é ignorado;
- ended avança para próximo reproduzível ou termina fila;
- device lost não produz estado impossível.

### Fila

- IDs únicos para paths duplicados;
- add, play-now, move, remove e clear;
- remoção/movimentação antes e depois do atual;
- anterior com limiar de 3 s;
- itens indisponíveis são pulados e permanecem diagnosticáveis;
- serialização preserva ordem e identidade necessária.

### Tempo e preferências

- posição/duração nunca negativas;
- clamp sob valores extremos;
- saltos somente em 1–300 s;
- volume em 0–1 e desmutar restaura último valor;
- migrações e defaults;
- sessão restaurada sempre pausada.

Testes baseados em propriedades são recomendados para sequências aleatórias de fila, clamp e serialização. Eles complementam exemplos, não os substituem.

## 8. Testes de contrato

Uma suite reutilizável deve receber factories/adapters e validar semântica consistente. Os harnesses C++ e Rust implementam a mesma lista versionada de casos, com os mesmos IDs, fixtures e resultados; cada candidato executa o contrato contra seu fake e seu adapter real. A paridade é semântica, não compartilhamento artificial de binários entre linguagens.

### `PlaybackBackend`

- load válido publica ready e capabilities;
- play/pause são idempotentes;
- seek confirma destino ou erro, sem snapshot falso;
- fim da mídia é emitido uma vez;
- unload/shutdown são idempotentes;
- erro não bloqueia próximo load;
- callbacks/eventos mantêm ordem documentada;
- chamadas após shutdown falham de forma tipada.

Em C++/Qt, `FakePlaybackBackend` e o adapter Qt Multimedia/FFmpeg precisam satisfazer o contrato. Em Rust/Tauri, o fake e o adapter Rodio/CPAL/Symphonia precisam satisfazer os mesmos casos. Precisão, latência e formatos pertencem à suite de integração do corpus e não podem ser inferidos dos testes com fake.

### `SessionStore`

O [consentimento de substituição](../../experiments/m0/candidates/qt/README.md#replacement-consent) também exige testes de boundary, sem disco pessoal: completion falsa muda aviso de uma fonte para ambas enquanto a confirmação está aberta; revisão antiga não altera dirty/revisão dos autosaves nem inicia writes. Cobrir reaparição do mesmo aviso, revisão vazia/desconhecida, reentrada com token antigo/atual e escopo ampliado durante agendamento. Quick deve fechar/inutilizar confirmação cancelada ou desatualizada, emitir somente o token capturado uma vez e proteger o foco de callbacks atrasados. Esses testes não provam integridade física ou atomicidade conjunta dos arquivos.

- round-trip;
- IDs e posição acima de `2^53` sem perda, com strings decimais não canônicas e overflow rejeitados;
- documento abaixo/no/acima do limite próprio de 16 MiB;
- escrita atômica;
- defaults quando ausente;
- migração de cada versão conhecida;
- arquivo truncado/corrompido;
- falha antes/depois do replace;
- backup e retenção limitada;
- paths Unicode.

### `SessionWriter` e `SessionAutosave`

O quarto recorte de M4.1 é testado no core C++ puro com store controlado e `ManualClock`, sem Qt, filesystem real, sleeps ou relógio de parede:

- provar que um único worker executa `save()` e a reconciliação fora do caller, serializa requests e devolve `Busy` enquanto há trabalho ativo;
- tratar a revisão como identidade do par snapshot + autorização de substituição, incluindo forwarding e retenção da autorização até `Saved`;
- após `Failed` retornado ou exceção de `save()`, cobrir readback exato de primário/backup (`true`), divergência ou status não recuperável (`false`) e exceção do próprio `load()` (sem valor), sempre preservando `Failed`; `Saved` e bloqueio futuro não chamam `load()`;
- cobrir flush atrás de revisão ativa, retry da mesma revisão falha, reutilização apenas de sucesso e consumo causal da completion devolvida, que não pode reaparecer em `poll()`;
- coordenar dois callers concorrentes de `shutdown()` contra uma gravação aceita e provar uma única drenagem/completion, join idempotente e rejeição tipada de trabalho posterior. O teste usa barreiras/CVs controladas; não alega deadline, pois o join real permanece ilimitado se o store não retornar;
- verificar o estado imediatamente anterior e o disparo exato do debounce de 500 ms e do teto de 5 s, sem deixar uma revisão aceita herdar o deadline do burst anterior;
- verificar que a posição confirmada altera somente o escalar preservando a fila, que o primeiro tick fixa o checkpoint de 10 s e que ticks contínuos não o postergam;
- exercitar deadlines estrutural, posicional e imediato de forma independente, sempre disparando pelo mais próximo, além de `checkpointConfirmedPosition()`, repair, save-now e flush;
- rejeitar posição negativa ou positiva sem ocorrência atual e nunca usar preview/interpolação como posição confirmada; esse recorte puro não tenta comprovar os gatilhos da composição;
- após falha ou versão futura, provar suspensão do rearm automático mesmo com novas mudanças, dirty preservado, completion antiga incapaz de limpar a revisão atual e retomada somente por retry/substituição/save-now/flush explícitos.

O quinto recorte acrescenta testes de bootstrap, presenter, adapter e processo para a integração de sessão. Eles cobrem leitura de `session.json` apenas com `restore_last_session=true`, hidratação antes do QML e restauração da ocorrência selecionada sempre pausada; posição vinculada à geração, tolerância de confirmação de 50 ms, espera por duração e proteção de EOF contra autoplay; autosave estrutural e posição confirmada em seek, pausa, evento terminal e saída; retry, substituição de versão futura, timeout de saída e reparo ao alternar `false` → `true`. O restart do executável Release instrumentado usa raiz e path Unicode e verifica pausa em 750 ms; outro processo prova que restauração desligada ignora a sessão sem chamar o store.

O fechamento passou 214/214 no CTest Release combinado e 172/172 no core Qt OFF. `all_qmllint` e os checks de formato de 109 C++/46 QML ficaram verdes. A composição `BUILD_TESTING=OFF` compilou, listou zero testes e rejeitou o hook reservado com exit 1. Essas provas ainda não cobrem encerramento forçado/crash, corrupção/reparo/replace falho no fluxo completo, lock multiprocesso, desktop nativo ou pacote; as jornadas empacotadas correspondentes e M4.1 permanecem abertas.

O sexto recorte amplia os alvos de processo e store sem usar sleeps nem arquivos pessoais. `m0.qt.session.restart` usa uma WAV pequena do corpus copiada para raízes temporárias Unicode e cobre quatro cenários do executável Release instrumentado: restart válido pausado; `QProcess::kill()` em `BeforePrimaryCommit`, depois de verificar primário, backup e PID proprietário do lock, seguido por save que reaproveita o lock obsoleto e relaunch pausado; primário corrompido recuperado do backup, falha real de replace por sharing Windows e retry pelo controle QML; e primário/backup inválidos usando defaults seguros com aviso dispensável e bytes preservados. Watchdogs usam `QDeadlineTimer`, e guards encerram somente os processos filhos ainda vivos.

`m0.qt.session.windows-store` executa um helper em processo separado em duas linhas parametrizadas, `BeforeBackupCommit` e `AfterPrimaryCommit`. O pai confirma por `QLockFile::getLockInfo()` que o PID filho possui a transação, captura primário/backup, exige que o concorrente retorne `Failed` sem mutação, libera o holder por protocolo de pipes e só então aceita uma nova gravação; ao final, o lock não existe. Os alvos focados exercitam essas provas locais, mas não equivalem a perda física de energia, desktop nativo ou execução empacotada. As jornadas empacotadas/nativas correspondentes e `CHK-E2E` permanecem abertas até essa evidência, assim como o fechamento formal de M4.1.

O fechamento automatizado final do sexto recorte passou 214/214 no CTest Release combinado. O core foi configurado e compilado de forma fresca, sem Qt, com MSVC 19.44/toolset 14.44, e passou 172/172; o alvo Quick final passou 1/1, executando 99 casos. `all_qmllint` ficou verde. O formato cobriu 109 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não fornece `--check`, a validação dos QML comparou a saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado de crash com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Essas provas continuam locais/instrumentadas: pacote/nativo, `CHK-E2E`, M4.1 e os demais gates contratuais permanecem abertos.

### M4.2 — geometria da janela

O incremento funcional de M4.2 separa as provas por fronteira e mantém o comportamento determinístico:

- testes puros da policy cobrem mínimo `720 × 520`, coordenadas negativas, contenção integral em uma única `availableGeometry` em DIPs, preservação do índice da tela, monitor removido, área reduzida por mudança de DPI, lacunas entre monitores, dados inválidos e overflow;
- testes do codec e do store cobrem `Preferences` com `std::optional<WindowPlacement>`, emissão do schema v2 com objeto ou `null`, leitura v1 válida sem promoção/regravação, rejeição estrutural do v2 e proteção de v3 ou superior antes dos campos conhecidos;
- testes de autosave e writer provam que uma mudança somente de placement cria nova revisão, que a revisão mais nova vence uma conclusão antiga e que mudanças posteriores de outras preferências preservam o placement;
- testes do adapter Qt cobrem captura apenas em estado normal e visível, coalescência das mudanças, descarte conservador do candidato ambíguo quando minimizar/maximizar/tela cheia vence o timer, recaptura ao voltar a normal, flush antes de ocultar/minimizar/sair e proteção contra reentrância ou rebind durante esse flush;
- o teste Quick mantém o root QML oculto até o sinal de conclusão do startup; o bootstrap sequencia `setScreen()` e depois `setGeometry()` antes desse sinal, e o restart verifica o resultado já restaurado após a revelação no harness; e
- um teste de restart instrumentado usa dois processos, persistindo a geometria no primeiro e verificando a restauração no segundo antes da exposição da janela.

Essas provas não substituem execução empacotada ou desktop nativo. `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` e validação física de DPI/múltiplos monitores continuam abertos; M4.2 não é considerada globalmente concluída por este incremento.

O fechamento automatizado passou 225/225 no CTest Release combinado, 183/183 num core fresco sem Qt com MSVC 19.44/toolset 14.44 e 1/1 no alvo Quick com 100 casos. `all_qmllint` ficou verde. O formato cobriu 112 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não fornece `--check`, a validação QML comparou sua saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado de restart de preferências com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

### M4.3 — instância única, shell e IPC limitado

O [feedback de caminhos inválidos antes do IPC](../../experiments/m0/candidates/qt/README.md#invalid-shell-source-feedback) usa o smoke instrumentado existente com fontes vazias, recurso Qt, drive-relative, dispositivo Windows e lote válido seguido de inválido. Exige exit 2, somente o marker fechado `invalidPaths`, stderr vazio e raiz de configuração ainda inexistente. O catálogo verifica a orientação acionável, tradução e fallback sem interpolação externa. A composição `BUILD_TESTING=OFF` deve continuar recusando os hooks reservados silenciosamente em exit 1, antes de qualquer configuração. Essa prova valida o encaminhamento ao reporter compartilhado, não a exibição do `QMessageBox` nativo, suprimida no harness.

O [caso de resposta IPC retida após falha da espera de escrita](../../experiments/m0/candidates/qt/README.md#ipc-buffered-reply) usa socket falso e a mesma transação conectada de produção. ACK/NACK completos são distinguidos de resposta ausente, parcial, malformada, excessiva ou com bytes extras; escrita parcial/rejeitada, bytes ainda pendentes e deadline não podem virar sucesso. Cada linha verifica uma única escrita e o frame exato. Preservar também o teste multiprocesso e a regressão antiga de ACK perdido, sem afrouxar assertions ou repetir até passar. O registro central separa Red determinístico e Green proporcional de qualquer alegação global/histórica.

As provas do recorte funcional local de M4.3 permanecem separadas por fronteira:

- parser e resolver exercitam argumentos nativos antes/depois de `--`, CWD diferente, Unicode, espaços, aspas/metacaracteres, paths relativos, rooted-only, drive/UNC/extended canônico plenamente qualificados, recurso Qt, namespaces Win32 de dispositivo, prefixos pseudo-extended e formas ambíguas;
- codec cobre schema fechado, comando e versão tipados, 0/1/1.000/1.001 paths e JSON abaixo/no/acima de 1 MiB, preservando spelling e rejeitando o lote inteiro sem concatenação shell;
- coordinator cobre eleição simultânea, takeover de lock obsoleto, ACK/NACK, fila e bytes de bootstrap, timeout de frame, cold start além do prazo antigo, permissão de foreground best-effort, pausa/retomada/encerramento e teto de conexões parciais com recuperação;
- dispatcher/presenter cobrem FIFO, backpressure durante importação, ativação e a garantia de drenar todo comando ACKado antes de persistir/sair; deadline cancela a intenção sem descartar trabalho;
- um teste de processo executa o shell Release instrumentado com raiz/CWD Unicode isolados, confirma um único backend, rejeições limitadas sem derrubar a principal, encaminhamento por processos secundários e sessão final com os paths absolutos esperados.

Essa evidência não substitui o pacote instalado nem o Explorer real. Associação/double-click, quoting produzido pelo shell, foreground efetivo, ACL entre usuários e desktop nativo continuam em `CHK-WIN`/`CHK-E2E`; fuzz/property do framing/schema e a revisão global de advisories/licenças mantêm `CHK-SEC` aberto. Nenhum benchmark faz parte deste incremento.

O Red final falhou pelas APIs de drenagem ainda ausentes no dispatcher/presenter. O Green focado de dispatcher, presenter e processo passou 3/3; a suíte Release combinada passou 229/229. No teste de processo Release instrumentado, A permanece ativo enquanto B, em path Windows comum acima de 260 caracteres, recebe ACK; a terceira secundária recebe NACK durante a saída, a barreira libera a drenagem e A+B aparecem no `session.json`, com um único backend. As fronteiras exatas de 0/1/1.000/1.001 paths e abaixo/no/acima de 1 MiB continuam em testes de camada, não são extrapoladas do executável. `m0_qt_shell_ui_qmllint` e o formato de 122 C++/46 QML passaram; o QML foi comparado à saída normalizada porque `qmlformat` 6.11.2 não oferece `--check`. `BUILD_TESTING=OFF` foi recompilado, listou zero testes, rejeitou o hook reservado com exit 1 sob o runtime autenticado e preservou `longPathAware=true`. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas.

### M4.4 — start behavior, tray e saída ordenada

As provas do recorte funcional local de M4.4 observam contratos em camadas:

- a policy pura de disposição inicial cobre preferência desligada, opção ligada com/sem tray e precedência de fonte explícita;
- o adapter Qt prova que invisibilidade do bootstrap não arma recuperação, enquanto `hideWindow()` arma `hidden_to_tray`; show/rebind limpam a intenção e a perda de tray durante ocultação explícita restaura a janela visível/minimizada;
- o Quick verifica que o toggle pode desligar `startMinimized` depois da perda da capacidade, que ativá-lo exige tray funcional e que a nota de sessão é condicional à opção de restauração;
- controller/presenter registram ordem e cardinalidade: nenhum shutdown ocorre durante drenagem ou persistência; após os dois saves, `PlaybackBackend::shutdown()` ocorre uma vez antes de `PlatformIntegration::requestExit()`; repetição é inerte; falha ou timeout não desliga o backend;
- o teste de processo Release instrumentado aplica a preferência depois do placement, cobre janela visível com a opção desligada, startup oculto com tray, ações reais mostrar/ocultar/mostrar, fallback visível com aviso sem tray, precedência de fonte inicial e restauração de uma principal oculta por IPC.

Os Reds falharam pelas expectativas novas de disposição, ownership da ocultação, toggle, precedência de fonte e shutdown ordenado; os Greens focados passaram antes dos gates amplos. A consolidação Release passou 229/229 e o core Qt OFF 183/183. `m0_qt_shell_ui_qmllint` ficou verde; o formato cobriu 122 C++/46 QML, comparando para QML a saída padrão normalizada porque `qmlformat` 6.11.2 não oferece `--check`. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou os dois hooks M4.4 com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

Essas provas não substituem pacote/desktop e tray nativos, primeiro frame perceptível sem flash, fallback Linux real, restart do Explorer ou NVDA/acessibilidade nativa. `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y`, `CHK-CLOSE` e M4.4 permanecem abertos globalmente.

### M4.5 — SMTC Windows e transporte assíncrono

As provas do recorte funcional local de M4.5 também permanecem separadas por fronteira:

- controller e presenter cobrem Play/Pause idempotentes, inversão da intenção antes do ACK, compensação após confirmação obsoleta, `Ready` antigo incapaz de apagar a intenção nova, limpeza em geração/terminal/EOF e navegação segundo o estado efetivo mais recente;
- `SystemMediaControlsBinding` cobre projeção de status, metadata, capacidades, posição/duração, deduplicação, seek desabilitado durante preview/ACK, despacho distinto pelos casos de uso comuns e publicação `Closed` com comandos tardios inertes durante saída/teardown;
- `WindowsSmtcAdapter` com boundary falsa cobre cache antes do bind, rebind e republicação, falha fechada, marshalling de cada comando para a thread da UI, destruição/shutdown idempotentes, descarte de callbacks de geração antiga e destruição/recriação da `QPlatformSurface`, inclusive cancelando rebind enfileirado quando a surface já desapareceu;
- o smoke nativo usa QPA Windows e um HWND real, consulta o mesmo SMTC externamente por `GetForWindow` e verifica estado inicialmente desabilitado, status Playing, flags de transporte, título/artista/álbum e limpeza em `Closed`.

Os Reds focados expuseram símbolos ainda ausentes, intenção oposta perdida antes do ACK, `Ready` obsoleto descartando intenção, capacidade de seek publicada durante confirmação e rebind tardio após perda da surface. Os Greens correspondentes passaram; o filtro `[async]` fechou 42 asserções em cinco casos, e `m0.qt.system-media-controls.binding`, `m0.qt.windows.smtc` e `m0.qt.windows.smtc-native` ficaram verdes. A consolidação Release passou 239/239 no CTest combinado e 190/190 no core Qt OFF. `m0_qt_shell_ui_qmllint` ficou verde; nenhum QML mudou. A composição `BUILD_TESTING=OFF` recompilou, listou zero testes e rejeitou o hook reservado com exit 1. Nenhum benchmark foi executado.

O smoke nativo comprova publicação e limpeza sobre o SMTC real, mas não sintetiza teclas físicas, não substitui pacote/E2E e não cobre MPRIS. M4.5, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E` e `CHK-CLOSE` permanecem abertos globalmente.

### M4.6 — primeiro recorte da página oficial de Aplicativos Padrão

Para a [identidade RC Music](../../experiments/m0/candidates/qt/README.md#rc-music-identity), verificar marca invariável sob tradução, título da janela QML e configuração repetida do nome visível do Qt, mantendo organização/aplicação e raízes de configuração/dados anteriores. Restaurar as propriedades globais no teste; não escrever dados nem modificar a fixture autenticada. O runner gráfico do contrato usa o mesmo ambiente offscreen estrito do Quick. Isso não comprova identificação SMTC, metadados Win32 ou associações instaladas.

As provas focadas deste recorte permanecem isoladas por fronteira e não abrem as Configurações reais durante a suíte:

- o presenter cobre suporte separado de disponibilidade, transições `Idle` → `Opening` → resultado tipado, sinal dedicado, supressão de solicitações duplicadas em voo, recuperação depois de `Failed`, indisponibilidade sem chamar a plataforma, exceção convertida em falha e conclusão tardia inerte após destruição ou commit efetivo da saída; uma saída ainda reversível permite concluir o handoff e voltar a abrir depois que a persistência falha e cancela a saída;
- `QtShellPlatform` recebe operações injetadas e prova que o suporte é Windows-only, que somente a URI interna fixa `ms-settings:defaultapps` chega ao opener, que disponibilidade falsa e saída impedem o dispatch, que retorno falso/exceção viram `Failed` e que cada solicitação aceita conclui uma única vez;
- o teste Quick verifica nome, descrição e papel acessíveis do botão, ativação por teclado, posição no ciclo de foco, motivo de desabilitação, feedback localizado de indisponibilidade/falha, ocultação completa quando a plataforma não suporta a operação e anúncio assertivo apenas com o painel aberto e visível.

Os Reds focados falharam pelas fronteiras, propriedades/estados e contratos de adapter/UI ainda ausentes. Os três Greens focados passaram individualmente: `m0.qt.shell-platform` 1/1, `m0.qt.presenter.initial-load` 1/1 e `m0.qt.quick.shell-states` 1/1. A consolidação passou 239/239 no CTest Release combinado, 190/190 no core Qt OFF e 1/1 no Quick com 100 casos internos; `m0_qt_shell_ui_qmllint`, formato de 131 C++/46 QML, composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado, e validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas ficaram verdes. Não se extrapola dessas provas a abertura efetiva das Configurações no desktop. Nenhum benchmark foi executado.

`Dispatched` comprova somente que o opener aceitou o handoff para o handler registrado; não prova visibilidade da página nem alteração de padrão. Escrita de `UserChoice`, ProgID, `Capabilities`, `RegisteredApplications`, matriz publicada de formatos, associação/double-click, pacote/instalador e implementação Linux não pertencem a este recorte. M4.6, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y` e os smokes de associação/pacote continuam abertos.

O [recorte posterior do template NSIS](../../experiments/m0/candidates/qt/windows-installer/README.md) compila e executa funções reais de cadastro/remoção sob `HKCU\Software\RCMusic.AssociationTests\<GUID>`, com leitura independente via Registry64. Verifica tipo/texto dos valores, aspas/Unicode, reinstalação, troca de diretório com manifesto fixo, desinstalador obsoleto, preservação de defaults/UserChoice/entradas externas, colisões não mutantes e retry após negação temporária de permissões. O runner restaura ACLs e remove somente sua subárvore fresca no `finally`; binários permanecem em `out/m0`. O harness utiliza marcador de texto como alvo e não abre mídia, não notifica Explorer nem escreve associações reais. O comando e o recibo do compilador estão no README do template. Esse check não depende de rebuild C++/QML e não substitui instalação/upgrade/uninstall do payload, matriz de formatos ou smoke nativo.

O [runtime real posterior](../../experiments/m0/candidates/qt/README.md#rc-music-local-runtime) exige plano de cópia com origem/destino seguros, criação exclusiva, identidade dos inputs compilados e hashes, seguido de checagem dos arquivos reais montados. Probes rodam sem ambiente Qt herdado: produção OFF recusa hooks e instrumentação separada observa startup/lifecycle com configuração temporária. Executáveis e QtTest dos probes não entram no payload; ausência de SDK no ambiente não substitui máquina limpa, CRT redistribuído, revisão de licenças ou inspeção nativa. Os comandos exatos ficam no README do template.

A jornada focada que sustenta a matriz local passou 18/18 no Qt Test: sete linhas básicas do corpus, dois aliases `.mp4`/`.oga` e sete cópias temporárias com sufixo neutro `.audio`, além de setup/cleanup. Exercita carga/metadados, pausa, seek a 50% com tolerância de 500 ms nas posições do presenter e backend, retorno a zero e reprodução até EOF. A reprodução fica mutada; não comprova áudio físico nem generaliza para todo perfil de codec. `.audio` não vira extensão publicada, e Opus/ALAC candidatos permanecem fora da declaração. O incremento C++ alterou somente o teste, sem correção de produção alegada.

A [integração local de M4.8](../../experiments/m0/candidates/qt/README.md#rc-music-local-installer) separa testes puros do plano, contrato nativo dos helpers NSIS e jornadas compiladas de instalação/atualização/remoção. O runner de lifecycle usa versões com arquivos adicionados/removidos, Registry GUID e sentinelas de mídia/configuração; verifica os hashes por leitura independente, não apenas exit code. O caso opcional do runtime real instala/remove o inventário inteiro em isolamento, sem tocar a instalação pessoal nem iniciar o player. Build do instalador valida origem, fontes/receipt e compilação `/WX`; esses checks não são benchmark, máquina limpa, inspeção nativa ou aprovação da matriz de formatos.

A evolução de associações confirmou Red com uma aprovação/seis falhas e Green 7/7, seguidos de Installer 32/32 e Associations 14/14, sem skips. Cobre legado OFF → ON pela assinatura vazia e `/VERIFY`, troca MP3/WAV → WAV/FLAC com protocolo de limpeza v2, ON → OFF, recusa não mutante do legado ON com lista diferente, preservação de entradas externas e retry após colisão ou negação temporária de limpeza. Manter descrições dos tipos retidos, sentinelas de defaults/UserChoice/OpenWithList e raízes GUID de Registry/menu em todos os cenários. O plano mantém associações OFF sem `-FormatMatrix`; quando informado, o JSON revisado/versionado deve corresponder aos hashes do runtime, corpus e fonte de teste, e sua identidade integra o payload. O campo `result_sha256` referencia evidência humana: validar seu formato não comprova execução/autenticidade do resultado. O escopo continua `windows-x64-local-development`; esses checks não fecham M4.6/M4.8, stack, formatos públicos ou validações manuais/nativas.

O [contrato do atalho Windows](../../experiments/m0/candidates/qt/windows-installer/README.md#contrato-do-atalho-windows) exige raiz de menu compilada e isolada em todos os cenários do runner. O oracle lê o `.lnk` via COM sem salvar, confirma destino/argumentos/diretório de trabalho e compara seus bytes antes/depois da leitura. Instalação, reinstalação, atualização de legado sem atalho e desinstalação devem manter a entrada coerente com a versão ativa; conflitos não autorizam mutações em links externos. Executar também o caminho com associações OFF, sem confundir essa prova com ativação pelo menu Iniciar real.

### M4.7 — primeiro recorte de suspend/resume no Windows

As provas deste recorte ficam separadas por fronteira e não suspendem fisicamente a máquina:

- o presenter comprova que o primeiro `Suspending` captura a posição confirmada da geração corrente e inicia `requestSaveNow()` sem `flush()`, pause, play ou autoplay; uma repetição consecutiva não cria outro checkpoint;
- o presenter também comprova que o polling funcional fica quiescente durante a suspensão e volta em `Resumed`; tanto uma retomada inicial quanto a retomada após suspensão consultam o backend e reconciliam estado, posição e erro da geração corrente sem comandos de transporte; duplicatas e eventos após a saída são inertes;
- `SystemPowerEventsBinding` comprova instalação/substituição do handler, entrega ao presenter, desarme no teardown e shutdown único, sem callback posterior;
- `WindowsPowerEvents` usa uma boundary nativa injetada para comprovar que `start()` cria e registra uma única janela receptora privada, com `HWND` não nulo e distinto da janela visual, e permanece idempotente enquanto disponível;
- filtros de event type, mensagem e `HWND`, entrega síncrona, exceção confinada, `Resumed` inicial e deduplicação de transições nativas consecutivas são exercitados no registro contínuo; eventos reconhecidos, inclusive duplicados, retornam consumidos com resultado `TRUE`, enquanto entradas alheias retornam `false` sem alterar o sentinel;
- falha inicial comprova o backoff single-shot finito de 1 s, 5 s e 30 s e o fim do orçamento; o contrato permite reiniciá-lo por outro `start()` explícito ou `applicationStateChanged`, com a segunda rota coberta no caso de recuperação; shutdown cancela o retry e torna seu timeout tardio inerte;
- criar, destruir e recriar uma janela visual não interrompe nem troca o receptor privado, enquanto shutdown libera exatamente uma vez o registro e torna um novo `start()` inerte;
- um caso QPA Windows registra de fato o `HWND` por `RegisterSuspendResumeNotification` e desfaz o registro, sem emitir uma suspensão real nem alterar o estado de energia do host.

Os alvos focados deste recorte são `m0.qt.presenter.initial-load`, `m0.qt.system-power-events.binding` e `m0.qt.windows.power-events`. Resultado final dos alvos, consolidação Release/core, formato, composição `BUILD_TESTING=OFF` e validação documental: build Release aprovado, 241/241 testes no CTest combinado sequencial, 190/190 no core Qt OFF, `qmllint` e formato de 139 C++/46 QML verdes; `BUILD_TESTING=OFF` recompilado com zero testes e hook reservado rejeitado em exit 1 sem criar a raiz solicitada; validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas aprovada. Nenhum QML foi alterado por este recorte; nenhum benchmark foi executado.

Os Reds falharam pela API de power ainda ausente, pelo polling que avançava durante suspensão com persistência pendente e pelo filtro que devolvia `false` em mensagens reconhecidas. Os Greens cobrem também `PostMessageW` real, inclusive callback que encerra ou destrói o adapter. O check dos 46 QML comparou a saída de `qmlformat` 6.11.2 normalizada em LF, pois essa versão não oferece `--check`. O manifesto de produção embutido conserva `longPathAware=true`; a validação estrita com `mt.exe` ainda rejeita o `processorArchitecture="*"` preexistente, sem alegação de aprovação integral desse manifesto.

Essas provas cobrem somente suspend/resume de `FR-038`. Elas não comprovam sleep físico, Modern Standby, hibernação, timing real de congelamento nem durabilidade do save assíncrono antes do freeze. Perda de dispositivo, drive desconectado, arquivo removido, restart do Explorer, pacote/E2E nativo e Linux continuam fora deste recorte; M4.7 permanece parcial.

### M4.7 — segundo recorte de retry da mesma ocorrência

O contrato de validação da recuperação explícita se distribui pelas fronteiras existentes:

- domínio: retry seleciona somente índice existente indisponível, sem torná-lo disponível; seleção normal continua recusando-o; confirmação de recuperação preserva ID/path/ordem/seleção, é idempotente e não altera duplicatas;
- modelo: limpar indisponibilidade e diagnóstico conserva metadata, índice persistente e seleção, emite `dataChanged` somente para a linha/roles afetadas e não reseta o modelo; um novo diagnóstico em item indisponível altera somente `ErrorCodeRole`, enquanto a repetição idêntica é inerte;
- presenter: `retryQueueOccurrence(ID)` usa somente o path interno, preserva seleção/backend quando o load é rejeitado, mantém indisponibilidade até confirmação `Ready`/`Playing`/`Paused` da nova geração e ignora confirmação antiga; falha da tentativa permanece acionável sem loop ou avanço automático;
- importação: retry explícito assume a intenção de reprodução antes de chegarem os lotes seguintes; problemas são deduplicados por ID de ocorrência e continuam contados depois que o item é recuperado;
- Quick: fila e erro expõem “Tentar reproduzir novamente” com teclado e semântica acessível; loading é associado ao ID correto, duplicatas permanecem distintas e “Abrir outro áudio” continua alcançável;
- integração real: a mesma ocorrência falha com um path temporário ausente e é tentada novamente após copiar uma WAV autenticada para esse path, preservando ID e modelo; não usar arquivos pessoais nem apresentar essa prova como perda/reconexão física de drive.

Os Reds, resultados executados e checks finais ficam no [registro central do segundo recorte](../../experiments/m0/candidates/qt/README.md#retry-m47). Não extrapolar esses testes para perda de dispositivo de saída, drive físico, localização do arquivo em outro path, Explorer, sleep físico, pacote/E2E nativo, NVDA ou Linux. M4.7 e `NFR-REL-005` permanecem abertos globalmente; benchmarks não pertencem ao recorte.

### M4.7 — terceiro recorte de recuperação da saída padrão

- o adapter Qt recebe uma fronteira de topologia injetável, cobrindo ausência inicial, perda ou substituição da saída durante `Loading`, `Ready`, `Paused` e `Playing`, e mudanças irrelevantes que conservam a saída padrão;
- a interrupção mantém geração, posição e duração confirmadas, retira o player anterior e publica erro tipado sem `Ended`, reconexão automática ou autoplay; sinais antigos e callbacks depois de shutdown/destruição são inertes;
- `LocalTrackSession` distingue erros da saída de arquivos indisponíveis; presenter e Quick cobrem recuperação explícita pausada, preservação da ocorrência/modelo, posição por geração, tentativa rejeitada ou repetida, seleção posterior e ações/anúncios acessíveis;
- uma regressão com writer falso perde a saída durante pause ou seek antes da entrega do erro enfileirado e exige que o snapshot persistido conserve posição e fila; a tentativa explícita seguinte busca o alvo original sem play;
- a integração usa decoder Qt Multimedia e metadata TagLib reais, WAV autenticada, saída mutada e topologia injetada para restaurar a posição depois de uma tentativa ainda sem dispositivo. Essa prova não altera hardware nem depende de arquivos pessoais.

Os Reds e checks executados são registrados somente no [registro central de recuperação de saída](../../experiments/m0/candidates/qt/README.md#audio-output-m47). Não extrapolar os fakes para desconexão/reconexão física USB/Bluetooth/HDMI, sinais reais da topologia Windows, áudio audível, sleep, Explorer, pacote, NVDA ou Linux. M4.7, `FR-038` e `NFR-REL-005` continuam parciais; a escolha manual de dispositivo em `FR-018`/M6.1 não pertence a este recorte. Benchmarks permanecem adiados.

### M4.7 — quarto recorte de recuperação da taskbar Windows

- o adapter da plataforma recebe diretamente `handleTrayHostRestart()` nos testes determinísticos: somente a janela ocultada pelo próprio adapter volta minimizada; janela visível, já minimizada, bootstrap e rebind preservam sua disposição;
- o binding com presenter e backend falso cobre `Playing`/`Paused`, posição confirmada, geração, fila com duplicatas, IDs e modelo sem reset, além da ausência de comandos de áudio; retorno do shell não reaplica `startMinimized`;
- saída pendente impede revelação, retorno da interação revalida a recuperação e saída confirmada/destruição tornam eventos antigos inertes;
- o monitor Windows exercita registro/retry, receptor `HWND` privado real, filtragem de mensagem/tipo/handle, entrega enfileirada e teardown. A capacidade de ocultação falha fechada se o monitor não estiver pronto;
- a caracterização nativa do Qt remove por `NIM_DELETE` somente o ícone do próprio teste e envia `TaskbarCreated` somente a seu receptor. A prova combina `Shell_NotifyIconGetRect` com ACK de `NIM_MODIFY` no-op (flags zero), que não altera nem adiciona um ícone ausente; após remoção, exige HRESULT de falha e ACK falso. Overflow legítimo pode devolver `S_FALSE` com retângulo zerado, portanto dimensões positivas não são requisito de registro. O teste preserva menu/tooltip/ícone e exercita ativação de teclado sem popup nem broadcast.

Reds, checks executados e limites ficam no [registro central de taskbar](../../experiments/m0/candidates/qt/README.md#taskbar-m47). `QSystemTrayIcon::isVisible()` e presença do shell não comprovam que `NIM_ADD` foi aceito; geometria pública vazia não demonstra ausência de suporte. A caracterização do Qt não é um novo Red do app e não encerra Explorer. M4.7/`FR-038`, restart real do Explorer, pacote, acessibilidade nativa e Linux continuam abertos; nenhum benchmark pertence ao recorte.

### Localização de arquivo M4.7

Os contratos de localização preservam a separação entre escolher uma origem e confirmar sua reprodução:

- core: substituir somente o path de um ID indisponível, preservando seleção/ordem/duplicatas/allocator, rejeitando entrada inválida ou idêntica sem mutação, sem I/O e com snapshot round-trip sem mudança de schema;
- modelo: limpar metadata da origem anterior mesmo quando somente a pasta muda, projetar `source_relocated` com `available=false` e preservar índices persistentes/seleção com `dataChanged` restrito, sem reset;
- presenter: concluir uma única solicitação por geração/ID/path, tratar cancelamento/falha/rejeição e callbacks antigos, preservar outra faixa enquanto se localiza uma ocorrência não atual e usar `RestorePaused`/geração nova para a atual, com posição vinculada ao seek; persistir o alvo restaurado ainda pendente diante de erros de origem recuperáveis, sem perder os cancelamentos por seleção/transporte explícito e geração;
- picker: uma seleção local por diálogo assíncrono, sem navegação automática para drive ausente, com rejeição, cancelamento e teardown seguros; a factory widget permite testes determinísticos sem hooks de produção;
- UI: ações de localizar no erro e na fila, estado pendente por ID, erro acionável, retry preservado, teclado, semântica acessível e foco; seleção de path não pode ser anunciada como recuperação confirmada;
- integração: arquivo renomeado em diretório temporário, porta de localização controlada e decoder real, sem depender de arquivos pessoais ou de desconectar dispositivos do host.

Reds, Greens consolidados dos alvos e riscos residuais ficam no [registro central de localização](../../experiments/m0/candidates/qt/README.md#file-location-m47); resultados novos não devem ser inferidos dos testes antigos. Uma reexecução verde do coordinator com logs habilitados não demonstra a causa nem corrige a falha intermitente da primeira consolidação. Fakes ou diálogo widget não substituem inspeção do seletor nativo, NVDA, drive físico desconectado ou pacote/E2E Windows. M4.7/`UX-008`/`NFR-REL-005` e Linux permanecem parciais, sem benchmarks neste recorte.

### Seleção principal assíncrona

O [foco deferido da fila](../../experiments/m0/candidates/qt/README.md#queue-deferred-focus) exige manter a mesma ocorrência após mover B e inserir X antes dela no mesmo turno; Delete deve remover B, preservando as outras. Cobrir também modal, perda de autoridade, ocultação/reabertura e remoção da faixa atual no tail, quando um auto-follow antigo não pode substituir a seleção de teclado posterior. Preservar o CRUD legado junto às regressões menores.

Para a [entrada de URLs locais](../../experiments/m0/candidates/qt/README.md#filesystem-url-boundary), fixtures válidas devem usar paths absolutos e casos relativos precisam ser construídos explicitamente. Cobrir NUL decodificado versus `%00` literal, namespaces Windows recusados, Unicode/espaços, UNC/extended, ordem e duplicatas. Asserções observam as raízes enviadas à discovery sem tocar no filesystem; drop conserva válidos e conta inválidos, enquanto a seleção mantém rejeição atômica. O contrato IPC deve permanecer verde antes/depois de compartilhar a policy.

Os checks de Abrir/Adicionar devem separar aceitação, conclusão e efeito sobre a fila:

- porta/presenter: callback inline reenfileirado, entrega duplicada/atrasada inerte, start rejeitado ou com exceção, lote inválido rejeitado atomicamente, cancelamento sem mutação e rejeição do load preservando contexto;
- concorrência: fila viva após IPC, avanço, limpeza e reordenação durante o diálogo, preservando IDs/duplicatas; abrir o seletor não envia áudio, mas também não bloqueia comandos e eventos normais;
- lifecycle: cancelamento por saída, troca de porta e destruição, NOTIFY do fim do estado ocupado mesmo quando persistência impede a saída, sem callback terminal emitido pelo destrutor; reentrância ao publicar início/conclusão não pode aplicar estado antigo;
- helper Qt: seleção única/múltipla, paths locais Unicode e duplicados, timers ativos, ownership da factory, cancel/rebind, janela recebida destruída durante cancelamento e equivalência do localizador após extração;
- UI: foco após conclusão efetiva, não após mera aceitação, com ID estável e revalidação de janela/modal/saída; ações mutuamente exclusivas e feedback acessível sem anunciar cancelamento como falha; marcadores locais de solicitação são limpos também quando rebind/saída encerram o estado ocupado sem emitir `Finished`, no mesmo turno de eventos ou depois dele; tarefas de foco já enfileiradas, inclusive `setQueueOpen`, não atravessam um novo seletor.

Reds, Greens consolidados e limites ficam no [registro central do seletor assíncrono](../../experiments/m0/candidates/qt/README.md#async-audio-picker). A invocação deve respeitar o ambiente definido por alvo: exportar `QT_FATAL_WARNINGS=1` globalmente alterou indevidamente dois testes de áudio e foi corrigido sem enfraquecer asserções ou CMake. O retorno verde do coordinator com diagnóstico ampliado não demonstra nem corrige a causa da intermitência anterior. O core Qt OFF não foi reexecutado naquele recorte sem mudança comportamental no núcleo. Testes widget/offscreen não substituem a inspeção do diálogo nativo Windows ou NVDA. A sanitização textual/numérica de `FR-023` pertence ao incremento separado descrito abaixo; os checks do seletor não a validam nem afirmam exploração. Nenhum benchmark integra estes checks funcionais.

### `MetadataReader`

- tags presentes/ausentes;
- fallback de título;
- encoding inválido;
- capa corrompida e nos limites/fora dos limites NFR-LIMIT;
- cancelamento, geração/resultado atrasado, budget de 2 s e ausência de acúmulo de jobs;
- propriedades por formato;
- extensão enganosa.

### Sanitização textual e numérica de metadata

O recorte funcional de `FR-023` começa em Catch2 puro e segue pelas fronteiras de extração e apresentação:

- texto: UTF-8 válido nos limites de escalares, sequências truncadas/overlong, surrogates e valores acima de U+10FFFF; rejeição atômica sem prefixo parcial, idempotência, espaços/controles e ausência após transformação;
- preservação: scripts/acento combinado, ZWJ/ZWNJ, variation selectors e emoji; markup permanece literal, sem transliteração ou promessa de remover toda categoria Unicode invisível;
- números: leitura integral, zeros à esquerda, faixa/total, fronteiras `uint32_t`, ano simples, calendário e precisões de horário aceitas; vazio, sinais, lixo, NUL, overflow e datas/componentes impossíveis são rejeitados;
- adapter: o limite pré-normalização não pode ser contornado por espaços/controles; ausência, diagnóstico por campo e fallback não transformam uma tag inválida em falha de reprodução; derivados temporários autenticados não ampliam o corpus canônico;
- presenter/UI: códigos fechados e microcopy localizada sem conteúdo descartado; ausência não vira zero falso, e novos diagnósticos não produzem texto de contagem de bytes enganoso;
- integração real: leitura das tags transformadas e reprodução do arquivo temporário devem comprovar o caminho integrado sem arquivos pessoais ou áudio audível como dependência.

Policy, extrator e apresentação têm Red/Green confirmado; o core sem Qt também passou sua suíte completa. Caracterização integrada real, suíte Release combinada e composição de produção também passaram, com checks automatizados consolidados no [registro central](../../experiments/m0/candidates/qt/README.md#metadata-sanitization), sem usar os resultados anteriores como evidência nova. Este recorte não prova limites internos do TagLib, fuzz/corpus hostil completo, pacote, NVDA ou Linux e não encerra `FR-023`/`CHK-SEC`. Benchmarks permanecem adiados.

## 9. Corpus de áudio

O corpus é um produto de teste versionado. Deve conter sinal sintético ou mídia com licença explícita, nunca música comercial ou arquivo pessoal.

Para cada fixture, registrar:

- nome e finalidade;
- contêiner e codec/perfil;
- parâmetros: duração, CBR/VBR, sample rate, canais, bit depth;
- tags/capa esperadas;
- licença e origem/receita de geração;
- hash SHA-256;
- tamanho máximo razoável;
- capacidades esperadas: duration, seek, tags e art.

Corpus mínimo:

- MP3 CBR e VBR, mono e estéreo;
- M4A AAC-LC, com e sem tags/capa;
- FLAC 16-bit;
- WAV PCM;
- Ogg/Vorbis;
- Opus e ALAC como candidatos;
- arquivo truncado por contêiner;
- extensão trocada;
- tags Unicode e vazias;
- capa ausente, inválida e nos limites/fora dos limites NFR-LIMIT;
- arquivo sem permissão quando o SO permitir fixture controlada.

Arquivos grandes/longos para performance podem ser gerados sob demanda por script determinístico e não precisam estar no Git.

## 10. UI e acessibilidade

Cada componente interativo testa:

- role e accessible name;
- foco por teclado e devolução de foco ao fechar painel;
- estados enabled/disabled/pressed/expanded;
- `Space` não conflita com inputs;
- tema e preferências atualizam rótulos;
- erros têm mensagem e ação;
- seek preview ignora ticks sem perder confirmação;
- texto longo/metadata parcial não remove controles.

Testes automatizados de axe ou ferramenta equivalente ajudam, mas não substituem NVDA/Orca.

Matriz manual mínima antes do MVP:

| Ambiente | Teste |
|---|---|
| Windows 11 + NVDA | jornadas abrir, controlar, fila, preferências, tray e erro |
| Windows 11 high contrast | foco, iconografia, slider e mensagens |
| Escala 100/150/200% | layout, tooltip, menus e painel |
| Windows reduced motion | transições e feedback |
| Linux referência + Orca | jornada principal e MPRIS quando disponível |

## 11. Integração e E2E

Jornadas automatizadas prioritárias:

1. estado vazio → abrir fixture → ready/play/pause;
2. arrastar seek → preview → confirmar → posição;
3. alterar saltos → usar botão/teclado → reiniciar → valor preservado;
4. adicionar/reordenar/remover fila;
5. arquivo inválido entre válidos → erro → próxima faixa;
6. tema sistema/claro/escuro sem perder estado;
7. segunda invocação encaminha path à primeira;
8. recuperação após término forçado restaura pausado.

Tray, associações, SMTC/MPRIS e install/uninstall devem ter smoke tests específicos, pois uma suíte de DOM não os prova. O smoke nativo atual de SMTC valida publicação e limpeza da sessão da janela; evento de tecla física e jornada empacotada continuam checks separados.

## 12. Performance e estabilidade

### Micro/benchmarks

- transições de fila com 1, 100 e 1.000 itens;
- serialização/restauração de sessão;
- leitura de metadata sem arte e com arte limitada;
- coalescing de 10.000 ticks simulados;
- tempo de atualização de view model.

### Cenários end-to-end

- cold start P50/P95 em build Release;
- open-to-audible com app aberto;
- seek-to-audible em MP3 VBR e M4A;
- working set/CPU vazio, pausado e tocando;
- adicionar diretório com 1.000 itens;
- reprodução contínua por oito horas;
- 500 trocas de faixa/seek para procurar leak ou deadlock.

### Comparação A/B do M0

Protocolo histórico, não pré-requisito de M1 após o ADR-0005. A/B só será retomado por solicitação explícita. Benchmarks do produto e Linux continuam adiados, sem reduzir metas nem declarar gates de release aprovados. Os recortes de evidência anteriores deste documento conservam seus limites originais.

- congelar antes da coleta o protocolo, os cenários, marcadores, tolerâncias e critérios de descarte;
- comparar shells Release com o mesmo recorte de UX, assets, dados, roteiro e baseline de máquina, usando backend falso e sem áudio ou rede reais;
- separar primeira execução, cold start e execução aquecida, com pelo menos 20 lançamentos válidos por cenário e candidato;
- medir externamente processo iniciado → janela acionável e agregar private working set/private bytes/CPU da árvore de processos; relatar memória compartilhada, pacote e pré-requisitos offline separadamente da métrica primária de RAM;
- executar a mesma jornada com NVDA, somente teclado, alto contraste, escalas 100/150/200%, movimento reduzido, bandeja e smoke Linux de referência;
- comparar Qt Multimedia/FFmpeg + TagLib e Rodio/CPAL/Symphonia + Lofty separadamente dos shells, usando o mesmo corpus, ambiente de áudio e casos de contrato;
- cobrir NFR-PERF-001–007 e tratar falha de acessibilidade P0 como bloqueadora, independentemente do resultado de recursos.

Não executar benchmark confiável junto de jobs concorrentes ou build debug. Registrar hardware, SO, energia, versão/commit, amostra, processo/árvore observada e método. No protocolo histórico, resultado dentro das tolerâncias pré-registradas era empate; não atribuir vitória comparativa à decisão explícita atual.

## 13. Cobertura e mutação

Metas iniciais após bootstrap:

| Área | Lines | Branches | Observação |
|---|---:|---:|---|
| Domain | ≥ 90% | ≥ 85% | regras puras devem ser densamente cobertas em qualquer candidato |
| Application | ≥ 85% | ≥ 80% | fakes cobrem falhas e cancelamento em qualquer candidato |
| Estado/presentation utilities | ≥ 85% | ≥ 80% | componentes também exigem assertions semânticas |
| Adapters/plataforma | sem quota cega | — | contratos, corpus e smoke tests valem mais que mocks de FFI |

Exclusões precisam ser justificadas. Não adicionar testes sem valor apenas para elevar percentual.

Mutation testing pode rodar periodicamente no domínio crítico (fila, seek, preferências) quando a suite estabilizar. Mutante sobrevivente exige teste ou justificativa; não precisa bloquear todo commit pessoal.

## 14. Flakiness e determinismo

- relógio real e sleeps são proibidos em unitários;
- random exige seed registrada;
- testes não dependem de ordem, rede ou locale global sem controle;
- diretórios temporários são únicos e removidos pelo framework;
- áudio real e UI desktop têm timeout explícito e diagnóstico;
- retry não “corrige” teste flaky; primeiro registrar e corrigir a causa;
- quarantine exige issue/registro, responsável e prazo, nunca silêncio.

## 15. Evidência no commit

Para [diagnóstico local Windows](../../experiments/m0/candidates/qt/README.md#local-diagnostics), separar os testes puros de formato/fila/limites dos testes físicos de slots e dos subprocessos. Um controle sem proteção precisa demonstrar os canais Qt/CRT/Win32 antes de exigir silêncio no filho protegido; stdout fixo e encerramento fatal continuam observáveis. A jornada do executável usa WAV sintético silencioso e config temporária, verifica opt-in, decoder real e evento de sessão inválida, sem suprimir mensagens no runner. Symlink indisponível por política do host é skip registrado, nunca aprovação. Rodar com Qt habilitado não substitui o core Qt OFF.

Antes de commitar comportamento:

- revisar o teste Red e o motivo de falha;
- executar o teste focado durante Green/Refactor;
- executar o conjunto do módulo;
- executar os [gates definidos](quality-gates.md) conforme o risco;
- revisar `git diff --check` e o diff completo;
- atualizar requisito/ADR quando o contrato mudou;
- registrar validação não executada e risco residual no handoff.

Commits devem conter teste e implementação juntos. Um spike pode usar harness exploratório sem teste apenas para medir uma API/ambiente e não pode implementar comportamento de produto; qualquer código promovido segue TDD antes de entrar no baseline. Nunca commitar uma suíte deliberadamente quebrada na branch principal.

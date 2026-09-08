# Estratégia de testes e TDD

**Estado:** contrato de qualidade inicial  
**Última revisão:** 2026-09-05
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

O [cliente de extensão opcional](../browser-extension.md#checks-locais) usa `node:test`/assert/VM com DOM, Chrome e relógio falsos, sem runtime npm no produto. Exercitar scripts de produção, validação/schema, escolha explícita, navegação/SPA, coalescência sem starvation e rejeição/timeout sem retry; teste estático de manifesto/CSP não substitui carregamento no Brave. O runner é separado do C++/Qt e não adiciona Node à compilação/execução da Lite. A biblioteca nativa opcional tem Qt Test para codec estrito, framing parcial/limites/deadline e sessão com admissão falsa: nenhuma URL é aberta. Vetores JSON compartilhados conferem o contrato JavaScript/C++; entradas byte-level cobrem tolerâncias do parser que reserialização esconderia. Executável de host, serviço/downloads e integração instalada exigirão testes próprios, sem fixtures de sites ou perfis pessoais.

- fixtures são compartilhadas por helper explícito e pelo mesmo manifesto versionado;
- nomes em inglês descrevem resultado, por exemplo `skip_forward_clamps_at_track_end`;
- usar Arrange–Act–Assert ou Given–When–Then, sem comentários redundantes;
- diferenças inevitáveis de harness ficam documentadas, mas resultado, tolerância e semântica esperados permanecem iguais.

### E2E e plataforma

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

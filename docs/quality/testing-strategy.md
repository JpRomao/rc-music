# Estratégia de testes e TDD

**Estado:** contrato de qualidade inicial  
**Última revisão:** 2026-09-01
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

## 4. Organização proposta

O ciclo TDD, os casos de contrato, o corpus e os IDs de cenário são comuns aos candidatos. Cada linguagem pode ter seu harness idiomático, mas não pode omitir um caso porque sua API concreta o torna inconveniente.

### C++20 + Qt 6 Quick/QML — hipótese principal

- usar Catch2 como runner único para domínio, application e contratos C++ puros; não adicionar uma segunda suite equivalente;
- registrar os executáveis no CTest e manter testes independentes de `QCoreApplication` quando o código exercitado pertence ao núcleo;
- usar Qt Test para presenters, adapters Qt e integrações de sinais/slots; usar Qt Quick Test somente onde renderização/comportamento QML for necessário;
- injetar factories C++ nas suites de contrato para executar os mesmos casos contra fakes e contra Qt Multimedia/FFmpeg, TagLib e persistência concreta;
- confirmar que testes de `domain`, `application` e `ports` compilam sem headers, tipos, event loop ou plugins Qt.

### Rust + Tauri 2 + Svelte — desafiante A/B

- manter testes unitários Rust próximos ao módulo em `#[cfg(test)]` quando ajudam a leitura;
- manter casos de uso e contratos Rust no diretório de testes definido pelo bootstrap, sem exigir WebView/Tauri para o núcleo;
- executar a mesma matriz de contratos contra fakes e contra Rodio/CPAL/Symphonia, Lofty e persistência concreta;
- testar componentes Svelte com Vitest + Testing Library, consultando por role/nome como o usuário ou leitor de tela;
- evitar selecionar classes, estrutura de DOM ou `data-testid` quando existe semântica; testar funções puras sem render e limitar snapshots a estruturas pequenas e revisáveis.

### Convenções comuns

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
| `FakePlatformIntegration` | tray presente/ausente, close, media keys, suspend/resume |
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
- rejeitar posição negativa ou positiva sem ocorrência atual e nunca usar preview/interpolação como posição confirmada. Seek confirmado, pausa, parada/fim, erro terminal, suspensão e saída pertencem a testes futuros da composição;
- após falha ou versão futura, provar suspensão do rearm automático mesmo com novas mudanças, dirty preservado, completion antiga incapaz de limpar a revisão atual e retomada somente por retry/substituição/save-now/flush explícitos.

Esses testes não comprovam restauração sempre pausada, wiring do timer, eventos reais de playback/lifecycle, saída visual nem reinício/crash do executável: presenter e bootstrap ainda não integram `SessionWriter`/`SessionAutosave`, e essas provas permanecem em M4.1.

### `MetadataReader`

- tags presentes/ausentes;
- fallback de título;
- encoding inválido;
- capa corrompida e nos limites/fora dos limites NFR-LIMIT;
- cancelamento, geração/resultado atrasado, budget de 2 s e ausência de acúmulo de jobs;
- propriedades por formato;
- extensão enganosa.

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

Tray, associações, SMTC/MPRIS e install/uninstall devem ter smoke tests específicos, pois uma suíte de DOM não os prova.

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

- congelar antes da coleta o protocolo, os cenários, marcadores, tolerâncias e critérios de descarte;
- comparar shells Release com o mesmo recorte de UX, assets, dados, roteiro e baseline de máquina, usando backend falso e sem áudio ou rede reais;
- separar primeira execução, cold start e execução aquecida, com pelo menos 20 lançamentos válidos por cenário e candidato;
- medir externamente processo iniciado → janela acionável e agregar private working set/private bytes/CPU da árvore de processos; relatar memória compartilhada, pacote e pré-requisitos offline separadamente da métrica primária de RAM;
- executar a mesma jornada com NVDA, somente teclado, alto contraste, escalas 100/150/200%, movimento reduzido, bandeja e smoke Linux de referência;
- comparar Qt Multimedia/FFmpeg + TagLib e Rodio/CPAL/Symphonia + Lofty separadamente dos shells, usando o mesmo corpus, ambiente de áudio e casos de contrato;
- cobrir NFR-PERF-001–007 e tratar falha de acessibilidade P0 como bloqueadora, independentemente do resultado de recursos.

Não executar benchmark confiável junto de jobs concorrentes ou build debug. Registrar hardware, SO, energia, versão/commit, amostra, processo/árvore observada e método. Resultado dentro das tolerâncias pré-registradas é empate e segue os critérios de NFR-STACK-006, não preferência de linguagem.

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

Antes de commitar comportamento:

- revisar o teste Red e o motivo de falha;
- executar o teste focado durante Green/Refactor;
- executar o conjunto do módulo;
- executar os [gates definidos](quality-gates.md) conforme o risco;
- revisar `git diff --check` e o diff completo;
- atualizar requisito/ADR quando o contrato mudou;
- registrar validação não executada e risco residual no handoff.

Commits devem conter teste e implementação juntos. Um spike pode usar harness exploratório sem teste apenas para medir uma API/ambiente e não pode implementar comportamento de produto; qualquer código promovido segue TDD antes de entrar no baseline. Nunca commitar uma suíte deliberadamente quebrada na branch principal.

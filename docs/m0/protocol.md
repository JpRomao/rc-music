# Protocolo comparativo M0

**Estado:** congelado para M0.0; nenhuma coleta comparativa executada

**Identificador:** `M0-P1`

**Ambiente:** [`M0-E1`](environment.md)

**Layout:** [`M0-L1`](layout.md)

Este protocolo mede produto observável, não reputação de linguagem. Qt e Tauri recebem a mesma fixture, roteiro, ordem, budgets e regras de falha. Shell e áudio são medidos separadamente; somente ramos viáveis são combinados.

## 1. Governança e integridade

- Somente build Release identificável por commit, manifests, lockfiles quando aplicáveis e SHA-256 é decisório.
- O executável parte de bundle/pasta staged equivalente ao pacote; não é iniciado por IDE, `cargo`, `npm` ou launcher de desenvolvimento.
- Protocolo, ambiente, fixture, assets, roteiro e harness têm versão/hash idênticos nos dois candidatos.
- O workspace deve estar limpo antes do build e da coleta.
- Candidatos são códigos opacos `A` e `B` no raw/análise; o mapeamento é revelado somente depois de o resumo estatístico ser fechado.
- Marcador autorrelatado pelo candidato é diagnóstico e nunca encerra métrica primária.
- Mudança de protocolo, fixture, runtime, driver relevante, configuração ou harness encerra o batch e exige repetir os dois candidatos.
- Linux recebe smoke e métricas diagnósticas separadas; amostras não são combinadas entre sistemas operacionais.
- Nenhum score composto escolhe a stack.

M0.1 materializa enums, constantes e unidades em `protocol.v1.json`, que é a fonte executável do harness; este documento continua sendo o contrato interpretativo. Testes golden exigem igualdade de todo valor duplicado entre Markdown, JSON e schemas. Divergência falha o harness/`CHK-DOC`, e o schema não pode afrouxar o contrato.

## 2. Preflight do host

Antes de uma tentativa que cria processo, o harness exige uma janela contínua de 30 s com:

- CPU externa média abaixo de 3% da máquina;
- disco do workspace abaixo de 5% de active time;
- nenhum processo do candidato vivo;
- energia, tela, escala e runtime iguais ao ambiente congelado;
- nenhuma atualização/reinicialização pendente;
- nenhum build, debugger, profiler, indexação deliberada, RDP ou uso humano concorrente.

Se a máquina não estiver quieta, o início é adiado e não conta como tentativa. Defender e serviços do sistema ficam na mesma configuração para os dois candidatos; não se cria exclusão seletiva. Suspensão é desativada durante batches longos.

Em cenários de ação com harness/backend deliberadamente aberto, a mesma janela de 30 s é aplicada antes de cada `action_observation`, excluindo apenas a árvore esperada do cálculo de ruído externo; nenhuma árvore extra pode existir e o estado inicial específico do cenário precisa ser confirmado. Launch, recursos, resposta visual e fila usam um processo novo por tentativa. Áudio de adapter usa observações sucessivas no mesmo harness aberto para representar “app aberto”; soak é uma única execução longa com preflight próprio.

O shell e o áudio local funcionam com egress negado. A deny rule permanece nos benchmarks temporizados; captura de tráfego e inspeção de fonte/configuração rodam em smoke de privacidade separado, porque o capturador não pode contaminar CPU/latência. Funcionamento offline é um check, enquanto ausência de telemetria exige ambos os smokes. Loopback local usado por instrumentação não autoriza fonte remota.

## 3. Tentativas, ordem e descarte

Cada candidato/cenário curto recebe exatamente 22 tentativas planejadas:

- pelo menos 20 precisam ser válidas;
- no máximo 2 podem ser descartadas (`2/22 = 9,09%`);
- todas as válidas entram nas estatísticas, nunca apenas as 20 melhores;
- 3 descartes tornam o batch inconclusivo e exigem nova bateria completa após corrigir a causa.

`attempt` é uma observação estatística. Quando o processo permanece aberto no áudio, cada tentativa contém exatamente uma `action_observation` após restaurar o estado inicial; não se contam várias ações correlacionadas como amostras adicionais.

A ordem vem de 22 blocos pareados, exatamente 11 `AB` e 11 `BA`. Para definir o lado de cada bloco sem PRNG dependente de runtime, M0.1 calcula SHA-256 UTF-8 de `M0-P1|20260823|block:<1..22>`, ordena os hashes como bytes unsigned, atribui `AB` aos 11 menores e `BA` aos demais, depois executa os blocos na ordem numérica. A agenda e seu SHA-256 são commitados antes da execução e validados por golden vector. Não se altera a ordem após ver resultados.

Descartes permitidos, classificados sem consultar a métrica:

- `harness_failure`: captura, relógio ou arquivo raw falhou antes de observar o resultado;
- `external_os_event`: update, reboot, troca de energia/display ou interrupção externa comprovada;
- `operator_protocol_breach`: input humano ou configuração incorreta detectada;
- `audio_endpoint_changed`: endpoint foi removido/trocado durante tentativa de áudio.

Não são descartáveis: crash, hang, timeout, janela não acionável, budget violado, alto uso do candidato, helper que escapa do shutdown, stall, underrun ou erro de codec. São falhas observáveis. Ausência de resultado até o timeout do cenário é `failed`, não outlier. Qualquer `failed` torna o candidato `fail` naquele cenário, mesmo que 20 execuções numéricas passem; menos de 20 válidas ou mais de 2 descartes torna o batch `inconclusive`.

## 4. Classes de lançamento

Cada classe possui 22 tentativas por candidato:

| Classe | Estado inicial |
|---|---|
| `profile-first` | package já staged/instalado; diretórios privados de configuração/cache inexistentes; perfil novo por tentativa |
| `process-cold` | perfil inicializado a partir da mesma imagem; árvore encerrada; 30 s de preflight; nenhum launch de priming |
| `warm` | uma execução não medida termina normalmente; árvore desaparece; espera fixa de 10 s; então inicia a medida |

`process-cold` é a definição operacional de cold start de NFR-PERF-001. O cache global do Windows não é esvaziado artificialmente, e o relatório não chama essa medida de “boot-cold” ou “storage-cold”. A agenda balanceada controla deriva e posição. Uma pequena rodada após reboot pode ser publicada como diagnóstico, mas não substitui as 22 tentativas nem entra no gate.

Perfis são cópias de imagens canônicas hashadas; um candidato não reutiliza diretório do outro.

## 5. Janela acionável

No Windows, `t0` é capturado por `QueryPerformanceCounter` imediatamente antes da criação externa do processo. O harness acompanha wrapper, processo principal e descendentes.

Uma janela fica acionável quando, por observação externa:

1. pertence à árvore do candidato, está visível, habilitada, não minimizada/nem cloaked e possui client area válida;
2. responde a `SendMessageTimeout(WM_NULL)` em até 50 ms;
3. UI Automation encontra o controle primário “Abrir áudio” pela role e accessible name comuns;
4. o controle está habilitado, keyboard-focusable e com bounding box dentro da janela;
5. após `SetForegroundWindow` apenas como preparação best effort, UI Automation `SetFocus` funciona e `HasKeyboardFocus`/evento UIA confirma que o foco do SO chegou ao controle numa sessão desktop local desbloqueada.

O polling usa QPC monotônico a cada 10 ms. `window_actionable_ns` termina no último requisito satisfeito; existência do HWND, `DOMContentLoaded`, evento QML, log, IPC ou mensagem “ready” não bastam. O primeiro frame externamente capturável é registrado como diagnóstico. Timeout: 10 s e falha da tentativa.

Gate: P95 de `process-cold` ≤ 800 ms.

## 6. Árvore, CPU e memória

O harness cria o processo suspenso, o associa a um Job Object por tentativa com kill-on-close e então o libera. Captura ETW de Process Start/Stop começa antes do launch e reconcilia o job/árvore com snapshots periódicos; a filiação permanece registrada mesmo se o pai terminar. Cada runtime recebe perfil exclusivo com nonce da tentativa. Processo breakaway é incluído quando ETW comprova descendência no nascimento ou quando executable + argumento/profile contém o nonce. Se a atribuição externa não puder ser resolvida simetricamente, a tentativa é `harness_failure`, não uma estimativa parcial; helper atribuível inesperado continua contado e só falha o candidato se escapar do shutdown ou violar outro gate.

Cada amostra registra PID + instante de criação, pai, executable hash, CPU user/kernel, private working set, private bytes, working set total, handles, threads e I/O.

```text
tree_private_working_set = soma do Working Set Private dos PIDs vivos

tree_cpu_percent =
  100 * delta(soma user_time + kernel_time)
  / (wall_interval * logical_processor_count)
```

Assim, 100% significa a capacidade inteira da máquina, não um único core. Processo encerrado no intervalo contribui com o acumulado final. Working set não privado é diagnóstico sujeito a páginas compartilhadas/dupla contagem e nunca é a RAM primária.

### Janelas

- `t=0` é o instante `window_actionable` daquela tentativa.
- Shell ocioso: snapshots a cada 250 ms; por tentativa, `memory_5s` é a amostra mais próxima de 5 s (±125 ms), `memory_60s` é a mediana de 55–60 s e `memory_peak` é o máximo de 0–60 s.
- Pausado com fixture carregada: estabilizar 30 s e medir CPU por 60 s; cada tentativa produz uma média intervalar.
- Progresso fake ativo: estabilizar 10 s e medir por 60 s para detectar bindings/animação contínua.
- Produto tocando: estabilizar 30 s e medir por 60 s na combinação M0.10.

Gates:

- tipo 7 P95 dos valores `memory_60s` das tentativas < 80 MiB ocioso;
- tipo 7 P95 dos valores `memory_60s` das tentativas < 120 MiB durante reprodução comum;
- P95 das médias de CPU por tentativa < 0,5% pausado.

Private bytes, 5 s, pico, handles, threads, I/O e memória não privada são sempre publicados, embora não sejam todos gates absolutos.

## 7. Resposta visual e fila

Entrada real usa `SendInput`, não comando interno.

Para resposta visual:

1. focar o botão play/pause;
2. marcar QPC antes de `SendInput(Space)`;
3. capturar externamente a região do controle por DXGI Desktop Duplication;
4. encerrar no primeiro frame que exibe o estado alternado;
5. registrar evento UIA da propriedade como diagnóstico secundário.

Desktop Duplication captura no refresh nativo de 165 Hz. A ROI é o bounding rectangle UIA do controle expandido em 4 pixels e limitado ao cliente da janela. A referência é a mediana RGB de três frames estáveis antes do input. Mudança visível começa no primeiro frame em que ao menos 2% dos pixels têm diferença absoluta média ≥ 5/255 e persiste por dois frames; o estado UIA esperado precisa ser confirmado em até 2 s. M0.1 fixa golden images sintéticas para ROI, threshold e foco.

Cada uma das 22 tentativas usa um processo novo e exatamente uma ação, enviada após 500 ms de baseline visual estável. No shell M0.4, P50/P95/máximo são diagnósticos e não fecham NFR-PERF-003. Na combinação M0.10, o mesmo método vira gate do produto: P95 ≤ 100 ms.

Para a fila de 1.000 itens, o harness inicia a carga pela mesma ação e abre uma janela externa fixa de probes por 5 s. Cada probe envia navegação de foco identificável somente após o ack UIA/visual anterior, com cadência mínima de 50 ms, alternando dois controles essenciais. `stall_max_ms` é o maior atraso input → foco visível nessa janela. Qualquer atraso confirmado > 100 ms falha NFR-PERF-006. Contagem/ordem acessível de 1.000 IDs é um check de correção com timeout de 30 s, não o marcador que encerra a métrica; não se exige renderizar 1.000 nós simultaneamente.

ETW/PresentMon pode complementar a análise, mas frame contínuo não é exigido de UI estática.

Desktop Duplication é o único método primário deste protocolo e a mesma ROI/detector mede foco visível na fila. Se ele não funcionar no host, não se troca silenciosamente para BitBlt/PrintWindow: um protocolo/ambiente novo precisa ser congelado e os dois candidatos repetidos.

## 8. Estatística, budgets e empate

Quantis usam Hyndman–Fan tipo 7. Raw preserva inteiros em bytes/nanossegundos; conversão ocorre só no resumo. Para cada métrica curta publicar:

- `n`, failures e discards;
- mínimo, P50, P95 e máximo;
- média/desvio apenas como diagnóstico;
- bootstrap percentil 95% com 10.000 reamostragens independentes por candidato. Para cada índice, usar os 64 bits iniciais big-endian de SHA-256 UTF-8 de `M0-P1|bootstrap|20260823|<metric>|<candidate>|<resample>|<slot>`, módulo `n`; M0.1 fixa golden vectors.

`resample` e `slot` começam em 1; o índice resultante começa em 0. A diferença é sempre `P95_A - P95_B`, e os limites 2,5%/97,5% do bootstrap também usam Hyndman–Fan tipo 7. Inteiros e diferenças são calculados com aritmética decimal capaz de preservar exatamente valores de 64 bits acima de `2^53`.

CI não pode esconder violação do ponto P95. Teto absoluto violado é no-go. Em regressão contra baseline da mesma máquina: ≥5% alerta e exige uma segunda bateria completa; ≥10% nas duas baterias bloqueia; qualquer teto absoluto bloqueia diretamente.

Bandas de empate por métrica:

| Métrica | Banda |
|---|---|
| startup/open/seek | `max(0,05 * min(P95_A, P95_B), 20 ms)` |
| resposta visual | `max(0,05 * min(P95_A, P95_B), 1000/165 ms)` |
| memória | `max(0,05 * min(P95_A, P95_B), 2 MiB)` |
| CPU | `max(0,05 * min(P95_A, P95_B), 0,05 ponto percentual)` |

Uma diferença absoluta de P95 dentro da banda é empate. A fração `1000/165 ms` permanece decimal exata no resumo e não é arredondada para um número inteiro de nanossegundos. Fora da banda, só é vantagem confirmada quando o bootstrap independente de 95% de `P95_A - P95_B` exclui zero com sinal coerente com o ponto; caso contrário o resultado da métrica é inconclusivo. Resultados conflitantes não viram score: ambos primeiro precisam passar todos os gates; empate global segue UX comprovada, acessibilidade, manutenção pessoal, segurança, licenças e integração, na ordem do ADR-0001.

## 9. Acessibilidade, UX e plataforma

Builds próprios de cada SO, derivados do mesmo commit/configuração/fixture, executam:

- teclado completo e foco visível;
- NVDA no Windows 11;
- high contrast;
- escalas 100%, 150% e 200%;
- movimento reduzido;
- tray/close/restore;
- Ubuntu/Orca em Wayland e Xorg.

Performance primária usa 100%; outras escalas são gates funcionais. Cada checklist manual registra build, SO, toolkit/runtime, tecnologia assistiva e versão, executor, data, cenário, resultado e evidência. Controle P0 sem role/name, inacessível por teclado ou sem foco visível elimina o candidato independentemente de recursos.

M0.4 prova bundle/staging e tray básicos comparáveis. M0.9 aprofunda instalador, associação, upgrade/uninstall e integração nativa do finalista; a evidência não é contada duas vezes.

## 10. Áudio externo

O harness de áudio usa a mesma API conceitual e WASAPI loopback no endpoint congelado. Fixtures de timing possuem assinaturas acústicas próprias por codec, robustas a resampling e compressão lossy. Volume do app/sistema, shared format, enhancements e spatial audio ficam congelados; notificações e outras fontes são silenciadas.

Cada observação parte do processo de harness aberto e estabilizado, backend no estado público `unloaded`, mesmo endpoint selecionado e nenhuma source/stream ativa. Após unload confirmado, o preflight externo de 30 s precede o próximo comando. O batch usa 22 cópias hash-idênticas por codec, uma por tentativa, pré-geradas antes do primeiro preflight; nenhuma é previamente submetida ao backend naquele processo. O cache global do SO não é limpo artificialmente e a agenda balanceada controla ordem. Seek recarrega a fixture, toca 5 s na assinatura inicial e usa alvo fixo pelo menos 30 s adiante antes de marcar `t0`.

### Open-to-audible

- `t0`: QPC imediatamente antes de `load_and_play(fixture_id)` no boundary público do adapter;
- `t1`: primeiro frame de loopback cuja correlação normalizada com a assinatura cruza o limiar por duas janelas consecutivas;
- inclui abertura do dispositivo, decode, buffering e fila; não subtrai latência do endpoint.

### Seek-to-audible

- faixa estabilizada em assinatura distinta;
- `t0` antes de `seek(target)`;
- `t1` quando o loopback correlaciona com a assinatura exclusiva do destino;
- áudio antigo em buffer não encerra a medida.

Loopback é capturado em float 48 kHz. A detecção usa referência decodificada da própria fixture, janelas de 20 ms, hop de 5 ms, correlação normalizada ≥ 0,80 por duas janelas consecutivas, RMS ≥ 20 dB acima do baseline anterior e ≥ −50 dBFS. `t1` é o início estimado da primeira janela válida. M0.5 congela somente sinais/frequências e hashes das referências por codec; mudar método/limiar exige novo protocolo. A métrica chama-se `adapter_render_loopback_audible_proxy`, pois não mede o transdutor físico.

São 22 tentativas por fixture/cenário. MP3 VBR e M4A/AAC-LC são obrigatórios. Gates: open P95 ≤ 750 ms e seek P95 ≤ 250 ms; exceção por codec precisa ser aprovada antes da coleta.

M0.10 repete os dois gates end-to-end no produto combinado. `product_open_to_loopback` começa em input externo/abertura de arquivo; `product_seek_to_loopback` começa em `SendInput` sobre o controle visível. As métricas do adapter e do produto são publicadas separadamente e nunca misturadas no mesmo P95.

Underrun tem meta zero: qualquer incremento de contador do backend/ETW atribuível ao app falha, assim como descontinuidade acústica inesperada >20 ms em fixture contínua. Gaps menores são publicados. Device reset externo comprovado pode ser descarte; dropout sem causa externa não.

### Armazenamento lento e stress

O cenário lento usa token bucket test-only por source: relógio monotônico, bucket inicialmente vazio com capacidade/burst de 64 KiB, 50 ms fixos antes do primeiro byte, refill de 2 MiB/s e reads de no máximo 64 KiB. Seek não repete a latência inicial nem enche o bucket. A taxa efetiva sobre pelo menos 8 MiB precisa ficar em ±5% antes de usar o wrapper. Qt usa `QIODevice` e Rust `Read + Seek`; testes de contrato provam a mesma receita antes de medir os adapters.

A bateria de stress executa 500 operações determinísticas de load/seek, seed `20260823`, aguardando ack ou timeout tipado antes da próxima. Ela mede estado antigo, jobs, handles e retorno da memória após settle; não é misturada ao soak.

## 11. Soak de oito horas

Cada candidato viável executa 30 minutos de estabilização não analisados e depois 8 horas medidas, playlist sintética comum, offline, avanço automático, sessão desbloqueada e suspensão desligada. A cada 60 s, por 480 observações, registrar private bytes/private working set, CPU intervalar, handles, threads, árvore, faixa, underruns e os mesmos indicadores externos de CPU/disco/energia/runtime do preflight.

Para private bytes, usar regressão OLS `y_t = alpha + beta * horas_t`, com intercepto, e intervalo bilateral de 95% Newey–West/HAC: kernel Bartlett, lag 30, resíduos OLS, correção finita `n/(n-k)` com `k=2` e crítico Student `t(0,975; n-k)`. Passa somente se:

- slope ≤ 1 MiB/h;
- limite inferior ≤ 0;
- limite superior ≤ 1 MiB/h;
- `final_median_30min <= initial_median_30min + max(initial_median_30min * 0.10, 10 MiB)`.

Precedência: crash/deadlock/underrun/job órfão, slope pontual >1 MiB/h, limite inferior >0 ou falha da mediana são `fail`; se isso não ocorrer mas o limite superior >1 MiB/h, o resultado é `inconclusive`; somente todas as condições acima resultam em `pass`. Handles/threads falham quando o slope HAC tem limite inferior >0 e a mediana final excede a inicial em pelo menos uma unidade.

Crash, deadlock, job órfão ou underrun atribuível ao app falham imediatamente. Interrupção externa comprovada torna o soak inconclusivo e exige repetição completa; não é descarte de amostra curta. Loopback é processado em streaming; só eventos/trechos de falha e hashes são preservados, não oito horas de PCM.

## 12. Evidência

Raw fica em `out/m0/raw/<experiment_id>` e é append-only JSONL UTF-8/LF. Cada evento inclui:

```json
{
  "schema_version": 1,
  "protocol_sha256": "...",
  "environment_sha256": "...",
  "fixture_sha256": "...",
  "experiment_id": "...",
  "run_id": "...",
  "candidate_code": "A",
  "scenario_id": "process-cold",
  "attempt": 1,
  "event_seq": 17,
  "utc": "2026-08-23T12:00:00Z",
  "qpc_ticks": 123,
  "qpc_frequency": 10000000,
  "event": "resource_sample",
  "payload": {}
}
```

Metadados exigem commit/dirty flag, artifact SHA-256, build mode, toolchains/lockfiles, hashes do harness/protocolo/ambiente/fixture, seed/bloco/ordem, status `valid|discarded|failed|inconclusive`, motivo enum, markers, árvore/métricas, ruído externo, artefatos e checks omitidos.

Inteiros representam bytes e nanossegundos; valor ausente é `null` + `error_code`, nunca `NaN`. JSON/summary não recebem hostname, username, serial, endpoint em claro ou path absoluto. ETL/WPR pode conter paths/identidade: fica transitório em `out/m0` com ACL do usuário, nunca entra no Git e somente seu hash redigido aparece no resumo. Raw não é corrigido: nova análise cria summary que referencia/supersede a anterior.

O resumo commitado contém tentativas/válidas/descartadas/falhas, método, P50/P95/max/CI, budget, decisão `pass|fail|inconclusive`, resultado do soak quando aplicável e SHA-256 de todos os raws/traces. Um batch decisório exige ambos os candidatos e hashes idênticos somente dos inputs comuns: protocolo, ambiente, fixture, cenário e harness. Artifacts, toolchains específicos e lockfiles diferem por candidato e são registrados individualmente.

## 13. Timeouts

| Operação | Timeout |
|---|---:|
| janela acionável | 10 s |
| ação visual/foco individual | 2 s; o budget continua 100 ms |
| carga da fila fake | 30 s; stall continua 100 ms |
| load local de áudio | 10 s; budget continua 750 ms |
| seek | 5 s; budget continua 250 ms |
| shutdown de árvore | 10 s; depois registrar hang e encerrar de forma escopada |

Timeout é falha do candidato. Encerramento forçado resolve somente PIDs + creation times registrados naquela tentativa; nunca usa nome genérico de processo.

## 14. Aceite executável de M0.1

O harness só fecha M0.1 depois de TDD Red/Green para:

- agenda/hash/golden vector dos 22 blocos;
- quantil tipo 7, bootstrap de diferença de P95 e bandas;
- precedência `failed`/`discarded`/`inconclusive` e limite 20/22;
- rejeição de schema, hash, path, status, sequência ou evento inválido;
- fixture com exatamente 1.000 IDs, hashes e licenças;
- JSONL append-only, `event_seq`, tamper de hash e redação;
- ROI/detector visual em frames sintéticos;
- árvore/processo sintético e cleanup limitado a PID + creation time;
- summary golden byte-idêntico em duas execuções.

O piloto live prova shape, relógio, atribuição e cleanup, não igualdade de tempos nem desempenho de candidato.

# Protocolo comparativo M0

**Estado:** congelado para os contratos materializados até M0.1; piloto sintético local publicado, nenhuma coleta comparativa A/B executada

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

### 6.1 Contrato executável de identidade e cleanup materializado

No Windows, a identidade forte é `(pid, creation_time_filetime_utc)` obtida por `GetProcessTimes` no mesmo handle retido; PID, nome, path, hash, process group e sessão isolados são somente diagnóstico e nunca conferem ownership. Listas de PID do Job e notificações são hints: cada entrada precisa ser convertida imediatamente em handle, birth token e `IsProcessInJob` no mesmo handle antes de entrar no ledger. A API mutante aceita apenas a capability opaca registrada pelo módulo, vinculada a esse ledger imutável; PID, lista ou token reconstruído pelo caller falham antes de qualquer syscall.

O smoke Windows cria um Job vazio com `KILL_ON_JOB_CLOSE` e sem `BREAKAWAY_OK`/`SILENT_BREAKAWAY_OK`, cria o root suspenso, captura sua identidade, atribui-o ao Job e só então o libera. Root, filho, neto e qualquer membro auxiliar do Job têm handles retidos. O cleanup fecha uma única vez o último handle do Job e espera todos esses handles sinalizarem por até 10 s. Saída natural concorrente é sucesso idempotente. Se a revalidação de um membro divergir, o Job próprio ainda é fechado para não vazar processos, mas o cleanup é falso. No mapper P isso gera check `skipped` e run `inconclusive/incomplete_evidence`; a causa fechada exata (`snapshot_validation_failed`, `identity_revalidation_failed`, `job_close_failed` ou `unsafe_run_root_contents`) permanece tanto em `check_recorded.reason_code` quanto em `checks_omitted.reason_code`, enquanto `run.error_code` continua nulo como exige Evidence. A futura coleta A/B deve registrar uma causa raw compatível e classificá-la como `harness_failure`. Isso é distinto da futura terminação direta de breakaway: mismatch nesse handle não sinaliza o processo e falha o cleanup. O smoke live não implementa ETW nem cleanup de breakaway.

Os estados fechados são `complete|partial|unavailable`. Gap ETW mantém causa explícita; root ausente ou atribuição externa obrigatória irresolvida prevalece como `error_code` do resolver mesmo quando também existe gap. Para P, todo `partial|unavailable` com check `skipped` permanece `inconclusive/incomplete_evidence` e usa `run.error_code=null`, como exige a derivação semântica atual de Evidence; a causa detalhada permanece no artifact. A política `harness_failure` de atribuição irresolvida se aplica à futura coleta A/B, não ao mapper P não decisório. O runner atual retorna `partial/etw_live_not_implemented`: ele prova somente a ordem causal do Job, a topologia sintética root → child → grandchild, o fechamento kill-on-close e zero survivors. A suíte comportamental separada prova que `Remove-Module` normal e crash abrupto do owner não atingem um sentinel independente; o Close direto prova zero survivors em outro caso. Nenhum deles prova árvore completa de candidato nem desempenho.

No Linux, a identidade de contrato é `(pid,start_ticks,boot_id)` e `pidfd` é a capability mutante preferida. O parser de `/proc/<pid>/stat` usa o último `)` para preservar `comm` com espaços/parênteses; boot ID e start ticks divergentes falham fechados. Sem pidfd não há fallback por `kill(pid)`: a identidade serve apenas para diagnóstico. Como o adapter/cgroup v2 live ainda não foi provisionado, Linux permanece `not_proven` e nunca é apresentado como cleanup completo.

`New-M0ProcessTreeArtifact` reconstrói o snapshot em ordem fechada e produz bytes canônicos ligados a `experiment_id`, `run_id`, `candidate_code=P`, cenário `synthetic-process-tree`, tentativa 1 e role `synthetic_pilot`, sempre `decision_eligible=false`. O schema separa `complete`, `partial` e `unavailable`: complete não admite erro/capability ausente; partial exige causa e ao menos uma capability ausente e conserva somente a topologia observada com segurança; unavailable exige roles, nodes, auxiliares e contagens vazios, QPC nulo e nenhuma autoridade de cleanup. Cada causa também fecha seus sinais: `etw_live_not_implemented` exige trace não coletado e capability ETW ausente; `etw_trace_incomplete` exige trace parcial coletado; `root_identity_missing` exige topologia vazia e ausência de autoridade mutante; `unresolved_external_attribution` exige a capability de prova externa ausente; `safe_identity_unavailable` exige identidade segura e ETW ausentes, enquanto `etw_trace_unavailable` exige ETW ausente. O inventário de ausências fecha também a precedência causal inversa: `safe-process-identity` só pode faltar em `safe_identity_unavailable`; `root-handle` ausente sempre seleciona `root_identity_missing`; e `external-attribution-proof` ausente seleciona `root_identity_missing` quando o root também falta, ou `unresolved_external_attribution` quando o root está presente. Assim, identidades revalidadas nunca coexistem com uma alegação de identidade segura/root ausente. O vínculo ETW é bidirecional: trace complete exige capability presente e não listada como missing; trace partial/unavailable exige capability ausente e listada. `job-completion-port` listado como missing exige capability falsa e, no estado partial, capability falsa precisa aparecer como missing. Toda topologia vazia tem contagens/auxiliares zero e nenhuma autoridade de Job/cleanup. Uma falha anterior ao snapshot ainda gera exatamente um artifact unavailable canônico de topologia vazia, sem inventar observações. `Test-M0Process.ps1` valida o artifact parcial live somente em memória e o descarta; não grava raw, run, snapshot-set ou summary, não sela Evidence e não publica piloto. O summary comum puro e seu golden permanecem separados desse smoke e de qualquer publicação `P`.

`Invoke-M0Pilot.ps1` materializa no Windows a publicação local sintética `P`, sem candidato e sem áudio. Sob trust retido e um `EvidenceRoot` novo e vazio, a ordem causal é: abrir o writer; publicar o launch-intent `CreateNew`; anexar e fazer flush de `run_started`; abrir uma única sessão Process; capturar snapshot; fechar a capability e exigir cleanup canônico; somente então cunhar o único artifact `process-tree`; anexar `artifact_recorded` e `check_recorded`; selar `run_sealed`; publicar intent/raw/run/snapshot da Evidence; e publicar summary/sidecar por último. O artifact físico pós-`Close` é a única fonte de process, cleanup, identidades, check e run. O caminho nominal atual é `partial/etw_live_not_implemented`; se o snapshot falhar depois de `Open`, o cleanup real ocorre primeiro e exatamente um fallback `unavailable/pre_snapshot_fallback` é cunhado, com topologia vazia e causa `safe_identity_unavailable`. Os dois ramos exigem zero sobreviventes. `P` usa `synthetic_pilot`, `decision_eligible=false`, nenhuma agenda/candidate input e nunca participa de snapshot-set, batch ou summary comum A/B. Isso não comprova ETW live, breakaway, Linux, desempenho de candidato ou power loss.

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

O catálogo `markers.v1.json` torna essas janelas verificáveis pelo QPC. Em `idle-memory`, `idle-sampling-window:begin` → `end` dura exatamente 60 s e `memory-5s-sample` precisa ficar entre 4,875 s e 5,125 s. Uma tentativa válida exige exatamente uma amostra `memory_5s` ancorada nesse marker e `memory_60s`/`memory_peak` ancoradas no fim da janela, todas em bytes, inteiras não negativas e sem erro. Em `paused-cpu`, `paused-stabilization-window:begin` precede a coleta por pelo menos 30 s e `paused-sampling-window` dura exatamente 60 s; em `active-progress`, a estabilização equivalente é de pelo menos 10 s e a coleta também dura exatamente 60 s. Cada uma dessas duas tentativas válidas exige uma amostra `tree_cpu_percent` em `basis_points`, ancorada no fim da coleta, inteira não negativa e sem erro. A duração é sempre `floor((end_ticks-start_ticks)*1000000000/qpc_frequency)`, com inteiros sem passagem por `double`. Status não válidos podem omitir as métricas obrigatórias; qualquer uma que exista ainda precisa respeitar ID, unidade, unicidade, QPC e ordem causal. Métricas diagnósticas adicionais continuam permitidas.

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

Desktop Duplication captura no refresh nativo de 165 Hz. O contrato sintético M0.1 usa frames `BGRA8_UNORM` top-down, pixels da esquerda para a direita e `stride_bytes >= width*4`; alpha e bytes de padding no fim de cada linha são ignorados. A geometria vem do bounding rectangle UIA em pixels físicos de tela e intervalos half-open. Coordenadas racionais do controle recebem `floor` em left/top e `ceil` em right/bottom, depois expansão exata de 4 pixels e clamp ao cliente; retângulo original sem interseção ou resultado vazio é inválido.

A referência é a mediana independente dos canais B, G e R dos três últimos frames de mesma geometria anteriores ao input. Um pixel muda se, e somente se, `abs(delta_B)+abs(delta_G)+abs(delta_R) >= 15`, equivalência inteira exata da antiga expressão “diferença absoluta média ≥ 5/255”; nenhuma implementação usa `float`, alpha ou padding nessa decisão. Um frame qualifica por `changed_pixels*100 >= total_pixels*2`. A confirmação requer o primeiro par de frames qualificados consecutivos, ambos com `AccumulatedFrames=1` e ordinais adjacentes; frame acumulado, gap ordinal ou frame não qualificado quebra a persistência. `t1` é o timestamp do primeiro frame do primeiro par e `value_ns = floor((t1_ticks-input_ticks)*1000000000/qpc_frequency)` usa `BigInteger`. Todos os frames repetem a frequência raiz, têm QPC/ordinal únicos e estritamente crescentes e obedecem `baseline QPC < input QPC < observed QPC`.

O seed UIA é somente o estado `playing` da fixture: botão `play-pause`, role `button`, foco externo verdadeiro e `Name` resolvido ordinalmente de `action_pause` (`Pausar`) antes do Space para `action_play` (`Reproduzir`) em até 2 s inclusivos. `TogglePattern` nunca é consultado nem usado; o contrato não inventa um `state_id` formal posterior nesta subfatia. `visual.v1.json`, seu sidecar, schemas fechados e `goldens/visual.v1.json` ancoram esses vetores. `M0.Visual.psm1` executa apenas ROI, detector e validação UIA sintéticos: captura DXGI live e o buffer que ordenará observações antes do append pertencem à próxima subfatia de coleta. Esse coletor deverá ordenar QPC crescente e, em empate, `visual-change` antes de `uia-change`. Em M0.4 o resultado visual permanece `diagnostic_only`, e `budget_exceeded` é proibido.

Cada uma das 22 tentativas usa um processo novo e exatamente uma ação, enviada após 500 ms de baseline visual estável. No shell M0.4, P50/P95/máximo são diagnósticos e não fecham NFR-PERF-003. Na combinação M0.10, o mesmo método vira gate do produto: P95 ≤ 100 ms.

Para a fila de 1.000 itens, o harness inicia a carga pela mesma ação e abre a janela half-open `[t0,t0+5s)`. O primeiro probe começa exatamente em `t0`; cada próximo input identificável só é enviado depois do ack UIA/visual anterior e respeita cadência mínima de 50 ms, alternando dois controles essenciais. Uma execução válida contém de 1 a 100 pares completos, com `occurrence_id` único. Um input iniciado antes do fim pode receber ack depois de `t0+5s` e ainda fechar seu par, mas nenhum input novo é aceito em ou depois do limite. Se a run terminar com input pendente, `valid` é proibido: `failed` exige exatamente uma action terminal causal posterior ao input, `discarded` exige exatamente um `noise_observation` observado posterior e `inconclusive` exige exatamente um `check_recorded` skipped posterior. Uma causa anterior ao input não fecha a ocorrência. `stall_max_ms` é o máximo dos atrasos input → foco visível da tentativa; depois, o P95 compara as tentativas. Qualquer atraso confirmado > 100 ms exige `budget_exceeded` e falha NFR-PERF-006. Contagem/ordem acessível de 1.000 IDs é um check de correção com timeout de 30 s, não o marcador que encerra a métrica; não se exige renderizar 1.000 nós simultaneamente.

Markers `external_harness` podem ser `measurement_boundary` ou `diagnostic`; markers `candidate_reported` são sempre diagnósticos e nunca encerram medição. IDs e keys usam comparação ordinal sensível a maiúsculas, `occurrence_id` é nulo nos markers singulares e obrigatório/único por marker repetido; input/ack correlatos reutilizam o mesmo ID entre keys diferentes. O fingerprint semântico do catálogo carregado fecha IDs, phases, sources, roles, cardinalidades, dependências, measurements e policies dos 11 cenários. Cada `action_observation` resolvida referencia por `event_seq` dois boundaries externos da mesma run, com `start < end < action`, e repete o valor QPC calculado pela fórmula inteira acima. Os papéis de decisão são fechados: `attempt_limit` somente para stall da fila, `batch_p95` para `window_actionable` de `process-cold` e `diagnostic_only` para lançamentos comparativos e resposta visual do shell. Uma tentativa individual de `batch_p95` acima do budget permanece `observed`; resposta visual de M0.4 nunca vira falha de budget.

ETW/PresentMon pode complementar a análise, mas frame contínuo não é exigido de UI estática.

Desktop Duplication é o único método primário deste protocolo e a mesma ROI/detector mede foco visível na fila. Se ele não funcionar no host, não se troca silenciosamente para BitBlt/PrintWindow: um protocolo/ambiente novo precisa ser congelado e os dois candidatos repetidos.

## 8. Estatística, budgets e empate

Quantis usam Hyndman–Fan tipo 7. Raw preserva inteiros em bytes/nanossegundos; conversão ocorre só no resumo. Para cada métrica curta publicar:

- `n`, failures e discards;
- mínimo, P50, P95 e máximo;
- média/desvio apenas como diagnóstico;
- bootstrap percentil 95% com 10.000 reamostragens independentes por candidato. Para cada índice, usar os 64 bits iniciais big-endian de SHA-256 UTF-8 de `M0-P1|bootstrap|20260823|<metric>|<candidate>|<resample>|<slot>`, módulo `n`; M0.1 fixa golden vectors.

`resample` e `slot` começam em 1; o índice resultante começa em 0. A diferença é sempre `P95_A - P95_B`, e os limites 2,5%/97,5% do bootstrap também usam Hyndman–Fan tipo 7. Inteiros e diferenças são calculados com aritmética decimal capaz de preservar exatamente valores de 64 bits acima de `2^53`.

A média diagnóstica é a média aritmética decimal. O desvio diagnóstico é amostral (`n - 1`), fica `null` para `n < 2` e usa raiz quadrada decimal por Newton, 12 casas e arredondamento midpoint-to-even; não depende de `Double`, cultura ou ordem de propriedades.

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

Metadados exigem commit/dirty flag, artifact SHA-256, build mode, toolchains/lockfiles, hashes do harness/protocolo/ambiente/fixture, seed/bloco/ordem, status `valid|discarded|failed|inconclusive`, motivo enum, markers, actions, árvore/métricas, ruído externo, artefatos e checks omitidos. `run.markers` é a projeção deduplicada na ordem da primeira ocorrência, iniciada por `run-started`; `run.actions` é a projeção exata e ordenada de cada `action_observation` raw, sem reinterpretação posterior. A fila preserva uma action por par no run e o summary aplica `max` dentro da tentativa antes do P95 entre tentativas.

Inteiros representam bytes e nanossegundos; valor ausente é `null` + `error_code`, nunca `NaN`. JSON/summary não recebem hostname, username, serial, endpoint em claro ou path absoluto. ETL/WPR pode conter paths/identidade: fica transitório em `out/m0` com ACL do usuário, nunca entra no Git e somente seu hash redigido aparece no resumo. Raw não é corrigido: nova análise cria summary que referencia/supersede a anterior.

O fechamento de cada run é uma transação recuperável por crash de processo, escopada a um `EvidenceRoot` e a leafs derivados exclusivamente de `run_id`: `.jsonl`, `.intent.json`, `.run.pending`, `.run.json`, `.snapshot.pending` e `.snapshot.json`. Um lock sentinel estável em `.m0-evidence.lock` serializa writers e nunca é apagado. Somente `Open-M0EvidenceWriter` em um root realmente vazio pode criar o sentinel inicial; qualquer conteúdo sem sentinel e qualquer recovery sem o sentinel original falham fechados sem recriá-lo. `Open-M0EvidenceWriter` mantém esse lock e o handle do raw durante a sessão; `Add-M0EvidenceEvent -Writer`, `Complete-M0EvidenceRun -Writer` e `Close-M0EvidenceWriter` aceitam somente o token opaco registrado no módulo. Antes de anexar `run_sealed`, o writer valida o raw ainda aberto, markers/actions/status e a projeção completa do run; falha de preflight não publica intent, seal, run ou snapshot. O intent canônico e imutável congela tamanho/hash/head do raw e os bytes/hashes esperados de seal, run e snapshot. A ordem de publicação é: intent `CreateNew`; seal no mesmo handle de raw com flush durável do arquivo; run temporário `CreateNew`, flush e rename atômico sem overwrite; snapshot temporário `CreateNew`, flush e rename atômico sem overwrite por último. O snapshot final é o único commit point. Retry somente do evento que já é o head, com bytes canônicos idênticos, retorna a identidade anterior sem novo append; qualquer replay antigo ou divergente falha fechado.

Estados públicos de recovery são `unstarted`, `intent_only`, `raw_sealed`, `run_published`, `committed`, `conflict` e `corrupt`. Recovery só reproduz bytes já congelados no intent: tail parcial ou completo não journalado, precondition stale, temp divergente e destino final divergente falham fechados e nunca são truncados ou sobrescritos. Um `.run.json` sem snapshot final permanece `run_published`, é recuperável e invisível a digest/batch decisório. Snapshot `candidate_run` exige `decision_eligible=true`; o formato já reserva `synthetic_pilot` somente com `decision_eligible=false`. Readers decisórios leem raw/run uma vez sob handles sem share de escrita/delete e exigem snapshot+intent coerentes antes de retornar. No Windows, a identidade física pós-open é comprovada pelo handle (`volume serial`, `file id`, número de links e path final) antes da publicação; em outras plataformas o writer falha com `M0E026` até existir adapter `openat`/`fstat` equivalente validado. Esta garantia cobre crash de processo e atomicidade de rename no filesystem suportado; M0.1 não alega sobreviver a power loss nem executar `fsync` do diretório.

O piloto acrescenta um launch-intent separado da transaction intent da Evidence, um artifact `artifacts/<run_id>.process-tree.json` e um summary derivado `<run_id>.pilot-summary.json` com sidecar SHA-256. Crash antes da Evidence intent — inclusive depois do launch-intent, do primeiro raw ou do artifact pós-`Close` — nunca relança a árvore nem recupera no mesmo root: exige novo `EvidenceRoot` e novo `RunId`, preservando todos os bytes antigos. A partir de `intent_only`, `raw_sealed`, temporários journalados ou `run_published`, a retomada no mesmo run valida primeiro intent, raw e artifact sob handles retidos e só reproduz os bytes já congelados; jamais executa Process novamente. Summary final ausente pode ser derivado do commit Evidence; pending/final byte-idêntico pode ser concluído; divergência falha sem overwrite. O sidecar do summary é o commit point apenas desse derivado `P`; o snapshot continua sendo o commit point da Evidence run.

Um batch não recebe paths soltos de runs. `New-M0EvidenceSnapshotSet` congela, na ordem da agenda, exatamente 44 snapshots decisórios A/B em um documento canônico com `{run_id, snapshot_relative_path, snapshot_sha256}`; o sidecar SHA-256 publicado por último é o commit point do conjunto. O builder rejeita membro ausente, extra, duplicado, fora da agenda, divergente ou não commitado e também qualquer snapshot `candidate_run` elegível do mesmo experimento/cenário/inputs que tenha sido omitido. A identidade dos cinco inputs comuns é comparada semanticamente após validar chaves exatas, normalizar hashes para lowercase e reconstruir ordem fixa; caixa ou ordem JSON diferente não cria outro domínio. Evidência `synthetic_pilot` usa `candidate_code=P`, cenário `synthetic-process-tree`, tentativa 1, sem agenda/candidate input, e nunca é elegível nem listável no conjunto decisório. Digest e batch adquirem uma única vez o lock de leitura do `EvidenceRoot`, leem conjunto, batch, intents, snapshots, raws e runs uma vez em memória e rejeitam conjunto sem sidecar ou qualquer hash/identidade incoerente.

O contrato `M0.Summary` transforma um input puro e previamente validado em um resumo canônico. Por candidato, o input contém contagens planejadas/válidas/descartadas/falhas/inconclusivas, métricas por tentativa, inputs comuns próprios, hash do manifesto de runs e exatamente 22 receipts; no total, o inventário fecha A/B × tentativas 1–22 com `run_id`, status e hashes de raw, head final, run e snapshot. A fonte também liga snapshot-set, batch, schedule e manifests de runs. O builder normaliza os cinco hashes comuns para lowercase, exige igualdade semântica A/B e deriva uma única cópia no output; `P` é proibido.

O resumo comum publica método, mínimo/P50/P95/máximo, média/desvio diagnóstico, CI, banda de empate, budget, decisão curta e final, soak quando aplicável e inventário canônico de todos os raws/traces. Valores brutos por tentativa continuam inteiros; o resumo converte nanossegundos para milissegundos, bytes para MiB e basis points para pontos percentuais antes de estatística, budgets e comparação. Ausência legítima usa os ramos fechados `null + error_code`, nunca `NaN`. O `runs_manifest_sha256` reproduz exatamente `m0.runs-manifest.v1`, e o output recompõe tanto esses manifests quanto `m0.summary-evidence-inventory.v1` antes de ser aceito.

Nesta subfatia, `summary-input.v1.json` é uma fixture sintética previamente validada e `New-M0Summary` não abre Evidence, não cria snapshot-set/batch e não publica decisão A/B. O adapter futuro `Invoke-M0ShellComparison.ps1` deverá validar fisicamente os 44 snapshots/runs/raws sob lock antes de construir esse input. Um batch decisório continuará exigindo ambos os candidatos e hashes idênticos somente dos inputs comuns; artifacts, toolchains e lockfiles específicos permanecem por candidato.

## 13. Timeouts

| Operação | Timeout |
|---|---:|
| janela acionável | 10 s |
| ação visual/foco individual | 2 s; o budget continua 100 ms |
| carga da fila fake | 30 s; stall continua 100 ms |
| load local de áudio | 10 s; budget continua 750 ms |
| seek | 5 s; budget continua 250 ms |
| shutdown de árvore | 10 s; depois registrar hang e encerrar de forma escopada |

Timeout é falha do candidato. PID + birth token identificam o ledger, mas não concedem autoridade mutante: encerramento aceita somente a capability opaca registrada e handles retidos daquela tentativa. Nunca reabre/alarga o alvo a partir de PID, nome genérico, group ou sessão.

## 14. Aceite executável de M0.1

O harness só fecha M0.1 depois de TDD Red/Green para:

- agenda/hash/golden vector dos 22 blocos;
- quantil tipo 7, bootstrap de diferença de P95 e bandas;
- precedência `failed`/`discarded`/`inconclusive` e limite 20/22;
- rejeição de schema, hash, path, status, sequência ou evento inválido;
- fixture com exatamente 1.000 IDs, hashes e licenças;
- JSONL append-only, `event_seq`, tamper de hash e redação;
- ROI/detector visual e transição UIA em frames/observações sintéticos, incluindo alpha, padding, mediana, thresholds inteiros, gaps e QPC grande;
- árvore/processo sintético com identidade forte, capability opaca, Job escopado e cleanup idempotente por handles retidos;
- piloto `P` Windows com launch-intent e `run_started` anteriores ao live, `Close` anterior ao artifact, sequência literal de quatro eventos, fallback único pós-falha de snapshot, recovery sem relaunch e zero participação em A/B;
- summary comum golden byte-idêntico produzido por duas execuções independentes do builder puro, em processos `pwsh` limpos e roots distintos contendo cópias determinísticas da fixture.

Essa igualdade byte a byte não compara dois pilotos live: PID, UTC, QPC, IDs transacionais e receipts reais são necessariamente voláteis. O piloto local publicado prova o contrato sintético, a ordem causal, o membership do Job, cleanup kill-on-close, fallback pós-falha de snapshot e recovery de crash de processo; não prova ETW/breakaway/Linux, igualdade de tempos, durabilidade contra power loss ou desempenho de candidato. O summary comum golden continua uma transformação pura A/B separada. M0.1 foi concluído pelos gates registrados em [`m0-1.md`](evidence/m0-1.md) e pelo commit `24d12eac5ce53ac6b24d58d9c30581a009e10fda`; nenhuma coleta comparativa A/B foi executada.

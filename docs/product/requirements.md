# Requisitos do produto

**Estado:** baseline vigente; stack Windows aceita no ADR-0005

**Versão alvo:** MVP 0.1  
**Última revisão:** 2026-09-06

**Nome do produto:** RC Music, confirmado pelo mantenedor em 2026-09-05; [ADR-0004](../architecture/decisions/0004-rc-music-identity.md). A confirmação não altera os requisitos ou as prioridades abaixo.

## 1. Convenções

Cada requisito possui um identificador estável para testes, ADRs e roadmap.

| Campo | Significado |
|---|---|
| Confirmado | Pedido explícito do mantenedor |
| Derivado | Necessário para tornar o pedido completo e testável; pode ser ajustado |
| P0 | Bloqueador para a release MVP 0.1 |
| P1 | Planejado para completar/polir o 0.1, mas adiável com ajuste explícito de escopo |
| P2 | Evolução futura |

Uma extensão de arquivo não é sinônimo de codec. O suporte publicado deve ser expresso como combinação de contêiner, codec e capacidades observadas no corpus de teste.

## 2. Requisitos funcionais

### 2.1 Entrada e formatos

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-001 | P0 | Confirmado | Abrir um ou mais arquivos pelo seletor nativo. A escolha válida deve iniciar a faixa selecionada ou adicioná-la à fila conforme a ação indicada. |
| FR-002 | P0 | Confirmado | Reconhecer MP3 e M4A e buscar suporte a formatos locais comuns. O baseline candidato é MP3, MP4/M4A com AAC, FLAC, WAV e Ogg/Vorbis; Opus e ALAC dependem do spike de codecs. |
| FR-003 | P0 | Derivado | Aceitar arrastar e soltar arquivos e diretórios na janela. Diretórios são enumerados fora da thread da UI; itens inválidos são reportados sem cancelar os válidos. |
| FR-004 | P0 | Derivado | Aceitar paths pela linha de comando e pelo shell do sistema, inclusive Unicode, espaços, aspas, caminhos longos e múltiplos arquivos. |
| FR-005 | P0 | Derivado | Detectar formato pelo conteúdo quando possível. Extensão incorreta ou arquivo corrompido deve produzir diagnóstico seguro, nunca crash. |
| FR-006 | P1 | Derivado | Mostrar filtros no seletor coerentes com a matriz validada e oferecer “todos os arquivos” para diagnóstico. |

O [recorte de seleção principal assíncrona](../../experiments/m0/candidates/qt/README.md#async-audio-picker) implementa no candidato o fluxo de `FR-001` em que Abrir/Adicionar aguardam o usuário sem substituir o processamento normal de áudio, IPC e persistência. A conclusão usa a fila então vigente; aceitar o seletor não confirma alteração da fila nem decode. Os checks automatizados consolidados e riscos residuais são registrados centralmente, sem concluir M2/M3 ou a validação nativa Windows.

### 2.2 Reprodução

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-010 | P0 | Confirmado | Reproduzir e pausar mantendo posição e volume. |
| FR-011 | P0 | Derivado | Oferecer faixa anterior, próxima, volume e mudo. Ao terminar uma faixa, avançar para o próximo item reproduzível. |
| FR-012 | P0 | Confirmado | Exibir tempo decorrido, duração e barra de progresso clicável e arrastável para voltar ou avançar. |
| FR-013 | P0 | Confirmado | Oferecer botões de retrocesso e avanço de 5 s por padrão. Os dois valores são configuráveis e persistidos; faixa proposta de 1 a 300 s. |
| FR-014 | P0 | Derivado | Limitar seek e saltos ao intervalo `0..duração`. Seek em pausa mantém pausa; mídia não seekable desabilita o controle e explica o motivo. |
| FR-015 | P0 | Derivado | Durante o arraste, mostrar a posição pretendida sem a atualização do engine “puxar” o controle. Ao soltar, enviar um único comando de seek. |
| FR-016 | P0 | Derivado | Manter uma fila ordenada com adicionar, tocar agora, reordenar, remover e limpar. Itens duplicados são permitidos e possuem IDs próprios. |
| FR-017 | P1 | Derivado | “Anterior” reinicia a faixa quando a posição passou de 3 s; antes disso, navega para o item anterior. O limiar poderá virar preferência somente se houver necessidade real. |
| FR-018 | P2 | Derivado | Em evolução futura, permitir escolher um dispositivo de saída quando o backend e a plataforma oferecerem essa capacidade. Mudanças e desconexões devem ser recuperáveis. |
| FR-019 | P1 | Confirmado | Pedido de 2026-09-06: F1 liga/desliga modo aleatório das próximas ocorrências sem interromper a atual; F2 liga/desliga repetição da fila, movendo a ocorrência concluída ao fim sem ressuscitar itens removidos. IDs, duplicatas e metadados são preservados; ambos os modos são persistidos. Desligar aleatório conserva a ordem exibida. Velocidade e repetir somente uma faixa continuam evoluções futuras separadas. |

### 2.3 Metadados

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-020 | P0 | Confirmado | Exibir tamanho e duração do arquivo. |
| FR-021 | P0 | Derivado | Exibir, quando disponíveis, título, artista, álbum, número da faixa, ano e capa. Sem título, usar nome do arquivo. |
| FR-022 | P0 | Derivado | Disponibilizar detalhes: nome, caminho, modificação, contêiner, codec, bitrate, sample rate, canais e bit depth quando conhecido. |
| FR-023 | P0 | Derivado | Tags ausentes ou inválidas não podem impedir reprodução. Texto é sanitizado e arte corrompida ou acima dos limites NFR-LIMIT é descartada em favor do placeholder. |
| FR-024 | P0 | Derivado | Leitura de tags, arte e propriedades ocorre de forma assíncrona e cancelável. Resultado atrasado de uma faixa antiga não pode sobrescrever a atual. |
| FR-025 | P2 | Derivado | Editar tags e capa fica fora do MVP. |

O [recorte de sanitização de `FR-023`](../../experiments/m0/candidates/qt/README.md#metadata-sanitization) define uma política explícita para título/artista/álbum e faixa/ano: UTF-8 inválido é rejeitado integralmente; espaços/controles separadores formam uma única linha aparada; uma lista fechada de controles invisíveis de layout é removida, preservando scripts, acentos, ZWJ/ZWNJ e emoji. Texto vazio após essa transformação fica ausente e o título usa o fallback do arquivo. Os limites existentes são aplicados aos bytes anteriores à normalização, sem permitir que sanitizar contorne o limite. Faixa/ano exigem leitura decimal integral, sem sinal, lixo ou overflow; o registro central explicita os formatos e intervalos aceitos. Diagnósticos não fatais devem distinguir texto ajustado de valor inválido sem expor o conteúdo descartado. Número da faixa e ano de `FR-021` já existiam: o incremento valida seus valores, não introduz campos ausentes.

A policy pura, o extrator TagLib e a apresentação possuem Red/Green confirmado; core sem Qt, caracterização integrada real, suíte Release combinada, formato e composição de produção também passaram. Os checks automatizados desse recorte estão consolidados no registro central. O recorte não fecha `FR-023`, `CHK-SEC` ou o corpus hostil canônico, não demonstra um limite de alocações internas do parser TagLib e não constitui alegação de exploração ou vulnerabilidade comprovada.

### 2.4 Integração com o desktop

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-030 | P0 | Confirmado | Poder ocultar a janela na bandeja do Windows sem interromper áudio. O comportamento ao fechar é configurável entre minimizar para bandeja e encerrar. |
| FR-031 | P0 | Derivado | Menu da bandeja oferece mostrar/ocultar, play/pause, anterior, próxima e sair. “Sair” encerra o engine e o processo depois de salvar o estado. |
| FR-032 | P0 | Derivado | No Linux, usar integração compatível quando disponível. Se não houver bandeja, o app não pode continuar invisível; deve minimizar normalmente e explicar a limitação. |
| FR-033 | P0 | Derivado | Executar como instância única. Novas invocações encaminham os arquivos para a instância existente sem disputar o dispositivo de áudio. |
| FR-034 | P0 | Confirmado | Declarar os tipos suportados no instalador/pacote e permitir que o usuário escolha o app como padrão. |
| FR-035 | P0 | Derivado | No Windows, registrar ProgID e capabilities por usuário e abrir a tela oficial de Aplicativos Padrão; nunca alterar `UserChoice` à força. |
| FR-036 | P0 | Derivado | No Linux, fornecer arquivo `.desktop`, ícones e MIME types para os formatos validados, respeitando XDG. |
| FR-037 | P0 | Derivado | Suportar atalhos de teclado e controles de mídia do sistema. As ações provenientes de UI, bandeja, teclado e sistema usam os mesmos casos de uso. |
| FR-038 | P0 | Derivado | Suspensão, retomada, reinício do Explorer e troca/desconexão do dispositivo não devem deixar processo zumbi ou estado impossível. |

Refinamento confirmado de `FR-037` em 2026-09-06: na janela principal, `←`/`→` usam os saltos configurados, `↑`/`↓` aumentam/diminuem volume em um ponto percentual, `Shift+←`/`Shift+→` acionam anterior/próxima (`Ctrl` permanece alias), `M` alterna mudo e `Espaço` alterna reprodução sem ativar o último botão clicado. Esses comandos prevalecem sobre botões, sliders e lista; texto, menus e modais conservam interação própria. `F1`/`F2` acionam `FR-019` sem repetição por tecla mantida. Na fila, `Ctrl+↑`/`Ctrl+↓` selecionam linhas e `Enter` toca a selecionada. Os atalhos são locais, não registros globais do Windows.

### 2.5 Aparência, preferências e sessão

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-040 | P0 | Confirmado | Oferecer temas do sistema, claro e escuro; troca sem reinício e sem flash claro perceptível. |
| FR-041 | P2 | Confirmado | Edição de tema é desejável, mas não importante. Uma evolução poderá expor somente tokens seguros e preservar contraste. |
| FR-042 | P0 | Derivado | Persistir atomicamente tema, volume, mudo, saltos, comportamento da bandeja e demais preferências locais. |
| FR-043 | P0 | Derivado | Persistir fila, item atual e posição. Após reinício ou crash, restaurar a sessão pausada para nunca produzir áudio inesperado. |
| FR-044 | P0 | Derivado | Configuração inválida usa backup válido ou defaults e mostra aviso acionável. Migrações de esquema são versionadas. |
| FR-045 | P1 | Derivado | Restaurar tamanho/posição da janela apenas quando continuar visível em algum monitor conectado. |

### 2.6 Extensões, navegação e fontes remotas planejadas

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-050 | P2 | Confirmado | YouTube é uma extensão opcional separada da principal, sem ser requisito obrigatório ou dependência dela. Investigar download/conversão e eventual reprodução por URL somente se técnica, jurídica e operacionalmente viáveis. |
| FR-051 | P1 | Confirmado | Extensão principal opcional para Brave/Chromium detecta mídia, lista candidatos com seleção individual/múltipla e envia somente os selecionados para download/conversão no RC Music. Integra Lite e Completa por adaptador separado; falha, expiração ou indisponibilidade remota não afeta arquivos locais. |
| FR-052 | P2 | Derivado | Antes de implementar, registrar ADR sobre termos do serviço, licença, distribuição e atualização de qualquer ferramenta externa. |
| FR-053 | P2 | Derivado | Tratar cancelamento, timeout, URL expirada, live, conteúdo privado/restrito, headers necessários e ausência de seek. Nenhum comando externo pode ser montado por concatenação de shell. |

Refinamento confirmado de FR-050–053 em 2026-09-06, detalhado no [ADR-0006](../architecture/decisions/0006-browser-editions-and-extensions.md): **Lite** preserva o player atual e exclui a engine de navegação; **Completa** acrescenta navegação interna isolada. Ambas oferecem integração opcional com a extensão externa. A principal e a de YouTube têm pacotes, permissões e instalação independentes; suporte a YouTube não pode ser oferecido indiretamente pela principal. A integração é inicialmente desmarcada no instalador; ativação no navegador respeita seu fluxo próprio, sem políticas compulsórias. MP4 com áudio deve ser convertido em MP3 antes de ser importado no player; fonte sem áudio, DRM ou codec não suportado deve falhar de modo acionável. Download tem progresso/cancelamento e só publica arquivo local validado, sem sobrescrever arquivos pessoais. Não há suporte universal prometido, captura de microfone, histórico de navegação ou coleta implícita de cookies. O escopo está aceito, mas extensões, edições e downloader ainda não estão implementados; ferramentas, engine e distribuição precisam de decisão técnica/revisão próprias.

Incrementos de 2026-09-08: o [cliente MV3 e recepção C++](../browser-extension.md) implementam detecção temporária/lista/seleção e protocolo; a [coordenação de jobs](../download-jobs.md) trata admissão, progresso/cancelamento e importação via portas falsas nos testes. O [transporte opcional](../download-network.md) acrescenta policy DNS/redirects, HTTP/TLS limitado e sessão de transferência, com socket testado em loopback e ainda fora do player. Isso atualiza o estado de implementação do refinamento acima, não reduz seu escopo: executável de host, composição real do downloader, gate de TLS, conversão/importação, persistência dos jobs, descoberta ampliada, Brave real, Completa e instaladores continuam pendentes; FR-051–053 não estão concluídos.

Incremento de 2026-10-01: os [componentes comuns de rede diskless](../download-conversion.md#incremento-de-rede-diskless) implementam worker Windows real de DNS/socket, protocolo binário privado/limitado e proxy supervisionado, ainda opcionais e fora do player. Sucesso de DNS/`Finished` exige sequência válida, Terminal explícito e reconhecimento do processo encerrado com seus pipes concluídos; o job também aguarda parada reconhecida antes de conversão, limpeza ou terminal. A captura da falha tipada antes de cancelar preserva `Timeout`/`InvalidResult`, sem convertê-los em erro genérico de rede. O controlador conserva ownership dos arquivos; o processo de rede não recebe mídia no filesystem.

O fechamento diskless passou 55/55 CTest ON e compilou worker real OFF/zero testes. O [incremento assíncrono posterior](../download-conversion.md#incremento-de-execução-assíncrona) implementa `DownloadExecution` fora do player com tarefa/I/O/destruição em thread privada, mailbox limitado e shutdown/teardown reconhecidos; passou 41/41 CTest focados e OFF/zero testes. A composição MP3/MP4→serviço usa transferência/helper e importador falsos, com subprocessos, filesystem e conversor/decoder reais; não comprova internet/TLS completo. O [runtime pré-launch](../download-conversion.md#runtime-privado-autenticado-antes-do-launch) acrescenta capability de nove artefatos/entrada nativa, com testes de filho real/loopback e retenção até ACK. Sua [preparação na tarefa assíncrona](../download-conversion.md#incremento-de-preparação-na-tarefa-assíncrona) já acontece fora da UI, sem iniciar sessão depois de cancel/shutdown durante a cópia, ainda fora do app e sem perfil de distribuição aprovado. Lite permanece player sem engine; Completa terá navegação isolada, ambas com integração externa opcional. Importador, journal/persistência/recuperação, perfil revisado e composição no app/host/UI, sandbox, CLI corrigido e distribuição continuam pendentes. FR-051–053 não estão concluídos.

Incremento de importação local de 2026-10-01: [PlayerDownloadImport](../download-jobs.md#importação-local-de-resultado-publicado) implementa o handoff de `Published` confiável ao presenter/fila real, append-only sem seleção/autoplay ou I/O. Recibos limitados por ID/path não duplicam após remoção manual; recusa pré-commit devolve capacidade e permite retry sem nova execução. Testes ligam serviço/presenter reais a executor/áudio falsos, não provam bytes ou arquivo de download real. Composição no app/host/UI, journal/recuperação, ferramentas revisadas/sandbox, descoberta ampliada, Completa e instaladores continuam pendentes; FR-051–053 não estão concluídos.

Incremento de checkpoint de 2026-10-02: [histórico privado limitado](../download-jobs.md#checkpoint-privado-e-recuperação-conservadora) conserva jobs/recibos sem campos URL/query e restaura trabalho interrompido de forma inerte, sem re-download/importação automática. Paths recuperados não viram autoridade para importação ou limpeza e Completed histórico não comprova persistência da fila. Codec/store Windows são opcionais separados, não serviço já composto no player. ACK durável, journal de ownership/residuais, reconciliação fila+recibos, writer/lifecycle/UI, segurança/ferramentas, Brave, Completa e instaladores continuam pendentes; FR-051–053 não estão concluídos.

Incremento worker-only de ownership de 2026-10-02: [journal privado de arquivos](../download-ownership.md) adiciona gates antes de sinks/rename/retorno importável e assessment somente leitura. Falha pósrename preserva final como candidato sem importar; registros não contêm paths arbitrários/URLs e não autorizam ações. Ainda faltam reconciliação durável fila+recibos, recovery/capacidade acionáveis, resíduos do runtime e composição app/host/UI; não encerra FR-051–053, Lite/Completa ou opcionais de instalação.

Incremento de importação durável de 2026-10-02: o [modo Session do importador](../download-jobs.md#importação-local-de-resultado-publicado) conserva fila+recibo por job/path no mesmo `session.json` v2, com migração de v1 para recibos vazios e sem expulsão nos tetos de 128 recibos/1 MiB nativo. O serviço permanece `Importing` até Saved matching; falha exige retry explícito sem novo append/download, e backup/estado inválido/futuro bloqueiam novas importações duráveis. Bootstrap/autosave preservam o ledger mesmo com restauração e opcionais OFF, sem hidratar/tocar suas faixas; remoção manual não ressuscita importação. Paths recuperados não autorizam ações e a atomicidade não é exactly-once universal nem transação global entre documentos. São componentes sem serviço/host/UI de downloads instanciado no app; reconciliação/capacidade acionáveis, resíduos do runtime, ferramentas/sandbox, Brave, Completa e instaladores continuam pendentes. FR-051–053 permanecem abertos.

Incremento de reconciliação histórica de 2026-10-02: o [restore estrito](../download-jobs.md) reconhece ID exato de recibo de sessão saudável mesmo com checkpoint de admissão antigo Queued/parcial/sem path, tornando o job `Imported` sem executar/importar arquivos ou inventar contadores. Candidato distinto/estado de falha ou cancelamento conflitante bloqueia tudo sem rewrite; conclusão sem recibo vira Unconfirmed/Interrupted, e órfãos apenas são relatados/protegem namespace, sem fabricar jobs ou consumir quotas de downloads. Controller captura ledger limitado/detached de Loaded primário ou Missing exatamente vazio antes de Ready; backup/defaults/futuro/DTO inválido bloqueiam novas admissões, e retry de downloads não altera a saúde de sessão. Model mostra “Já importado na fila”, sem path/ações; paths privados nunca são capabilities. Reconciliação física/acionável, resíduos, composição dos serviços no app, Brave/Completa/opcionais/instaladores e gates de segurança/release continuam pendentes; FR-051–053 não estão concluídos.

## 3. Requisitos de UX e acessibilidade

| ID | Prioridade | Requisito |
|---|---:|---|
| UX-001 | P0 | A ação primária “Abrir áudio” é evidente no estado vazio e acessível em até uma interação. |
| UX-002 | P0 | Play/pause, progresso, saltos, anterior/próxima e volume permanecem visíveis durante reprodução. |
| UX-003 | P0 | A tela principal mostra somente informações essenciais; detalhes técnicos usam painel ou folha sob demanda. No Windows, abrir o aplicativo de produção não cria uma janela de terminal adicional. |
| UX-004 | P0 | Fluxo principal completo por teclado, com foco visível, ordem lógica e nomes acessíveis em botões de ícone. |
| UX-005 | P0 | Leitor de tela anuncia troca de faixa, erros e ações importantes, mas não cada atualização do cronômetro. |
| UX-006 | P0 | Contraste mínimo WCAG 2.2 AA, escala funcional de 100% a 200%, alto contraste do Windows e preferência de movimento reduzido. |
| UX-007 | P0 | Nenhum estado depende somente de cor. Alvos interativos possuem tamanho adequado e estados hover, pressed, focus e disabled. |
| UX-008 | P0 | Erros preservam contexto e oferecem próxima ação: tentar novamente, localizar arquivo, pular ou ver detalhes. |
| UX-009 | P1 | Troca de faixa e carregamento não causam saltos bruscos de layout; skeleton/placeholder mantém geometria estável. |
| UX-010 | P1 | O aplicativo usa strings centralizadas; pt-BR é o idioma inicial e a estrutura não impede tradução posterior. |

## 4. Requisitos não funcionais

### 4.1 Desempenho

Metas iniciais para Windows x64, SSD e máquina de referência a ser registrada no spike M0:

| ID | Meta inicial |
|---|---|
| NFR-PERF-001 | Janela interativa em até 800 ms no P95 em cold start. |
| NFR-PERF-002 | Arquivo local audível em até 750 ms no P95 com o app aberto. |
| NFR-PERF-003 | Ação visual reconhecida em até 100 ms; seek audível em até 250 ms ou limite documentado do codec. |
| NFR-PERF-004 | CPU média pausada abaixo de 0,5%; progresso não usa loop de atualização desnecessariamente rápido. |
| NFR-PERF-005 | Soma do private working set da árvore do app abaixo de 80 MiB ocioso e 120 MiB durante reprodução comum. Working set compartilhado e memória comprometida são registrados separadamente, sem dupla contagem como métrica primária. |
| NFR-PERF-006 | Adicionar 1.000 itens não bloqueia a UI por mais de 100 ms; processamento é incremental/cancelável. |
| NFR-PERF-007 | Oito horas de reprodução contínua sem crescimento sustentado de memória. |

As metas são orçamentos de engenharia, não alegações antes da medição. Se o spike provar que uma meta conflita com compatibilidade ou acessibilidade, a alteração exige ADR e números reproduzíveis.

### 4.2 Stack aceita e comprovação do produto

A decisão explícita do mantenedor no [ADR-0005](../architecture/decisions/0005-windows-product-stack.md) substitui a seleção por A/B dos ADRs anteriores. Os IDs abaixo permanecem estáveis, mas agora separam escolha técnica de comprovação de qualidade. Aceitar a stack não comprova desempenho, suporte Linux, corpus completo ou distribuição.

| ID | Requisito |
|---|---|
| NFR-STACK-001 | C++20 + Qt 6 Quick/QML é a stack definitiva do produto Windows, escolhida explicitamente pelo mantenedor no ADR-0005. A escolha não é uma alegação de superioridade de startup/memória sobre Tauri e não exige concluir o A/B. |
| NFR-STACK-002 | O produto tem uma única implementação e unidade de build/distribuição da stack aceita. Reutilizar os módulos/testes existentes sem duplicar regras; experimentos, fixtures comparativas e toolchains não escolhidas ficam fora das dependências permanentes do produto. Preservar seus registros históricos sem exigir reexecução para cada incremento. |
| NFR-STACK-003 | As medições posteriores do produto usam Release, protocolo/cenários/métricas/tolerâncias registrados antes da coleta, ambiente e commit identificáveis e toda a árvore de processos. Separar custo de shell e áudio e revalidar NFR-PERF-001–007 no app combinado; pacote/pré-requisitos são relatados separadamente da RAM. Os benchmarks estão adiados, não aprovados. |
| NFR-STACK-004 | O produto mantém os gates UX-004–007 e integração de bandeja Windows; falha de acessibilidade P0 bloqueia release. Linux conserva build/smoke e integrações próprios para etapa posterior, sem bloquear o desenvolvimento Windows ou ser anunciado como validado antecipadamente. |
| NFR-STACK-005 | A stack usa C++20, Qt 6 Quick/QML com estilo Basic customizado, Qt Multimedia/FFmpeg e TagLib. Domínio e aplicação permanecem C++ puro, sem `QObject`/QML; o motor implementa `PlaybackBackend`. Distribuir um único engine, com versões/configuração deliberadas e gates de segurança, Qt LGPL, FFmpeg/codec e TagLib antes de distribuição. |
| NFR-STACK-006 | O ADR-0005 aceita a escolha técnica e retira A/B/benchmarks como pré-requisito de entrada em M1. Budgets, corpus, segurança, acessibilidade e distribuição continuam pendentes até prova proporcional; mudar metas, dispensar P0 ou trocar stack exige nova decisão explícita, não pode ser inferido dessa aceitação. |

### 4.3 Confiabilidade e segurança

| ID | Requisito |
|---|---|
| NFR-REL-001 | Entradas inválidas do corpus e dentro dos limites publicados retornam erro controlado sem encerrar o processo. Todo crash reproduzível descoberto é bloqueador de release; isolamento absoluto contra falha/OOM de decoder exigiria processo separado e não é alegado no MVP. |
| NFR-REL-002 | Código próprio executado no callback de saída não realiza logging, lock longo, chamada de UI nem I/O adicional. O M0 mede underruns e verifica se o backend faz decode ou leitura bloqueante no caminho de tempo real; se houver impacto relevante, adicionar prefetch/ring buffer limitado ou rejeitar o backend. |
| NFR-REL-003 | Preferências e sessão são escritas de modo atômico e recuperável. |
| NFR-REL-004 | Paths são tratados como dados; nunca concatenados em comandos de shell. |
| NFR-REL-005 | Arquivos removidos, drives desconectados e dispositivos perdidos geram recuperação ou erro acionável. |
| NFR-REL-006 | Dependências e fixtures possuem licença conhecida; codecs e distribuição passam por revisão específica. |

Evidência parcial no candidato Qt/Windows: o [recorte de recuperação da saída padrão M4.7](../../experiments/m0/candidates/qt/README.md#audio-output-m47) distingue falha de dispositivo de falha de arquivo e recupera a mesma ocorrência pausada mediante ação explícita. A topologia dos testes é injetada; desconexão física, áudio audível e os demais cenários de `FR-038`/`NFR-REL-005` ainda exigem validação. Isso não implementa a escolha manual de dispositivo prevista para `FR-018`/M6.1 nem altera sua prioridade futura.

O [recorte de taskbar M4.7](../../experiments/m0/candidates/qt/README.md#taskbar-m47) acrescenta evidência parcial de `FR-038`: a janela ocultada pelo próprio app para a bandeja torna-se alcançável minimizada ao receber a notificação de recriação, sem alterar fila ou reprodução. O receptor é independente da janela visual, e saída pendente adia a recuperação. Os testes não reiniciam Explorer; presença do shell não comprova registro do ícone, e a caracterização nativa limita-se ao ícone do próprio processo de teste. Reinício real, pacote e gates nativos continuam necessários para fechamento.

O [recorte de localização M4.7](../../experiments/m0/candidates/qt/README.md#file-location-m47) implementa uma ação de `UX-008` para `NFR-REL-005`: selecionar outro path para a mesma ocorrência indisponível, sem perder ordem ou duplicatas. A ocorrência atual é recuperada pausada quando a nova origem puder ser carregada; uma ocorrência não atual permanece pendente de verificação por retry, sem interromper o áudio corrente. Seleção pelo diálogo não é confirmação de decode. Os checks automatizados verdes, incluindo recuperação de arquivo renomeado com decoder real, e os riscos residuais ficam no registro central; este recorte não encerra os requisitos nem valida diálogo nativo, drive físico, pacote, NVDA ou Linux.

Limites iniciais, revisáveis somente com corpus e medição:

| ID | Limite/resultado |
|---|---|
| NFR-LIMIT-001 | Arte embutida: até 10 MiB codificados, no máximo 4.096 px por eixo e 16 megapixels; gerar thumbnail de até 1.024 × 1.024. Acima disso, usar placeholder sem impedir playback. |
| NFR-LIMIT-002 | Metadata retida, sem arte: até 256 KiB no total e 16 KiB por campo usado pelo produto. Campos acima do limite são ignorados/truncados com diagnóstico. |
| NFR-LIMIT-003 | Probe de metadata tem budget interativo inicial de 2 s. Ao trocar de faixa ou estourar o budget, o resultado fica cancelado/descartado; o M0 deve provar que trabalho subjacente não acumula. |
| NFR-LIMIT-004 | Um comando IPC aceita no máximo 1.000 paths e 1 MiB serializado. Lotes maiores são divididos/rejeitados com mensagem, sem bloquear a UI. |

### 4.4 Privacidade e rede

| ID | Requisito |
|---|---|
| NFR-PRIV-001 | O MVP funciona integralmente offline e não coleta telemetria. |
| NFR-PRIV-002 | Histórico, fila e paths permanecem apenas no dispositivo. |
| NFR-PRIV-003 | Logs locais não incluem conteúdo de arquivos ou URL completa por padrão e possuem limite de retenção. |
| NFR-PRIV-004 | Qualquer rede futura é explícita, cancelável e limitada à ação solicitada. |

### 4.5 Manutenibilidade

| ID | Requisito |
|---|---|
| NFR-MAINT-001 | Mudanças de comportamento seguem Red–Green–Refactor. |
| NFR-MAINT-002 | Domínio e casos de uso não dependem do toolkit, sistema operacional ou decoder concreto. |
| NFR-MAINT-003 | O projeto começa como uma única unidade de build e distribuição, organizada internamente em módulos. Bibliotecas ou pacotes separados só são extraídos quando houver benefício demonstrável. |
| NFR-MAINT-004 | Todas as mudanças terminam em commit atômico após validação proporcional. |
| NFR-MAINT-005 | Requisitos, testes e ADRs usam identificadores estáveis e links bidirecionais quando aplicável. |

## 5. Atalhos padrão

| Ação | Atalho |
|---|---|
| Abrir arquivos | `Ctrl+O` |
| Play/pause | `Space`, inclusive com foco no último botão clicado |
| Retroceder/avançar | `←` / `→`, usando valores configurados |
| Faixa anterior/próxima | `Shift+←` / `Shift+→`, com aliases `Ctrl+←` / `Ctrl+→` |
| Aumentar/diminuir volume | `↑` / `↓`, um ponto percentual |
| Mudo | `M` |
| Ligar/desligar aleatório | `F1` |
| Ligar/desligar repetição da fila | `F2` |
| Selecionar linha da fila | `Ctrl+↑` / `Ctrl+↓` |
| Ativar controle ou tocar linha selecionada | `Enter` |
| Preferências | `Ctrl+,` |
| Mostrar/ocultar fila | `Ctrl+L` |
| Sair de verdade | `Ctrl+Q` |

Editores de texto, menus e painéis/diálogos ativos conservam suas teclas próprias; feche ou cancele o painel para voltar aos comandos da janela. Atalhos globais não fazem parte do MVP. Teclas de mídia usam a integração oficial do sistema quando disponíveis.

## 6. Critérios de aceitação do MVP

1. Cada combinação declarada na matriz de formatos abre, toca e termina usando fixtures do projeto; combinação não suportada gera erro e a fila continua.
2. Seek para 50% chega ao alvo em ±500 ms ou na granularidade documentada do codec.
3. O progresso visível difere do engine em no máximo 250 ms durante reprodução normal.
4. Alterar saltos para 7 s e 20 s atualiza botões, atalhos e estado restaurado após reinício.
5. Duplo clique em arquivo associado encaminha o path para a instância já aberta.
6. Fechar para a bandeja mantém áudio; “Sair” encerra completamente o processo.
7. Trocar tema não exige reinício, preserva contraste e não produz flash claro perceptível.
8. Crash ou término forçado recupera fila e posição, mas reinicia pausado.
9. Remover arquivo, suspender o PC ou perder o dispositivo resulta em recuperação ou erro acionável sem travar.
10. As jornadas principais funcionam com teclado e leitor de tela.
11. Gates de qualidade e orçamentos publicados passam na máquina de referência, ou o risco residual é explicitamente aceito.

## 7. Matriz de formatos a validar no M0

| Contêiner/extensão | Codec | Play | Duração | Seek | Tags | Capa | Estado |
|---|---|---:|---:|---:|---:|---:|---|
| MP3 `.mp3` | MPEG Layer III CBR/VBR | — | — | — | — | — | Pendente |
| ISO MP4 `.m4a` | AAC-LC | — | — | — | — | — | Pendente |
| FLAC `.flac` | FLAC | — | — | — | — | — | Pendente |
| RIFF `.wav` | PCM | — | — | — | n/a | n/a | Pendente |
| Ogg `.ogg` | Vorbis | — | — | — | — | — | Pendente |
| Ogg/WebM `.opus` | Opus | — | — | — | — | — | Candidato |
| ISO MP4 `.m4a` | ALAC | — | — | — | — | — | Candidato |

“Pendente” nesta tabela significa aprovação ampla do suporte ainda aberta. Há uma [declaração experimental Windows local](../../experiments/m0/candidates/qt/windows-installer/README.md#matriz-local-e-evolucao-dos-formatos) dos perfis básicos, incluindo aliases `.mp4`/`.oga`, vinculada ao runtime específico e a 18/18 testes de load/duração/seek/metadados/EOF sobre fixtures curtas e saída mutada. Ela pode ser selecionada explicitamente ao compilar o instalador privado; não publica suporte irrestrito aos contêineres, não classifica Opus/ALAC nem encerra capa, áudio audível, segurança ou release.

## 8. Rastreabilidade resumida

O [manifesto executável](../quality/traceability.json) mantém uma entrada por ID com destino, testes, evidências e lacunas; o [guia CHK-TRACE](../quality/traceability.md) descreve validação e consulta bidirecional. Estados são revisados por requisito completo, sem confundir implementação funcional com aprovação nativa ou de release.

| Pedido original | Requisitos |
|---|---|
| Windows e Linux, com prioridade para Windows | FR-030–038, plataforma e NFRs |
| MP3, M4A e outros | FR-001–006 e matriz de formatos |
| YouTube por URL no futuro | FR-050–053 |
| UI/UX moderna, leve e minimalista | UX-001–010, NFR-PERF-001–007, NFR-STACK-001–006 |
| Bandeja | FR-030–032 |
| Metadados, tamanho e duração | FR-020–024 |
| App padrão | FR-033–036 |
| Progresso arrastável | FR-012, FR-014–015 |
| Saltos ±5 configuráveis | FR-013 |
| Temas claro/escuro e possível edição | FR-040–041 |

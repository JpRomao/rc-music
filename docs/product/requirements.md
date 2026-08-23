# Requisitos do produto

**Estado:** baseline para revisão  
**Versão alvo:** MVP 0.1  
**Última revisão:** 2026-08-23

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
| FR-019 | P2 | Derivado | Shuffle, repeat e velocidade de reprodução são evoluções separadas; não fazem parte do núcleo inicial. |

### 2.3 Metadados

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-020 | P0 | Confirmado | Exibir tamanho e duração do arquivo. |
| FR-021 | P0 | Derivado | Exibir, quando disponíveis, título, artista, álbum, número da faixa, ano e capa. Sem título, usar nome do arquivo. |
| FR-022 | P0 | Derivado | Disponibilizar detalhes: nome, caminho, modificação, contêiner, codec, bitrate, sample rate, canais e bit depth quando conhecido. |
| FR-023 | P0 | Derivado | Tags ausentes ou inválidas não podem impedir reprodução. Texto é sanitizado e arte corrompida ou acima dos limites NFR-LIMIT é descartada em favor do placeholder. |
| FR-024 | P0 | Derivado | Leitura de tags, arte e propriedades ocorre de forma assíncrona e cancelável. Resultado atrasado de uma faixa antiga não pode sobrescrever a atual. |
| FR-025 | P2 | Derivado | Editar tags e capa fica fora do MVP. |

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

### 2.5 Aparência, preferências e sessão

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-040 | P0 | Confirmado | Oferecer temas do sistema, claro e escuro; troca sem reinício e sem flash claro perceptível. |
| FR-041 | P2 | Confirmado | Edição de tema é desejável, mas não importante. Uma evolução poderá expor somente tokens seguros e preservar contraste. |
| FR-042 | P0 | Derivado | Persistir atomicamente tema, volume, mudo, saltos, comportamento da bandeja e demais preferências locais. |
| FR-043 | P0 | Derivado | Persistir fila, item atual e posição. Após reinício ou crash, restaurar a sessão pausada para nunca produzir áudio inesperado. |
| FR-044 | P0 | Derivado | Configuração inválida usa backup válido ou defaults e mostra aviso acionável. Migrações de esquema são versionadas. |
| FR-045 | P1 | Derivado | Restaurar tamanho/posição da janela apenas quando continuar visível em algum monitor conectado. |

### 2.6 Fontes remotas futuras

| ID | Prioridade | Origem | Requisito e critério principal |
|---|---:|---|---|
| FR-050 | P2 | Confirmado | Investigar reprodução por URL de vídeo do YouTube, se técnica, jurídica e operacionalmente viável. |
| FR-051 | P2 | Derivado | Fontes remotas são resolvidas por adaptador separado. Falha, expiração ou indisponibilidade remota não afeta arquivos locais. |
| FR-052 | P2 | Derivado | Antes de implementar, registrar ADR sobre termos do serviço, licença, distribuição e atualização de qualquer ferramenta externa. |
| FR-053 | P2 | Derivado | Tratar cancelamento, timeout, URL expirada, live, conteúdo privado/restrito, headers necessários e ausência de seek. Nenhum comando externo pode ser montado por concatenação de shell. |

## 3. Requisitos de UX e acessibilidade

| ID | Prioridade | Requisito |
|---|---:|---|
| UX-001 | P0 | A ação primária “Abrir áudio” é evidente no estado vazio e acessível em até uma interação. |
| UX-002 | P0 | Play/pause, progresso, saltos, anterior/próxima e volume permanecem visíveis durante reprodução. |
| UX-003 | P0 | A tela principal mostra somente informações essenciais; detalhes técnicos usam painel ou folha sob demanda. |
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

### 4.2 Seleção de stack e comprovação no M0

A direção abaixo foi confirmada pelo mantenedor para orientar o experimento, mas não aceita antecipadamente uma stack. Medições reproduzíveis, UX e acessibilidade prevalecem sobre pressupostos baseados somente na linguagem.

| ID | Requisito |
|---|---|
| NFR-STACK-001 | O M0 trata C++20 + Qt 6 Quick/QML como hipótese principal para menor cold start e memória no Windows, com Tauri 2 + Svelte como desafiante A/B direto. Qt não é aceito apenas por ser C++, e Tauri não é rejeitado apenas por usar WebView. |
| NFR-STACK-002 | Os shells são comparados em build Release com o mesmo recorte de UX, assets, dados, roteiro e baseline de máquina. O ensaio de shell não usa áudio ou rede reais; os backends são comparados separadamente no mesmo corpus para não atribuir à linguagem custos do decoder ou da saída. |
| NFR-STACK-003 | O protocolo, cenários, métricas e tolerâncias são registrados antes da coleta e medem o candidato completo, incluindo processos auxiliares. O ensaio de shell cobre NFR-PERF-001/004–006; o ensaio de áudio cobre NFR-PERF-002/003/007; a combinação final revalida NFR-PERF-001–007. Tamanho do pacote e pré-requisitos offline são relatados separadamente da RAM. |
| NFR-STACK-004 | Os dois candidatos passam pelos mesmos gates UX-004–007, integração de bandeja e build/smoke Linux de referência. Falha de acessibilidade P0 bloqueia o candidato mesmo que ele vença em recursos. |
| NFR-STACK-005 | O candidato Qt usa como baseline C++20, Qt 6 Quick/QML com estilo Basic customizado, Qt Multimedia/FFmpeg e TagLib. Domínio e aplicação permanecem C++ puro, sem dependência de `QObject`/QML; o motor implementa a porta `PlaybackBackend`, e o pacote passa pelos gates de Qt LGPL e configuração/licença do FFmpeg. O desafiante usa o baseline Tauri/Rust documentado nos ADRs propostos. |
| NFR-STACK-006 | Somente candidatos que atendam aos budgets vigentes de UI e áudio, ou a desvios explicitamente aprovados por ADR, podem ser aceitos. Resultado dentro das tolerâncias pré-registradas é empate; nesse caso, decidir por UX comprovada, acessibilidade, manutenção para uma pessoa, segurança e licenciamento. A escolha final exige ADR explícito com números e ambiente reproduzível. |

### 4.3 Confiabilidade e segurança

| ID | Requisito |
|---|---|
| NFR-REL-001 | Entradas inválidas do corpus e dentro dos limites publicados retornam erro controlado sem encerrar o processo. Todo crash reproduzível descoberto é bloqueador de release; isolamento absoluto contra falha/OOM de decoder exigiria processo separado e não é alegado no MVP. |
| NFR-REL-002 | Código próprio executado no callback de saída não realiza logging, lock longo, chamada de UI nem I/O adicional. O M0 mede underruns e verifica se o backend faz decode ou leitura bloqueante no caminho de tempo real; se houver impacto relevante, adicionar prefetch/ring buffer limitado ou rejeitar o backend. |
| NFR-REL-003 | Preferências e sessão são escritas de modo atômico e recuperável. |
| NFR-REL-004 | Paths são tratados como dados; nunca concatenados em comandos de shell. |
| NFR-REL-005 | Arquivos removidos, drives desconectados e dispositivos perdidos geram recuperação ou erro acionável. |
| NFR-REL-006 | Dependências e fixtures possuem licença conhecida; codecs e distribuição passam por revisão específica. |

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

## 5. Atalhos padrão propostos

| Ação | Atalho |
|---|---|
| Abrir arquivos | `Ctrl+O` |
| Play/pause | `Space` quando o foco não estiver em editor/controle incompatível |
| Retroceder/avançar | `←` / `→`, usando valores configurados |
| Faixa anterior/próxima | `Ctrl+←` / `Ctrl+→` |
| Volume | `↑` / `↓` |
| Mudo | `M` |
| Preferências | `Ctrl+,` |
| Mostrar/ocultar fila | `Ctrl+L` |
| Sair de verdade | `Ctrl+Q` |

Atalhos globais não fazem parte do MVP. Teclas de mídia usam a integração oficial do sistema quando disponíveis.

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

“Pendente” significa que a documentação deseja esse suporte, não que ele já foi comprovado.

## 8. Rastreabilidade resumida

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

# Estado e decisões do projeto

**Atualizado em:** 2026-08-31

**Fase:** M0.6 em andamento, com empacotamento/A-B/automação nativa/benchmarks pesados suspensos; lifecycle/eventos tipados e geração isolada estão verdes, assim como `PB-001`/`PB-006` para WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis e `PB-004` para MP3 VBR/M4A AAC via Qt Multimedia; `MR-001`, `MR-003`, `MR-018`, `MR-019` e `MR-101`–`MR-103` no TagLib cobrem a matriz básica por conteúdo, enquanto polling/`MR-016`/`MR-017`/`MR-020` permanecem verdes no reader assíncrono limitado. Uma caracterização separada do extrator retorna `UnsupportedFormat` para Opus/ALAC, mas playback inicia independentemente e o Qt pode decodificar candidatos, logo não há rejeição ponta a ponta nem classificação de suporte. A composição UI oferece `.mp3`, `.m4a`, `.mp4`, `.flac`, `.ogg`, `.oga` e `.wav` em variantes minúsculas/maiúsculas e percorre nove linhas válidas sobre sete fixtures autenticadas com fallback Unicode, autoplay, play/pause, seek e metadata assíncrona com adapters reais e saída mutada. O erro local agora conserva título/contexto Unicode e oferece reabrir; regressões de metadata/playback/integração usam prefixos temporários de 64 bytes derivados de MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis para cobrir erro tipado, M4A sem faixa de áudio, recuperação e descarte de geração antiga. Esses derivados não são fixtures canônicas; M0.5/M0.6, áudio audível, comportamento ponta a ponta dos candidatos, hostis canônicos restantes, arte/limites, gates nativos e decisão de stack permanecem pendentes

No modo real, a fila local também coordena eventos terminais por geração e ID: `Ended` avança sem circular com autoplay; no tail, as duas opções mantêm ou limpam a seleção e sua escolha agora é persistida pelo recorte M3.5. Erros de origem marcam somente a ocorrência corrente e tentam continuar preservando reprodução/pausa; `Busy` mantém recuperação manual e snapshots terminais repetidos/antigos não alteram a intenção nem a nova faixa. Uma caracterização Qt Multimedia real comprova válida A → `InvalidMedia` → válida B e o segundo `Ended` pausado no tail, sem wrap, quarta carga ou reset do modelo. Os demais gates ainda impedem concluir FR-011/FR-016/M3.3.

A entrada por drag-and-drop de arquivos e diretórios locais está funcional no candidato Qt/Windows. A descoberta recursiva usa worker único, batches limitados, mailbox com backpressure, cancelamento por geração e profundidade máxima; resultados são inseridos incrementalmente por âncoras de ocorrência. A UI cobre posição antes/depois/fim sobre a fila, append fora dela, lote misto, erro parcial, progresso, cancelamento, foco e anúncios acessíveis. Handles Windows são abertos sem seguir reparse points, a identidade do diretório é revalidada e a interrupção de I/O é repetida até o reconhecimento do cancelamento ou a saída confirmada no teardown. M3.2 permanece parcial porque não houve `CHK-PERF` de 1.000 itens/stall de 100 ms, volume lento com deadline, validação nativa de symlink/NVDA nem E2E empacotado.

M3.6 está funcional somente no candidato Qt/Windows. Strings essenciais são centralizadas em catálogos QML `*Text.qml` e no catálogo C++ do filtro nativo; o pt-BR é gerado por `qt_add_translations` e instalado em runtime por `QTranslator`, com plural correto para “1 segundo”. Erros locais possuem causas tipadas, texto seguro e ação de recuperação. Uma seleção não vazia rejeitada sincronamente preserva fila/contexto e oferece nova escolha; importações parciais/rejeitadas também são acionáveis. A fila acessível omite IDs de ocorrência e códigos técnicos, e texto longo permanece contido em 720 × 520 nos testes automatizados. Isso não aceita a stack nem fecha M3: NVDA, DPI, alto contraste e movimento reduzido nativos, pacote/E2E e os demais gates indicados continuam abertos. M3.7 é o próximo recorte; Linux, A/B e benchmarks seguem suspensos até a paridade funcional Windows.

**Código do produto:** ainda não iniciado

## 1. O que está confirmado

Os itens de produto/processo abaixo vieram diretamente do mantenedor:

- player desktop pessoal, leve e rápido;
- Windows é mais importante que Linux, mas ambos devem ser contemplados;
- Rust ou C++ são candidatos de implementação;
- UI/UX moderna, leve e minimalista é a prioridade mais alta;
- MP3, M4A e outros formatos comuns devem ser investigados/suportados;
- bandeja no Windows;
- metadados incluindo tamanho e duração;
- declaração/fluxo para escolha como aplicativo padrão;
- barra de progresso arrastável e seek;
- saltos padrão de −5 s/+5 s configuráveis;
- temas claro e escuro;
- edição de tema é desejável, mas não prioritária;
- áudio de YouTube por URL é possibilidade futura, se viável;
- desenvolvimento seguirá TDD;
- `agents.md` deveria ser o primeiro arquivo;
- mudanças devem ser sempre commitadas;
- o M0 usará C++20 + Qt 6 Quick/QML como hipótese principal de desempenho e Rust + Tauri 2 + Svelte como desafiante A/B direto;
- shell/UI e backend de áudio serão comparados separadamente antes da escolha da stack.
- o baseline derivado, fila/sessão P0 e o experimento M0.0–M0.10 foram autorizados em 2026-08-23.

Os contratos experimentais derivados sob a autorização do M0 são:

- Windows 11 x64 é Tier 1; Ubuntu 24.04 LTS amd64/GNOME é Tier 2; Windows 10 é somente legado/best effort;
- ambiente `M0-E1`, protocolo `M0-P1` e layout `M0-L1` ficam congelados antes da coleta.
- M0.1 foi concluído em `24d12ea` com fixture, harness, summary comum e piloto sintético `P` não decisório; nenhuma coleta A/B foi executada.

Esses contratos confirmam a direção do experimento, não a aceitação de uma stack.

## 2. O que é recomendado, mas ainda proposto

| Decisão | Recomendação | Estado |
|---|---|---|
| Hipótese principal de shell/UI | C++20 + Qt 6 Quick/QML, com domínio e aplicação em C++ puro | Baseline experimental; ADR-0001 comparativo ainda proposto |
| Desafiante direto de shell/UI | Rust + Tauri 2 + Svelte/TypeScript/Vite | Baseline experimental; ADR-0001 comparativo ainda proposto |
| Áudio no candidato Qt | Qt Multimedia/FFmpeg atrás de `PlaybackBackend` | Ramo do ADR-0002 comparativo; não aceito |
| Áudio no candidato Tauri | Rodio/CPAL + cadeia Symphonia compatível e testada, sem versão direta duplicada | Ramo do ADR-0002 comparativo; não aceito |
| Metadata | TagLib no baseline Qt; Lofty + filesystem no baseline Tauri | Candidatos do M0; não aceitos |
| Estrutura | uma unidade de build e distribuição com módulos internos; extrações só por necessidade demonstrada | Proposto |
| Persistência | JSON versionado e atômico; sem banco no MVP | Proposto |
| Integração | portas comuns com adapters do toolkit escolhido, SMTC Windows, MPRIS Linux e associações nativas | Proposto |
| Windows package | NSIS como baseline experimental por usuário para provar Default Apps; tecnologia final definida no M0 | Proposto |
| Linux package | `.deb` primeiro, AppImage depois | Proposto |
| Remoto | porta isolada, desativada no MVP | Proposto em ADR-0003 |

Alternativas preservadas:

- Rust + Slint somente se a evidência justificar um experimento adicional de footprint;
- GStreamer/libmpv apenas como substituição de engine quando requisitos futuros justificarem.

## 3. Baseline derivado aprovado para execução

Estes comportamentos não foram todos pedidos literalmente, mas foram autorizados em D0.9 como baseline. Alteração posterior exige atualização explícita dos requisitos/plano:

- fila básica;
- volume/mudo e anterior/próxima;
- CLI e instância única;
- atalhos/SMTC/MPRIS;
- restauração pausada de sessão;
- preferências atômicas e versionadas;
- contraste, teclado, leitor de tela e reduced motion;
- valores de salto independentes entre 1 e 300 s;
- “anterior” reinicia após 3 s;
- pt-BR inicial com strings preparadas para tradução;
- metas de startup, memória, CPU e latência;
- limites iniciais de arte/metadata/IPC, a validar no M0;
- NSIS como baseline experimental Windows por usuário e `.deb` como primeiro pacote Linux; a tecnologia Windows final será escolhida no M0.

Os detalhes executáveis permanecem nos [requisitos](product/requirements.md), na [UX](product/ux-specification.md) e no inventário de preferências do [registro M0](m0/README.md).

## 4. Decisões em aberto

1. escolher shell/UI e backend de áudio somente após as comparações separadas e a decisão explícita por ADR;
2. nome e identidade visual do aplicativo;
3. licença para eventual distribuição pública;
4. exigir versão portátil no 0.1 ou depois;
5. aprovar/recalibrar budgets após medições;
6. decidir no futuro se YouTube será pesquisado, oferecido via player oficial ou rejeitado.

Nenhuma dessas perguntas bloqueia a documentação. ADR-0001/0002 precisam de decisão ou evidência M0 antes do bootstrap definitivo.

## 5. Alegações que ainda não podem ser feitas

- “o app já oferece suporte publicado a WAV/MP3/M4A/FLAC/Ogg/Opus”: o candidato experimental percorre a matriz básica WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis com Qt Multimedia e integra metadata/título Unicode à apresentação, mas os testes mantêm volume zero/mudo; não há prova audível/dispositivo físico nem formato aprovado ou matriz publicada. Opus/ALAC continuam candidatos: o extrator isolado retorna `UnsupportedFormat`, porém playback pode iniciar e o Qt pode decodificá-los, de modo que o comportamento integrado não foi classificado;
- “é leve”: os budgets são metas, não medidas;
- “funciona em Linux”: arquitetura e plano existem, build não;
- “pode ser app padrão”: o fluxo está especificado, instalador não existe;
- “as preferências já restauram a sessão”: o armazenamento M3.5 persiste as dez preferências no candidato Qt/Windows, mas restauração de fila/item/posição e início minimizado efetivo continuam em M4.1/M4.4; FR-042/FR-043/FR-044 não estão integralmente concluídos;
- “alto contraste está resolvido”: existe implementação e evidência automatizada com paletas injetadas, mas isso não comprova Windows real, ramos internos do Qt Basic, Linux, NVDA ou DPI nativo nem fecha UX-006;
- “a bandeja está validada no Windows”: menu, avisos, fallback e saída passaram em testes offscreen, mas isso não comprova ícone/Explorer reais, ativação nativa, NVDA ou Linux;
- “o pacote está validado em máquina sem SDK ou pronto para distribuir”: inventário, imports PE e smokes offscreen original/relocado passaram sem paths Qt do SDK no ambiente filho, mas isso não remove o SDK do host nem resolve CRT externo, licenças, segurança ou ambiente limpo;
- “o SDK Qt composto já corrige o player”: o run autenticado e verificado contém Qtbase/QML/Quick/Svg/D3D e a receita `package-v2` está congelada, mas ela ainda não foi executada para construir, executar ou empacotar o shell e não prova carregabilidade, ABI, CRT, PCRE2, licenças/notices/SBOM ou redistribuição;
- “YouTube é suportado”: é pesquisa futura com possível incompatibilidade de políticas;
- “ADRs foram aceitos”: todos continuam `Proposto`.

## 6. Prontidão documental

| Área | Documento | Estado |
|---|---|---|
| Contrato de trabalho | [agents.md](../agents.md) | Concluído |
| Visão | `product/vision.md` | Baseline |
| Requisitos | [Requisitos](product/requirements.md) | Para revisão |
| UX | [UX](product/ux-specification.md) | Proposta para protótipo |
| Stack | [Análise](architecture/stack-analysis.md) | Recomendação |
| Arquitetura | [Arquitetura](architecture/architecture.md) | Proposta |
| ADRs | [Índice](architecture/decisions/README.md) | Propostos |
| TDD | [Estratégia](quality/testing-strategy.md) | Contrato inicial |
| Gates | [Gates](quality/quality-gates.md) | Contrato inicial |
| Corpus | [Corpus](quality/test-corpus.md) | M0.5 em andamento; WAV e matriz básica de codecs autenticados, sem suporte publicado; os prefixos truncados atuais são derivados temporários, não fixtures canônicas; arte/limites e truncados versionados pendentes |
| Plano executável | [Plano](development-plan.md) | Baseline de execução |
| Roadmap | [Roadmap](roadmap.md) | Proposto |
| Workflow | [Workflow](development-workflow.md) | Contrato inicial |
| Segurança | [Threat model](security/threat-model.md) | Baseline local |
| Release | [Processo](delivery/release-process.md) | Proposta |
| M0 | [Ambiente, protocolo e evidências](m0/README.md) | M0.0/M0.1 concluídos; M0.2 aberto; SDK Qt composto autenticado; M0.5 com 12 fixtures; Qt Multimedia/TagLib, reader limitado, jornada local integrada, recuperação por quatro derivados temporários truncados e importação incremental de arquivos/diretórios verdes em M0.6; áudio audível, automação nativa, pacote v2, comportamento ponta a ponta de Opus/ALAC, corpus hostil canônico/limites, gates e decisão pendentes |

## 7. Próximo passo recomendado

O M0 está autorizado sem aceitar stack. A sequência atual é:

1. implementar M3.7 com teclado/foco, leitor de tela, escala, contraste e movimento reduzido, incluindo os gates nativos de Windows quando a automação estiver disponível;
2. continuar os recortes funcionais Windows na ordem do plano, com TDD e checks proporcionais;
3. manter `package-v2`, Linux, M0.3/M0.4, A/B e benchmarks suspensos até a paridade funcional, preservando seus gates e ADRs como pendentes.

No modo de fixture, a navegação manual segue a [política experimental comum](m0/README.md): anterior reinicia após 3 s quando há seek ou busca o anterior disponível; próxima pula indisponíveis sem circular a fila. Trocas começam em zero e preservam estado de reprodução, volume, mudo e modelo; vazio, carregando, erro e seek ocupado não navegam. EOF continua `clamp_paused`, e a recuperação do cenário de erro permanece separada. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

No modo real, `Ended` e erro de origem já coordenam a fila conforme o parágrafo de fase acima, inclusive as duas opções de fim cuja escolha passou a ser persistida em M3.5. Tocar linha e CRUD/reordenação estão ligados por IDs estáveis. Adicionar preserva a atual; `Ctrl+O` anexa e toca a primeira nova ocorrência. Mover, remover e limpar têm botões, teclado e menu; toda limpeza não vazia pede confirmação, e a reordenação emite anúncio coberto por Quick. Remover a atual tenta apenas o sucessor à frente, sem circular. Drag-and-drop/diretórios já usam descoberta incremental no worker e projeção em lotes; a transição terminal padrão também está coberta com adapters reais. `CHK-PERF`, E2E empacotado e NVDA/manual continuam pendentes, então M3.1/M3.2/M3.3 permanecem parciais pelos gates residuais.

A seção 5 também registra atalhos locais com TDD para abertura, transporte, Preferências e saída, incluindo guardas de foco/modal, autorrepetição e volume fracionário. Não são atalhos globais nem aprovação de FR-037; o stage histórico continua anterior a essa correção.

A fatia de preferências/temas foi fechada em `ab87803`; naquele marco, as preferências ainda eram somente em memória. A [política comum e evidência](m0/README.md) preserva esse histórico na seção 6 e registra separadamente HC na seção 7. Alto contraste está implementado com precedência temporária sobre os três temas, pares opacos e movimento independente; os checks automatizados passaram após as correções da auditoria independente. Fixture/protocolo permanecem inalterados. O recorte M3.5 posterior tornou funcional no candidato Qt/Windows o armazenamento atômico e recuperável das dez preferências, sem concluir M3 nem aceitar a stack. `restoreLastSession` e `startMinimized` apenas persistem; seus efeitos pertencem a M4.1/M4.4. O restart automatizado é Release instrumentado/offscreen, e não há prova de pacote, primeiro frame nativo sem flash, NVDA, migração histórica, perda real de energia ou benchmark. O deadline visual de saída não cancela fisicamente uma gravação síncrona em curso, e o shutdown do writer ainda pode aguardar `join()` sem limite. Paletas injetadas e escalas offscreen tampouco comprovam alto contraste nativo, DPI real ou aprovação de UX-006.

O recorte M3.6 posterior separa texto essencial de estado/ações em catálogos traduzíveis, inclui o filtro do diálogo nativo e instala o catálogo pt-BR compilado em runtime. O contrato cobre plural, causas de erro seguras, recuperação sem perda de fila/contexto, fila sem identificadores técnicos verbalizados, importação terminal acionável e cópia longa no mínimo 720 × 520. O fechamento automatizado passou 174/174 no Release combinado, 135/135 no core Qt OFF, Quick 64/64 nas escalas padrão, 1,5× e 2×, `qmllint` silencioso, formato de 90 C++/39 QML e a composição `BUILD_TESTING=OFF` com zero testes e argumentos reservados rejeitados em exit 1. Nenhum benchmark foi executado; os gates nativos e de pacote/E2E permanecem abertos, e o target SDK Qt enxuto requer `QT_HOST_PATH` para localizar as ferramentas host de tradução.

A seção 8 registra a política comum de bandeja e sua implementação Qt: menu compartilhando o transporte, intenções separadas de fechar/ocultar/sair, confirmação inicial por execução e fallback explicado. O smoke do executável fecha a janela QML e encerra normalmente, sem kill como evidência de sucesso. Os checks automatizados passaram após regressões de animação/foco e perda de disponibilidade durante a ocultação. Isso não conclui FR-030/FR-031/FR-032, o pacote ou os gates nativos de M0.2.

A seção 9 registra a receita e os checks do pacote local, sem alterar o preflight histórico. Inventário SHA-256, imports PE e relocação com Unicode passaram; o stage tem 1.378 arquivos/82.388.347 bytes e usa o CRT externo do host. O bloqueio inicial por `RTSSHooks64.dll` foi superado após uma leitura encontrar zero processos RTSS. As duas jornadas offscreen passaram no stage de receita commitada e na cópia relocada, com saída normal, módulos amostrados somente do pacote/System32 e manifesto íntegro. Nenhuma aplicação externa, whitelist ou configuração do sistema foi alterada. Isso não equivale a validar desktop nativo, máquina limpa, licenças de redistribuição ou instalação.

A [triagem de segurança](m0/evidence/m0-2-security-triage.md) acrescenta achados nas fontes FreeType/PCRE2, sem exploração do shell demonstrada, e confirma MSVC 14.43 fora de suporte no pacote histórico. O vínculo PE/SPDX foi conferido, mas advisories, servicing e licenças não receberam aprovação global. A triagem preservou dependências, toolchain e contrato histórico; a instalação posterior autorizada é registrada abaixo. `CHK-SEC` permanece em aberto.

A fila tem validação funcional: menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação. Isso não conta o pool completo nem as instanciações ao longo da execução e não fecha NFR-PERF-006. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida. A [tentativa posterior autorizada](m0/evidence/m0-2-native-attempt-20260827.md) permaneceu visualmente inconclusiva e não aprovou gates nativos/NVDA. Após a preparação `b59a247` e o primeiro cancelamento, a [nova instalação autorizada](m0/evidence/m0-2-msvc-installation.md) terminou com sucesso, sem reboot, e MSVC 19.44.35228 passou no novo build/CTest 65/65 e receipt real. A integração histórica ao pacote passou nos smokes offscreen; depois, o [Qtbase corrigido](m0/evidence/m0-2-qtbase-refresh.md) foi reconstruído e o [SDK composto autenticado](m0/evidence/m0-2-qt-composite-sdk.md) publicou 9.880 folhas/2.400.301.667 bytes no run `f75eb9bea8280844c6d1a2b3f4`, aprovado pelo verificador independente. A receita `package-v2` está implementada e congelada, sem alegação de execução real. Ainda faltam build/runtime/ABI contra essa identidade, pacote v2 e seus smokes; CRT, PCRE2, licenças/notices/SBOM, redistribuição, acessibilidade nativa/NVDA, `CHK-SEC`, desempenho, M0.2 e decisão de stack permanecem pendentes.

Até essa decisão, não iniciar implementação de produto conforme [agents.md](../agents.md).

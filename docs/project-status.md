# Estado e decisões do projeto

**Atualizado em:** 2026-08-28

**Fase:** M0.2 em andamento; shell/bandeja e atalhos locais implementados; pacote histórico MSVC 14.44 com smokes offscreen original/relocado aprovado; Qtbase 6.11.2 corrigido reconstruído e autenticado isoladamente, ainda sem SDK composto ou integração ao shell/pacote; tentativa nativa inconclusiva e pausada, com gates nativos/manuais e `CHK-SEC` pendentes

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
- drag-and-drop, CLI e instância única;
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

- “o app toca MP3/M4A/Opus”: não existe implementação/corpus executado;
- “é leve”: os budgets são metas, não medidas;
- “funciona em Linux”: arquitetura e plano existem, build não;
- “pode ser app padrão”: o fluxo está especificado, instalador não existe;
- “as preferências já persistem e restauram a sessão”: a fatia experimental de preferências/temas usa somente memória e não conclui FR-042/FR-043/FR-044;
- “alto contraste está resolvido”: existe implementação e evidência automatizada com paletas injetadas, mas isso não comprova Windows real, ramos internos do Qt Basic, Linux, NVDA ou DPI nativo nem fecha UX-006;
- “a bandeja está validada no Windows”: menu, avisos, fallback e saída passaram em testes offscreen, mas isso não comprova ícone/Explorer reais, ativação nativa, NVDA ou Linux;
- “o pacote está validado em máquina sem SDK ou pronto para distribuir”: inventário, imports PE e smokes offscreen original/relocado passaram sem paths Qt do SDK no ambiente filho, mas isso não remove o SDK do host nem resolve CRT externo, licenças, segurança ou ambiente limpo;
- “o Qtbase corrigido já corrige o player”: o rebuild isolado tem manifesto/receipt autenticados, mas ainda não compõe QML/Quick/Svg, não foi usado para construir ou empacotar o shell e não prova carregabilidade, ABI ou redistribuição;
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
| Corpus | [Corpus](quality/test-corpus.md) | Especificação |
| Plano executável | [Plano](development-plan.md) | Baseline de execução |
| Roadmap | [Roadmap](roadmap.md) | Proposto |
| Workflow | [Workflow](development-workflow.md) | Contrato inicial |
| Segurança | [Threat model](security/threat-model.md) | Baseline local |
| Release | [Processo](delivery/release-process.md) | Proposta |
| M0 | [Ambiente, protocolo e evidências](m0/README.md) | M0.0/M0.1 e preflight M0.2 concluídos; Qtbase corrigido autenticado isoladamente; shell M0.2 ainda em andamento |

## 7. Próximo passo recomendado

O M0 está autorizado sem aceitar stack. A sequência atual é:

1. compor e autenticar um SDK Qt novo com o Qtbase corrigido e módulos QML/Quick/Svg oficiais, resolver o delta de Windows SDK, reconstruir/empacotar o shell, repetir os smokes e resolver as pendências de segurança; retomar os gates nativos/manuais de M0.2 somente com captura/interação verificáveis, sem áudio real;
2. criar em M0.3 o shell Rust/Tauri equivalente, sem áudio ou rede reais;
3. medir desempenho, acessibilidade e integrações críticas dos dois shells em M0.4;
4. comparar separadamente os candidatos de áudio no mesmo corpus mínimo;
5. voltar aos ADRs com evidência reproduzível e escolher somente em M0.10.

A navegação manual segue a [política experimental comum](m0/README.md): anterior reinicia após 3 s quando há seek ou busca o anterior disponível; próxima pula indisponíveis sem circular a fila. Trocas começam em zero e preservam estado de reprodução, volume, mudo e modelo; vazio, carregando, erro e seek ocupado não navegam. EOF continua `clamp_paused`, e a recuperação do cenário de erro permanece separada. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

A seção 5 também registra atalhos locais com TDD para abertura, transporte, Preferências e saída, incluindo guardas de foco/modal, autorrepetição e volume fracionário. Não são atalhos globais nem aprovação de FR-037; o stage histórico continua anterior a essa correção.

A fatia de preferências/temas foi fechada em `ab87803`; a [política comum e evidência](m0/README.md) preserva esses resultados na seção 6 e registra separadamente HC na seção 7. Alto contraste está implementado com precedência temporária sobre os três temas, pares opacos e movimento independente; os checks automatizados passaram após as correções da auditoria independente. Fixture/protocolo permanecem inalterados. Preferências são somente em memória; sessão/fim da fila reais e FR-042/FR-043/FR-044 continuam futuros. Paletas injetadas e escalas offscreen não comprovam alto contraste nativo, DPI real/NVDA ou aprovação de UX-006.

A seção 8 registra a política comum de bandeja e sua implementação Qt: menu compartilhando o transporte, intenções separadas de fechar/ocultar/sair, confirmação inicial por execução e fallback explicado. O smoke do executável fecha a janela QML e encerra normalmente, sem kill como evidência de sucesso. Os checks automatizados passaram após regressões de animação/foco e perda de disponibilidade durante a ocultação. Isso não conclui FR-030/FR-031/FR-032, o pacote ou os gates nativos de M0.2.

A seção 9 registra a receita e os checks do pacote local, sem alterar o preflight histórico. Inventário SHA-256, imports PE e relocação com Unicode passaram; o stage tem 1.378 arquivos/82.388.347 bytes e usa o CRT externo do host. O bloqueio inicial por `RTSSHooks64.dll` foi superado após uma leitura encontrar zero processos RTSS. As duas jornadas offscreen passaram no stage de receita commitada e na cópia relocada, com saída normal, módulos amostrados somente do pacote/System32 e manifesto íntegro. Nenhuma aplicação externa, whitelist ou configuração do sistema foi alterada. Isso não equivale a validar desktop nativo, máquina limpa, licenças de redistribuição ou instalação.

A [triagem de segurança](m0/evidence/m0-2-security-triage.md) acrescenta achados nas fontes FreeType/PCRE2, sem exploração do shell demonstrada, e confirma MSVC 14.43 fora de suporte no pacote histórico. O vínculo PE/SPDX foi conferido, mas advisories, servicing e licenças não receberam aprovação global. A triagem preservou dependências, toolchain e contrato histórico; a instalação posterior autorizada é registrada abaixo. `CHK-SEC` permanece em aberto.

A fila tem validação funcional: menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação. Isso não conta o pool completo nem as instanciações ao longo da execução e não fecha NFR-PERF-006. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida. A [tentativa posterior autorizada](m0/evidence/m0-2-native-attempt-20260827.md) permaneceu visualmente inconclusiva e não aprovou gates nativos/NVDA. Após a preparação `b59a247` e o primeiro cancelamento, a [nova instalação autorizada](m0/evidence/m0-2-msvc-installation.md) terminou com sucesso, sem reboot, e MSVC 19.44.35228 passou no novo build/CTest 65/65 e receipt real. A integração histórica ao pacote passou nos smokes offscreen; depois, o [Qtbase corrigido](m0/evidence/m0-2-qtbase-refresh.md) foi reconstruído isoladamente e autenticado por outro contrato/manifesto/receipt, preservando o pacote anterior. Ainda falta compor o SDK corrigido e validar novo build/pacote. Acessibilidade nativa/NVDA, `CHK-SEC` e desempenho permanecem pendentes.

Até essa decisão, não iniciar implementação de produto conforme [agents.md](../agents.md).

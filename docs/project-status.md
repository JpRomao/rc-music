# Estado e decisões do projeto

**Atualizado em:** 2026-08-23  
**Fase:** D0 — documentação inicial  
**Código do produto:** ainda não iniciado

## 1. O que está confirmado

Estas decisões vieram diretamente do mantenedor e são requisitos de produto/processo:

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

Os dois últimos itens confirmam a direção do experimento, não a aceitação de uma stack.

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
| Windows package | instalador por usuário que comprove o fluxo de Default Apps; tecnologia definida no M0 | Proposto |
| Linux package | `.deb` primeiro, AppImage depois | Proposto |
| Remoto | porta isolada, desativada no MVP | Proposto em ADR-0003 |

Alternativas preservadas:

- Rust + Slint somente se a evidência justificar um experimento adicional de footprint;
- GStreamer/libmpv apenas como substituição de engine quando requisitos futuros justificarem.

## 3. Requisitos derivados para revisão

Estes comportamentos tornam o produto consistente, mas não foram pedidos literalmente e podem ser ajustados:

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
- instalador Windows por usuário e `.deb` como primeiros formatos de pacote; a tecnologia do instalador Windows será escolhida no M0.

Revisar [requisitos](product/requirements.md) e [UX](product/ux-specification.md) antes de aceitá-los como detalhe final.

## 4. Decisões em aberto

1. autorizar o M0 A/B com Qt como hipótese principal e Tauri como desafiante, sem aceitar antecipadamente qualquer stack;
2. escolher shell/UI e backend de áudio somente após as comparações separadas e a decisão explícita por ADR;
3. nome e identidade visual do aplicativo;
4. licença do repositório e modelo de distribuição;
5. distribuição Linux de referência;
6. manter Windows 10 como Tier 1 pelo tempo proposto;
7. exigir versão portátil no 0.1 ou depois;
8. confirmar fila e restauração como P0;
9. aprovar/recalibrar budgets após medições;
10. decidir no futuro se YouTube será pesquisado, oferecido via player oficial ou rejeitado.

Nenhuma dessas perguntas bloqueia a documentação. ADR-0001/0002 precisam de decisão ou evidência M0 antes do bootstrap definitivo.

## 5. Alegações que ainda não podem ser feitas

- “o app toca MP3/M4A/Opus”: não existe implementação/corpus executado;
- “é leve”: os budgets são metas, não medidas;
- “funciona em Linux”: arquitetura e plano existem, build não;
- “pode ser app padrão”: o fluxo está especificado, instalador não existe;
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
| Roadmap | [Roadmap](roadmap.md) | Proposto |
| Workflow | [Workflow](development-workflow.md) | Contrato inicial |
| Segurança | [Threat model](security/threat-model.md) | Baseline local |
| Release | [Processo](delivery/release-process.md) | Proposta |

## 7. Próximo passo recomendado

O próximo passo seguro é o mantenedor revisar este arquivo e os três ADRs. Com autorização para M0:

1. aceitar apenas o escopo do experimento, não a stack definitiva;
2. criar shells Qt e Tauri equivalentes, ambos com backend falso;
3. comparar separadamente os candidatos de áudio no mesmo corpus mínimo;
4. medir desempenho e testar acessibilidade e integrações críticas nos dois candidatos;
5. voltar aos ADRs com evidência reproduzível e escolher.

Até essa decisão, não iniciar implementação de produto conforme [agents.md](../agents.md).

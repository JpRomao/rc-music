# Estado e decisões do projeto

**Atualizado em:** 2026-08-26

**Fase:** M0.2 em andamento; preflight Qt concluído e primeiro incremento do core experimental iniciado

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
| M0 | [Ambiente, protocolo e evidências](m0/README.md) | M0.0/M0.1 e preflight M0.2 concluídos; shell M0.2 em andamento |

## 7. Próximo passo recomendado

O M0 está autorizado sem aceitar stack. A sequência atual é:

1. continuar em M0.2 o shell experimental C++20/Qt Quick/QML por TDD, evoluindo o core e o presenter determinísticos já iniciados para timer e UI sem áudio real, usando o [preflight autenticado](m0/evidence/m0-2-preflight.md);
2. criar em M0.3 o shell Rust/Tauri equivalente, sem áudio ou rede reais;
3. medir desempenho, acessibilidade e integrações críticas dos dois shells em M0.4;
4. comparar separadamente os candidatos de áudio no mesmo corpus mínimo;
5. voltar aos ADRs com evidência reproduzível e escolher somente em M0.10.

Até essa decisão, não iniciar implementação de produto conforme [agents.md](../agents.md).

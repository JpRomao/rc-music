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
- mudanças devem ser sempre commitadas.

## 2. O que é recomendado, mas ainda proposto

| Decisão | Recomendação | Estado |
|---|---|---|
| Linguagem/core | Rust | Proposto em ADR-0001 |
| Shell/UI | Tauri 2 + Svelte/TypeScript/Vite | Proposto em ADR-0001 |
| Áudio | Rodio/CPAL + features Symphonia testadas | Proposto em ADR-0002 |
| Metadata | Lofty + filesystem | Proposto em ADR-0002 |
| Estrutura | um pacote com módulos, extraindo crates só por necessidade | Proposto |
| Persistência | JSON versionado e atômico; sem banco no MVP | Proposto |
| Integração | tray/single-instance Tauri, SMTC Windows, MPRIS Linux | Proposto |
| Windows package | NSIS por usuário | Proposto |
| Linux package | `.deb` primeiro, AppImage depois | Proposto |
| Remoto | porta isolada, desativada no MVP | Proposto em ADR-0003 |

Alternativas preservadas:

- Rust + Slint se footprint do WebView for o único bloqueio;
- C++20 + Qt Quick/QML + Qt Multimedia se Tauri falhar em múltiplos critérios;
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
- NSIS e `.deb` como primeiros formatos de pacote.

Revisar `product/requirements.md` e `product/ux-specification.md` antes de aceitá-los como detalhe final.

## 4. Decisões em aberto

1. aceitar Rust/Tauri/Svelte para o spike M0 ou pedir spike comparativo diferente;
2. aceitar Rodio/Symphonia/Lofty como candidato de áudio/metadata;
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
| Contrato de trabalho | `../agents.md` | Concluído |
| Visão | `product/vision.md` | Baseline |
| Requisitos | `product/requirements.md` | Para revisão |
| UX | `product/ux-specification.md` | Proposta para protótipo |
| Stack | `architecture/stack-analysis.md` | Recomendação |
| Arquitetura | `architecture/architecture.md` | Proposta |
| ADRs | `architecture/decisions/` | Propostos |
| TDD | `quality/testing-strategy.md` | Contrato inicial |
| Gates | `quality/quality-gates.md` | Contrato inicial |
| Corpus | `quality/test-corpus.md` | Especificação |
| Roadmap | `roadmap.md` | Proposto |
| Workflow | `development-workflow.md` | Contrato inicial |
| Segurança | `security/threat-model.md` | Baseline local |
| Release | `delivery/release-process.md` | Proposta |

## 7. Próximo passo recomendado

O próximo passo seguro é o mantenedor revisar este arquivo e os três ADRs. Com autorização para M0:

1. aceitar apenas o escopo do spike, não a stack definitiva;
2. criar branch/commits de protótipo Tauri/Svelte e backend falso;
3. criar spike separado Rodio/Symphonia/Lofty com corpus mínimo;
4. medir e testar NVDA/tray/single-instance;
5. voltar aos ADRs com evidência e escolher.

Até essa decisão, não iniciar implementação de produto conforme `agents.md`.

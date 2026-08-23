# Roadmap

**Estado:** proposta de ordem e escopo; sem compromisso de datas  
**Última revisão:** 2026-08-23

## 1. Como ler

- **Concluído:** entregável presente e validado dentro do escopo documental.
- **Próximo:** pronto para iniciar após decisão/autorização necessária.
- **Planejado:** depende de marcos anteriores.
- **Opcional:** só entra após uso real justificar.
- **Pesquisa:** pode resultar em rejeição, não é promessa de feature.

Cada marco termina em commits verdes, documentação atualizada e demonstração do critério de saída. Datas serão definidas somente se o mantenedor desejar; ordem e gates são mais importantes para este projeto pessoal.

## 2. Visão dos marcos

```text
D0 Documentação
      │
      ▼
M0 Spikes de decisão ──── reprovar ──► fallback Slint ou Qt
      │ aprovar
      ▼
M1 Fundação TDD
      ▼
M2 Vertical slice local
      ▼
M3 Experiência completa do player
      ▼
M4 Integração desktop e recuperação
      ▼
M5 Hardening e release 0.1
      ▼
M6 Organização 0.2 (opcional)
      ▼
R1 Fontes remotas/YouTube (pesquisa separada)
```

## 3. D0 — Baseline documental

**Estado:** Concluído nesta etapa, aguardando revisão do mantenedor.

### Entregas

- contrato operacional em `agents.md`;
- visão, requisitos rastreáveis e UX;
- análise Rust/C++ e recomendação de stack;
- arquitetura, fronteiras e ADRs propostos;
- estratégia TDD, corpus e gates;
- roadmap, segurança, workflow e processo de release;
- README como índice e estado do projeto.

### Gate de saída

- links e estrutura verificados;
- nenhuma proposta apresentada como decisão aceita;
- todos os arquivos commitados;
- mantenedor revisa os itens confirmados, derivados e em aberto.

## 4. M0 — Spikes de decisão

**Estado:** Próximo após aprovação para implementar spikes.  
**Objetivo:** eliminar riscos estruturais antes de construir o produto.

### M0.1 — UI e shell Tauri/Svelte

Construir somente:

- estados vazio/tocando/erro;
- player bar com slider e seek preview usando backend falso;
- painel de fila e preferências;
- temas sistema/claro/escuro;
- teclado, foco, 100–200% e movimento reduzido;
- tray e close/restore básicos;
- build Release e instalador de teste.

Validar:

- NVDA/UI Automation;
- cold start, working set e CPU idle;
- ausência de flash e estabilidade visual;
- CSP/capabilities mínimas;
- WebView2 em Windows 10/11 e build Linux de referência.

### M0.2 — Áudio e metadata

Usando UI mínima/CLI de teste:

- Rodio/Symphonia abre, toca, pausa, busca e termina cada fixture;
- Lofty lê tags, arte e propriedades;
- posição/seek medidos em MP3 CBR/VBR e M4A/AAC;
- cancelamento e troca rápida não publicam estado antigo;
- arquivo corrompido não derruba processo;
- sessão curta e soak exploratório verificam handles/memória.

Publicar a primeira matriz de formatos.

### M0.3 — Integração e pacote Windows

Provar:

- instância única e encaminhamento de vários paths Unicode;
- tray incluindo saída real;
- associação registrada pelo NSIS e abertura da tela de apps padrão;
- SMTC e teclas de mídia;
- suspend/resume e reinício do Explorer;
- install, upgrade e uninstall em ambiente descartável.

### M0.4 — Decisão

Comparar resultados com `docs/architecture/stack-analysis.md`:

- aprovar ADR-0001/0002 e registrar medidas; ou
- spike Slint se somente WebView for pesado; ou
- spike C++/Qt se a combinação falhar em múltiplas integrações/codec/acessibilidade; ou
- revisar requisito/orçamento com o mantenedor.

### Gate de saída

- pelo menos uma stack atende UX, formatos principais, plataforma e budgets aceitos;
- ADRs mudam de estado em commit explícito;
- protótipo descartável é removido ou promovido conscientemente;
- riscos e baseline de hardware publicados.

## 5. M1 — Fundação dirigida por testes

**Estado:** Planejado.  
**Objetivo:** criar o menor esqueleto sustentável após a stack ser aceita.

### Sequência TDD

1. bootstrap repositório e gates antes de feature;
2. tipos `TrackId`, tempo, volume e preferências;
3. máquina de estado de playback;
4. fila por ID e regras anterior/próxima;
5. portas e fakes mínimos;
6. controller de aplicação e snapshots;
7. boundary IPC com validação;
8. shell UI consumindo fake.

### Entregas

- manifests/lockfiles/toolchain pinados;
- scripts `format`, `lint`, `check`, `test`, `build`, `validate`;
- CI Windows/Linux básica;
- cobertura do core;
- configuração de segurança Tauri;
- fixtures iniciais e manifesto;
- primeiro executável ainda sem alegação de player completo.

### Gate de saída

- ciclo Red–Green–Refactor demonstrado no histórico;
- domínio roda sem Tauri/audio;
- UI roda contra fake;
- gates rápidos verdes em Windows e Linux;
- arquitetura real corresponde aos documentos ou ADR é atualizado.

## 6. M2 — Vertical slice de reprodução local

**Estado:** Planejado.  
**Objetivo:** abrir um arquivo real e completar a jornada principal.

### Escopo

- seletor nativo e abertura por path;
- load async, play/pause, duração e posição;
- seek clicável/arrastável;
- saltos padrão ±5 s e preferências;
- volume/mudo;
- metadata essencial, capa e fallback;
- erro recuperável;
- uma faixa por vez, antes da fila visual completa.

### TDD/validação

- cada comportamento nasce no fake/contrato;
- adapter real satisfaz suite comum;
- corpus aprovado para formatos P0;
- E2E abrir → tocar → seek → pausar;
- performance open-to-audible e seek-to-audible.

### Gate de saída

- critérios FR-001/002/010/012–015/020–024 demonstrados;
- nenhum I/O pesado bloqueia UI;
- formatos publicados refletem apenas testes verdes;
- instalável de preview interno no Windows.

## 7. M3 — Experiência completa do player

**Estado:** Planejado.  
**Objetivo:** transformar o vertical slice em produto agradável.

### Escopo

- fila adicionar/tocar agora/reordenar/remover/limpar;
- anterior/próxima e fim da fila;
- drag-and-drop de arquivos/diretórios;
- painel de detalhes técnicos;
- preferências completas do MVP;
- temas sistema/claro/escuro;
- todos os estados e microcopy;
- atalhos e foco;
- layout/escala/alto contraste/movimento reduzido.

### Gate de saída

- jornadas 1–4, 8 e 9 de UX passam;
- revisão manual com NVDA;
- adicionar 1.000 itens respeita budget;
- nenhum controle P0 fica escondido ou depende somente de mouse;
- protótipo e implementação convergem, com decisão visual registrada.

## 8. M4 — Desktop, sessão e recuperação

**Estado:** Planejado.  
**Objetivo:** tornar o player um cidadão confiável do Windows e suportável no Linux.

### Escopo Windows

- single-instance e argumentos do shell;
- tray e close behavior;
- SMTC/teclas de mídia;
- ProgID/capabilities/associações;
- página oficial de defaults;
- persistência atômica e restauração pausada;
- suspend/resume/device lost/Explorer restart;
- instalador, upgrade e uninstall.

### Escopo Linux

- distribuição de referência;
- MPRIS;
- `.desktop`, MIME e ícones;
- tray com fallback visível;
- Wayland/X11 smoke;
- `.deb` funcional.

### Gate de saída

- jornadas 5–7 passam no Windows empacotado;
- sessão corrompida/falha atômica tem recuperação;
- arquivos do usuário não são removidos indevidamente;
- matriz Tier 1/Tier 2 publicada sem extrapolar evidência.

## 9. M5 — Hardening e release 0.1

**Estado:** Planejado.

### Escopo

- resolver bugs P0/P1;
- corpus completo e formatos finais;
- fuzz/property/mutation focados nos riscos encontrados;
- oito horas de playback e stress de troca/seek;
- performance P50/P95 e regressões;
- acessibilidade Windows completa e Linux possível;
- revisão de dependências, licenças, notices e SBOM;
- assinatura Windows se a release for pública;
- changelog, manual curto e troubleshooting;
- artefatos/hashes/manifesto reproduzíveis.

### Gate de saída

Todos os critérios em `docs/product/requirements.md` seção 6 e `docs/quality/quality-gates.md` seção 9 passam, ou exceções são aceitas e documentadas antes da tag.

## 10. M6 — Organização 0.2

**Estado:** Opcional; iniciar somente após uso real do 0.1.

Candidatos independentes:

- playlists nomeadas e M3U8;
- shuffle/repeat;
- biblioteca opcional por pastas e busca;
- mini-player;
- escolha de dispositivo;
- notificações configuráveis;
- editor de tema limitado a tokens.

Biblioteca indexada é o primeiro caso que pode justificar SQLite. Cada candidato recebe requisito, teste de usabilidade e decisão própria; não criar “grande 0.2” obrigatória.

## 11. R1 — Fontes remotas e YouTube

**Estado:** Pesquisa; sem promessa de release.

Ordem:

1. streams HTTP/podcasts autorizados e capacidades sem duração/seek;
2. threat model e cache remoto;
3. revisão jurídica/políticas do provedor;
4. comparação resolver próprio, libmpv e sidecar;
5. protótipo isolado sem distribuição;
6. ADR com decisão de implementar, oferecer player oficial visível ou rejeitar.

Critério obrigatório: qualquer falha remota deixa playback local intacto. YouTube audio-only em background/tray pode ser incompatível com políticas oficiais e é aceitável que a pesquisa termine em “não implementar”.

## 12. Dependências e caminho crítico

| Entrega | Depende de |
|---|---|
| Escolher stack | M0 UI + áudio + integração |
| Declarar formatos | corpus e adapter real |
| Construir UI final | protótipo/acessibilidade do toolkit |
| Associação padrão | formato aprovado + instalador |
| Restauração | fila + persistência atômica |
| Release Windows | M2 + M3 + M4 + M5 |
| Release Linux | stack aprovada + M2/M3 + integrações Tier 2 |
| URL remota | player local estável + ADR-0003 + R1 |

## 13. Controle de escopo

Até a release 0.1, rejeitar por padrão:

- biblioteca/banco;
- conta/nuvem/telemetria;
- equalizador e DSP;
- plugins;
- editor de tags;
- auto-update;
- video player;
- download remoto;
- skin engine irrestrito;
- formatos sem fixture/teste.

Uma exceção requer necessidade real, requisito rastreável, impacto nos marcos e commit documental antes da implementação.


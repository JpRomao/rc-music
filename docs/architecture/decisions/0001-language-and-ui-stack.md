# ADR-0001: Rust, Tauri 2 e Svelte para o desktop

**Estado:** Proposto  
**Data:** 2026-08-23  
**Decisor:** mantenedor do projeto

## Contexto

O produto precisa de UI consumer moderna e minimalista, resposta rápida, baixo consumo relativo, Windows excelente e Linux real. Deve continuar simples para uma pessoa e permitir TDD do núcleo.

Foram avaliados Rust/Tauri/Svelte, C++/Qt Quick, Rust/Slint e Rust/Iced. A análise completa está em `../stack-analysis.md`.

## Decisão proposta

Usar:

- Rust no domínio, casos de uso, áudio, persistência e integrações privilegiadas;
- Tauri 2 como shell desktop;
- Svelte + TypeScript estrito + Vite para uma SPA local;
- CSS variables e componentes próprios para tokens e temas;
- IPC tipado e estreito, com snapshots coalescidos a 5–10 Hz;
- WebView sem conteúdo remoto, capabilities mínimas e CSP restrita.

## Motivos

- prioridade explícita de UX favorece o ecossistema web de apresentação;
- Tauri usa WebView do sistema, evitando Chromium empacotado;
- Rust melhora segurança e TDD nas áreas que processam arquivos não confiáveis;
- tray, single-instance, bundles e associações possuem suporte oficial;
- Svelte/Vite é menor e mais direto que um framework full-stack.

## Consequências positivas

- iteração visual e acessibilidade rápidas;
- núcleo independente e testável sem desktop;
- instaladores e integração multiplataforma com menos código próprio;
- caminho claro para temas e prototipação.

## Consequências negativas

- dois toolchains e linguagens;
- memória/startup superiores a Slint;
- IPC e sincronização de estado exigem disciplina;
- WebKitGTK varia entre distribuições;
- UI web aumenta superfície de segurança se capabilities forem amplas.

## Alternativas

### C++20 + Qt Quick/QML

Alternativa mais coesa e madura. Ganha em integração e backend multimídia, perde em segurança de memória, obrigações Qt/FFmpeg e possível tamanho. É o fallback quando Tauri falhar em múltiplos critérios.

### Rust + Slint

Melhor candidato para footprint mínimo. Exige provar componentes, leitor de tela e integrações. É o fallback quando a única falha Tauri for consumo/startup.

### Rust + Iced/egui

Não entregam vantagem suficiente para o perfil consumer e o acabamento esperado nesta fase.

## Condições para aceitar

- spike M0 reproduz MP3 e M4A com seek;
- UI protótipo passa por NVDA, teclado e escala de 200%;
- tray, single-instance e argumento Unicode funcionam em build empacotado;
- performance respeita metas ou desvios são explicitamente aprovados;
- build Windows e Linux de referência é reproduzível;
- revisão de CSP, capabilities e licenças concluída.

## Gatilhos de revisão

- working set ou cold start inviável;
- WebKitGTK impede paridade essencial;
- acessibilidade do WebView falha em jornada principal;
- bridge/IPC causa complexidade ou bugs persistentes;
- distribuição exige WebView2 offline excessivo.


# ADR-0001: Comparação entre C++/Qt e Rust/Tauri/Svelte

**Estado:** Substituído

**Data:** 2026-08-23  
**Decisor:** mantenedor do projeto

Substituído em 2026-09-05 pelo [ADR-0005](0005-windows-product-stack.md), mediante decisão explícita do mantenedor, não por vitória medida no A/B. O texto abaixo preserva a proposta e o protocolo históricos; não bloqueia o desenvolvimento Windows atual.

## Contexto

O produto precisa de UI consumer moderna e minimalista, resposta rápida, baixo consumo, Windows excelente e Linux real. Também deve continuar simples para uma pessoa, permitir TDD do núcleo e respeitar os budgets NFR-PERF.

Uma implementação C++ não é automaticamente mais rápida que uma Rust, e o tamanho do executável não representa o working set da aplicação. A comparação relevante é entre os shells completos: Qt Quick/QML possui runtime próprio, enquanto Tauri usa o WebView do sistema e, no Windows, deve ser medido com toda a árvore de processos do WebView2. CPU de reprodução e compatibilidade de codecs pertencem ao ensaio de áudio do ADR-0002, não podem ser atribuídas ao toolkit de UI.

Foram consideradas C++20/Qt Quick, Rust/Tauri/Svelte, Rust/Slint e Rust/Iced. A comparação qualitativa está na [análise de stack](../stack-analysis.md), e o protocolo obrigatório está em NFR-STACK-001–006 dos [requisitos](../../product/requirements.md).

## Decisão proposta

Executar no M0 uma comparação A/B direta e reproduzível, sem aceitar antecipadamente nenhum toolkit:

- tratar **C++20 + Qt 6 Quick/QML** como hipótese principal de menor cold start e memória no Windows;
- tratar **Rust + Tauri 2 + Svelte** como desafiante A/B com o mesmo recorte de produto;
- comparar primeiro os shells com backend falso e dados determinísticos, isolando o custo da UI do custo de decode e saída de áudio;
- aplicar gates equivalentes de UX, acessibilidade, performance, plataforma e empacotamento;
- aceitar uma stack somente em ADR explícito posterior, sustentado pelas medidas e pelo ambiente reproduzível.

O ramo Qt usa como baseline:

- C++20 no domínio e nos casos de uso;
- Qt 6 Quick/QML com estilo Basic customizado e bindings compilados quando aplicável;
- domínio e aplicação sem dependência de `QObject`, QML ou tipos Qt;
- comunicação direta e estreita entre view model e núcleo, sem regra de negócio na UI.

O ramo Tauri usa como baseline:

- Rust no domínio e nos casos de uso;
- Tauri 2 como shell desktop;
- Svelte, TypeScript estrito e Vite para a SPA local;
- CSS variables e componentes próprios para tokens e temas;
- IPC tipado e estreito, com snapshots coalescidos a 5–10 Hz;
- WebView sem conteúdo remoto, capabilities mínimas e CSP restrita.

Slint permanece alternativa condicionada a um novo spike se nenhum dos dois candidatos satisfizer o conjunto de gates e a causa principal for footprint. Iced/egui não justificam um terceiro ramo neste momento.

## Protocolo A/B

Cada candidato deve produzir um build Release com os mesmos assets, conteúdo e jornada:

- estados vazio, tocando e erro;
- player bar com slider e seek preview contra backend falso;
- fila determinística com 1.000 itens;
- preferências e temas sistema/claro/escuro;
- teclado, foco visível, alto contraste, escala de 100/150/200% e movimento reduzido;
- tray e close/restore básicos;
- pacote Windows e build/smoke Linux de referência.

Antes da coleta, registrar máquina, SO, energia, versões, commit, roteiro, marcadores, tolerâncias e método. Cada cenário de lançamento usa pelo menos 20 amostras válidas por candidato e separa primeira execução, cold start e execução aquecida. O tempo processo iniciado → janela acionável é observado externamente.

A coleta deve:

- cobrir NFR-PERF-001 e NFR-PERF-004–006 no shell, deixando NFR-PERF-002/003/007 para o ensaio de áudio correspondente;
- agregar private working set/private bytes e CPU de toda a árvore de processos, inclusive descendentes do WebView2; working set compartilhado é dado complementar e não é somado como métrica primária;
- registrar P50/P95 e não somente o melhor resultado;
- relatar tamanho instalado, pacote e pré-requisitos offline separadamente da RAM;
- não rodar junto de builds ou cargas concorrentes que invalidem a comparação.

## Motivos

- Qt Quick é a hipótese mais plausível para atingir os budgets estritos de memória e startup, além de oferecer integração desktop e multimídia maduras;
- Tauri mantém vantagens relevantes de segurança de memória, velocidade de iteração visual e ecossistema web, portanto merece comparação direta em vez de rejeição por pressuposto;
- o mesmo recorte e os mesmos gates evitam favorecer o candidato mais completo ou mais otimizado;
- separar shell e áudio impede atribuir ao C++/Rust custos que pertencem ao decoder, buffering ou dispositivo de saída;
- uma decisão baseada em medidas reduz o risco de escolher uma stack incompatível com os requisitos mais caros de reverter.

## Consequências positivas

- escolha final rastreável por evidência;
- budgets de performance e acessibilidade atuam como gates reais;
- núcleo permanece testável sem UI em ambos os ramos;
- footprint de runtime não é confundido com tamanho em disco;
- protótipos podem ser descartados sem comprometer a arquitetura definitiva.

## Consequências negativas

- o M0 exige dois shells pequenos e trabalho descartável deliberado;
- dois toolchains precisam ser instalados apenas durante o experimento;
- resultados próximos podem não produzir um vencedor puramente quantitativo;
- integrações de plataforma e empacotamento precisam de provas proporcionais nos dois ramos;
- Qt exige revisão LGPL/FFmpeg e disciplina de segurança em C++; Tauri exige disciplina de IPC, CSP/capabilities e consideração do WebView2/WebKitGTK.

## Condições para aceitar um candidato

- cumprir os budgets NFR-PERF vigentes ou ter cada desvio aprovado explicitamente por ADR;
- completar o mesmo roteiro sem bloqueio P0 de UX ou acessibilidade, incluindo NVDA, teclado, alto contraste, escalas e movimento reduzido;
- provar tray, close/restore e pacote Windows, além do build/smoke Linux de referência;
- manter o domínio e os casos de uso independentes do toolkit;
- concluir revisão de dependências, licenças e modelo de distribuição;
- publicar números, método, ambiente, limitações e resultados do ADR-0002;
- receber decisão explícita do mantenedor em mudança posterior do estado do ADR.

Resultado dentro das tolerâncias pré-registradas é empate. Nesse caso, a decisão considera, nesta ordem, UX comprovada, acessibilidade, manutenção para uma pessoa, segurança, licenciamento e integração de plataforma. Nenhum candidato vence somente pela linguagem ou por um único número.

## Gatilhos de revisão

- nenhum candidato atende budgets e acessibilidade simultaneamente;
- o protocolo mostra viés ou diferença material entre os recortes;
- WebView2/WebKitGTK ou Qt/QML impede paridade essencial;
- custo de distribuição offline, licenciamento ou manutenção torna um candidato impraticável;
- surge requisito P0 que invalida o desenho comparado.

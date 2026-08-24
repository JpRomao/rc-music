# Music Player

Player de áudio desktop pessoal, local-first, Windows-first e planejado para Linux. O objetivo é combinar inicialização rápida e baixo consumo com uma interface moderna, minimalista e realmente agradável para uso diário.

> **Estado atual:** D0 e M0.0 concluídos; M0.1 está em andamento, com o harness-base e a fixture sintética do shell materializados. Ainda não existe implementação de produto, instalador ou formato de áudio comprovado.

“Music Player” é um nome provisório. Nome, identidade visual e licença ainda pertencem ao mantenedor.

## Proposta do produto

O MVP planejado concentra-se em abrir e ouvir arquivos locais:

- MP3, M4A/AAC e formatos comuns validados por corpus;
- play/pause, anterior/próxima, volume/mudo;
- progresso clicável e arrastável;
- saltos de −5 s/+5 s por padrão, configuráveis;
- fila simples;
- tags, capa e detalhes como tamanho, duração, codec e bitrate;
- bandeja, instância única, teclas de mídia e associações do sistema;
- temas do sistema, claro e escuro;
- preferências e recuperação de sessão local;
- teclado, leitor de tela, alto contraste e escala;
- Windows como Tier 1 e Linux como Tier 2.

YouTube por URL é somente uma linha de pesquisa futura. O modo audio-only em background pode conflitar com políticas oficiais, além de exigir um resolver frágil; não faz parte do MVP e pode ser rejeitado após análise.

## Hipóteses de stack para o M0 — nenhuma aceita

O M0 tratará **C++20 + Qt 6 Quick/QML** como hipótese principal de desempenho no Windows. O baseline usa estilo Basic customizado, Qt Multimedia/FFmpeg e TagLib, mantendo domínio e aplicação em C++ puro, independentes de Qt e QML.

**Rust + Tauri 2 + Svelte/TypeScript/Vite** será o desafiante A/B direto, com o baseline de áudio Rodio/CPAL, Symphonia e Lofty já descrito nos ADRs propostos. Os shells serão avaliados com o mesmo recorte de UX; os backends de áudio serão comparados separadamente no mesmo corpus para distinguir o custo da UI do custo de decode e saída.

Esta ordem expressa uma hipótese mensurável, não uma escolha definitiva. Desempenho, UX, acessibilidade, manutenção, segurança e licenciamento decidirão a stack por ADR explícito, conforme os [requisitos NFR-STACK-001–006](docs/product/requirements.md). Nenhum ADR está aceito neste momento.

## Documentação

Comece por:

| Documento | Conteúdo |
|---|---|
| [agents.md](agents.md) | Contrato obrigatório: TDD, commits, segurança e limites |
| [Estado do projeto](docs/project-status.md) | Confirmado, proposto, derivado, aberto e alegações proibidas |
| [Visão](docs/product/vision.md) | Problema, princípios, público, escopo e plataformas |
| [Requisitos](docs/product/requirements.md) | FR/UX/NFR, prioridades, critérios e matriz de formatos |
| [UX](docs/product/ux-specification.md) | Layout, estados, interações, tray, teclado e acessibilidade |
| [Análise de stack](docs/architecture/stack-analysis.md) | Rust/C++ e Tauri/Qt/Slint, áudio, licenças e fontes |
| [Arquitetura](docs/architecture/architecture.md) | Domínio, portas, eventos, concorrência, IPC e persistência |
| [ADRs](docs/architecture/decisions/README.md) | Decisões propostas e seus gates |
| [TDD e testes](docs/quality/testing-strategy.md) | Red–Green–Refactor, fakes, contratos, E2E e performance |
| [Gates](docs/quality/quality-gates.md) | Checks por risco, CI e release |
| [Corpus](docs/quality/test-corpus.md) | Fixtures, manifesto e matriz de codecs |
| [Plano executável](docs/development-plan.md) | Incrementos, checks, evidência, rastreabilidade e checkpoint atual |
| [Registro M0](docs/m0/README.md) | Autorização, ambiente/protocolo congelados e índice de evidências |
| [Roadmap](docs/roadmap.md) | D0, spikes M0, MVP 0.1 e evoluções |
| [Workflow](docs/development-workflow.md) | Unidade de trabalho, commits, bugs, deps e handoff |
| [Threat model](docs/security/threat-model.md) | Fronteiras, ameaças, controles e supply chain |
| [Release](docs/delivery/release-process.md) | Versionamento, artefatos, instalação e publicação |

## Regras de desenvolvimento

- Leia [agents.md](agents.md) antes de trabalhar.
- Toda mudança concluída termina em commit atômico.
- Mudança de comportamento segue Red → Green → Refactor.
- Correção começa por teste de regressão.
- Windows recebe validação Tier 1; Linux não recebe alegação além do ambiente testado.
- Formato só é anunciado depois de passar pelo corpus.
- Decisão `Proposta` não vira `Aceita` sem evidência e decisão explícita.
- Não adicionar rede, banco, telemetria, auto-update ou segundo engine prematuramente.

## Estado do repositório

Por enquanto não há manifests/toolchains de implementação nem comandos de build. O validador documental já existe; comandos planejados de produto estão em [quality-gates.md](docs/quality/quality-gates.md) e só devem ser apresentados como executáveis após o M0 e o bootstrap criarem e validarem os runners correspondentes.

Validações atuais:

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\validate-docs.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Invoke-M0HarnessTests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Fixture.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Statistics.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Markers.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Evidence.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Visual.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Layout.ps1
git status --short --branch
```

O validador documental cobre links relativos, IDs/matriz de requisitos, checks, etapas, estados ADR e whitespace do diff. Os runners M0 validam somente os contratos materializados até aqui; não fazem alegação de desempenho nem substituem os checks futuros dos candidatos. O status confirma que a mudança terminou no commit esperado.

## Próximo passo

1. completar coleta live, resumo e piloto neutro em M0.1 sobre os contratos, markers, evidência e detector sintético já materializados;
2. construir e medir os dois candidatos sem aceitar stack antecipadamente;
3. medir e então aceitar/rejeitar os ADRs;
4. somente depois iniciar o MVP guiado por TDD.

O roadmap deliberadamente não contém datas. O objetivo é manter o projeto pequeno, verificável e agradável de evoluir.

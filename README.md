# Music Player

Player de áudio desktop pessoal, local-first, Windows-first e planejado para Linux. O objetivo é combinar inicialização rápida e baixo consumo com uma interface moderna, minimalista e realmente agradável para uso diário.

> **Estado atual:** documentação e análise concluídas para revisão. Ainda não existe implementação, instalador ou formato de áudio comprovado pelo projeto.

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

## Stack recomendada — ainda proposta

O primeiro spike recomendado usa:

- Rust para domínio, áudio, arquivos, persistência e integrações;
- Tauri 2 como shell desktop;
- Svelte + TypeScript + Vite para a UI;
- Rodio/CPAL com a cadeia Symphonia compatível para reprodução e decode;
- Lofty para tags e propriedades;
- adapters próprios para Windows SMTC e Linux MPRIS;
- NSIS customizado para o modelo de Default Apps no Windows e `.deb` no Linux.

Essa direção prioriza UX sem empacotar Chromium. Ela ainda precisa provar memória, cold start, M4A/seek, underruns do caminho Rodio/CPAL, árvore de dependências, acessibilidade, tray, instância única, associações e empacotamento no M0. Rust + Slint é o fallback de footprint; C++20 + Qt Quick/QML é a alternativa madura se o conjunto Tauri falhar em múltiplos critérios.

Nenhum ADR está aceito neste momento.

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

Por enquanto não há manifests Rust/Node nem comandos de build. Os comandos planejados e gates estão em [quality-gates.md](docs/quality/quality-gates.md) e só devem ser apresentados como executáveis após o bootstrap criar e validar os scripts correspondentes.

Validações documentais atuais:

```powershell
git diff --check
git status --short --branch
```

Links relativos, IDs de requisitos e consistência entre estados devem ser revisados em toda mudança documental.

## Próximo passo

1. revisar [estado e decisões](docs/project-status.md);
2. ajustar requisitos derivados que não combinem com o uso pessoal desejado;
3. autorizar o M0 e a hipótese Rust/Tauri/Svelte, sem aceitá-la definitivamente;
4. executar spikes separados de UI, áudio/corpus e integração Windows;
5. medir e então aceitar/rejeitar os ADRs;
6. somente depois iniciar o MVP guiado por TDD.

O roadmap deliberadamente não contém datas. O objetivo é manter o projeto pequeno, verificável e agradável de evoluir.

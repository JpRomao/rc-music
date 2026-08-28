# Music Player

Player de áudio desktop pessoal, local-first, Windows-first e planejado para Linux. O objetivo é combinar inicialização rápida e baixo consumo com uma interface moderna, minimalista e realmente agradável para uso diário.

> **Estado atual:** D0, M0.0 e M0.1 concluídos; o M0.2 possui preflight Qt validado, core/presenter determinísticos e uma jornada QML tipada vazia/tocando/erro com transporte, fila virtualizada e navegação manual anterior/próxima. Preferências, temas, alto contraste, bandeja e atalhos locais estão implementados. O pacote histórico de receita commitada `63717e5`, com MSVC 14.44, passou nos smokes offscreen original/relocado. O Qtbase 6.11.2 foi reconstruído com o backport FreeType `5a280ec`; depois, o [SDK Qt composto autenticado](docs/m0/evidence/m0-2-qt-composite-sdk.md) publicou e verificou independentemente o run `f75eb9bea8280844c6d1a2b3f4`, com 9.880 folhas/2.400.301.667 bytes. A receita `package-v2` para consumir essa identidade está implementada e congelada, mas ainda não foi executada: não há build, runtime, pacote ou smoke novo do shell contra o SDK composto. A validação nativa permanece parcial e pausada; foco, NVDA, HC/DPI, bandeja/saída nativas e `CHK-SEC` continuam pendentes. Não existe implementação de produto, shell validado integralmente, instalador, formato de áudio comprovado ou stack aceita.

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

O repositório já contém o descriptor, os schemas, o manifesto de stage e o runner autenticado do preflight Qt em `experiments/m0/provisioning/qt-shell`; toolchains/downloads grandes ficam ignorados em `out/m0`. O scaffold experimental em `experiments/m0/candidates/qt/shell` contém o core C++20 puro, o backend manual determinístico, o adapter Qt da fixture, o modelo tipado da fila e o presenter com ações, metadata atual/contextual, cenário recuperável de faixa indisponível e agendamento de 200 ms. O shell Qt Quick tipado já percorre automaticamente vazio, tocando/pausado e erro com barra persistente, seek, saltos, volume, mudo e recuperação; sua fila lateral projeta os 1.000 IDs congelados em uma `ListView` lazy com reutilização, foco/seleção acessíveis, `Ctrl+L` e `Esc`. A navegação manual anterior/próxima segue a política comum do [registro M0](docs/m0/README.md): reinício após 3 s quando há seek, busca do vizinho disponível sem circular a fila e troca em posição zero preservando o estado de reprodução, volume, mudo e modelo. Um smoke do executável real entra no erro por uma opção fechada do harness e recupera pela ação QML “Pular”, preservando a barra e o modelo da fila. Fixture, fila e SVGs ficam autenticados em recursos `qrc`. Os comandos Release validados estão no [README do candidato Qt](experiments/m0/candidates/qt/README.md). Preferências/temas, alto contraste e bandeja estão implementados no candidato; o pacote local passou no smoke offscreen original/relocado, mas bandeja/alto contraste nativos, medições externas e validações manuais ainda estão pendentes. Não há scaffold do produto na raiz.

A receita aditiva do [SDK Qt composto](experiments/m0/provisioning/qt-shell/README-composite.md) combinou e autenticou isoladamente o Qtbase reconstruído com os archives oficiais de Qt Declarative, Qt SVG e D3D para Windows SDK 10.0.26100.0. O [run comprovado](docs/m0/evidence/m0-2-qt-composite-sdk.md) contém 9.880 folhas/2.400.301.667 bytes e passou na verificação independente. A receita de consumo `package-v2` está implementada e congelada em `experiments/m0/candidates/qt/package-v2`, com comandos no [README do candidato Qt](experiments/m0/candidates/qt/README.md), mas sua execução real continua pendente. Esses contratos não comprovam build, runtime/ABI, pacote v2, smokes, CRT, PCRE2, licenças/notices/SBOM, redistribuição, `CHK-SEC`, gates nativos, fechamento de M0.2 ou aceitação de stack.

O candidato Qt implementa o painel modal Geral/Reprodução/Aparência, edição imediata dos saltos e temas/movimento com resolução do sistema. A [política comum e evidência](docs/m0/README.md) separa a fatia de preferências/temas (`ab87803`) da fatia HC: paleta do sistema com precedência temporária, estados opacos, sliders e scrollbar controlados, sem mudar a preferência guardada ou o movimento. Os checks automatizados HC passaram após as correções da auditoria independente. Paletas injetadas e escalas offscreen não comprovam alto contraste nativo, NVDA ou DPI real. Preferências ficam somente em memória, sem rollback automático de valores válidos já aplicados; sessão/fim da fila reais e FR-042/FR-043/FR-044 permanecem para o produto.

A bandeja usa o mesmo transporte do presenter e distingue fechar, ocultar e sair. O primeiro fechamento para a bandeja pede confirmação na própria janela, com opt-out somente nesta execução; falta de bandeja minimiza normalmente e deixa uma explicação disponível. O smoke `--m0-smoke=tray-lifecycle` percorre menu, aviso, ocultação/restauração, perda de capacidade e saída do executável real, offscreen, com código zero e janela fechada. A seção 8 do registro M0 reúne os testes e limites; bandeja/Explorer nativos e gates manuais continuam pendentes.

A seção 5 registra os atalhos locais e seus Reds/Greens: abertura da fixture, reprodução, saltos configurados, volume/mudo, fila, Preferências e saída. As guardas preservam foco, teclas de controles e modais, sem registro de atalhos globais. Esses incrementos estão no novo pacote MSVC 14.44, não no pacote histórico descrito abaixo.

A seção 9 do registro M0 documenta a receita local e seu histórico: origem/commit, inventário SHA-256, imports PE, `qt.conf` relativo e pré-requisitos externos. O stage histórico `tray-200b983-clean-01` possui 1.378 arquivos/82.388.347 bytes. Após o bloqueio inicial por RivaTuner, o smoke offscreen passou sem RTSS nas duas jornadas, tanto nesse stage de receita commitada quanto na cópia para um caminho com espaços e acentos. O manifesto foi validado antes/depois e os módulos amostrados vieram somente do pacote/System32, sem paths Qt do SDK. O runner não alterou aplicações externas, configuração ou whitelist. Os comandos estão no [README do candidato Qt](experiments/m0/candidates/qt/README.md). O CRT continua externo; isso não aprova máquina limpa, desktop nativo, distribuição ou encerramento de M0.2.

A [triagem de segurança](docs/m0/evidence/m0-2-security-triage.md) registra achados nas fontes vendorizadas de FreeType/PCRE2, alcance no shell ainda não demonstrado e MSVC 14.43 fora de suporte no pacote histórico. A identidade PE/SPDX foi conferida, mas `CHK-SEC` permanece aberto; nenhum componente foi atualizado ou risco aceito por essa revisão.

A [regressão standalone FreeType](docs/m0/evidence/m0-2-freetype-backport.md) reproduziu o defeito no vendor Qtbase `ef55f427`: três falhas em nove casos na árvore original e Green 9/9 numa cópia isolada com o patch oficial `5a280ec`. A árvore original de 22.856 arquivos foi preservada; somente `ttgxvar.c` difere na cópia, por nove adições e 12 remoções. O inventário Qt declara FreeType 2.14.3, enquanto os headers dessa fonte e do SDK oficial registram 2.14.2; a divergência permanece explícita. Esse Green não recompila nem corrige Qt6Gui ou qualquer pacote, não encerra `CHK-SEC`/M0.2 e não aceita redistribuição ou stack.

A [reconstrução Qtbase autenticada](docs/m0/evidence/m0-2-qtbase-refresh.md) aplicou o mesmo patch à cópia fixada do Qtbase e publicou o run privado `96bvah3m0t0t71m990olv5gpvs`: instalação com 4.289 folhas/526.177.624 bytes, 15 PEs contratuais e o consumidor direto `qminimal`, manifesto `dc26c096…afbf6b` e receipt `8a4e3c13…28619`, todos aprovados novamente por um processo verificador independente. O [passo composto posterior](docs/m0/evidence/m0-2-qt-composite-sdk.md) incorporou essa instalação a QML/Quick/Svg/D3D sem alterar o SDK oficial, o pacote `msvc-63717e5-clean-01` ou as evidências históricas. A receita `package-v2` que autentica esse consumo está implementada e congelada, sem execução real até este checkpoint; ainda não há prova de build, carregabilidade/ABI, pacote v2, smokes, PCRE2, licenças/notices/SBOM/CRT ou redistribuição.

O teste funcional verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação; não conta todo o pool de reutilização nem o total de instanciações durante a execução. Essa evidência não fecha NFR-PERF-006 nem o budget de stall de 100 ms, que depende do harness externo comparativo de M0.4. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida; a [primeira tentativa posterior autorizada](docs/m0/evidence/m0-2-native-attempt-20260827.md) teve capturas divergentes e não fechou os gates nativos/NVDA. O fim da faixa fake continua `clamp_paused`, e “Pular” no cenário de erro continua uma recuperação separada; CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem encerram FR-011/FR-016/FR-017.

Após nova autorização, a [instalação MSVC 14.44](docs/m0/evidence/m0-2-msvc-installation.md) e o build Release passaram, com contrato/receipt reais separados do histórico. A [integração e o pacote de receita commitada](docs/m0/evidence/m0-2-msvc-package.md) foram validados: `msvc-63717e5-clean-01`, com `recipe_dirty=false`, passou nas duas jornadas offscreen tanto no original quanto na cópia com espaços/acentos. A retomada nativa confirmou abertura da fixture, pausa, fila, Preferências e tema Claro, mas a evidência de foco permaneceu inconclusiva. Os inputs foram pausados após a ferramenta bloquear uma tentativa de `Esc` por interação manual; a saída do processo não foi observada. Nenhum gate NVDA, HC/DPI, bandeja/saída nativas ou segurança foi encerrado por esses resultados.

Validações atuais:

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\validate-docs.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Invoke-M0HarnessTests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Fixture.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Statistics.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Markers.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Evidence.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Visual.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Process.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Summary.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Layout.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\tests\QtShellToolchain.Tests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\Test-M0QtShellToolchain.ps1 -RepositoryRoot (Resolve-Path .).Path -ExpectedContractSha256 ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\tests\QtShellComposite.Tests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\tests\QtShellCompositeNative.Tests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\tests\QtShellCompositeLauncher.Tests.ps1
git status --short --branch
```

Uma publicação manual do piloto sintético usa um processo limpo, um root novo e vazio sob `out/m0` e IDs novos:

```powershell
$m0Out = Join-Path (Resolve-Path .) 'out\m0'
New-Item -ItemType Directory -Path $m0Out -Force | Out-Null
$pilotRoot = Join-Path $m0Out 'pilot-manual-01'
New-Item -ItemType Directory -Path $pilotRoot -ErrorAction Stop | Out-Null
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Invoke-M0Pilot.ps1 -EvidenceRoot $pilotRoot -ExperimentId exp-pilot-local -RunId run-P-local-01
```

O validador documental cobre links relativos, IDs/matriz de requisitos, checks, etapas, estados ADR e whitespace do diff. Os runners M0 validam somente os contratos materializados até aqui. `Invoke-M0Pilot.ps1` cria no Windows uma árvore sintética sem áudio, fecha o Job, publica Evidence recuperável e um summary separado com role `synthetic_pilot`; `P` é sempre não decisório e nunca entra no conjunto A/B. O preflight Qt valida bytes/proveniência, stage isolado e capabilities QML, mas não comprova shell, bundle, acessibilidade manual, Linux ou desempenho. O status confirma que a mudança terminou no commit esperado.

## Próximo passo

1. executar a receita `package-v2` já congelada para configurar e construir o shell exclusivamente contra o SDK composto autenticado e Windows SDK 10.0.26100.0, publicar o pacote e repetir os smokes original/relocado; resolver as pendências de segurança e retomar os gates nativos/manuais de M0.2 quando a interação estiver disponível e puder produzir evidência verificável;
2. executar M0.3 com o shell Rust/Tauri equivalente, sem áudio ou rede reais;
3. medir os dois shells em M0.4 e somente depois continuar as comparações de áudio e aceitar/rejeitar os ADRs;
4. iniciar o MVP guiado por TDD apenas depois da decisão M0.10.

O roadmap deliberadamente não contém datas. O objetivo é manter o projeto pequeno, verificável e agradável de evoluir.

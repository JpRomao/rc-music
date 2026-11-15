# Fundação nativa Aurora — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for native execution of this bounded slice. Steps use checkbox (`- [x]`) syntax for tracking. Apply the project's proportional-validation policy rather than generic repeated full-suite defaults.

**Goal:** Entregar uma primeira casca Qt/QML funcional de Aurora: sem integração Lidarr, temas aceitos, navegação Agora/Fila e transporte persistente, preservando os recursos existentes.

**Architecture:** Manter uma ApplicationWindow, presenter/backend/owners e stores. Alterar a composição visual e os defaults de aparência do produto; preservar fixtures M0 e suas expectativas históricas. Playlists/Downloads/Preferências continuam painéis reais acionados pela lateral, explicitamente ainda não páginas migradas.

**Tech Stack:** C++20, Qt 6.11.2 Quick/QML/Qt Test/Quick Test, CMake/Ninja/MSVC14.44, Qt Multimedia/FFmpeg e TagLib existentes. Sem dependências novas.

**Spec:** [Contrato visual aprovado](../specs/2026-11-15-aurora-shell-design.md).

## Global Constraints

- Origem de execução: checkout isolado `out/product/brave-letras`, branch `feat/brave-letras-mp3`, base `4efd8bad38cdfeb3682a92d8e8b9894f9d84e33b`; preservar WIP do checkout principal.
- O usuário aprovou Aurora, cores claro/escuro e prosseguimento; detalhes técnicos desta entrega são decisões do executor. Executar nativamente nesta sessão, com uma revisão independente agrupada ao final.
- Não alterar áudio, providers, schemas, Sandbox/DNS/TLS/ACKs/pins, downloads OFF ou arquivos pessoais. Sem instalação, Registro, UAC, push, merge ou reinício do app pessoal.
- Janela mínima 720×520; controles >=40px, preferidos44px; Sistema/Claro/Escuro, HC e reduced motion permanecem. Tema atual não é sobrescrito pelo dark inicial do HTML.
- Biblioteca permanente e busca online são frentes posteriores. Fila não recebe o nome Biblioteca. Não introduzir páginas falsas.
- Fixtures autenticadas, recursos históricos e seus hashes permanecem. Alterações intencionais do contrato visual de produto recebem expectativas próprias, sem enfraquecer oráculos funcionais.
- Fronteira test/prod e plugins/ABI continuam separados. Nenhum hook de teste novo na aplicação real.
- Seguir a política de commits do projeto somente dentro do escopo autorizado; não incluir WIP/arquivos ignorados, não publicar.

## Review Focus

1. Troca de aparência ou Agora/Fila com áudio ativo: mesmo ID/posição/volume/fila e nenhuma chamada extra de load/play/pause/seek. Testar no Task3.
2. Respostas tardias de seleção/locate e troca automática em página diferente: foco não regressa a item oculto ou sai de modal. Preservar/rodar consumidores de foco no Task3.
3. Fechamento com playlists/downloads não salvos: acesso à recuperação permanece; retirar apenas o participante Lidarr da drenagem. Testar consumidores de lifecycle no Task1/3.
4. Alto contraste, tema Sistema e conteúdo longo em720×520: cores do sistema prevalecem e controles continuam alcançáveis, sem overlay da fila sobre o player. Testar no Task2/3.
5. Root e grafo de produção: tipo/recurso/biblioteca Lidarr ausentes e sem macros/cenários no produto, mesmo com BUILD_TESTING ON. Testar no Task1 e auditar QRCs no Task4.

## Fechamento da primeira frente

Os passos abaixo foram exercitados no checkpoint nativo e na retomada i18n, com evidências anteriores reconciliadas, sem repetir a suíte global. O [relatório de entrega](../../delivery/aurora-native-foundation-20261115.md) registra os comandos, Reds/Greens, limites e a revisão independente do delta. A formalização ocorre no commit local que incorpora este plano; isso não conclui o plano diretor de onze fases, o aceite humano Qt, instalação ou release. O empacotamento privado autorizado é uma operação posterior, vinculada ao commit real e com recibos próprios.

## Ambiente de execução e evidência

Árvore ON confirmada: `out/product/dl-settings-tests` (Ninja/Release/origem deste checkout/Qt Network SDK qualificado). O target contract está registrado mas ainda não construído; não executar binário ausente como se fosse Red. MSVC disponível via `Microsoft.VisualStudio.DevShell.dll` do VS18 BuildTools, argumentos `-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0`.

Antes de compilar, reproduzir o preflight de SDK do runner existente. O coordenador é dono exclusivo desta árvore. Logs/recibos ficam no workspace desta execução, sem regenerar evidência congelada. Chamadas diretas Qt reproduzem o ENVIRONMENT registrado: offscreen/software/Basic, logging console/stderr, warnings fatais, fontes Windows e PATH do Qt correto.

## Task 1: Retirar a capacidade Lidarr integralmente

**Files:** `cmake/RcMusicUi.cmake`, `src/app/main.cpp`, `src/ui/qml/Main.qml`, `tests/ui/CMakeLists.txt`, `tests/ui/quick/tst_playlists.qml`, `tests/cmake/UiBuildBoundaryTests.cmake`; remover cinco fontes exclusivas e três testes exclusivos inventariados no mapa R1. Atualizar requisitos/ADR/status/manual/rastreabilidade que os referenciam.

**Interfaces:**
- Consumes: composição atual e gate `PlayerPresenter::setSourceExitDrainedProbe`; `Qt6::Network`, lyrics/playlists/downloads permanecem.
- Produces: módulo/Main sem propriedade/tipo/ação Lidarr e fechamento sem esse participante. Demais funções `setQueueOpen`, `openPlaylists`, `openDownloads`, `openPreferences` preservadas.

- [x] Escrever regressão de ausência na árvore QML/tipo registrado e no command closure de `m0_qt_shell`/`rc_music_ui_quick_tests`; a prova deve exercitar registro/instanciação ou grafo real, não comparar texto do fonte.
- [x] Construir o alvo necessário e executar o recorte; Expected: falha pela capacidade Lidarr ainda presente, não por ferramentas/target ausente.
- [x] Retirar `QtLidarrService`, `LidarrBinding`, `LidarrPanel`, instâncias, includes, propriedade inicial, lifecycle e referências de foco/ancoragem; excluir target/linkage e testes exclusivos. Manter fronteiras/guardas e todos os outros participantes.
- [x] Adaptar o consumidor de Playlists preservando visibilidade, limites e não sobreposição; retirar apenas seu fake Lidarr.
- [x] Preservar FR-055 como `fora de escopo`/`documentary`/`tests: []`, com decisão em docs e justificativa; manter inventário de IDs e alinhar o plano/ADRs. Marcar guia/checkpoints antigos como históricos, sem apagar evidência.
- [x] Regenerar o build afetado e executar ausência + consumidores Playlists + build-boundary; Expected: zero falhas, sem dependência ativa Lidarr. Atualizar docs/traceabilidade e validar os links/markers alterados. Commit local coerente segundo política aplicável.

## Task 2: Aplicar a aparência Aurora ao contrato nativo

**Files:** `src/presentation/ShellUiContract.hpp/.cpp`, `tests/ui/qtest/ShellUiContractTests.cpp`, `tests/ui/quick/quick_main.cpp`; novo catálogo `src/ui/qml/NavigationText.qml` e componente de troca rápida quando necessários ao Task3.

**Interfaces:**
- Consumes: `ShellUiContract::updateAppearance(ThemeMode, MotionMode, bool, bool, optional<HighContrastPalette>)`, os papéis existentes e `PlayerPresenter::setThemePreference(ThemePreference)`.
- Produces: aparência Aurora no construtor de produto; construtor `FixtureBundle` continua carregando paletas históricas. Novos testes podem receber contexto `testProductUiContract` ligado ao presenter local, sem modificar fixtures ou expor hooks ao produto.

- [x] Escrever asserts de consumo do novo perfil: paletas próprias nas trocas Sistema/Claro/Escuro, mesmo comportamento de notificações/HC/motion e constantes não visuais preservadas. Remapear deliberadamente os antigos testes de equivalência integral produto/M0 para esses contratos separados.
- [x] Executar os slots novos/alterados; Expected: Red por cores antigas do produto.
- [x] Aplicar tokens aceitos da Spec. `borderColor` funcional usa contorno de controle, não a divisória decorativa de baixo contraste; sucesso mantém semântica/contraste próprios. Não alterar o construtor histórico nem dados autenticados.
- [x] Integrar a troca rápida pela preferência existente (Enter/click; Espaço mantém playback), sem alterar store/esquema/default persistido. Strings visíveis vêm de catálogo `*Text.qml` e são texto simples.
- [x] Executar contract e os consumidores de aparência/HC/contraste relevantes; Expected: mesmos contratos históricos e matriz nativa Aurora verdes. Conferir formato só dos C++/QML alterados.

## Task 3: Casca persistente com Agora e Fila reais

**Files:** `src/ui/qml/Main.qml`, novo `src/ui/qml/SideNavigation.qml`, `NavigationText.qml`, `NowPlayingState.qml` se ajuste adaptativo necessário, `cmake/RcMusicUi.cmake`, novos testes `tests/ui/quick/tst_aurora_shell.qml`; adaptar testes de geometria realmente afetados.

**Interfaces:**
- Consumes: `Main.setQueueOpen(bool)`, `toggleQueue`, `openPlaylists`, `openDownloads`, `openPreferences`, `openTrackDetails`, `QueuePanel` FocusScope e `TransportBar` existentes.
- Produces: SideNavigation recebe contract/estados/capacidades e emite intenções; expõe aliases de controles quando Main precisa restaurar foco. Main conserva os objectNames públicos e um único QueuePanel/TransportBar/presenter.
- Destinos centrais: Agora é o estado de reprodução real; Fila é o QueuePanel real embutido. Ações auxiliares abrem painéis atuais. Não inventar uma biblioteca com dados da fila.

- [x] Escrever Quick cobrindo sidebar, ida/volta Agora/Fila, persistência da instância do transporte, contador de áudio inalterado, theme toggle, IDs e estado preservados, ausência Lidarr e ações auxiliares.
- [x] Executar apenas `tst_aurora_shell.qml` com o ambiente do CTest; Expected: Red pelos elementos/navegação ausentes na casca anterior.
- [x] Publicar SideNavigation/NavigationText antes de adicioná-los ao CMake. Reorganizar a janela em lateral + centro e rodapé fixo; manter bindings/owners acima dos destinos. Reusar transporte inteiro sem perder skip, seek, modos e volume.
- [x] Atualizar geometria da fila/drop e restauração de foco; não manter atalhos bloqueados permanentemente pela mera visibilidade de uma página. Modais continuam com suas proteções atuais.
- [x] Adaptar casos de layout deliberadamente mudados: menu antigo horizontal vira lateral, fila antes overlay vira conteúdo central. Manter asserts semânticos, alvos, bounds, foco, seleção e ordem de ocorrências; não só apagar asserts para obter Green.
- [x] Exercitar mínimo720×520, tamanho maior, texto longo, HC/reduced motion, saída recuperável e consumidores de foco/seleção/drop. Expected: transporte visível e utilizável, fila não sobrepõe player e navegação não toca áudio.

## Task 4: Fechamento do marco nativo

**Files:** documentação de estado/entrega e evidência isolada; sem mudar fontes por hábito.

- [x] Após a última alteração relevante, compilar `rc_music_ui_contract_tests rc_music_ui_quick_tests rc_music_ui_presenter_tests m0_qt_shell` e lint de produção/teste; registrar quais targets realmente existem no cache.
- [x] Executar uma regressão UI agrupada: contract/presenter/quick/build-boundary e os processos de startup/fechamento realmente afetados. Não rodar todo o CTest global de rede/conversão/core não alterados. Se o recorte falhar, reproduzir/corrigir focadamente antes de decidir a nova ampla necessária.
- [x] Capturar renderização nativa com presenter/áudio falsos e configuração isolada. Não relançar aplicativo pessoal nem apresentar imagem HTML como Qt. Conferir QRCs e grafo alcançável para ausência Lidarr/cenários no produto.
- [x] Revisão independente única do diff contra a Spec; corrigir Critical/Important com Red→Green e consumidores afetados, registrar minors. Não duplicar as mesmas suítes entre implementador/revisor.
- [x] Documentar fronteira entregue, comandos/resultados/contagens reais, falhas e pendências. Aceite visual nativo humano, NVDA, DPI físico, provider, biblioteca durável, instalação e release continuam separados.
- [x] Revisar diff/Git e fechar commits locais coerentes apenas no escopo permitido. Não merge/push/instalação.

# Roadmap

**Estado:** proposta de ordem e escopo; sem compromisso de datas  
**Última revisão:** 2026-09-01

## 1. Como ler

- **Concluído:** entregável presente e validado dentro do escopo documental.
- **Próximo:** pronto para iniciar após decisão/autorização necessária.
- **Planejado:** depende de marcos anteriores.
- **Opcional:** só entra após uso real justificar.
- **Pesquisa:** pode resultar em rejeição, não é promessa de feature.

Cada marco termina em commits verdes, documentação atualizada e demonstração do critério de saída. Datas serão definidas somente se o mantenedor desejar; ordem e gates são mais importantes para este projeto pessoal.

Os incrementos, checks e evidências de cada passo estão no [plano de desenvolvimento executável](development-plan.md). Este arquivo permanece como visão macro de escopo e dependências.

## 2. Visão dos marcos

```text
D0 Documentação
      │
      ▼
M0 Spikes A/B Qt ↔ Tauri ── nenhum atende ─► revisar/Slint
      │ escolher e aprovar
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

**Estado:** Concluído em D0.9; baseline autorizado apenas para o M0.

### Entregas

- contrato operacional em [agents.md](../agents.md);
- visão, requisitos rastreáveis e UX;
- análise Rust/C++ e recomendação de stack;
- arquitetura, fronteiras e ADRs propostos;
- estratégia TDD, corpus e gates;
- plano executável com checks e rastreabilidade;
- roadmap, segurança, workflow e processo de release;
- README como índice e estado do projeto.

### Gate de saída — alcançado

- links e estrutura verificados;
- nenhuma proposta apresentada como decisão aceita;
- todos os arquivos commitados;
- mantenedor revisou os itens confirmados, derivados e em aberto sem aceitar antecipadamente uma stack.

## 4. M0 — Spikes de decisão

**Estado:** Em andamento; M0.0 e M0.1 concluídos; M0.2 permanece aberto e seus trabalhos pesados estão suspensos; M0.5 possui 12 fixtures sintéticas autenticadas; M0.6 cobre lifecycle/eventos tipados e geração isolada, `PB-001`/`PB-006` para WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis e `PB-004` para MP3 VBR/M4A AAC via Qt Multimedia, além de `MR-001`, `MR-003`, `MR-018`, `MR-019` e `MR-101`–`MR-103` no TagLib com `MR-018` aplicado à matriz básica por conteúdo; Opus/ALAC recebem `UnsupportedFormat` somente numa caracterização separada do extrator, sem rejeição ponta a ponta. O reader assíncrono continua limitado, e a integração aplicação/QML percorre nove linhas válidas sobre sete fixtures, incluindo aliases `.mp4`/`.oga`, com seleção injetada na fronteira do seletor de sete extensões em caixa baixa/alta, fallback Unicode, autoplay, play/pause, seek e metadata assíncrona. Quatro regressões adicionais derivam prefixos temporários de 64 bytes de MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis e cobrem erro tipado, contexto Unicode/reabertura, recuperação e descarte de geração antiga; elas não são fixtures canônicas. M0.5/M0.6, áudio audível, automação nativa, comportamento ponta a ponta dos candidatos, hostis canônicos restantes, arte/limites, gates nativos/manuais, `CHK-SEC` e decisão de stack permanecem pendentes.

**Objetivo:** eliminar riscos estruturais antes de construir o produto.

Este roadmap usa rótulos macro `M0-A` a `M0-D`. O plano executável detalha o mesmo trabalho como M0.0–M0.10: `M0-A` = M0.0–M0.4, `M0-B` = M0.5–M0.8, `M0-C` = M0.9 e `M0-D` = M0.10. Os dois namespaces não devem ser intercambiados em evidências.

Ambiente, protocolo e layout foram congelados em [M0-E1/M0-P1/M0-L1](m0/README.md). O [preflight Qt](m0/evidence/m0-2-preflight.md) fechou a toolchain necessária ao início de M0.2; Qt Multimedia e TagLib já foram preparados em raízes isoladas para os primeiros slices M0.6, e provisionamentos de etapas posteriores continuam bloqueando somente seus checks dependentes.

M0.1 fechou a fixture e o harness comum em [`24d12ea`](m0/evidence/m0-1.md), incluindo o piloto sintético `P` não decisório. O preflight M0.2 registrou Qt 6.11.2 Desktop MSVC 2022 x64, ferramentas QML, `clang-format` e NVDA em contrato autenticado. Os incrementos TDD atuais incluem o core C++20 puro, o backend manual determinístico, o loader Qt ancorado no manifesto, o modelo tipado da fila comum, o presenter com ações de transporte, preferências e timer de 200 ms e a jornada QML vazia/tocando/erro com transporte, fila lateral virtualizada de 1.000 itens e navegação manual anterior/próxima. Dados e SVGs são recursos autenticados e relocáveis; preferências/temas, alto contraste e bandeja estão implementados no candidato Qt. O pacote histórico passou no smoke offscreen original/relocado; depois, o [Qtbase corrigido](m0/evidence/m0-2-qtbase-refresh.md) foi reconstruído e o [SDK composto](m0/evidence/m0-2-qt-composite-sdk.md) o combinou com QML/Quick/Svg/D3D. O run `f75eb9bea8280844c6d1a2b3f4`, com 9.880 folhas/2.400.301.667 bytes, passou na verificação independente, mas ainda não foi usado em build, runtime ou package v2 do shell. Verificações nativas/manuais e `CHK-SEC` continuam pendentes. A [política experimental de navegação](m0/README.md) é comum a M0.2/M0.3, sem mudança da fixture/protocolo: reinício após 3 s com seek, vizinho disponível sem circular, troca em zero preservando reprodução/volume/mudo/modelo e comandos inertes em vazio/carregando/erro/seek ocupado. EOF mantém `clamp_paused`; a recuperação do cenário de erro fica separada. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

A fatia de preferências/temas foi fechada em `ab87803`, naquele marco ainda sem persistência, sessão ou fim da fila reais. A [política comum e evidência](m0/README.md) separa esse histórico da fatia HC, implementada com precedência temporária da paleta do sistema, pares opacos e movimento independente. Os checks automatizados HC passaram após as correções da auditoria independente. Paletas injetadas e escalas offscreen não substituem alto contraste nativo, DPI real, NVDA ou validação manual, nem encerram UX-006.

O recorte M3.5 posterior tornou funcional no candidato Qt/Windows o armazenamento das dez preferências aprovadas: JSON `schemaVersion: 1` limitado a 64 KiB, gravação atômica com backup, recuperação de documento inválido, preservação de versão futura, debounce de 500 ms e writer único fora da thread visual. A saída pendente possui deadline visual de 2 s, mas não cancela fisicamente a chamada síncrona já iniciada; o shutdown ainda pode aguardar `join()` sem limite. O restart automatizado é Release instrumentado com `BUILD_TESTING=ON` e offscreen, não pacote ou desktop nativo. `restoreLastSession` e `startMinimized` apenas fazem round-trip; seus efeitos ficam em M4.1/M4.4. Não houve migração histórica, ensaio de perda real de energia, validação de primeiro frame nativo/NVDA, pacote nem benchmark. Assim, M3.5 não está concluído globalmente.

M3.6 também está funcional somente no candidato Qt/Windows. Strings essenciais passam por catálogos QML `*Text.qml` ou pelo catálogo C++ do filtro nativo; pt-BR é compilado com `qt_add_translations`, instalado por `QTranslator` e cobre o singular “1 segundo”. Erros locais apresentam causa segura e ação concreta, rejeição síncrona preserva fila/contexto, importação parcial ou rejeitada oferece nova escolha e a acessibilidade da fila não expõe IDs/códigos internos. A automação cobre cópia longa em 720 × 520, mas não substitui NVDA, DPI, alto contraste ou movimento reduzido nativos nem pacote/E2E.

M3.7 está funcional no recorte automatizável do mesmo candidato: teclado cobre a jornada até a confirmação de limpeza; o progresso ativo sem seek possui ToolTip e as indisponibilidades são descritas de forma acessível; foco sobrevive a estados, transporte e modais; a árvore acessível nativa do Qt é inspecionada; anúncios de loading/faixa/erro/seek são deduplicados e coordenados com importação; foco primário cumpre contraste ≥ 3:1; e movimento reduzido preserva status sem animação contínua. O fechamento passou 174/174 no combinado e Quick 90/90 nas três escalas. NVDA, DPI/alto contraste/movimento reduzido nativos e pacote/E2E permanecem pendentes. M3.8/benchmarks fica adiado; M4.1 é o próximo incremento funcional. Linux e A/B continuam suspensos, sem aceitar a stack.

A bandeja está implementada sob a política comum da seção 8: menu, intenções separadas de fechar/ocultar/sair, aviso inicial por execução, fallback explicado e restauração do modo da janela. A suíte Release estrita passou 65/65 e o smoke do executável encerra normalmente com a janela QML fechada. Regressões cobrem animações/foco e perda de capacidade durante a ocultação. Isso não comprova bandeja/Explorer nativos nem encerra FR-030/FR-031/FR-032 ou M0.2; gates nativos/manuais continuam pendentes.

O pacote de avaliação local da seção 9 já tem receita, inventário exato, imports PE e relocação de arquivos verificados. O bloqueio inicial por `RTSSHooks64.dll` foi superado após uma consulta encontrar zero processos RTSS: as duas jornadas offscreen passaram no stage de receita commitada e na cópia com espaços/acentos, sem alterar a whitelist ou aplicações externas. A evidência não aprova desktop nativo, máquina limpa, segurança ou distribuição. M0.2 continua em andamento, sem retomada de Computer Use e sem início de M0.3.

O rebuild Qtbase v2 e o SDK composto preservaram a árvore/contrato v1, o SDK oficial e todos os stages históricos. O [run composto autenticado](m0/evidence/m0-2-qt-composite-sdk.md) contém 9.880 folhas/2.400.301.667 bytes, sem colisões ou fallback, e passou na verificação independente. O build Release exclusivo com essa identidade, package v2 e smokes original/relocado continuam pendentes e suspensos enquanto a fundação funcional de áudio avança. Build/runtime/ABI, CRT, PCRE2, licenças/notices/SBOM, redistribuição, `CHK-SEC`, gates nativos, M0.2 e decisão de stack permanecem sem aprovação.

O teste da fila verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação, não o pool completo nem as instanciações ao longo da execução. Essa prova funcional não fecha NFR-PERF-006 nem substitui a medição externa do stall de 100 ms em M0.4. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida; nenhuma nova verificação manual é alegada para a navegação, e acessibilidade nativa/NVDA continua pendente. Nenhuma coleta comparativa A/B foi executada e nenhuma stack ou engine foi aceita.

O candidato Qt combinado agora compõe o presenter com Qt Multimedia e o reader TagLib assíncrono. O modo real oferece `.mp3/.MP3`, `.m4a/.M4A`, `.mp4/.MP4`, `.flac/.FLAC`, `.ogg/.OGG`, `.oga/.OGA` e `.wav/.WAV` no seletor nativo, mostra imediatamente o stem Unicode como fallback, inicia metadata fora do caller e, na abertura inicial com intenção `Autoplay`, tenta tocar ao receber `Ready` da geração atual. O teste integrado percorre nove linhas sobre as sete fixtures autenticadas da matriz básica em path Unicode, incluindo aliases `.mp4` e `.oga`, com saída mutada e seleção do arquivo injetada no boundary; metadata termina sem erro e as linhas com tags publicam o título Unicode embutido. Um caso adicional seleciona dois arquivos juntos e navega até `Playing` na geração 2 do backend Qt real. A caracterização `UnsupportedFormat` de Opus/ALAC pertence somente ao extrator: playback inicia independentemente e o Qt pode decodificar candidatos escolhidos via `.m4a` ou “Todos os arquivos”, portanto o comportamento ponta a ponta permanece sem classificação. Isso prova a jornada básica sem bloquear o event loop, mas não automatiza o diálogo visual, não prova áudio audível/dispositivo físico, não publica suporte de formato e não conclui M0-B/M0.6. Empacotamento, A/B, automação nativa e benchmarks continuam suspensos/pendentes por decisão do mantenedor.

O seletor real agora usa multiseleção e entrega todos os paths ao core. A fila local preserva ordem e duplicatas por ocorrência, começa pela primeira escolha e usa um modelo Qt próprio com os mesmos papéis visuais da fixture, sem alterar o protocolo congelado. Abertura rejeitada não substitui a fila ativa; metadata atualiza a linha corrente; anterior/próxima percorrem a fila real sem resetar o modelo. Quando a origem não estava `Playing`, o destino fica `Ready`, sem comando `play()` ou `pause()`, e rejeições transitórias preservam o alvo. `Ended` atual avança sem circular com autoplay; no tail, as duas opções em memória mantêm ou limpam a seleção. Erros de origem marcam somente o ID corrente e tentam continuar preservando reprodução/pausa; eventos antigos são inertes. A lista também toca uma ocorrência disponível por ID estável e CRUD/reordenação atualizam o modelo incrementalmente. Drag-and-drop aceita arquivos/diretórios, calcula inserção sobre a fila e usa descoberta recursiva em worker único com batches, backpressure, cancelamento e isolamento de reparse/TOCTOU no Windows. Uma caracterização com adapters reais percorre válida A → inválida → válida B: observa `Ended` nas gerações 1/3, `InvalidMedia` na 2, pausa B no tail e não cria quarta carga nem reset. Ainda faltam o gate externo de responsividade de M3.2, persistência, automação do diálogo e validação NVDA/manual; FR-001/FR-003/FR-006/FR-011/FR-016 e M2.1/M3.1/M3.2/M3.3 continuam parciais.

Na jornada de falha, o presenter mantém o título/contexto Unicode da seleção local e oferece reabrir. O adapter rejeita como mídia inválida um M4A truncado que o Qt chega a carregar sem faixa de áudio, após uma confirmação curta que evita consumir o autoplay por ordem transitória dos sinais. Metadata, playback e integração exercitam quatro prefixos temporários de 64 bytes derivados das fontes autenticadas MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis, observando erro tipado, recuperação com a fonte válida e descarte de callbacks/resultados antigos. O primeiro Green de playback-lifecycle e integração passou **2/2**, respectivamente em **5,21 s** e **1,42 s**; após o build dos cinco alvos relevantes e a correção da corrida, o CTest focado completo passou **17/17 em 11,87 s**. Os prefixos não entram no manifesto/corpus canônico; nenhum benchmark, A/B ou empacotamento foi executado nesse incremento.

### M0-A — UI e shell A/B

Construir dois shells descartáveis em Release, C++/Qt Quick e Rust/Tauri/Svelte, com os mesmos assets, dados e roteiro. Não usar áudio nem rede reais nesta etapa.

O recorte comum contém:

- janela 960 × 640 e estados vazio/tocando/erro;
- player bar com slider acessível e seek preview;
- snapshots falsos a 5 Hz com interpolação visual local;
- fila com 1.000 itens, painel de preferências e temas sistema/claro/escuro;
- teclado, foco, alto contraste, 100/150/200% e movimento reduzido;
- tray e close/restore básicos;
- build Release e pacote de teste.

O [protocolo M0-P1](m0/protocol.md) versiona fixture, roteiro, marcador externo, baseline, 22 tentativas planejadas, descarte máximo, estatística e tolerâncias. Primeira execução, process-cold e warm ficam separados. Private working set/private bytes e CPU cobrem a árvore completa; memória em 5 s/60 s, pico, resposta de entrada e stall da fila são registrados sem contar páginas compartilhadas como métrica primária. Tamanho instalado e pré-requisitos offline são métricas separadas.

Executar nos dois candidatos a mesma jornada NVDA/teclado, high contrast, movimento reduzido, escala e smoke Linux. No Tauri, revisar CSP/capabilities e a árvore WebView2; no Qt, revisar nomes/roles acessíveis, bindings/animations contínuos e plugins distribuídos.

### M0-B — Áudio e metadata A/B

Usando harness mínimo e o mesmo corpus:

O corpus atual já congela o catálogo canônico, a fundação WAV/bordas e oito fixtures de codecs sintéticos com tags Unicode, mas não publica formato suportado. Os prefixos truncados de 64 bytes usados nas regressões atuais são derivados temporários e não ampliam esse catálogo. Arte, limites, fixtures canônicas truncadas por contêiner e o adapter desafiante permanecem trabalho de M0.5–M0.8.

No ramo Qt, a vertical real usa esse corpus para percorrer autoplay, seek, pausa e polling de metadata com saída mutada em nove linhas sobre sete fixtures básicas autenticadas: WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis, mais os aliases `.mp4` e `.oga`. O backend cobre `PB-001`/`PB-006` nessa matriz e `PB-004` em MP3 VBR/M4A AAC, usando tolerâncias versionadas de 0 ms para WAV, 20 ms para FLAC/Vorbis, 25 ms para AAC e 50 ms para MP3. Separadamente, TagLib lê a matriz básica por conteúdo, preserva tags Unicode, aceita stream MP3 sem tag sob extensão `.audio` com fallback pelo stem e rejeita entradas truncadas/aleatórias com erro tipado. Prefixos temporários de 64 bytes dos quatro codecs básicos acrescentam regressões de falha/recuperação e geração sem assumir o papel de fixtures canônicas. A caracterização `UnsupportedFormat` de Opus/ALAC fica fora de `MR-018` e não classifica o fluxo integrado, porque playback é independente. Essa evidência reduz o risco funcional, mas não publica matriz de suporte nem substitui áudio audível, candidatos/hostis canônicos restantes, limites de arte/metadata, medições, soak ou comparação com o ramo desafiante.

- Qt Multimedia/FFmpeg + TagLib e Rodio/CPAL/Symphonia + Lofty satisfazem a mesma suite `PlaybackBackend`/`MetadataReader`;
- cada fixture abre, toca, pausa, busca e termina, com duração e capacidades registradas;
- posição/seek são medidos em MP3 CBR/VBR e M4A/AAC;
- open-to-audible, seek-to-audible, CPU, memória e underruns usam o mesmo método;
- cancelamento e troca rápida não publicam estado antigo;
- fixtures corrompidas/hostis retornam erro controlado sem derrubar processo;
- sessão curta, soak exploratório e armazenamento lento controlado verificam handles, crescimento e stalls;
- o ramo Qt registra módulos/plugins, configuração e licença do FFmpeg e obrigações Qt/TagLib;
- o ramo Rust registra `cargo tree -d -e features` e confirma uma cadeia Symphonia deliberada, sem duplicação acidental.

Publicar a primeira matriz de formatos por candidato. Somente caminhos viáveis avançam; o produto final não empacota os dois engines.

Sessões curtas eliminam problemas cedo, mas cada ramo ainda viável executa o soak de oito horas definido no ADR-0002 antes da decisão M0-D. O aplicativo integrado repete o soak em M5.

### M0-C — Integração e pacote comparáveis

Provar, para cada candidato ainda viável:

- instância única e encaminhamento de vários paths Unicode;
- tray incluindo saída real;
- pacote instalável/desinstalável em ambiente descartável;
- build/smoke na distribuição Linux de referência;
- caminho demonstrável para SMTC/MPRIS e associações sem vazar plataforma para o domínio.

No candidato finalista, usar NSIS como baseline experimental para completar a prova com ProgID, `Capabilities`, `RegisteredApplications`, tela oficial de apps padrão, SMTC/teclas de mídia, suspend/resume, reinício do Explorer, upgrade e uninstall. A tecnologia final do instalador só é aceita após a evidência. Se esse aprofundamento revelar custo estrutural, executar a mesma prova no outro candidato antes da decisão.

### M0-D — Decisão

Combinar UI e áudio somente nos candidatos viáveis, reexecutar os budgets e comparar com a [análise de stack](architecture/stack-analysis.md):

- aceitar ADR-0001/0002 e registrar medidas, ambiente, tolerâncias, licenças e riscos; ou
- declarar empate de medição e escolher por UX comprovada, acessibilidade, manutenção pessoal, segurança e licenciamento; ou
- considerar spike Slint equivalente se nenhum candidato atender por footprint da UI; ou
- revisar backend, requisito ou orçamento com o mantenedor quando nenhum caminho for viável.

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
7. boundary UI/aplicação com validação;
8. shell UI consumindo fake.

### Entregas

- manifests/lockfiles/toolchain pinados;
- scripts `format`, `lint`, `check`, `test`, `build`, `validate`;
- CI Windows/Linux básica;
- cobertura do core;
- configuração de segurança e privilégios própria da stack aceita;
- fixtures iniciais e manifesto;
- primeiro executável ainda sem alegação de player completo.

### Gate de saída

- ciclo Red–Green–Refactor evidenciado pelo teste e motivo de falha registrados no handoff; commits finais permanecem verdes;
- domínio roda sem toolkit de UI ou backend de áudio concreto;
- UI roda contra fake;
- gates rápidos verdes em Windows e Linux;
- arquitetura real corresponde aos documentos ou ADR é atualizado.

## 6. M2 — Vertical slice de reprodução local

**Estado:** Planejado.  
**Objetivo:** abrir um arquivo real e completar a jornada principal.

### Escopo

- seletor nativo com multiseleção e abertura por path;
- load async, play/pause, duração e posição;
- seek clicável/arrastável;
- saltos padrão ±5 s e validação dos valores; a persistência real fecha em M3;
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

- critérios de abertura/playback/seek/metadata de M2 demonstrados; FR-013 só fecha após persistência em M3 e FR-022 após o painel visual;
- nenhum I/O pesado bloqueia UI;
- formatos publicados refletem apenas testes verdes;
- instalável de preview interno no Windows.

## 7. M3 — Experiência completa do player

**Estado:** Em andamento no candidato Qt/Windows; fila/CRUD, importação incremental, painel de detalhes/diagnóstico, armazenamento M3.5, strings/microcopy M3.6 e acessibilidade automatizável M3.7 estão funcionais. Permanecem abertos os efeitos de restauração/início minimizado, desempenho, `CHK-SEC`, E2E empacotado e validação nativa/NVDA; M4.1 é o próximo incremento funcional e M3.8 permanece adiado.
**Objetivo:** transformar o vertical slice em produto agradável.

### Escopo

- fila adicionar/tocar agora/reordenar/remover/limpar;
- anterior/próxima e fim da fila;
- drag-and-drop de arquivos/diretórios;
- painel de detalhes técnicos;
- preferências atômicas do MVP para tema, movimento, volume, mudo, saltos, fechar, restauração, início minimizado e fim da fila;
- temas sistema/claro/escuro;
- strings centralizadas em pt-BR, preparadas para tradução;
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

**Estado:** Em andamento no candidato C++/Qt/Windows; snapshot, porta, codec v1, store Windows atômico e writer/autosave do core de M4.1 estão verdes, sem ainda concluir bootstrap, integração, restauração ou E2E.
**Objetivo:** tornar o player um cidadão confiável do Windows e suportável no Linux.

O primeiro recorte de M4.1 adiciona snapshot validado de fila, ocorrência atual e posição, com restauração transacional que preserva IDs/duplicatas e revalida disponibilidade em runtime. O recorte seguinte materializa `SessionStore` separado e o codec JSON v1 com inteiros exatos, paths Unicode, versão futura e limite de 16 MiB. O terceiro implementa `session.json`/`.bak` no Windows com replace atômico, lock cooperativo, recuperação sem reescrita, backup byte a byte, falha fechada para arquivos ilegíveis e substituição futura somente explícita.

O quarto recorte permanece no core C++ puro: um único worker usa o store fora do caller, limita trabalho a ativo/pendente e oferece `flush()` e shutdown concorrente seguro, embora sem deadline caso o store não retorne. Um `Failed` é reconciliado por readback tri-state sem virar `Saved`. O autosave combina debounce estrutural trailing de 500 ms limitado a 5 s com checkpoint de posição O(1) fixo em 10 s; os deadlines independentes evitam starvation, e ticks seguintes não empurram o checkpoint. Falha e versão futura bloqueada suspendem automação até ação explícita; autorização futura recebe nova revisão e é preservada em falhas. O Red de 22 símbolos e as regressões de completion/revisão antecederam o Green. O fechamento passou 169/169 no core, 210/210 no combinado, formato de 107 C++, composição sem testes com zero/argumento reservado rejeitado, nenhum QML alterado e nenhum benchmark. Bootstrap, restauração sempre pausada, integração dos checkpoints urgentes de pausa/seek/evento terminal e E2E de corrupção/restart/kill permanecem pendentes; o executável ainda não salva nem restaura sessão, e o lock carece de prova multiprocesso. FR-043/FR-044, NFR-REL-003 e M4.1 continuam abertos. Linux, A/B e benchmarks seguem adiados até a paridade funcional Windows.

### Escopo Windows

- single-instance e argumentos do shell;
- tray e close behavior;
- SMTC/teclas de mídia;
- ProgID/capabilities/associações;
- página oficial de defaults;
- persistência atômica e restauração pausada;
- restauração de janela somente em monitor visível;
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

Todos os critérios vigentes nos [requisitos](product/requirements.md), seção 6, e nos [gates de qualidade](quality/quality-gates.md), seção 9, passam. P0 falho exige alteração/repriorização explícita antes do RC; P1 adiado exige ajuste formal de escopo. Somente desvios que o próprio requisito permite podem seguir por ADR prévio.

## 10. M6 — Organização 0.2

**Estado:** Opcional; iniciar somente após uso real do 0.1.

Candidatos independentes:

- playlists nomeadas e M3U8;
- shuffle;
- repeat;
- velocidade de reprodução;
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

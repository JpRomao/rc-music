# Roadmap

**Decisão vigente:** C++20 + Qt Quick/QML + Qt Multimedia/FFmpeg + TagLib aceitos pelo mantenedor em 2026-09-05 no [ADR-0005](architecture/decisions/0005-windows-product-stack.md). A/B não é mais pré-requisito do produto. M1 está em promoção incremental: o [núcleo independente](core-build.md) já compila/testa na raiz e alimenta o shell existente; o [bootstrap real](bootstrap-separation.md) já dispensa os dados de demonstração em runtime. Ainda falta retirar componentes históricos compilados e promover a unidade gráfica completa para a raiz. Não há aprovação retroativa dos benchmarks ou gates experimentais.

Benchmarks e Linux permanecem adiados. Tauri/Rust e seus checks ficam fora das dependências do produto; os registros comparativos são preservados como histórico. A confirmação de stack não reautoriza Computer Use interrompido, instalação no host ou distribuição pública.

O incremento funcional posterior corrige [progresso e volume por acessibilidade](../experiments/m0/candidates/qt/README.md#accessible-slider-input), com ações/valor direto aplicados ao player e regressões de sincronização, pausa e bloqueios. Builds ON/OFF e checks focados passaram; o instalador citado abaixo ainda não contém essa correção. Não encerra acessibilidade nativa ou release.

O checkpoint Windows mais recente é a [entrega local atualizada](../experiments/m0/candidates/qt/windows-installer/README.md#local-delivery-refresh), que incorpora os incrementos do menu da fila, feedback de abertura e identidade do executável. A matriz foi revalidada no novo runtime e o instalador compilado de receita limpa passou na conferência de inventário; instalação/remoção dos bytes reais ocorreu somente em isolamento. Preserva a evolução de formatos com propriedade/retry e legado OFF já implementada. Não houve associação real, benchmark ou fechamento de M4.6/M4.8 e dos gates públicos de distribuição.

**Estado:** stack aceita; sequência de promoção e conclusão Windows, sem compromisso de datas

**Última revisão:** 2026-09-06

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
M0 Histórico dos spikes + decisão explícita ADR-0005
      │ stack Windows aceita; A/B não bloqueia
      ▼
M1 Promoção incremental + fundação TDD
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

**Estado:** Concluído em D0.9; aquela autorização cobria apenas M0. A decisão posterior do ADR-0005 permite iniciar a promoção do produto Windows.

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

## 4. M0 — Histórico dos spikes e escolha resolvida

**Estado:** a parte decisória M0-D/M0.10 é atendida pelo [ADR-0005](architecture/decisions/0005-windows-product-stack.md) commitado, por escolha explícita do mantenedor. M0.0/M0.1 e os recortes funcionais conservam suas evidências; etapas comparativas e medições ausentes não são consideradas concluídas. Os riscos aplicáveis de produto seguem em M1–M5.

**Objetivo vigente:** preservar o histórico e os riscos do experimento, sem impedir a promoção incremental da stack aceita por exigir um novo A/B.

Este roadmap usa rótulos macro `M0-A` a `M0-D`. O plano executável detalha o mesmo trabalho como M0.0–M0.10: `M0-A` = M0.0–M0.4, `M0-B` = M0.5–M0.8, `M0-C` = M0.9 e `M0-D` = M0.10. Os dois namespaces não devem ser intercambiados em evidências.

### Registros históricos anteriores à decisão

Os relatos abaixo mantêm o estado de cada recorte, inclusive menções antigas a stack proposta, ausência de builds ou próximas tarefas. Não representam a sequência vigente; a aceitação está em M0-D e o próximo trabalho real está em M1. Nenhum número antigo é apresentado como nova validação.

Ambiente, protocolo e layout foram congelados em [M0-E1/M0-P1/M0-L1](m0/README.md). O [preflight Qt](m0/evidence/m0-2-preflight.md) fechou a toolchain necessária ao início de M0.2; Qt Multimedia e TagLib já foram preparados em raízes isoladas para os primeiros slices M0.6, e provisionamentos de etapas posteriores continuam bloqueando somente seus checks dependentes.

M0.1 fechou a fixture e o harness comum em [`24d12ea`](m0/evidence/m0-1.md), incluindo o piloto sintético `P` não decisório. O preflight M0.2 registrou Qt 6.11.2 Desktop MSVC 2022 x64, ferramentas QML, `clang-format` e NVDA em contrato autenticado. Os incrementos TDD atuais incluem o core C++20 puro, o backend manual determinístico, o loader Qt ancorado no manifesto, o modelo tipado da fila comum, o presenter com ações de transporte, preferências e timer de 200 ms e a jornada QML vazia/tocando/erro com transporte, fila lateral virtualizada de 1.000 itens e navegação manual anterior/próxima. Dados e SVGs são recursos autenticados e relocáveis; preferências/temas, alto contraste e bandeja estão implementados no candidato Qt. O pacote histórico passou no smoke offscreen original/relocado; depois, o [Qtbase corrigido](m0/evidence/m0-2-qtbase-refresh.md) foi reconstruído e o [SDK composto](m0/evidence/m0-2-qt-composite-sdk.md) o combinou com QML/Quick/Svg/D3D. O run `f75eb9bea8280844c6d1a2b3f4`, com 9.880 folhas/2.400.301.667 bytes, passou na verificação independente, mas ainda não foi usado em build, runtime ou package v2 do shell. Verificações nativas/manuais e `CHK-SEC` continuam pendentes. A [política experimental de navegação](m0/README.md) é comum a M0.2/M0.3, sem mudança da fixture/protocolo: reinício após 3 s com seek, vizinho disponível sem circular, troca em zero preservando reprodução/volume/mudo/modelo e comandos inertes em vazio/carregando/erro/seek ocupado. EOF mantém `clamp_paused`; a recuperação do cenário de erro fica separada. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

A fatia de preferências/temas foi fechada em `ab87803`, naquele marco ainda sem persistência, sessão ou fim da fila reais. A [política comum e evidência](m0/README.md) separa esse histórico da fatia HC, implementada com precedência temporária da paleta do sistema, pares opacos e movimento independente. Os checks automatizados HC passaram após as correções da auditoria independente. Paletas injetadas e escalas offscreen não substituem alto contraste nativo, DPI real, NVDA ou validação manual, nem encerram UX-006.

O recorte M3.5 posterior tornou funcional no candidato Qt/Windows o armazenamento das dez preferências aprovadas: JSON `schemaVersion: 1` limitado a 64 KiB, gravação atômica com backup, recuperação de documento inválido, preservação de versão futura, debounce de 500 ms e writer único fora da thread visual. A saída pendente possui deadline visual de 2 s, mas não cancela fisicamente a chamada síncrona já iniciada; o shutdown ainda pode aguardar `join()` sem limite. O restart automatizado é Release instrumentado com `BUILD_TESTING=ON` e offscreen, não pacote ou desktop nativo. Naquele recorte, `restoreLastSession` e `startMinimized` apenas faziam round-trip; seus efeitos locais foram integrados posteriormente em M4.1/M4.4. Não houve migração histórica, ensaio de perda real de energia, validação de primeiro frame nativo/NVDA, pacote nem benchmark. Assim, M3.5 não está concluído globalmente.

M3.6 também está funcional somente no candidato Qt/Windows. Strings essenciais passam por catálogos QML `*Text.qml` ou pelo catálogo C++ do filtro nativo; pt-BR é compilado com `qt_add_translations`, instalado por `QTranslator` e cobre o singular “1 segundo”. Erros locais apresentam causa segura e ação concreta, rejeição síncrona preserva fila/contexto, importação parcial ou rejeitada oferece nova escolha e a acessibilidade da fila não expõe IDs/códigos internos. A automação cobre cópia longa em 720 × 520, mas não substitui NVDA, DPI, alto contraste ou movimento reduzido nativos nem pacote/E2E.

M3.7 está funcional no recorte automatizável do mesmo candidato: teclado cobre a jornada até a confirmação de limpeza; o progresso ativo sem seek possui ToolTip e as indisponibilidades são descritas de forma acessível; foco sobrevive a estados, transporte e modais; a árvore acessível nativa do Qt é inspecionada; anúncios de loading/faixa/erro/seek são deduplicados e coordenados com importação; foco primário cumpre contraste ≥ 3:1; e movimento reduzido preserva status sem animação contínua. O fechamento passou 174/174 no combinado e Quick 90/90 nas três escalas. NVDA, DPI/alto contraste/movimento reduzido nativos e pacote/E2E permanecem pendentes. M3.8/benchmarks fica adiado; M4.1 é o próximo incremento funcional. Linux e A/B continuam suspensos, sem aceitar a stack.

A bandeja está implementada sob a política comum da seção 8: menu, intenções separadas de fechar/ocultar/sair, aviso inicial por execução, fallback explicado e restauração do modo da janela. A suíte Release estrita passou 65/65 e o smoke do executável encerra normalmente com a janela QML fechada. Regressões cobrem animações/foco e perda de capacidade durante a ocultação. Isso não comprova bandeja/Explorer nativos nem encerra FR-030/FR-031/FR-032 ou M0.2; gates nativos/manuais continuam pendentes.

O pacote de avaliação local da seção 9 já tem receita, inventário exato, imports PE e relocação de arquivos verificados. O bloqueio inicial por `RTSSHooks64.dll` foi superado após uma consulta encontrar zero processos RTSS: as duas jornadas offscreen passaram no stage de receita commitada e na cópia com espaços/acentos, sem alterar a whitelist ou aplicações externas. A evidência não aprova desktop nativo, máquina limpa, segurança ou distribuição. M0.2 continua em andamento, sem retomada de Computer Use e sem início de M0.3.

O rebuild Qtbase v2 e o SDK composto preservaram a árvore/contrato v1, o SDK oficial e todos os stages históricos. O [run composto autenticado](m0/evidence/m0-2-qt-composite-sdk.md) contém 9.880 folhas/2.400.301.667 bytes, sem colisões ou fallback, e passou na verificação independente. O build Release exclusivo com essa identidade, package v2 e smokes original/relocado continuam pendentes e suspensos enquanto a fundação funcional de áudio avança. Build/runtime/ABI, CRT, PCRE2, licenças/notices/SBOM, redistribuição, `CHK-SEC`, gates nativos, M0.2 e decisão de stack permanecem sem aprovação.

O teste da fila verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação, não o pool completo nem as instanciações ao longo da execução. Essa prova funcional não fecha NFR-PERF-006 nem substitui a medição externa do stall de 100 ms em M0.4. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida; nenhuma nova verificação manual é alegada para a navegação, e acessibilidade nativa/NVDA continua pendente. Nenhuma coleta comparativa A/B foi executada e nenhuma stack ou engine foi aceita.

O candidato Qt combinado agora compõe o presenter com Qt Multimedia e o reader TagLib assíncrono. O modo real oferece `.mp3/.MP3`, `.m4a/.M4A`, `.mp4/.MP4`, `.flac/.FLAC`, `.ogg/.OGG`, `.oga/.OGA` e `.wav/.WAV` no seletor nativo, mostra imediatamente o stem Unicode como fallback, inicia metadata fora do caller e, na abertura inicial com intenção `Autoplay`, tenta tocar ao receber `Ready` da geração atual. O teste integrado percorre nove linhas sobre as sete fixtures autenticadas da matriz básica em path Unicode, incluindo aliases `.mp4` e `.oga`, com saída mutada e seleção do arquivo injetada no boundary; metadata termina sem erro e as linhas com tags publicam o título Unicode embutido. Um caso adicional seleciona dois arquivos juntos e navega até `Playing` na geração 2 do backend Qt real. A caracterização `UnsupportedFormat` de Opus/ALAC pertence somente ao extrator: playback inicia independentemente e o Qt pode decodificar candidatos escolhidos via `.m4a` ou “Todos os arquivos”, portanto o comportamento ponta a ponta permanece sem classificação. Isso prova a jornada básica sem bloquear o event loop, mas não automatiza o diálogo visual, não prova áudio audível/dispositivo físico, não publica suporte de formato e não conclui M0-B/M0.6. Empacotamento, A/B, automação nativa e benchmarks continuam suspensos/pendentes por decisão do mantenedor.

O seletor real agora usa multiseleção e entrega todos os paths ao core. A fila local preserva ordem e duplicatas por ocorrência, começa pela primeira escolha e usa um modelo Qt próprio com os mesmos papéis visuais da fixture, sem alterar o protocolo congelado. Abertura rejeitada não substitui a fila ativa; metadata atualiza a linha corrente; anterior/próxima percorrem a fila real sem resetar o modelo. Quando a origem não estava `Playing`, o destino fica `Ready`, sem comando `play()` ou `pause()`, e rejeições transitórias preservam o alvo. `Ended` atual avança sem circular com autoplay; no tail, as duas opções em memória mantêm ou limpam a seleção. Erros de origem marcam somente o ID corrente e tentam continuar preservando reprodução/pausa; eventos antigos são inertes. A lista também toca uma ocorrência disponível por ID estável e CRUD/reordenação atualizam o modelo incrementalmente. Drag-and-drop aceita arquivos/diretórios, calcula inserção sobre a fila e usa descoberta recursiva em worker único com batches, backpressure, cancelamento e isolamento de reparse/TOCTOU no Windows. Uma caracterização com adapters reais percorre válida A → inválida → válida B: observa `Ended` nas gerações 1/3, `InvalidMedia` na 2, pausa B no tail e não cria quarta carga nem reset. Ainda faltam o gate externo de responsividade de M3.2, persistência, automação do diálogo e validação NVDA/manual; FR-001/FR-003/FR-006/FR-011/FR-016 e M2.1/M3.1/M3.2/M3.3 continuam parciais.

Na jornada de falha, o presenter mantém o título/contexto Unicode da seleção local e oferece reabrir. O adapter rejeita como mídia inválida um M4A truncado que o Qt chega a carregar sem faixa de áudio, após uma confirmação curta que evita consumir o autoplay por ordem transitória dos sinais. Metadata, playback e integração exercitam quatro prefixos temporários de 64 bytes derivados das fontes autenticadas MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis, observando erro tipado, recuperação com a fonte válida e descarte de callbacks/resultados antigos. O primeiro Green de playback-lifecycle e integração passou **2/2**, respectivamente em **5,21 s** e **1,42 s**; após o build dos cinco alvos relevantes e a correção da corrida, o CTest focado completo passou **17/17 em 11,87 s**. Os prefixos não entram no manifesto/corpus canônico; nenhum benchmark, A/B ou empacotamento foi executado nesse incremento.

### M0-A — UI e shell A/B (protocolo histórico)

Não é tarefa obrigatória do produto atual. A retomada comparativa exige pedido explícito; os critérios de UX/acessibilidade continuam aplicáveis ao Qt aceito.

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

### M0-B — Áudio e metadata A/B (protocolo histórico)

O ramo Rust e o ensaio comparativo não são dependências de M1. Corpus, segurança e contratos funcionais permanecem necessários para Qt Multimedia/FFmpeg e TagLib; budgets não foram aprovados como cumpridos.

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

O protocolo original exigia soak antes da seleção. O ADR-0005 retira essa condição de entrada em M1; o soak de oito horas do produto e as medições permanecem gates posteriores em M5, adiados e sem resultado alegado.

### M0-C — Integração e pacote comparáveis (protocolo histórico)

A comparação entre candidatos foi arquivada. A integração/pacote Windows continua em M4/M5 com a receita local existente; Linux permanece adiado, não validado.

Provar, para cada candidato ainda viável:

- instância única e encaminhamento de vários paths Unicode;
- tray incluindo saída real;
- pacote instalável/desinstalável em ambiente descartável;
- build/smoke na distribuição Linux de referência;
- caminho demonstrável para SMTC/MPRIS e associações sem vazar plataforma para o domínio.

No candidato finalista, usar NSIS como baseline experimental para completar a prova com ProgID, `Capabilities`, `RegisteredApplications`, tela oficial de apps padrão, SMTC/teclas de mídia, suspend/resume, reinício do Explorer, upgrade e uninstall. A tecnologia final do instalador só é aceita após a evidência. Se esse aprofundamento revelar custo estrutural, executar a mesma prova no outro candidato antes da decisão.

### M0-D — Decisão aceita

O [ADR-0005](architecture/decisions/0005-windows-product-stack.md) registra C++20 + Qt Quick/QML + Qt Multimedia/FFmpeg + TagLib como escolha definitiva do mantenedor para o produto Windows e substitui ADR-0001/0002. ADR-0003 permanece `Proposto`; ADR-0004 permanece `Aceito`, com RC Music visível e MusicPlayer interno.

A escolha não deriva de vitória ou empate medido entre candidatos, não comprova budgets e não aprova os gates remanescentes. A/B deixa de ser obrigatório; não criar Tauri nem repetir benchmarks para permitir M1.

### Gate de decisão / entrada em M1

- decisão explícita, ADR aceito e contratos alinhados em commit;
- protocolo/evidências históricos preservados, sem atribuir resultados não executados;
- primeiro incremento de promoção delimitado com código, dependências, testes e critérios de saída próprios;
- riscos de corpus, segurança/licenças, acessibilidade, integração e distribuição ainda rastreados; Linux e benchmarks adiados.

Esse gate autoriza a próxima etapa, não conclui todos os trabalhos M0 nem qualquer requisito de release.

## 5. M1 — Fundação dirigida por testes

**Estado:** próxima frente autorizada após o commit da decisão; ainda não concluída.

**Objetivo:** promover incrementalmente a implementação existente para uma unidade real de produto Windows, preservando código/testes e retirando acoplamentos ao harness experimental. Não reescrever o player nem considerar um wrapper na raiz sobre o CMake M0 como promoção concluída.

### Sequência de promoção e TDD

1. inventariar uma fatia existente a promover: fontes, targets, dependências pinadas, testes e recursos de runtime versus fixture;
2. extrair/compor sua unidade real de build/test no produto, preservando o core independente de Qt e a identidade dos dados;
3. verificar equivalência da mudança estrutural e reaproveitar as regressões de tipos, estado, fila, portas e controller; mudanças de comportamento seguem Red–Green–Refactor;
4. compor scripts de validação a partir dos runners existentes e CI Windows, sem dependências Rust/Tauri;
5. promover presenter/shell e distinguir execução com fakes da composição real de áudio, antes de declarar o executável independente de M0.

### Entregas

- unidade de produto com fontes/dependências efetivamente promovidas, não só um entrypoint delegado;
- manifests/toolchain pinados e inventário do que ainda permanece no experimento;
- scripts `format`, `lint`, `check`, `test`, `build`, `validate` e CI Windows; extensão Linux adiada;
- core, portas, fakes e regressões existentes reutilizados, com cobertura apurada;
- configuração de segurança/privilégios e recursos de produto separados das fixtures autenticadas históricas;
- evidência de configure/build/test da unidade promovida, sem alegação de release completa.

### Gate de saída

- cada incremento tem checks proporcionais e commit; comportamento alterado tem Red observado, movimentação mecânica tem equivalência verificada;
- domínio roda sem toolkit de UI ou backend de áudio concreto;
- UI roda contra fake e a composição real permanece separada, sem dependência permanente do harness M0;
- `validate` e CI Windows verdes sobre a unidade real; Linux adiado sem inferir suporte validado;
- cobertura, rastreabilidade e fronteiras satisfazem a seção M1 do plano; código preexistente não fecha automaticamente todas as etapas;
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

O [recorte de seleção principal assíncrona](../experiments/m0/candidates/qt/README.md#async-audio-picker) retoma M2.1/M3.1 no candidato Qt/Windows, usando uma porta C++ e helper Qt compartilhado com Localizar. Abrir anexa/toca a primeira nova ocorrência; Adicionar preserva a faixa atual, aplicando ambos à fila viva na conclusão. Geração/lifetime, validação atômica do lote e foco posterior à conclusão fazem parte do contrato implementado. Presenter, adapters, UI, suíte completa, build Release e composição de produção têm checks automatizados consolidados. O registro mantém a intermitência do coordinator sem causa demonstrada, sem confundir retorno verde com correção. M2/M3 continuam parciais pelos gates restantes, sem retomar benchmarks ou ensaios disruptivos.

A [sanitização textual/numérica de `FR-023`](../experiments/m0/candidates/qt/README.md#metadata-sanitization) é o incremento funcional seguinte de M2.6/M3.4: policy C++ pura com UTF-8 validado, texto em linha única e faixa/ano sem parse parcial ou overflow. Policy, extrator TagLib e apresentação têm Red/Green confirmado; o core sem Qt também está verde. Caracterização integrada real, suíte Release combinada, formato e produção também passaram; os checks automatizados do recorte estão consolidados. O próximo checkpoint é a próxima lacuna funcional Windows comprovada por auditoria dos requisitos, sem repetir gates já executados nem inventar escopo. O trabalho não encerra `FR-023`, corpus hostil canônico, limites internos do TagLib ou gates nativos e não afirma exploração ou vulnerabilidade demonstrada.

## 7. M3 — Experiência completa do player

**Estado:** Em andamento no candidato Qt/Windows; fila/CRUD, importação incremental, painel de detalhes/diagnóstico, armazenamento M3.5, strings/microcopy M3.6 e acessibilidade automatizável M3.7 estão funcionais. Os efeitos locais de restaurar sessão e iniciar minimizado foram integrados depois por M4.1/M4.4. Permanecem abertos desempenho, primeiro frame/integração nativos, `CHK-SEC`, E2E empacotado e validação NVDA; M3.8 permanece adiado.
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

**Estado:** Em andamento no candidato C++/Qt/Windows; snapshot, porta, codec v1, store Windows atômico, writer/autosave, bootstrap condicional, presenter, restart Release ligado/desligado, recuperação destrutiva local e lock multiprocesso de M4.1 estão cobertos. M4.2 está funcional localmente com Preferences JSON v2/migração v1, policy de placement segura e restart em dois processos. M4.3 está funcional localmente com instância única, CLI/shell, IPC tipado limitado e drenagem antes da saída. M4.4 está funcional localmente com start behavior por capacidade/preferência/fonte, ownership explícito de ocultação e shutdown do backend antes da saída da plataforma. M4.5 está funcional localmente com porta comum, binding e SMTC C++/WinRT real. M4.6 possui o primeiro recorte funcional local da página oficial de Aplicativos Padrão, mas registro, formatos, pacote e Linux permanecem pendentes. M4.7 possui recortes de suspend/resume por porta tipada/registro Win32, retry explícito da mesma ocorrência indisponível e recuperação pausada da saída padrão; dispositivo/drive físicos, localização do arquivo em outro path, Explorer, sleep físico, pacote e Linux permanecem pendentes. As jornadas empacotadas/nativas, teclas físicas, MPRIS/Linux e os checks aplicáveis ainda impedem o fechamento global.
**Objetivo:** tornar o player um cidadão confiável do Windows e suportável no Linux.

O primeiro recorte de M4.1 adiciona snapshot validado de fila, ocorrência atual e posição, com restauração transacional que preserva IDs/duplicatas e revalida disponibilidade em runtime. O recorte seguinte materializa `SessionStore` separado e o codec JSON v1 com inteiros exatos, paths Unicode, versão futura e limite de 16 MiB. O terceiro implementa `session.json`/`.bak` no Windows com replace atômico, lock cooperativo, recuperação sem reescrita, backup byte a byte, falha fechada para arquivos ilegíveis e substituição futura somente explícita.

O quarto recorte permaneceu no core C++ puro: um único worker usa o store fora do caller, limita trabalho a ativo/pendente e oferece `flush()` e shutdown concorrente seguro, embora sem deadline caso o store não retorne. Um `Failed` é reconciliado por readback tri-state sem virar `Saved`. O autosave combina debounce estrutural trailing de 500 ms limitado a 5 s com checkpoint de posição O(1) fixo em 10 s; os deadlines independentes evitam starvation, e ticks seguintes não empurram o checkpoint. Falha e versão futura bloqueada suspendem automação até ação explícita; autorização futura recebe nova revisão e é preservada em falhas. O Red de 22 símbolos e as regressões de completion/revisão antecederam o Green. Naquele fechamento, 169/169 no core e 210/210 no combinado passaram, o formato cobriu 107 C++, a composição sem testes listou zero testes e rejeitou argumento reservado, nenhum QML mudou e nenhum benchmark foi executado. Bootstrap, restauração e integração ainda eram pendências daquele recorte.

O quinto recorte liga a sessão ao executável. O startup lê `session.json` somente quando `restore_last_session=true`; desligado, usa snapshot vazio sem consultar o store. O presenter hidrata a fila antes do QML, abre a ocorrência selecionada em `RestorePaused` e restaura somente a posição confirmada da geração corrente, com tolerância de 50 ms, espera por duração conhecida e proteção de EOF contra avanço/autoplay. Mudanças estruturais e posições confirmadas alimentam o autosave; seek, pausa, evento terminal e saída usam checkpoint imediato. Retry, substituição explícita de versão futura e recuperação após timeout ficam acionáveis, e alternar `false` → `true` força reparo ou substituição da sessão antiga. Um restart do executável Release instrumentado restaurou um path Unicode pausado em 750 ms; com a opção desligada, ignorou a sessão sem lê-la. O fechamento passou 214/214 no combinado e 172/172 no core Qt OFF; `all_qmllint` e o formato de 109 C++/46 QML ficaram verdes. `BUILD_TESTING=OFF` compilou, listou zero testes e rejeitou o hook reservado em exit 1. Nenhum benchmark foi executado. Ainda faltam E2E de encerramento forçado/crash, corrupção/reparo/replace falho, lock multiprocesso e prova nativa/pacote. FR-043/FR-044, NFR-REL-003 e M4.1 continuam abertos; M4.2+ não foi concluído. Linux, A/B e benchmarks seguem adiados até a paridade funcional Windows.

O sexto recorte executa localmente os cenários destrutivos em processos Windows reais do build Release instrumentado. A terminação forçada ocorre em `BeforePrimaryCommit`, depois de verificar primário intacto, backup completo e PID proprietário do lock; o save seguinte recupera o snapshot, remove o lock obsoleto e o relançamento restaura a faixa pausada em 750 ms. Primário corrompido com backup válido abre em `RecoveredFromBackup`; uma negação real de delete sharing faz o replace falhar, e o retry pelo botão QML repara o primário quando o handle é liberado. Dois documentos inválidos resultam em defaults seguros e aviso dispensável, sem reescrita silenciosa. O `QLockFile` também é exercitado entre processos em `BeforeBackupCommit` e `AfterPrimaryCommit`: o PID do holder é confirmado, o concorrente falha sem mutação e uma gravação posterior sucede após a liberação. A prova não representa perda física de energia nem as jornadas empacotadas/nativas correspondentes; por isso `CHK-E2E`, FR-043/FR-044, NFR-REL-003 e M4.1 continuam formalmente abertos, e M4.2+ ainda não foi concluído.

O fechamento automatizado final desse sexto recorte passou 214/214 no CTest Release combinado, 172/172 num core fresco sem Qt com MSVC 19.44/toolset 14.44 e 1/1 no alvo Quick final, com 99 casos executados. `all_qmllint` ficou verde. O formato cobriu 109 arquivos C++ e 46 QML; `qmlformat` 6.11.2 não oferece `--check`, então os QML foram comparados pela saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, registrou zero testes e rejeitou o hook reservado de crash com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Pacote/nativo, `CHK-E2E`, M4.1, FR-043/FR-044, NFR-REL-003 e os demais gates aplicáveis permanecem abertos; M4.2+ continua não concluído.

O recorte funcional local de M4.2 evolui preferências para o schema v2 com `WindowPlacement` opcional, lê v1 sem promover ou reescrever o arquivo e preserva a proteção contra versões futuras. A policy pura usa cada `availableGeometry` conectado em DIPs, mínimo `720 × 520`, contenção integral por área individual e cálculos protegidos contra overflow; monitor removido, geometria incompatível ou mudança de DPI caem no posicionamento seguro padrão. A janela QML permanece oculta até `setScreen()` e `setGeometry()`, e a geometria normal-visível é capturada de forma coalescida com flush antes de ocultar, minimizar ou sair pelos casos de uso. Se uma transição nativa não normal vence o debounce, o candidato ambíguo é descartado em favor do último normal estabilizado e o retorno a normal agenda nova captura. Um restart instrumentado em dois processos cobre persistência e restauração local.

O fechamento automatizado de M4.2 passou 225/225 no CTest Release combinado, 183/183 num core fresco sem Qt com MSVC 19.44/toolset 14.44 e 1/1 no alvo Quick com 100 casos. `all_qmllint` ficou verde. O formato cobriu 112 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não oferece `--check`, os QML foram comparados pela saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, registrou zero testes e rejeitou o hook reservado de restart de preferências com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

Essa evidência não encerra M4.2: pacote/nativo, `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` e DPI físico/múltiplos monitores continuam pendentes.

O recorte funcional local de M4.3 adquire a instância principal antes de preferências, sessão, backend e QML. Invocações secundárias não criam outro backend: resolvem os argumentos no CWD invocador, enviam um comando `openSources` v1 por um `QLocalServer` com `UserAccessOption`, aguardam ACK/NACK limitado e cedem permissão de foreground best-effort. A principal ativa a janela e serializa os lotes no pipeline real de importação.

O codec aceita até 1.000 paths e 1 MiB de JSON UTF-8, excluindo o prefixo de quatro bytes, e rejeita o lote inteiro acima do limite. Fila/bytes de bootstrap, conexões ativas, buffers e timeouts são limitados. Paths Unicode relativos, rooted-only, drive/UNC/extended canônico e CWDs diferentes são resolvidos antes do wire; recursos Qt, drive-relative, NUL, namespaces Win32 de dispositivo e grafias pseudo-extended são recusados. Ao sair, a principal pausa ingressos e drena tudo que já recebeu ACK antes do checkpoint final; timeout cancela a intenção sem descartar trabalho.

Essa evidência local/instrumentada não encerra M4.3: pacote instalado, Explorer/double-click, quoting nativo, foreground real, ACL entre usuários, fuzz/property e revisão global de segurança permanecem pendentes.

O fechamento automatizado de M4.3 passou 229/229 no CTest Release combinado. O teste de processo Release instrumentado confirmou dois ACKs, path Windows comum acima de 260 caracteres, NACK durante a drenagem, A+B persistidos e backend único. `m0_qt_shell_ui_qmllint`, formato de 122 C++/46 QML e validação documental passaram; o QML foi comparado à saída normalizada porque `qmlformat` 6.11.2 não oferece `--check`. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes, rejeitou o hook reservado com exit 1 usando o runtime autenticado e manteve `longPathAware=true` no manifesto. Nenhum benchmark foi executado.

O recorte funcional local de M4.4 aplica `startMinimized` depois do placement. Com a opção desligada, a janela é mostrada; ligada com tray disponível, entra no event loop oculta; sem tray, permanece visível com aviso. Fonte explícita inicial ou por IPC prevalece e restaura/ativa a janela. O teste de processo Release instrumentado também percorre mostrar/ocultar/mostrar pelas `QAction` reais da tray. O toggle permite desligar a opção após a perda da capacidade; sua ajuda exige tray funcional para ativação, e a nota de sessão só promete restauração quando essa preferência está ligada.

`QtShellPlatform` separa a invisibilidade do bootstrap da intenção `hidden_to_tray`: somente ocultação explícita arma a recuperação, show/rebind a limpam e a perda de tray nesse estado devolve uma janela visível/minimizada. A saída drena fontes já ACKadas e persiste preferências/sessão; só depois chama `PlaybackBackend::shutdown()` uma vez e solicita a saída da plataforma. Falha ou timeout cancelam a intenção sem desligar o backend.

Os Reds focados falharam pelas novas expectativas de disposição, ownership da ocultação, toggle, precedência de fonte e ordem de shutdown; os Greens correspondentes antecederam os gates amplos. O CTest Release combinado passou 229/229 e o core Qt OFF 183/183. `m0_qt_shell_ui_qmllint` ficou verde; o formato cobriu 122 C++/46 QML, com `qmlformat` comparado pela saída padrão normalizada em LF. `BUILD_TESTING=OFF` foi recompilado, listou zero testes e rejeitou os dois hooks M4.4 com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

M4.4 permanece aberta por pacote/desktop e tray nativos, primeiro frame perceptível sem flash, fallback Linux real, restart do Explorer, NVDA/acessibilidade nativa e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y` e `CHK-CLOSE`.

O recorte funcional local de M4.5 introduz a porta C++ pura `SystemMediaControls`, separada da integração geral da plataforma. O binding publica metadata, estado, capacidades e timeline com deduplicação, bloqueia seek durante preview/ACK pendente e encaminha Play, Pause, Previous, Next e Seek aos mesmos casos de uso de UI, atalhos e tray. O controller mantém intenção desejada e comando em voo separadamente, serializando reversões e compensando confirmações obsoletas.

No Windows, o adapter C++/WinRT obtém SMTC para o `HWND` real, publica status/botões/metadata/timeline, faz marshalling para a thread da UI, falha fechado e refaz o vínculo quando a surface nativa é recriada. O smoke QPA Windows comprovou SMTC real, publicação e limpeza em `Closed`; não sintetizou teclas físicas. O fechamento passou 239/239 no Release combinado, 190/190 no core Qt OFF, os três alvos SMTC, `qmllint`, `clang-format` em 131 C++ e a composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado. Nenhum QML mudou e nenhum benchmark foi executado.

M4.5 permanece aberta por teclas de mídia físicas, pacote, MPRIS/Linux e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E` e `CHK-CLOSE`.

O primeiro recorte funcional local de M4.6 separa suporte da plataforma, disponibilidade momentânea e resultado tipado da abertura. No Windows, `QtShellPlatform` entrega somente `ms-settings:defaultapps` a `QDesktopServices`; `Dispatched` significa handoff aceito pelo handler, não página visível ou padrão alterado. O presenter usa estado, geração, lifetime e sinal dedicado; preserva a conclusão durante uma saída ainda reversível e a invalida no commit efetivo. A UI acessível fica oculta em plataforma não suportada e só anuncia indisponibilidade/falha com o painel aberto e visível.

Os alvos focados `m0.qt.shell-platform`, `m0.qt.presenter.initial-load` e `m0.qt.quick.shell-states` passaram 1/1 cada. A consolidação passou 239/239 no CTest Release combinado, 190/190 no core Qt OFF e 1/1 no Quick com 100 casos internos; `m0_qt_shell_ui_qmllint`, formato de 131 C++/46 QML, composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado, e validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas ficaram verdes. Nenhum benchmark foi executado.

M4.6 continua aberta: não houve escrita em `UserChoice`, ProgID, `Capabilities`, `RegisteredApplications`, formatos publicados, associação, pacote/instalador ou implementação Linux. O mantenedor confirmou **RC Music** no [ADR-0004](architecture/decisions/0004-rc-music-identity.md), resolvendo a identidade textual; matriz publicada e integração do pacote permanecem pendentes antes do registro. Os recortes de M4.7 já implementados não substituem essas entregas.

Posteriormente, o [template NSIS por usuário](../experiments/m0/candidates/qt/windows-installer/README.md) materializou cadastro e remoção com propriedade, conflitos e retry, exercitados por instaladores compilados somente em Registry temporário isolado. Não houve registro real de associações. O próximo limite é integrar esse módulo ao payload e à matriz validada do pacote; seu contrato atual cobre manifesto fixo e não comprova distribuição, alteração de lista entre versões, Explorer ou recuperação arbitrária de crash. M4.6 permanece parcial e os trabalhos pesados suspensos não são retomados por esse teste funcional.

O primeiro recorte funcional local de M4.7 separa suspend/resume na porta C++ pura `SystemPowerEvents`. `WindowsPowerEvents` cria na thread da UI um `QWindow` privado, invisível e distinto da janela visual, registra seu `HWND` estável por `RegisterSuspendResumeNotification`/`DEVICE_NOTIFY_WINDOW_HANDLE` e conserva a inscrição até shutdown. Destruir/recriar a surface visual não interrompe nem troca esse receptor. Somente `PBT_APMSUSPEND` e `PBT_APMRESUMEAUTOMATIC` nativos dirigidos a ele viram eventos tipados síncronos; o filtro consome cada mensagem reconhecida com resultado `TRUE`, enquanto entradas alheias retornam `false`. Transições nativas consecutivas iguais são deduplicadas, e um `Resumed` nativo inicial continua válido. Falha inicial preserva o receptor e usa backoff coarse single-shot finito de 1 s, 5 s e 30 s; após esgotar o orçamento, apenas `start()` explícito ou `applicationStateChanged` reinicia as tentativas. Shutdown cancela o timer, desfaz o registro e torna retry/callback tardio inerte.

`Suspending` captura a última posição confirmada e chama `requestSaveNow()` sem `flush()` ou comando de pause/play/autoplay; o polling funcional fica quiescente enquanto o de persistência assíncrona ainda necessário pode continuar. `Resumed` atualiza capabilities, consulta o backend e reconcilia estado, posição e erro da geração corrente pelo fluxo normal, rearmando o timer aplicável. Os alvos focados são presenter, binding e adapter Win32; resultado de fechamento amplo: build Release aprovado, 241/241 testes no CTest combinado sequencial, 190/190 no core Qt OFF, `qmllint` e formato de 139 C++/46 QML verdes; `BUILD_TESTING=OFF` recompilado com zero testes e hook reservado rejeitado em exit 1 sem criar a raiz solicitada; validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas aprovada. Nenhum QML mudou e nenhum benchmark foi executado.

Esse recorte cobre somente suspend/resume de `FR-038`. O teste nativo registra um `HWND`, mas não põe o host em sleep e não prova Modern Standby, hibernação, timing real de freeze ou durabilidade do save assíncrono antes dele. Device/drive perdido, arquivo removido, restart do Explorer, pacote/E2E e Linux seguem pendentes; M4.7 continua parcial e `NFR-REL-005` permanece sem evidência nova.

O segundo recorte local de M4.7 acrescenta “Tentar reproduzir novamente” na fila e no erro. A ação usa `retryQueueOccurrence(ID)`, sem path novo vindo de QML, e preserva identidade, ordem, duplicatas e modelo. A indisponibilidade só é removida quando a nova geração confirma `Ready`, `Playing` ou `Paused`; uma tentativa que falha conserva diagnóstico e oferece retry explícito, sem loop ou avanço automático. Load rejeitado preserva a sessão anterior, o loading identifica a ocorrência por ID e “Abrir outro áudio” continua disponível. Retry assume a intenção sobre lotes incrementais posteriores, e problemas da importação continuam contabilizados por ID mesmo depois da recuperação.

O [registro central do segundo recorte](../experiments/m0/candidates/qt/README.md#retry-m47) reúne contrato, evidência intermediária e checks finais, incluindo a jornada de WAV restaurada no mesmo path temporário. Ele não encerra M4.7 ou `NFR-REL-005`: dispositivo, drive físico, localização em outro path, Explorer, sleep físico, pacote/E2E nativo, acessibilidade manual e Linux permanecem pendentes. Benchmarks seguem adiados pela prioridade funcional.

O terceiro recorte local de M4.7 acrescenta recuperação explícita da saída padrão. O adapter Qt distingue `AudioOutputUnavailable` e `AudioOutputChanged`, retira o player anterior e conserva a posição confirmada, sem tratar o arquivo como indisponível ou percorrer a fila. Reconexão não toca automaticamente; “Tentar novamente” reabre a mesma ocorrência pausada, restaura a posição quando há seek e mantém “Abrir outro áudio” como alternativa.

O [registro central do terceiro recorte](../experiments/m0/candidates/qt/README.md#audio-output-m47) separa testes de topologia injetada e decoder real mutado dos cenários físicos ainda abertos. A troca real de USB/Bluetooth/HDMI, áudio audível, drive físico, localização em outro path, Explorer, sleep, pacote/E2E nativo, acessibilidade manual e Linux não são declarados validados. M4.7, `FR-038` e `NFR-REL-005` continuam parciais; a escolha manual de saída dentro do app continua a evolução `FR-018`/M6.1. Benchmarks seguem adiados.

O quarto recorte local de M4.7 acrescenta um receptor Windows privado para `TaskbarCreated` e recupera, minimizada e sem comando de áudio, somente a janela ocultada pelo próprio app para a bandeja. O monitor pronto participa da capacidade antes de permitir ocultação; presença do shell não é confirmação de registro do ícone. Saída pendente adia recuperação, e bootstrap, rebind ou volta do shell não reaplicam `startMinimized`.

O [registro central de taskbar](../experiments/m0/candidates/qt/README.md#taskbar-m47) reúne contratos e uma caracterização nativa que remove/recria exclusivamente o ícone do teste, preservando suas ações. Ela não encerra Explorer nem prova seu reinício real. M4.7 e `FR-038` continuam parciais, com pacote, cenários físicos e gates nativos/Linux ainda abertos. Benchmarks seguem adiados.

O quinto recorte local de M4.7 acrescenta localização assíncrona de arquivo em outro path usando somente o ID da ocorrência na UI. A fila conserva identidade, ordem e duplicatas; metadata antiga é limpa. Uma ocorrência não atual continua indisponível até retry confirmar a nova origem, sem interromper outra faixa. Para a ocorrência atual em erro, um load aceito recupera em `RestorePaused` e restaura a posição quando há seek. Conclusões antigas são descartadas por geração/ID/path, e a persistência usa o snapshot/schema v1 existente.

O [registro central de localização](../experiments/m0/candidates/qt/README.md#file-location-m47) reúne Reds e checks automatizados verdes de core, modelo, presenter, picker, UI, decoder real e composição de produção, preservando os riscos residuais da consolidação. M4.7/`UX-008`/`NFR-REL-005` continuam parciais; diálogo nativo, drive físico, NVDA, pacote/E2E Windows, demais cenários físicos e Linux mantêm seus gates. O próximo passo permanece no desenvolvimento funcional Windows; benchmarks seguem adiados.

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
| Stack Windows | decidida explicitamente no ADR-0005; sem A/B como pré-requisito |
| Promover unidade de produto | decisão commitada + fontes/fronteiras/testes identificados + checks M1 próprios |
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

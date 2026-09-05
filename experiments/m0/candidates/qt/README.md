# Candidato Qt

Shell experimental C++20/Qt Quick de M0.2. A stack continua apenas como candidata: este diretório não é o scaffold do produto e não aceita o ADR-0001.

## Estado

Os incrementos TDD materializados em `shell/` incluem o core C++20 puro, com portas para playback e integração de plataforma, o backend manual determinístico e o suporte Qt isolado para carregar a fixture congelada e expor a fila em um `QAbstractListModel`. O modelo nasce vazio e aceita exatamente uma carga não vazia por reset, preservando a mesma identidade. O presenter `QObject` começa no estado vazio, preserva valores opcionais por propriedades `has…`, aplica explicitamente todos os defaults tipados e publica de forma atômica a fila e o snapshot falso derivados da fixture. Ele oferece ações semânticas de transporte e preferências, projeta metadata tipada da faixa atual e do contexto anterior e agenda um único tick preciso de 200 ms somente durante reprodução ou enquanto um seek aguarda ack/rejeição. O cenário falso de faixa indisponível interrompe qualquer seek pendente, para o timer e recupera o estado tocando pela ação estreita `skipUnavailable`, sem recarregar ou substituir o modelo de 1.000 itens. A entrada artificial usa somente o controle de cenário fechado do harness C++, acionável no executável de teste por `--m0-scenario=unavailable`; ela não aparece no metaobject nem na API QML e é rejeitada em builds com `BUILD_TESTING=OFF`. O core cobre toggle play/pause, saltos com clamp, seek com preview/ack, cancelamento de seek quando o backend deixa de ser transportável, preferências em memória e a decisão de close/tray. O backend reproduz os snapshots de 200 ms, transporte, clamp/pausa no fim e ack/rejeição de seek sem relógio ou áudio reais. O adapter Qt autentica os dois arquivos antes do parse, ancora seus bytes na cadeia protocolo → manifesto e projeta em tipos explícitos o contrato de apresentação, preferências, estados, detalhes, assets e fila, preservando ordem, nullabilidade e casos hostis. A seam de decodificação sem hash existe somente sob `src/` para testes semânticos causais e não faz parte da API pública.

A camada visual cria uma `ApplicationWindow` 960 × 640 responsiva até o mínimo 720 × 520. O módulo QML estático registra uma única vez `ShellUiContract` e `PlayerPresenter` como tipos não criáveis; o QML recebe somente essas instâncias controladas, sem filesystem, backend ou API genérica. No modo de fixture, “Abrir áudio” carrega a fila congelada em uma interação e a jornada mantém a mesma barra ao alternar entre vazio, tocando/pausado e erro recuperável. A barra compacta expõe progresso e tempos, saltos ±5 s, play/pause, navegação manual anterior/próxima, volume e mudo; seek por clique, drag ou teclado usa preview e exatamente uma tentativa de commit observada pelo teste, e o estado de erro preserva metadata contextual e oferece “Pular”. A fila lateral read-only usa os tokens congelados de 320–380 px, sobrepõe o conteúdo em 720 px e reserva espaço em 960 px. Sua `ListView` habilita reutilização com cache zero, uma única parada de Tab e delegates stateless; cada linha expõe ID estável, nome completo, seleção, indicador atual, duração e indisponibilidade, preservando markup como texto plano. Quick Test percorre os casos Unicode, markup, texto longo, indisponível e último item, verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação e cobre `Ctrl+L`, `Esc` e devolução de foco. Essa contagem não inclui todo o pool de reutilização nem o total de instanciações durante a execução. Botões e sliders possuem indicador de foco explícito, targets mínimos, IDs, nomes/roles e semântica `focusable`/`focused`; estados habilitado/desabilitado, ordem de Tab e limites sem sobreposição são verificados em 720 × 520. Textos de metadata usam formato plano, e capas decorativas não duplicam anúncios. Fixture, fila e SVGs são validados na configuração e fechados no executável sob aliases `qrc`; o bootstrap não incorpora caminhos do checkout. Qt Test verifica a boundary, o registro tipado, a ordem integral dos 1.000 IDs e os hashes/tamanhos finais; um smoke separado abre o executável em modo de fixture no erro e recupera pela ação QML “Pular”, preservando a barra e o modelo da fila. A árvore nativa por NVDA, escalas 100–200%, contraste alto, movimento reduzido e o stall externo de 100 ms continuam validações pendentes.

No modo de fixture, a navegação manual segue a [política experimental comum a M0.2/M0.3](../../../../docs/m0/README.md), sem alterar fixture ou protocolo. Anterior em posição maior que 3.000 ms e com seek reinicia por seek para zero; até 3.000 ms ou sem seek procura a ocorrência anterior disponível. Próxima pula indisponíveis no sentido seguinte, sem circular; não encontrar vizinho deixa o estado inalterado. Ambos os comandos são inertes em vazio/carregando/erro ou com seek em preview/aguardando ack. Uma troca começa em zero com a duração da linha de destino e preserva `Ready`/`Paused`/`Playing`, volume, mudo e identidade do modelo. EOF permanece `clamp_paused`. A recuperação “Pular” do cenário `queue-0999`, com contexto `queue-0017`, permanece separada e volta ao seed `queue-0017` em 84 s. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

O painel sincroniza seleção e rolagem com a ocorrência atual sem mover o foco do transporte. O título atual fica limitado à área disponível quando a fila está aberta. Quick Test cobre a preservação do foco durante a troca e os limites do título longo de `queue-0512`, sem alegar inspeção visual manual.

### Jornada local real — fatia funcional M0.6

Quando `M0_QT_SHELL_SUPPORT=ON` e `M0_QT_AUDIO_SUPPORT=ON`, o mesmo executável usa o modo real do presenter. `main.cpp` compõe `QtMultimediaPlaybackBackend`, `SingleWorkerMetadataReader`, `TagLibMetadataExtractor`, `SingleWorkerLocalSourceDiscovery` e um `QFileDialog` com `.mp3/.MP3`, `.m4a/.M4A`, `.mp4/.MP4`, `.flac/.FLAC`, `.ogg/.OGG`, `.oga/.OGA`, `.wav/.WAV` e “Todos os arquivos”; no Windows, a conversão preserva o path Unicode por `std::filesystem::path` wide. Filtro não equivale a suporte. Opus/ALAC retornam `UnsupportedFormat` somente numa caracterização separada do extrator; playback inicia independentemente e o Qt pode decodificar candidatos escolhidos por `.m4a` ou “Todos os arquivos”, portanto não há rejeição ponta a ponta. `LocalTrackSession`, no core sem Qt, publica imediatamente o stem do arquivo como fallback, mantém gerações independentes de playback/metadata e descarta eventos/resultados antigos. A intenção `Autoplay` tenta `play()` uma vez somente em `Ready` da geração atual; `StayReady` consome esse estado sem emitir `play()` ou `pause()`.

O presenter enfileira callbacks do backend para a thread Qt e usa um único timer de 25 ms para polling de metadata e descoberta somente enquanto houver trabalho, parando em estados terminais. A barra QML usa os mesmos comandos para play/pause e seek sem exigir a fila congelada; `Ctrl+O` e a ação do estado de erro podem abrir outro arquivo. Enquanto um erro local sem continuação está visível, o presenter preserva o título/contexto Unicode da faixa que falhou e expõe `open-audio` como ação primária; uma reabertura aceita substitui a sessão, limpa o erro e mantém callbacks/resultados da geração anterior fora do estado atual. Qualquer snapshot `Error` atual encerra o start pendente; somente `SourceUnavailable`, `InvalidMedia` e `AccessDenied` tornam a ocorrência indisponível. Destruir o presenter para o timer, remove o callback e cancela metadata/descoberta pendentes antes dos adapters saírem de escopo. O modo de fixture e seus testes permanecem disponíveis e mutuamente exclusivos desse caminho.

O core também inclui `LocalFileQueue`: uma fila local ordenada que preserva duplicatas por IDs de ocorrência próprios e rejeita lotes vazios ou com path vazio sem alterar o estado anterior. A abertura inicial cria a fila; com fila existente, `Ctrl+O` anexa o lote e toca sua primeira ocorrência, enquanto Adicionar anexa sem interromper a atual — ou inicia a primeira nova se a sessão estava vazia. Ordem e duplicatas são preservadas; rejeição imediata do novo load conserva fila, ocorrência, metadata e capa anteriores. Anterior/próxima percorrem as ocorrências reais sem resetar o modelo. Quando a origem não estava `Playing`, o destino fica `Ready`, sem comando `play()` ou `pause()`; uma rejeição transitória do backend mantém o alvo disponível. A metadata concluída atualiza também a linha correspondente. `Ended` atual busca a próxima ocorrência disponível sem circular e usa `Autoplay`; no tail, `StopKeepSelection` conserva a faixa pausada e `StopClearSelection` descarrega a sessão e limpa somente a seleção. Erros de origem marcam somente o ID corrente com código tipado e tentam a próxima ocorrência preservando `Playing`/`Paused`/`StayReady`; se o load for rejeitado, a recuperação manual conserva essa intenção. Eventos terminais antigos não alteram a nova geração. Enter, Space, duplo clique e a ação acessível da linha solicitam “tocar agora” pelo ID estável; duplicatas não se confundem, a seleção não reseta o modelo e um start `StayReady` ainda pendente vira `Autoplay` sem novo load. Mover, remover e limpar preservam IDs e atualizam o modelo incrementalmente. Remover a atual tenta apenas o sucessor à frente, sem circular; limpar qualquer fila não vazia exige confirmação. A UI oferece botões, Delete, Alt+↑/↓, Menu/Shift+F10 e menu contextual, e publica anúncio acessível ao reordenar. Uma caracterização Qt real cobre válida A → `InvalidMedia` → válida B, `Ended` nas gerações 1/3 e tail pausado sem wrap/quarta carga/reset. A política escolhida de fim agora é persistida por M3.5; validação NVDA/manual continua pendente.

Drag-and-drop local usa a porta C++ pura `LocalSourceDiscovery` e o adapter `SingleWorkerLocalSourceDiscovery`; QML/presenter não enumeram o filesystem. A janela aceita arquivos/diretórios, calcula antes/depois/fim quando o cursor está sobre a fila e anexa nos demais pontos. URLs remotas são rejeitadas sem descartar as locais do mesmo lote. Arquivos explícitos preservam ordem, duplicatas e extensões desconhecidas para diagnóstico por conteúdo; entradas encontradas em diretórios seguem a policy central `.mp3/.m4a/.mp4/.flac/.ogg/.oga/.wav`. O worker publica batches padrão de 32, progresso a cada 128 inspeções, profundidade máxima 64 e mailbox de um slot com backpressure. Geração e âncoras por ID isolam resultado antigo e alterações concorrentes da fila; cancelamento preserva batches já consumidos. No Windows, a enumeração usa handles com `FILE_FLAG_OPEN_REPARSE_POINT`, revalida volume/ID e repete `CancelSynchronousIo` durante o polling até o reconhecimento físico do cancelamento; o shutdown repete a interrupção até a saída confirmada antes do `join()`. O status QML mostra contadores, cancelar/fechar, resultado parcial, anúncios terminais e foco restaurado, sem comprimir a área principal em 720 × 520 e sem alpha em alto contraste. `Busy`/`NotReady` permanecem recuperáveis; falha assíncrona de ocorrência importada conta uma vez. O gate externo de stall, volume lento com deadline, E2E empacotado, criação nativa de symlink e NVDA/manual permanecem pendentes; filtro/extensão continuam sem equivaler a suporte publicado.

O painel de detalhes de M3.4 abre sob demanda na faixa ativa e no erro. Ele separa Faixa, Arquivo, Áudio e Diagnóstico, preserva valores opcionais por pares `has…`/valor e mostra “Não informado” em vez de zeros sintéticos. Caminho e diagnóstico são texto plano selecionável com cópia explícita; a UI traduz somente códigos tipados e não recebe mensagens livres do reader/backend. O modal bloqueia atalhos e drop, mantém o foco dentro do painel e dentro do viewport, permite Tab/Backtab/Escape e devolve o foco ao acionador ou a um fallback seguro. A extração inclui `last_write_time` opcional e uma policy de 16 KiB por campo/256 KiB agregados; a fronteira agregada é unitária porque os três campos textuais somam no máximo 48 KiB; o incremento posterior de sanitização também contabiliza oito bytes para os dois números válidos. A automação cobre fixture completa, metadata parcial/atualizada, erro simultâneo de metadata/playback, cópia, Unicode/markup, 720 × 520 e alto contraste. NVDA e inspeção nativa de escala/alto contraste continuam pendentes.

O fechamento M3.4 passou build Release completo, **154/154 testes em 30,56 s**, core MSVC sem Qt **118/118 em 1,66 s** e Quick **42/42** nas escalas padrão, 1,5× e 2×. `m0_qt_shell_ui_qmllint`, formato dos **74 arquivos C++/33 QML**, validação dos **44 Markdown/83 requisitos/17 checks/76 etapas** e `git diff --check` também ficaram verdes. Reds adicionais provaram foco fora do viewport, remoção do controle focado, modal concorrente e a perda de precisão de um valor `2^53 + 1`; o Green usa strings decimais exatas na UI sem remover as propriedades numéricas. Nenhum benchmark foi executado.

O fechamento de tocar agora passou o build Release completo, `all_qmllint` e **131/131 testes em 25,63 s**, além de formato dos 67 arquivos C++ e 22 QML, validação documental e `git diff --check`. Nenhum benchmark, A/B ou empacotamento foi executado.

O fechamento de CRUD/reordenação passou o build Release completo, `all_qmllint` e **134/134 testes em 30,50 s**, além de formato dos **67 C++/26 QML**, validação dos **44 Markdown/83 requisitos/17 checks/76 etapas** e `git diff --check`. A jornada Quick real confirma autoplay, IDs duplicados, modelo incremental, remoção da ocorrência atual com e sem sucessor, foco, confirmação/cancelamento, bloqueio de atalhos durante o modal, anúncio `Polite` e nomes contendo os tokens da própria mensagem. Nenhum benchmark, A/B, empacotamento ou validação nativa/manual foi executado.

O fechamento da importação incremental passou **150/150 em 26,40 s** no CTest Release combinado e **117/117 em 1,81 s** no core MSVC 14.44 sem Qt; o discovery focado passou **125 asserções em 12 casos**, o presenter **92/92**, `LocalTrackSession` **136 asserções em 14 casos** e o Quick **35 cenários**. O build Release completo e `all_qmllint` também ficaram verdes. Nenhum benchmark, A/B, empacotamento ou validação nativa/manual foi executado.

A caracterização integrada adicional usa as WAVs válida/truncada autenticadas em um único lote Unicode e a composição real. O CTest focado `m0.qt.audio.local-integration` passou **1/1 em 1,95 s** após a barreira que confirma o consumo do `Ended`; o comportamento já estava correto, então não houve Red de produção. O teste prova salto automático sobre `InvalidMedia`, código `invalid_media` somente na ocorrência intermediária e parada pausada na segunda válida, sem wrap, quarta carga ou reset do modelo. O fechamento Release completo passou **150/150 em 26,73 s**; formato C++, documentação e diff também ficaram verdes, sem benchmark.

O teste integrado usa backend Qt Multimedia, reader/TagLib e presenter reais, mas injeta a escolha de arquivo na fronteira do diálogo. Ele percorre nove linhas sobre sete fixtures autenticadas — WAV PCM, MP3 CBR, MP3 VBR, MP3 Unicode, M4A/AAC, FLAC e Ogg/Vorbis, mais aliases `.mp4`/`.oga` — copiadas para `ação 日本 ☕`; verifica o mesmo path nos dois adapters, fallback imediato, event loop responsivo, `Loading → Ready → Playing`, seek, pausa e metadata terminal sem erro. Um caso adicional seleciona dois MP3 em um único lote, navega para o segundo e exige `Playing` na geração 2 do backend real. As oito linhas com tags substituem o fallback pelo título Unicode embutido. Duração usa a tolerância versionada da fixture: WAV 0 ms, FLAC/Vorbis 20 ms, AAC 25 ms e MP3 50 ms. Volume zero e mudo são aplicados antes do load. Portanto o teste não clica no diálogo nativo, não prova saída audível/dispositivo físico, não executa ainda `Ended`/erro entre arquivos com os adapters reais e não publica suporte de formato.

Uma regressão integrada adicional deriva, durante o próprio teste, prefixos temporários de 64 bytes das fontes autenticadas MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis. Cada linha abre o derivado sob nome Unicode, observa erro tipado de metadata e playback, mantém o fallback/contexto e a ação de reabrir, então seleciona uma cópia válida e alcança `Playing` com metadata da nova origem. Os prefixos são entradas efêmeras de regressão, não fixtures canônicas ou novos itens do manifesto; por isso não fecham M0.5, não ampliam a matriz publicada e não concluem M0.6.

### Preferências/temas — fatia fechada em `ab87803`

A fatia implementou preferências/temas conforme a política comum da seção 6 do [registro M0](../../../../docs/m0/README.md), sem alterar a fixture/protocolo. O painel modal fica limitado ao conteúdo, mantendo o transporte visível, e usa Geral/Reprodução/Aparência com seleção exclusiva. Diálogo, botões, seletores, toggles e editores possuem roles explícitos; `Tab`/`Shift+Tab` permanecem no painel. Abrir fecha o painel da fila, seleciona Geral e foca a categoria; `Ctrl+,` abre/refoca sem alternar, e `Esc`/Fechar devolve foco ao botão `preferences`. Com dropdown aberto, o primeiro `Esc` fecha só a lista, preservando o modal. Atalhos de transporte/fila não atuam com o painel aberto. IDs e textos adicionais são centralizados no candidato conforme a tabela comum; volume/mudo não são duplicados. Naquele recorte, Aplicativos Padrão ficou fora para não ser uma ação sem efeito; o primeiro recorte M4.6 posterior acrescenta o launcher real, enquanto registro/associação continuam pendentes em M0.9/M4.6.

Os campos de salto são avaliados a cada tecla: inteiros ASCII válidos de 1–300 s são aplicados imediatamente e de modo independente; drafts inválidos mostram “Use um número inteiro entre 1 e 300.” e preservam o último valor válido do core. Um prefixo válido já pode ter sido aplicado antes de a edição ficar inválida; não há rollback automático. Fechar/reabrir descarta somente o draft e exibe o último valor válido do core. Naquele fechamento, a nota visível explicava que as preferências ficavam somente em memória e que sessão/fim da fila reais eram futuros; EOF continuava `clamp_paused`, sem concluir FR-042/FR-043/FR-044. Tema “Sistema” usa `QStyleHints::colorScheme`, desconhecido usa claro e os modos Claro/Escuro prevalecem; a resolução ocorre antes do QML e quando o esquema/preferência muda. Os 12 tokens da paleta congelada alcançam também a palette Basic e os sliders. Movimento “Sistema” no Windows consulta `SPI_GETCLIENTAREAANIMATION` somente para leitura; desconhecido/sem API usa reduzido, e a escolha Reduzido prevalece. Painel e feedback usam respectivamente 160 ms e 120 ms no modo normal e 0 ms no reduzido, sem animação contínua. As fontes oficiais e a separação entre APIs e políticas do experimento estão no registro M0.

Os checks automatizados desta fatia passaram: Red/Green do presenter, jornada Quick Test, suíte isolada de `QtSystemAppearance`, CTest Release estrito 58/58 em 7,25 s e core Qt OFF 51/51 com 473 asserções. Após o ajuste final dos delegates, Quick passou 7/7 em cada fator `QT_SCALE_FACTOR=1.5` e `2`, em modo estrito/offscreen; dez repetições do Quick e dez do smoke do executável passaram. Formato C++/QML, `qmllint`, contratos/hashes da fixture e markers também passaram.

Reds adicionais reproduziram contraste 1:1 no item destacado do seletor Basic e altura de 38 px abaixo do mínimo de 40 px. O delegate corrigido usa `Highlight`/`HighlightedText` ou `Base`/`Text` e altura preferida de 44 px que acomoda o conteúdo. A regressão também verifica `Esc` no dropdown mantendo o modal e `Tab`/`Shift+Tab` em Aparência; o Release completo acima já inclui essa correção, sem alegação de alto contraste nativo.

O build Qt ON com `BUILD_TESTING=OFF` daquela fatia, atualizado pelo rebuild final dos delegates, não continha testes ou controles artificiais e rejeitava cenário indisponível/smoke com exit 1; naquele ponto, o alias `--m0-scenario=empty` ainda era aceito. A validação posterior de M3.5 fechou essa lacuna: a composição atual sem testes rejeita todo argumento reservado, inclusive `--m0-scenario=empty`. O default permaneceu vivo por 1,5 s sem output antes da limpeza dos PIDs; isso não prova saída graciosa. Não restaram processos de validação. A [evidência consolidada](../../../../docs/m0/README.md) registra a auditoria dos builds e os limites dos smokes.

No harness, `FixtureRepositoryTests` e `PlayerPresenterTests` agora usam `QGuiApplication` offscreen: com `QCoreApplication`, `QAbstractItemModelTester` exercitava papéis que geravam warnings de `QPixmap`. Essa correção é do ambiente de teste Qt e não adiciona Qt ao core.

### Persistência Windows M3.5 — funcional no candidato

As dez preferências aprovadas são carregadas antes da apresentação e persistidas em JSON `schemaVersion: 1`, limitado a 64 KiB: tema, movimento, volume, mudo, saltos para trás/para frente, comportamento ao fechar, restaurar sessão, iniciar minimizado e fim da fila. O adapter usa `QSaveFile` sem fallback direto, mantém um backup e serializa alterações com debounce trailing de 500 ms por um único writer fora da thread visual. Documento inválido recupera do backup ou usa defaults com aviso; versão futura no primário ou no backup impede sobrescrita automática até confirmação explícita.

Uma saída com estado sujo solicita gravação imediata, bloqueia novas ações e mantém o event loop ativo. Ela só é confirmada após sucesso; falha, versão futura ou deadline visual de 2 s restauram a janela e expõem retry/substituição. Esse deadline não cancela fisicamente uma chamada síncrona já iniciada, e o shutdown do writer ainda pode aguardar `join()` sem limite se o armazenamento não retornar.

O teste de restart abre duas vezes o executável Release instrumentado com `BUILD_TESTING=ON`, hooks de teste, plataforma offscreen, software/Basic e a mesma raiz temporária com espaços/Unicode. Ele comprova o round-trip do snapshot e o tema carregado antes da criação do QML nesse harness. Não comprova o build `BUILD_TESTING=OFF`, pacote, desktop nativo, primeiro frame perceptível sem flash ou NVDA; os hooks não pertencem à composição sem testes. Falhas injetadas antes/depois do commit não equivalem a perda real de energia, e nenhuma migração histórica foi exercitada porque este é o primeiro schema. Naquele fechamento, `restoreLastSession` e `startMinimized` somente persistiam; seus efeitos locais foram materializados depois em M4.1 e M4.4.

Na rerun final, o build Release combinado e seus 174/174 testes passaram; o core com Qt desabilitado passou 135/135. O Quick passou no fator padrão e em `QT_SCALE_FACTOR=1.5`/`2`, os dois smokes shell-only passaram, `all_qmllint` ficou verde e o check de formato cobriu 89 arquivos C++ e 35 QML. O rebuild `BUILD_TESTING=OFF` ficou sem testes/hooks/fontes de teste e rejeitou com exit 1 cenário explícito, smoke e raiz de configuração reservada, sem criar essa raiz. A validação documental cobriu 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Portanto o armazenamento M3.5 está funcional neste candidato, não concluído globalmente; M3.6 abaixo é posterior.

### Strings e microcopy M3.6 — funcional no candidato

Strings essenciais da apresentação ficam em catálogos QML `*Text.qml`; o filtro do diálogo nativo passa por `ApplicationText`. O catálogo `music-player_pt_BR.ts` é compilado com `qt_add_translations` e instalado antes da UI por `QTranslator`, incluindo `numerus` para “1 segundo”/plural. A fixture autenticada não muda, e os contratos rejeitam texto essencial espalhado pelo QML ou pelo bootstrap C++.

O presenter projeta causas tipadas de origem indisponível, mídia inválida, acesso negado e falha de backend em microcopy segura e ação `open-audio`. Cancelamento do seletor não vira erro; uma seleção não vazia rejeitada sincronamente publica recuperação e preserva fila, faixa e contexto anteriores. Importações parciais/rejeitadas mostram contagens neutras, orientação e “Escolher outros arquivos”. A fila anuncia posição, duração e causa localizada sem expor ID de ocorrência ou código interno. Título, mensagem e ação longos permanecem visíveis e contidos no mínimo 720 × 520 em Quick Test.

O fechamento passou **174/174** no CTest Release combinado, **135/135** no core Qt OFF e **64/64** no Quick nos fatores padrão, 1,5× e 2×. `m0_qt_shell_ui_qmllint` terminou silencioso e o formato cobriu **90 C++/39 QML**. O build `BUILD_TESTING=OFF` registrou zero testes e rejeitou argumento reservado com exit 1. Nenhum benchmark foi executado. NVDA, DPI, alto contraste e movimento reduzido nativos, pacote/E2E e demais gates indicados permanecem abertos; o recorte M3.7 abaixo é posterior. Linux, A/B e benchmarks continuam suspensos, e nenhuma stack foi aceita.

Quando o SDK Qt usado como target é enxuto e não traz os executáveis host do LinguistTools, o configure exige `QT_HOST_PATH` apontando para uma instalação Qt host compatível; a ausência dessa âncora é uma limitação de build, não falha do catálogo em runtime.

### Teclado e acessibilidade M3.7 — funcional no recorte automatizável

A jornada Quick agora percorre somente por teclado abertura, fila, navegação, reprodução, “Limpar fila” e confirmação, incluindo Tab/Shift+Tab contidos no modal. Transporte expõe atalhos em descrição/ToolTip e explica por que seek/saltos estão indisponíveis. Transições Empty/Loading/Playing/Error movem foco apenas quando o estado que o possuía desaparece; fila, preferências, detalhes, importação e avisos mantêm autoridade. `QAccessibleInterface` é consultada diretamente para roles, nomes, descrições, foco e parentesco dos controles públicos.

Loading, faixa atual, erro terminal e seek rejeitado emitem anúncios deduplicados sem tick spam nem paths/códigos; durante descoberta, a região de importação fala sozinha. O aro da ação primária usa contraste semântico ≥ 3:1 em claro/escuro, e movimento reduzido interrompe a animação contínua sem esconder o status. A restauração de foco do aviso também rejeita a raiz passiva após transições sobrepostas.

O fechamento passou **174/174** no CTest Release combinado e **90/90** no Quick nos fatores padrão, 1,5× e 2×. `m0_qt_shell_ui_qmllint`, formato de **90 C++/46 QML** e a composição `BUILD_TESTING=OFF` ficaram verdes; esta listou zero testes e rejeitou argumento reservado em exit 1. Nenhum benchmark foi executado. NVDA, DPI, alto contraste e movimento reduzido nativos e pacote/E2E seguem pendentes; naquele fechamento, M3.8 ficou adiado e M4.1 era o próximo incremento funcional, sem aceitar a stack.

### Sessão M4.1 — domínio, persistência e integração no executável

`SessionSnapshot` contém a fila persistível, a ocorrência atual opcional e a posição confirmada. `LocalFileQueue::snapshot/restore` preserva ordem, paths Unicode, duplicatas e IDs, mas não disponibilidade transitória; toda origem restaurada volta disponível para revalidação posterior. A hidratação valida integralmente IDs positivos/únicos, paths e seleção antes do commit, deixa fila/cursor intactos em falha e reconstrói um allocator circular que nunca emite zero nem colide após wrap. Reds ligaram com os quatro símbolos ainda ausentes; o Green focado passou 118 asserções em sete casos, o build/teste Qt OFF passou 142/142 e o Release combinado passou 181/181. Formato cobriu 93 C++/46 QML, enquanto `BUILD_TESTING=OFF` listou zero testes e rejeitou argumento reservado em exit 1.

`SessionStore` é uma porta própria e não persiste preferências. O codec v1 usa `{schemaVersion, queue: {items, currentOccurrenceId}, positionMs}`; IDs `uint64_t` e posição `int64_t` são strings ASCII decimais canônicas, preservando os máximos e valores acima de `2^53`. Paths UTF-16 Windows, UNC, Unicode e duplicados permanecem literais, sem consulta/canonicalização; `U+0000` é rejeitado. Chaves extras são toleradas, campos v1 são obrigatórios e versão futura é classificada antes deles. O limite inclusivo de 16 MiB é conferido antes do parse e depois do encode. O Red compilou e falhou no link somente pelos dois métodos do codec ausentes; o Green focado passou 1/1. A porta ganhou cobertura no core Qt OFF; o fechamento passou 183/183 no Release combinado, 143/143 sem Qt, `BUILD_TESTING=OFF` com zero testes/argumento reservado em exit 1 e `clang-format` nos 98 arquivos C++. Nenhum QML mudou e nenhum benchmark foi executado.

`QtJsonWindowsSessionStore` usa `session.json`/`.bak`, raiz absoluta Unicode, `QSaveFile` sem fallback direto e lock cooperativo durante inspeção, rotação e commits. A precedência de load preserva versões futuras e recupera backup válido sem reparar arquivos. Save falha fechado para slot ilegível, bloqueia qualquer versão futura sem confirmação, copia o primário anterior byte a byte e só substitui backup futuro depois do commit primário. O Red inicial falhou no link pelos três métodos ausentes; regressões posteriores ficaram vermelhas para a corrida entre stores e para arquivo ilegível antes do Green final. Quatro fault points, handles Windows e falhas reais de replace cobrem backup e primário. O fechamento passou 184/184 no Release combinado, 143/143 sem Qt, `BUILD_TESTING=OFF` com zero testes/argumento reservado em exit 1 e `clang-format` nos 101 arquivos C++. Nenhum QML mudou e nenhum benchmark foi executado. Naquele fechamento, `Failed` ainda podia suceder um commit sem writer para reconciliá-lo por load; prova multiprocesso, writer/autosave, restauração sempre pausada e E2E de corrupção/restart/kill permaneciam necessários. O recorte não encerrou M4.1, FR-043/FR-044 ou NFR-REL-003.

Naquele quarto recorte, writer e autosave permaneceram no core, sem integração ao bootstrap ou ao presenter. `SingleWorkerSessionWriter` serializa uma escrita fora do caller, faz `flush` aguardar a revisão ativa antes de gravar uma revisão posterior e reconcilia todo `Failed` por `load` no próprio worker sem promovê-lo a sucesso. Snapshot pedido recuperável exatamente do primário ou backup é sinalizado como `SaveIncomplete`; mismatch ou readback indisponível vira `SaveFailed`. `SessionAutosave` aplica debounce estrutural trailing de 500 ms com idade máxima de 5 s, checkpoint fixo de posição confirmada em 10 s que ticks posteriores não postergam, deadlines independentes e checkpoint imediato mesmo para posição igual quando já há estado sujo; ticks escalares não recopiam a fila. Conclusões antigas não alteram estado novo, falha/bloqueio atual suspende rearm automático e retry/confirmação explícitos rearmam. A autorização de substituir versão futura recebe revisão distinta do request non-replace ativo e é retida até `Saved`, inclusive em `flush`. O Red inicial falhou no link por 22 símbolos, e regressões ficaram vermelhas para conclusão duplicada e colisão da revisão de autorização. Os 10 casos novos do writer e 16 do autosave somam 26; o filtro de sessão terminou com 34 casos. O fechamento passou 169/169 no core Qt OFF, 210/210 no Release combinado, `clang-format` em 107 arquivos C++ e `BUILD_TESTING=OFF` com zero testes e argumento reservado rejeitado em exit 1. Nenhum QML mudou; `qmlformat` e benchmarks não foram executados. `flush`/`shutdown` podem aguardar sem limite se o store não retornar. Bootstrap, eventos confirmados da geração corrente, restauração sempre pausada e E2E de restart/corrupção/kill/replace falho ainda eram pendências daquele recorte; ele não encerrou M4.1.

O quinto recorte integra essa infraestrutura ao executável Qt/Windows. Depois de carregar preferências, o startup lê `session.json` somente com `restore_last_session=true`; quando a opção está desligada, entrega snapshot vazio sem consultar o store. Antes do QML, o presenter hidrata a fila e abre a ocorrência selecionada como `RestorePaused`, sem autoplay. A posição restaurada é vinculada à geração aberta, aguarda duração conhecida para destino positivo e aceita confirmação com tolerância de 50 ms; trocar a geração descarta a pendência, e a marca de EOF impede avanço ou reprodução inesperada. Mudanças estruturais usam o autosave coalescido, posição vem apenas do backend confirmado e seek, pausa, evento terminal e saída disparam checkpoint imediato. Retry, confirmação de substituir versão futura, recuperação após timeout e o reparo forçado ao alternar restauração de `false` para `true` permanecem acionáveis no aviso existente.

O restart do executável Release instrumentado comprovou path Unicode restaurado pausado em 750 ms; outro cenário comprovou que restauração desabilitada ignora a sessão sem lê-la. O fechamento passou 214/214 no CTest Release combinado e 172/172 no core Qt OFF. `all_qmllint` terminou verde, e os checks de formato cobriram 109 arquivos C++ e 46 QML. A composição `BUILD_TESTING=OFF` compilou, listou zero testes e rejeitou o hook reservado com exit 1. Nenhum benchmark foi executado. Ainda faltam E2E de encerramento forçado/crash, corrupção/reparo/replace falho, lock multiprocesso e validação nativa/pacote; portanto M4.1, FR-043/FR-044, NFR-REL-003 e os gates correspondentes permanecem abertos.

O sexto recorte passa a cobrir quatro jornadas locais no alvo `m0.qt.session.restart`. O shell Release instrumentado restaura a sessão pausada em 750 ms, inicia um checkpoint estrutural e para em `BeforePrimaryCommit`; o teste observa o primário anterior intacto, o backup byte a byte e o PID do próprio processo como owner do `QLockFile`, força `CrashExit`, faz o save seguinte reaproveitar e remover o lock obsoleto e relança com a faixa ainda pausada. Primário inválido e backup válido produzem `RecoveredFromBackup`; a negação nativa de delete sharing força uma falha real de replace, e o botão `preference-persistence-retry` repara o primário depois que o handle é liberado. Quando os dois documentos são inválidos, o shell inicia vazio em `UsingDefaults`, aciona `preference-persistence-dismiss` e não reescreve seus bytes. O alvo `m0.qt.session.windows-store` ainda usa duas linhas multiprocesso, `BeforeBackupCommit` e `AfterPrimaryCommit`: o pai confirma o owner PID do helper, o writer concorrente retorna `Failed` sem alterar primário/backup, e a liberação permite ao holder e à gravação seguinte concluir e remover o lock. Esses hooks existem somente em `BUILD_TESTING=ON`; os alvos focados cobrem a evidência local, mas não perda física de energia, desktop nativo ou pacote. As jornadas empacotadas/nativas correspondentes, `CHK-E2E` e M4.1 permanecem abertos.

O fechamento automatizado final desse recorte passou 214/214 no CTest Release combinado. Um core fresco sem Qt, compilado com MSVC 19.44/toolset 14.44, passou 172/172; o alvo Quick final passou 1/1 e executou 99 casos. `all_qmllint` ficou verde. O formato cobriu 109 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não oferece `--check`, os arquivos QML foram conferidos pela saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado de crash com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Essa consolidação não fecha pacote/nativo, `CHK-E2E`, M4.1 nem os demais gates indicados.

### Geometria segura da janela M4.2 — funcional no recorte automatizável

Sem reclassificar a evidência histórica de M3.5, o formato atual de `preferences.json` passa a `schemaVersion: 2` e acrescenta `windowPlacement` opcional com `x`, `y`, `width` e `height`. Um documento v1 válido continua aceito e é migrado somente em memória para ausência de geometria, sem regravação causada apenas pelo load; todo encode atual produz v2. Em v2, o campo deve ser `null` ou um objeto completo com inteiros exatos, dimensões positivas e coordenadas que podem ser negativas. O limite de 64 KiB e a preservação de versões futuras continuam valendo.

O adapter publica somente a geometria estabilizada de uma janela normal e visível; estados oculto, minimizado, maximizado e fullscreen não substituem a última posição normal. Mudanças próximas são coalescidas, e uma captura pendente é drenada sincronamente antes de ocultar, minimizar ou sair pelos casos de uso, antes do flush final das preferências. Como o backend Windows pode publicar primeiro a geometria expandida, uma transição nativa não normal que vence o debounce cancela o candidato ambíguo e conserva o último normal já persistido; retornar a normal agenda nova captura. No startup, a raiz QML permanece oculta enquanto a posição salva é comparada às `availableGeometry` atuais do Qt. A restauração normaliza o tamanho mínimo de 720 × 520 e só aplica o retângulo quando ele cabe integralmente em uma única área útil; monitor removido, área reduzida por DPI, gap entre monitores ou dados inválidos recusam a posição e preservam a geometria padrão. A primeira visibilidade só é liberada após essa resolução.

O restart de preferências executa duas instâncias sequenciais do shell Release instrumentado, offscreen, com a mesma raiz temporária Unicode. A primeira captura `{20, 30, 720, 520}` e sai pelo flush normal; o teste lê o JSON como schema v2 também por `QtJsonWindowsPreferencesStore`, grava uma sessão deliberadamente ignorada e a segunda instância confirma tema e geometria restaurados sem consumir essa sessão. Isso prova o processo real e a ordem anterior à visibilidade no harness, mas não um primeiro frame perceptível no desktop. Pacote autenticado, execução nativa com monitor removido e DPI real, `CHK-WIN`, `CHK-E2E` e `CHK-CLOSE` permanecem abertos; M4.2 e FR-045 ainda não são declarados concluídos.

O fechamento automatizado de M4.2 passou 225/225 no CTest Release combinado e 183/183 num core fresco sem Qt com MSVC 19.44/toolset 14.44. O alvo Quick passou 1/1 com 100 casos, `all_qmllint` ficou verde e o formato cobriu 112 arquivos C++ e 46 QML. Como `qmlformat` 6.11.2 não oferece `--check`, o check QML comparou sua saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado de restart de preferências com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

### Instância única e argumentos M4.3 — funcional no recorte automatizável

O executável combinado adquire cedo um `QLockFile` sob a raiz de configuração do usuário e publica um `QLocalServer` com `UserAccessOption`. A instância secundária resolve os argumentos contra seu próprio CWD, serializa somente `openSources` v1, tenta `AllowSetForegroundWindow` para o PID da principal como best-effort e aguarda ACK/NACK limitado; ela retorna antes de construir `QtMultimediaPlaybackBackend`. A principal restaura a janela e entrega os paths ao mesmo `OpenSourcesDispatcher`/discovery usado pela UI. O comportamento efetivo da ACL entre usuários ainda exige o check nativo/empacotado.

O JSON aceita 0–1.000 paths e no máximo 1 MiB, sem contar o prefixo big-endian de quatro bytes. Acima disso o lote inteiro é rejeitado, não dividido. A fila de bootstrap tem 16 comandos/4 MiB; conexões ativas, buffer por frame e deadlines também são limitados, e a principal permanece operacional depois de excesso, conexão parcial ou frame grande. Paths relativos e rooted-only são transformados em filesystem fully-qualified no processo invocador; drive, UNC e extended canônico Unicode preservam spelling, enquanto QResource, drive-relative, NUL, namespaces Win32 de dispositivo e grafias pseudo-extended falham antes/depois do wire.

ACK transfere responsabilidade à principal. Quando a saída é solicitada, novos IPCs recebem NACK transitório, todo comando já ACKado é drenado em FIFO e só então sessão/preferências são salvas e a saída é commitada; o deadline cancela a intenção e reabre a janela sem descartar os imports. As mensagens visíveis passam por `ApplicationText`. O teste de processo usa o shell Release instrumentado e raiz/CWD Unicode isolados, comprova um único backend e confere os paths finais em `session.json`.

O harness não substitui Explorer/associação instalada, foreground real, ACL entre usuários ou pacote nativo. `CHK-WIN`, `CHK-E2E`, `CHK-SEC` e `CHK-CLOSE` permanecem abertos globalmente; fuzz/property do framing/schema também fica para hardening. Nenhum benchmark foi executado.

O Red final falhou pelas APIs de drenagem ainda ausentes; o Green focado de dispatcher/presenter/teste de processo passou 3/3 e o fechamento Release combinado passou 229/229. O teste de processo Release instrumentado manteve A ativo, ACKou B num path Windows comum acima de 260 caracteres, recusou a terceira secundária durante a saída, retomou a drenagem e verificou A+B no `session.json` com um único backend. `m0_qt_shell_ui_qmllint` ficou verde. O formato cobriu 122 arquivos C++ e 46 QML; como `qmlformat` 6.11.2 não oferece `--check`, o check QML comparou sua saída padrão normalizada em LF. `BUILD_TESTING=OFF` foi recompilado, listou zero testes, rejeitou o hook reservado com exit 1 sob o runtime autenticado e manteve `longPathAware=true` no manifesto. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas.

### Início minimizado e saída M4.4 — funcional no recorte automatizável

A disposição inicial é resolvida depois de aplicar o placement. `startMinimized=false` mostra a janela; `true` com tray disponível mantém o root oculto antes do event loop; sem tray, a janela é revelada com aviso. Uma fonte inicial explícita prevalece sobre a preferência, e um `openSources` recebido depois por IPC restaura/ativa a principal oculta. O teste de processo Release instrumentado percorre também mostrar/ocultar/mostrar pelas `QAction` reais da tray. O toggle permite desativar a preferência após a perda da capacidade; o helper explica que a ativação exige bandeja funcional, e a nota de sessão é condicional a `restoreLastSession`.

`QtShellPlatform` usa uma intenção `hidden_to_tray` independente da visibilidade transitória do `QWindow`. O bootstrap invisível não arma polling nem fallback; somente `hideWindow()` o faz, e show/rebind limpam a intenção. Se a tray desaparecer durante ocultação explícita, a janela volta visível/minimizada. Na saída, fontes ACKadas são drenadas e preferências/sessão precisam concluir; então `PlaybackBackend::shutdown()` ocorre uma única vez antes de `PlatformIntegration::requestExit()`. Falha ou timeout cancelam a saída sem desligar o backend.

Os Reds focados falharam pelas expectativas novas de disposição, ownership da ocultação, toggle, precedência de fonte e ordem de shutdown; os Greens correspondentes passaram antes da consolidação. O CTest Release combinado passou 229/229, e o core Qt OFF passou 183/183. `m0_qt_shell_ui_qmllint` ficou verde. O formato cobriu 122 C++/46 QML; como `qmlformat` 6.11.2 não oferece `--check`, o check QML comparou sua saída padrão normalizada em LF. `BUILD_TESTING=OFF` foi recompilado, listou zero testes e rejeitou os dois hooks M4.4 com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

O teste de processo não substitui pacote/desktop e tray nativos, primeiro frame perceptível sem flash, fallback Linux real, restart do Explorer ou NVDA/acessibilidade nativa. `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y`, `CHK-CLOSE` e M4.4 permanecem abertos globalmente.

### SMTC Windows M4.5 — funcional localmente

A porta C++ pura `SystemMediaControls` publica título, artista, álbum, estado, capacidades, posição e duração e recebe Play, Pause, Previous, Next e Seek. `SystemMediaControlsBinding` projeta o estado do presenter, deduplica snapshots, desabilita seek durante preview ou confirmação pendente e encaminha comandos aos mesmos casos de uso usados pela UI, pelos atalhos locais e pela tray. O controller mantém separadas a intenção explícita mais recente e o comando em voo, serializando reversões e emitindo uma compensação depois de ACK obsoleto sem duplicar comandos.

O adapter Windows usa C++/WinRT e `ISystemMediaTransportControlsInterop::GetForWindow` sobre o `HWND` real. Ele publica status, metadata, botões e timeline, desabilita operações não suportadas, entrega callbacks na thread da UI, falha fechado e trata destruição/recriação da surface com release, rebind e republicação. `Closed` limpa metadata e desabilita os controles; shutdown revoga handlers e torna callbacks tardios inertes.

Os Reds/Greens cobriram APIs ausentes, Play/Pause explícitos, intenção invertida antes do ACK, `Ready` obsoleto, navegação com intenção pendente, capacidade de seek, surface destruída/recriada e rebind obsoleto. O filtro `[async]` passou cinco casos e 42 asserções. O CTest Release combinado passou 239/239, e o core Qt OFF passou 190/190. `m0.qt.system-media-controls.binding`, `m0.qt.windows.smtc` e `m0.qt.windows.smtc-native` ficaram verdes; o último usa QPA Windows e WinRT reais e comprovou `HWND`, estado inicialmente desabilitado, status `Playing`, botões, título/artista/álbum e limpeza em `Closed`.

`m0_qt_shell_ui_qmllint` passou e `clang-format` validou 131 arquivos C++. Nenhum QML mudou, portanto `qmlformat` não foi executado. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado.

O smoke nativo não sintetiza teclas físicas e não substitui pacote/E2E. M4.5 permanece aberta por teclas de mídia físicas, pacote, MPRIS/Linux, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E` e `CHK-CLOSE`.

### Aplicativos Padrão M4.6 — primeiro recorte funcional local

`PlatformIntegration` separa `defaultAppsSettingsSupported`, `defaultAppsSettingsAvailable` e a conclusão tipada de `openDefaultAppsSettings`. O presenter publica os estados `Idle`, `Opening`, `Dispatched`, `Unavailable` e `Failed` por `defaultAppsSettingsChanged`, recusa duplicação em voo e protege callbacks por geração e lifetime. Se a saída ainda pode ser cancelada por falha de persistência, a conclusão continua válida; somente o commit efetivo da saída a invalida.

No Windows, `QtShellPlatform` fornece internamente a URI fixa `ms-settings:defaultapps` a `QDesktopServices::openUrl`. O opener injetado mantém os testes determinísticos e prova que nenhuma outra URI chega à fronteira. `Dispatched` significa somente handoff aceito pelo handler registrado; não prova página visível nem mudança de padrão. A QML não fornece URI, path ou ProgID.

O painel Geral mostra o botão apenas quando a plataforma suporta a operação. Nome, descrição, teclado, foco e motivo de indisponibilidade são acessíveis; feedback de falha/indisponibilidade é localizado e só é anunciado assertivamente enquanto o painel está aberto e visível. Fora do Windows, botão e feedback ficam ocultos.

Os Reds focados exigiram as novas fronteiras, propriedades/estados e contratos do adapter/UI. Os Greens passaram individualmente em `m0.qt.shell-platform` 1/1, `m0.qt.presenter.initial-load` 1/1 e `m0.qt.quick.shell-states` 1/1. A consolidação passou 239/239 no CTest Release combinado, 190/190 no core Qt OFF e 1/1 no Quick com 100 casos internos; `m0_qt_shell_ui_qmllint`, formato de 131 C++/46 QML, composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado, e validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas ficaram verdes. Nenhum benchmark foi executado.

M4.6 continua parcial. Este recorte não escreve `UserChoice`, não cria ProgID, `Capabilities` ou `RegisteredApplications`, não publica formatos, não instala associação/pacote e não implementa Linux. Identidade, matriz publicada, instalador e os gates nativos continuam pendentes.

### Suspend/resume Windows M4.7 — primeiro recorte funcional local

A porta C++ pura `SystemPowerEvents` separa as transições `Suspending`/`Resumed` da integração geral da plataforma. `SystemPowerEventsBinding` instala o handler que entrega esses eventos ao presenter e o remove antes do shutdown. No adapter Windows, uma boundary própria registra por `RegisterSuspendResumeNotification(..., DEVICE_NOTIFY_WINDOW_HANDLE)` o `HWND` estável de um `QWindow` privado e invisível criado na thread da UI e liga `User32`. Esse receptor não é a janela visual e permanece registrado até shutdown. O filtro aceita somente `WM_POWERBROADCAST` dirigido a ele e traduz `PBT_APMSUSPEND`/`PBT_APMRESUMEAUTOMATIC`; cada mensagem reconhecida é consumida com resultado `TRUE`, inclusive quando deduplicada, enquanto entradas alheias retornam `false`. Somente eventos nativos são entregues. Duplicatas nativas consecutivas são suprimidas, enquanto um `Resumed` nativo inicial é entregue.

Se o registro inicial falha, `WindowsPowerEvents` preserva o receptor, publica capacidade indisponível e agenda backoff coarse single-shot finito de 1 s, 5 s e 30 s. Esgotado esse orçamento, o timer para; apenas `start()` explícito ou `applicationStateChanged` enquanto indisponível o reinicia. Shutdown cancela o timer, desfaz o registro, remove o filtro e destrói o receptor, tornando retry e callback tardios inertes. Criar, destruir ou recriar a surface da janela visual não muda o `HWND` privado nem interrompe a inscrição. Mensagens, event types e handles alheios não atravessam a porta, e o teardown é idempotente.

No suspend, o presenter captura a posição confirmada da geração atual e inicia `requestSaveNow()` sem `flush()`, pause, play ou autoplay. O polling funcional fica quiescente, preservando somente o acompanhamento necessário à persistência assíncrona. No resume, capabilities e snapshot do backend são relidos; estado, posição e erro atuais são reconciliados pelo fluxo normal e o polling aplicável é rearmado. Eventos repetidos e eventos posteriores ao commit da saída não alteram estado.

Os alvos focados são `m0.qt.presenter.initial-load`, `m0.qt.system-power-events.binding` e `m0.qt.windows.power-events`, incluindo registro real do `HWND` sob QPA Windows sem suspender a máquina. Resultado final dos alvos, consolidação Release/core, formato e composição `BUILD_TESTING=OFF`: build Release aprovado, 241/241 testes no CTest combinado sequencial, 190/190 no core Qt OFF, `qmllint` e formato de 139 C++/46 QML verdes; `BUILD_TESTING=OFF` recompilado com zero testes e hook reservado rejeitado em exit 1 sem criar a raiz solicitada; validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas aprovada. Nenhum QML mudou e nenhum benchmark foi executado.

O recorte cobre apenas suspend/resume de `FR-038`. Não houve sleep físico, Modern Standby, hibernação ou prova de durabilidade do save assíncrono antes do freeze. Device/drive perdido, arquivo removido, restart do Explorer, pacote/E2E, Linux e `NFR-REL-005` permanecem pendentes; M4.7 continua parcial.

### Retry M4.7

O segundo recorte funcional local de M4.7 acrescenta recuperação explícita da ocorrência original indisponível. `retryQueueOccurrence(ID)` aceita somente um ID já pertencente à fila e reutiliza seu path interno; QML não fornece nem substitui o caminho. A tentativa preserva ID, ordem, duplicatas e identidade do modelo. `selectUnavailableForRetry` seleciona sem afirmar recuperação, e a ocorrência só volta a disponível quando a nova geração confirma `Ready`, `Playing` ou `Paused`; nesse momento, o modelo limpa o diagnóstico com `dataChanged` restrito e conserva metadata. Eventos de gerações antigas não confirmam a recuperação.

Se o novo load for rejeitado, seleção e backend anteriores permanecem intactos. Se uma tentativa aceita terminar em erro, a ocorrência conserva seu diagnóstico acionável e permite outra tentativa explícita; essa falha não cria loop nem avança automaticamente para a próxima faixa. Um diagnóstico tipado diferente pode atualizar a mesma ocorrência ainda indisponível. O retry explícito assume a intenção de reprodução em relação à importação incremental em andamento, impedindo que a chegada posterior de outro lote substitua a escolha do usuário. A contagem de problemas da importação registra IDs que falharam, independentemente da disponibilidade atual: recuperar um item não apaga a ocorrência histórica nem permite contá-la de novo por uma falha repetida.

A fila e o estado de erro oferecem “Tentar reproduzir novamente”, com acionamento por teclado e semântica acessível. O loading identifica a ocorrência por ID, inclusive entre paths duplicados, e impede repetição da mesma tentativa em voo. “Abrir outro áudio” continua disponível como alternativa de recuperação. Não há seleção de um novo path para religar a ocorrência neste recorte.

Os testes de domínio e modelo cobrem seleção restrita, limites, duplicatas, preservação de ID/path/ordem/metadata, recuperação idempotente e sinais sem reset. Presenter e Quick cobrem a coordenação da tentativa, erro, identidade, carregamento e ações acessíveis. A integração com adapters reais exercita uma WAV autenticada copiada para o mesmo path temporário que estava ausente, repetindo a carga da mesma ocorrência. Essa jornada não usa arquivos pessoais nem equivale a desconectar/reconectar um drive físico.

Antes da consolidação, foram confirmados os Reds pelas APIs ausentes de domínio/modelo, pelos botões Quick ausentes, por cinco falhas de coordenação da tentativa e pela contagem de problemas após recuperação. O core Qt OFF passou 193/193 e o alvo Quick passou 1/1. A integração `restoredSourceRetriesTheSameOccurrenceThroughTheRealBackend` passou 3/3 incluindo setup/cleanup, sem skips: path ausente → WAV restaurada no mesmo path → `Playing` da mesma ocorrência. O formato completo passou em 139 C++/46 QML. Esses resultados intermediários não substituem a consolidação final.

O fechamento aprovou o build Release completo e 244/244 testes no CTest combinado sequencial. O core Qt OFF passou 193/193, e o teste real de arquivo restaurado passou sem skips, mantendo a mesma ocorrência. `m0_qt_shell_ui_qmllint`, formato de 139 C++/46 QML e revisão de código ficaram verdes; QML foi comparado à saída normalizada de `qmlformat`, sem usar a opção inexistente `--check`. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado em exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` também passou.

M4.7 e `NFR-REL-005` continuam abertos globalmente. Este segundo recorte não cobre dispositivo de saída, drive físico, localização do arquivo em outro path, restart do Explorer, sleep físico, pacote/E2E nativo, acessibilidade manual ou Linux. A recuperação de saída é tratada no terceiro recorte abaixo. Nenhum benchmark integra este recorte.

### Audio output M4.7

O terceiro recorte funcional local de M4.7 distingue perda ou troca da saída padrão de uma falha no arquivo. `QtMultimediaPlaybackBackend` observa `QMediaDevices::audioOutputsChanged` por uma fronteira Qt injetável, consulta a identidade da saída padrão e fixa esse dispositivo ao criar cada player. Ausência de saída durante um load aceito produz `AudioOutputUnavailable`; mudança da identidade durante `Loading`, `Ready`, `Paused` ou `Playing` interrompe a geração atual com `AudioOutputUnavailable` ou `AudioOutputChanged`. Alterar a topologia sem mudar a saída padrão não interrompe a faixa. Os comandos de transporte e os sinais do player também reconciliam a saída antes de prosseguir.

Na interrupção, o adapter conserva posição, duração e geração confirmadas, desabilita seek, desconecta os sinais, para e descarrega o player antigo e o retira da saída antes de publicar o erro. Reconectar ou mudar novamente o dispositivo não recarrega nem toca automaticamente. Callbacks tardios, players aposentados e eventos depois de shutdown/destruição não reativam a reprodução. Esses erros pertencem à saída, portanto `LocalTrackSession` não marca o arquivo indisponível e o presenter não percorre a fila como se cada origem tivesse falhado.

A operação existente `executeErrorAction()` despacha a ação fechada `recover-audio-output`. Ela reutiliza o path da ocorrência corrente, sem aceitar ID de dispositivo ou path novo do QML, e recarrega em `RestorePaused`. A tentativa preserva fila, duplicatas, volume, mudo e identidade do modelo; rejeição síncrona do load conserva a sessão anterior. A posição anterior é vinculada à nova geração e restaurada quando a mídia permite seek, com a tolerância e a proteção de EOF já usadas na restauração de sessão. Uma nova perda de saída durante pause/seek conserva o alvo e o snapshot persistível, mesmo quando o erro síncrono do backend ainda aguarda sua entrega enfileirada ao presenter. Uma seleção explícita posterior invalida essa posição. A tentativa aceita também assume a intenção sobre lotes incrementais posteriores, sem autoplay. A UI apresenta causas localizadas distintas, “Tentar novamente” e a alternativa “Abrir outro áudio”, com teclado, descrição acessível e anúncio de erro.

Os testes do adapter usam topologia injetada e fixtures autenticadas para exercitar ausência inicial, perda/substituição nos quatro estados ativos, mudanças irrelevantes, reconciliação em sinais, recuperação reentrante e teardown. Presenter e Quick cobrem ação, posição, identidade da ocorrência, rejeição/erro repetido, seleção que substitui a tentativa e apresentação acessível. Uma integração combina decoder Qt Multimedia e metadata TagLib reais, saída mutada e topologia injetada: pausa uma WAV em 350 ms, simula perda, tenta ainda sem saída e recupera explicitamente em outra saída, sem `Playing` nas gerações de recuperação. Ela não desabilita hardware, não usa arquivos pessoais e não demonstra reconexão física ou áudio audível.

Foram confirmados os Reds pelos novos códigos de erro ausentes, por três falhas de coordenação do presenter e pelo backend que permanecia `Loading` quando não havia saída. A revisão encontrou ainda perda do alvo quando pause/seek falhavam antes da entrega do erro enfileirado: as duas linhas do Red observaram posição persistida em 0 em vez de 42.000 ms. O Green dessa regressão passou 4/4 incluindo setup/cleanup, preservando posição, fila e retry sem play. O Green focado do adapter passou 16/16, o core Qt OFF passou 194/194 e os alvos de presenter/integração passaram 2/2 antes dessa regressão adicional. A integração real recuperou a mesma ocorrência na geração 3, pausada em 350 ms, sem `Ended` nem `Playing` nas gerações de recuperação.

O primeiro gate amplo encontrou um falso positivo no verificador de texto: o ID interno `recover-audio-output`, comparado em um binding `property bool`, era classificado como mensagem visível. Uma regressão confirmou o problema, e o helper passou a reconhecer somente declarações booleanas como esse contexto técnico, mantendo casos negativos de `property string`, `text` e `Accessible.name`. O alvo `ShellUiContractTests` ficou verde em 1/1 depois da correção, sem traduzir IDs internos nem retirar os checks dos catálogos.

O fechamento aprovou o build Release completo e 245/245 testes no CTest combinado sequencial. O core Qt OFF passou 194/194; `m0_qt_shell_ui_qmllint`, revisão de código e formato de 139 C++/46 QML ficaram verdes, com o C++ do helper rechecado após sua correção. QML foi comparado à saída normalizada de `qmlformat`, sem a opção inexistente `--check`. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado com exit 1, sem criar a raiz solicitada; stdout/stderr ficaram vazios no runtime normal autenticado. Esse resultado não é uma alegação de silêncio sob logging Qt forçado. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` também passou.

O recorte acrescenta evidência funcional para `FR-038` e `NFR-REL-005`, sem encerrá-los nem concluir M4.7. USB/Bluetooth/HDMI físicos, atualização real da topologia pelo Windows, áudio audível, sleep físico, drive desconectado, arquivo relocalizado, Explorer, pacote/E2E nativo, acessibilidade manual e Linux continuam sem cobertura deste recorte. A escolha manual de saída dentro do app permanece a evolução `FR-018`/M6.1, não uma funcionalidade entregue aqui. Nenhum benchmark foi executado.

### Taskbar M4.7

O quarto recorte funcional local de M4.7 mantém a janela alcançável após `TaskbarCreated`, sem colocar Windows no domínio. No QPA Windows real, `QtShellPlatform` combina intenção de ícone não vazio com presença de shell/taskbar por `GetShellWindow` e `FindWindowW(L"Shell_TrayWnd", ...)`, além de exigir o monitor nativo pronto antes de permitir ocultação. Falha ao registrar a mensagem ou criar o receptor torna a capacidade indisponível; `refreshAvailability()` pode repetir explicitamente `start()`. Leitores injetados continuam determinísticos, e os outros QPAs conservam seu caminho existente. Essa leitura confirma presença, não o sucesso de `NIM_ADD`: `QSystemTrayIcon::isVisible()` também é somente intenção. A geometria pública do ícone não é usada para negar suporte, pois um ícone legítimo pode estar no overflow.

`WindowsTaskbarEvents` registra `TaskbarCreated` e cria um `QWindow` top-level privado e invisível, independente da janela visual. Somente a mensagem registrada, no tipo `windows_generic_MSG`, dirigida a esse `HWND` vira notificação enfileirada. Mensagens e handles alheios são inertes; shutdown remove o filtro e destrói o receptor, e entregas já enfileiradas usam contexto/guardas para não sobreviver ao encerramento ou à destruição. O receptor não usa `HWND_MESSAGE`, que não recebe o broadcast de recriação da taskbar.

O Qt já tenta registrar novamente seu `QSystemTrayIcon` ao receber essa mensagem. O app não manipula o `HWND` privado do ícone nem duplica o registro: `handleTrayHostRestart()` oferece uma rota conservadora pela janela normalmente minimizada, mas somente quando o próprio adapter havia ocultado essa janela para a bandeja. Janela visível, já minimizada, apenas oculta no bootstrap ou substituída por rebind não é revelada nem recebe nova política de startup. A minimização conserva o modo anterior e não solicita ativação; o caminho Windows do Qt 6.11.2 usa `SW_SHOWMINNOACTIVE` nesse estado. Não são introduzidos flags persistentes de janela. A preferência `startMinimized` não é reaplicada quando o shell retorna, e uma nova notificação não oculta uma janela recuperada.

Saída pendente desabilita interação e adia essa recuperação; o retorno da interação a executa somente se a janela continua sob ownership de ocultação. Saída confirmada, destruição e rebind tornam a recuperação antiga inerte. A integração presenter/plataforma exige que fila, duplicatas, IDs, identidade do modelo, geração, posição e estados `Playing`/`Paused` sejam preservados, sem load, play, pause, seek, unload ou shutdown causados pelo evento. Cancelamento de saída conserva seu comportamento anterior de restaurar a janela para apresentar o problema de persistência.

Os Reds foram confirmados pela ausência do método novo na plataforma e por dez falhas dos contratos de inicialização/probe do monitor antes da implementação. Os testes separam o adapter e binding determinísticos da boundary Windows real. A caracterização nativa adicional remove somente o par ícone/`HWND` verificado como pertencente ao próprio processo de teste, por `NIM_DELETE`, e envia `TaskbarCreated` somente ao receptor desse ícone: não encerra Explorer nem faz broadcast. Neste host, um ícone no overflow retornou `S_FALSE` com retângulo zerado em `Shell_NotifyIconGetRect`, exigindo corrigir o oráculo que antes dependia de dimensões positivas. A prova passou a exigir também ACK de `NIM_MODIFY` com flags zero: esse no-op não altera conteúdo, estado, tooltip ou callback e não pode adicionar um ícone ausente. Antes da remoção e depois de cada uma das duas mensagens de recriação, o ícone próprio deve responder ACK verdadeiro; depois de `NIM_DELETE`, são obrigatórios tanto HRESULT de falha quanto ACK falso. O teste conserva identidade, ícone, tooltip e menu, exercita ativação de teclado pelo callback Qt e limpa seu próprio registro. Isso caracteriza a recuperação já existente no Qt autenticado; não é um novo Red do código da aplicação.

A revisão encontrou ainda uma regressão de reentrância: duas linhas do Red mostraram que saída ou rebind disparados por `windowStateChanged` durante a minimização podiam revelar uma janela indevida. A correção revalida lifetime, saída e geração do binding após o flush e a mudança de estado, antes de tornar a janela visível. Cenários de destruição do adapter durante `windowStateChanged` e durante o flush também foram escritos antes do Green; não basta verificar o estado somente antes de emitir sinais Qt. Um Red adicional no polling mostrou `availabilityChanged` depois de uma saída efetiva disparada durante a minimização; `refreshAvailability()` também revalida `exit_requested_` após esse caminho antes de publicar a capacidade.

A caracterização nativa passou 1/1 pelo CTest, sem skips, mantendo Explorer intacto. O fechamento aprovou o build Release completo, `m0_qt_shell_ui_qmllint` e 247/247 testes no CTest combinado sequencial em 55,43 s, incluindo monitor e bandeja nativos sem skips. O alvo `QtShellPlatformTests` passou seus 51/51 casos internos após as regressões de reentrância. O formato de 143 C++ ficou verde; os 46 QML passaram na comparação com a saída normalizada de `qmlformat`, e seus hashes foram preservados. Não foi usada a opção inexistente `--check`; o problema informado pelo mantenedor pertencia à invocação, enquanto o procedimento do README já era compatível.

A composição de produção `BUILD_TESTING=OFF` foi recompilada em Release, listou zero testes e rejeitou o hook reservado com exit 1, stdout/stderr vazios, sem criar a raiz solicitada nem deixar processo residual. O core Qt OFF não foi reexecutado neste recorte, pois nenhum código do núcleo foi modificado; os 194/194 do recorte anterior não são um resultado novo. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` também passou. Esses checks não alteram os limites de validação nativa descritos abaixo.

M4.7 e `FR-038` continuam parciais. Mensagens dirigidas a receptores de teste e a remoção isolada de um ícone não comprovam reinício real do Explorer, comportamento do desktop durante sua indisponibilidade, pacote instalado, NVDA ou Linux. O recorte não acrescenta prova física de áudio/drive/sleep nem recuperação de arquivo em outro path. Nenhum benchmark integra este trabalho.

### File location M4.7

O quinto recorte funcional local de M4.7 acrescenta `locateQueueOccurrence(ID)` para substituir o path de uma ocorrência indisponível sem recriar a fila. QML fornece somente o ID; a porta C++ pura `AudioFileLocator` recebe o path interno anterior e devolve `Selected`, `Cancelled` ou `Failed` com, quando aplicável, um novo path local. A seleção não comprova existência continuada, permissão de leitura nem decodificação bem-sucedida. `QtAudioFileLocator` usa um diálogo assíncrono vinculado à janela, aceita uma única seleção local e não navega nem sonda automaticamente o path ausente, que pode estar em um drive desconectado. A implementação normal solicita o diálogo nativo; a factory dos testes usa o diálogo widget, sem introduzir hooks no executável de produção.

`LocalFileQueue::replaceUnavailableSource` aceita somente um ID existente indisponível e um path não vazio diferente do anterior, sem I/O ou canonicalização física. A troca conserva identidade, ordem, duplicatas, seleção e allocator. `LocalQueueListModel::replaceUnavailableSource` reconstrói somente aquela linha: usa o nome novo como fallback, limpa título de tags, artista, álbum e duração anteriores, substitui o diagnóstico por `source_relocated` e mantém `available=false`. Os sinais são restritos aos campos de origem/metadata/diagnóstico; índices persistentes, seleção do modelo e as demais linhas permanecem válidos, sem reset.

Quando a ocorrência não é a atual, localizar altera somente seu path e a projeção pendente de verificação, sem load, metadata, play, pause ou seek na faixa que já toca. O retry explícito existente pode verificar o novo path depois; somente a geração correspondente em `Ready`, `Paused` ou `Playing` remove a indisponibilidade. Se a ocorrência localizada ainda é a atual em erro, a aceitação do novo load precede a troca da fila: o presenter abre a nova origem em `RestorePaused`, renova gerações de playback/metadata, limpa arte antiga e restaura a posição confirmada quando há seek, usando as regras existentes de tolerância, duração e EOF. Rejeição do load conserva o path anterior e oferece outra tentativa; não existe autoplay implícito causado pelo seletor.

Um alvo de posição restaurada ainda pendente não é descartado quando a origem falha com `source_unavailable`, `invalid_media` ou `access_denied`; ele permanece disponível para o snapshot e para a recuperação, como já ocorre nos erros recuperáveis de saída. Seleção e transporte explícitos, assim como a gestão de geração, continuam obedecendo aos cancelamentos existentes, sem transferir uma posição antiga a outra faixa.

A solicitação carrega geração, ID e path anterior. A conclusão é reenfileirada mesmo quando a porta a entrega inline; repetição, cancelamento, remoção da ocorrência, path já substituído, recuperação concorrente e entrega após saída/destruição não podem aplicar uma seleção obsoleta. Reordenação não invalida o ID: o destino é resolvido novamente ao concluir. A troca de path alimenta o autosave estrutural pelo snapshot já existente, mantendo JSON de sessão schema v1. A indisponibilidade continua transitória e não é persistida; o bootstrap conserva o contrato anterior de revalidar fontes restauradas, sem transformar a seleção pelo diálogo em prova de decode.

O contrato da UI oferece “Localizar arquivo” no erro e no menu da ocorrência indisponível, conserva abrir outro áudio e retry, identifica a operação em andamento por ID e apresenta falha acionável sem perder contexto. Teclado, nomes/descrições acessíveis, feedback localizado e restauração de foco pertencem aos checks Quick deste recorte. Uma ocorrência com `source_relocated` deve informar que a nova origem ainda precisa ser verificada, não anunciar recuperação confirmada.

Evidência automatizada consolidada deste recorte, sem antecipar o fechamento global:

- core Red: quatro asserções falharam em três dos quatro casos de relocalização diante do stub; Green: 68 asserções em quatro casos, incluindo snapshot round-trip, e CTest Qt OFF 198/198;
- modelo Red: falhas nas duas linhas `new-name` e `same-name-new-directory`; Green: alvo fixture/modelo 41/41 casos internos;
- presenter Red: a nova API ainda ausente impediu a compilação dos contratos; duas regressões adicionais reproduziram, cada uma com 2 passagens/1 falha, a falta de NOTIFY do estado ocupado após cancelamento por saída e o diagnóstico obsoleto após retry aceito. A correção publica `stateChanged` enfileirado e vinculado ao contexto somente em `requestExit`, nunca no destrutor, e limpa o erro de localização do mesmo ID ao aceitar retry. Green final: 175/175 casos internos, incluindo as três linhas de persistência descritas abaixo;
- posição restaurada Red: após corrigir a composição de autosave das fixtures, as três linhas de persistência (`source_unavailable`, `invalid_media`, `access_denied`) salvaram 0 em vez de 42000 ms, e a integração com arquivo renomeado/decoder real terminou em 0 em vez de 350 ms. `reconcileRestoredPosition` foi corrigido para preservar o alvo nesses erros recuperáveis, mantendo os cancelamentos anteriores; as regressões passaram na consolidação final;
- picker Red: 3 passagens e 5 falhas antes da implementação; Green: 8/8 casos internos e CTest 1/1, usando diálogo widget;
- Quick Red: seis falhas para os novos contratos de ação/feedback; Green: suíte Quick completa 122/122 sem falhas ou skips e CTest 1/1;
- integração local com decoder real: 20/20 casos internos, incluindo arquivo renomeado em diretório temporário e recuperação pausada em 350 ms;
- formato completo: repasse final de 147 arquivos C++ aprovado, assim como 47 QML com hashes inalterados pela verificação; build Release combinado e `qmllint` aprovados;
- CTest Release combinado final: 252/252, serial (`-j 1`), em 58,56 s; o coordinator passou 18/18 casos internos sem falhas ou skips nessa rodada;
- composição de produção Release `BUILD_TESTING=OFF` refeita após o ajuste da posição restaurada e aprovada: zero testes listados por CTest, rejeição do argumento de teste reservado com exit 1 e stdout/stderr vazios, sem configuração ou processos residuais;
- validação documental: 44 Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` aprovado.

A primeira consolidação CTest passou 249/252: duas fixtures novas precisaram compor a dependência obrigatória de autosave, e o coordinator terminou em exit 1 sem output. As fixtures foram corrigidas; a causa da falha intermitente do coordinator não foi demonstrada nem corrigida. O coordinator passou 18/18 em reexecuções isoladas com log e na consolidação completa final, executada com `QT_LOGGING_TO_CONSOLE=1` e `QT_FORCE_STDERR_LOGGING=1` para observabilidade. As asserções não foram alteradas: o resultado verde não representa correção de IPC nem benchmark e não apaga o risco residual da primeira falha.

Os alvos relevantes validados são `m0.qt.audio-file-locator`, `m0.qt.fixture.repository-and-model`, `m0.qt.presenter.initial-load`, `m0.qt.quick.shell-states` e `m0.qt.audio.local-integration`, além do núcleo puro e da suíte combinada. Nenhum resultado de recorte anterior substitui esses checks; a consolidação automatizada não inclui as jornadas nativas e físicas abaixo.

M4.7, `UX-008` e `NFR-REL-005` continuam parciais. O diálogo nativo não foi inspecionado manualmente; testes de widget/fakes não comprovam sua jornada Windows ou acessibilidade com NVDA. Drive físico desconectado, áudio audível, dispositivos/sleep físicos, restart real do Explorer, pacote/E2E nativo e Linux permanecem fora da evidência deste checkpoint. Esses ensaios residuais não substituem a prioridade atual de desenvolver as funcionalidades Windows pendentes; cenários físicos ou disruptivos exigem uma rodada própria. Não há mudança de stack aceita, schema de persistência, matriz pública de formatos ou escopo de fontes remotas. Nenhum benchmark integra o recorte.

### Async audio picker

O incremento de seleção principal assíncrona retoma os contratos funcionais de M2.1/M3.1 no candidato Qt/Windows. A composição de produção deixa de chamar `QFileDialog::getOpenFileNames` e passa a usar a porta C++ pura `AudioFilesPicker`: `pick(completion)` aceita no máximo uma solicitação, e a conclusão `Selected`, `Cancelled` ou `Failed` devolve paths locais completos preservando ordem e duplicatas. Aceitar a solicitação não significa que a fila já mudou nem comprova leitura ou decode. A porta é ligada por `setAudioFilesPicker` somente em C++; QML não recebe paths do seletor. Os construtores com picker síncrono legado permanecem para fakes/testes; a produção usa um fallback inerte e a porta assíncrona real.

`QtAudioFilesPicker` e `QtAudioFileLocator` compartilham o helper interno `QtAudioFileDialog`, com modos `ExistingFiles` e `ExistingFile`, respectivamente. O diálogo é vinculado à janela, aberto por `open()` assíncrono, somente para seleção local e sem sondar mídia. O localizador conserva seu contrato de não navegar automaticamente para a origem ausente. A factory widget viabiliza testes determinísticos; a implementação normal solicita o backend nativo Windows, sem hook novo no executável de produção.

O presenter reenfileira a conclusão mesmo se a porta chamar inline, usando geração e lifetime para descartar resultados antigos. Abrir/Adicionar não podem se sobrepor entre si ou à localização de ocorrência; cancelar, trocar a porta, sair e destruir o owner invalidam a solicitação. Abrir o seletor não envia comandos de áudio: a reprodução, os eventos do backend, os comandos de mídia do sistema, o IPC e o polling normal continuam operantes enquanto ele aguarda a escolha. Isso não proíbe comandos legítimos do usuário ou avanço por `Ended` durante a seleção.

A conclusão é aplicada à fila viva, não a uma cópia capturada antes do diálogo. Abrir anexa os arquivos e toca a primeira ocorrência nova; Adicionar anexa conservando a faixa então atual e somente inicia a primeira nova quando já não há faixa. Remoção, reordenação, limpeza e entrada por IPC enquanto se escolhe não são revertidas. Todos os paths recebidos são validados antes da alteração: lote vazio ou com path relativo, sem nome de arquivo ou com NUL é rejeitado atomicamente. Cancelar é inerte; rejeição do load preserva fila e faixa anteriores. O autosave e os schemas de preferências/sessão permanecem os existentes.

O contrato de apresentação expõe `selectingAudioFiles`, `audioFileSelectionFailed` e `audioFileSelectionFinished(bool changed)`. Estado ocupado não equivale a saída pendente; ações de nova seleção/localização ficam indisponíveis sem suspender o engine. Foco e feedback dependem da conclusão efetiva, e não do retorno que apenas aceitou a solicitação. A restauração de foco revalida janela ativa, modal, saída e identidade da seleção, sem reutilizar índice obsoleto nem roubar foco de outro diálogo. Cancelamento externo por rebind/saída pode encerrar o estado ocupado sem emitir `Finished`; a UI limpa seus marcadores também nesse caminho, inclusive no mesmo turno de eventos ou em turno posterior. A liberação da fila e tarefas de foco diferidas, incluindo `setQueueOpen`, respeitam uma seleção nova.

Evidência automatizada consolidada deste incremento, sem concluir os marcos globais:

- presenter Red inicial confirmado por ausência das novas APIs; a implementação foi acrescentada depois desse Red e passou um Green intermediário de 192/192. O Red funcional adicional passou 2 casos e falhou em 4: a rejeição da seleção apagava a descoberta ativa em Abrir/Adicionar, e cancelamento externo reentrante reabria a seleção durante saída/rebind. O caso de destruição no cancelamento apresentou 4 passagens nas asserções, mas o processo terminou em `0xc0000005` no cleanup `cleanupThreadData`; essa execução não foi verde. Os fixes foram revisados, e o Green posterior completo do presenter passou 200/200, sem skips e com exit 0;
- destruição durante completion: o baseline 4/4 já estava verde. Os guards dentro dos helpers de aplicação da seleção são hardening/refactor; não há Red alegado para esse ajuste preventivo;
- picker Red confirmado com 3 passagens/6 falhas antes da implementação; o localizador passou o baseline 8/8 antes da extração do helper comum. Green final dos adapters: picker 13/13 e localizador 8/8, com CTest 2/2 em 3,26 s, sem skips;
- UI Red inicial confirmado com 120 passagens/10 falhas, sem skips; o Red adicional de marcadores/foco programado apresentou 131 passagens/3 falhas. Após as correções, Quick completo passou 135/135 e seu alvo CTest passou 1/1 em 21,76 s; a consolidação final posterior passou 136/136, sem skips;
- uma caracterização adicional da UI percorreu recuperação de importação seguida de cancelamento e passou 3/3, sem patch de produção; ela também passou na consolidação Quick final;
- build Release completo e `all_qmllint` aprovados; isso não substitui a suíte CTest completa;
- formato final aprovado em 153 arquivos C++ e 48 QML; hashes dos QML inalterados. O check QML compara stdout normalizado em LF, sem `--check`, conforme o procedimento já documentado;
- composição de produção Release `BUILD_TESTING=OFF` aprovada: build exit 0, zero testes listados por CTest, argumento de teste reservado rejeitado em exit 1 com stdout/stderr vazios, raiz de configuração não criada e nenhum processo residual;
- CTest Release combinado final passou 253/253 em 60,28 s, com exit 0 e ambiente por contrato. O log final confirmou presenter 200/200, Quick 136/136, localizador 8/8, picker 13/13, coordinator 18/18, playback real 50/50 e integração real 20/20, todos sem skips;
- validação documental: 44 Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` aprovado. Nenhum processo `m0_qt` do workspace permaneceu ativo ao final da verificação;
- revisão independente encontrou falha de lifetime no rebind: o cancelamento do diálogo pode destruir a nova janela recebida por referência antes de ela ser vinculada. A regressão `reentrantCancellationMayDestroyTheNewWindowBeforeBinding` confirmou Red com crash `0xc0000005` em `QWindow::setTransientParent`; a correção guarda `QPointer<QWindow>` antes de cancelar e recusa o vínculo quando a janela deixou de existir. O Green dos adapters inclui essa regressão.

A primeira execução CTest completa passou 250/253. Dois alvos de áudio abortaram com `0xc0000409` porque a execução exportou indevidamente `QT_FATAL_WARNINGS=1` globalmente, além da configuração por alvo do CMake. A rodada final removeu somente essa exportação global indevida; não houve mudança de CMake ou relaxamento das expectativas dos testes para aceitar avisos. O coordinator passou 17/18 na primeira rodada: `pausesAndResumesAcceptanceUntilShutdownBecomesFinal` observou outcome `Failed` (4), não `Timeout` (3), sem o detalhe necessário para demonstrar a causa. Sua asserção ganhou apenas diagnóstico de detalhe/outcome/pending, sem alterar timeout ou expectativa. A rodada final completa passou com ambiente corrigido e logging forçado somente para observabilidade, sem loop de repetições. A causa da intermitência do coordinator continua não confirmada: o retorno verde não é uma correção de IPC.

A revisão da UI encontrou marcadores de solicitação que permaneciam ativos quando cancelamento por rebind/saída não emitia `Finished`; as regressões e o Green acima cobrem esse caminho e o foco programado. O core Qt OFF não foi reexecutado neste incremento, pois não houve mudança de comportamento no núcleo; seus resultados anteriores não são resultados atuais. Os checks automatizados e a revisão deste incremento estão consolidados, sem usar resultados do incremento anterior como prova nova. O diálogo nativo Windows ainda exige inspeção própria, assim como NVDA, pacote, Linux e demais gates nativos; fakes/widget não comprovam a jornada visual nativa. M2 e M3 não estão concluídos, e nenhuma stack, formato novo ou benchmark é aprovado por este recorte.

Naquele fechamento, a sanitização textual e numérica de metadata em `FR-023` era o próximo backlog funcional concreto. Conversão UTF-8, limites de retenção e apresentação `PlainText` não substituíam uma política explícita e testes para textos de controle/vazios semânticos e valores numéricos inválidos. Número da faixa e ano já eram extraídos e exibidos quando conhecidos; não se tratava de campos ausentes. O incremento seguinte está registrado abaixo, sem alegação de exploração ou vulnerabilidade demonstrada.

### Metadata sanitization

O incremento funcional de `FR-023` trata os valores de título, artista, álbum, número da faixa e ano no candidato Qt/Windows. A policy `MetadataSanitization` pertence ao core C++20 puro, sem Qt, TagLib ou I/O. Core, extrator, apresentação e caracterização integrada real estão verdes, com checks automatizados finais consolidados. Número da faixa e ano já existiam no produto experimental: este trabalho explicita a validade dos valores, não acrescenta campos antes ausentes.

Política textual implementada:

- validar toda a entrada como escalares UTF-8; sequências truncadas, bytes de continuação isolados, overlong, surrogates e valores acima de U+10FFFF produzem `InvalidEncoding`, sem conservar prefixo parcial;
- transformar U+0000–U+0020, U+007F–U+009F e os espaços U+00A0, U+1680, U+2000–U+200A, U+2028/U+2029, U+202F, U+205F e U+3000 em separação única por espaço ASCII, removendo a separação no início/fim;
- remover a lista explícita de controles de layout U+061C, U+200B, U+200E/U+200F, U+202A–U+202E, U+2060, U+2066–U+2069 e U+FEFF. Isso não promete remover toda categoria Unicode invisível;
- preservar os demais escalares byte a byte, incluindo acentos compostos/decompostos, scripts, ZWNJ/ZWJ, variation selectors e sequências emoji. Não há transliteração, normalização de forma Unicode nem interpretação de markup; a UI continua usando texto plano;
- produzir `Unchanged`, `Sanitized` ou `Empty` conforme o resultado. Valor vazio após essa política fica ausente e título ausente usa o fallback do arquivo. O caller aplica primeiro o limite dos bytes UTF-8 originais: sanitizar espaços/controles não pode tornar aceitável uma entrada originalmente acima de 16 KiB. O orçamento agregado de retenção continua 256 KiB.

Política numérica implementada:

- somente dígitos ASCII, após aparar espaço ASCII, tab, CR, LF, VT e FF nas bordas; não aceitar sinais, controles internos, NUL, lixo após número ou leitura parcial;
- número da faixa entre 1 e `uint32_t` máximo, com zeros à esquerda permitidos; `faixa/total` também é aceito quando ambos são decimais positivos no mesmo intervalo. O total não é publicado e não se exige `faixa <= total`;
- ano simples entre 1 e 9999, com um a quatro dígitos; datas exigem `YYYY-MM` ou `YYYY-MM-DD`, com mês/dia e ano bissexto verificados pelo calendário;
- data completa pode ter `T` seguido de `HH`, `HH:MM` ou `HH:MM:SS`, com fração decimal não vazia somente após segundos e sufixo opcional `Z` ou `±HH:MM`. Horas são 00–23; minutos/segundos, 00–59; o offset usa os mesmos intervalos de hora/minuto. Não há parse genérico ISO-8601, segundos intercalares, formas compactas ou correção automática de datas impossíveis.

O adapter aplica o limite por campo à representação UTF-8 original antes de sanitizar ou interpretar números. O orçamento agregado contabiliza os textos efetivamente retidos e quatro bytes por número válido: o máximo integrado dos cinco campos passa de 48 KiB para 48 KiB + 8 bytes, ainda sem alcançar a fronteira de 256 KiB. Diagnósticos `TextSanitized` e `InvalidValue` são não fatais e deduplicados por código/campo. Ausência real não gera valor zero falso; texto não vazio que se torna vazio/inválido é descartado com diagnóstico e conserva o fallback do título.

A interpretação numérica usa as representações públicas do formato, sem depender de getters que já fizeram conversão parcial ou signed. No ID3v2, frames `TRCK`/`TDRC` preservam o separador `T` da data antes do parser; no ID3v1, o snapshot em memória preserva os quatro bytes do ano. Fallback entre subtags é por campo, não por tag inteira, e Vorbis conserva aliases `TRACKNUM`/`YEAR` quando aplicáveis. Para o par binário `trkn` MP4, o adapter recupera o intervalo unsigned de 16 bits projetado como signed pelo TagLib; não generaliza essa correção a texto numérico inválido. A extração continua no worker existente, sem nova leitura de arquivo para o snapshot ID3v1.

Presenter e QML projetam somente os códigos `metadata_text_sanitized`/`metadata_invalid_value` e os campos fechados, agora incluindo `track_number`/`year`. Microcopy localizada distingue ajuste para exibição de valor ignorado, sem mostrar o conteúdo descartado nem uma contagem de bytes enganosa nesses dois diagnósticos. Os checks do extrator, da apresentação, da caracterização nova com decoder real e da consolidação completa estão verdes.

Evidência automatizada consolidada deste incremento:

- policy pura: Red em 10 casos, com 7 casos falhando e 51 asserções falhas; Green posterior nos mesmos 10 casos com 99 asserções. A suíte completa core Qt OFF passou 208/208 e repetiu 208/208 em 3,04 s, exit 0, após o ajuste final mecânico de include/formato;
- adapter TagLib: Red em 10 casos, com 9 casos falhando e 62 asserções falhas; Green focado passou 10/10 com 320 asserções, e o grupo CTest de metadata passou 35/35;
- apresentação: Reds confirmados em cinco linhas de Presenter e dois casos Quick; Greens completos de Presenter 205/205 e Quick 138/138;
- revisão independente read-only não encontrou regressão concreta na policy de UTF-8, espaços, ZWJ/ZWNJ/emoji ou parsers de calendário/`uint32_t`. Não foram executados builds ou testes adicionais pelo revisor.

A nova caracterização integrada passou na primeira execução, sem Red de produção alegado: `m0.qt.audio.local-integration` passou 1/1 em 3,97 s (4,01 s total). O `Testing/Temporary/LastTest.log` do build CTest confirmou 21 casos sem skips, incluindo `invalidTagsAreSanitizedWithoutInterruptingRealDecodedPlayback`. O teste deriva um MP3 temporário autenticado em path Unicode, ajusta título com controles, descarta artista vazio e faixa/ano inválidos, preserva álbum, confirma `Playing` seguido de pausa e uma única carga sem erro, usando Qt Multimedia/TagLib reais e saída mutada.

Build Release combinado e `all_qmllint` passaram com exit 0. O formato passou em 156 arquivos C++ e 48 QML; os hashes QML foram preservados, comparando stdout normalizado em LF, sem `--check`. A revisão independente não identificou achado P0–P2. A composição de produção `BUILD_TESTING=OFF`, com MSVC 14.44/SDK 22621, passou Release, listou zero testes, rejeitou o argumento reservado com exit 1 e stdout/stderr vazios, não criou a raiz de configuração com UUID e não deixou processo residual. A suíte Release combinada final passou 273/273 em 65,69 s, exit 0. O `LastTest.log` confirmou 273 testes aprovados, nenhuma falha/skip e, internamente, Presenter 205, Quick 138 e integração real 21 novamente sem skips. Nenhum processo `m0_qt` próprio do workspace permaneceu ativo ao final.

A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas, e `git diff --check` dos sete documentos contratuais ficou verde. O próximo checkpoint é uma auditoria curta dos fluxos Windows restantes contra os requisitos aceitos, seguida de um recorte TDD para a próxima lacuna funcional comprovada. Os checks deste fechamento não precisam ser repetidos sem mudança relevante; parser budget, corpus, benchmarks ou ensaios nativos não são automaticamente a próxima tarefa de desenvolvimento.

Limites: a política de retenção própria atua depois da conversão do TagLib e não limita alocações internas desse parser. Derivados temporários de fixtures autenticadas não ampliam o corpus canônico, e este recorte não fecha corpus hostil/fuzz, `FR-023`, `CHK-SEC`, M2/M3 ou gates de pacote/NVDA/Linux. Nenhuma exploração ou vulnerabilidade é afirmada. O foco continua nas funcionalidades Windows, sem benchmarks, A/B, pacote-v2 ou ensaios físicos disruptivos.

### Transport recovery

Este recorte corrige dois contratos já existentes de navegação e recuperação (`FR-011`, `FR-014` e `UX-008`), sem acrescentar escopo ao produto. No modo local em `Error`, anterior/próxima podem continuar disponíveis no presenter, mas o pai QML desabilitado e o gate comum dos atalhos impediam usar essas ações. A UI passa a respeitar a capacidade de cada ação, mantendo play/seek indisponíveis quando apropriado e preservando um destino de foco utilizável. O reparo de foco consulta capacidades do presenter por controle, sem depender de `item.enabled` antes de o binding atualizar; conserva foco na navegação ainda disponível e não o desloca por `seekPending` transitório.

O segundo bug aparece quando commit de preview, salto para trás ou salto para frente recebe `NotSeekable`, `Busy` ou `NotReady` do backend local. A rejeição retornava sem `playbackChanged`/`seekRejected`, deixando a UI sem sincronização do preview ou feedback. A correção reconcilia o preview com a posição confirmada sem posição falsa, publica feedback acessível e protege sinais contra destruição ou rejeição obsoleta após reentrada.

Evidência confirmada: o Red Quick de navegação passou 3 casos e falhou em 5, cobrindo dois botões, dois atalhos e foco. O Red do presenter para seek passou 3 casos e falhou em 11: nove linhas de commit/backward/forward × `NotSeekable`/`Busy`/`NotReady`, mais duas regressões reentrantes. O Green do presenter passou 14/14 no foco e 217/217 no alvo completo. O novo grupo Quick passou 10/10; `AccessibilityStateFocus` passou 7/7. A cobertura inclui arraste visual, feedback acessível/foco e controles filhos que continuam desabilitados mesmo com o contêiner de transporte habilitado.

A primeira tentativa de Green do novo grupo Quick teve quatro falhas por expectativa incorreta de ausência de autoplay: retry explícito usa `Autoplay` no contrato vigente. A expectativa do teste foi corrigida sem mudar produção. A primeira suíte completa passou 272/273: somente Quick falhou, com 147/148 casos internos, no teste já existente de perda da capacidade de seek. O código consultava `item.enabled` antes da atualização do binding; o ajuste passou a consultar capacidades expostas pelo presenter, mantendo os testes originais. Revisões independentes não encontraram achado P0–P2.

Após o último ajuste de foco QML, build Release e `all_qmllint` passaram, e a suíte CTest completa passou 273/273, sem falhas, em 63,82 s. O `LastTest.log` confirmou Presenter 217/217 e Quick 148/148, sem falhas ou skips. A composição final `BUILD_TESTING=OFF` também foi reconstruída (9/9): CTest listou zero testes, o argumento reservado retornou exit 1 com stdout/stderr de 0 B, a raiz temporária de configuração não foi criada e o processo iniciado terminou sem resíduo. O formato cobriu 156 C++/49 QML; após o último ajuste, `Main.qml`, `TransportBar.qml` e o novo teste Quick foram reconferidos por stdout normalizado em LF com hashes preservados, e os demais arquivos não mudaram desde o passe completo. Core Qt OFF não foi reexecutado, pois nenhum comportamento do núcleo mudou. Não restou processo `m0_qt` do workspace. Validação documental: 44 Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` dos quatro docs aprovado.

Os resultados deste recorte não reutilizam o Green do incremento anterior como prova nova. O ambiente offscreen/software não comprova pacote ou integração física nativa; NVDA/nativo, corpus, Linux e fases amplas permanecem com seus limites próprios. Nome definitivo do app ainda aguarda resposta; não se infere identidade para registrar associações. Nenhum benchmark ou ensaio disruptivo integra esta correção.

### Alto contraste — implementação e evidência automatizada

A fatia HC está implementada sob a política comum da seção 7 do [registro M0](../../../../docs/m0/README.md), com checks automatizados e revisão de código aprovados. A paleta do sistema prevalece temporariamente sobre os três temas sem sobrescrever a preferência escolhida; sair reaplica a escolha atual, e movimento continua independente. A nota `preferences-high-contrast-note`, com ID acessível explícito, explica essa precedência. `HighContrastPalette` projeta cinco cores opacas, validadas antes da publicação, sobre conteúdo, foco, hover, seleção e controles desabilitados; `TransportSlider` concentra a apresentação de progresso/volume. A scrollbar controlada mantém opacidade 1 em HC e usa feedback de 120/0 ms fora dele, sem fade residual no modo reduzido.

O adapter usa `QAccessibilityHints` para detectar o modo e `GetSysColor` no Windows para suas cores, com fallback `QPalette` não validado nativamente no Linux. A leitura inicial antecede o QML; notificações de esquema/contraste/paleta e mensagens Windows são coalescidas após o processamento Qt. A fixture e o protocolo continuam intocados.

A auditoria independente encontrou três P2, reproduzidos em Reds Quick sequenciais de 9 passagens/1 falha: handle focado usando `Highlight` sobre `Window`, `Accessible.id` ausente na nota HC e anel de foco do ComboBox com contraste aproximado de 1,09:1. Pares de cor e ID foram corrigidos com regressões; a revisão independente de código confirmou os achados resolvidos.

Checks confirmados nesta fatia, separados dos resultados históricos acima:

- Contrato C++: Red 12 passagens/6 falhas → Green 18/18, repetido três vezes; adapter: Red 17 passagens/16 falhas → Green 33/33, com 30 repetições por CTest.
- `tst_high_contrast.qml`: Red por método ausente e depois HC falso → Green; Quick completo 10/10 sem skips, também 10/10 em cada escala offscreen 1.5/2. Quick e smoke do executável passaram dez vezes cada.
- CTest Release estrito final, após os três ajustes da auditoria: 58/58 em 8,38 s; `qmllint` silencioso; formato de 36 C++/17 QML e diff check aprovados; fixture/markers com hashes inalterados. As escalas e as dez repetições de cada teste acima foram reexecutadas após esses ajustes.
- Core Qt OFF: 51/51, 473 asserções e auditoria de 113 comandos/cache/imports sem Qt. Rebuild incremental `BUILD_TESTING=OFF` pós-ajustes aprovado: zero testes, 153 comandos sem hooks/fontes de teste, nenhum executável de teste nem DLL Qt Test.

No build sem testes atualizado, o default estrito ficou vivo por 1,5 s sem output e foi encerrado pelo próprio PID, sem processos residuais; isso não prova saída graciosa. Cenário indisponível e sua combinação com smoke artificial foram rejeitados com exit 1 no build HC antes das três correções visuais, que não alteraram o parsing. O registro M0 detalha esses limites; não se trata de build novo.

Os testes usam paletas, leitores e notificações injetados: verificam a apresentação do candidato, não a leitura real de `GetSysColor`, a alternância de HC do Windows, ramos internos do Qt Basic ou Linux. Escalas offscreen não comprovam DPI real/NVDA, e não houve inspeção manual da UI nessa fatia HC. Os resultados de `ab87803` acima permanecem como histórico separado desta fatia.

M0.2 permanece em andamento. Validação nativa completa do pacote, medições, bandeja/alto contraste nativos e acessibilidade manual continuam pendentes; UX-006 não está encerrado. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida. A [observação posterior do pacote `63717e5`](../../../../docs/m0/evidence/m0-2-msvc-package.md) registrou abertura/pausa/fila/Preferências/tema, mas ficou parcial e não fecha os gates nativos. A prova funcional de virtualização não declara NFR-PERF-006 aprovado; o gate de stall exige o harness externo de M0.4, e a carga única da fixture em memória não comprova processamento incremental/cancelável de arquivos. Existe agora uma jornada funcional de áudio real com saída mutada, ainda sem prova audível, automação do diálogo ou pacote atualizado; não há rede, leitura automatizada de biblioteca pessoal nem dependência Qt no core.

## Bandeja e ciclo de vida

A [política comum da seção 8](../../../../docs/m0/README.md) está implementada no candidato Qt. `QtShellPlatform` usa `QSystemTrayIcon`/`QMenu`, com `QApplication` e Qt Widgets somente na borda do shell. O core continua C++ puro. O menu usa o transporte do presenter e expõe título atual/contextual, reproduzir/pausar, anterior/próxima, mostrar/ocultar e sair. Rótulos e avisos adicionais ficam centralizados em `LifecycleText`; o ícone reutiliza o SVG autenticado da fixture.

Fechar respeita a preferência; ocultar explicitamente a ignora; sair encerra uma única vez. O primeiro fechamento para bandeja oferece confirmação/cancelamento e um checkbox de aviso somente nesta execução. Sem bandeja observada, a janela é minimizada normalmente e disponibiliza uma explicação na restauração, inclusive se a capacidade mudar durante a tentativa de ocultar. O timer coarse de 1 s consulta disponibilidade somente enquanto a janela está oculta. O adapter preserva o último modo normal/maximizado/fullscreen e não alterna visibilidade em cliques repetidos no ícone. A API Windows do Qt retorna disponibilidade constante; isso não comprova presença do ícone ou recuperação do Explorer.

O novo smoke `--m0-smoke=tray-lifecycle`, exclusivo de `BUILD_TESTING=ON` e cenário `Empty`, exercita o executável real com capacidade de bandeja injetada e plataforma offscreen: menu, pausa, aviso QML, ocultação/restauração maximizada, navegação, perda de capacidade e saída. Sucesso exige código zero e janela fechada pelo caminho enfileirado normal; o watchdog só pode produzir falha. A trava de saída precede `window.close()` e `quit()`, evitando que o fechamento retorne à bandeja. Não há áudio, rede ou automação do desktop nesses testes.

Os checks de ciclo de vida incluem core/presenter, contrato do adapter, binding integrado, jornadas Quick e o smoke real. A suíte Release estrita passou 65/65; o core Qt OFF passou 55/55 com 1.849 asserções. Regressões cobrem perda tardia da bandeja, cancelamento/reabertura durante fade e foco de Preferências/fila concorrendo com o modal. Os detalhes e limites dos checks ficam na seção 8 do registro M0. A bandeja nativa, NVDA, DPI/HC reais, Linux e a validação nativa do pacote permanecem pendentes; o smoke local offscreen está registrado separadamente abaixo. Não há conclusão integral de FR-030/FR-031/FR-032.

## Checks de formato C++ e QML

Execute na raiz do repositório após o preflight autenticado, com PowerShell 7 e `rg` disponíveis. O caminho abaixo é o `clang-format` 19.1.1 do Visual Studio validado no host. O `qmlformat` 6.11.2 não oferece `--check`; o check compara sua saída padrão com o arquivo normalizado para LF, sem editar os arquivos. Seu `--dry-run` apenas informa settings e não substitui essa comparação.

```powershell
$formatRepo = (Resolve-Path .).Path
$formatSource = Join-Path $formatRepo 'experiments\m0\candidates\qt\shell'
$clangFormat = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe'
$qmlFormat = Join-Path $formatRepo 'out\m0\toolchains\qt-6.11.2-msvc2022_64\bin\qmlformat.exe'
if (-not (Test-Path -LiteralPath $clangFormat -PathType Leaf) -or
    -not (Test-Path -LiteralPath $qmlFormat -PathType Leaf)) {
    throw 'Execute o preflight: ferramentas de formato ausentes.'
}

$cppFiles = @(rg --files $formatSource -g '*.cpp' -g '*.hpp')
if ($LASTEXITCODE -ne 0 -or $cppFiles.Count -eq 0) { throw 'Falha ao enumerar C++.' }
& $clangFormat --dry-run --Werror @cppFiles
if ($LASTEXITCODE -ne 0) { throw 'Formato C++ divergente.' }

$qmlFiles = @(rg --files $formatSource -g '*.qml')
if ($LASTEXITCODE -ne 0 -or $qmlFiles.Count -eq 0) { throw 'Falha ao enumerar QML.' }
foreach ($qmlFile in $qmlFiles) {
    $formattedLines = @(& $qmlFormat --newline unix $qmlFile)
    if ($LASTEXITCODE -ne 0) { throw "qmlformat falhou: $qmlFile" }
    $formatted = ($formattedLines -join "`n") + "`n"
    $original = (Get-Content -LiteralPath $qmlFile -Raw).Replace("`r`n", "`n").Replace("`r", "`n")
    if ($formatted -cne $original) { throw "Formato QML divergente: $qmlFile" }
}
Write-Output "Formato OK: $($cppFiles.Count) C++ e $($qmlFiles.Count) QML."
```

## Ambiente de build MSVC 14.44

O preflight Qt histórico permanece imutável. O [contrato de refresh](../../provisioning/qt-shell/msvc-refresh-contract.v1.json), instalado e validado conforme a [evidência de instalação](../../../../docs/m0/evidence/m0-2-msvc-installation.md), fixa separadamente MSVC 19.44.35228.0, linker/dumpbin 14.44.35228.0 e toolset 14.44.35207. Builds antigos com 19.43 não são aceitos pela receita atual; não reaproveite seus diretórios.

Após o preflight autenticado, abra uma sessão nova de PowerShell 7 x64 na raiz do repositório e selecione explicitamente a instância e o SDK:

```powershell
$m0VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\BuildTools'
Import-Module (Join-Path $m0VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $m0VsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0'
$m0CompilerBin = Join-Path $m0VsRoot 'VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64'
$m0Compiler = Join-Path $m0CompilerBin 'cl.exe'
$m0Linker = Join-Path $m0CompilerBin 'link.exe'
$m0Ninja = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
```

A receita autentica CMake, Ninja, `windeployqt` e `qtpaths`, além das ferramentas MSVC, antes de executá-las e exige o ambiente MSVC/SDK esperado. Não corrige silenciosamente `INCLUDE`, `LIB` ou flags herdadas; use uma sessão limpa se houver divergência. O Ninja histórico autenticado continua sendo usado por path absoluto, sem depender de sua presença no `PATH` da nova instância. Isso não é autenticação transitiva de todo codegen/loader: `qmlimportscanner` não tem pin próprio no descriptor histórico, e todos os headers/libs do SDK não são individualmente atestados.

## Build e testes do core

Na sessão preparada acima, use um diretório próprio do novo compilador:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\core-msvc-14.44-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DM0_CATCH2_SOURCE_DIR=$catch2" "-DCMAKE_CXX_COMPILER=$m0Compiler" "-DCMAKE_LINKER=$m0Linker" "-DCMAKE_MAKE_PROGRAM=$m0Ninja"
& $cmake --build $build --target m0_qt_core_tests
& $ctest --test-dir $build -C Release --output-on-failure -R '^m0\.qt\.core\.'
```

O Catch2 precisa ser o source tree autenticado do preflight e o build permanece sob `out/m0`; nenhuma dependência é baixada durante configure ou build.

## Build e testes com suporte Qt

Para validar também o repository da fixture, o modelo da fila e a jornada QML, use a mesma shell x64 e a toolchain Qt autenticada. Os caminhos-fonte da fixture, do manifesto e do protocolo são fechados pelo próprio scaffold e não são parâmetros substituíveis; o executável consome cópias autenticadas em recursos `qrc`:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\support-msvc-14.44-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$qt = Join-Path $repo 'out\m0\toolchains\qt-6.11.2-msvc2022_64'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DM0_CATCH2_SOURCE_DIR=$catch2" -DM0_QT_SHELL_SUPPORT=ON "-DCMAKE_PREFIX_PATH=$qt" "-DCMAKE_CXX_COMPILER=$m0Compiler" "-DCMAKE_LINKER=$m0Linker" "-DCMAKE_MAKE_PROGRAM=$m0Ninja"
& $cmake --build $build
& $cmake --build $build --target m0_qt_shell_ui_qmllint
& $ctest --test-dir $build -C Release --output-on-failure
```

No build do harness acima, que mantém `BUILD_TESTING=ON`, o cenário congelado de erro pode ser aberto manualmente no próprio executável e recuperado com “Pular”:

```powershell
$env:PATH = (Join-Path $qt 'bin') + ';' + $env:PATH
& (Join-Path $build 'm0_qt_shell.exe') --m0-scenario=unavailable
```

Para repetir apenas os contratos de bandeja e seu smoke sem abrir UI nativa:

```powershell
& $ctest --test-dir $build -C Release --output-on-failure -R '^m0\.qt\.(shell-platform(-binding)?|shell-executable\.tray-lifecycle)$'
```

Em um diretório novo configurado com `-DBUILD_TESTING=OFF`, os cenários/hooks de smoke são rejeitados, e não há executáveis de teste nem dependência do app em Qt Test. O build novo `out/m0/builds/qt-shell/tray-no-tests-20260827` comprovou isso; a observação de startup offscreen por 1,5 s usou encerramento forçado somente para cleanup, não como prova de saída normal.

Configure com `M0_QT_SHELL_SUPPORT=OFF` — o padrão — continua compilando e testando o core sem localizar Qt nem ler a fixture.

## Build da jornada local real

`M0_QT_AUDIO_SUPPORT=ON` adiciona `m0_qt_audio_adapters`, com Qt Multimedia, e `m0_taglib_metadata_extractor`, com TagLib dinâmico. A porta C++ de playback expõe snapshot, load/unload/shutdown, callback de eventos e resultados tipados. O fake e o adapter real compartilham a ordem snapshot final → `ended` e o descarte por geração. O adapter cria uma sessão `QMediaPlayer` por geração e descarta a anterior estruturalmente; seus testes cobrem load e `ended` único em `PB-001`/`PB-006` para WAV PCM, MP3 CBR/VBR/Unicode, M4A/AAC, FLAC 16-bit e Ogg/Vorbis, seek confirmado em `PB-004` para MP3 VBR/M4A AAC, além de play/pause idempotentes, replay, lifecycle, erro tipado, reentrada, troca rápida, extensão enganosa e recuperação após vazio/truncado nos recortes já documentados. O adapter também observa a capacidade de áudio publicada pelo Qt: M4A truncado que chega a um estado carregado sem faixa de áudio permanece `Loading` durante uma confirmação single-shot de 100 ms e então termina em erro tipado `InvalidMedia`, em vez de `Ready`; isso evita tratar como fatal uma ordem transitória entre os sinais `LoadedMedia` e `hasAudio`. Os testes do extrator cobrem os casos canônicos `MR-001`, `MR-003`, `MR-018`, `MR-019` e `MR-101`–`MR-103`: a matriz básica por conteúdo — inclusive M4A/AAC, FLAC e Ogg/Vorbis sob extensão neutra `.audio` —, tags Unicode, stream MP3 sem ID3 com fallback Unicode e rejeição tipada de ID3 truncado/frame-sync aleatório. Regressões adicionais derivam prefixos temporários de 64 bytes de MP3 CBR, M4A/AAC, FLAC e Ogg/Vorbis e verificam `InvalidData` no extrator, erro/recuperação no playback e ausência de eventos da geração truncada após uma carga válida imediata. Esses derivados são criados no diretório temporário do teste a partir de fontes autenticadas; não são fixtures canônicas nem alteram o catálogo M0.5. Uma caracterização separada, sem tag `MR-018`, retorna `UnsupportedFormat` para Opus/ALAC somente no extrator. Outro teste cobre ISO BMFF `ftyp` com `largesize`: após o Red `UnsupportedFormat`, o Green reconhece o contêiner e retorna `InvalidData` antes da metadata inválida. Antes de compilar os testes, o configure autentica tamanho e SHA-256 das seis fixtures básicas de codec além do WAV e também dos dois candidatos. A porta C++ pura `MetadataReader` usa `SingleWorkerMetadataReader` para executar esse extrator síncrono fora do caller, manter somente um trabalho ativo e o pendente mais recente e publicar polling terminal tipado para cancelamentos, deadlines e gerações antigas em `MR-016`, `MR-017` e `MR-020`; uma chamada TagLib já iniciada não é fisicamente interrompida.

Com `M0_QT_SHELL_SUPPORT=ON` no mesmo configure, o executável liga esses adapters ao presenter/QML, inclui `m0.qt.audio.local-integration` e o smoke `m0.qt.shell-executable.combined-startup`, e copia `tag.dll` ao lado de `m0_qt_shell.exe`. O smoke combinado carrega QML e o backend FFmpeg, confirma o estado vazio real e sai pelo lifecycle de produção. Os smokes de cenário congelado continuam registrados somente no build sem áudio, onde o presenter usa a fixture. O backend preserva o path nativo com UTF-16 no Windows e UTF-8 no POSIX. O modo de fixture continua sendo compilável sem o suporte de áudio.

No slice MP3, o Green focado `[mp3]` passou **93 asserções em quatro casos**; a suíte do extrator passou **146 asserções em nove casos**, e sua descoberta CTest `^m0\.qt\.metadata\.` passou **9/9**. Esses números permanecem como histórico daquele recorte. O primeiro Green da matriz básica passou **221 asserções em 11 casos**; após a regressão `largesize`, a suíte final passou **226 asserções em 12 casos**. `MR-018` cobre a leitura da matriz básica; a caracterização Opus/ALAC é separada. Esses resultados são de extração/propriedades e não constituem prova audível, rejeição integrada dos candidatos ou publicação de suporte.

No primeiro slice, o teste integrado `m0.qt.audio.local-integration` passou **1/1 em 0,42 s**, com as linhas WAV PCM e MP3 CBR Unicode. O primeiro Red da nova expectativa MP3 observou 2.040 ms fora da tolerância então fixada em ±20 ms; a tolerância integrada foi alinhada a ±50 ms, já usada pelo contrato do extrator/corpus, e o Green comprovou metadata sem erro e o título embutido. O smoke do executável combinado passou **1/1**; naquele fechamento, levou 0,39 s. O core passou **2.148 asserções em 85 casos**, `PlayerPresenterTests` **53/53**, Quick Test **22/22**, o build e `qmllint` ficaram verdes e `tag.dll` estava ao lado do executável.

No fechamento focado anterior da matriz básica, o build relevante ficou verde e CTest passou **14/14 em 6,34 s**: 11 casos de metadata, playback **1/1 em 4,75 s**, startup combinado **1/1 em 0,40 s** e integração **1/1 em 0,82 s** com sete linhas. O corpus passou **14/14**, o runner público aprovou 12 fixtures e os checks documentais daquele recorte ficaram verdes.

Após a revisão, o build relevante voltou a ficar verde e CTest passou **15/15 em 6,49 s**: 12 casos de metadata, playback **1/1 em 4,35 s**, startup combinado **1/1 em 0,43 s** e integração **1/1 em 1,13 s** com nove linhas sobre sete fixtures. O corpus permaneceu **14/14**, e o runner público aprovou 12 fixtures; o manifesto atual tem SHA-256 `de5491e6688b7cdad739c3b0e6beda21d15c73dffa02a8c9f1cfddf92255ba1e`. `scripts/validate-docs.ps1` validou 44 arquivos Markdown, 83 requisitos, 17 checks e 76 etapas; `git diff --check` também ficou verde.

O primeiro Green focado das regressões de erro/recuperação passou **2/2**: `m0.qt.audio.playback-lifecycle` **1/1 em 5,21 s** e `m0.qt.audio.local-integration` **1/1 em 1,42 s**. Após corrigir a corrida entre descoberta de faixas e autoplay, os cinco alvos relevantes compilaram e o CTest focado completo passou **17/17 em 11,87 s**, incluindo 13 testes de metadata, presenter, playback, startup combinado e integração. As execuções diretas registraram **2.150 asserções em 86 casos** no core, **262 asserções em 13 casos** no extrator, **35/35** no playback e **54/54 em 4,08 s** no presenter. A integração inclui as quatro linhas temporárias truncado → válido descritas acima. Nenhum benchmark, A/B ou empacotamento foi executado nesse incremento.

Use MSVC 14.44 e Windows SDK 10.0.26100.0. O SDK Qt base, o overlay Qt Multimedia e o prefixo TagLib ficam separados; o configure não os copia nem altera:

```powershell
$m0VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\BuildTools'
Import-Module (Join-Path $m0VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $m0VsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.26100.0'

$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-audio\audio-dev-01' # use um nome novo por execução
$catch2 = (Resolve-Path (Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3')).Path
$qtBase = (Resolve-Path (Join-Path $repo 'q\f75eb9bea8280844c6d1a2b3f4\sdk')).Path.Replace('\', '/')
$qtHost = (Resolve-Path (Join-Path $repo 'out\m0\toolchains\qt-6.11.2-msvc2022_64')).Path.Replace('\', '/')
$qtMultimedia = (Resolve-Path (Join-Path $repo 'out\m0\qt-multimedia-archive-probe-20260829-01\stage')).Path.Replace('\', '/')
$taglib = (Resolve-Path (Join-Path $repo 'out\m0\taglib-2.3.1-msvc144-probe-20260829-01\install')).Path.Replace('\', '/')
$compiler = 'C:/Program Files/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe'
$linker = 'C:/Program Files/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/link.exe'
$ninja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON `
  -DM0_QT_SHELL_SUPPORT=ON -DM0_QT_AUDIO_SUPPORT=ON `
  "-DM0_CATCH2_SOURCE_DIR=$catch2" "-DCMAKE_PREFIX_PATH=$qtBase;$taglib" `
  "-DQT_HOST_PATH=$qtHost" "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=$qtMultimedia" `
  "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_LINKER=$linker" "-DCMAKE_MAKE_PROGRAM=$ninja"
& $cmake --build $build --config Release --parallel 4
& $ctest --test-dir $build -C Release --output-on-failure
```

O overlay precisa usar `/` ao atravessar o cache CMake; backslashes em `QT_ADDITIONAL_PACKAGES_PREFIX_PATH` podem virar escapes inválidos em um `try_compile`. Os testes deixam a saída mutada ao executar `play()`. Esse recorte não executa benchmark, não automatiza o diálogo visual, não incorpora arquivo pessoal e não publica suporte a nenhum dos formatos exercitados; estados/seek/EOF do engine não substituem prova de áudio audível ou dispositivo físico. Empacotamento, A/B, automação nativa e medições continuam suspensos/pendentes por decisão do mantenedor.

## Pacote local de avaliação

A receita em `package/` prepara somente uma pasta privada para M0.2, não um instalador nem uma release para distribuição. O executável contém os hooks de `BUILD_TESTING=ON`, mas o payload não inclui executáveis de teste ou Qt Test. O runtime C++ do host é um pré-requisito externo; nenhum redistributable é instalado ou copiado. A seção 9 do [registro M0](../../../../docs/m0/README.md) contém a política, proveniência e limitações.

O stage histórico `tray-34edef1-local-02`, derivado do shell de `34edef1`, passou na conferência de 1.378 arquivos/82.388.347 bytes e 69 PEs x64. A cópia para `relocação com espaços 34edef1-01` preservou o mesmo manifesto e payload. Naquela tentativa, o smoke isolado foi bloqueado por `RTSSHooks64.dll` injetado pelo RivaTuner; `error-recovery` do pacote não chegou a executar. Os smokes do build comum estavam verdes no CTest, mas não substituíam essa integração. Nenhuma configuração externa foi alterada e o processo do player foi limpo.

O stage posterior `tray-200b983-clean-01` foi gerado pela receita commitada em `200b983`, com worktree limpo e `recipe_dirty=false`. Seu manifesto é `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99`; o payload confere integralmente com o stage anterior. O build existente não precisou recompilar. Essa regeneração estática não executou o aplicativo.

Após `2cd898a`, zero processos RTSS foram encontrados e o mesmo runner passou nas duas jornadas offscreen do stage `tray-200b983-clean-01` e de sua nova cópia `relocação com espaços 200b983-01`. Saídas normais em código 0, logs vazios, captura exigida antes de 500 ms e manifesto íntegro antes/depois estão registrados na seção 9 do [registro M0](../../../../docs/m0/README.md). Somente módulos do pacote/System32 foram amostrados; a whitelist permaneceu inalterada. Desktop nativo, segurança/toolchain, máquina limpa e M0.2 continuam pendentes.

### Gerar e verificar

Use PowerShell 7.6.5 no ambiente **MSVC 14.44/SDK 10.0.22621.0** preparado acima, na raiz do repositório, após configurar o novo build Qt ON/Release. Fontes do shell, fixture e protocolo devem estar commitados e limpos. A receita identifica separadamente mudanças ainda não commitadas em si mesma ou no contrato de refresh (`recipe_dirty`); antes de medições, todo o worktree deve estar limpo. O nome do destino deve ser novo: não há opção de sobrescrita.

```powershell
$ErrorActionPreference = 'Stop'
$packageRepo = (Resolve-Path .).Path
$packageScripts = Join-Path $packageRepo 'experiments/m0/candidates/qt/package'
$packageBuild = Join-Path $packageRepo 'out/m0/builds/qt-shell/support-msvc-14.44-release'
$packageName = 'local-' + [guid]::NewGuid().ToString('N')
$package = & (Join-Path $packageScripts 'New-QtShellPackage.ps1') -BuildDirectory $packageBuild -Name $packageName
$package

Import-Module (Join-Path $packageScripts 'QtShellPackageManifest.psm1') -Force
Test-QtShellPackageManifest -PackageRoot $package.package_root -ManifestPath $package.manifest_path -ExpectedManifestSha256 $package.manifest_sha256 | Out-Null
```

O retorno contém o hash que identifica o manifesto gerado. Para verificar um pacote guardado ou copiado, use o hash registrado na evidência externa; recalcular um hash a partir de um manifesto desconhecido ou confiar somente no sidecar não autentica sua origem. Os diretórios precisam permanecer quiescentes durante geração, verificação e execução. `evidence/` guarda log de build, receipt MSVC, mapa de cópias e imports PE; `app/` contém o payload inventariado. O manifesto vincula o hash do receipt pós-build e mantém separados os hashes dos contratos histórico e de refresh. O validador de payload não revalida por si só os arquivos externos de `evidence/`.

### Smoke offscreen, sem paths do SDK

Feche o RivaTuner e outros injetores/overlays antes deste check. O runner rejeita módulos externos à pasta do pacote e ao Windows, exige Qt/plugins no payload e não altera aplicações externas ou configurações do SO. Ele cria ambiente filho mínimo e diretórios neutros de trabalho/perfil, usa `offscreen`, `QT_QUICK_BACKEND=software` e estilo Basic, sem herdar os paths Qt/QML do shell de desenvolvimento.

```powershell
$packageSmoke = & (Join-Path $packageScripts 'Test-QtShellPackageSmoke.ps1') -PackageRoot $package.package_root -ManifestPath $package.manifest_path -ExpectedManifestSha256 $package.manifest_sha256
$packageSmoke | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path (Split-Path $package.manifest_path) 'evidence/smoke-offscreen.v1.json') -Encoding utf8
```

O runner executa somente `tray-lifecycle` e `error-recovery`, em ordem, e só retorna aprovação se ambas terminarem com código 0, output vazio e captura dos módulos requeridos antes de 500 ms. O manifesto é validado antes de iniciar e novamente depois de ambas as jornadas. O timeout de cada jornada é 5 s; kill serve somente ao cleanup de falha. Snapshots são amostras, não um trace completo do loader. Guard local de erro impede diálogos de loader/crash na criação do processo e restaura as flags do caller. Isso não substitui bandeja/Explorer nativos, NVDA, HC/DPI reais, Linux, máquina limpa ou performance.

### Refresh MSVC na receita

Após a [primeira tentativa cancelada](../../../../docs/m0/evidence/m0-2-msvc-refresh.md), a [nova instalação autorizada](../../../../docs/m0/evidence/m0-2-msvc-installation.md) terminou com sucesso. A receita atual autentica o contrato fixado e os arquivos CMake/ferramentas antes e depois do build; falha em qualquer fase impede criar o stage. O manifesto usa a identidade pós-build, incluindo linker, ambiente e hashes separados do refresh. O pacote histórico não é substituído ou reclassificado. Testes sintéticos da orquestração não substituem compilação/linkagem, pacote real ou integração nativa.

A [evidência de integração](../../../../docs/m0/evidence/m0-2-msvc-package.md) registra 206/206 testes do empacotador, Release Qt 65/65, core Qt OFF 55/55 e o pacote diagnóstico com smoke 2/2. Depois do commit `63717e5`, o pacote `msvc-63717e5-clean-01` foi gerado com `recipe_dirty=false` e passou nas duas jornadas originais e nas duas relocadas. A tentativa nativa mostrou abertura/pausa/fila/Preferências/tema, mas as entradas foram pausadas ao detectar interação manual; foco acessível, saída, NVDA e HC/DPI não foram aprovados. A [regressão FreeType isolada](../../../../docs/m0/evidence/m0-2-freetype-backport.md) teve três falhas em nove casos no vendor original e Green 9/9 com o patch oficial, mas ainda não corrige QtGui/pacote. Segurança continua pendente; a reconstrução Qt exige nova identidade/configuração sem modificar o SDK histórico.

### Rebuild Qtbase autenticado

A [execução isolada de 2026-08-28](../../../../docs/m0/evidence/m0-2-qtbase-refresh.md) concluiu configure/build/install do Qtbase 6.11.2 com `5a280ec` em um `RunRoot` novo. O manifesto v2 `dc26c096…afbf6b` e o receipt `8a4e3c13…28619`, ancorado externamente, foram revalidados em outro processo junto com as 4.289 folhas físicas, os 15 PEs contratuais e `qminimal`. O SDK Qt oficial e `msvc-63717e5-clean-01` preservam seus bytes anteriores.

O passo posterior [compôs e autenticou o SDK Qt](../../../../docs/m0/evidence/m0-2-qt-composite-sdk.md) no run `f75eb9bea8280844c6d1a2b3f4`: contrato `f75eb9b…`, manifesto `221afa…`, receipt `30399b…` e 9.880 arquivos/2.400.301.667 bytes; o processo independente retornou `verified=True`. O SDK oficial, o contrato v1 e os builds/pacotes históricos permanecem preservados. A receita e o contrato `package-v2` estão implementados e congelados, mas ainda não foram usados para configurar, compilar, empacotar ou executar este candidato. O próximo incremento é executar essa receita contra a identidade composta com Windows SDK `10.0.26100.0`, seguido dos checks Release e smokes original/relocado. Até essa execução, não há prova nova de carregabilidade, ABI, pacote, máquina limpa, CRT, licenças/notices/SBOM ou redistribuição. `CHK-SEC`, M0.2 e os gates nativos continuam abertos; a interação desktop permanece pausada.

### Receita autenticada `package-v2` — execução real pendente

A receita congelada fica em `package-v2/` e exige Windows x64 e PowerShell 7.2 ou superior. Os exemplos abaixo são os comandos operacionais para a execução real futura; nenhum build, pacote ou smoke novo é alegado por esta documentação. Execute-os somente a partir do commit da receita, em um worktree limpo e numa nova sessão PowerShell. O hash do contrato é uma âncora externa literal obrigatória: não o recalcule a partir do contrato recebido e não o derive do sidecar.

Prepare as âncoras e somente os diretórios-pai. `BuildRoot`, `OutputRoot`, `ScratchRoot` e `SmokeWorkspaceRoot` devem ser filhos novos, ausentes, distintos e não sobrepostos sob `out/m0`:

```powershell
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess -or
    $PSVersionTable.PSVersion -lt [version]'7.2') {
    throw 'A receita package-v2 exige Windows x64 e PowerShell 7.2 ou superior.'
}

$packageV2Repo = (Resolve-Path .).Path
$packageV2Dir = Join-Path $packageV2Repo 'experiments\m0\candidates\qt\package-v2'
$packageV2Launcher = Join-Path $packageV2Dir 'Invoke-M0QtShellCompositePackage.ps1'
$packageV2Contract = Join-Path $packageV2Dir 'qt-shell-composite-package-contract.v2.json'
$compositeReceipt = Join-Path $packageV2Repo 'q\f75eb9bea8280844c6d1a2b3f4\evidence\qt-shell-composite-receipt.v2.json'

$packageV2ContractSha256 = '364c31c0449347ac631435ea22d872f60624449a064fbf1b6829947cac76477f'
$compositeReceiptSha256 = '30399b3099cf04bdd50d6f083d077e82a590afefeec0617fda89ef5266a1d63e'
$qtbaseReceiptSha256 = '8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619'

$packageV2Common = @{
    ContractPath = $packageV2Contract
    ExpectedContractSha256 = $packageV2ContractSha256
    CompositeReceiptPath = $compositeReceipt
    ExpectedCompositeReceiptSha256 = $compositeReceiptSha256
    ExpectedQtBaseReceiptSha256 = $qtbaseReceiptSha256
    RepositoryRoot = $packageV2Repo
}

$buildParent = Join-Path $packageV2Repo 'out\m0\builds\qt-shell\package-v2'
$packageParent = Join-Path $packageV2Repo 'out\m0\packages\qt-shell\package-v2'
$scratchParent = Join-Path $packageV2Repo 'out\m0\scratch\qt-shell\package-v2'
$smokeParent = Join-Path $packageV2Repo 'out\m0\smoke\qt-shell\package-v2'
$reportParent = Join-Path $packageV2Repo 'out\m0\reports\qt-shell\package-v2'
foreach ($parent in @($buildParent, $packageParent, $scratchParent, $smokeParent, $reportParent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
```

O modo **Build** autentica receita, receipts, fontes e ferramentas antes/depois, produz os builds Qt ON e core OFF Release, executa seus checks e publica um pacote novo:

```powershell
$buildId = [guid]::NewGuid().ToString('N')
$buildRoot = Join-Path $buildParent $buildId
$packageRoot = Join-Path $packageParent $buildId
$buildScratchRoot = Join-Path $scratchParent ('build-' + $buildId)

$packageV2Build = & $packageV2Launcher @packageV2Common `
    -BuildRoot $buildRoot `
    -OutputRoot $packageRoot `
    -ScratchRoot $buildScratchRoot
$packageV2Build
```

O modo **Relocate** primeiro autentica o receipt canônico do pacote de origem, copia seu inventário fechado para um root novo com espaços e acentos e autentica novamente o destino:

```powershell
$relocationId = [guid]::NewGuid().ToString('N')
$relocatedPackageRoot = Join-Path $packageParent ('relocação com espaços-' + $relocationId)
$relocationScratchRoot = Join-Path $scratchParent ('relocate-' + $relocationId)

$packageV2Relocated = & $packageV2Launcher @packageV2Common `
    -SourcePackageRoot $packageV2Build.package_root `
    -ReceiptPath $packageV2Build.receipt_path `
    -ExpectedReceiptSha256 $packageV2Build.receipt_sha256 `
    -OutputRoot $relocatedPackageRoot `
    -ScratchRoot $relocationScratchRoot
$packageV2Relocated
```

O modo **Smoke** exige o receipt canônico e seu hash externo capturado do Build. `ReportPath` deve estar dentro do repositório, fora do pacote, com diretório-pai já existente e arquivo-alvo ausente. Use roots e relatórios diferentes para original e relocado:

```powershell
$originalSmokeId = [guid]::NewGuid().ToString('N')
$originalSmokeWorkspace = Join-Path $smokeParent ('original-' + $originalSmokeId)
$originalReportPath = Join-Path $reportParent ('original-' + $originalSmokeId + '.json')
$packageV2OriginalSmoke = & $packageV2Launcher @packageV2Common `
    -PackageRoot $packageV2Build.package_root `
    -ReceiptPath $packageV2Build.receipt_path `
    -ExpectedReceiptSha256 $packageV2Build.receipt_sha256 `
    -SmokeWorkspaceRoot $originalSmokeWorkspace `
    -ReportPath $originalReportPath
$packageV2OriginalSmoke

$relocatedSmokeId = [guid]::NewGuid().ToString('N')
$relocatedSmokeWorkspace = Join-Path $smokeParent ('relocated-' + $relocatedSmokeId)
$relocatedReportPath = Join-Path $reportParent ('relocated-' + $relocatedSmokeId + '.json')
$packageV2RelocatedSmoke = & $packageV2Launcher @packageV2Common `
    -PackageRoot $packageV2Relocated.package_root `
    -ReceiptPath $packageV2Relocated.receipt_path `
    -ExpectedReceiptSha256 $packageV2Relocated.receipt_sha256 `
    -SmokeWorkspaceRoot $relocatedSmokeWorkspace `
    -ReportPath $relocatedReportPath
$packageV2RelocatedSmoke
```

Os testes sintéticos da receita não executam o build pesado nem substituem os checks reais. Rode cada arquivo em um processo limpo para evitar estado compartilhado entre módulos:

```powershell
$packageV2Tests = @(
    'CompositeBuild.Tests.ps1'
    'Contract.Tests.ps1'
    'PackageV2Production.Tests.ps1'
    'PackageV2ProductionExecution.Tests.ps1'
    'PackageV2Relocation.Tests.ps1'
    'PackageV2Smoke.Tests.ps1'
    'PackageV2Staging.Tests.ps1'
    'PackageV2Verifier.Tests.ps1'
)
foreach ($testFile in $packageV2Tests) {
    & pwsh -NoLogo -NoProfile -NonInteractive -File `
        (Join-Path $packageV2Dir "tests\$testFile")
    if ($LASTEXITCODE -ne 0) { throw "Falha nos testes package-v2: $testFile" }
}
```

Não há dependência deliberada em Pester ou PSScriptAnalyzer. O parser AST abaixo é somente um check de sintaxe PowerShell, não lint, e a ausência de PSScriptAnalyzer não deve ser apresentada como lint aprovado:

```powershell
$packageV2Scripts = @(Get-ChildItem -LiteralPath $packageV2Dir -Recurse -File |
    Where-Object { $_.Extension -in @('.ps1', '.psm1') })
foreach ($scriptFile in $packageV2Scripts) {
    $tokens = $null
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $scriptFile.FullName, [ref]$tokens, [ref]$parseErrors)
    if (@($parseErrors).Count -ne 0) {
        throw "Parser AST falhou: $($scriptFile.FullName): $($parseErrors -join '; ')"
    }
}
```

### Testes do empacotador

Os testes usam fixtures sintéticas e contratos locais autenticados, sem áudio, rede ou execução do player:

```powershell
foreach ($testFile in @('Builder.Tests.ps1', 'ToolchainRefresh.Tests.ps1', 'RefreshedBuild.Tests.ps1', 'Prerequisites.Tests.ps1', 'Manifest.Tests.ps1', 'Smoke.Tests.ps1')) {
    & pwsh -NoLogo -NoProfile -File (Join-Path $packageScripts "tests/$testFile")
    if ($LASTEXITCODE -ne 0) { throw "Falha nos testes: $testFile" }
}
```

Não há dependência em Pester ou PSScriptAnalyzer. Parser PowerShell, testes, revisão do diff e os checks nativos do shell fazem parte do fechamento; ausência de PSScriptAnalyzer não é reportada como lint aprovado. Arquivos de evidência e fixtures temporárias ficam sob `out/m0` e não são incluídos no Git.

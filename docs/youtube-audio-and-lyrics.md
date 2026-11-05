# YouTube em MP3 e letra com fallback

**Estado:** ingresso separado, resolução/processo/worker e composição de admissão durável/fila compartilhada implementados em código; os checks do incremento atual de admissão estão em fechamento. Aquisição YouTube de produção, runtime autenticado configurado, integração Letras/fallback e instalação continuam pendentes.

O mantenedor definiu áudio do YouTube em MP3 e Letras.mus.br como fonte preferencial de letra, com outro provedor como fallback. O [ADR-0017](architecture/decisions/0017-youtube-audio-and-lyrics-acquisition.md) registra o desenho, a pesquisa e os gates. O fallback proposto é o LRCLIB já usado pelo RC Music.

## Resultado esperado

Na extensão YouTube separada, selecionar o vídeo, conferir título/artista e escolher **Baixar MP3 + letra**. O áudio deve ser adquirido sem a imagem do vídeo quando houver faixa separada, convertido quando necessário e validado como MP3. A letra deve ser associada ao arquivo final, com origem explícita e preservação das formas disponíveis. Falha na letra conserva o áudio e permite tentar somente a letra depois. Nada começa apenas por abrir o popup; importação não inicia reprodução.

## Limites atuais

O primeiro cliente trata o vídeo da aba ativa, não toda uma playlist do YouTube. O contrato admite lote limitado para a evolução posterior. A principal permanece sem YouTube. O host lógico YouTube é independente, ainda sem executável/registro/configuração instalada; ausência da ponte não é download concluído nem ACK fabricado.

O código agora possui resolver, adapter de processo Windows e composição de execução em worker, com rota tipada até transferência/conversão/publicação. O pipeline normal do app ainda não configura ou habilita essa aquisição. `yt-dlp` e `ffmpeg` encontrados no ambiente não são uma configuração autenticada do produto nem aprovação para empacotamento. O conjunto EJS local completo está ausente; `NoChild` continua impedindo qualificar a execução que precise de Node descendente. Não foi baixado áudio público. Os gates anteriores de produção remota permanecem abertos.

A admissão YouTube agora tem ponte assíncrona e sessão de protocolo próprias, compartilhando controlador, fila e executor com mídia direta. O contexto completo identifica o pedido e é validado; não autentica a origem por si. Ainda falta o host IPC/configuração instalada que obtenha essa origem de forma confiável, além do runtime real. Os metadados completos não são persistidos como payload: ficam hash/contagem e IDs; replay explícito após reiniciar não retoma o áudio automaticamente.

A consulta do exemplo do Letras foi bloqueada por HTTP 403, e não foi confirmado contrato de API/extração compatível. Não há parser Letras de produção nem comprovação das variantes dessa página. O app atual continua usando LRCLIB, com busca explícita; a cadeia Letras → LRCLIB e sua associação ao download ainda precisam ser compostas. Não tratar retorno LRCLIB como Letras. LLM, tradução e sincronização inventada continuam fora do fluxo.

## Evidência do ingresso separado

Recibos locais em `out/product/test-runs/youtube-audio-20261104/`. O Red Node passou **5 casos e falhou 18**, antes do comportamento de policy/ponte/controller. O Red Qt passou **38 casos e falhou 26**, com stubs vazios. Depois, o novo teste de montagem DOM real com browser falso teve Red **35/1**; o clique e a renderização foram implementados sem remover suas assertions.

Green final: **203/203 testes Node** de ambas as extensões, incluindo **36** do pacote YouTube, e sintaxe dos **10 módulos JavaScript**. O CTest passou **2/2**, com **66 casos Qt** do novo codec e **188** da regressão do codec principal, sem falhas/skips. O corpus compartilhado tem **8 pedidos e 4 respostas**, consumidos em Node e Qt; cobre Unicode, protocolo separado, host Letras, segmentos ambíguos, tipos e ACK parcial. Não há dependências npm novas.

O limite foi alinhado a **20 itens**, conforme a capacidade existente do serviço comum; não se admite um lote maior para produzir ACK parcial. O teste da principal continua recusando YouTube, `youtu.be`, `youtube-nocookie` e `googlevideo`. Os arquivos do pacote principal foram preservados por hash; nenhum bloqueio dele foi retirado.

Naquele recorte, o build Release compilou o alvo independente de codec/teste, sem ligá-lo ao executável do player, fornecer um host nativo ou carregar yt-dlp/FFmpeg. Os testes usaram Chrome/Native Messaging falsos, sem consultar YouTube ou Letras. Não foram executados setup, navegador, Registro, perfis AppContainer, UAC, download de mídia pública ou benchmarks. Os seis arquivos AppContainer em andamento e os cinco artefatos congelados anteriores foram preservados.

Esse recorte não é aquisição YouTube funcional, fallback Letras implementado, instalador atualizado ou aceitação Brave nativa. Continuar por resolver/runtime e composição protegida com aquisição/conversão/importação, depois cadeia de letras/associação e integração instalada; acesso Letras e demais gates de FR-050/FR-052 continuam abertos. O objetivo completo não está concluído.

## Evidência do resolver e da rota de execução

Recibos locais em `out/product/test-runs/youtube-resolver-20261105/`. O fechamento do codec/sessão passou **2/2 CTest**, com **93 casos Qt** do codec e **12** da sessão, sem promover o resultado a prova de YouTube real. O primeiro valida dados/argv limitados; a segunda só reconhece um capability depois de resultado correspondente, saída bem-sucedida e parada/EOF do processo e pipes. O helper dos testes obtém esse capability pela sessão com processo falso, sem construtor de teste que transforme dados JSON em autorização.

| Componente do incremento | Estado e limite da evidência |
|---|---|
| Codec de resolução e argv | Implementado; 93 casos Qt Green; vídeo único, áudio AAC/M4A, limites/UTF-8/expiração/restrições/headers |
| `YoutubeResolverSession` | Implementada; 14 casos Qt Green no fechamento, incluindo regressões de estado desconhecido e cancelamento que lança exceção |
| Processo Windows do resolver | Adapter implementado, com preparação confiável e lifetime do runtime; configuração/execução de yt-dlp autêntico ainda não qualificadas |
| Execução e factory no worker | 11 casos Qt da composição, com três cenários de DTO inválido em filhos QtTest próprios; 32 do executor legado e 22 da tarefa preparada |
| Rede e job por capability | 170 casos Qt de política, 64 de transferência e 73 da sessão de job; URL assinado, redirects, expiração, DNS/pin/quotas/stop ACK e métodos legados |
| Host/instalação/runtime do produto | Ainda sem configuração instalada/autenticada e sem aquisição habilitada no app normal |
| EJS e Node descendente | Conjunto local completo ausente; `NoChild` preservado, sem qualificação desse runtime |
| Letras → LRCLIB e associação final | Não compostos; Letras respondeu 403 e não possui parser de produção neste código |

As novas entradas não retiram bloqueios da principal. A sessão de transferência usa somente o stream reconhecido pelo capability; não recebe URL Googlevideo arbitrário do pedido. Relógios são fornecidos explicitamente pela composição: o prazo de cada hop é limitado pelo capability e pela expiração do alvo, observados com tempo monotônico. Erro/cancelamento continua exigindo reconhecimento de parada antes de converter, publicar, limpar ou encerrar o job.

TDD: codec/sessão tiveram Red **48/45** e **3/9** (passagens/falhas). A rota teve Reds próprios: política **156/14**, transferência **58/6**, processo **6/15**, job **70/3**, execução **2/5**. A revisão acrescentou Reds para cancelamento durante preparo (**7/1**), sessão/enum/retry (**12/2**) e metadados/DTO/cancelamento da composição (**8/3**, depois **9/2** com três variantes de DTO inválido). Erros de include do harness/build foram corrigidos separadamente e não contam como Red comportamental. Não se retiraram assertions para chegar ao Green.

Fechamento proporcional: **9/9 CTest, 501 casos Qt**, sem falhas/skips, incluindo **22** do adapter Windows com helper local. O preparo não inicia transferência antes de devolver o controle para observar cancelamento; DTO inválido não reconhece parada, e cancelamento com exceção é tentado novamente. Publicação já confirmada é retida como evidência mesmo quando o job falha. Revisão independente não encontrou bloqueadores adicionais. Formato dos **26 C++** passou. Builds Release Lite, Completa e módulo de rede sem desktop passaram, todos com `BUILD_TESTING=OFF` e inventário de zero testes; o módulo YouTube não depende da UI. Nenhum instalador foi atualizado.

O helper nativo usa arquivos sintéticos próprios; a regressão preexistente da sessão de job também executa conversão/validação FFmpeg com fixtures locais licenciadas, por IO falso, sem internet. Não foram executados yt-dlp ou Node como runtime de mídia, navegador/GUI, novos perfis AppContainer, WFP/UAC, setup/Registro ou benchmarks. Os arquivos AppContainer em andamento, os artefatos congelados e o pacote principal da extensão foram preservados por hash.

Os testes da rota usam IO falso e arquivos próprios. Isso não comprova um vídeo público, o conjunto real yt-dlp/Node/EJS, uma instalação Brave, o runtime autenticado do produto ou o ciclo completo MP3 + letra. O acesso/contrato do Letras, o fallback composto, a importação final no fluxo instalado, a integração opcional dos instaladores e os gates nativos/de distribuição permanecem pendentes. O objetivo completo não está concluído.

## Evidência da admissão durável e fila compartilhada

Recibos em `out/product/test-runs/youtube-admission-20261105/`. Fechamento proporcional: **10/10 CTest, 549 casos Qt**, mais **167 casos puros/4.313 assertions** do serviço/controller/checkpoint, sem falhas/skips. As contagens das seções anteriores pertencem aos seus respectivos commits e recortes.

| Componente acrescentado | Contrato implementado e validado no recorte automatizado |
|---|---|
| Contexto opcional do `DownloadItem` | `YoutubeAudioItem` completo participa de identidade, igualdade e quotas; não concede autorização e preserva o algoritmo v1 para pedidos legados |
| `YoutubeAudioAdmission` | Ponte `AsyncYoutubeAudioSubmission` → controlador durável comum; admissão depende do Saved correspondente |
| `NativeYoutubeAudioSession` | Protocolo/observador separado, origem exata da configuração/transporte e limite de 64 KiB antes do corpo; Pending sem ACK; EOF/reentrada/exceções não cancelam nem repetem o trabalho admitido |
| `makeWindowsMediaAndYoutubeExecution` | Uma fila e um executor para mídia direta e YouTube, com contexto validado e sem retirada de bloqueios do ingresso legado |
| `WindowsPlayerDownloads` | Dois ports sobre o mesmo controlador/owner, sem registrar host ou configurar runtime automaticamente |

A jornada instrumentada passou nos **7 casos Qt** do owner: entrega um frame YouTube, desconecta/reconecta, aguarda checkpoint Saved, percorre o worker com resolver falso e destino Published falso, e aguarda Session Saved da importação sem seleção/autoplay. A sessão YouTube passou **45 casos**, a ponte **7**, o executor **14**, e identity/checkpoint **107**; os restantes são regressões da principal e da execução/sessão existente. Isso verifica composição e reconhecimentos entre donos; não executa aquisição YouTube real nem conversão pelo destino falso.

Red confirmado: núcleo **2 casos passaram/2 falharam**, com **45 assertions passadas/18 falhas**; identity/checkpoint **83/24**, ponte **3/4**, roteamento **11/3**, sessão YouTube **18/27**. A principal passou **190/190** já no Red porque seu guard de roundtrip existente recusou mecanicamente o novo contexto; não se inventou falha para ele. Avisos de `nodiscard` e macro Qt obsoleta do harness foram corrigidos antes do Red comportamental. Os vetores v1 antigos continuam verdes; o novo domínio recebeu também vetor SHA-256 independente e fixo para preservar compatibilidade dos recibos.

Formato dos **22 C++**, revisão e builds Release Lite/Completa, rede standalone e serviço puro sem Qt passaram. Os quatro builds `BUILD_TESTING=OFF` listaram zero testes. Documentação passou com **95 Markdown/86 requisitos/17 checks/82 etapas**; rastreabilidade conserva estados **14 planejados/70 em andamento/2 comprovados**. Não houve alteração QML ou novo setup. A regressão preexistente do job usa FFmpeg com fixtures locais; o novo fluxo usa fakes, sem baixar mídia pública, executar yt-dlp/Node como runtime, criar perfil AppContainer, consultar WFP/UAC, instalar/registrar componentes, interagir com navegador/janela nativa ou retomar benchmarks. Os seis WIP e os cinco artefatos congelados foram preservados.

O recibo persistido representa hash/contagem e IDs do pedido, sem o payload completo de artista/página/opção de letra. Replay explícito depois de reiniciar verifica essa identidade sem iniciar ou retomar o áudio. Assim, o contexto em memória não é tratado como uma fila persistente de downloads retomáveis. Não se deve anunciar recuperação automática de resolução, progresso ou consulta de letras a partir desse recibo.

Continuam pendentes host IPC e instalação, origem real autenticada no browser, runtime autenticado/configurado, EJS e execução de Node compatível com `NoChild`, aquisição pública e cadeia Letras → LRCLIB com associação da letra final. Os novos componentes não instalam ferramentas, registram integrações nem habilitam downloads de produção no app normal. O objetivo completo não está concluído.

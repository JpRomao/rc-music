# ADR-0017: aquisição YouTube em MP3 e letras com fallback

**Estado:** Proposto

**Data:** 2026-11-04

**Escopo aceito pelo mantenedor:** áudio do YouTube entregue como MP3, letra preferencialmente do Letras.mus.br e outro provedor como fallback. A separação opcional da extensão YouTube permanece conforme [ADR-0006](0006-browser-editions-and-extensions.md). Ferramentas externas, execução, acesso ao Letras e distribuição ainda não estão aprovados por este registro.

## Contexto e pesquisa

O pedido atual torna explícito o fluxo completo; guardar versões locais da letra, já adotado no [ADR-0016](0016-lyrics-variants.md), não o substitui. O player normal ainda não compõe o downloader YouTube de produção. O cliente da extensão YouTube e seu codec de ingresso separado já foram implementados; a principal continua recusando YouTube também no codec/policy C++. O incremento posterior acrescenta componentes de resolução e execução, sem configurar um host/runtime autêntico ou habilitar aquisição no app normal.

A opção MP3 de um downloader não significa que o YouTube entregue um arquivo MP3 nativo. A [documentação do yt-dlp](https://github.com/yt-dlp/yt-dlp#post-processing-options) descreve extração e conversão pelo FFmpeg. O fluxo deve preferir uma faixa somente de áudio e convertê-la quando necessário, sem baixar a imagem do vídeo e sem renomear AAC/Opus para `.mp3`. A presença de yt-dlp/FFmpeg no computador não os aprova como componentes do instalador.

A página de exemplo do Letras respondeu 403 na consulta HTTP. Os [termos publicados pelo Ouvir Música](https://www.ouvirmusica.com.br/aviso-legal/) declaram abranger Letras.mus.br e restringem métodos de extração de dados. Não foi localizado contrato primário de API pública para um player externo. Portanto, não inventar um parser “compatível” com HTML sintético, usar endpoints internos como contrato ou alegar acesso confirmado. A preferência do mantenedor por Letras permanece registrada, com integração pendente de acesso/contrato compatível. O [LRCLIB](https://lrclib.net/docs.txt) tem API pública já usada pelo app e será o fallback proposto; não promete original e romanização separadas.

As [políticas da API YouTube](https://developers.google.com/youtube/terms/developer-policies-guide) restringem download/separação de áudio por clientes dessa API. Este desenho não usa a Data API como fonte de MP3 nem autoriza usos proibidos pelo serviço. Termos, ferramenta/versão, licenças, procedência e distribuição continuam nos gates de FR-050/FR-052.

## Desenho proposto

1. Criar pacote `extensions/chromium/youtube` e host lógico `com.rcmusic.youtube`, distintos da principal/`com.rcmusic.media`. Permissões mínimas de aba ativa e Native Messaging. Abrir o popup apenas apresenta o vídeo da aba; somente seleção e clique explícitos enviam um pedido. Não ler cookies, histórico, microfone ou URLs de streams do player.
2. Usar contrato próprio `youtube.audio.request` v1: UUID v4, lista de IDs de vídeo e título/artista editáveis, opção de letra e página Letras opcional. O app constrói a URL canônica a partir de um ID de 11 caracteres; a mensagem não transporta executável, argumentos, destino ou URL de mídia arbitrária. Origem instalada/host confiável autoriza a capacidade; um campo da página não a concede.
3. Validar integralmente o pedido: até 20 IDs únicos, 64 KiB de JSON estrito, 512 bytes UTF-8 por título/artista e 2048 por página Letras; título obrigatório, artista/página opcionais. A página aceita somente HTTPS no host Letras exato, sem credenciais/query/fragmento ou segmentos ambíguos. Isso valida um identificador, não consulta ou aprova o provedor. Respostas correlacionam ID e quantidade; ACK é admissão, não arquivo baixado.
4. Resolver a faixa de áudio em adapter separado e worker supervisionado, com runtime explicitamente configurado/autenticado e argumentos fixos, sem shell/config/plugins/cookies herdados ou auto-update. Há codec/sessão, adapter de processo Windows e composição em worker implementados para esse caminho; yt-dlp/runtime real ainda exigem preparação e qualificação próprias. Priorizar M4A/AAC quando disponível para aproveitar a matriz já testada; Opus/WebM depende de qualificação do backend real escolhido.
5. Compor resolução → transferência limitada → conversão/validação MP3 → publicação sem overwrite → importação durável existente, sem seleção/autoplay. Progresso, cancelamento, reconhecimento de parada, ownership e Saved continuam obrigatórios. Um resultado JSON de resolução não autoriza sozinho URL, arquivo ou importação.
6. Associar letras à gravação usando título/artista corrigíveis e, quando disponível, duração/álbum; canal de vídeo ou `/naruto/` da página não são automaticamente artista da gravação. A busca explícita segue Letras → LRCLIB somente com adapters disponíveis e origem informada. Não buscar metadados/letras automaticamente ao abrir o player ou a extensão.
7. MP3 e letras possuem resultados independentes: falha/ausência de letra conserva o áudio concluído; retry de letra não baixa áudio novamente. Salvar várias formas deve usar uma alteração de catálogo reconhecida, preservando as existentes e a identidade do arquivo final. Não inventar romanização ou timestamps; LLM/tradução continuam posteriores.
8. Opcionais de instalação, host/origens instaladas, ferramentas e runtime/artefatos têm seus gates próprios. Não registrar componentes, executar setup ou ativar produção por um teste de codec/cliente. Lite continua sem engine interna; Completa e Lite compartilham a futura aquisição protegida.

## Alternativas e consequências

- Remover o bloqueio da principal violaria a separação aceita e permitiria pedidos indiretos sem capacidade confiável.
- Baixar vídeo completo para descartar a imagem acrescenta tráfego e disco sem necessidade quando há áudio separado.
- Usar qualquer CLI achada em PATH como componente do produto perderia procedência e controle de versão/runtime.
- Letras sem acesso verificado não pode ser anunciado como provedor funcional; LRCLIB existente não deve ser rotulado como Letras.
- Falha de letra não deve invalidar ou repetir uma aquisição de áudio concluída. Origem e cobertura ficam visíveis.

## Validação e critérios de revisão

### Incremento de resolução e rota por capability — 2026-11-05

O codec de resolução passou **93 casos Qt**, e a sessão de resolução passou inicialmente **12**, em **2/2 CTest**; o fechamento com regressões da revisão tem **14** da sessão e **501 Qt/9 CTest** no total proporcional. O codec aceita a projeção limitada de um vídeo com áudio M4A/AAC acessível sem login, recusa DRM/live/playlist e valida tamanho, duração, headers e URL Googlevideo com expiração explícita. Uma transmissão já arquivada e finita pode ser aceita. A sessão exige resultado correlacionado, saída bem-sucedida e reconhecimento de parada/EOF antes de criar `ResolvedYoutubeAudioCapability`; JSON válido isoladamente não produz essa autoridade.

Os componentes seguintes estão implementados, com fechamento proporcional registrado pelo responsável pelos checks no [guia](../../youtube-audio-and-lyrics.md#evidência-do-resolver-e-da-rota-de-execução): `WindowsYoutubeResolverProcess`, factory de execução e coordenação no worker existente; parser/redirects de rede por capability; e entradas tipadas em `DirectDownloadSession`/`WindowsDownloadJobSession`. O job usa identidade do capability e conserva a fonte canônica; a transferência recebe o URL assinado sem reescrever path/query. DNS de todos os endereços, IP pinning, quotas, HTTP 200, conversão/publicação e reconhecimento nativo de parada mantêm suas fronteiras. Os métodos legados continuam com a política que recusa YouTube/Googlevideo.

Expiração usa epoch fornecido pela composição confiável e tempo monotônico observado, sem relógio de parede oculto na sessão. Cada redirect revalida o escopo HTTPS Googlevideo `/videoplayback` e o prazo residual; um URL expirado não pode iniciar a resolução do próximo host. Cancelar ou observar erro não dispensa a drenagem dos processos e pipes antes de reconhecer terminal ou usar/remover o workspace.

Essa implementação não qualifica um runtime real. A preparação autenticada ainda não tem configuração de distribuição, o conjunto local completo de EJS não está disponível e a restrição `NoChild` permanece: ela não qualifica um yt-dlp que precise lançar Node descendente. Não houve composição instalada do host, aquisição pública ou aprovação de ferramentas/sandbox por esses testes. Acesso Letras continua bloqueado por 403, sem parser de produção; a cadeia Letras → LRCLIB e a associação às letras salvas também não foram compostas. O ADR permanece **Proposto**.

### Incremento de admissão durável e fila compartilhada — 2026-11-05

O código agora acrescenta contexto opcional `YoutubeAudioItem` ao `DownloadItem`, com video ID, título, artista, página Letras e opção de letra. O contexto completo participa de validação, igualdade, quotas e identidade do pedido; não é uma permissão de origem. Pedidos legados sem contexto conservam seu algoritmo v1 de fingerprint. O checkpoint não passa a armazenar o payload completo desses metadados: conserva hash/contagem e IDs; um replay explícito após reiniciar não inicia nem retoma áudio automaticamente.

A ponte `YoutubeAudioAdmission` implementa `AsyncYoutubeAudioSubmission` sobre o controlador durável comum. `NativeYoutubeAudioSession` mantém protocolo/observador separados da principal, com origem exata vinda da configuração/transporte, teto de 64 KiB conferido antes do corpo e nenhuma resposta de admissão durante Pending. EOF, reentrada e exceções não viram cancelamento, retry ou ACK fabricado do trabalho que pertence ao controlador. Isso implementa a fronteira em código; não comprova origem autenticada de uma instalação Brave ou um host IPC operacional.

`makeWindowsMediaAndYoutubeExecution` usa a mesma fila/executor para mídia direta e contexto YouTube validado, e `WindowsPlayerDownloads` expõe os dois ports de admissão sobre o mesmo owner. A rota específica ainda depende da composição C++ confiável e do capability de resolução; a principal não pode obtê-la direta ou indiretamente acrescentando uma flag ao JSON. A jornada instrumentada adicionada liga frame YouTube → checkpoint Saved → worker com resolver falso → destino Published falso → importação com Session Saved, sem seleção/autoplay.

**Evidência deste incremento:** **10/10 CTest, 549 casos Qt**, mais **167 casos puros/4.313 assertions**, sem falhas/skips, e formato de 22 C++. Builds Lite/Completa, rede standalone e serviço sem Qt passaram sem testes. Reds, vetor de compatibilidade e recibos estão no [guia](../../youtube-audio-and-lyrics.md#evidência-da-admissão-durável-e-fila-compartilhada) e em `out/product/test-runs/youtube-admission-20261105/`; os Greens anteriores não são transferidos para essas mudanças. Continuam pendentes configuração/autenticação de runtime, host IPC/instalação e origem real do browser, EJS/Node compatíveis com NoChild, aquisição pública, Letras → LRCLIB e associação da letra final. O ADR permanece **Proposto**.

### Incremento de letras após publicação e fallback explícito — 2026-11-05

O [fluxo opcional de letras do áudio adquirido](../../download-lyrics.md) está implementado sobre a publicação reconhecida do MP3 e o contexto original vivo. `WindowsPlayerDownloads` oferece somente jobs com `fetch_lyrics=true`, uma vez por job durante o lifetime do owner e um alvo de revisão por vez. Histórico recuperado não inicia consulta, e o checkpoint permanece sem payload completo de pedido ou stream assinado. Falha de letra não repete nem invalida áudio/importação.

Letras continua indisponível: a consulta anterior recebeu 403, não há parser nem contrato de acesso confirmado. A composição informa isso e usa o LRCLIB existente como fallback explícito. `DownloadLyricsLookup` possui serviço exclusivo, conserva cooldown/cancelamento e configura metadados sem ler áudio ou `.lrc`. Título/artista ausentes exigem correção; idioma, escrita, romanização e tempos não são inferidos. Todos os conteúdos sincronizados/plain distintos retornados ficam disponíveis para revisão, sem promessa de formas que o provedor não forneça.

`DownloadLyricsBinding` exige revisão, nome e escolha explícita Original/Romanizada antes de guardar uma forma. Identidade do backend/alvo e `requestToken` revogam ações antigas; editar título/artista exige nova busca antes de guardar aquele resultado. O destino utiliza o mesmo writer das edições manuais do ADR-0016, reservado durante load → merge → save. Versões anteriores são preservadas; retry de gravação conserva candidato/ticket exatos, sem HTTP ou novo áudio. Schema 1 e os limites locais permanecem, sem CAS multiprocesso. Guardar não muda faixa, autoplay ou versão exibida; a escolha posterior usa **Versões**.

Fechamento: **7/7 CTest, 494 Qt**, incluindo **356 Quick** e **11 Qt** do owner. Core combinado **304 casos/7.138 assertions**, também sem Qt; subsuite de downloads **170 casos/4.338 assertions**. Merge/writer/serviço/lookup/projeção têm seus recortes no [registro deste incremento](../../download-lyrics.md#evidência-deste-incremento); totais sobrepostos não devem ser somados. Builds/lint Lite/Completa e formato **23 C++/3 QML** passaram. A jornada usa catálogo real somente em TEMP e rede/resolver/publicação falsos, sem novo ensaio online LRCLIB, runtime yt-dlp/Node, mídia pública ou GUI nativa. Produção normal continua OFF; runtime/host autenticados, EJS/NoChild, Letras primário, instalação e distribuição continuam pendentes. O ADR permanece **Proposto**.

Ingressos usam testes Node/Qt offline com schemas compartilhados, IDs/URLs hostis, Unicode, limites, ausência/falha da ponte, resposta obsoleta e clique explícito. A principal deve continuar recusando YouTube e seus testes permanecer verdes. Codec/cliente não provam resolver, internet, conversão real, associação de letra em aquisição pública, Brave instalado ou distribuição.

Resolver e coordenação precisam de Red/Green com portas falsas/processos controlados, cancelamento e tempo/disco limitados; depois runtime/ferramentas, prova real autorizada e composição instalada. Letras exige uma fonte acessível e contrato compatível antes do adapter de produção. Os gates nativos anteriores não são reabertos por este pedido. Evidências e pendências ficam no [guia do fluxo](../../youtube-audio-and-lyrics.md).

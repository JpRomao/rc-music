# ADR-0016: versões locais de letras por música

**Estado:** Aceito

**Data:** 2026-11-04

**Decisão de escopo:** pedido explícito do mantenedor para guardar várias formas de letra e acrescentá-las depois; domínio, porta, store e composição Qt abaixo adotados no código. Os checks e gates conservam sua evidência própria.

## Contexto

O mantenedor pediu guardar todas as formas disponíveis de uma letra e adicionar outras posteriormente. Japonês em kanji/kana e japonês romanizado são representações do mesmo idioma. Algumas músicas terão somente uma delas. Tradução e geração por LLM foram levantadas como evoluções possíveis, sem escolha de modelo, serviço ou implantação.

O [ADR-0013](0013-lyrics-and-online-lookup.md) conserva o histórico da primeira entrega, com `.lrc` irmão e LRCLIB somente em memória. Esta decisão acrescenta persistência explicitamente solicitada, sem consulta automática, novos hosts ou aquisição de áudio. A pesquisa do exemplo do Letras recebeu HTTP 403; não confirmou MP3 direto, variantes atuais ou API/licença para um cliente externo. A [resposta antiga do desenvolvedor](https://apps.apple.com/mz/app/letras-com/id773347891), de 18/05/2022, menciona vídeos do YouTube e ausência de download de músicas. Não é validação do funcionamento atual de cada página.

## Decisão

1. Manter várias versões independentes por arquivo de áudio, com ID próprio, nome, idioma, escrita, tipo Original/Romanizada, origem textual e indicação de conteúdo gerado. Uma versão é suficiente; não fabricar outra nem substituir silenciosamente a original. Idioma e escrita são metadados informados pelo usuário, sem detecção automática ou validação de precisão linguística.
2. Oferecer **Versões** junto à letra: adicionar texto/LRC colado, salvar a letra atualmente carregada, selecionar a versão exibida e remover somente a versão escolhida após confirmação. Adicionar posteriormente não exige novo download ou mudança da fila. O fluxo não abre nem altera o áudio, tags ou `.lrc` irmão.
3. Conservar o parser e relógio existentes. Texto com tempos mantém seus cues; texto sem tempos permanece sem sincronização. Serializar uma letra carregada sem inventar timestamps. Metadados e letras são texto simples. Geração, romanização automática, tradução e alinhamento de tempos ficam fora desta entrega.
4. Usar uma porta síncrona `LyricsVariantsStore` e um adapter Qt local. Cada caminho absoluto exato identifica um documento `lyrics/<SHA-256 do path UTF-8>.json` na configuração. Guardar também o caminho no documento e conferir sua igualdade antes de carregar/gravar. O hash seleciona o nome; não anonimiza metadados nem acompanha arquivos movidos/renomeados automaticamente.
5. Separar esse catálogo da sessão, preferências, playlists e arquivos de mídia. Usar schema v1 estrito, IDs decimais em strings, até 16 versões, 128 bytes por nome, 64 por idioma/escrita, 2048 por origem, 256 KiB de conteúdo por versão e 4 MiB de conteúdo somado. Limitar o JSON a 10 MiB. As versões respeitam UTF-8 e os limites de parsing já aceitos.
6. Gravar com lock cooperativo sem espera, backup anterior e `QSaveFile` sem fallback direto. Leitura não repara documentos. Versão futura/indisponibilidade impede edição; dados inválidos não autorizam gravar um catálogo vazio sobre os existentes. Falha depois de um commit físico continua sem confirmação de salvamento.
7. Executar I/O fora da thread visual, com um job ativo e identidade da faixa copiada. Publicar mudanças somente após Saved. Conservar candidato em falha para retry sem repetir a mutação, inclusive se a reprodução mudar de faixa; recarregar exige abandonar explicitamente o candidato em memória e não promete rollback. Resultado de outra faixa não pode aparecer na atual.
8. A saída fecha admissão e aguarda leitura/gravação e resolução de alterações sem confirmação. Cancelar saída rearma as ações. Fechar o processo não confirma a escrita. As confirmações QML ficam vinculadas ao backend, à faixa e ao ID escolhido, sendo revogadas quando o alvo muda. Se a fila ficar sem faixa atual, a UI mantém acesso à resolução: retry continua para o alvo original; abandono explícito remove somente o candidato em memória, sem leitura ou promessa de desfazer o disco.
9. O caminho `.lrc`/LRCLIB atual continua disponível sem salvar automaticamente. Persistência exige ação explícita, com indicação da origem. Nenhuma nova requisição é necessária para alternar, carregar ou editar versões guardadas. Não enviar letras, áudio ou paths a uma LLM.

## Evolução: writer compartilhado com letras adquiridas — 2026-11-05

A composição opcional de [letras do áudio adquirido](../../download-lyrics.md) reutiliza `LyricsBinding` e `LyricsVariantsStore`; não cria um writer paralelo para o mesmo catálogo. A admissão C++ recebe ticket, alvo publicado e drafts limitados. A reserva cobre leitura fresca, `mergeAcquiredLyrics` e gravação em worker, impedindo sobreposição com edições manuais. O merge ignora IDs dos drafts, preserva entradas existentes, atribui IDs locais e deduplica conteúdo LRC canônico dentro de tipo/idioma/escrita. Conteúdo divergente da mesma forma exige a escolha explícita de manter versões distintas; não substitui uma versão salva.

Falha de gravação conserva candidato e ticket exatos para retry sem repetir merge, consulta ou aquisição de áudio. Troca de faixa não muda o alvo da escrita, e a biblioteca visual só recebe um resultado correspondente após `Saved`; a seleção atual é preservada. Ler/gravar versões não altera áudio, `.lrc`, tags ou fila. O usuário pode guardar uma forma por vez e selecionar a desejada em **Versões** ao tocar o arquivo depois.

Schema 1, identidade por path exato, limites, backup e lock cooperativo permanecem. A serialização pertence a este actor compartilhado; não é CAS, merge transacional entre processos ou promessa de rollback após confirmação ambígua. Preparação/cancelamento de saída e abandono explícito continuam obedecendo aos contratos da decisão original. Evidência disponível: merge puro **21 casos/253 assertions** e writer **24 casos Qt**; fechamento integrado e Quick constam do [registro](../../download-lyrics.md#evidência-deste-incremento). Runtime/host YouTube, acesso Letras e distribuição permanecem gates do ADR-0017, sem alterar o estado aceito desta persistência local.

## Evolução de 2026-11-13

O catálogo aceita **Tradução** como forma de texto fornecida pelo usuário, com idioma de destino opcional. A gravação usa schema 2 quando há uma tradução. Versões anteriores do RC Music tratam esse documento como futuro e bloqueiam a sobrescrita. Catálogos apenas com original e romanização continuam no schema 1.

Para um MP3 publicado com página Letras validada, a revisão aceita várias formas coladas em uma única operação do writer. A URL da revisão é a origem de cada versão. A consulta primária depende de acesso compatível, como descrito no ADR-0017.

## Alternativas e consequências

- Um único `.lrc` não atende às várias representações e pode exigir escrita em pasta de mídia sem permissão. Os documentos de configuração evitam modificar a biblioteca pessoal.
- Guardar cada forma como se fosse outra música duplicaria áudio e confundiria fila e playlists. As versões pertencem ao arquivo existente.
- Scraping de Letras ou captura de seu player não resolve autorização, identidade da gravação ou aquisição de MP3. A integração depende de evidência e decisão próprias; não é habilitada por este ADR.
- Um banco, serviço remoto ou engine de LLM não é necessário para persistir texto. O catálogo limitado usa o SDK Qt existente.
- A identidade por path exato não funde automaticamente caminhos com caixa diferente, aliases, cópias ou faixas movidas. Seleção de uma versão afeta a exibição da sessão; as versões são persistentes, sem promessa de memorizar a última seleção.
- O lock/backup oferece proteção cooperativa e recuperação delimitada, sem alegação de filesystem hostil, troca adversarial de ancestrais ou durabilidade após perda física de energia. Armazenamento pessoal não concede licença de redistribuição das letras.

## Validação e revisão

TDD deve verificar Unicode, IDs/duplicatas, variantes únicas e múltiplas, limites, serializer com cues simultâneos/vazios e texto sem tempos. Store deve cobrir restart, separação de arquivos, backup, corrupção, futuro, lock, indisponibilidade e falhas antes/depois de commit, preservando áudio sentinela. Binding deve provar Saved antes da publicação, resultado obsoleto, retry do mesmo candidato, troca de faixa com falha e preparação/cancelamento de saída. Quick deve cobrir ações, foco/teclado, confirmação por identidade, texto simples e geometria em 720 × 520. Builds Lite/Completa e regressões de letras/playlists têm seus próprios checks.

Revisar antes de adicionar tradução, LLM, exibição simultânea alinhada, migração de paths, mais provedores, extração de sites, MP3 remoto ou sincronização automática. As evidências finais pertencem a [letras e variantes](../../lyrics-variants.md); este documento não aprova gates nativos, pacote, licenças ou distribuição.

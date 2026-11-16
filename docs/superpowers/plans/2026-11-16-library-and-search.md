# Plano: biblioteca permanente e busca interna

**Base:** [ADR-0019](../../architecture/decisions/0019-internal-search-library-and-extension-retirement.md) e a proposta aprovada `out/product/uiux-planning-4efd8ba/BIBLIOTECA-PERMANENTE.md`.

**Estado:** em execução desde 2026-11-16, em fatias. Cada fatia termina com commit e checks próprios.

## Fatias

| Fatia | Entrega | Checks |
|---|---|---|
| L1 | Domínio `LocalLibrary` e store `library.json` v1 (escrita atômica, backup, lock, versão futura) | Catch2 do domínio; Qt Test do store |
| L2 | Ingresso local alimenta a biblioteca: Abrir, Adicionar, soltar, encaminhamento e playlist; semente aditiva da fila e das playlists; `Saved` correlacionado | binding + restart |
| L3 | Importação de download nos dois destinos (biblioteca e fila) com recibos, sem refazer download nem ressuscitar remoção | importação/retry |
| L4 | Página Biblioteca em Lista (Capas como alternativa), busca local sem acento, adicionar à fila/playlist, remover registro e localizar arquivo | Quick + modelo |
| L5 | Busca online pelo yt-dlp, só metadados, seleção virando pedido de aquisição por ID | resolvedor + UI |
| L6 | Filtros da Atividade: com erro, concluídos, na fila e baixando | modelo + Quick |
| L7 | Colar vários links do YouTube e do Letras de uma vez | parser + binding + Quick |
| L8 | Até 3 downloads simultâneos, cada um com seu próprio job | serviço + executor + restart |

A retirada da extensão saiu do plano (revisão do ADR-0019 em 2026-11-16). Ela fica para uma frente futura, com outro papel: mídias fora do YouTube e do Letras.

## Contrato de L1

- Um registro por caminho exato (comparação de `std::filesystem::path` sem normalizar nem case-fold). Reingressar o mesmo caminho devolve o ID existente.
- O ID é próprio, estável e nunca é reaproveitado: `nextId` persiste no documento, então remover o maior ID e reiniciar não o devolve a outro arquivo.
- Remover tira só o registro. Indisponibilidade é um estado do registro, não remoção.
- Localizar troca o caminho do mesmo ID. Um caminho que já pertence a outro registro é conflito, sem fusão.
- Título, artista e álbum seguem a política de texto de linha única já usada (`sanitizeMetadataText` sem alterações) ou ficam vazios. Duração é opcional.
- Limites: 20.000 registros, 512 bytes por texto, 32.768 unidades por caminho e 24 MiB de carga útil.
- A busca sem acento fica na apresentação (L4), com `QString`, porque o núcleo não depende de Qt.
- O store copia o padrão do `QtJsonPlaylistStore`: lock cooperativo, primário mais `.bak`, versão futura nunca sobrescrita, e só `Saved` confirma.

## Contrato de L2

- `LibraryBinding` carrega e grava `library.json` num worker. O catálogo confirmado só avança com `Saved`. Ingressos que chegam durante uma gravação entram numa gravação seguinte.
- Uma falha mantém a mudança pendente até um novo ingresso ou `retrySave`, sem repetir sozinha. A saída do app espera só a gravação em andamento; uma falha nunca segura a saída.
- `PlayerPresenter::setLocalAdmissionObserver` recebe os caminhos aceitos em Abrir, Adicionar, soltar/encaminhamento e enfileirar playlist, depois que a fila confirma. Exceções do observador não desfazem a fila. Downloads ficam para L3.
- Semente de primeiro uso: só quando `library.json` não existia, uma vez da fila e uma vez das playlists (`PlaylistsBinding::entryPaths`). Depois disso, sementes são ignoradas, então um registro removido não volta.
- Biblioteca ilegível (inválida, versão futura, indisponível) nunca é sobrescrita; ingressos ficam em memória, com limite.

## Contrato de L3

- `library.json` guarda um registro limitado (`downloadJobs`, 1.024 jobs, o mais antigo sai primeiro) dos downloads já admitidos. `LocalLibrary::admitDownload` admite cada job uma vez; um job já registrado não muda nada, então um replay ou a reconciliação nunca ressuscita um registro removido. Um ingresso local explícito do mesmo arquivo continua funcionando.
- `PlayerPresenter::setDownloadAdmissionObserver` informa cada importação de download confirmada na fila, com o ID do job no modo durável. Uma importação repetida não anexa de novo, então não é informada duas vezes.
- Reconciliação a cada início: `downloadImportReceipts()` da sessão passa pela biblioteca. Um download cuja gravação na biblioteca se perdeu (falha ou queda entre as confirmações) entra na próxima abertura, sem refazer o download nem tocar na fila.
- A ordem não é atômica entre os dois destinos: fila e biblioteca confirmam separadamente. A distinção desses estados na Atividade fica para L4.

## Contrato de L4a

- `LibraryListModel` projeta só registros confirmados, em ordem de entrada. O título cai para o nome do arquivo sem extensão; duração em `m:ss`. O filtro local decompõe acentos, ignora caixa e exige todos os termos em título, artista, álbum ou nome do arquivo, sem rede nem disco.
- `LibraryBinding` expõe `model`, `filterText`, `visibleCount`, `enqueue(id)` (fim da fila, sem tocar, pelo mesmo caminho das playlists) e `remove(id)` (só o registro, confirmado por `Saved`).
- Página `LibraryPanel` no modo Aurora, abrindo em Lista. Tem cabeçalho editorial, filtro, contador “N de M faixas”, colunas Nº / Faixa e artista / Álbum / Tempo, adicionar à fila e remover com confirmação que diz que o áudio fica. Estados distintos para vazio, sem resultado e falha de carga ou gravação. A entrada “Biblioteca” fica na lateral entre Início/Fila e Playlists.
- Fica para L4b: metadados em segundo plano (hoje só aparecem tags para registros já preenchidos), visão em Capas, localizar arquivo indisponível, adicionar à playlist e a distinção na Atividade.

## Contrato de L4b

- Metadados em segundo plano: `LibraryBinding::setMetadataExtractor` (TagLib no app) lê lotes de até 64 registros nunca lidos ou indisponíveis. Cada registro é tentado no máximo uma vez por sessão. Arquivo inalcançável fica indisponível; formato ilegível continua listado. Duração 0 marca "lido, duração desconhecida", para não reler a cada início, e aparece em branco. O resultado só vale se o registro ainda existir no mesmo caminho, e o enriquecimento nunca repete sozinho uma gravação que falhou.

- Localizar: faixa indisponível ganha "Localizar", com escolha de arquivo própria da biblioteca. O ID e as tags ficam, e o registro volta a ficar disponível. Um caminho de outra faixa é conflito, sem mudança. Fechar a página cancela o pedido, e respostas tardias para registros removidos são ignoradas.
- Adicionar à playlist: menu por linha com as playlists salvas, via `PlaylistsBinding::appendToPlaylist`. Só `Saved` confirma, e a seleção da página Playlists não muda.
- Capas: alternativa à Lista, que continua padrão. As capas são marcadores em tons do tema com a inicial do álbum ou do título. Arte real precisa de um cache de capas próprio, ainda pendente.
- Atividade: cada download com MP3 final mostra para onde ele foi: "MP3 pronto", "Na fila de reprodução" (concluído ou já importado) e "Na biblioteca" ou "Biblioteca pendente". A biblioteca vem de `LibraryBinding::downloadJobs`, o registro confirmado de jobs, que só muda com `Saved` e continua valendo se o registro for removido depois (o download entrou na biblioteca).
- Capas reais: `LibraryArtworkProvider` (`image://libraryart/<id>`) lê a arte embutida com o mesmo extrator TagLib, fora da UI (duas leituras por vez), só quando um cartão aparece. As 256 miniaturas mais recentes ficam em memória, com chave de registro e caminho, então um arquivo localizado em outro lugar é lido de novo. `LibraryArtworkIndex` guarda ID e caminho dos registros disponíveis e acompanha cada `Saved`. Sem arte, registro desconhecido ou indisponível rende um pixel transparente e o marcador do tema continua visível. Nada é gravado em disco.
- Pendente de L4b: teste no app real.

## Contrato de L5

- Resolvedor: `--search <consulta>` é o único outro modo do entrypoint. A consulta chega normalizada (espaços colapsados, sem controle, até 400 bytes UTF-8). Só o `YoutubeSearchIE`, com extração plana `ytsearch12:`, sem player, formato ou JavaScript. Saída `{"entries": [...]}` com até 12 vídeos individuais: `id`, `title` e, quando houver, `channel` e `duration`. Canais, playlists, ao vivo, IDs inválidos ou repetidos e textos acima de 512 bytes ficam de fora.
- Player: `WindowsYoutubeBrokerProcess` em modo `Search` passa `--search <consulta>` ao extractor do mesmo runtime verificado dos downloads, não lê o EJS e recusa qualquer frame de desafio, então o Node nunca roda. `YoutubeSearchSession` só publica resultados depois de saída 0 com pipes reconhecidos; o decodificador estrito recusa campo desconhecido, ID inválido ou repetido e lista longa. `ThreadedYoutubeSearch` roda uma busca por vez numa thread própria e drena a sessão no cancelamento e na destruição.
- Página Buscar: entrada na lateral só quando a busca está disponível (componente de downloads YouTube ativo). Enter ou Buscar inicia; Cancelar interrompe. Mensagens distintas para buscando, sem resultado, cancelada, sem resposta, demora e resposta inesperada. Cada resultado tem Baixar, que manda um `YoutubeAudioRequest` comum por ID pela admissão existente (sem artista, porque canal não é artista); só IDs da lista atual podem ser pedidos, uma vez cada. O áudio segue para a biblioteca e a fila pelo caminho de downloads de L3.
- Teste real (2026-11-16): runtime `prod` reconstruído com yt-dlp 2026.10.26 (wheel conferido pelo hash do lock, venv sem rede, 36 arquivos), perfil de downloads com os workers do código atual e app Release montado por `New-RcMusicLocalRuntime.ps1`. No app, a busca "Asterisk Orange Range" trouxe 12 resultados com canal e duração. Baixar o primeiro publicou o MP3 em 13 s, e a Atividade mostrou "Concluído · MP3 pronto · Na fila de reprodução · Na biblioteca". O teste usou o perfil real do mantenedor, com cópia de segurança antes. O runtime ainda não está assinado (ADR-0020): o perfil usou os hashes do recibo.
- Achado: um download antigo interrompido antes de criar arquivos (sem registro de propriedade) responde "A origem dos arquivos não pôde ser confirmada" em Verificar arquivos e não oferece como encerrar o item. Ele não bloqueia outros downloads, mas fica preso no histórico.

## Contrato de L6

- A Atividade ganha os filtros Todos, Com erro, Concluídos, Na fila e Baixando, acima da lista. Com erro reúne falhou, importação falhou e interrompido; Concluídos reúne concluído, importando e importado; Na fila é só a fila; Baixando reúne baixando, convertendo, publicando e cancelando. Cancelados aparecem só em Todos.
- O filtro é um `SortFilterProxyModel` do QtQml.Models sobre o mesmo modelo de jobs, com `FunctionFilter` pelo papel `state`. Um job que muda de estado troca de filtro sozinho. As ações continuam usando o ID estável do job, não a posição na lista filtrada.
- Teclado: o grupo é uma parada do Tab (o filtro ativo), e as setas trocam de filtro, como num grupo de opções. Cada filtro tem sua mensagem de lista vazia.

## Estado

- L1 concluída: commit `d4cbc8c`.
- L2 concluída: catálogo alimentado pelo ingresso local, com 704/704 CTest (sem a suíte Quick, sem mudança de QML).
- L3 concluída: downloads nos dois destinos com registro de jobs, 707/707 CTest (sem a suíte Quick).
- L4a concluída: página Biblioteca em Lista. Quick 527/527, contando os 6 casos de composição repetidos com `RC_MUSIC_UI_ARTIFACT_DIR`, que o CTest define.
- L4b, parte 1 (metadados): 707/707 CTest.
- L4b concluída (metadados, localizar, playlist e Capas): CTest 707/707, Quick 530/530.
- L5 concluída no código (resolvedor, broker, sessão, binding e página Buscar) e testada no app real com o runtime reconstruído.
- L6 concluída: filtros da Atividade.
- L4b, restante: Atividade com destinos do MP3 (`63dc156`) e capas reais em memória.

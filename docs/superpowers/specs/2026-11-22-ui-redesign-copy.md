# Revisão dos textos do RC Music (pt-BR)

Anexo de [2026-11-22-ui-redesign-design.md](2026-11-22-ui-redesign-design.md).

O mantenedor aprovou o tom e as trocas em 2026-11-22, mantendo “novamente” (regra R1, seção 3.1). Cada fatia aplica as trocas da sua área no mesmo commit em que ajusta os testes que as fixam. As linhas citadas são do `main` em `dd41755` e podem ter mudado: confira antes de editar.

Como ler as tabelas:

- **teste que fixa** traz o `arquivo:linha` (relativo a `tests/`) onde o texto atual aparece literalmente.
  - O prefixo `~` marca uma coincidência parcial: o texto tem placeholder, e o teste contém o maior trecho fixo.
  - `—` quer dizer que nenhum teste fixa o texto.
  - Os testes rodam com avisos fatais, então cada pino precisa mudar no mesmo commit do texto.
- Textos curtos e genéricos ("Tentar novamente", "Remover", "Pausar") podem trazer pinos de outro catálogo com o mesmo literal. Confira antes de editar.
- `tests/ui/fixtures/fixture.v1.json`, `FixtureRepositoryTests.cpp` e `ShellUiContractTests.cpp:291-321` fixam os rótulos do modo legado (`ShellUiContract`). Eles só mudam se o modo legado mudar.
- Ao mudar um `qsTr`, rode o `lupdate`. O arquivo `src/ui/translations/music-player_pt_BR.ts` usa as fontes como chave. Os textos com `%n` precisam de formas de plural (numerus) no `.ts`.

---

## 1. Glossário

| termo | usar para | evitar |
|---|---|---|
| **faixa** | Item que toca. Use em listas, contagens, menus e controles ("Remover da fila", "%n faixas", "Tocar %1"). | "item" (fila), "registro" (biblioteca), "música" em controles |
| **música** | Só em frases de conversa e títulos de seção ("ouvir música junto", placeholder da busca). | em botões, contagens e mensagens de erro |
| **áudio** | O som ou o arquivo de som, em oposição a vídeo e letra ("Abrir áudio", "Abrir outro áudio", "O áudio e as outras versões continuam"). | "Áudio local" como nome de artista |
| **arquivo** | O arquivo no disco ("Localizar arquivo", "Arquivo não encontrado"). | "fonte", "origem", "ocorrência" |
| **fila** | Só a fila de reprodução. | "fila de downloads": use "Aguardando" para o estado e "os downloads" para o conjunto |
| **biblioteca** | Tudo o que foi adicionado. | "coleção", "suas listas" |
| **playlist** | Lista salva pelo usuário. | "lista" sozinho, "listas" |
| **letra** | Singular para a letra de uma faixa ("Letra", "Abrir letra", "Fechar letra"). | "Letras" no botão do player, porque Letras também é o site Letras.mus.br |
| **versão da letra** | Cada variante salva de uma letra. | "versão" sozinha quando puder ser confundida com a versão do app |
| **tipo de versão** | Sincronizada, Texto simples, Romaji etc. | "forma da versão" |
| **salvar** | Gravar no disco. | "guardar" (só em Letra da faixa baixada) |
| **descartar alterações** | Desistir do que ainda não foi salvo. | "abandonar", "descartar edição", "descartar tentativa", "edição pendente", "em memória" |
| **alterações** | O que mudou e ainda não foi salvo. | "edição", "alteração em memória", "tentativa" |
| **sala / Ouvir junto** | Ouvir junto é o recurso; a sala é a instância. | "build", "servidor de apresentação" |
| **anfitrião** | Quem abriu a sala. | "host" |
| **pessoa / pessoas** | Quem está na sala. | "participantes", "membros" |
| **cancelar convite** | Invalidar um link de convite. | "revogar" |
| **Atividade** | A página de downloads. "Download" é cada item. | "painel Downloads", "Downloads" como nome de página, "a fila de downloads" |
| **pedido** | Só no selo "Pedido" da busca e nos pedidos para entrar na sala. | "pedidos encerrados" (use "downloads encerrados") |
| **adicionar à fila** | O MP3 baixado entra na fila. | "importar", "importação" em texto visível |
| **Configurações** | A página de ajustes do RC Music, inclusive nos avisos sobre dados salvos. | "Preferências" |
| **Configurações do Windows** | O app do Windows. Sempre com "do Windows". | "Configurações" sozinha para o Windows |
| **RC Music** | O próprio aplicativo. | "player", "app", "aplicativo" |
| **versão mais nova do RC Music** | Dados salvos por outra versão do app. | "versão mais recente das letras/playlists" (parece uma versão de letra) |
| **cópia de segurança** | O arquivo de recuperação. | "backup" |
| **novamente** | Repetição (“Tentar novamente”), como no próprio Windows. Regra R1, ver 3.1. | “de novo” (16 ocorrências) |
| **tocar** | Ação sobre uma faixa ("Tocar agora", "Tocar %1", "Tentar tocar novamente"). | "reproduzir" fora do botão Reproduzir/Pausar e da bandeja |
| **Reproduzir / Pausar** | Só o botão de transporte e o menu da bandeja, por ser o par usual do sistema. | — |
| **Buscar** | A página e a ação de busca (YouTube, letras). | "Pesquisar", "Consultar", "consulta" |
| **Não foi possível…** | Abertura padrão de erro. | "Não deu", "Falha ao/de", "A gravação … falhou" |
| **reticências (…)** | Botão que abre uma janela ou seletor e pede mais dados; texto em andamento ("Salvando…"). | botão que age direto |
| **Ex.:** | Exemplo em placeholder. | "Por exemplo" solto no placeholder |
| **unidade** | Disco do Windows (C:, D:). | "drive" |

---

## 2. Inconsistências encontradas

1. **Configurações × Preferências.**
   - A navegação e o título da página dizem "Configurações" (`NavigationText`, `ProfileText.qml:5`).
   - "Preferências" aparece em:
     - o sobretítulo `NavigationText.qml:41` ("PREFERÊNCIAS DO APLICATIVO");
     - os erros `ErrorText.qml:16` e `:19` ("Preferências > Reprodução");
     - todos os avisos de dados salvos em `PreferencesText.qml:38-71`;
     - o modo legado `ShellUiContract.cpp:194`.
   - "Configurações" também nomeia o app do Windows (`PreferencesText.qml:16-19`) sem distinção.
2. **host × anfitrião.**
   - "host" aparece em `QueueText.qml:44`, `:45` e `RoomText.qml:98`.
   - "anfitrião" aparece em `RoomText.qml:99` e em todo `ListeningRoomBinding.cpp`.
3. **guardar × salvar.**
   - "guardar" aparece em:
     - Letra da faixa baixada (`LyricsText.qml:130-140`);
     - `DownloadLyricsBinding.cpp:187`, `:228-247`;
     - `LyricsBinding.cpp:524`, `:659`, `:674`, `:927`, `:933`, `:1086`.
   - "salvar" é usado no resto do app, inclusive no diálogo vizinho de versões (`LyricsText.qml:36-41`, `:96`).
4. **Abandonar × Descartar.**
   - "Abandonar alteração" aparece em `LyricsText.qml:68`, `:101`, `:104`, `:105`, `:108` e `LyricsBinding.cpp:552`, `:1434`.
   - "Descartar edição" aparece em `PlaylistsText.qml:29`, `:41-44`.
   - "Descartar tentativa" aparece em `LyricsText.qml:134`, `:138`, `:139`.
   - São três jeitos de dizer a mesma ação.
5. **Forma da versão × Tipo de versão.**
   - "Forma" aparece em `LyricsText.qml:128-129`, `DownloadLyricsBinding.cpp:187`, `:193` e `LyricsBinding.cpp:659`.
   - "Tipo de versão" aparece em `LyricsText.qml:82`.
6. **Escolher × Escolha.** "Escolher gravação/formato" (`LyricsText.qml:29`, `:31`) e "Escolha a gravação/o formato" (`:120`, `:122`) nomeiam os mesmos campos.
7. **Tentar salvar × Tentar salvar novamente.** Os dois aparecem para a mesma ação: `LyricsText.qml:41` × `:67`, `PlaylistsText.qml:27`, e `PreferencesText.qml:62` ("Tentar novamente" para salvar).
8. **novamente × de novo.** Há 106 ocorrências de "novamente" e 16 de "de novo".
   - Exemplo: `OnlineSearchBinding.cpp:387` diz "tente de novo", e `BrowserMediaPanel.cpp:321` diz "tentar novamente" para a mesma situação.
9. **"fila" com dois sentidos.**
   - Fila de downloads: `DownloadText.qml:15`, `:21`, `DownloadJobsModel.cpp:57`, `OnlineSearchBinding.cpp:387`, `:389`, `BrowserMediaPanel.cpp:319-328`, `:371`.
   - Fila de reprodução: todo o resto. Por isso `DownloadText.qml:54` precisa dizer "Na fila de reprodução".
10. **Atividade × Downloads × painel Downloads.**
    - A página se chama Atividade (`OnlineSearchBinding.cpp:387-394`).
    - Mesmo assim aparecem "painel Downloads" (`BrowserMediaPanel.cpp:319`), "Confira Downloads" (`:321-328`), "Voltar aos downloads" (`DownloadText.qml:47`), "Feche Downloads" (`DownloadApplicationBinding.cpp:1480`) e "já foi para os downloads" (`SearchText.qml:21`).
11. **"versão" com dois sentidos.**
    - "Uma versão mais recente das letras/playlists/biblioteca" (`LyricsBinding.cpp:710`, `:754`, `PlaylistsBinding.cpp:101`, `:125`, `LibraryBinding.cpp:335`) se refere à versão do app.
    - Na tela de letras, "versão" é a versão da letra.
12. **backup × cópia de segurança.**
    - "backup" aparece em `LibraryBinding.cpp:316`, `PlaylistsBinding.cpp:97` e `LyricsBinding.cpp:706`.
    - "cópia de segurança" aparece em `PreferencesText.qml:40`, `:48` e `:56`.
13. **Nome do próprio app.** O app aparece como:
    - "player": `ApplicationText.hpp:30-37`, `LifecycleText.hpp:33`, `PreferencesText.qml:50`, `EmbeddedBrowserBinding.cpp:63`;
    - "app": `RoomText.qml:16`, `LyricsText.qml:83`, `ListeningRoomBinding.cpp:773`, `:912`;
    - "aplicativo": `PreferencesText.qml:67`, `NavigationText.qml:41`;
    - "RC Music": o resto.
14. **"sessão" com quatro sentidos.**
    - "Usar nesta sessão" (`LyricsText.qml:43`).
    - "Nenhum download nesta sessão" (`DownloadText.qml:11`).
    - "Avisar … nesta sessão" (`LifecycleText.hpp:36`).
    - "Restaurar última sessão", que é o sentido certo.
    - Além disso, "nesta execução" aparece em `PreferencesText.qml:54`, `:59` e `ProfileText.qml:21`.
15. **Rótulos que dizem a mesma coisa de formas diferentes.**
    - "Dispensar aviso" (`PreferencesText.qml:61`) × "Fechar aviso" (`RoomText.qml:49`).
    - "Encerrar" (`PreferencesText.qml:7`) × "Sair" (menu da bandeja, `LifecycleText.hpp`).
    - "revogar" (`RoomText.qml:17`, `:38`) × "cancelou este convite" (`ListeningRoomBinding.cpp:906`).
16. **Fallback de artista.**
    - "Áudio local" (`NavigationText.qml:10`, usado em `QueuePreview.qml:181` e `Main.qml:766`).
    - "Artista desconhecido" (`LibraryText.qml:15`).
17. **Reticências.** "Adicionar arquivos" (`QueueText.qml:6`) × "Adicionar arquivos…" (`PlaylistsText.qml:24`). Os dois abrem o seletor.
18. **Duração.** "TEMPO" (`LibraryText.qml:14`) × "Duração" (Detalhes da faixa).
19. **Letra × Letras.** O botão do player diz "Letras" (`TransportText.qml:19`). Ele abre a gaveta "Letra", e "Letras" também é o nome do site.
20. **Tocar × Reproduzir.**
    - "Reproduzir %1" aparece em `NavigationText.qml:11` e "Tentar reproduzir novamente" em `ErrorText.qml:23` e `QueueText.qml:21`.
    - O redesenho usa "Tocar agora".
21. **Título.** "Título da música" (`LyricsText.qml:23`) × "Título" (Detalhes da faixa).
22. **"Não deu"** (`RoomText.qml:69`) destoa do "Não foi possível" usado em todo o resto.
23. **Gênero.** "Você foi removido da sala." (`ListeningRoomBinding.cpp:909`) é masculino.
24. **Erros sem frase completa nem ponto.** Os vizinhos dessas linhas são frases completas:
    - "Link de convite inválido" (`ListeningRoomBinding.cpp:201`);
    - "Arquivo de áudio inválido" (`:523`);
    - "Sem permissão" (`:529`, `:555`);
    - "A sala está cheia" (`:894`).
25. **Jargão visível.**
    - "build" (`RoomText.qml:4`) e "servidor de apresentação" (`:5`).
    - "pykakasi" (`LyricsBinding.cpp:791`, `:849`).
    - "catálogo" (`DownloadLyricsBinding.cpp:244`).
    - "metadados" (`TrackDetailsText.qml:39-52`, `LyricsText.qml:35`).
    - "ocorrência" (`QueueText.qml:25`) e "registro" (`LibraryText.qml:27`).
    - Nomes de arquivo internos: `library.json` (`LibraryBinding.cpp:308`) e `playlists.json` (`PlaylistsBinding.cpp:111`).
    - "Temporários próprios" (`DownloadJobsModel.cpp:376`).
    - "frames … sandbox" (`BrowserMediaPanel.cpp:68`) e "drive" (`DownloadApplicationBinding.cpp:209`).
26. **Plurais quebrados.** Todos saem errados quando o número é 1:
    - "%1 faixas" (`PlaylistsText.qml:10`) dá "1 faixas".
    - "%1 de %2 faixas" (`LibraryText.qml:7`).
    - "Até %1 pessoas além de você" (`RoomText.qml:27`).
    - "%1 pessoas com este cargo" (`RoomText.qml:71`).
    - O app já usa numerus em outros lugares, como `qsTr("%n segundo", …, count)`.
27. **Textos fora da tradução.** São `QStringLiteral` sem `tr()`, então o `lupdate` não os vê:
    - `QtLyricsService.cpp:242-805`;
    - `WindowsWebView2Browser.cpp:600-626`;
    - parte de `BrowserMediaSnapshotCodec.cpp`.
28. **"descartada" com sentido errado.** "A capa foi descartada…" (`TrackDetailsText.qml:47-50`) sugere que o arquivo foi alterado. Na verdade, a capa só não foi carregada.
29. **Textos sem uso.**
    - `NavigationText.collectionHint` (`:27`) e `ProfileText.currentAvatar` (`:11`) não aparecem em nenhuma QML.
30. **Confirmações que não seguem a regra 4.**
    - `DownloadConfirmation` usa o mesmo texto como título e como botão de confirmação: arquivar, adicionar à fila, sair sem salvar, encerrar recuperação.
    - A confirmação de `LyricsVariantsPanel` não tem título visível, e o corpo é a pergunta (`LyricsText.qml:104-107`).
    - "Excluir playlist?" não nomeia a playlist (`PlaylistsText.qml:38`).

---

## 3. Propostas por catálogo

Total: 375 linhas nas tabelas, mais as 16 trocas da regra R1 em 3.1.

Notas sobre as tabelas:

- "(igual à linha N)" quer dizer que o mesmo texto aparece duplicado.
- Quando o texto ganha `%n`, a QML ou o C++ precisam passar o número como terceiro argumento de `qsTr`/`tr`, e o `.ts` precisa das formas singular e plural.
- Quando o texto ganha um `%1` novo, quem o usa precisa passar esse argumento:
  - `PlaylistsText.qml:38` e `LyricsText.qml:103`;
  - as confirmações de `LyricsVariantsPanel.qml:1163-1193`, que passam a mostrar `abandonPending`, `reloadLyrics` ou `deleteVersionTitle` como título.

### LyricsText.qml (51)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| LyricsText.qml:7 | reviewSave | Revisar salvamento | Revisar alterações | “salvamento” soa técnico; forma par com “Descartar alterações” | ui/quick/tst_lyrics_variants.qml:718 |
| LyricsText.qml:8 | reviewLyricsSave | Revisar salvamento da letra | Revisar alterações não salvas da letra | nome acessível do mesmo botão | — |
| LyricsText.qml:20 | saveForTrack | Salvar letra em %1 | Salvar letra em “%1” | nome da faixa entre aspas, como no resto do app | — |
| LyricsText.qml:22 | exactPrivacy | A consulta exata usa o título e o artista do formulário e o álbum e a duração da faixa atual. Pode consultar o LyricFind configurado. | A busca exata envia o título e o artista deste formulário, mais o álbum e a duração da faixa atual. Ela também pode consultar o LyricFind. | “consulta” vira “busca”; frase direta | — |
| LyricsText.qml:23 | trackTitle | Título da música | Título | glossário: faixa; igual a Detalhes da faixa | — |
| LyricsText.qml:24 | titleHint | Informe o título se estiver em branco | Digite o título | dica curta; a atual parece condição | — |
| LyricsText.qml:26 | artistHint | Informe o artista se estiver em branco | Digite o artista | idem | — |
| LyricsText.qml:29 | chooseRecording | Escolher gravação | Escolha a gravação | mesmo texto do diálogo da letra baixada (linha 120) | — |
| LyricsText.qml:31 | chooseFormat | Escolher formato | Escolha o formato | mesmo texto da linha 122 | — |
| LyricsText.qml:35 | exactLookup | Consulta por metadados exatos | Busca exata | “metadados” é jargão; o aviso ao lado explica o que é enviado | — |
| LyricsText.qml:43 | useForSession | Usar nesta sessão | Usar sem salvar | “sessão” é ambíguo; o contraste real é com “Salvar versão” | — |
| LyricsText.qml:49 | closeReader | Fechar letra completa | Fechar letra | redesenho: a gaveta se chama Letra | — |
| LyricsText.qml:56 | currentFile | Arquivo / busca atual | Letra do arquivo ou da busca | “Arquivo / busca atual” não diz que é uma letra | — |
| LyricsText.qml:58 | generatedByTool | Gerada por ferramenta | Gerada automaticamente | “ferramenta” é vago | — |
| LyricsText.qml:64 | selectCurrentToSave | Selecione Arquivo / busca atual para salvar a letra carregada. | Para salvar a letra carregada, selecione “Letra do arquivo ou da busca”. | acompanha a linha 56; objetivo primeiro | — |
| LyricsText.qml:67 | trySave | Tentar salvar | Tentar salvar novamente | hoje há “Tentar salvar” e “Tentar salvar novamente” para a mesma ação | — |
| LyricsText.qml:68 | abandonChange | Abandonar alteração | Descartar alterações | um verbo só: Playlists e a letra baixada usam “Descartar” | ui/quick/tst_lyrics_variants.qml:1095 |
| LyricsText.qml:73 | readingLrc | Lendo LRC… | Lendo arquivo LRC… | nomeia o objeto | — |
| LyricsText.qml:74 | importLrc | Importar .lrc | Importar arquivo LRC… | abre seletor: reticências | — |
| LyricsText.qml:83 | optionalOrigin | Origem (opcional; não é acessada pelo app) | Origem (opcional; o RC Music não abre este endereço) | “acessada pelo app” é vago | — |
| LyricsText.qml:87 | pasteAdvice | Cole a letra ou o conteúdo LRC. Tempos existentes são preservados; texto sem tempos não é sincronizado automaticamente. | Cole a letra em texto ou no formato LRC. Os tempos do LRC são mantidos; texto sem tempos não fica sincronizado. | voz ativa, mais curta | — |
| LyricsText.qml:88 | lyricsOrLrc | Texto da letra ou LRC | Letra em texto ou LRC | mesmo rótulo da linha 125 | — |
| LyricsText.qml:89 | generatedText | Texto gerado por ferramenta | Texto gerado automaticamente | acompanha a linha 58 | — |
| LyricsText.qml:92 | editRejected | Confira o texto, os tempos ou reabra a versão se ela mudou. | Não foi possível salvar. Confira o texto e os tempos, ou reabra a versão se ela mudou. | diz o que aconteceu antes do que fazer | — |
| LyricsText.qml:93 | romajiRejected | Confira o texto e mantenha os tempos da letra original. | Não foi possível salvar. Confira o texto e mantenha os tempos da letra original. | idem | — |
| LyricsText.qml:94 | queuedRejected | Confira o conteúdo e os limites. Se a faixa mudou na fila, abra o formulário novamente. | Não foi possível salvar. Confira o texto; se a faixa mudou na fila, abra esta janela novamente. | “limites” e “formulário” são vagos | — |
| LyricsText.qml:95 | addRejected | Não foi possível adicionar a versão. Confira o conteúdo e os limites informados. | Não foi possível adicionar a versão. Confira o nome e o texto; o texto pode ter passado do tamanho permitido. | “limites informados” não aparecem na tela | — |
| LyricsText.qml:99 | selectLrc | Selecionar arquivo de letra LRC | Escolher arquivo LRC | título do seletor, como os outros “Escolher…” | — |
| LyricsText.qml:101 | abandonPending | Abandonar alteração pendente | Descartar alterações? | título de confirmação em forma de pergunta | ui/quick/tst_lyrics_variants.qml:1101 |
| LyricsText.qml:102 | reloadLyrics | Recarregar letras do disco | Recarregar as versões salvas? | idem | — |
| LyricsText.qml:103 | deleteVersionTitle | Excluir versão da letra | Excluir a versão “%1”? | título nomeia a coisa (novo %1: a QML passa o nome) | — |
| LyricsText.qml:104 | abandonWithoutTrackAdvice | Abandonar a tentativa em memória? Não há faixa selecionada para recarregar. Uma gravação sem confirmação pode já ter sido efetivada; abandonar a tentativa não desfaz os arquivos gravados. | Nenhuma faixa está selecionada para recarregar. As alterações não salvas serão descartadas; se parte delas já tiver sido gravada, ela continua no disco. | tira “tentativa em memória” e “efetivada” | — |
| LyricsText.qml:105 | reloadAdvice | Abandonar a tentativa em memória e recarregar do disco? Uma gravação sem confirmação pode já ter sido efetivada; recarregar não promete desfazê-la. | As alterações não salvas serão descartadas e as versões serão lidas novamente do disco. Se o salvamento já tiver sido concluído sem aviso, recarregar não o desfaz. | idem; o título vira a pergunta | — |
| LyricsText.qml:106 | deleteTrackVersionAdvice | Excluir a versão “%1” de “%2”? O áudio e as outras versões serão preservados. | A versão “%1” sai de “%2”. O áudio e as outras versões continuam. | corpo diz a consequência; a pergunta vai para o título | — |
| LyricsText.qml:107 | deleteVersionAdvice | Excluir a versão “%1”? O áudio e as outras versões serão preservados. | A versão “%1” será excluída. O áudio e as outras versões continuam. | idem | — |
| LyricsText.qml:108 | abandon | Abandonar | Descartar alterações | botão repete o verbo do título | ui/quick/tst_lyrics_variants.qml:1106 |
| LyricsText.qml:110 | deleteAction | Excluir | Excluir versão | botão repete o verbo e o objeto | — |
| LyricsText.qml:111 | downloadedTitle | Letra do áudio baixado | Letra da faixa baixada | glossário: faixa | — |
| LyricsText.qml:116 | lookupPrivacy | A busca no LRCLIB envia título e artista, quando informado. | A busca no LRCLIB envia o título e, se houver, o artista. | “quando informado” fica solto | — |
| LyricsText.qml:117 | otherMatches | Ver outras correspondências | Ver outros resultados | “correspondências” é formal | — |
| LyricsText.qml:119 | backToDirect | Voltar à busca direta | Voltar ao resultado encontrado | “busca direta” é termo interno; a tela mostra “Versão encontrada” | — |
| LyricsText.qml:128 | versionKind | Forma da versão | Tipo de versão | o outro diálogo usa “Tipo de versão” (linha 82) | — |
| LyricsText.qml:129 | selectKind | Escolha a forma | Escolha o tipo | idem | — |
| LyricsText.qml:130 | stagedVersions | A guardar: %1 | Prontas para salvar: %1 | glossário: salvar (não “guardar”) | — |
| LyricsText.qml:133 | retryKeep | Tentar guardar novamente | Tentar salvar novamente | glossário: salvar | — |
| LyricsText.qml:134 | discardAttempt | Descartar tentativa | Descartar alterações | mesma ação do diálogo de versões | ui/quick/tst_download_lyrics.qml:488 |
| LyricsText.qml:136 | keepVersions | Guardar versões | Salvar versões | glossário: salvar | — |
| LyricsText.qml:137 | keepAsNew | Guardar como nova versão | Salvar como nova versão | igual à linha 96 | ui/quick/tst_download_lyrics.qml:726 |
| LyricsText.qml:138 | discardAttemptTitle | Descartar tentativa de guardar letra | Descartar alterações? | igual à 139; uma das duas pode sair | — |
| LyricsText.qml:139 | confirmDiscardAttempt | Descartar tentativa de guardar letra? | Descartar alterações? | título em pergunta, sem “tentativa de guardar” | — |
| LyricsText.qml:140 | discardAttemptAdvice | O áudio e as versões já guardadas serão preservados. Você pode continuar esta revisão. | O áudio e as versões já salvas continuam. Você pode seguir revisando. | glossário: salvar | — |

### RoomText.qml (20)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| RoomText.qml:4 | unavailable | Sala indisponível neste build | Ouvir junto não está disponível nesta versão | “build” é jargão | ui/quick/tst_listening_room.qml:197 |
| RoomText.qml:5 | unavailableHint | Esta versão do RC Music foi gerada sem o servidor de apresentação da sala. | Esta versão do RC Music não inclui o servidor que conecta as salas. | “servidor de apresentação” é termo interno | — |
| RoomText.qml:10 | privacyNotice | Como em chamadas de voz, os participantes de uma sala ficam sabendo o endereço IP uns dos outros. Só convide quem você conhece. | Como em chamadas de voz, quem está na mesma sala consegue ver o endereço IP dos outros. Só convide quem você conhece. | glossário: pessoas, não “participantes” | — |
| RoomText.qml:16 | untilAppCloses | Termina quando eu fechar o app | Até você fechar o RC Music | fala com “você”; “app” vira RC Music | ui/quick/tst_listening_room.qml:213 |
| RoomText.qml:17 | untilRevoked | Vale até eu revogar | Até você cancelar o convite | “revogar” é formal; o convidado já lê “cancelou este convite” | ui/quick/tst_listening_room.qml:214 |
| RoomText.qml:27 | hostCapacity | Até %1 pessoas além de você | Até %n pessoas além de você (numerus; 1: “Até 1 pessoa além de você”) | plural: hoje sai “Até 1 pessoas” | — |
| RoomText.qml:38 | revoke | Revogar convite | Cancelar convite | mesmo verbo da mensagem ao convidado | — |
| RoomText.qml:68 | chooseAnotherInitialRole | Escolha outro cargo inicial antes | Para excluir, escolha outro cargo inicial | diz para que serve o pedido | ui/quick/tst_listening_room_roles.qml:372 |
| RoomText.qml:69 | rosterUnsaved | Não deu para salvar cargos, ajustes e bloqueios. Eles valem só até a sala fechar. | Não foi possível salvar os cargos, ajustes e bloqueios. Eles valem só até a sala fechar. | “Não deu” destoa do resto | ui/quick/tst_listening_room_roles.qml:527 |
| RoomText.qml:71 | roleHolders | %1 pessoas com este cargo | %n pessoas com este cargo (numerus; 1: “1 pessoa com este cargo”) | plural: hoje sai “1 pessoas” | — |
| RoomText.qml:75 | permissionAddFromRoom | Adicionar da sala | Adicionar faixas das playlists | “da sala” não diz de onde vem a faixa | ui/quick/tst_listening_room_roles.qml:333 |
| RoomText.qml:76 | permissionAddPlaylist | Adicionar playlist | Adicionar playlists inteiras | diferencia da permissão anterior | ui/quick/tst_listening_room_roles.qml:333 |
| RoomText.qml:78 | permissionUploadOwnTracks | Enviar músicas próprias | Enviar faixas do computador | glossário: faixa; diz a origem | ui/quick/tst_listening_room_roles.qml:333 |
| RoomText.qml:92 | overrideName | %1 para %2 | %1 (%2) | “Pular para Ana” lê como “pular até Ana”; vira “Pular (Ana)” | — |
| RoomText.qml:98 | catalogTitle | Playlists do host | Playlists do anfitrião | glossário: anfitrião, não host | ui/quick/tst_listening_room.qml:584 |
| RoomText.qml:99 | catalogNoPermission | Para pôr músicas destas playlists na fila, peça permissão ao anfitrião. | Para adicionar faixas destas playlists à fila, peça permissão ao anfitrião. | glossário: faixa; “pôr” vira “adicionar” | — |
| RoomText.qml:103 | addAllName | Adicionar todas as músicas de %1 | Adicionar todas as faixas de %1 | glossário: faixa | ~ui/quick/tst_listening_room.qml:607 |
| RoomText.qml:105 | uploadOwn | Adicionar música minha | Adicionar do computador | par com “Adicionar do YouTube”; diz a origem | core/ListeningRoomGuestTests.cpp:1414; qt/ListeningRoomBindingTests.cpp:87; ui/quick/tst_listening_room.qml:639 |
| RoomText.qml:106 | uploadOwnName | Adicionar uma música sua à fila da sala | Adicionar uma faixa do seu computador à fila da sala | idem | ui/quick/tst_listening_room.qml:651 |
| RoomText.qml:107 | uploading | Enviando sua música… | Enviando sua faixa… | glossário: faixa | ui/quick/tst_listening_room.qml:655 |

### PreferencesText.qml (41)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| PreferencesText.qml:5 | closeBehavior | Ao fechar | Ao fechar a janela | diz o que é fechado | ui/quick/tst_preferences.qml:355 |
| PreferencesText.qml:6 | minimizeToTray | Minimizar para bandeja | Continuar na bandeja | mesmo nome do aviso da bandeja | — |
| PreferencesText.qml:7 | exitApp | Encerrar | Sair do RC Music | o menu da bandeja usa “Sair” | — |
| PreferencesText.qml:10 | trayRequired | Para ativar, é necessária uma bandeja funcional. | Precisa da bandeja do sistema, que não está disponível agora. | diz o motivo sem passiva | — |
| PreferencesText.qml:15 | openDefaultAppsSettings | Abrir Aplicativos Padrão | Abrir Aplicativos padrão do Windows | nome do Windows 11 e de onde ele é | ui/quick/tst_preferences.qml:358 |
| PreferencesText.qml:16 | openDefaultAppsSettingsDescription | Abre as configurações oficiais do Windows em outra janela. | Abre as Configurações do Windows em outra janela. | “oficiais” sobra | ui/quick/tst_preferences.qml:361 |
| PreferencesText.qml:17 | defaultAppsSettingsOpening | Abrindo as configurações do Windows. | Abrindo as Configurações do Windows… | reticências em ação em andamento | — |
| PreferencesText.qml:18 | defaultAppsSettingsUnavailable | As configurações de Aplicativos Padrão do Windows não estão disponíveis. | A página Aplicativos padrão do Windows não está disponível. | grafia do Windows | ui/quick/tst_preferences.qml:418 |
| PreferencesText.qml:19 | defaultAppsSettingsFailed | Não foi possível abrir Aplicativos Padrão. Abra Configurações > Aplicativos > Aplicativos Padrão. | Não foi possível abrir Aplicativos padrão. No Windows, abra Configurações > Aplicativos > Aplicativos padrão. | distingue das Configurações do RC Music | ui/quick/tst_preferences.qml:396 |
| PreferencesText.qml:30 | noAudioOutputs | Nenhum dispositivo de saída foi encontrado. | Nenhuma saída de áudio encontrada. | glossário: saída de áudio | — |
| PreferencesText.qml:37 | highContrastActive | O alto contraste do sistema está ativo. O tema escolhido será usado quando ele for desativado. | O alto contraste do Windows está ligado. O tema escolhido volta quando você desligá-lo. | voz ativa | ui/quick/tst_high_contrast.qml:180 |
| PreferencesText.qml:38 | memoryNote | As preferências são salvas localmente. Quando a restauração da última sessão está ativa, ela é retomada pausada; iniciar minimizado mantém o aplicativo na bandeja quando ela está disponível. | As configurações ficam salvas neste computador. Ao restaurar a última sessão, a faixa volta pausada. Ao iniciar minimizado, o RC Music abre direto na bandeja, se ela estiver disponível. | glossário: configurações; frases curtas | ui/quick/tst_preferences.qml:453 |
| PreferencesText.qml:39 | recoveredTitle | Preferências recuperadas | Configurações recuperadas | glossário: a página se chama Configurações | ui/quick/tst_preference_persistence_notice.qml:99 |
| PreferencesText.qml:40 | recoveredMessage | Uma cópia de segurança válida foi recuperada. Revise as preferências antes de continuar, se desejar. | As configurações foram recuperadas de uma cópia de segurança. Se quiser, confira se está tudo certo. | idem; mais direto | — |
| PreferencesText.qml:41 | defaultsTitle | Preferências padrão restauradas | Configurações voltaram ao padrão | idem | ui/quick/tst_preference_persistence_notice.qml:104 |
| PreferencesText.qml:42 | defaultsMessage | As preferências salvas eram inválidas. Valores padrão seguros estão em uso. | Não foi possível ler as configurações salvas. O RC Music está usando os valores padrão. | “inválidas” culpa o arquivo sem explicar | — |
| PreferencesText.qml:43 | futureVersionTitle | Preferências de uma versão mais recente | Configurações de uma versão mais nova | glossário | — |
| PreferencesText.qml:44 | futureVersionMessage | As alterações não podem ser salvas sem substituir as preferências criadas por uma versão mais recente do aplicativo. | Estas configurações foram salvas por uma versão mais nova do RC Music. Para salvar as suas mudanças, é preciso substituí-las. | ordem: o que houve, o que fazer | — |
| PreferencesText.qml:45 | saveFailedTitle | Não foi possível salvar as preferências | Não foi possível salvar as configurações | glossário | — |
| PreferencesText.qml:46 | saveFailedMessage | Suas alterações continuam nesta sessão, mas ainda não foram salvas. | Suas mudanças valem até você fechar o RC Music, mas ainda não foram salvas. | “nesta sessão” é ambíguo | — |
| PreferencesText.qml:48 | sessionRecoveredMessage | Uma cópia de segurança válida da fila e da posição foi recuperada. Confira a fila antes de retomar a reprodução. | A fila e a posição foram recuperadas de uma cópia de segurança. Confira a fila antes de tocar. | voz ativa, mais curta | ui/quick/tst_preference_persistence_notice.qml:149 |
| PreferencesText.qml:50 | sessionDefaultsMessage | Os dados salvos da fila e da posição eram inválidos. O player iniciou com uma sessão vazia e sem reproduzir áudio. | Não foi possível ler a fila e a posição salvas. O RC Music abriu com a fila vazia e sem tocar nada. | “player”/“sessão vazia” viram linguagem comum | ui/quick/tst_preference_persistence_notice.qml:155 |
| PreferencesText.qml:51 | sessionFutureVersionTitle | Sessão de uma versão mais recente | Sessão de uma versão mais nova | glossário: “mais nova” | ui/quick/tst_preference_persistence_notice.qml:194 |
| PreferencesText.qml:52 | sessionFutureVersionMessage | A sessão não pode ser salva sem substituir a fila e a posição criadas por uma versão mais recente do aplicativo. | A fila e a posição foram salvas por uma versão mais nova do RC Music. Para salvar as atuais, é preciso substituí-las. | ordem: o que houve, o que fazer | — |
| PreferencesText.qml:54 | sessionSaveFailedMessage | A fila e a posição continuam nesta execução, mas ainda não foram salvas. | A fila e a posição valem até você fechar o RC Music, mas ainda não foram salvas. | “execução” é jargão | — |
| PreferencesText.qml:55 | combinedWarningTitle | Preferências e sessão exigem atenção | Configurações e sessão precisam de atenção | glossário; “exigem” é duro | ui/quick/tst_preference_persistence_notice.qml:223 |
| PreferencesText.qml:56 | combinedRecoveredMessage | Preferências e sessão foram recuperadas de cópias de segurança válidas. Revise as preferências e confira a fila antes de continuar. | As configurações e a sessão foram recuperadas de cópias de segurança. Confira as configurações e a fila antes de continuar. | glossário | ui/quick/tst_preference_persistence_notice.qml:224 |
| PreferencesText.qml:57 | combinedDefaultsMessage | As preferências e a sessão salvas eram inválidas. Valores padrão seguros e uma sessão vazia estão em uso; nenhum áudio foi iniciado automaticamente. | Não foi possível ler as configurações e a sessão salvas. O RC Music está usando os valores padrão, com a fila vazia e sem tocar nada. | idem linhas 42 e 50 | — |
| PreferencesText.qml:58 | combinedFutureVersionMessage | Preferências e sessão foram criadas por uma versão mais recente. Os dados locais não podem ser salvos sem substituí-los. | As configurações e a sessão foram salvas por uma versão mais nova do RC Music. Para salvar as atuais, é preciso substituí-las. | “dados locais” é vago | — |
| PreferencesText.qml:59 | combinedSaveFailedMessage | Alterações nas preferências e na sessão continuam nesta execução, mas ainda não foram salvas. | As mudanças nas configurações e na sessão valem até você fechar o RC Music, mas ainda não foram salvas. | “execução” é jargão | — |
| PreferencesText.qml:60 | reviewPreferences | Revisar preferências | Abrir configurações | diz o que o botão faz | ui/quick/tst_preference_persistence_notice.qml:129 |
| PreferencesText.qml:61 | dismissWarning | Dispensar aviso | Fechar aviso | a sala usa “Fechar aviso” para o mesmo gesto | — |
| PreferencesText.qml:62 | retrySave | Tentar novamente | Tentar salvar novamente | diz o que será tentado | ui/fixtures/fixture.v1.json:53; ui/qtest/FixtureRepositoryTests.cpp:346; ui/qtest/ShellUiContractTests.cpp:312; ui/quick/tst_accessibility_announcements.qml:299 (+3) |
| PreferencesText.qml:63 | replacePreferences | Substituir preferências | Substituir configurações | glossário | ui/quick/tst_preference_persistence_notice.qml:236 |
| PreferencesText.qml:65 | replaceLocalData | Substituir dados locais | Substituir configurações e sessão | nomeia o que será substituído | ui/quick/tst_preference_persistence_notice.qml:210 |
| PreferencesText.qml:66 | replaceConfirmationTitle | Substituir preferências mais recentes? | Substituir as configurações mais novas? | glossário | — |
| PreferencesText.qml:67 | replaceConfirmationMessage | Esta ação substitui preferências criadas por uma versão mais recente do aplicativo e não pode ser desfeita. | As configurações salvas pela versão mais nova do RC Music serão trocadas pelas atuais. Isso não pode ser desfeito. | consequência concreta | — |
| PreferencesText.qml:68 | replaceSessionConfirmationTitle | Substituir sessão mais recente? | Substituir a sessão mais nova? | glossário: “mais nova” | ui/quick/tst_preference_persistence_notice.qml:203 |
| PreferencesText.qml:69 | replaceSessionConfirmationMessage | Esta ação substitui a fila e a posição salvas por uma versão mais recente do aplicativo e não pode ser desfeita. | A fila e a posição salvas pela versão mais nova do RC Music serão trocadas pelas atuais. Isso não pode ser desfeito. | consequência concreta | ui/quick/tst_preference_persistence_notice.qml:204 |
| PreferencesText.qml:70 | replaceLocalDataConfirmationTitle | Substituir dados locais mais recentes? | Substituir configurações e sessão mais novas? | nomeia o que será substituído | — |
| PreferencesText.qml:71 | replaceLocalDataConfirmationMessage | Esta ação substitui as preferências e a sessão (fila e posição) salvas por uma versão mais recente do aplicativo e não pode ser desfeita. | As configurações, a fila e a posição salvas pela versão mais nova do RC Music serão trocadas pelas atuais. Isso não pode ser desfeito. | consequência concreta | ui/quick/tst_preference_persistence_notice.qml:217 |

### TrackDetailsText.qml (12)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| TrackDetailsText.qml:20 | modifiedUtc | Modificação (UTC) | Modificado em (UTC) | rótulo de data mais natural | — |
| TrackDetailsText.qml:31 | noDiagnostics | Nenhum diagnóstico. | Nenhum problema encontrado. | “diagnóstico” como contagem soa estranho | ui/quick/tst_track_details.qml:119 |
| TrackDetailsText.qml:39 | metadataSourceUnavailable | O arquivo não estava disponível para leitura de metadados. | Não foi possível abrir o arquivo para ler as informações da faixa. | “metadados” é jargão; voz ativa | — |
| TrackDetailsText.qml:40 | metadataUnsupportedFormat | O formato não é suportado pelo leitor de metadados. | O RC Music não lê as informações deste formato de arquivo. | idem | — |
| TrackDetailsText.qml:41 | metadataInvalidData | Não foi possível ler os metadados deste arquivo. | As informações deste arquivo estão danificadas. | hoje é quase igual à linha 42 | ui/quick/tst_track_details.qml:274 |
| TrackDetailsText.qml:42 | metadataReadFailed | Falha ao ler os metadados deste arquivo. | Não foi possível ler as informações deste arquivo. | “Falha ao” destoa do padrão “Não foi possível” | — |
| TrackDetailsText.qml:47 | artworkEncodedBytesExceeded | A capa foi descartada porque possui {bytes} bytes codificados, acima do limite. | A capa não foi carregada porque é grande demais ({bytes} bytes). | “descartada” sugere que o arquivo foi alterado | — |
| TrackDetailsText.qml:48 | artworkAxisPixelsExceeded | A capa foi descartada porque suas dimensões são {width} × {height} px. | A capa não foi carregada porque é grande demais ({width} × {height} px). | idem | — |
| TrackDetailsText.qml:49 | artworkPixelCountExceeded | A capa foi descartada porque sua quantidade total de pixels excede o limite. | A capa não foi carregada porque tem pixels demais. | idem | — |
| TrackDetailsText.qml:50 | artworkInvalidData | A capa foi descartada porque os dados são inválidos. | A capa não foi carregada porque está danificada. | idem | — |
| TrackDetailsText.qml:51 | metadataReadCancelled | A leitura de metadados foi cancelada. | A leitura das informações foi cancelada. | “metadados” é jargão | — |
| TrackDetailsText.qml:52 | metadataReadExpired | A leitura de metadados excedeu o tempo disponível. | A leitura das informações demorou demais. | idem | — |

### DownloadText.qml (17)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| DownloadText.qml:4 | archive | Arquivar pedidos encerrados | Arquivar encerrados | redesenho; “pedidos” é termo interno. A confirmação ganha título próprio (seção 4) | — |
| DownloadText.qml:5 | archiveAdvice | Os pedidos encerrados disponíveis para arquivamento sairão do histórico após salvar. Os MP3, as letras salvas e a fila de reprodução serão preservados. O mesmo pedido continua registrado para evitar downloads duplicados. | Os downloads encerrados saem da lista. Os MP3, as letras salvas e a fila continuam como estão. O RC Music continua lembrando desses pedidos para não baixar o mesmo novamente. | voz ativa; “fila de reprodução” vira “fila” | — |
| DownloadText.qml:10 | closeWithoutSaveAdvice | O histórico de downloads não foi salvo. Sair agora pode perder os últimos estados. Os arquivos não serão removidos. | O histórico de downloads não foi salvo, e as últimas mudanças podem se perder. Nenhum arquivo será apagado. | “últimos estados” é técnico | — |
| DownloadText.qml:11 | empty | Nenhum download nesta sessão. | Nenhum download ainda. | o histórico sobrevive entre aberturas; “nesta sessão” engana (confirmar) | — |
| DownloadText.qml:15 | emptyQueued | Nenhum download na fila. | Nenhum download aguardando. | acompanha o filtro “Aguardando” | ui/quick/tst_downloads.qml:528 |
| DownloadText.qml:21 | filterQueued | Na fila | Aguardando | “Na fila” confunde com a fila de reprodução | qt/DownloadJobsModelTests.cpp:851; ui/quick/tst_downloads.qml:492 |
| DownloadText.qml:29 | discardPartialsAdvice | Após nova verificação, o RC Music poderá remover os arquivos temporários ainda presentes deste download. O MP3 final, se existir, será preservado. Quando todos os itens estiverem encerrados, você poderá arquivar o pedido. | O RC Music confere os arquivos novamente e pode apagar os temporários deste download que ainda existirem. O MP3 final, se existir, continua. Depois de encerrar todos os itens, você pode arquivar o download. | voz ativa; “pedido” vira download | — |
| DownloadText.qml:30 | discardWithoutFilesAdvice | A verificação confirmou a ausência dos arquivos locais esperados. O RC Music fará uma nova verificação antes de registrar o encerramento. Depois de encerrar todos os itens, você poderá arquivar o pedido. | A verificação não encontrou os arquivos deste download. O RC Music confere mais uma vez antes de encerrar. Depois de encerrar todos os itens, você pode arquivar o download. | idem | ui/quick/tst_downloads.qml:677 |
| DownloadText.qml:32 | reviewingFiles | Verificando arquivos | Verificando arquivos… | reticências em ação em andamento | — |
| DownloadText.qml:33 | reviewFilesAgain | Reverificar arquivos | Verificar novamente | “Reverificar” não é usual | ui/qtest/DownloadRecoveryReviewCompositionTests.cpp:398; ui/quick/tst_downloads.qml:607 |
| DownloadText.qml:37 | importRecoveredAdvice | Adicionar este áudio validado à fila? A inclusão aguardará a confirmação de gravação e não iniciará a reprodução. O arquivo será preservado. | O MP3 validado vai para a fila, sem começar a tocar. O arquivo continua onde está. | tira “confirmação de gravação”; a pergunta vai para o título (seção 4) | — |
| DownloadText.qml:38 | retryAdmission | Tentar salvar o pedido novamente | Tentar salvar o download novamente | “pedido” é interno | — |
| DownloadText.qml:39 | retryArchive | Tentar salvar o arquivamento novamente | Tentar arquivar novamente | mais curto | — |
| DownloadText.qml:41 | retryImport | Tentar importar novamente | Tentar adicionar à fila novamente | “importar” é interno | — |
| DownloadText.qml:47 | closeInformation | Voltar aos downloads | Fechar | o popup só fecha; a página se chama Atividade | — |
| DownloadText.qml:54 | inQueue | Na fila de reprodução | Na fila | glossário: “fila” já é a de reprodução | — |
| DownloadText.qml:56 | libraryPending | Biblioteca pendente | Ainda não está na biblioteca | “Biblioteca pendente” não diz o estado | — |

### QueueText.qml (20)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| QueueText.qml:4 | drawerEyebrow | REPRODUÇÃO / TEMPORÁRIA | A SEGUIR (ou remover no redesenho) | “TEMPORÁRIA” confunde; a gaveta ganha título e fechar | — |
| QueueText.qml:5 | manageLyrics | Gerenciar letras | Versões da letra… | mesmo nome do diálogo que abre | — |
| QueueText.qml:6 | add | Adicionar arquivos | Adicionar arquivos… | abre seletor; Playlists já usa reticências | ui/quick/tst_empty_shell.qml:513 |
| QueueText.qml:9 | remove | Remover | Remover da fila | verbo + objeto | ui/quick/tst_empty_shell.qml:513 |
| QueueText.qml:12 | shuffleOn | Aleatório: Ligado | Aleatório: ligado | minúscula após dois-pontos | — |
| QueueText.qml:13 | shuffleOff | Aleatório: Desligado | Aleatório: desligado | idem | — |
| QueueText.qml:15 | repeatOn | Repetir: Ligada | Repetir: ligado | concordância com o aleatório | — |
| QueueText.qml:16 | repeatOff | Repetir: Desligada | Repetir: desligado | idem | — |
| QueueText.qml:21 | retry | Tentar reproduzir novamente | Tentar tocar novamente | glossário: tocar | ui/quick/tst_error_microcopy.qml:328; ui/quick/tst_queue_microcopy.qml:100 |
| QueueText.qml:22 | retryDescription | Tenta abrir o mesmo arquivo novamente, preservando sua posição na fila. | Abre o mesmo arquivo novamente, sem mudar a fila. | mais curta | — |
| QueueText.qml:23 | retrying | Tentando reproduzir novamente… | Tentando tocar novamente… | acompanha a linha 21 | ui/quick/tst_queue_microcopy.qml:123 |
| QueueText.qml:25 | locateDescription | Atualiza o local desta ocorrência sem interromper a faixa em reprodução. | Indica onde o arquivo está agora, sem parar o que está tocando. | “ocorrência” é jargão | — |
| QueueText.qml:28 | sourceRelocated | Novo arquivo selecionado. Tente reproduzir para verificar. | Arquivo localizado. Tente tocar a faixa para conferir. | glossário: tocar | ui/quick/tst_file_location.qml:219 |
| QueueText.qml:29 | clearTitle | Limpar fila? | Limpar a fila? | título em pergunta com artigo, como as outras confirmações | ui/quick/tst_local_queue_crud.qml:238 |
| QueueText.qml:30 | clearOneMessage | O item da fila será removido. Esta ação não pode ser desfeita. | A faixa sai da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito. | regra 4: diz o que é preservado | — |
| QueueText.qml:31 | clearManyMessage | Os {count} itens da fila serão removidos. Esta ação não pode ser desfeita. | As {count} faixas saem da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito. | idem; “itens” vira “faixas” | ~ui/quick/tst_local_queue_crud.qml:239 |
| QueueText.qml:35 | sourceUnavailable | Arquivo não disponível. | Arquivo não encontrado. | mesmo texto do título de erro | ui/quick/tst_empty_shell.qml:305 |
| QueueText.qml:38 | accessDenied | Acesso ao arquivo negado. | Sem permissão para abrir o arquivo. | linguagem comum | — |
| QueueText.qml:44 | hostControls | O host controla | O anfitrião controla | glossário: anfitrião | ui/quick/tst_listening_room.qml:316; ui/quick/tst_listening_room_roles.qml:556 |
| QueueText.qml:45 | hostNotAllowed | O host não liberou | O anfitrião não liberou | glossário: anfitrião | ui/quick/tst_listening_room_roles.qml:490 |

### PlaylistsText.qml (18)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| PlaylistsText.qml:8 | saveQueue | Salvar fila como… | Salvar fila como playlist… | nomeia o resultado | — |
| PlaylistsText.qml:10 | trackCount | %1 faixas | %n faixas (numerus; 1: “1 faixa”) | plural: hoje sai “1 faixas” | — |
| PlaylistsText.qml:11 | empty | Suas playlists aparecerão aqui. | Você ainda não tem playlists. Use Nova playlist ou salve a fila como playlist. | estado vazio diz o que fazer | — |
| PlaylistsText.qml:18 | emptyPlaylist | Esta playlist está vazia. | Esta playlist está vazia. Adicione arquivos ou as faixas da fila. | idem | — |
| PlaylistsText.qml:21 | remove | Remover | Remover da playlist | redesenho: item do menu da faixa | ui/quick/tst_empty_shell.qml:513 |
| PlaylistsText.qml:25 | addCurrentQueue | Adicionar fila atual | Adicionar faixas da fila | “Adicionar fila atual” é ambíguo | — |
| PlaylistsText.qml:27 | retrySave | Tentar salvar | Tentar salvar novamente | mesmo rótulo das outras ações de repetir | — |
| PlaylistsText.qml:29 | discardEdit | Descartar edição… | Descartar alterações… | um verbo e um substantivo para o app todo | — |
| PlaylistsText.qml:30 | retryLoad | Tentar abrir novamente | Tentar carregar novamente | a Biblioteca já usa este texto | — |
| PlaylistsText.qml:33 | selectionChanged | A seleção mudou. Cancele e escolha a playlist novamente. | A playlist selecionada mudou. Cancele e escolha a playlist novamente. | diz qual seleção | — |
| PlaylistsText.qml:34 | editRejected | Não foi possível aceitar esta alteração. Confira o nome e o estado das playlists. | Não foi possível fazer essa alteração. Confira o nome e tente novamente. | “estado das playlists” é vago | — |
| PlaylistsText.qml:38 | confirmDelete | Excluir playlist? | Excluir “%1”? | título nomeia a playlist (novo %1) | — |
| PlaylistsText.qml:39 | deleteAdvice | Somente a lista será excluída. Os arquivos de áudio e a fila atual serão mantidos. | Só a playlist é excluída. Os arquivos, a biblioteca e a fila continuam como estão. | glossário: playlist, não “lista” | — |
| PlaylistsText.qml:40 | deleteRejected | Não foi possível aceitar a exclusão. Confira o estado das playlists. | Não foi possível excluir a playlist agora. Tente novamente. | “aceitar a exclusão” é interno | — |
| PlaylistsText.qml:41 | discardTitle | Descartar edição pendente | Descartar alterações? | igual à 42; uma das duas pode sair | — |
| PlaylistsText.qml:42 | confirmDiscard | Descartar edição pendente? | Descartar alterações? | glossário: alterações | — |
| PlaylistsText.qml:43 | discardAdvice | A edição em memória será descartada. Uma tentativa de salvamento anterior pode já ter gravado a edição no disco. | As alterações não salvas serão descartadas. Se uma tentativa anterior já tiver gravado parte delas, essa parte continua salva. | tira “em memória” | — |
| PlaylistsText.qml:44 | confirmDiscardAction | Descartar edição | Descartar alterações | botão repete o verbo | — |

### NavigationText.qml (6)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| NavigationText.qml:6 | pageDescription | Sua reprodução e suas listas, no mesmo lugar. | O que está tocando, sua fila e sua biblioteca. | redesenho da Início; “listas” é vago | — |
| NavigationText.qml:10 | localAudio | Áudio local | Artista desconhecido | a Biblioteca usa isso para o mesmo caso | — |
| NavigationText.qml:11 | playTrack | Reproduzir %1 | Tocar %1 | glossário: tocar | ~qt/QtShellPlatformTests.cpp:31; ~ui/fixtures/fixture.v1.json:27; ~ui/qtest/FixtureRepositoryTests.cpp:320; ~ui/qtest/ShellPlatformBindingTests.cpp:37 (+3) |
| NavigationText.qml:27 | collectionHint | Ouça, organize e continue de onde parou. | (remover) | sem uso em nenhuma QML | — |
| NavigationText.qml:41 | settingsEyebrow | PREFERÊNCIAS DO APLICATIVO | AJUSTES DO RC MUSIC | evita “preferências” | — |
| NavigationText.qml:42 | settingsDescription | Aparência, reprodução, arquivos e integrações. | Aparência, reprodução, downloads e perfil. | acompanha as abas reais | — |

### LibraryText.qml (5)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| LibraryText.qml:5 | filterPlaceholder | Filtrar sua coleção… | Filtrar a biblioteca… | glossário: biblioteca, não “coleção” | — |
| LibraryText.qml:7 | countText | %1 de %2 faixas | %1 de %n faixas (numerus; 1: “1 de 1 faixa”) | plural | — |
| LibraryText.qml:14 | timeHeader | TEMPO | DURAÇÃO | Detalhes da faixa usa “Duração” | — |
| LibraryText.qml:27 | removeAdvice | Só o registro sai da biblioteca. O arquivo de áudio, a fila, as playlists e o que está tocando continuam como estão. | A faixa sai só da biblioteca. O arquivo, a fila, as playlists e o que está tocando continuam como estão. | “registro” é interno | — |
| LibraryText.qml:31 | emptyAdvice | Abra ou adicione arquivos, ou baixe uma música: ela fica guardada aqui mesmo depois que a fila muda. | Abra arquivos do computador ou baixe faixas em Buscar. Tudo fica guardado aqui, mesmo quando a fila muda. | diz onde baixar; glossário: faixa | — |

### ErrorText.qml (14)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| ErrorText.qml:5 | sourceUnavailableMessage | A faixa não está mais disponível neste local. | O arquivo não está mais no lugar de antes. Localize o arquivo ou abra outro áudio. | regra 3: diz o que fazer | — |
| ErrorText.qml:9 | invalidMediaMessage | O arquivo está corrompido ou usa um formato não reconhecido. | O arquivo pode estar corrompido ou em um formato que o RC Music não reconhece. Abra outro áudio. | regra 3: diz o que fazer | ui/quick/tst_error_microcopy.qml:113 |
| ErrorText.qml:10 | accessDeniedTitle | Acesso ao arquivo negado | Sem permissão para abrir o arquivo | linguagem comum | — |
| ErrorText.qml:12 | backendFailureTitle | A reprodução encontrou um problema | Não foi possível continuar tocando | padrão “Não foi possível” | — |
| ErrorText.qml:13 | backendFailureMessage | Escolha outro áudio para continuar. | Abra outro áudio para continuar. | mesmo verbo do botão “Abrir outro áudio” | — |
| ErrorText.qml:15 | audioOutputUnavailableMessage | Conecte uma saída de áudio ou selecione outra nas configurações de som do Windows. Depois, tente novamente. A faixa será recuperada pausada. | Conecte uma saída de áudio ou escolha outra nas configurações de som do Windows e tente novamente. A faixa volta pausada. | mais curta | ui/quick/tst_error_microcopy.qml:136 |
| ErrorText.qml:16 | audioOutputUnavailableInAppMessage | Reconecte a saída de áudio e tente novamente, ou escolha outra em Preferências > Reprodução. A faixa será recuperada pausada. | Reconecte a saída de áudio e tente novamente, ou escolha outra em Configurações > Reprodução. A faixa volta pausada. | a página se chama Configurações | — |
| ErrorText.qml:18 | audioOutputChangedMessage | A saída de áudio mudou e a reprodução foi interrompida. Tente novamente para recuperar esta faixa pausada. A posição será restaurada quando o arquivo permitir. | A reprodução parou. Tente novamente para retomar a faixa pausada, na mesma posição quando possível. | não repete o título | ui/quick/tst_error_microcopy.qml:138 |
| ErrorText.qml:19 | audioOutputChangedInAppMessage | A saída de áudio mudou e a reprodução foi interrompida. Tente novamente ou escolha outra saída em Preferências > Reprodução. A faixa será recuperada pausada. A posição será restaurada quando o arquivo permitir. | A reprodução parou. Tente novamente ou escolha outra saída em Configurações > Reprodução. A faixa volta pausada, na mesma posição quando possível. | idem; Configurações | — |
| ErrorText.qml:21 | unknownErrorMessage | Escolha outro áudio para continuar. | Abra outro áudio para continuar. | mesmo verbo do botão | — |
| ErrorText.qml:23 | retryAction | Tentar reproduzir novamente | Tentar tocar novamente | glossário: tocar | ui/quick/tst_error_microcopy.qml:328; ui/quick/tst_queue_microcopy.qml:100 |
| ErrorText.qml:24 | retryDescription | Tenta abrir o mesmo arquivo novamente, preservando sua posição na fila. | Abre o mesmo arquivo novamente, sem mudar a fila. | igual à fila | — |
| ErrorText.qml:26 | locateDescription | Localiza o arquivo e recupera esta faixa pausada, preservando sua posição na fila. A posição no áudio será restaurada quando o arquivo permitir. | Escolha onde o arquivo está agora. A faixa volta pausada, no mesmo lugar da fila e na mesma posição quando possível. | mais curta | — |
| ErrorText.qml:29 | recoverOutputDescription | Reconecta a saída de áudio e recupera esta faixa pausada, preservando a fila. A posição será restaurada quando o arquivo permitir. | Abre a saída de áudio novamente e retoma esta faixa pausada, sem mudar a fila. A posição volta quando possível. | “Reconecta” promete mexer no aparelho | — |

### TransportText.qml (3)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| TransportText.qml:15 | seekInactive | A posição não pode ser alterada no estado atual. | Não é possível mudar a posição agora. | “no estado atual” é técnico | ui/quick/tst_keyboard_full_journey.qml:119 |
| TransportText.qml:19 | lyricsAction | Letras | Letra | abre a gaveta “Letra”; “Letras” também é o site | extensions/youtube-popup.test.mjs:31; qt/DownloadLyricsBindingTests.cpp:324; qt/DownloadLyricsLookupTests.cpp:109; qt/LyricsBindingTests.cpp:1241 (+3) |
| TransportText.qml:20 | lyricsUnavailable | Esta faixa ainda não tem letra para ler. | Esta faixa ainda não tem letra. | mesmo texto do estado vazio do redesenho | — |

### SearchText.qml (3)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| SearchText.qml:16 | linksPlaceholder | Cole um link do YouTube por linha. Um link do Letras na mesma linha ou na linha seguinte leva a letra junto. | Cole um link do YouTube por linha. Para levar a letra junto, cole o link do Letras na mesma linha ou na de baixo. | objetivo antes da instrução | — |
| SearchText.qml:21 | requestedName | %1 já foi para os downloads | %1 já está na Atividade | a página se chama Atividade | — |
| SearchText.qml:23 | unavailable | A busca online fica disponível quando o componente de downloads do YouTube está ativo. | A busca no YouTube só funciona com o componente de downloads ativo. | mais curta | — |

### SourceImportText.qml (4)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| SourceImportText.qml:4 | selectingFiles | Aguardando seleção de arquivos… | Escolha os arquivos na janela que abriu… | diz o que fazer | ui/quick/tst_accessibility_announcements.qml:95; ui/quick/tst_audio_file_selection.qml:77 |
| SourceImportText.qml:5 | fileSelectionFailed | Não foi possível abrir o seletor. Tente abrir ou adicionar arquivos novamente. | Não foi possível abrir a janela de arquivos. Tente novamente. | “seletor” é técnico | ui/quick/tst_audio_file_selection.qml:106 |
| SourceImportText.qml:14 | completedWithIssuesAdvice | Alguns itens não eram arquivos de áudio locais válidos ou não puderam ser lidos. | Alguns itens não são áudios que o RC Music toca ou não puderam ser lidos. | mais simples | — |
| SourceImportText.qml:15 | rejectedAdvice | Escolha arquivos ou pastas locais disponíveis. | Escolha arquivos ou pastas deste computador. | “locais disponíveis” é técnico | — |

### ProfileText.qml (4)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| ProfileText.qml:5 | setupMessage | Escolha um apelido e um avatar. Eles aparecem para quem ouvir música junto com você. Dá para mudar depois em Configurações. | Escolha um apelido e um avatar. Eles aparecem para quem ouvir música junto com você. Dá para mudar depois em Configurações > Perfil. | diz a aba exata | — |
| ProfileText.qml:7 | nicknameInvalid | O apelido precisa ter de 1 a 32 caracteres, numa linha só, sem espaços repetidos nem caracteres especiais. | Use de 1 a 32 caracteres, sem espaços repetidos nem caracteres especiais. | começa pelo que fazer; “numa linha só” sobra num campo de uma linha | ui/quick/tst_profile.qml:11 |
| ProfileText.qml:11 | currentAvatar | Avatar atual | (remover) | sem uso em nenhuma QML | — |
| ProfileText.qml:21 | unavailable | O perfil não pode ser lido ou foi criado por uma versão mais recente do RC Music. Ele não será alterado nesta execução. | Não foi possível ler o seu perfil, ou ele foi criado por uma versão mais nova do RC Music. Ele não será alterado até você fechar o RC Music. | “nesta execução” é jargão | — |

### PlaybackAccessibilityText.qml (2)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| PlaybackAccessibilityText.qml:4 | loadingAudio | Carregando áudio. | Carregando a faixa. | glossário: faixa | — |
| PlaybackAccessibilityText.qml:6 | skippedDuringImport | Uma ou mais faixas foram puladas por estarem indisponíveis. Consulte os detalhes na fila. | Uma ou mais faixas foram puladas porque não estão disponíveis. Veja os detalhes na fila. | mais natural | — |

### BrowserText.qml (1)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| BrowserText.qml:5 | unavailable | Navegação indisponível | Não foi possível abrir o navegador | título diz o que aconteceu | — |

### LifecycleText.hpp (3)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| LifecycleText.hpp:33 | trayMessage | O player continuará na bandeja. Use Sair para encerrar. | O RC Music continua aberto na bandeja. Para fechar de vez, use Sair no menu da bandeja. | “player” vira RC Music; diz onde está o Sair | ui/quick/tst_lifecycle.qml:75 |
| LifecycleText.hpp:36 | trayNoticeEnabled | Avisar ao fechar para a bandeja nesta sessão | Continuar avisando até fechar o RC Music | “nesta sessão” é ambíguo | — |
| LifecycleText.hpp:39 | unavailableMessage | Não há bandeja disponível neste ambiente. Ao fechar, a janela é minimizada normalmente. | A bandeja do sistema não está disponível. Ao fechar, a janela só é minimizada. | “neste ambiente” é técnico | ui/quick/tst_lifecycle.qml:154 |

### ShellUiContract.cpp (1)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| ShellUiContract.cpp:194 | preferencesTitle | Preferências | Configurações | só no modo legado (fixture); alinha com a navegação | ui/fixtures/fixture.v1.json:40; ui/qtest/FixtureRepositoryTests.cpp:333; ui/qtest/ShellUiContractTests.cpp:313; ui/quick/tst_preferences.qml:318 (+2) |

### ApplicationText.hpp (8)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| ApplicationText.hpp:15 | roomUploadDialogTitle | Adicionar música minha à sala | Escolher faixa para a sala | glossário: faixa | — |
| ApplicationText.hpp:18 | localAudioDialogFilter | Áudio local (%1);;Todos os arquivos (*) | Arquivos de áudio (%1);;Todos os arquivos (*) | “Áudio local” é vago | — |
| ApplicationText.hpp:25 | invalidPaths | A solicitação contém um caminho inválido ou ambíguo. Use caminhos completos ou abra os arquivos pelo seletor do RC Music. | Um dos arquivos tem um caminho inválido. Use o caminho completo ou abra os arquivos pelo RC Music. | “solicitação” e “ambíguo” são técnicos | ui/qtest/ShellUiContractTests.cpp:900 |
| ApplicationText.hpp:30 | queueFull | O player está processando muitas solicitações. Tente novamente em instantes. | O RC Music está ocupado. Tente novamente em instantes. | “player” vira RC Music | ui/qtest/ShellUiContractTests.cpp:887 |
| ApplicationText.hpp:32 | temporarilyUnavailable | O player está concluindo uma operação. Tente novamente em instantes. | O RC Music está terminando uma tarefa. Tente novamente em instantes. | idem | ui/qtest/ShellUiContractTests.cpp:889 |
| ApplicationText.hpp:34 | shuttingDown | O player está sendo encerrado. Tente abrir os arquivos novamente. | O RC Music está fechando. Abra os arquivos novamente daqui a pouco. | idem | ui/qtest/ShellUiContractTests.cpp:891 |
| ApplicationText.hpp:36 | timedOut | O player já está aberto, mas não respondeu a tempo. | O RC Music já está aberto, mas não respondeu a tempo. Tente novamente. | diz o que fazer | ui/qtest/ShellUiContractTests.cpp:893 |
| ApplicationText.hpp:37 | fallback | Não foi possível encaminhar os arquivos para o player. | Não foi possível enviar os arquivos para o RC Music. | “encaminhar”/“player” | ui/qtest/ShellUiContractTests.cpp:895 |

### BrowserMediaPanel.cpp (8)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| BrowserMediaPanel.cpp:64 | scope | Página principal e componentes acessíveis | Procura na página e nos quadros que podem ser lidos | “componentes acessíveis” confunde com acessibilidade | qt/BrowserMediaPanelTests.cpp:120 |
| BrowserMediaPanel.cpp:68 | scope tooltip | Inclui frames acessíveis; frames de outra origem ou sandbox inacessíveis ficam de fora. | Quadros de outros sites ou protegidos ficam de fora. | “frames”/“sandbox” são jargão | — |
| BrowserMediaPanel.cpp:319 | hint | Pedido aceito na fila de downloads. Acompanhe no painel Downloads. | Pedido aceito. Acompanhe em Atividade. | a página se chama Atividade | — |
| BrowserMediaPanel.cpp:321 | hint | A fila de downloads está ocupada. Confira Downloads antes de tentar novamente. | Os downloads estão ocupados agora. Confira a Atividade antes de tentar novamente. | evita “fila” para downloads | — |
| BrowserMediaPanel.cpp:324 | hint | A fila de downloads recusou a seleção. Confira as fontes e selecione novamente. | Não foi possível aceitar a seleção. Confira as mídias e selecione novamente. | idem | — |
| BrowserMediaPanel.cpp:326 | hint | O pedido ultrapassou o limite da fila. Confira Downloads antes de tentar novamente. | O pedido passou do limite de downloads. Confira a Atividade antes de tentar novamente. | idem | — |
| BrowserMediaPanel.cpp:328 | hint | Não foi possível confirmar o pedido. Confira Downloads antes de tentar novamente. | Não foi possível confirmar o pedido. Confira a Atividade antes de tentar novamente. | idem | — |
| BrowserMediaPanel.cpp:371 | download tooltip | Conferir novamente e enviar a seleção à fila de downloads. | Conferir novamente e enviar a seleção para download. | evita “fila de downloads” | — |

### DownloadApplicationBinding.cpp (15)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| DownloadApplicationBinding.cpp:209 | directory_status | Escolha uma pasta local gravável no mesmo drive da pasta de downloads do aplicativo. | Escolha uma pasta deste computador em que o RC Music possa gravar, na mesma unidade da pasta de downloads padrão. | “gravável”/“drive” são técnicos | — |
| DownloadApplicationBinding.cpp:271 | directory_status | Escolha uma pasta local gravável no mesmo drive da pasta de downloads do aplicativo. | (igual à linha 209) | idem | — |
| DownloadApplicationBinding.cpp:1480 | warning | As vagas de importação continuam reservadas. Feche Downloads e selecione Tentar novamente no aviso da sessão. | As vagas para adicionar à fila continuam reservadas. Use Tentar salvar novamente no aviso sobre a sessão. | “Feche Downloads” não existe na Aurora; cita o rótulo novo de PreferencesText:62 | — |
| DownloadApplicationBinding.cpp:1490 | warning | Não foi possível confirmar a retomada. Selecione Tentar novamente. | Não foi possível confirmar a retomada. Tente novamente. | “Tentar novamente” não é o rótulo do botão que aparece | — |
| DownloadApplicationBinding.cpp:1493 | warning | A gravação da solicitação falhou. O download não será iniciado sem confirmação. | Não foi possível salvar o pedido. O download só começa depois que ele for salvo. | voz ativa | — |
| DownloadApplicationBinding.cpp:1502 | warning | Não foi possível confirmar o salvamento do arquivamento. O histórico exibido foi preservado. | Não foi possível confirmar o arquivamento. A lista continua como estava. | mais curta | — |
| DownloadApplicationBinding.cpp:1507 | warning | Reabra o RC Music para revisar a sessão da fila. Repetir a leitura de downloads não confirma essa sessão; os dados existentes foram preservados. | Feche e abra o RC Music novamente para conferir a fila. Tentar carregar o histórico não resolve isso. Nada foi apagado. | frases curtas | — |
| DownloadApplicationBinding.cpp:1510 | warning | Não foi possível reconciliar o histórico de downloads. Selecione Recuperar histórico para verificar novamente. Os dados e as confirmações de importação serão preservados. | Não foi possível conferir o histórico de downloads. Use Recuperar histórico para verificar novamente. Nada será apagado. | “reconciliar” é técnico | — |
| DownloadApplicationBinding.cpp:1513 | warning | O histórico de downloads diverge das confirmações de importação da fila. Os dados foram preservados. É necessária uma revisão desses históricos antes de liberar novos downloads. | O histórico de downloads não bate com o que entrou na fila. Nada foi apagado, mas novos downloads ficam bloqueados até esses históricos serem revisados. | “diverge das confirmações de importação” é técnico | — |
| DownloadApplicationBinding.cpp:1517 | warning | Não foi possível reconciliar o histórico. Os dados existentes foram preservados. | Não foi possível conferir o histórico. Nada foi apagado. | idem | — |
| DownloadApplicationBinding.cpp:1528 | status | Falha ao salvar o histórico final | Não foi possível salvar o histórico | padrão “Não foi possível” | — |
| DownloadApplicationBinding.cpp:1529 | warning | A gravação final não foi confirmada. Tente novamente ou encerre sem confirmar o histórico. | Tente salvar novamente ou saia sem salvar o histórico. | cita o botão real “Sair sem salvar o histórico” | — |
| DownloadApplicationBinding.cpp:1581 | warning | O histórico atingiu o limite de pedidos. Novos pedidos estão indisponíveis. | O histórico chegou ao limite de downloads. Arquive os encerrados para pedir novos. | diz o que fazer | — |
| DownloadApplicationBinding.cpp:1583 | warning | O histórico atingiu o limite de itens. Novos pedidos estão indisponíveis. | O histórico chegou ao limite de itens. Arquive os encerrados para pedir novos. | idem | — |
| DownloadApplicationBinding.cpp:1586 | warning | O histórico atingiu o limite de dados dos pedidos. Novos pedidos estão indisponíveis. | O histórico chegou ao limite de dados. Arquive os encerrados para pedir novos. | idem | — |

### OnlineSearchBinding.cpp (4)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| OnlineSearchBinding.cpp:387 | status | A fila de downloads está ocupada. Confira Atividade e tente de novo. | Os downloads estão ocupados agora. Confira a Atividade e tente novamente. | “fila” fica só para a reprodução | — |
| OnlineSearchBinding.cpp:389 | status | A fila de downloads está cheia. Confira Atividade antes de pedir outro. | Há downloads demais agora. Confira a Atividade antes de pedir outro. | idem | — |
| OnlineSearchBinding.cpp:392 | status | Os downloads recusaram esse vídeo. Escolha outro resultado. | Não é possível baixar esse vídeo. Escolha outro resultado. | “Os downloads recusaram” personifica | — |
| OnlineSearchBinding.cpp:394 | status | Não foi possível confirmar o pedido. Confira Atividade antes de tentar de novo. | Não foi possível confirmar o pedido. Confira a Atividade antes de tentar novamente. | artigo antes de Atividade | — |

### LibraryBinding.cpp (7)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| LibraryBinding.cpp:183 | notice | Não foi possível adicionar à biblioteca: limite atingido ou caminho inválido. | Não foi possível adicionar à biblioteca. Ela chegou ao limite ou o caminho do arquivo é inválido. | frase em vez de dois-pontos | — |
| LibraryBinding.cpp:283 | notice | Arquivo localizado. A faixa continua com o mesmo lugar na biblioteca. | Arquivo localizado. A faixa continua no mesmo lugar da biblioteca. | regência | — |
| LibraryBinding.cpp:295 | notice | Não foi possível abrir a escolha de arquivo. Tente novamente. | Não foi possível abrir a janela de arquivos. Tente novamente. | “escolha de arquivo” é estranho | — |
| LibraryBinding.cpp:306 | notice | A biblioteca foi salva por uma versão mais recente do RC Music. O arquivo foi preservado. | A biblioteca foi salva por uma versão mais nova do RC Music. Nada foi alterado. | glossário: “mais nova”, “Nada foi alterado” | — |
| LibraryBinding.cpp:308 | notice | Não foi possível carregar a biblioteca. Confira library.json e library.json.bak na pasta de configuração e tente novamente. | Não foi possível carregar a biblioteca. Nada foi alterado. Tente novamente. | regra 3: sem nomes de arquivo internos | — |
| LibraryBinding.cpp:316 | notice | Biblioteca recuperada do backup. | Biblioteca recuperada da cópia de segurança. | glossário: cópia de segurança | — |
| LibraryBinding.cpp:335 | notice | Uma versão mais recente da biblioteca foi encontrada. O arquivo foi preservado e as músicas novas não foram salvas. | A biblioteca foi salva por uma versão mais nova do RC Music. Ela não foi alterada, e as faixas novas não foram salvas. | glossário: faixa; ambiguidade de “versão” | — |

### PlaylistsBinding.cpp (11)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| PlaylistsBinding.cpp:78 | notice | Confira o nome, a seleção e os limites da playlist. | Não foi possível fazer essa alteração. Confira o nome e a playlist selecionada. | “limites” é vago | — |
| PlaylistsBinding.cpp:97 | notice | Playlists recuperadas do backup. | Playlists recuperadas da cópia de segurança. | glossário | — |
| PlaylistsBinding.cpp:101 | notice | O disco tem uma versão mais recente das playlists. A edição continua em memória. | As playlists no disco são de uma versão mais nova do RC Music. Suas alterações continuam abertas, sem salvar. | “versão mais recente das playlists” parece outra versão da playlist | — |
| PlaylistsBinding.cpp:103 | notice | Não foi possível recarregar as playlists. A edição continua em memória. | Não foi possível recarregar as playlists. Suas alterações continuam abertas, sem salvar. | “em memória” é jargão | — |
| PlaylistsBinding.cpp:109 | notice | As playlists foram salvas por uma versão mais recente. Os arquivos foram preservados. | As playlists foram salvas por uma versão mais nova do RC Music. Nada foi alterado. | glossário | — |
| PlaylistsBinding.cpp:111 | notice | Não foi possível carregar as playlists. Confira os arquivos playlists.json e playlists.json.bak na pasta de configuração e tente novamente. | Não foi possível carregar as playlists. Nada foi alterado. Tente novamente. | regra 3: sem nomes de arquivo internos | — |
| PlaylistsBinding.cpp:125 | notice | Uma versão mais recente das playlists foi encontrada. Os arquivos foram preservados; recarregue as listas do disco. | As playlists no disco são de uma versão mais nova do RC Music. Nada foi gravado. Recarregue do disco. | ambiguidade de “versão”; glossário: playlists | — |
| PlaylistsBinding.cpp:127 | notice | Não foi possível confirmar o salvamento. Tente salvar novamente ou recarregue as listas do disco. | Não foi possível confirmar se a playlist foi salva. Tente salvar novamente ou recarregue do disco. | “as listas” vira playlist | ui/quick/tst_playlists.qml:17 |
| PlaylistsBinding.cpp:231 | notice | Não foi possível selecionar o arquivo de substituição. Tente novamente. | Não foi possível abrir a janela de arquivos. Tente novamente. | “arquivo de substituição” é interno | — |
| PlaylistsBinding.cpp:345 | notice | Não foi possível copiar a fila. Tente novamente. | Não foi possível copiar as faixas da fila. Tente novamente. | diz o que foi copiado | — |
| PlaylistsBinding.cpp:492 | notice | Edição descartada. Recarregue as playlists para voltar a editar. | Alterações descartadas. Recarregue as playlists para editar novamente. | glossário: alterações | — |

### DownloadLyricsBinding.cpp (15)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| DownloadLyricsBinding.cpp:76 | searchError | Aguarde antes de buscar novamente no LRCLIB. | Muitas buscas seguidas no LRCLIB. Espere um pouco e tente novamente. | diz o motivo | — |
| DownloadLyricsBinding.cpp:78 | searchError | A busca no LRCLIB excedeu o tempo limite. | O LRCLIB demorou demais para responder. Tente novamente. | regra 3: o que fazer | — |
| DownloadLyricsBinding.cpp:80 | searchError | Não foi possível conectar ao LRCLIB. | Não foi possível conectar ao LRCLIB. Confira a internet e tente novamente. | igual à LyricsBinding:59 (hoje divergem) | — |
| DownloadLyricsBinding.cpp:82 | searchError | O LRCLIB não permitiu a consulta. | O LRCLIB recusou a busca. Tente novamente mais tarde. | regra 3 | — |
| DownloadLyricsBinding.cpp:84 | searchError | O LRCLIB enviou uma resposta inválida. | O LRCLIB respondeu de um jeito inesperado. Tente novamente. | regra 3; padrão da busca online | — |
| DownloadLyricsBinding.cpp:86 | searchError | A resposta do LRCLIB excedeu os limites. Tente uma busca mais específica. | A resposta do LRCLIB veio grande demais. Tente uma busca mais específica. | “excedeu os limites” é técnico | — |
| DownloadLyricsBinding.cpp:187 | notice | Confira o texto, o nome, a forma e os limites da versão antes de guardar. | Não foi possível salvar. Confira o texto, o nome e o tipo da versão; ela pode ter passado do tamanho permitido. | glossário: salvar, tipo | — |
| DownloadLyricsBinding.cpp:193 | notice | Confira o texto, o nome e a forma da versão %1 do conjunto. | Confira o texto, o nome e o tipo da versão %1. | “do conjunto” é interno; glossário: tipo | — |
| DownloadLyricsBinding.cpp:228 | statusText | Guardando letras… | Salvando letras… | glossário: salvar | — |
| DownloadLyricsBinding.cpp:231 | statusText | Letras guardadas. | Letras salvas. | idem | — |
| DownloadLyricsBinding.cpp:234 | statusText | O conteúdo já estava guardado. | Estas letras já estavam salvas. | idem | — |
| DownloadLyricsBinding.cpp:240 | statusText | O salvamento está pendente. Tente guardar novamente; o áudio foi preservado. | As letras ainda não foram salvas. Tente salvar novamente; o áudio não foi alterado. | idem | — |
| DownloadLyricsBinding.cpp:244 | statusText | A versão excede os limites do catálogo ou contém dados inválidos. | A versão é grande demais ou tem dados inválidos. | “catálogo” é interno | — |
| DownloadLyricsBinding.cpp:247 | statusText | Tentativa de guardar a letra descartada. | Alterações da letra descartadas. | glossário: descartar alterações | — |
| DownloadLyricsBinding.cpp:333 | label | texto | texto simples | mesmo nome do formato (“Texto simples”) | — |

### LyricsBinding.cpp (31)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| LyricsBinding.cpp:55 | searchError | Aguarde antes de buscar novamente no LRCLIB. | Muitas buscas seguidas no LRCLIB. Espere um pouco e tente novamente. | igual à DownloadLyricsBinding:76 | — |
| LyricsBinding.cpp:57 | searchError | A busca no LRCLIB excedeu o tempo limite. | O LRCLIB demorou demais para responder. Tente novamente. | idem | — |
| LyricsBinding.cpp:59 | searchError | Falha de conexão com o LRCLIB. Confira sua conexão e tente novamente. | Não foi possível conectar ao LRCLIB. Confira a internet e tente novamente. | idem; “Falha de” destoa | — |
| LyricsBinding.cpp:61 | searchError | O LRCLIB não permitiu a consulta. | O LRCLIB recusou a busca. Tente novamente mais tarde. | idem | — |
| LyricsBinding.cpp:63 | searchError | O LRCLIB enviou uma resposta inválida. | O LRCLIB respondeu de um jeito inesperado. Tente novamente. | idem | — |
| LyricsBinding.cpp:65 | searchError | A resposta do LRCLIB excedeu os limites. Tente uma busca mais específica. | A resposta do LRCLIB veio grande demais. Tente uma busca mais específica. | idem | — |
| LyricsBinding.cpp:354 | notice | Confira o nome, o texto e os limites da versão da letra. | Não foi possível salvar. Confira o nome e o texto da versão; ela pode ter passado do tamanho permitido. | “limites” é vago | — |
| LyricsBinding.cpp:431 | notice | Salvando a escolha de letra… | Salvando a letra escolhida… | “escolha de letra” é estranho | — |
| LyricsBinding.cpp:443 | notice | Não foi possível salvar a escolha de letra. Tente novamente. | Não foi possível salvar a letra escolhida. Tente novamente. | idem | — |
| LyricsBinding.cpp:467 | notice | Confira o nome, o texto e os limites da versão da letra. | (igual à linha 354) | idem | — |
| LyricsBinding.cpp:524 | notice | Preparando versões da letra para guardar… | Preparando as versões para salvar… | glossário: salvar | — |
| LyricsBinding.cpp:552 | notice | Não foi possível preparar as versões. Tente novamente ou abandone a alteração. | Não foi possível preparar as versões. Tente novamente ou descarte as alterações. | glossário: descartar | — |
| LyricsBinding.cpp:594 | notice | Não foi possível confirmar o salvamento. Tente salvar novamente ou recarregue as versões do disco. | Não foi possível confirmar se a versão foi salva. Tente salvar novamente ou recarregue as versões do disco. | diz o que não se sabe | — |
| LyricsBinding.cpp:617 | notice | Não foi possível carregar as versões da faixa. Os arquivos foram preservados. | Não foi possível carregar as versões da faixa. Nenhum arquivo foi alterado. | “preservados” é formal | — |
| LyricsBinding.cpp:635 | notice | Não foi possível salvar a escolha de letra. Tente novamente. | (igual à linha 443) | idem | — |
| LyricsBinding.cpp:655 | notice | Não foi possível carregar as versões existentes. Os arquivos foram preservados. | Não foi possível carregar as versões existentes. Nenhum arquivo foi alterado. | idem | — |
| LyricsBinding.cpp:659 | notice | Há outra versão desta forma. Confira o conteúdo antes de guardar ambas. | Já existe outra versão deste tipo. Confira o conteúdo antes de salvar as duas. | glossário: tipo, salvar | — |
| LyricsBinding.cpp:674 | notice | Estas versões já estavam guardadas. | Estas versões já estavam salvas. | glossário: salvar | — |
| LyricsBinding.cpp:686 | notice | As versões excedem os limites ou contêm dados inválidos. Os arquivos foram preservados. | As versões são grandes demais ou têm dados inválidos. Nenhum arquivo foi alterado. | “excedem os limites” é técnico | — |
| LyricsBinding.cpp:706 | notice | Versões da letra recuperadas do backup. | Versões da letra recuperadas da cópia de segurança. | glossário | — |
| LyricsBinding.cpp:710 | notice | Uma versão mais recente das letras foi encontrada. Os arquivos foram preservados. | As letras foram salvas por uma versão mais nova do RC Music. Nada foi alterado. | “versão mais recente das letras” parece versão de letra | — |
| LyricsBinding.cpp:712 | notice | Não foi possível carregar as versões da letra. Os arquivos foram preservados; tente recarregar. | Não foi possível carregar as versões da letra. Nada foi alterado; tente recarregar. | “preservados” é formal | — |
| LyricsBinding.cpp:754 | notice | Uma versão mais recente das letras foi encontrada. Os arquivos foram preservados; recarregue do disco. | As letras no disco são de uma versão mais nova do RC Music. Nada foi gravado. Recarregue do disco. | ambiguidade de “versão” | — |
| LyricsBinding.cpp:756 | notice | Não foi possível confirmar o salvamento. Tente salvar novamente ou recarregue as versões do disco. | (igual à linha 594) | idem | — |
| LyricsBinding.cpp:791 | notice | Não foi possível gerar romaji com o pykakasi. | Não foi possível gerar o romaji. Tente novamente. | regra 3: sem nome de biblioteca interna (pykakasi) | — |
| LyricsBinding.cpp:825 | romajiStatus | Prepare o componente local de romaji e reinicie o RC Music. | O gerador de romaji não está pronto neste computador. Prepare-o e abra o RC Music novamente. | “componente local” é vago | — |
| LyricsBinding.cpp:849 | notice | Não foi possível iniciar o pykakasi. | Não foi possível iniciar o gerador de romaji. | sem nome interno | — |
| LyricsBinding.cpp:927 | statusText | Preparando versões da letra para guardar… | Preparando as versões para salvar… | igual à linha 524 | — |
| LyricsBinding.cpp:933 | statusText | Estas versões já estavam guardadas. | Estas versões já estavam salvas. | igual à linha 674 | — |
| LyricsBinding.cpp:1086 | notice | Tentativa de guardar a letra descartada. | Alterações da letra descartadas. | igual à DownloadLyricsBinding:247 | — |
| LyricsBinding.cpp:1434 | notice | Alteração em memória abandonada. Não há faixa selecionada; os arquivos existentes foram preservados. | Alterações descartadas. Nenhuma faixa estava selecionada; os arquivos existentes não mudaram. | “em memória abandonada” é jargão | — |

### DownloadLyricsLookup.cpp (4)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| DownloadLyricsLookup.cpp:162 | message | Confira a faixa publicada e os limites dos metadados da letra. | Não foi possível buscar a letra. Confira as informações da faixa baixada. | “publicada”/“metadados” são internos | — |
| DownloadLyricsLookup.cpp:179 | message | Aguarde antes de tentar buscar novamente. | Muitas buscas seguidas. Espere um pouco e tente novamente. | igual às do LRCLIB | — |
| DownloadLyricsLookup.cpp:271 | message | Consulta automática do Letras indisponível. Busca no LRCLIB e no LyricFind. | A busca automática no Letras não está disponível. Buscando no LRCLIB e no LyricFind. | frase completa; “Consulta”→“busca” | — |
| DownloadLyricsLookup.cpp:272 | message | Consulta automática do Letras indisponível. Busca no LRCLIB. | A busca automática no Letras não está disponível. Buscando no LRCLIB. | idem | — |

### QtLyricsService.cpp (2)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| QtLyricsService.cpp:607 | message | Letra não encontrada para esta versão da música. | Letra não encontrada para esta gravação. | “versão da música” confunde com versão da letra; envolver em tr() | — |
| QtLyricsService.cpp:626 | message | O serviço limitou as consultas. Aguarde antes de buscar novamente. | Muitas buscas seguidas. Espere um pouco e tente novamente. | igual às do LRCLIB; envolver em tr() | — |

### WindowsWebView2Browser.cpp (1)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| WindowsWebView2Browser.cpp:605 | placeholder | Endereço HTTPS | Endereço do site (HTTPS) | linguagem comum; envolver em tr() | — |

### EmbeddedBrowserBinding.cpp (1)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| EmbeddedBrowserBinding.cpp:63 | error | A janela do player ainda não está pronta. Tente abrir o navegador novamente. | A janela do RC Music ainda não está pronta. Tente abrir o navegador novamente. | “player” vira RC Music | — |

### ListeningRoomBinding.cpp (13)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| ListeningRoomBinding.cpp:201 | error | Link de convite inválido | Esse link de convite não é válido. Confira se copiou o link inteiro. | regra 3: frase + próximo passo | qt/ListeningRoomBindingTests.cpp:275 |
| ListeningRoomBinding.cpp:509 | error | Não foi possível enviar a sua música. | Não foi possível enviar a sua faixa. Tente novamente. | glossário: faixa | — |
| ListeningRoomBinding.cpp:523 | error | Arquivo de áudio inválido | Esse arquivo não é um áudio que o RC Music toca. Escolha outro. | regra 3 | qt/ListeningRoomBindingTests.cpp:1159; ui/quick/tst_listening_room.qml:658 |
| ListeningRoomBinding.cpp:526 | error | A música passa de 50 MiB. | O arquivo passa de 50 MiB. Escolha um menor. | glossário: faixa/arquivo; próximo passo | qt/ListeningRoomBindingTests.cpp:1171 |
| ListeningRoomBinding.cpp:529 | error | Sem permissão | Você não tem permissão para isso nesta sala. | “Sem permissão” solto não diz para quê | qt/ListeningRoomBindingTests.cpp:984 |
| ListeningRoomBinding.cpp:535 | error | Não foi possível enviar a sua música. | (igual à linha 509) | idem | — |
| ListeningRoomBinding.cpp:555 | error | Sem permissão | (igual à linha 529) | idem | qt/ListeningRoomBindingTests.cpp:984 |
| ListeningRoomBinding.cpp:647 | error | Não foi possível preparar a identidade da sala neste computador. Tente de novo. | Não foi possível preparar a sala neste computador. Tente novamente. | “identidade da sala” é interno | qt/ListeningRoomBindingTests.cpp:335 |
| ListeningRoomBinding.cpp:773 | error | O servidor da sala usa outra versão do RC Music. Atualize o app para abrir salas. | O servidor da sala usa outra versão do RC Music. Atualize o RC Music para abrir salas. | glossário: RC Music, não “app” | — |
| ListeningRoomBinding.cpp:894 | error | A sala está cheia | A sala está cheia. | frase completa como as vizinhas | qt/ListeningRoomBindingTests.cpp:454; qt/ListeningRoomEndToEndTests.cpp:631; ui/quick/tst_listening_room.qml:681 |
| ListeningRoomBinding.cpp:909 | error | Você foi removido da sala. | O anfitrião tirou você da sala. | neutro em gênero; diz quem fez | — |
| ListeningRoomBinding.cpp:912 | error | A sala usa outra versão do RC Music. Atualize o app para entrar. | A sala usa outra versão do RC Music. Atualize o RC Music para entrar. | glossário: RC Music | — |
| ListeningRoomBinding.cpp:929 | error | A sala usa outra versão do RC Music. Atualize o app para entrar. | (igual à linha 912) | idem | — |

### DownloadJobsModel.cpp (30)

| arquivo:linha | propriedade | hoje | proposta | motivo | teste que fixa |
|---|---|---|---|---|---|
| DownloadJobsModel.cpp:57 | statusText(Queued) | Na fila | Aguardando | “Na fila” confunde com a fila de reprodução | qt/DownloadJobsModelTests.cpp:851; ui/quick/tst_downloads.qml:492 |
| DownloadJobsModel.cpp:73 | statusText(ImportFailed) | Arquivo pronto; importação pendente | Baixado · falta adicionar à fila | “importação pendente” é interno | qt/DownloadJobsModelTests.cpp:1047; ui/quick/tst_downloads.qml:468 |
| DownloadJobsModel.cpp:75 | statusText(Interrupted) | Interrompido; revisão necessária | Interrompido · precisa de revisão | mais natural | qt/DownloadJobsModelTests.cpp:702; ui/quick/tst_downloads.qml:495 |
| DownloadJobsModel.cpp:77 | statusText(Importing) | Confirmando importação na fila | Adicionando à fila… | “importação” é interno | qt/DownloadJobsModelTests.cpp:735 |
| DownloadJobsModel.cpp:79 | statusText(Imported) | Já importado na fila | Adicionado à fila | idem; “Já” sobra | qt/DownloadJobsModelTests.cpp:651; ui/quick/tst_downloads.qml:467 |
| DownloadJobsModel.cpp:89 | errorText(Unavailable) | O serviço de download não está disponível. Tente novamente mais tarde. | Os downloads não estão disponíveis agora. Tente novamente mais tarde. | “serviço” é interno | — |
| DownloadJobsModel.cpp:91 | errorText(Network) | Falha na conexão. Verifique a rede e selecione a mídia novamente. | A conexão caiu. Confira a internet e escolha a mídia novamente. | linguagem comum | — |
| DownloadJobsModel.cpp:93 | errorText(Expired) | O endereço expirou. Atualize a página e selecione a mídia novamente. | O link expirou. Atualize a página e escolha a mídia novamente. | “endereço” vira link | — |
| DownloadJobsModel.cpp:95 | errorText(Restricted) | Bloqueado pelas verificações de segurança do RC Music. Selecione outra mídia autorizada. | O RC Music bloqueou este download por segurança. Escolha outra mídia. | voz ativa; “autorizada” sobra | ui/qtest/DownloadApplicationBindingTests.cpp:2275 |
| DownloadJobsModel.cpp:98 | errorText(SourceRejected) | A fonte recusou o acesso ao áudio. O link pode ter expirado ou exigir outro acesso; tente baixar de novo. | O site recusou o acesso ao áudio. O link pode ter expirado ou estar restrito; tente baixar novamente. | “outro acesso” é vago | — |
| DownloadJobsModel.cpp:105 | errorText(LimitExceeded) | Um limite de download foi atingido. Confira o tamanho da mídia e se há pedidos encerrados para arquivar. | O download passou de um limite. Confira o tamanho da mídia ou arquive downloads encerrados. | “pedidos” é interno | — |
| DownloadJobsModel.cpp:110 | errorText(Publication) | Não foi possível salvar o arquivo. Verifique espaço e permissões da pasta de downloads. | Não foi possível salvar o arquivo. Confira o espaço livre e a permissão da pasta de downloads. | mais natural | — |
| DownloadJobsModel.cpp:113 | errorText(Import) | O MP3 está pronto, mas não entrou na fila. Tente importar novamente. | O MP3 está pronto, mas não entrou na fila. Tente adicionar novamente. | “importar” é interno | — |
| DownloadJobsModel.cpp:117 | errorText(InvalidResult) | O resultado não pôde ser validado. Selecione outra mídia. | O arquivo baixado não passou na verificação. Escolha outra mídia. | “resultado não pôde ser validado” é passivo e vago | — |
| DownloadJobsModel.cpp:135 | retryReason(SourceUnavailable) | A origem não foi retida após reiniciar. Envie este item novamente pelo navegador. | O link da mídia não foi guardado depois que o RC Music reiniciou. Envie o item novamente pelo navegador. | “origem não foi retida” é técnico | — |
| DownloadJobsModel.cpp:140 | retryReason(RecoveryRequired) | É necessário revisar a recuperação dos arquivos antes de outro download. Após revisar, envie o item novamente pelo navegador se necessário. | Revise os arquivos deste download antes de baixar novamente. Depois, se precisar, envie o item novamente pelo navegador. | voz ativa | — |
| DownloadJobsModel.cpp:145 | retryReason(NotFailed/ImportFailed) | O MP3 já está pronto. Use Tentar importar novamente, sem baixar o áudio outra vez. | O MP3 já está pronto. Use “Tentar adicionar à fila novamente”; não é preciso baixar outra vez. | cita o rótulo novo (DownloadText:41) | — |
| DownloadJobsModel.cpp:149 | retryReason(LimitExceeded) | O histórico atingiu um limite. Arquive pedidos encerrados antes de tentar novamente. | O histórico chegou a um limite. Arquive downloads encerrados antes de tentar novamente. | glossário | — |
| DownloadJobsModel.cpp:152 | retryReason(Unavailable) | Nova tentativa indisponível enquanto há confirmação, recuperação ou encerramento pendente. | Espere as tarefas pendentes deste download terminarem para tentar novamente. | lista de estados internos | — |
| DownloadJobsModel.cpp:213 | reviewText(OwnedPartial) | Arquivos parciais reconhecidos e preservados. Não há áudio pronto para importar; selecione a mídia novamente se necessário. | Arquivos parciais encontrados e mantidos. Não há áudio pronto para adicionar; escolha a mídia novamente se precisar. | “importar”/“reconhecidos” | — |
| DownloadJobsModel.cpp:222 | reviewText(OwnedFinalCandidate) | Arquivo final reconhecido; conteúdo ainda não validado ou importado. Nada foi alterado. | Arquivo final encontrado, ainda não validado nem adicionado à fila. Nada foi alterado. | idem | — |
| DownloadJobsModel.cpp:247 | reviewText(DiscardIncomplete) | O descarte dos temporários não foi concluído. Parte deles pode ter sido removida; o MP3 final não é alvo. Verifique novamente antes de outra tentativa. | Nem todos os temporários foram apagados; parte pode ter sido removida. O MP3 final não é apagado. Verifique novamente antes de outra tentativa. | “não é alvo” é jargão | — |
| DownloadJobsModel.cpp:250 | reviewText(DiscardUnconfirmed) | O descarte não foi confirmado no histórico. Temporários podem ter sido removidos; o MP3 final não é alvo. Não é possível afirmar que o histórico foi salvo. | O descarte não foi registrado no histórico. Alguns temporários podem ter sido apagados; o MP3 final não é apagado. Não dá para garantir que o histórico foi salvo. | idem | — |
| DownloadJobsModel.cpp:264 | validationText(Validated) | MP3 validado; não foi importado na fila. Libere a validação quando terminar a revisão; ela não publica nem importa o arquivo. | MP3 validado, mas ainda fora da fila. Quando terminar, use Liberar validação; isso não adiciona nem apaga o arquivo. | “publica nem importa” é interno | — |
| DownloadJobsModel.cpp:267 | validationText(Released) | Validação liberada. O arquivo não foi importado nem removido. | Validação liberada. O arquivo não foi adicionado à fila nem apagado. | “importado” é interno | — |
| DownloadJobsModel.cpp:376 | ReviewText(DiscardPartials) | Encerrando a recuperação. Temporários próprios ainda presentes podem ser removidos. | Encerrando a recuperação. Os temporários deste download que ainda existirem podem ser apagados. | “Temporários próprios” é interno | — |
| DownloadJobsModel.cpp:382 | ReviewText | Não foi possível verificar os arquivos. Reinicie o RC Music antes de tentar novamente. Tudo foi preservado. | Não foi possível verificar os arquivos. Reinicie o RC Music antes de tentar novamente. Nada foi apagado. | “Tudo foi preservado” | — |
| DownloadJobsModel.cpp:421 | ValidationText | Liberando a validação; aguardando a parada e a liberação dos arquivos. | Liberando a validação e os arquivos… | mais curta | — |
| DownloadJobsModel.cpp:425 | ValidationText | Adicionando à fila; aguardando a confirmação da gravação. | Adicionando à fila… | igual ao estado Importing | — |
| DownloadJobsModel.cpp:427 | ValidationText | Não foi possível confirmar a gravação da fila. Tente adicionar novamente; o áudio não será duplicado. | Não foi possível confirmar que a faixa entrou na fila. Tente adicionar novamente; ela não será duplicada. | glossário: faixa | — |

### 3.1 Regra R1: “de novo” → “novamente” (troca mecânica, 16 textos)

O mantenedor manteve “novamente”, que é como o próprio Windows escreve. Os textos que já dizem “novamente” ficam como estão, salvo as trocas das tabelas e das notas abaixo. Estes 16 textos dizem “de novo” e passam a dizer “novamente”, sem outra mudança além das que as tabelas já pedem:

| arquivo:linha | hoje | teste que fixa |
|---|---|---|
| DownloadJobsModel.cpp:99 | outro acesso; tente baixar de novo. | — |
| ListeningRoomBinding.cpp:647 | Não foi possível preparar a identidade da sala neste computador. Tente de novo. | qt/ListeningRoomBindingTests.cpp:335 |
| ListeningRoomBinding.cpp:693 | Não foi possível abrir a sala. Tente de novo. | — |
| ListeningRoomBinding.cpp:779 | Não foi possível conectar à internet para abrir a sala. Verifique a conexão e tente de novo. | — |
| ListeningRoomBinding.cpp:920 | Não foi possível conectar ao servidor da sala. Verifique a conexão e tente de novo. | — |
| ListeningRoomBinding.cpp:923 | Não foi possível conectar ao anfitrião. Verifique a conexão e tente de novo. | — |
| OnlineSearchBinding.cpp:194 | Não foi possível consultar os links agora. Tente de novo. | — |
| OnlineSearchBinding.cpp:318 | A busca online não respondeu. Confira a conexão e tente de novo. | — |
| OnlineSearchBinding.cpp:320 | A busca demorou demais. Tente de novo. | — |
| OnlineSearchBinding.cpp:322 | A busca devolveu uma resposta inesperada. Tente de novo. | — |
| OnlineSearchBinding.cpp:387 | A fila de downloads está ocupada. Confira Atividade e tente de novo. | — |
| OnlineSearchBinding.cpp:394 | Não foi possível confirmar o pedido. Confira Atividade antes de tentar de novo. | — |
| LibraryText.qml:19 | Não foi possível adicionar à fila agora. Tente de novo. | — |
| LibraryText.qml:25 | Não foi possível adicionar à playlist agora. Confira a página Playlists e tente de novo. | — |
| LibraryText.qml:33 | Tentar carregar de novo | — |
| LibraryText.qml:34 | Tentar salvar de novo | — |

`LibraryText.qml:33-34` e `OnlineSearchBinding.cpp:387`, `:394` também aparecem nas tabelas acima; vale a forma das tabelas, já com “novamente”.

Textos com “novamente” que pedem mais do que manter a palavra:

- `DownloadJobsModel.cpp:500`: "tentar importar novamente" → "tentar adicionar à fila novamente". Segue o glossário: "adicionar à fila".
- `DownloadJobsModel.cpp:228`: "Conflito entre os arquivos e o histórico. Tudo foi preservado; …" → "Os arquivos não batem com o histórico. Nada foi apagado; não use este resultado como um áudio validado. Você pode verificar novamente."
- `DownloadJobsModel.cpp:232`, `:280` e `DownloadApplicationBinding.cpp:1558`: "Tudo foi preservado" / "os dados foram preservados" → "Nada foi apagado".
- `DownloadJobsModel.cpp:269`, `:277`, `:297`, `:416` e `DownloadApplicationBinding.cpp:1563`: "Nada foi importado" → "Nada foi adicionado à fila".
- `EmbeddedBrowserBinding.cpp:103`: mantenha "Microsoft Edge WebView2 Runtime", que é o nome do componente que a pessoa precisa instalar.
- `BrowserMediaPanel.cpp:398`: "Inicie a mídia na página e tente Detectar mídia novamente" → "Dê play na mídia da página e use Detectar mídia novamente."
- `LyricsBinding.cpp:1103`: "Há um salvamento pendente de outra faixa." → "Outra faixa tem alterações não salvas."

### 3.2 Plurais (numerus)

Troque `%1` por `%n` e passe o número como terceiro argumento:

- `PlaylistsText.qml:10`, usado em `PlaylistsPanel.qml:351` e `:444`.
- `LibraryText.qml:7`, usado em `LibraryPanel.qml:125`. Aqui só o total muda de forma, então fica "%1 de %n faixa(s)".
- `RoomText.qml:27`, usado em `ListeningRoomPanel.qml:62` e `:416`.
- `RoomText.qml:71`, usado em `RoomRolesEditor.qml:167`.

Hoje esses textos são montados com `.arg()`. A função do catálogo precisa receber o número:

```qml
function trackCount(count) { return qsTr("%n faixas", "Quantidade de faixas da playlist", count) }
```

O `.ts` precisa das duas formas: "%n faixa" e "%n faixas".

Os testes que hoje procuram "faixas" e "pessoas" literalmente continuam valendo para n ≥ 2. Acrescente um caso com n = 1.

### 3.3 Textos para envolver em `tr()`

- **`QtLyricsService.cpp:242-805`.** Todas as mensagens são `QStringLiteral`. Use `QCoreApplication::translate("QtLyricsService", …)` ou `tr()` se a classe for `QObject`. As duas mais visíveis já estão nas tabelas (`:607`, `:626`).
- **`WindowsWebView2Browser.cpp:600-626`.** O placeholder e as dicas são `QStringLiteral`.
- **`BrowserMediaSnapshotCodec.cpp`.** Tem rótulos de origem e duração que aparecem no painel de mídia.

### 3.4 Textos sem uso (remover em commit `chore:`)

- **`NavigationText.qml:27` `collectionHint`** ("Ouça, organize e continue de onde parou."): nenhuma QML lê.
- **`ProfileText.qml:11` `currentAvatar`** ("Avatar atual"): nenhuma QML lê.
- Antes de remover, confira `ShellUiContractTests::essentialQmlTextIsCentralizedAndExtractable` e o `.ts`. O `lupdate` marca as entradas como obsoletas.

---

## 4. Textos do redesenho

Os textos abaixo são novos. Cada um vira uma propriedade nova no catálogo indicado e não tem teste ainda.

### 4.1 Biblioteca (`LibraryText`)

| elemento | texto visível | nome acessível / descrição | observação |
|---|---|---|---|
| ação no menu da faixa | Tocar agora | "Tocar %1 agora" / "Entra logo depois da faixa atual e começa a tocar." | Duplo clique faz o mesmo. Na dica da linha: "Clique duas vezes para tocar." |
| ação | Adicionar à fila | "Adicionar %1 à fila" / "Entra no fim da fila." | |
| submenu | Adicionar à playlist | "Adicionar %1 a uma playlist" | Se não houver playlist: item desabilitado "Nenhuma playlist ainda" e, abaixo, "Nova playlist…" |
| ação | Remover da biblioteca… | "Remover %1 da biblioteca" | Usa as reticências porque abre a confirmação. O corpo da confirmação é `LibraryText.qml:27` com a proposta da tabela. |
| confirmação | título: Remover “%1” da biblioteca? · botão: Remover da biblioteca | — | Título em pergunta, que nomeia a faixa. O botão repete o verbo. |
| botão ⋯ (todas as telas) | — | "Mais ações" / "Mais ações para %1" | Um só texto para todos os ⋯. Pode ficar em `NavigationText` como texto comum. |

### 4.2 Playlist (`PlaylistsText`)

| elemento | texto visível | nome acessível / descrição | observação |
|---|---|---|---|
| ação da playlist inteira | Tocar agora | "Tocar a playlist %1 agora" | Mesmo verbo da faixa |
| ação da playlist inteira | Adicionar à fila | "Adicionar a playlist %1 à fila" | |
| ação por faixa | Tocar agora / Adicionar à fila | "Tocar %1 agora" / "Adicionar %1 à fila" | Usa os mesmos textos da Biblioteca. |
| switch | Visível para a sala | descrição: "Quem está na sua sala pode ver esta playlist e, com permissão, adicionar faixas dela." | O estado vem do switch. Não escreva "Ligado/Desligado" no rótulo. |
| confirmação de excluir | título: Excluir “%1”? · corpo: `PlaylistsText.qml:39` proposto · botão: Excluir playlist | — | O botão atual "Excluir" vira "Excluir playlist". |
| confirmação de descartar | título: Descartar alterações? · corpo: `:43` proposto · botão: Descartar alterações | — | |

### 4.3 Buscar (`SearchText`)

| elemento | texto visível | nome acessível / descrição | observação |
|---|---|---|---|
| controle segmentado | **Buscar** / Colar links | "Modo de busca" | O redesenho diz "Pesquisar". Use "Buscar", porque a página, a ação e o botão "Buscar letra" já usam esse verbo. |
| selo do resultado | Com letra | "Letra encontrada no Letras" | Se o selo vier do LRCLIB, troque o nome do serviço no nome acessível. |
| estado do resultado pedido | Pedido | "%1 já está na Atividade" | Igual a `SearchText.qml:21` proposto. |

### 4.4 Gavetas e linha da letra (`TransportText`, `QueueText`, `LyricsText`)

| elemento | texto visível | nome acessível / descrição | observação |
|---|---|---|---|
| linha fina da letra | Abrir letra | "Abrir a letra completa" | |
| gaveta da fila | Fechar fila | "Fechar a fila" | |
| gaveta da letra | Fechar letra | "Fechar a letra" | Substitui "Fechar letra completa" (`LyricsText.qml:49`). |
| lixeira vermelha | Limpar fila… | "Limpar a fila" | Usa reticências porque abre a confirmação. |
| confirmação de limpar | título: Limpar a fila? · corpo: `QueueText.qml:30`/`:31` proposto · botão: Limpar fila | — | Hoje o título é "Limpar fila?" (`QueueText.qml:29`). |
| letra vazia | Esta faixa ainda não tem letra. | — | Mesmo texto de `TransportText.qml:20` proposto. |
| letra vazia, apoio | Busque no LRCLIB ou adicione a letra você mesmo. | — | |
| letra vazia, ações | Buscar letra / Adicionar versão | — | "Buscar letra" já existe e é fixado em `ShellUiContractTests.cpp:846` (⟦Buscar letra⟧). |
| fila vazia | A fila está vazia. · Abrir áudio | — | Reusa o verbo "Abrir áudio". |

### 4.5 Atividade (`DownloadText`)

| elemento | texto visível | nome acessível / descrição | observação |
|---|---|---|---|
| aviso de atenção (único) | %n faixa baixada tem letra para revisar / %n faixas baixadas têm letra para revisar | — | Usa numerus. "Revisar" bate com o botão. O redesenho diz "conferir", mas aqui fica "revisar" pela consistência. |
| ação do aviso | Revisar letras | "Revisar letras das faixas baixadas" | |
| ação da página | Arquivar encerrados | — | `DownloadText.qml:4` proposto |
| confirmação de arquivar | título: **Arquivar downloads encerrados?** · corpo: `:5` proposto · botão: Arquivar (`:6`, sem mudança) | — | Hoje o título reusa o rótulo do botão da página (`DownloadsPanel.qml:1329`). Crie `archiveTitle`. |
| confirmação de adicionar | título: **Adicionar à fila?** · corpo: `:37` proposto · botão: Adicionar à fila | — | Hoje título e botão usam `importRecovered` (`DownloadsPanel.qml:1356`, `:1361`). Crie `importRecoveredTitle`. |
| confirmação ao sair | título: **Sair sem salvar o histórico?** · corpo: `:10` proposto · botões: Voltar / Sair sem salvar | — | Hoje título e botão usam `closeWithoutSave` (`DownloadsPanel.qml:1416`, `:1420`). Crie `closeWithoutSaveTitle`. |
| confirmação de encerrar recuperação | título: **Encerrar a recuperação deste download?** · corpo: `:29`/`:30` propostos · botão: Encerrar recuperação | — | Hoje título e botão usam `confirmDiscard` (`DownloadsPanel.qml:1386`, `:1391`). |
| ação principal por estado | Falhou → **Tentar novamente** · Baixado, falta adicionar → **Adicionar à fila** · Interrompido → **Verificar arquivos** · Ativo → **Cancelar** · Adicionado → (nenhuma) | "%1: %2", com o título do download e a ação | Uma só ação visível. O resto vai para "Mais ações". |
| menu ⋯ | Mais ações | "Mais ações para %1" | O menu leva as ações que já existem e saíram da linha: "Informações do download", "Validar MP3", "Encerrar recuperação…", "Liberar validação". |

### 4.6 Ouvir junto (`RoomText`)

| elemento | texto visível | nome acessível / descrição | observação |
|---|---|---|---|
| Expulsar: título | Expulsar %1? | — | |
| Expulsar: corpo | %1 sai da sala agora. Com o convite, pode pedir para entrar novamente. | — | Confirme se uma pessoa expulsa precisa de aprovação para voltar. |
| Expulsar: botão | Expulsar | — | |
| Expulsar e bloquear: título | Expulsar e bloquear %1? | — | |
| Expulsar e bloquear: corpo | %1 sai da sala e não consegue mais entrar nas suas salas. Você pode desbloquear depois em Pessoas conhecidas. | — | O bloqueio fica salvo em `room-roles.json` e vale para salas futuras. |
| Expulsar e bloquear: botão | Expulsar e bloquear | — | |
| Fechar sala: título | Fechar a sala? | — | |
| Fechar sala: corpo | Todas as pessoas saem da sala agora. O que está tocando continua com você. | — | Confirme se o anfitrião continua tocando depois de fechar. Se não continuar, use "A reprodução para para todos." |
| Fechar sala: botão | Fechar sala | — | |
| switch de 3 estados por permissão | Como o cargo / Liberar / Negar | "%1 (%2)", com a permissão e a pessoa, e o estado como valor | Mantenha os três textos. O nome acessível segue `RoomText.qml:92` proposto. |
| permissões (rótulos) | Pular · Pausar · Adicionar faixas das playlists · Adicionar playlists inteiras · Editar fila · Enviar faixas do computador · Adicionar do YouTube | descrições: "Próxima, anterior e mudar a posição" · "Pausar e continuar" · "Faixas das playlists visíveis para a sala" · "Uma playlist visível inteira" · "Remover e reordenar" · "Enviar arquivos seus para o anfitrião" · "Reservado" | As descrições seguem `docs/superpowers/specs/2026-11-17-listening-room-design.md:198-206`. |
| janela Cargos | título: Cargos da sala · botão que abre: Editar cargos… | — | |

### 4.7 Cartão de importação (`SourceImportText`)

| elemento | texto visível | nome acessível | observação |
|---|---|---|---|
| botão de texto | Cancelar | "Cancelar importação" | |
| botão de texto | Fechar | "Fechar resumo da importação" | |

### 4.8 Lateral (`BrowserText`)

| elemento | texto visível | nome acessível / descrição | observação |
|---|---|---|---|
| item Navegar | Navegar | descrição: "Abre em uma janela separada." | Diz ao leitor de tela o que o ícone de janela externa mostra na tela; a descrição fica também na lateral compacta, que esconde o ícone. |

---

## 5. As 15 mudanças mais importantes

1. **Configurações em todo lugar.** "Preferências recuperadas" / "Revisar preferências" / "Preferências > Reprodução" → "Configurações recuperadas" / "Abrir configurações" / "Configurações > Reprodução". Arquivos: `PreferencesText.qml:38-71`, `ErrorText.qml:16`, `:19`, `NavigationText.qml:41`.
2. **"O host controla" / "Playlists do host"** → "O anfitrião controla" / "Playlists do anfitrião". Arquivos: `QueueText.qml:44`, `:45`, `RoomText.qml:98`.
3. **Guardar → Salvar** na letra da faixa baixada. "Guardar versões" / "A guardar: %1" / "Guardando letras…" → "Salvar versões" / "Prontas para salvar: %1" / "Salvando letras…". Arquivos: `LyricsText.qml:130-140`, `DownloadLyricsBinding.cpp:228-247`.
4. **Um só "Descartar alterações".** "Abandonar alteração", "Descartar edição…" e "Descartar tentativa" → "Descartar alterações", com título "Descartar alterações?". Arquivos: `LyricsText.qml:68`, `:101`, `:108`, `:134`, `:139`, `PlaylistsText.qml:29`, `:41-44`.
5. **Plurais.** "1 faixas", "Até 1 pessoas…" e "1 pessoas com este cargo" → numerus "%n faixa(s)", "%n pessoa(s)". Arquivos: `PlaylistsText.qml:10`, `LibraryText.qml:7`, `RoomText.qml:27`, `:71`.
6. **Estados de download.** "Arquivo pronto; importação pendente" / "Confirmando importação na fila" / "Já importado na fila" → "Baixado · falta adicionar à fila" / "Adicionando à fila…" / "Adicionado à fila". Arquivo: `DownloadJobsModel.cpp:73-79`.
7. **Download aguardando.** "Na fila" (download) → "Aguardando". Arquivos: `DownloadJobsModel.cpp:57`, `DownloadText.qml:15`, `:21`. Com isso, "Na fila de reprodução" → "Na fila" (`DownloadText.qml:54`).
8. **Versão do app.** "Uma versão mais recente das letras/playlists foi encontrada" → "As letras/playlists foram salvas por uma versão mais nova do RC Music. Nada foi alterado." Arquivos: `LyricsBinding.cpp:710`, `:754`, `PlaylistsBinding.cpp:101`, `:109`, `:125`, `LibraryBinding.cpp:306`, `:335`.
9. **"Usar nesta sessão"** → "Usar sem salvar". Arquivo: `LyricsText.qml:43`.
10. **Erros curtos da sala.** "Link de convite inválido" / "Sem permissão" / "A sala está cheia" → "Esse link de convite não é válido. Confira se copiou o link inteiro." / "Você não tem permissão para isso nesta sala." / "A sala está cheia." Arquivo: `ListeningRoomBinding.cpp:201`, `:529`, `:555`, `:894`.
11. **"Você foi removido da sala."** → "O anfitrião tirou você da sala." Arquivo: `ListeningRoomBinding.cpp:909`.
12. **Convite.** "Revogar convite" / "Vale até eu revogar" / "Termina quando eu fechar o app" → "Cancelar convite" / "Até você cancelar o convite" / "Até você fechar o RC Music". Arquivo: `RoomText.qml:16`, `:17`, `:38`.
13. **Letra e Buscar.** "Letras" (botão do player) → "Letra" (`TransportText.qml:19`). O redesenho usa "Buscar / Colar links", não "Pesquisar".
14. **Saída de áudio.** "A saída de áudio mudou e a reprodução foi interrompida. Tente novamente…" → "A reprodução parou. Tente novamente ou escolha outra saída em Configurações > Reprodução. …". Arquivo: `ErrorText.qml:15-19`.
15. **Capa.** "A capa foi descartada porque…" → "A capa não foi carregada porque…". Arquivo: `TrackDetailsText.qml:47-50`.

---

## 6. Dúvidas e como se resolvem

- **“de novo” × “novamente”.** Decidido em 2026-11-22: fica “novamente” (seção 3.1).
- **MiB × MB.** `ListeningRoomBinding.cpp:526` diz "50 MiB". Para o público geral, "50 MB" é mais familiar, mas só é exato se o limite for decimal.
- **"Parar e manter/limpar seleção"** (`BrowserMediaPanel`) e **"Liberar validação"** (`DownloadJobsModel.cpp:264`). Não ficou claro o efeito para a pessoa. Por isso os textos foram mantidos e só a explicação ficou mais simples.
- **"Biblioteca pendente"** (`DownloadText.qml:56`). Propus "Ainda não está na biblioteca". Falta confirmar se o download vai para a biblioteca sozinho depois.
- **Histórico de downloads.** Falta confirmar se o histórico sobrevive a um reinício. Se não sobreviver, mantenha "Nenhum download nesta sessão." (`DownloadText.qml:11`).
- **Fechar sala.** Falta confirmar se o anfitrião continua tocando depois de fechar a sala.
- **Expulsão.** Falta confirmar se uma pessoa expulsa precisa de aprovação para voltar pelo mesmo convite.
- **Versões diferentes.** "Atualize o RC Music" pode ser rígido demais se a incompatibilidade for só de protocolo. Nesse caso: "Você e o anfitrião precisam de versões compatíveis do RC Music."
- **Pinos.** Os de textos curtos podem ser falsos positivos, porque literais iguais aparecem em catálogos diferentes. Confira com `rg` antes de editar.
- **Comportamento do app.** As dúvidas acima sobre o que o app faz (fechar a sala, expulsão, histórico de downloads, limite de 50 MiB, validação, biblioteca pendente) se resolvem lendo o código na fatia de cada área, antes de trocar o texto. Nenhum texto pode prometer o que o app não faz.

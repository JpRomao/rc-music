# Letras e versões por música

**Estado:** recurso validado automaticamente no Windows. O [setup Completa experimental de 2026-11-13](windows-installer.md#setup-completa-experimental-de-a114f20) inclui LyricFind e geração de romaji, que exigem as preparações abaixo. Aceitação nativa e instalada pendentes.

O [ADR-0016](architecture/decisions/0016-lyrics-variants.md) registra a evolução de FR-054. O catálogo guarda original, romanização e traduções fornecidas pelo usuário. No fluxo de downloads, é possível guardar juntas as versões coladas de uma página do Letras. A consulta automática ao Letras continua pendente de acesso compatível. [Letras do áudio baixado](download-lyrics.md)

## Usar as versões

Abra **Versões** junto à letra. Adicione texto, use **Importar .lrc** para carregar um rascunho de um arquivo local ou salve a letra atualmente carregada. Ao tocar uma música pela primeira vez, o RC Music usa a primeira versão salva. Se a música ainda não tiver versões salvas, usa a letra do `.lrc` irmão quando disponível. **Arquivo / busca atual** volta à letra do arquivo ou da consulta desta sessão. **Sem letra** oculta a letra. O RC Music lembra a opção escolhida em **Versões** para cada música, inclusive após reiniciar.

Selecione uma versão salva e clique em **Editar**. É possível alterar nome, texto, idioma, escrita e forma, e salvar na mesma versão ou como nova. Para alterar o tempo de um verso, marque **Permitir alterar tempos**. Excluir pede confirmação da versão e da faixa. O áudio, as playlists e o `.lrc` irmão são preservados.

Para buscar, adicionar, editar ou remover versões sem trocar o que está tocando, abra o menu de uma faixa disponível na **Fila** e escolha **Gerenciar letras**. O painel identifica a faixa e conserva o alvo quando a reprodução avança. Se a faixa sair da fila ou mudar de arquivo antes de salvar, reabra o painel para escolher o destino.

O aviso acima da barra de progresso acompanha a gravação. Em caso de falha, **Revisar salvamento** abre as opções de recuperação. Uma gravação já iniciada continua vinculada ao arquivo escolhido, inclusive se a faixa sair da fila.

LRC conserva seus tempos quando aceito pelo parser. Texto comum permanece sem sincronização. Idioma, escrita e origem identificam cada versão.

### Ler enquanto ouve

**Abrir letra** mostra a letra completa. O leitor sincronizado acompanha o verso confirmado pelo player. Use **Voltar ao verso atual** para retomar o acompanhamento depois de rolar ou navegar manualmente. Quando a faixa aceita busca por posição, clique no verso ou pressione Enter com o verso selecionado. **A−** e **A+** ajustam o tamanho e guardam a preferência para a próxima sessão.

### Gerar romaji

Com o [componente local pykakasi preparado](../tools/lyrics-romanizer/README.md), selecione **Gerar romaji** junto de uma letra japonesa em exibição e revise o texto antes de salvar. A ação também está disponível em **Versões** e em **Fila → Gerenciar letras** ao selecionar uma versão japonesa salva. O player conserva os tempos da versão sincronizada e guarda o romaji como uma versão adicional. Para uma letra que usa somente kanji, indique o idioma `ja` na versão salva para distingui-la de outros idiomas que usam caracteres Han. A leitura de alguns kanji pode precisar de correção. O conversor usa somente o texto e funciona offline.

### Buscar e escolher uma gravação

Abra **Buscar letra**, confira título, artista e álbum e clique em **Buscar**. O formulário preenche esses campos com as tags do áudio. Quando faltar uma tag, você pode preencher o campo manualmente. É possível buscar só pelo título. O RC Music envia os campos preenchidos ao LRCLIB e usa a duração para ordenar e sinalizar gravações diferentes. Abrir o formulário e editar os campos são ações locais.

Escolha a gravação pelo título, artista, álbum e duração disponíveis. Quando aparecer mais de um formato, escolha **Sincronizada** ou **Texto simples** para ler a prévia. **Usar nesta sessão** muda a exibição e **Salvar versão** grava a letra com o nome informado e origem LRCLIB. O catálogo registra o resultado como original e permite acrescentar idioma e escrita ao gerenciar a versão. Fechar a busca, cancelar ou receber um erro conserva a letra que estava em uso. Um resultado instrumental exige confirmação própria.

Em **Versões** ou **Fila → Gerenciar letras**, clique em **Buscar no LRCLIB**, escolha a gravação e o formato e confira a prévia. O gerenciamento da fila lê as tags da faixa selecionada mesmo que ela nunca tenha sido tocada. O nome do arquivo identifica a faixa na interface quando necessário. **Salvar versão** adiciona a letra à faixa indicada no painel. A música e a letra em uso seguem durante a consulta, a prévia e a gravação. Se o salvamento falhar, o botão de tentar novamente repete a escrita da versão escolhida.

### Fonte opcional LyricFind

Use as chaves de parceiro fornecidas para a sua integração. No Windows, coloque o arquivo `lyricfind.json` na pasta `%LOCALAPPDATA%\MusicPlayer\MusicPlayer` e reinicie o RC Music. Exemplo de estrutura com valores ilustrativos:

```json
{
  "displayKey": "SUA_CHAVE_DISPLAY",
  "territory": "BR",
  "preferLyricFind": true
}
```

O campo `lrcKey` recebe a chave de sincronização quando incluída no contrato. Com o LyricFind configurado, o diálogo oferece **Consulta por metadados exatos**. Nessa ação, o RC Music tenta primeiro o LRCLIB por padrão. Use `preferLyricFind: true` para inverter a ordem. Quando a primeira fonte não encontra a faixa, o player consulta a segunda. A consulta ao LyricFind envia título, artista, álbum se disponível e o território configurado.

O RC Music exibe os créditos e o link do LyricFind junto da letra. As letras obtidas por essa integração ficam disponíveis para leitura, conforme as condições de exibição do provedor. Use uma fonte com permissão de armazenamento para salvar versões ou gerar romaji. A API Web Service documenta a consulta de letras e LRC. As traduções do catálogo dependem do contrato e da interface que o parceiro fornecer. [Documentação da API](https://docs.lyricfind.com/) · [Contato do LyricFind](https://www.lyricfind.com/contact)

A consulta preserva sinais de mais e sequências literais como `%2B` no título, no artista e no álbum.

O player exibe a versão salva selecionada acima da barra de progresso. Se a opção, o resultado da busca ou a faixa mudar com o formulário aberto, reabra o formulário para confirmar a letra atual.

Se a reprodução avançar para outra ocorrência do mesmo arquivo, uma consulta ainda em andamento é cancelada. A letra local e o catálogo continuam associados ao arquivo. A regressão preservou as versões já guardadas, descartou a resposta anterior e confirmou uma nova busca. Passaram 112 casos Qt nas quatro suítes afetadas. Logs em `out/product/test-runs/lyrics-duplicate-occurrence-20261111/`.

## Salvamento e recuperação

As versões e a escolha explícita pertencem ao caminho exato do arquivo e ficam na pasta de configuração do RC Music, dentro de `lyrics`. Mover ou renomear o áudio não transfere essas associações automaticamente. Lite e Completa usam os mesmos dados. A escolha **Arquivo / busca atual** persiste, e uma letra usada só para prévia continua disponível durante a sessão. Quando a versão escolhida é excluída, o catálogo volta à seleção automática.

Cada música aceita até **16 versões**, com **256 KiB** por texto/LRC e **4 MiB** de conteúdo somado. O documento JSON é limitado a **10 MiB**. Os demais limites e o backup/lock estão no ADR. Dados malformados ou excessivos são recusados sem truncar ou publicar um catálogo parcial.

Alterações aparecem após confirmação de escrita. Em falha, tente salvar novamente o mesmo candidato ou confirme sua substituição pelo estado lido do disco. A recarga não promete desfazer uma escrita que tenha ocorrido antes da falha de confirmação. Uma gravação da faixa anterior continua pertencendo àquela faixa; não pode trocar a letra atual. Se a fila ficar sem faixa atual, **Versões** permanece acessível enquanto houver alteração pendente. Nesse caso, **Abandonar alteração** descarta somente o candidato em memória, sem prometer ler ou desfazer o disco; o retry continua destinado à faixa original.

## Evidência de implementação

Recibos locais: `out/product/test-runs/lyrics-variants-20261104/`. Reds observados antes das implementações: domínio **2 casos passando/7 falhando**, store **3/17**, binding focado **2/2**, Quick **2/14**, restart **2/1** (filho retornando 2). O domínio Green passou **9 casos/54 assertions** e o store inicial **23 casos Qt**; o restart em dois processos elevou esse alvo a **24**.

A revisão acrescentou Reds para busca aceita cuja resposta ficava oculta pela seleção salva (**15/1** no binding) e pendência após remover a última faixa (**2/1** no binding e **16/1** no Quick). Os Greens preservam a seleção quando a busca é recusada, revelam a resposta quando aceita e mantêm retry/abandono acessíveis sem faixa atual. Uma falha intermediária de clique Quick ocorreu antes do polimento da linha de recuperação recém-exibida; o harness passou a aguardar layout e verificar dimensões/separação dos botões antes do mesmo clique real e da mesma assertion de ação.

| Check final | Resultado |
|---|---|
| Seleção proporcional Release | **295/295 CTest**, 76,42 s, sem falhas/skips |
| Componentes dessa seleção | 283 testes Core e 12 alvos Qt, com 845 casos Qt incluindo inicialização/encerramento |
| Letras: serviço / store / binding | 17 / 24 / 17 casos Qt |
| Quick completo / novo painel | 336 / 17 casos |
| Regressões: playlists / presenter / importação | 35+15+3 / 306 / 79 casos Qt |
| Processos: preferências / sessão / startup | 3 / 7 / 3 casos Qt |
| Core Windows sem Qt | **283/283 CTest** |
| Builds Release | Lite ON/OFF e Completa OFF passaram; OFF expõe zero testes |
| Lint e formato | `all_qmllint` sem avisos; 11 C++ alterados e todos os 71 QML, com saída normalizada de `qmlformat`, sem `--check` |
| Grafos do executável | 322/301/329 comandos, com domínio/store/binding/painel, sem fontes de teste/Registry/WFP; navegador somente na Completa |

O restart grava original e romanizada em um processo e lê em outro, com CWDs diferentes, conferindo IDs acima de `2^53`, metadados, conteúdo, ordem e hashes de primário, backup e áudio sentinela. Binding usa transporte falso para provar salvamento online somente por ação explícita; não fez uma nova consulta real ao provedor. Falhas injetadas antes/depois de commit distinguem escrita física de reconhecimento Saved. A seleção acima não é a suíte inteira de AppContainer, instaladores ou aceitação física.

Não houve instalação, registro no host, nova execução AppContainer, benchmark ou alteração de músicas/dados pessoais. Os seis arquivos AppContainer em andamento e cinco artefatos congelados foram preservados por hash. Instaladores não foram regenerados. FR-054/M3.9 e gates globais permanecem **em andamento**. Letras.mus.br, MP3 remoto, LLM, tradução, exibição simultânea alinhada, Linux, durabilidade após perda física de energia e aceitação nativa/empacotada conservam seus próprios limites.

### Seleção dos formatos online — 2026-11-10

Red observado no binding por ausência da seleção/revisão e no painel Quick com **16 casos aprovados/4 falhos**. O Green passou **6/6 CTest** em 49,70 s, incluindo serviço, store, binding, lookup de downloads, binding de downloads e a suíte Quick completa com **361 casos**. O build Release do player e `all_qmllint` passaram. Uma regressão adicional mostrou que a posição de reprodução notificava a lista de formatos; após separar essa notificação, o fechamento dos dois bindings passou **2/2 CTest**, com **49 casos Qt**, e o player foi recompilado.

Os casos novos usam resposta de rede falsa e armazenamento temporário. Conferem uma única busca, prévia e salvamento dos dois conteúdos, tempos preservados, seleção salva, áudio sentinela e rejeição de formulários de outra escolha, busca ou faixa. Recibos em `out/product/test-runs/online-lyrics-variants-20261110/`.

A chegada de tags ou duração da mesma faixa preserva a busca já confirmada. A próxima busca usa os novos metadados. Trocar de faixa revoga a resposta anterior. A regressão reproduziu o cancelamento indevido antes da correção e passou com resposta de rede falsa. O serviço e o lookup de downloads passaram **2/2 CTest, 57 casos Qt**. Recibos em `out/product/test-runs/download-lyrics-paste-20261111/`.

### Edição pela fila — 2026-11-11

Os testes reproduziram a falta da ação no menu e a perda do aviso durante a troca de faixa. A validação final passou **3/3 CTest**, com **58 casos Qt** dos bindings e **374 casos Qt Quick**, além do build Release e `all_qmllint`. Os casos usam fila e armazenamento de teste para conferir um caminho FLAC Unicode, revalidação do destino, falha e retry da gravação, preservação da letra atual e foco por teclado. Recibos em `out/product/test-runs/queued-lyrics-20261111/`. A aceitação instalada permanece pendente.

### Navegação nas versões por teclado

No Red, End manteve o foco no primeiro item de uma lista com 16 versões salvas. O Green passou em 22/22 Qt Quick, incluindo navegação até a última versão, preservação do cursor pelo ID textual após reordenação e retorno a **Arquivo / busca atual**. O teste conferiu o ID `9007199254740993`, a seleção por Enter e Espaço e a reprodução inalterada. Build Release focado, formato e lint dos QML alterados passaram. Logs em `out/product/test-runs/lyrics-variants-keyboard-20261111/`. [Atalhos no manual](user-guide.md#letras-das-músicas)

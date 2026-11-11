# Letras e versões por música

**Estado:** recurso validado automaticamente no Windows. Os [setups de 2026-11-11](windows-installer.md#setups-atualizados-em-2026-11-11) incluem a escolha dos formatos online. Aceitação nativa e instalada pendentes.

O [ADR-0016](architecture/decisions/0016-lyrics-variants.md) registra a evolução de FR-054: guardar várias formas de uma letra e adicionar outras depois, sem duplicar áudio. Original e romanizada conservam o idioma; tradução e geração por LLM continuam como ideias futuras. Não há integração de download com Letras.mus.br nesta entrega.

## Usar as versões

Abra **Versões** junto à letra. Adicione texto ou LRC com nome, idioma, escrita e origem, ou salve a letra atualmente carregada. Escolha a versão que será exibida acima da barra de progresso; **Arquivo / busca atual** volta à letra do `.lrc` irmão ou da consulta desta sessão. Uma música pode ter somente uma forma; não é necessário preencher original e romanizada.

Adicionar outra versão depois mantém as anteriores. Excluir remove somente o texto escolhido, após confirmação, e conserva áudio, fila, playlists e `.lrc` irmão. O salvamento é explícito; abrir o player ou buscar uma letra não cria cache persistente automaticamente.

LRC conserva seus tempos quando aceito pelo parser; texto comum permanece sem sincronização. Não há geração automática de tempos, romanização ou tradução. Idioma, escrita e origem são informados pelo usuário; a indicação de conteúdo gerado permite identificar texto produzido externamente.

### Resultados da busca no player

Depois de confirmar **Buscar letra**, abra **Versões** e escolha **Arquivo / busca atual**. Se o LRCLIB trouxer conteúdos distintos, o seletor oferece **Sincronizada** e **Texto simples**. A sincronizada vem selecionada e a prévia acompanha a opção escolhida. Para guardar cada resultado, use **Salvar letra atual**, informe um nome e escolha **Original** ou **Romanizada**. A origem registrada é LRCLIB.

Os rótulos descrevem o formato recebido. Idioma, escrita e forma são informados ao salvar.

O player exibe a versão salva selecionada acima da barra de progresso. Se a opção, o resultado da busca ou a faixa mudar com o formulário aberto, reabra o formulário para confirmar a letra atual.

## Salvamento e recuperação

As versões pertencem ao caminho exato do arquivo e ficam na pasta de configuração do RC Music, dentro de `lyrics`. Mover ou renomear o áudio não transfere essas associações automaticamente. Lite e Completa usam os mesmos dados. A escolha exibida é da sessão; as versões salvas sobrevivem ao reinício.

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

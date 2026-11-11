# Playlists locais do RC Music

**Estado:** recurso implementado no código Windows, com adição direta e localização de arquivos. Os [instaladores de 2026-11-11](windows-installer.md#setups-atualizados-em-2026-11-11) incluem a adição direta. Aceitação nativa e instalada da localização permanecem pendentes.

O mantenedor colocou playlists nomeadas como próxima prioridade em FR-056/M6.6. O [ADR-0015](architecture/decisions/0015-named-playlists.md) registra o domínio puro, a persistência local e a composição do painel. Lite e Completa usam o mesmo recurso, independente da fila em reprodução.

## Criar e organizar

1. Abra **Playlists** no player e escolha **Nova playlist** para criar uma lista vazia. Informe o nome e confirme **Salvar**.
2. Para copiar a fila que está no player, escolha **Salvar fila como…** e informe o nome da nova lista. A fila e a música atual são preservadas.
3. Selecione uma playlist e use **Adicionar arquivos…** para escolher faixas locais diretamente. Use **Adicionar fila atual** para copiar as faixas que já estão no player. As duas ações acrescentam as faixas na ordem recebida e permitem duplicatas.
4. Selecione uma faixa da lista e use os controles de mover para cima/baixo ou **Remover**. Se o arquivo mudou de lugar, use **Localizar arquivo…** e selecione o novo caminho. A ação atualiza só a entrada selecionada e mantém sua posição e as duplicatas. A edição preserva a fila em reprodução e os arquivos de áudio.
5. Use **Renomear** para alterar o nome da lista escolhida. **Excluir** pede confirmação do alvo; somente a playlist é removida.

Listas e entradas possuem IDs próprios: nomes iguais e faixas repetidas são permitidos. Nomes e paths são apresentados como texto simples. Trocar a seleção ou alterar o alvo de uma confirmação não deve redirecionar a ação para outra lista.

## Adicionar músicas à fila

Selecione a lista desejada e clique em **Adicionar à fila**. Suas faixas são acrescentadas ao fim da fila atual, na ordem salva, com novas ocorrências de fila. A ação preserva a seleção e a reprodução correntes; para tocar, escolha explicitamente uma ocorrência no player.

Abrir uma playlist apenas mostra seu conteúdo. Salvar, editar, renomear ou excluir uma lista não inicia áudio, consulta remota ou download. A persistência da lista não depende da opção de restaurar a sessão do player.

## Salvamento e erros

O painel mostra a operação de salvamento e só publica a edição nas listas após o reconhecimento **Saved** do store. Enquanto há trabalho ativo, a operação de arquivos roda em um worker e novas edições ficam indisponíveis até sua conclusão; o áudio conserva seu fluxo normal.

O seletor de arquivos abre de forma assíncrona e vincula a seleção à playlist escolhida. Cancelar o diálogo, trocar o alvo ou iniciar a saída invalida a seleção pendente. Após a confirmação dos arquivos, o salvamento segue o mesmo fluxo das outras edições.

**Localizar arquivo…** abre um seletor de um único arquivo e vincula o resultado à entrada escolhida. O novo caminho só aparece após a confirmação do salvamento. Use **Adicionar à fila** para incluir as faixas com os caminhos atualizados.

Se o salvamento falhar, **Tentar salvar** submete novamente o mesmo candidato. A opção de recarregar do disco abandona esse candidato em memória e consulta o estado realmente armazenado. Uma falha de confirmação pode acontecer depois da gravação física; por isso, recarregar pode reencontrar a alteração e não promete desfazê-la.

Durante a preparação de saída, o app fecha novas admissões e aguarda o trabalho pendente. Uma alteração sem confirmação precisa ser resolvida; a saída cancelada devolve as ações do painel. Fechar o processo não é uma confirmação de salvamento.

Quando não for possível carregar o catálogo, use **Tentar abrir novamente** depois de conferir a disponibilidade dos arquivos na pasta de configuração. Versão futura e dados inválidos são preservados; o app não grava uma lista vazia sobre esses documentos por consequência do erro.

## Arquivos e limites

O catálogo usa `playlists.json`, `playlists.json.bak` e `playlists.json.lock`, na pasta de configuração do RC Music. Ele contém nomes, IDs e referências aos arquivos locais; o store não abre, altera ou apaga esses áudios. O backup conserva a versão anterior do primário válido e pode recuperar uma leitura quando o primário estiver inválido ou ausente. A leitura não repara os arquivos automaticamente.

| Limite | Valor |
|---|---:|
| Playlists no catálogo | 128 |
| Entradas somadas entre todas as listas | 10.000 |
| Nome de playlist | 256 bytes UTF-8 |
| Path de cada entrada | 32.768 unidades nativas |
| Payload de nomes e paths | 8 MiB |
| Documento JSON | 16 MiB |

Os IDs são strings decimais no JSON, preservando exatamente valores acima de `2^53`. A versão inicial recusa registros incompletos/malformados e excesso de limites, sem carregar um subconjunto silenciosamente. O lock é cooperativo e sem espera; uma instância ocupada ou um documento indisponível produz um resultado acionável para tentar novamente.

Importação/exportação M3U8 permanece uma evolução posterior. A primeira entrega usa as listas salvas do próprio app, sem banco de dados, conta ou rede.

## Evidência de implementação

Os recibos locais ficam em `out/product/test-runs/playlists-implementation-20261104/`. Os Reds foram observados antes das respectivas implementações: domínio 20 casos falhando; store 33 falhas e binding 9; painel sem a propriedade de composição; restart com filho recusando o fluxo ainda ausente. Regressões adicionais cobriram leitura de fila com exceção/saída reentrante, papéis acessíveis dos diálogos e confirmação da adição à fila quando um observador falha depois do commit. Essa última falha não pode induzir o chamador a repetir uma adição já efetivada.

| Check final do recorte | Resultado |
|---|---|
| Domínio puro | 20 casos, 228 assertions |
| Store / binding Qt | 35 / 15 casos, incluindo inicialização e encerramento do Qt Test |
| Painel Quick isolado / suíte Quick | 16 / 319 casos |
| Restart de playlists | 3 casos Qt Test; gravação e leitura em dois processos distintos |
| Presenter / importação durável | 306 / 79 casos |
| Codecs compartilhados de download / navegador | 188 / 68 casos |
| Core Release sem Qt, no Windows | 274/274 CTest |
| Builds Release Lite ON, Lite OFF e Completa OFF | Passaram; as duas composições OFF expõem zero testes |
| `all_qmllint` | Passou sem avisos |
| Formato | 17 C++ alterados e todos os 69 QML; comparação normalizada da saída de `qmlformat`, sem `--check` |
| Documentação / rastreabilidade | 90 Markdown, 86 requisitos, 17 checks e 82 etapas; 489 referências válidas |

A regressão inicial passou 286/287 CTest; a única falha era um clique do teste Quick antes do polimento do layout. O harness passou a aguardar visibilidade/polimento e a verificar a separação dos botões, mantendo o clique real. A seleção final Quick + restart passou 2/2 CTest e os cinco alvos de presenter/importação/restart de preferências, sessão e startup passaram 5/5 após a última correção. Essas seleções complementam a regressão inicial; não são uma nova execução integral de toda a suíte.

O teste de restart preserva nomes Unicode, IDs, ordem, duplicatas e hashes de todos os arquivos no diretório entre a leitura e seu encerramento. Os testes usam portas falsas e diretórios temporários próprios, sem depender de músicas pessoais. O grafo do aplicativo inclui domínio/store/binding/painel nas duas edições; o navegador permanece exclusivo da Completa, e fontes de teste, Registry e WFP não entram nesses grafos de produção.

Não houve instalação, registro no host, uso de perfis AppContainer, benchmark ou alteração de dados pessoais. Os seis arquivos AppContainer em andamento e os cinco artefatos congelados anteriores foram preservados. Os instaladores existentes não foram regenerados neste incremento. FR-056/M6.6 continua **em andamento** até a aceitação nativa e empacotada; estes checks não encerram acessibilidade nativa, Linux, durabilidade após perda de energia nem os gates globais do plano.

### Adição direta de arquivos — 2026-11-10

O Red do painel teve **14 passagens e 4 falhas** pela ausência do botão. O Red do binding falhou ao compilar pela ausência da porta na composição e das operações de seleção. Após a implementação, o build Windows Release do player e `all_qmllint` passaram. A seleção final passou **4/4 CTest**, com **358 casos Quick**, **17 do binding**, **4 de restart** e **35 do store**. O caso de restart abre um catálogo salvo em outro processo e confere Unicode, ordem, IDs e duplicatas com zero leituras ou alterações da fila. Os logs estão em `out/product/test-runs/playlist-direct-files-20261110/`. A validação automatizada usa um seletor falso e mantém pendentes a interação com o diálogo nativo e a aceitação instalada.

### Localização de arquivo na playlist

O Red confirmou a ausência da substituição no núcleo, no binding, no restart e no painel. O fechamento passou **307 casos do núcleo**, **4/4 CTest selecionados com 66 casos Qt**, **19 casos Quick**, build Release e `all_qmllint`. A regressão de symlink do núcleo registrou a limitação de permissões deste Windows. Um segundo processo leu o novo caminho Unicode do catálogo real com os mesmos IDs e ordem. Os hashes das três fixtures de áudio foram preservados e a fila ficou intacta. O diálogo foi simulado nessa jornada. Logs em `out/product/test-runs/playlist-locate-20261111/`.

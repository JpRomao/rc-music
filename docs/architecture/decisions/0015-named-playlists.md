# ADR-0015: playlists locais nomeadas e persistentes

**Estado:** Aceito

**Data:** 2026-11-04

**Decisão de escopo:** o mantenedor pediu playlists no app como próxima prioridade, antecipando FR-056/M6.6. O domínio, a porta de armazenamento e a composição Qt descritos abaixo foram adotados na implementação. Checks, aceitação nativa e distribuição conservam suas evidências próprias.

## Contexto

A fila de reprodução e sua restauração de sessão representam o trabalho corrente. O usuário também precisa manter várias listas com nomes, recuperar essas listas após reiniciar e organizá-las sem interromper a música atual. Uma playlist salva contém referências aos arquivos locais; nomes iguais e caminhos repetidos precisam continuar distinguíveis.

O pedido antecipa playlists nomeadas antes da continuidade das integrações remotas. Não altera os contratos de reprodução, importação durável, Lidarr, extensões ou AppContainer. O [guia de playlists](../../playlists.md) descreve o fluxo da primeira entrega; M3U8 permanece evolução posterior de intercâmbio.

## Decisão

### Domínio e identidade

1. Manter `PlaylistLibrary` no núcleo C++ puro, sem Qt, rede ou I/O. `PlaylistLibrarySnapshot` contém playlists com ID, nome UTF-8 e entradas ordenadas com ID e `source_path`. A porta separada `PlaylistStore` carrega/salva esse snapshot.
2. Usar IDs inteiros de 64 bits não zero. IDs de playlists e de entradas pertencem a namespaces separados; IDs de entradas são únicos no catálogo inteiro. Nome e caminho não são identidade: nomes iguais e faixas duplicadas são permitidos, preservando ordem e ocorrências distintas. Esses IDs não são os IDs transitórios da fila em reprodução.
3. Validar restauração e mutações antes de comprometer o novo estado. Snapshot inválido não substitui o catálogo saudável. Nomes seguem a política textual existente de UTF-8 e linha única, sem normalizar silenciosamente um nome persistido inválido; paths têm limites lexicais e não são abertos pelo domínio.

### Operações e reprodução

4. Oferecer criação de lista vazia, renomear, excluir, salvar a fila atual como outra playlist, adicionar arquivos locais ou a fila a uma lista existente e remover/reordenar entradas por ID. Essas operações não alteram a fila, a seleção nem a reprodução em andamento. Excluir uma playlist remove somente a lista, com confirmação na UI, sem apagar áudios.
5. A seleção de uma lista apenas mostra seu conteúdo. **Adicionar à fila** copia suas faixas na ordem salva e usa o ingresso local existente, atribuindo ocorrências próprias da fila. Acrescenta ao fim sem selecionar faixa ou iniciar reprodução. Tocar uma ocorrência continua sendo uma ação explícita do player.
6. Manter o mesmo catálogo nas edições Lite e Completa, independente da sessão de reprodução e das opções de restauração da fila. Playlists funcionam offline e não iniciam consultas de metadata, letras ou downloads por serem abertas ou editadas.

### Armazenamento local

7. Adotar `QtJsonPlaylistStore` para o documento `playlists.json`, seu backup `playlists.json.bak` e o lock cooperativo `playlists.json.lock`, na raiz de configuração fornecida pela composição confiável. O construtor copia o diretório absoluto e permanece sem I/O. Nenhum arquivo de áudio é aberto, movido ou apagado pelo store.
8. Usar JSON `schemaVersion: 1`, com IDs em strings decimais canônicas e paths codificados sem perda de Unicode. O codec recusa campos v1 desconhecidos, duplicação de chaves, IDs inválidos, strings malformadas e snapshots fora dos limites. `StrictJson` recebe limite de profundidade **5** para esse schema; os codecs existentes conservam o padrão **4**. O parâmetro é escolhido pelo codec confiável, entre 1 e 64, não pelo documento.
9. Limitar o catálogo a **128 playlists**, **10.000 entradas no total**, nomes de **256 bytes UTF-8**, paths de **32.768 unidades nativas** e payload agregado de **8 MiB** de nomes e paths. O documento JSON tem limite próprio de **16 MiB**, verificado antes do parse. Entradas excessivas ou inválidas não produzem um catálogo parcial.
10. Carregar sob `QLockFile::tryLock(0)`, sem aguardar outro escritor e sem reparar documentos. Ausência dos dois slots produz catálogo vazio. Primário válido carrega normalmente; primário inválido/ausente com backup saudável permite recuperação. Versão futura em qualquer slot bloqueia edição/gravação pela composição; arquivo indisponível produz erro. Ambos inválidos não autorizam gravar defaults sobre os dados existentes.
11. Antes de substituir um primário válido, salvar seus bytes exatos no backup. Usar `QSaveFile`, fallback de escrita direta desligado, `flush()` e `commit()`. Backup saudável permanece intacto ao salvar uma recuperação de primário inválido. Versão futura em qualquer slot recusa toda gravação, sem opção de sobrescrita nesta entrega.
12. Reconhecer **Saved** somente após os commits exigidos. Injeções antes/depois dos commits de backup/primário e exceções conservam **Failed** quando não há confirmação, inclusive se a nova versão já puder ser observada no disco. Readback ou existência do arquivo não promovem esse resultado a Saved. São testes de falhas delimitadas, sem alegação de durabilidade após perda física de energia.

### Worker e apresentação

13. Executar a porta síncrona em um único job Qt Concurrent por binding, mantendo o store vivo pelo job. A apresentação não faz I/O de documento na thread visual. Enquanto há operação ativa, não acumular mutações concorrentes nem backlog ilimitado.
14. O binding prepara um catálogo candidato e publica a mudança nas listas após **Saved**. Uma falha mantém o candidato disponível para **Tentar salvar**, com os mesmos IDs. A recarga só abandona o candidato em memória depois de validar o catálogo lido. Uma gravação sem confirmação pode já estar no disco e reaparecer na recarga.
15. Aguardar o resultado de um salvamento ativo antes de iniciar a preparação de saída. Uma falha ou o vencimento do prazo cancela a intenção de sair e reabre o painel com a edição preservada para recuperação explícita. Depois de iniciada a preparação, fechar admissão e drenar os jobs. O cancelamento da preparação rearma as ações do painel, e o shutdown definitivo mantém admissão fechada.
16. Projetar IDs como strings na apresentação, renderizar nomes e paths em texto simples e vincular confirmações à identidade escolhida. Mudança da seleção ou do alvo revoga a ação do diálogo, em vez de redirecionar uma exclusão/renomeação. O modal contém foco e suspende atalhos de transporte enquanto recebe entrada de texto.
17. Usar uma instância dedicada do seletor local assíncrono para adicionar arquivos, ligada à mesma janela do player. A seleção captura o ID da playlist e usa a mutação e a confirmação de gravação existentes. Troca de alvo e preparação de saída invalidam o pedido antes de fechar seu diálogo. A instância dedicada mantém o cancelamento independente do seletor da fila.

## Alternativas e consequências

- Reutilizar somente a fila/sessão não oferece várias listas nomeadas e acoplaria suas edições à reprodução atual.
- Arquivos M3U8 são úteis para intercâmbio, mas exigem outra política de paths, entradas inválidas e erros parciais. Serão tratados em uma evolução posterior.
- SQLite, contas e sincronização não são necessários para este catálogo pessoal limitado; o documento único conserva uma transação simples para nomes, IDs, ordem e entradas.
- O backup guarda a versão anterior confirmada pelo fluxo de gravação. Não é um histórico completo de revisões. Paths salvos podem deixar de existir e continuam sujeitos ao tratamento local de arquivo indisponível quando o usuário tentar reproduzi-los.
- O lock e os checks de arquivos são controles cooperativos Qt. Esta decisão não afirma resistência a troca adversarial de ancestrais/reparse, identidade atômica de diretório, filesystem hostil ou concorrência entre edições de snapshots carregados por donos não cooperativos.

## Validação e critérios de revisão

Domínio deve cobrir CRUD, IDs, ordem, duplicatas, Unicode, limites e rejeição transacional de snapshot inválido. Store deve ter round-trip/restart em diretório próprio, IDs acima de `2^53`, backup, versão futura, corrupção, indisponibilidade, lock e falhas antes/depois de commit, mantendo áudios intactos. A ampliação do parser precisa preservar as regressões dos codecs que continuam com profundidade 4.

Binding deve testar publicação somente após Saved, tentativa repetida do mesmo candidato, recarga após resultado ambíguo, fila preservada e preparação de saída. Quick deve exercitar os componentes reais do painel com backend falso: confirmações, identidade obsoleta, teclado/foco, texto simples, duplicatas e geometria mínima. Builds Lite/Completa e grafo de produção têm checks próprios.

As evidências e contagens finais pertencem ao [guia](../../playlists.md#evidência-de-implementação). Fonte implementada e testes determinísticos não comprovam aceitação nativa, filesystem após perda de energia, Linux ou instalador atualizado. Revisar este ADR antes de adicionar M3U8, substituição automática da fila, importação de URLs, outro formato de persistência, sincronização ou limites maiores.

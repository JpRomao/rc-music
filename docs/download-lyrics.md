# Letras associadas ao áudio adquirido

**Estado:** fluxo opcional de consulta, revisão e gravação implementado e validado no recorte automatizado. Downloads de produção no app normal continuam OFF. Runtime YouTube autenticado, host instalado e provedor primário Letras permanecem pendentes.

Este incremento liga um MP3 com publicação reconhecida ao catálogo de [versões locais de letras](lyrics-variants.md). O pedido [YouTube em MP3 com letra](youtube-audio-and-lyrics.md) continua separado da extensão principal. A consulta usa o LRCLIB existente e informa explicitamente **Letras indisponível nesta versão**. Não há parser Letras, leitura de seu player ou nova prova de acesso online neste ensaio.

## Fluxo de uso na composição opcional

1. O pedido YouTube deve conter a opção de buscar letra. Depois de reconhecida a publicação do MP3, a composição oferece a consulta usando o contexto original daquele job. Não usa paths ou metadados reconstruídos de histórico recuperado.
2. Título e artista disponíveis iniciam a busca no LRCLIB. Se faltarem, a revisão pede seu preenchimento; **Buscar letra** permite corrigir esses campos e tentar novamente. A abertura do app ou de um histórico não inicia uma consulta.
3. Em Downloads, **Revisar letras** abre o diálogo da aquisição apresentada. Escolha uma das versões encontradas e confira a prévia em texto simples. Conteúdos sincronizados e sem tempos distintos ficam disponíveis separadamente. A ação **Colar letra** aceita texto simples ou LRC, inclusive quando a busca falha ou não encontra uma versão.
4. Informe o nome e escolha explicitamente **Original** ou **Romanizada**. Idioma e escrita são opcionais. O app não detecta idioma/alfabeto, não converte a escrita e não traduz o texto.
5. **Guardar como nova versão** grava uma letra por vez no catálogo do arquivo publicado. Versões existentes são preservadas. Conteúdo canônico já guardado para a mesma forma não é duplicado.
6. Após a confirmação, outra versão encontrada pode ser guardada. **Fechar** deixa a revisão disponível para reabrir. **Concluir revisão** encerra essa revisão e libera sua reserva de arquivamento. O próximo job elegível pode então aparecer. Resolva qualquer gravação pendente antes de concluir.
7. Para selecionar ou acrescentar uma versão mais tarde, abra o mesmo MP3 e use **Versões** junto à letra. O catálogo fica associado ao arquivo e as versões guardadas continuam disponíveis após arquivar o pedido ou reiniciar o RC Music.

Falha, ausência ou resultado instrumental da letra conservam o MP3 e a importação de áudio. Repetir a consulta não pede novo download de áudio. **Tentar guardar novamente** repete somente a gravação do candidato retido, sem HTTP e sem adquirir áudio novamente.

## Contratos de execução e persistência

`DownloadService::publishedYoutubeAudio` fornece o job, o MP3 com publicação reconhecida e seu contexto original vivo. `WindowsPlayerDownloads` só oferece letra para esse contexto com `fetch_lyrics=true`, uma vez por job durante o lifetime do owner e com uma revisão ativa por vez. Histórico recuperado permanece inerte; o checkpoint não passa a guardar os metadados completos do pedido nem URLs assinadas. Não há retomada automática da consulta após reiniciar.

A conclusão explícita libera a reserva apenas do job daquela revisão. Se outro pedido já estiver com um arquivamento em andamento, a liberação acompanha o job retido para a próxima operação. O arquivamento continua exigindo o salvamento do checkpoint e todos os itens daquele pedido encerrados.

O teste do owner Windows confirmou o arquivamento na mesma sessão, preservou a versão salva no catálogo temporário e recuperou os mesmos IDs no reenvio. Outra regressão confirmou a revogação da confirmação de arquivamento quando uma segunda revisão foi concluída. Passaram o build Release, lint QML, **191 casos do núcleo**, **3/3 CTest** e **64 casos Qt Quick** dos dois painéis. Logs em `out/product/test-runs/finish-download-lyrics-review-20261111/`.

`DownloadLyricsLookup` usa um `QtLyricsService` exclusivo, separado da letra da faixa em reprodução. `setLookupTrack` configura somente metadados: não abre o áudio nem lê um `.lrc` irmão. O mesmo serviço permanece entre consultas, preservando cooldown, limites e cancelamento; resultados antigos são revogados por geração. A página Letras não é consultada enquanto esse adapter estiver indisponível. A origem dos candidatos deste incremento é LRCLIB.

`DownloadLyricsBinding` conserva a identidade do alvo e o `requestToken` da revisão. Ações antigas são revogadas quando a identidade muda. Para versões encontradas, o diálogo também confere os metadados usados na busca. O texto colado passa pelo mesmo parser LRC e recebe origem manual no catálogo. Seus tempos, quando fornecidos, são preservados.

O alvo é validado antes de aparecer no diálogo. Cancelar a saída restaura a edição manual da revisão válida e mantém a busca parada até uma nova ação do usuário.

`LyricsBinding::appendAcquiredLyrics` compartilha **o mesmo writer** das edições manuais. A reserva cobre load → merge → save; nenhuma edição manual ou outra aquisição atravessa essa operação. O merge puro preserva o catálogo existente e atribui IDs locais novos. A seleção atual só é atualizada para o alvo correspondente depois de `Saved`; resultado de outra faixa não é aplicado à faixa atual.

Falha de leitura preserva drafts e alvo para nova tentativa. Após preparar um candidato, falha de gravação conserva o catálogo candidato exato e seu ticket; retry não repete o merge. Recarregar versões exige abandono explícito da alteração em memória e não promete desfazer um commit físico cuja confirmação falhou. Versão futura/indisponibilidade do catálogo bloqueia a gravação. Preparação de saída fecha nova admissão e espera os recursos e alterações pendentes; cancelar a saída rearma as ações.

O catálogo mantém **schema 1**, path exato, lock cooperativo, backup e os limites do ADR-0016: até 16 versões, 128 bytes por nome, 64 por idioma/escrita, 2048 por origem, 256 KiB por conteúdo e 4 MiB somados. Compartilhar o writer serializa as alterações desta composição; não acrescenta CAS ou uma transação de merge entre processos. Áudio, tags, `.lrc` irmão, fila e seleção de reprodução são preservados.

## Evidência deste incremento

Os números abaixo são os resultados proporcionais já comunicados pelo responsável pelos checks. Suites combinadas incluem componentes da tabela; não somar os totais como testes independentes.

| Componente | Evidência disponível |
| --- | --- |
| Merge puro adquirido | 21 casos / 253 assertions |
| Writer compartilhado `LyricsBinding` | 24 casos Qt |
| `QtLyricsService`, lookup sem `.lrc` e conteúdos distintos | 26 casos Qt |
| `DownloadLyricsLookup` | 30 casos Qt |
| `DownloadLyricsBinding` | 23 casos Qt |
| Owner `WindowsPlayerDownloads` | 11 casos Qt, incluindo FIFO automático após dispensar revisão |
| Core combinado | 304 casos / 7.138 assertions |
| Downloads puro | 170 casos / 4.338 assertions |
| Quick do diálogo e painel | 356 casos Qt, incluindo os 18 novos testes do diálogo |
| CTest integrado, builds e formato | 7/7 CTest, 494 Qt sem falhas/skips; Release Lite/Completa e lint; 23 C++/3 QML |

**Recibos finais:** `out/product/test-runs/youtube-lyrics-20261105/`. Core sem Qt passou os mesmos 304 casos/7.138 assertions. Lite e Completa `BUILD_TESTING=OFF` listaram zero testes. Documentação passou com 96 Markdown/86 requisitos/17 checks/82 etapas; rastreabilidade passou 44 testes, mantendo estados 14 planejados/70 em andamento/2 comprovados. O pacote/instalador não foi atualizado.

TDD: merge teve Red 2 casos passados/19 falhos (144 assertions passadas/30 falhas), serviço 17/9, writer 18/6, lookup 2/28, view 3/20, getter de publicação 2/1 e owner 7/3; números Qt são passagens/falhas. O diálogo teve Red 2/18. No Green, um teste de semáforo foi corrigido para observar seu sinal uma vez, sem consumir duas vezes; a execução Quick expôs um binding loop real ao revogar o pedido. O fechamento foi adiado e guardado, mantendo ações antigas bloqueadas imediatamente; os 18 testes permaneceram intactos e a suíte completa terminou verde. Isso não transfere resultados das composições falsas para prova de internet, sandbox ou instalação.

A jornada de composição usa catálogo real somente em TEMP próprio, com rede, resolver e publicação falsos. Ela separa confirmação do MP3, importação e gravação de letra, e verifica preservação de fila/áudio e resultados recuperados inertes. Não equivale a um download YouTube público ou a um novo ensaio online do LRCLIB; a consulta LRCLIB está implementada em código real, mas sua rede foi substituída nos testes novos.

Não foram executados yt-dlp/Node como runtime, mídia pública, perfis AppContainer, UAC/WFP, Registro, setup, GUI nativa ou benchmarks. Runtime/host autenticados e instalados, Letras primário acessível com contrato compatível, aquisição pública, aceitação Brave/Windows e distribuição continuam gates próprios. LLM, tradução, romanização automática e sincronização inventada permanecem fora do fluxo. Os ADRs [0016](architecture/decisions/0016-lyrics-variants.md) e [0017](architecture/decisions/0017-youtube-audio-and-lyrics-acquisition.md) conservam seus estados distintos; este recorte não aprova o runtime ou encerra o plano integral.

### Inclusão manual na revisão

O Red confirmou a ausência da ação em três casos do binding e dois do painel. Uma regressão adicional reproduziu a perda da edição manual ao cancelar a saída durante a busca inicial. O fechamento passou **4/4 alvos CTest com 111 casos Qt** e **90 casos Quick** nas quatro suítes de downloads e letras, sem falhas ou skips. Build Release, `all_qmllint` e formato passaram. Os testes usaram rede e catálogo de teste para conferir letras japonesas e romanizadas, tempos de LRC, falha de consulta, deduplicação, retry da escrita e proteção do alvo. Recibos em `out/product/test-runs/download-lyrics-paste-20261111/`.

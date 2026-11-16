# ADR-0019: Busca interna pelo yt-dlp, biblioteca permanente e retirada futura da extensão

**Estado:** Aceito

**Data:** 2026-11-16

**Relaciona-se a:** [ADR-0006](0006-browser-editions-and-extensions.md) (extensões), [ADR-0017](0017-youtube-audio-and-lyrics-acquisition.md) (aquisição YouTube) e [ADR-0018](0018-aurora-and-lidarr-removal.md) (Aurora).

## Contexto

O plano de reformulação Aurora deixou três decisões com o mantenedor: de onde vêm os resultados da busca online, se a proposta de biblioteca permanente seria adotada e o destino da extensão de navegador quando a busca interna existir. Em 2026-11-16 o mantenedor respondeu:

> Siga com a busca do próprio yt-dlp como recomendado. A proposta de biblioteca permanente, aprovada, com a biblioteca abrindo em Lista. Sobre a extensão: descarte quando não for mais necessária por já termos funcionando tudo no app.

## Decisão

1. **Provedor da busca online: o próprio yt-dlp, só metadados.** O resolvedor empacotado ganha um modo de busca separado do modo de resolução, que devolve resultados tipados (ID do vídeo, título, canal, duração), sem URLs de mídia, cookies ou cabeçalhos. A seleção do usuário vira um pedido comum de aquisição por ID, pelo pipeline existente. Não há chave, conta, cota nem custo. `ytsearch:` nunca entra no campo de ID de vídeo.
2. **Escopo inicial da busca:** músicas/vídeos individuais com seleção explícita e lista de resultados limitada; playlists e canais ficam para depois. A mesma busca vale para as duas edições, sem depender do WebView2.
3. **Biblioteca permanente aprovada como proposta** em `out/product/uiux-planning-4efd8ba/BIBLIOTECA-PERMANENTE.md`: catálogo próprio `library.json` (v1), independente da fila, das playlists e do histórico. Um registro por caminho exato. Sem varrer ou monitorar o disco. Remover da biblioteca nunca apaga o áudio. Itens indisponíveis ficam marcados, não somem. O registro só aparece como salvo após `Saved`.
4. **A página Biblioteca abre em Lista**, com a visão em Capas como alternativa.
5. **Extensão de navegador:** continua opcional até que buscar, baixar, publicar e importar funcionem inteiramente no app. Depois disso, extensões, host de Native Messaging, IPC dedicado e o empacotamento correspondente são removidos.

## Revisão de 2026-11-16: a extensão fica

Depois do teste real da busca no app, o mantenedor mudou o item 5:

> A extensão será colocada mais para frente com o intuito de pegar mídias fora do YouTube e do Letras.

O item 5 deixa de valer. A extensão não será retirada. Ela continua opcional e passa a ter outro papel: capturar mídias de sites que não são o YouTube nem o Letras, numa frente futura. YouTube e Letras ficam no app, pela busca interna e por links colados. Os gates do ADR-0006 continuam valendo para a extensão.

## Alternativas

- **YouTube Data API:** exige chave e cota diária baixa, e seus termos não permitem usar os resultados para download. Rejeitada.
- **Adiar a busca online:** manteria a extensão como único caminho para obter músicas. Rejeitada.
- **Biblioteca como projeção da fila/playlists:** não satisfaz a permanência pedida. Rejeitada.
- **Retirar a extensão já:** removeria o único caminho de aquisição antes de o substituto existir. Rejeitada.

## Consequências

- A busca usa a mesma ferramenta que já baixa o áudio, então não muda a posição legal do produto em relação ao YouTube, mas herda a fragilidade do yt-dlp. Atualizar o yt-dlp sem recompilar o app passa a ser prioridade.
- A biblioteca e a busca exigem planos técnicos próprios, com testes determinísticos antes de qualquer adapter real, e persistência com recuperação de falhas.
- A retirada da extensão foi cancelada na revisão acima; a extensão segue sob os gates do ADR-0006.

## Critérios de revisão

Rever o provedor se o yt-dlp deixar de oferecer busca utilizável ou se o uso de rede do modo de busca violar as políticas de isolamento do resolvedor. Rever a retirada da extensão se a jornada interna não cobrir algum site que o mantenedor ainda use.

# YouTube em MP3 e letra com fallback

**Estado:** cliente e codec do ingresso separado implementados; aquisição de produção e integração Letras ainda pendentes.

O mantenedor definiu áudio do YouTube em MP3 e Letras.mus.br como fonte preferencial de letra, com outro provedor como fallback. O [ADR-0017](architecture/decisions/0017-youtube-audio-and-lyrics-acquisition.md) registra o desenho, a pesquisa e os gates. O fallback proposto é o LRCLIB já usado pelo RC Music.

## Resultado esperado

Na extensão YouTube separada, selecionar o vídeo, conferir título/artista e escolher **Baixar MP3 + letra**. O áudio deve ser adquirido sem a imagem do vídeo quando houver faixa separada, convertido quando necessário e validado como MP3. A letra deve ser associada ao arquivo final, com origem explícita e preservação das formas disponíveis. Falha na letra conserva o áudio e permite tentar somente a letra depois. Nada começa apenas por abrir o popup; importação não inicia reprodução.

## Limites atuais

O primeiro cliente trata o vídeo da aba ativa, não toda uma playlist do YouTube. O contrato admite lote limitado para a evolução posterior. A principal permanece sem YouTube. O host lógico YouTube é independente, ainda sem executável/registro/configuração instalada; ausência da ponte não é download concluído nem ACK fabricado.

O app ainda não possui resolver/execução YouTube no pipeline normal. `yt-dlp` e `ffmpeg` foram encontrados no ambiente, sem execução/instalação neste incremento e sem aprovação para empacotamento. Não foi baixado áudio público. Os gates anteriores de produção remota permanecem abertos.

A consulta do exemplo do Letras foi bloqueada por HTTP 403, e não foi confirmado contrato de API/extração compatível. Não há parser Letras de produção nem comprovação das variantes dessa página. O app atual continua usando LRCLIB, com busca explícita; a cadeia Letras → LRCLIB e sua associação ao download ainda precisam ser compostas. Não tratar retorno LRCLIB como Letras. LLM, tradução e sincronização inventada continuam fora do fluxo.

## Evidência do ingresso separado

Recibos locais em `out/product/test-runs/youtube-audio-20261104/`. O Red Node passou **5 casos e falhou 18**, antes do comportamento de policy/ponte/controller. O Red Qt passou **38 casos e falhou 26**, com stubs vazios. Depois, o novo teste de montagem DOM real com browser falso teve Red **35/1**; o clique e a renderização foram implementados sem remover suas assertions.

Green final: **203/203 testes Node** de ambas as extensões, incluindo **36** do pacote YouTube, e sintaxe dos **10 módulos JavaScript**. O CTest passou **2/2**, com **66 casos Qt** do novo codec e **188** da regressão do codec principal, sem falhas/skips. O corpus compartilhado tem **8 pedidos e 4 respostas**, consumidos em Node e Qt; cobre Unicode, protocolo separado, host Letras, segmentos ambíguos, tipos e ACK parcial. Não há dependências npm novas.

O limite foi alinhado a **20 itens**, conforme a capacidade existente do serviço comum; não se admite um lote maior para produzir ACK parcial. O teste da principal continua recusando YouTube, `youtu.be`, `youtube-nocookie` e `googlevideo`. Os arquivos do pacote principal foram preservados por hash; nenhum bloqueio dele foi retirado.

O build Release compila o alvo independente de codec/teste. Ele não está ligado ao executável do player, não é um host nativo e não carrega yt-dlp/FFmpeg. Os testes usam Chrome/Native Messaging falsos e não consultam YouTube ou Letras. Não foram executados setup, navegador, Registro, perfis AppContainer, UAC, download de mídia pública ou benchmarks. Os seis arquivos AppContainer em andamento e os cinco artefatos congelados anteriores foram preservados.

Esse recorte não é aquisição YouTube funcional, fallback Letras implementado, instalador atualizado ou aceitação Brave nativa. Continuar por resolver/runtime e composição protegida com aquisição/conversão/importação, depois cadeia de letras/associação e integração instalada; acesso Letras e demais gates de FR-050/FR-052 continuam abertos. O objetivo completo não está concluído.

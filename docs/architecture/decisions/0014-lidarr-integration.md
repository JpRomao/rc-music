# ADR-0014: integração opcional com Lidarr

**Estado:** Aceito

**Data:** 2026-11-04

**Decisão de escopo:** o mantenedor pediu uma integração com Lidarr, colocou-a como prioridade e confirmou pesquisa e solicitação de downloads. Indicou sua instância `http://localhost:8686/`. A aceitação corresponde ao escopo e à ordem de desenvolvimento; implementação, checks e conexão com a instância real têm evidências próprias.

## Contexto

O mantenedor quer pesquisar músicas pelo RC Music. O Lidarr permite consultar artistas e lançamentos e coordenar buscas nos indexadores e clientes de download configurados pelo usuário. Seu modelo se concentra em artistas, álbuns e releases; uma pesquisa no catálogo não representa disponibilidade de um arquivo de áudio nem reprodução imediata.

O pedido autoriza esta integração opcional específica além dos serviços remotos anteriormente delimitados. Ele não aceita outros provedores nem altera os gates do [ADR-0003](0003-local-first-and-remote-sources.md), das [extensões e edições](0006-browser-editions-and-extensions.md) ou do [worker AppContainer](0011-media-worker-appcontainer.md). O trabalho existente nesses recortes permanece preservado.

## Decisão

1. Priorizar uma integração com uma instância de Lidarr indicada pelo usuário, sem instalar, iniciar ou configurar o servidor automaticamente. Usar sua API HTTP documentada, sem scraping, shell, navegador interno ou incorporação do runtime Lidarr.
2. Disponibilizar a mesma integração opcional nas edições Lite e Completa. Reprodução local, fila, preferências e letras devem continuar utilizáveis sem servidor ou rede; a Lite não recebe uma engine de navegador por causa dessa integração.
3. Solicitar endereço e chave de API no próprio RC Music, oferecendo `http://localhost:8686/` como endereço inicial. Na primeira integração, conservar a chave somente em memória durante a execução, sem arquivo, preferência ou log. Enviá-la apenas no cabeçalho `X-Api-Key` para a instância escolhida, nunca em URLs, termos de pesquisa, resultados ou mensagens de erro.
4. Separar pesquisa de catálogo, presença na biblioteca e pedido de download. O resultado deve identificar artista/álbum e a ação oferecida, sem apresentar metadata como arquivo já baixado. As rotas e os DTOs concretos pertencem ao adapter.
5. Consultar o servidor somente por ação explícita. Abrir o painel, iniciar o player, trocar de faixa ou receber uma resposta antiga não autoriza pesquisa, adição à biblioteca ou download.
6. Qualquer operação que adicione conteúdo ou solicite busca/download deve corresponder à seleção e à confirmação vigentes. Para um álbum novo, exigir escolhas válidas de diretório, perfil de qualidade e perfil de metadata retornados pelo servidor antes de pedir a adição e a busca daquele álbum. Para um álbum já cadastrado, solicitar `AlbumSearch` por ID exato. Ao cadastrar o artista necessário, usar `monitorNewItems: "none"` e não enviar `artist.addOptions`; somente o álbum selecionado recebe `monitored: true` e `addOptions.searchForNewAlbum: true`. Não transformar uma seleção de álbum em pedido de discografia inteira. O RC Music não inventa valores nem presume indexadores ou clientes de download prontos.
7. Distinguir aceitação do pedido de sua execução ou conclusão. Esta versão informa a aceitação e orienta acompanhar resultados e downloads no Lidarr; um comando aceito não comprova que encontrou ou baixou o álbum. Timeout ou falha de comunicação não deve disparar repetição automática de uma mutação.
8. Não importar, executar, apagar ou reproduzir automaticamente caminhos devolvidos pelo servidor. A integração não concede autoridade sobre arquivos do host, não altera a fila atual e não reutiliza caminhos remotos como capabilities do pipeline de downloads.
9. Manter transporte, autenticação, limites e validação de respostas no adapter; projetar texto/estado e intenções pela apresentação. O núcleo de reprodução permanece independente de Qt Network e do schema do Lidarr.
10. Limitar cada pedido HTTP a **10 s**, resposta a **4 MiB** e lista a **500 resultados**; cancelar consultas obsoletas e respeitar o ciclo de saída do app. A conexão carrega estado e opções em quatro pedidos sequenciais. Aceitar uma base HTTP ou HTTPS configurada explicitamente, inclusive instância na rede local. Informar que HTTP não criptografa a conexão e recomendar HTTPS fora do próprio computador. Não seguir redirecionamentos automaticamente nem ignorar falhas TLS. A chave não deve ser encaminhada para outro destino por resposta do servidor.

### Rotas da primeira integração

Todas as rotas abaixo pertencem à base `/api/v1/` da instância configurada. Os recursos e perfis são dados do servidor; não adicionar uma API pública de catálogo diretamente ao player.

| Ação | API Lidarr |
|---|---|
| Conectar e conferir disponibilidade | `GET system/status` |
| Carregar escolhas para uma adição | `GET rootfolder`, `GET qualityprofile`, `GET metadataprofile` |
| Pesquisar catálogo de artistas | `GET artist/lookup?term=...` |
| Pesquisar catálogo de álbuns | `GET album/lookup?term=...` |
| Adicionar um álbum escolhido e solicitar busca | `POST album`, com o artista/perfis/diretório e `addOptions.searchForNewAlbum: true` |
| Solicitar busca de álbum já cadastrado | `POST command`, com `name: "AlbumSearch"` e `albumIds` contendo somente o ID escolhido |

Conexão e consultas usam operações assíncronas. A interface apresenta **Busca solicitada no Lidarr** quando há aceitação do pedido; downloads continuam sob gestão do servidor e de seu cliente externo. Esta entrega não transfere bytes de mídia para o RC Music, importa resultados ou inicia autoplay. A API de lookup usada pesquisa artistas/álbuns, sem busca global de faixas pelo título; a UI não deve prometer encontrar qualquer música individual ou baixar uma faixa isolada de um álbum.

## Alternativas e consequências

- Consultar diretamente MusicBrainz atende a pesquisa de metadata, mas não integra a biblioteca e os pedidos coordenados pelo Lidarr escolhidos pelo mantenedor.
- Abrir apenas a interface web do Lidarr reduz implementação, mas mantém a pesquisa fora do RC Music e pode exigir o navegador que a edição Lite pretende dispensar.
- Incluir o servidor Lidarr no instalador acrescentaria runtime, operação contínua e configuração de indexadores/clientes sem necessidade para uma integração com instância existente.
- O RC Music depende da disponibilidade, versão e configuração do servidor escolhido. Artistas e lançamentos ausentes no catálogo ou arquivos ausentes nos indexadores precisam de resultado acionável, sem promessa de cobertura universal.
- A integração via API não incorpora o código do Lidarr nem implica distribuir seu servidor. Distribuição, licenças, TLS e notices do RC Music mantêm suas verificações; a disponibilidade de uma API não concede direitos sobre os áudios encontrados.

## Validação e critérios de revisão

O [guia da integração](../../lidarr-integration.md) deve registrar o comportamento efetivamente implementado e os checks. Testes determinísticos devem usar transporte falso para endereço/chave inválidos, autenticação, respostas malformadas/excessivas, timeout, cancelamento e respostas obsoletas. A UI deve testar envio explícito, texto simples, teclado, seleção e indisponibilidade sem afetar áudio local. Operações de escrita exigem testes de confirmação e de resultado ambíguo, sem retry automático.

Builds e grafos Lite/Completa devem comprovar que não houve engine ou servidor acrescentado à Lite. Uma conexão com Lidarr real é um smoke separado, com instância e ações identificadas; testes falsos não comprovam catálogo, indexadores, download ou importação física. Checks verdes de um incremento não concluem os requisitos globais, release ou os gates remotos anteriores.

Revisar este ADR antes de persistir credenciais, automatizar pesquisas ou downloads, instalar o servidor, aceitar arquivos remotos na fila, adicionar outro provedor ou alterar a política de endpoint. O escopo de pesquisa e aquisição permanece limitado às ações aceitas e expostas ao usuário.

## Referências

- [Documentação oficial da API Lidarr](https://lidarr.audio/docs/api/).
- [OpenAPI mantido no repositório Lidarr](https://github.com/Lidarr/Lidarr/blob/develop/src/Lidarr.Api.V1/openapi.json).
- [Guia oficial de configuração e primeira busca](https://wiki.servarr.com/lidarr/quick-start-guide).
- [Adição de álbum e cadastro do artista no servidor](https://github.com/Lidarr/Lidarr/blob/develop/src/NzbDrone.Core/Music/Services/AddAlbumService.cs).
- [Tratamento de monitoramento após varredura do artista](https://github.com/Lidarr/Lidarr/blob/develop/src/NzbDrone.Core/Music/Handlers/ArtistScannedHandler.cs).

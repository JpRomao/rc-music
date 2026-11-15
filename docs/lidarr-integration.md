# Integração opcional com Lidarr

**Estado:** histórico; integração retirada do escopo do aplicativo por decisão do mantenedor em 2026-11-15. FR-055 permanece **fora de escopo** conforme o [ADR-0018](architecture/decisions/0018-aurora-and-lidarr-removal.md), que substitui o ADR-0014.

**Aviso de preservação:** as seções e âncoras abaixo descrevem exclusivamente o fluxo anterior e suas evidências. Não são instruções de uso ou validação da versão sem Lidarr, não representam Green atual e não deixam pesquisa autenticada/download real como próxima tarefa. Nenhuma chave, servidor, pedido remoto ou arquivo pessoal precisa ser lido, alterado ou removido. Outros consumidores de Qt Network e os fluxos de letras/downloads/playlists permanecem independentes.

O [ADR-0014](architecture/decisions/0014-lidarr-integration.md) registra o escopo originalmente aceito. A integração era compartilhada pelas edições Lite e Completa, conectando o RC Music a uma instância Lidarr configurada pelo usuário. O player não instalava nem iniciava o servidor. Runtimes/setups antigos conservam sua própria identidade; este registro não os atualiza ou requalifica.

## Fluxo de produto

1. Informar no RC Music o endereço da instância e sua chave de API. O endereço inicial é `http://localhost:8686/`, indicado pelo mantenedor.
2. Conectar e receber estado acionável de autenticação ou disponibilidade.
3. Pesquisar artistas/álbuns por uma ação explícita e selecionar um resultado.
4. Selecionar um álbum e confirmar separadamente o pedido no Lidarr. Se ele ainda não estiver cadastrado, escolher diretório e perfis existentes no servidor para adicionar e buscar somente esse álbum; se já existir, solicitar sua busca pelo ID.
5. Acompanhar o resultado sem interromper a reprodução local ou interpretar comando aceito como download concluído.

Esse fluxo descreve a integração histórica, sem alegação de validação autenticada com servidor real. **Busca solicitada no Lidarr** confirmava a aceitação de um pedido, sem afirmar que houve resultado, download ou importação. A primeira integração não transferia o áudio para o RC Music nem alterava a fila ou iniciava reprodução.

Uma consulta concluída sem correspondências exibe **Nenhum resultado para esta busca** e permite outra pesquisa. O painel identifica álbuns de catálogo e álbuns já cadastrados, inclusive no nome acessível do resultado. A confirmação explica se o Lidarr vai cadastrar o álbum ou buscar o cadastro existente. Se a consulta ou a conexão mudar enquanto a confirmação estiver aberta, escolha o álbum novamente.

**Ver álbuns cadastrados** lista os álbuns de um artista já registrado na instância. Selecione um álbum para abrir **Ver faixas**, filtrar pelo título e ver se o Lidarr indica um arquivo para a faixa. Feche a lista para voltar ao álbum selecionado e solicitar a busca, se desejar.

## Conexão e API

A conexão explícita consulta `system/status` e carrega `rootfolder`, `qualityprofile` e `metadataprofile` sob `/api/v1/`. A pesquisa usa `artist/lookup` ou `album/lookup` com o termo informado. Os resultados e as escolhas são ligados à conexão e à consulta vigentes.

A navegação da biblioteca consulta `album?artistId=…` e `track?albumId=…` apenas ao acionar o controle de um resultado cadastrado. O filtro de títulos roda no RC Music, e os dados de arquivo da API são reduzidos à indicação de presença no Lidarr. [Álbuns na API Lidarr](https://github.com/Lidarr/Lidarr/blob/develop/src/Lidarr.Api.V1/Albums/AlbumController.cs) · [Faixas na API Lidarr](https://github.com/Lidarr/Lidarr/blob/develop/src/Lidarr.Api.V1/Tracks/TrackController.cs)

O termo enviado preserva sinais de mais e sequências literais como `%2B` nos nomes pesquisados.

O endereço pode usar HTTP ou HTTPS e apontar para outra máquina da rede. A integração usa HTTP/1.1, inclusive sobre TLS. O servidor ou proxy precisa aceitar esse protocolo. HTTP não criptografa a conexão, incluindo a chave enviada ao servidor. Prefira HTTPS fora do próprio computador. Erros de certificado HTTPS são recusados, sem botão para ignorá-los. Redirecionamentos não são seguidos automaticamente.

Para um álbum novo, o pedido `POST album` inclui as escolhas válidas e `addOptions.searchForNewAlbum: true`. Para um álbum já cadastrado, `POST command` recebe `name: "AlbumSearch"` e somente o ID escolhido em `albumIds`. A adição de um álbum pode cadastrar o artista correspondente no Lidarr; a confirmação precisa deixar essa consequência clara. O artista necessário usa `monitorNewItems: "none"`, sem `artist.addOptions`, para não solicitar a busca da discografia inteira ou monitorar automaticamente os outros álbuns novos.

As escolhas de pasta/perfis são usadas no cadastro de um artista novo. O Lidarr conserva as configurações de artistas já existentes; adicionar um álbum não move seus arquivos nem substitui essas preferências.

## Catálogo, biblioteca e downloads

O Lidarr consulta metadata de artistas e lançamentos e procura releases nos indexadores configurados. O download é encaminhado ao cliente configurado no servidor. A pesquisa de catálogo recebe nome de artista ou álbum. A pesquisa por título de faixa se aplica à lista de um álbum cadastrado. Os pedidos são feitos por álbum completo.

Perfis, diretório de biblioteca, indexadores e clientes pertencem à configuração do Lidarr. O RC Music deve informar o que falta e usar valores existentes escolhidos pelo usuário, sem cadastrar fontes ou preencher paths fictícios. A [documentação oficial de início rápido](https://wiki.servarr.com/lidarr/quick-start-guide) explica essa configuração.

## Privacidade e limites

- Endereço e chave são fornecidos no próprio app. A chave fica somente em memória nesta primeira versão e precisa ser informada novamente em outra execução. É enviada no cabeçalho `X-Api-Key`, sem aparecer em URLs, arquivos de preferências, diagnósticos, resultados ou mensagens de erro.
- Respostas HTTP 401 e 403 encerram a sessão e descartam a chave, os resultados e os perfis carregados. O painel conserva o endereço para uma nova conexão explícita. Para tentar outro pedido, faça uma nova busca, selecione o álbum e confirme.
- Pesquisa e mutações exigem ação explícita; não há envio automático ao abrir o painel ou trocar de música.
- Os resultados são texto simples e dados de catálogo, sem execução de HTML ou conteúdo remoto.
- Um timeout de escrita pode deixar o resultado desconhecido; não autoriza repetir automaticamente o comando.
- Cada pedido HTTP tem prazo de **10 s**, resposta limitada a **4 MiB** e lista limitada a **500 resultados**. A conexão faz quatro pedidos sequenciais para estado e opções; a interface permanece assíncrona. Não há redirecionamento automático ou exceção de validação TLS; o uso de HTTP deve ser informado ao conectar.
- Caminhos retornados pelo servidor não autorizam abrir, importar ou apagar arquivos do Windows. Fila e reprodução permanecem sob os casos de uso locais existentes.
- O player deve continuar utilizável quando Lidarr ou rede estiverem indisponíveis. Esta integração não habilita a extensão, o navegador interno ou o pipeline remoto de produção.

## Evidência de implementação

**Evidência histórica anterior à retirada.** As contagens, falhas e recibos seguintes não foram reexecutados nem promovidos a qualificação do app sem Lidarr. Os testes exclusivos foram retirados com a capacidade; FR-055 possui verificação documental e decisão explícita, sem vínculos a arquivos de teste removidos.

Evidências em `out/product/test-runs/lidarr-integration-20261104/`. Red inicial do serviço **3 passagens/30 falhas**, binding **3/3** e UI recusada por propriedade ainda ausente; os stubs foram substituídos depois dessas execuções. A regressão da seleção pública consumida teve **32/1 → 33/33 Qt**. O teste de timeout expôs um erro no oráculo: o reply já havia recebido DeferredDelete; o teste passou a observar o abort por um registro que sobrevive ao objeto, conservando as exigências de timeout e ausência de retry. O binding terminou **8/8 Qt**, incluindo serviço real sobre QNAM falso, projeção e POST exato.

A UI terminou **12/12 Qt Quick** no arquivo dedicado: confirmação, seleção obsoleta, campos mascarados, texto simples também nas opções do servidor, header com opcionais e geometria mínima **720 × 520**. O primeiro teste de opções falhou por TextFormat ausente; a troca por conteúdo/delegates PlainText corrigiu o caso. O teste de seleção obsoleta encontrou um binding loop; a confirmação agora conserva seu snapshot e desabilita o envio com aviso, sem mutar o snapshot durante o cálculo. Ajustes de espera de delegates corrigiram o harness sem enfraquecer os contratos.

O fechamento passou **263/263 CTest**, sem falhas/skips, em **63,69 s**: **254 testes de core**, serviço/binding Lidarr, letras, presenter, toda a suíte Quick e três jornadas de processo (preferências, sessão e startup). Quick passou **303/303**, incluindo os 12 casos dedicados; letras conservaram **17/17 serviço e 6/6 binding**. Testes de processo usam executável instrumentado/offscreen e temporários próprios, sem instalação ou interação GUI nativa. Isso não aprova acessibilidade nativa ou integração instalada.

Builds Release Lite com testes ON/OFF e Completa experimental OFF passaram; `all_qmllint` terminou sem warnings. Formato de **7 C++ alterados e todos os 67 QML** passou, preservando os seis arquivos AppContainer preexistentes. Os grafos Lite ON/OFF têm **304/275 comandos**, com cliente/binding/painel Lidarr e sem engine; Completa OFF tem **309**, incluindo sua engine existente. Ambos os builds OFF registram zero CTest. Os novos componentes não puxam testes, helpers de Registro ou WFP para o produto e não adicionam um runtime Lidarr.

A conferência real separada enviou somente `GET http://localhost:8686/api/v1/system/status`, sem chave: o servidor respondeu **401**. Isso comprova alcance do endpoint e exigência de autenticação, sem comprovar versão, catálogo, configuração ou downloads. Nenhuma chave foi extraída de arquivos pessoais; não houve POST no servidor real, importação, mudança da fila pessoal, instalação ou regeneração de instaladores. Naquele marco, a aceitação autenticada dependeria de chave informada no próprio app; não foi demonstrada por esse GET e não é etapa a continuar após a retirada.

O contrato anterior exigia respostas próprias e transporte falso nos testes automáticos, sem internet ou instância pessoal. O smoke autenticado previsto não foi concluído e deixa de ser tarefa vigente com a retirada. Instalação, alteração de configuração no servidor e download real não foram autorizados por uma simples validação do cliente.

Em 2026-11-11, os testes reproduziram a mensagem genérica para busca vazia, a falta de distinção nos resultados e o envio indevido de uma confirmação a um serviço substituto. Passaram **34 casos do serviço, 9 do binding e 13 Qt Quick** do painel, além do build Release e lint QML. Os casos usam respostas sintéticas. A instância local respondeu 401 sem chave e a aceitação autenticada continuou não demonstrada naquele marco. Logs em `out/product/test-runs/playlist-keyboard-lidarr-results-20261111/`.

Cinco regressões reproduziram recusas de acesso durante busca ou pedido de álbum, incluindo um 403 com resposta acima do limite. Após a correção, passaram **40 casos Qt do serviço**, o binding e **13 casos Qt Quick** do painel. Logs em `out/product/test-runs/lidarr-auth-recovery-20261111/`.

Na regressão de teclado, End deixou o cursor no segundo de 40 resultados. O Green passou em **14/14 Qt Quick** do Lidarr, incluindo navegação até uma linha fora da área visível, preservação do cursor pela chave após reordenação, seleção por teclado e passagem de foco para o botão de solicitação. O build Release do alvo Quick, o lint e o formato dos QML alterados passaram. Os testes usam um serviço simulado. Logs em `out/product/test-runs/lidarr-keyboard-results-20261111/`. [Aviso da retirada no manual](user-guide.md#pesquisar-e-solicitar-álbuns-no-lidarr)

A navegação da biblioteca passou em **43 casos Qt do serviço**, **9 do binding** e **16 Qt Quick**. A resposta precisa corresponder ao artista ou álbum selecionado. Os testes verificaram a revogação após mudança de contexto, a ausência de caminhos de arquivos na projeção e a confirmação separada do álbum após consultar suas faixas. Logs em `out/product/test-runs/lidarr-library-tracks-20261111/`.

## Referências

- [API oficial Lidarr](https://lidarr.audio/docs/api/).
- [Schema OpenAPI no repositório oficial](https://github.com/Lidarr/Lidarr/blob/develop/src/Lidarr.Api.V1/openapi.json).

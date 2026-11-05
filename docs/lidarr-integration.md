# Integração opcional com Lidarr

**Estado:** prioridade de desenvolvimento confirmada pelo mantenedor em 2026-11-04; implementação e validação em andamento.

O [ADR-0014](architecture/decisions/0014-lidarr-integration.md) registra o escopo aceito. A integração será compartilhada pelas edições Lite e Completa, conectando o RC Music a uma instância Lidarr configurada pelo usuário. O player não instala nem inicia o servidor.

## Fluxo de produto

1. Informar no RC Music o endereço da instância e sua chave de API. O endereço inicial é `http://localhost:8686/`, indicado pelo mantenedor.
2. Conectar e receber estado acionável de autenticação ou disponibilidade.
3. Pesquisar artistas/álbuns por uma ação explícita e selecionar um resultado.
4. Selecionar um álbum e confirmar separadamente o pedido no Lidarr. Se ele ainda não estiver cadastrado, escolher diretório e perfis existentes no servidor para adicionar e buscar somente esse álbum; se já existir, solicitar sua busca pelo ID.
5. Acompanhar o resultado sem interromper a reprodução local ou interpretar comando aceito como download concluído.

Esse fluxo descreve a integração em desenvolvimento, sem alegação de validação com servidor real. **Busca solicitada no Lidarr** confirma a aceitação de um pedido, sem afirmar que houve resultado, download ou importação. A primeira integração não transfere o áudio para o RC Music nem altera a fila ou inicia reprodução.

## Conexão e API

A conexão explícita consulta `system/status` e carrega `rootfolder`, `qualityprofile` e `metadataprofile` sob `/api/v1/`. A pesquisa usa `artist/lookup` ou `album/lookup` com o termo informado. Os resultados e as escolhas são ligados à conexão e à consulta vigentes.

O endereço pode usar HTTP ou HTTPS e apontar para outra máquina da rede. HTTP não criptografa a conexão, incluindo a chave enviada ao servidor; prefira HTTPS fora do próprio computador. Erros de certificado HTTPS são recusados, sem botão para ignorá-los. Redirecionamentos não são seguidos automaticamente.

Para um álbum novo, o pedido `POST album` inclui as escolhas válidas e `addOptions.searchForNewAlbum: true`. Para um álbum já cadastrado, `POST command` recebe `name: "AlbumSearch"` e somente o ID escolhido em `albumIds`. A adição de um álbum pode cadastrar o artista correspondente no Lidarr; a confirmação precisa deixar essa consequência clara. O artista necessário usa `monitorNewItems: "none"`, sem `artist.addOptions`, para não solicitar a busca da discografia inteira ou monitorar automaticamente os outros álbuns novos.

As escolhas de pasta/perfis são usadas no cadastro de um artista novo. O Lidarr conserva as configurações de artistas já existentes; adicionar um álbum não move seus arquivos nem substitui essas preferências.

## Catálogo, biblioteca e downloads

O Lidarr consulta metadata de artistas e lançamentos e procura releases nos indexadores configurados. O download é encaminhado ao cliente configurado no servidor. Uma linha de catálogo não prova disponibilidade de áudio, presença na biblioteca ou conclusão de um download. A pesquisa desta integração recebe nome de artista ou álbum; o lookup não oferece uma busca global de faixas pelo título. Para procurar uma música, use seu artista ou o álbum correspondente. Não há promessa de download de uma faixa isolada do álbum.

Perfis, diretório de biblioteca, indexadores e clientes pertencem à configuração do Lidarr. O RC Music deve informar o que falta e usar valores existentes escolhidos pelo usuário, sem cadastrar fontes ou preencher paths fictícios. A [documentação oficial de início rápido](https://wiki.servarr.com/lidarr/quick-start-guide) explica essa configuração.

## Privacidade e limites

- Endereço e chave são fornecidos no próprio app. A chave fica somente em memória nesta primeira versão e precisa ser informada novamente em outra execução. É enviada no cabeçalho `X-Api-Key`, sem aparecer em URLs, arquivos de preferências, diagnósticos, resultados ou mensagens de erro.
- Pesquisa e mutações exigem ação explícita; não há envio automático ao abrir o painel ou trocar de música.
- Os resultados são texto simples e dados de catálogo, sem execução de HTML ou conteúdo remoto.
- Um timeout de escrita pode deixar o resultado desconhecido; não autoriza repetir automaticamente o comando.
- Cada pedido HTTP tem prazo de **10 s**, resposta limitada a **4 MiB** e lista limitada a **500 resultados**. A conexão faz quatro pedidos sequenciais para estado e opções; a interface permanece assíncrona. Não há redirecionamento automático ou exceção de validação TLS; o uso de HTTP deve ser informado ao conectar.
- Caminhos retornados pelo servidor não autorizam abrir, importar ou apagar arquivos do Windows. Fila e reprodução permanecem sob os casos de uso locais existentes.
- O player deve continuar utilizável quando Lidarr ou rede estiverem indisponíveis. Esta integração não habilita a extensão, o navegador interno ou o pipeline remoto de produção.

## Evidência de implementação

Evidências em `out/product/test-runs/lidarr-integration-20261104/`. Red inicial do serviço **3 passagens/30 falhas**, binding **3/3** e UI recusada por propriedade ainda ausente; os stubs foram substituídos depois dessas execuções. A regressão da seleção pública consumida teve **32/1 → 33/33 Qt**. O teste de timeout expôs um erro no oráculo: o reply já havia recebido DeferredDelete; o teste passou a observar o abort por um registro que sobrevive ao objeto, conservando as exigências de timeout e ausência de retry. O binding terminou **8/8 Qt**, incluindo serviço real sobre QNAM falso, projeção e POST exato.

A UI terminou **12/12 Qt Quick** no arquivo dedicado: confirmação, seleção obsoleta, campos mascarados, texto simples também nas opções do servidor, header com opcionais e geometria mínima **720 × 520**. O primeiro teste de opções falhou por TextFormat ausente; a troca por conteúdo/delegates PlainText corrigiu o caso. O teste de seleção obsoleta encontrou um binding loop; a confirmação agora conserva seu snapshot e desabilita o envio com aviso, sem mutar o snapshot durante o cálculo. Ajustes de espera de delegates corrigiram o harness sem enfraquecer os contratos.

O fechamento passou **263/263 CTest**, sem falhas/skips, em **63,69 s**: **254 testes de core**, serviço/binding Lidarr, letras, presenter, toda a suíte Quick e três jornadas de processo (preferências, sessão e startup). Quick passou **303/303**, incluindo os 12 casos dedicados; letras conservaram **17/17 serviço e 6/6 binding**. Testes de processo usam executável instrumentado/offscreen e temporários próprios, sem instalação ou interação GUI nativa. Isso não aprova acessibilidade nativa ou integração instalada.

Builds Release Lite com testes ON/OFF e Completa experimental OFF passaram; `all_qmllint` terminou sem warnings. Formato de **7 C++ alterados e todos os 67 QML** passou, preservando os seis arquivos AppContainer preexistentes. Os grafos Lite ON/OFF têm **304/275 comandos**, com cliente/binding/painel Lidarr e sem engine; Completa OFF tem **309**, incluindo sua engine existente. Ambos os builds OFF registram zero CTest. Os novos componentes não puxam testes, helpers de Registro ou WFP para o produto e não adicionam um runtime Lidarr.

A conferência real separada enviou somente `GET http://localhost:8686/api/v1/system/status`, sem chave: o servidor respondeu **401**. Isso comprova alcance do endpoint e exigência de autenticação, sem comprovar versão, catálogo, configuração ou downloads. Nenhuma chave foi extraída de arquivos pessoais; não houve POST no servidor real, importação, mudança da fila pessoal, instalação ou regeneração de instaladores. A chave deve ser informada no próprio app para a aceitação autenticada.

Os testes automáticos devem usar respostas próprias e transporte falso, sem internet ou instância pessoal. O smoke com servidor real deve registrar versão, ações e resultados, mantendo credenciais fora da evidência. Instalação, alteração de configuração no servidor e download real exigem seus próprios escopos; não decorrem da simples validação do cliente.

## Referências

- [API oficial Lidarr](https://lidarr.audio/docs/api/).
- [Schema OpenAPI no repositório oficial](https://github.com/Lidarr/Lidarr/blob/develop/src/Lidarr.Api.V1/openapi.json).

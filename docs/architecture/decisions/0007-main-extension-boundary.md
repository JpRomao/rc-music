# ADR-0007: fronteira da extensão principal Chromium

**Estado:** Aceito

**Data:** 2026-09-08

**Escopo:** implementação incremental de R1.7/FR-051–053, incluindo cliente MV3, biblioteca opcional de admissão C++ e host/transporte Windows separados; não escolhe downloader, conversor, engine embarcada ou distribuição.

## Contexto

O [ADR-0006](0006-browser-editions-and-extensions.md) aceita a extensão principal independente de YouTube e compartilhada pelas edições Lite/Completa. Precisamos implementar a seleção de mídia sem transformar conteúdo de páginas em comandos privilegiados nem adicionar acesso permanente ao histórico. A revisão foi registrada como proposta no commit `a42a470`, antes do código; a aceitação técnica ocorre pela adoção no [cliente testado](../../browser-extension.md), não por aprovação de host, ferramentas ou distribuição.

## Decisão

Usar Manifest V3, JavaScript/HTML/CSS próprios sem dependências de runtime ou código remoto, e APIs Chromium 106+ (`documentId`). Brave é o alvo primário; a compatibilidade real continua exigindo smoke isolado autorizado. Node.js já disponível serve somente ao runner `node:test` local; não é empacotado, instalado nem exigido pelo player.

A extensão usa `activeTab`, `scripting` e `nativeMessaging`. O clique abre o popup e autoriza inspeção temporária da página principal; não há `<all_urls>`, cookies, storage, histórico, webRequest, content scripts permanentes ou acesso a arquivos locais. A API oficial permite injeção em mundo isolado e identificação do documento, mesmo após navegações na mesma origem. [activeTab](https://developer.chrome.com/docs/extensions/develop/concepts/activeTab), [scripting](https://developer.chrome.com/docs/extensions/reference/api/scripting).

O detector somente produz candidatos. A página privilegiada do popup valida snapshots, apresenta seleção explícita e chama Native Messaging diretamente. Não há listener genérico de comandos de páginas/service worker; o detector não tem caminho para iniciar downloads. O canal detector aceita apenas a identidade e URL exatas do popup próprio e para ao desconectar. O popup fixa aba/documento/URL, invalida mudanças de navegação/SPA e revalida antes do envio. O [modelo de ameaças](../../security/browser-extension-threat-model.md) define limites e responsabilidades que o host futuro também deverá impor.

Detecção inicial cobre `audio`, `video`, seus `source` e links diretos MP3/MP4/M4A na página principal, com atualização enquanto o popup estiver aberto. Não confundir candidatos com arquivos decodificados ou autorização do titular. Iframes, Shadow DOM aberto/fechado, tráfego anterior ao clique, blob/MSE, playlists adaptativas e conteúdo protegido continuam lacunas explícitas, não suporte universal. Evoluir cobertura depois, sem reduzir o requisito a este primeiro recorte.

O pedido `download.request` v1 contém ID e de 1 a 20 itens com URL HTTP(S), título e tipo de candidato. Não aceita comando, destino no disco, headers, cookies ou opções de executável. Só seleção explícita e confirmação de uso autorizado o produzem. Resposta correlacionada significa aceitação do lote pelo host, nunca download/conversão concluídos. Não retransmitir automaticamente após timeout/desconexão; resultado incerto exige conferir a fila no app antes de repetir.

O nome de protocolo local de desenvolvimento é `com.rcmusic.media`, sem registro no Windows. Na adoção inicial deste ADR ainda não existiam executável de host ou adapter de stdin/stdout; o incremento posterior abaixo implementa ambos sem instalar a ponte. A extensão continua mostrando ausência/erro em vez de fingir sucesso quando não houver host instalado. O protocolo oficial usa JSON UTF-8, prefixo de comprimento de 32 bits em ordem nativa e origem do chamador no primeiro argumento; o adapter Windows usa I/O binário e não emite logs em stdout. Isso não é `OpenSources`, cujo contrato continua estritamente local. [Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).

### Biblioteca nativa opcional de admissão

A [composição CMake própria](../../../cmake/RcMusicBrowserIntegration.cmake), ativada explicitamente por `RC_MUSIC_BUILD_BROWSER_INTEGRATION` e desligada por padrão, separa codec, framing e sessão em uma biblioteca C++/Qt Core. Não conecta essa biblioteca ao player, não acrescenta navegador à Lite e não cria transporte ou download. O [registro do componente](../../browser-extension.md) distingue evidência automatizada de integração ainda pendente.

- `NativeMessageFrame` monta uma única mensagem por sessão com prefixo little-endian do Windows e corpo de 1 byte a 256 KiB. Tamanho e bytes excedentes são verificados antes de copiar/reservar o corpo; prefixos fragmentados, overflow e duas mensagens coalescidas não podem contornar o limite. Um resultado completo entrega o corpo uma só vez.
- O prazo de montagem é total, de 5 s, inclusive na fronteira, medido pelo relógio monotônico fornecido pelo chamador. Novos fragmentos não renovam o prazo; relógio regressivo, entrada posterior ao estado terminal ou EOF incompleto encerram a sessão. Esse prazo não mede download/conversão nem substitui o watchdog independente do host descrito abaixo.
- `NativeDownloadCodec` exige UTF-8 e strings JSON bem formados, escapes válidos, pares de surrogates válidos, chaves únicas após decodificação e profundidade limitada, antes de aplicar schema exato. URLs/títulos e respostas conservam os limites do cliente; validação sintática de hostname não comprova segurança de DNS/redirects.
- `NativeDownloadSession` aceita somente a origem configurada exata no formato `chrome-extension://` + 32 letras `a`–`p` + `/`, sem wildcard, path adicional ou query. A origem permitida vem da composição confiável, nunca do JSON da página; identidade final e instalação ainda não foram definidas. Essa comparação não autentica um processo local do mesmo usuário capaz de invocar o executável com argumentos próprios.
- A porta pura [DownloadSubmission](../../../include/m0/ports/DownloadSubmission.hpp) recebe apenas o lote tipado e faz admissão, não download/conversão síncronos. Lote inválido não chega à porta; ACK exige recibo válido com um ID de job distinto por item. Falha/incerteza não gera ACK fictício nem retry. O futuro serviço precisa assumir o lote independentemente da conexão e deduplicar request IDs entre sessões; essa garantia ainda não está implementada por esta biblioteca.

O incremento posterior do [ADR-0008](0008-download-job-service.md) implementa essa porta com retenção/deduplicação na mesma execução e testa conexões independentes contra o serviço real. Ele não acrescenta transporte de host, persistência entre processos, execução de download ou integração instalada às garantias desta biblioteca.

O recorte assíncrono posterior do mesmo ADR acrescenta `AsyncDownloadSubmission` e overload da sessão: Pending não envia ACK/IDs/mensagem no wire, completion válida mantém resposta v1 e um ID distinto por item. Depois do frame completo, a observação tem prazo total próprio de 5 s inclusivos, sem renovação por poll e com clock regressivo recusado. EOF/entrada extra/timeout fecha somente o observador, sem cancelar/repetir o lote. `DownloadAdmissionController` só publica recibo após Saved matching do checkpoint, permitindo replay de recibos históricos após Loaded válido em novo startup sem reexecutar. O overload síncrono legado não comprova durabilidade; o host abaixo usa exclusivamente a composição assíncrona. Identidade instalada, composição protegida de produção, ownership de mídia e recuperação acionável permanecem gates separados.

### Host e transporte privados Windows

A adoção posterior materializa [RcMusicNativeMessaging.cmake](../../../cmake/RcMusicNativeMessaging.cmake), opt-in por `RC_MUSIC_BUILD_NATIVE_MESSAGING` e OFF por padrão, em componente próprio com Qt Network da toolchain já aceita. A biblioteca Qt Core anterior permanece independente de transporte; nenhum engine ou dependência nova é escolhido. `rc_music_native_host` usa a main real e configuração confiável gerada no build. `RC_MUSIC_NATIVE_MAIN_ORIGIN` e `RC_MUSIC_NATIVE_ENDPOINT` ficam vazios por padrão: o host desabilitado termina em exit 2 antes de `QCoreApplication`, I/O ou conexão. Argumentos, ambiente, página ou JSON nunca fornecem a configuração permitida.

A origem crua do primeiro argumento precisa coincidir exatamente com a configurada. O nome do endpoint aceita somente 1–96 caracteres ASCII alfanuméricos, `_` ou `-`, sem paths. O único argumento adicional aceito é `--parent-window=` com decimal uint64 canônico, tratado como metadata inerte; isso não comprova compatibilidade com a emissão signed intptr_t do Chromium nem HWND/Brave real. Configuração parcial/inválida, origem diferente, flags desconhecidas/duplicadas e tentativas de override falham fechadas.

O canal local dedicado ao dono tem preâmbulo privado fechado de 61 bytes (`RCMDLIPC`, versão 1 e origem exata de 52 bytes), seguido do frame de download v1 little-endian. Não altera eleição, schema, framing ou ACK de `OpenSources`. `QtNativeDownloadServer` solicita `UserAccessOption`, limita backlog/sessões a oito, buffers e prazos, e chama somente `AsyncDownloadSubmission` rápido; não espera store, I/O ou tick do serviço na UI. `Pending` não produz resposta. O dono retém jobs/recibos independentemente dos observadores; shutdown fecha ingresso e conexões sem cancelar/repetir o lote admitido.

`QtNativeDownloadClient` conecta uma única vez, sem retry ou autolaunch do app, e só aceita uma resposta única de até 4.096 bytes após EOF do peer. Codec estrito exige schema, UUID correlacionado e IDs distintos válidos; o encoder tipado conserva URLs/queries sem renormalização. Indisponibilidade anterior à escrita pode devolver `NotAvailable`; falha pós-escrita, timeout ou resposta malformada deixa o resultado incerto, nunca ACK/rejeição inventados. Reentrada em clock/porta não reentra no poll nem declara `stopped` enquanto callback/poll ainda estiver na pilha.

O adapter de stdin/stdout Win32 exige pipes binários, limita bytes disponíveis antes da alocação e escreve um único frame por worker com buffer próprio, prazo de 2 s e cancelamento até parada física reconhecida. O relógio da sessão é relido **após** a leitura potencialmente bloqueante, inclusive em Data/Waiting/Closed; uma leitura que retorna na fronteira do prazo não admite o pedido. Watchdog independente de 15 s cobre stall e teardown não cooperantes, terminando somente o próprio host, nunca o dono persistente ou outros processos. Nenhum dado de página/URL é logado.

Identidade sintética pertence somente a `rc_music_native_host_test` e à [composição instrumentada](../../process-tests.md#composição-instrumentada-do-ingresso-native-messaging), em builds ON separados da configuração vazia de produção. O ensaio usa host/IPC/serviços/UI/stores/workers reais, mas somente publicação MP3 sintética; não há rede/conversão nesse fluxo. Produção segue sem ingresso/serviço de downloads ativo. Origem em argv e pipe do mesmo usuário não autenticam o navegador; primeira instância/colisão do pipe Windows, identidade instalada, registro/upgrade/ownership e Brave/Chromium reais continuam gates explícitos do [modelo de ameaças](../../security/browser-extension-threat-model.md).

## Segurança, licenças e distribuição

Somente código próprio, sem copiar exemplos ou empacotar bibliotecas novas; licença pública do projeto e publicação continuam pendentes. A biblioteca de admissão reutiliza Qt Core e o transporte separado reutiliza Qt Network da mesma toolchain aceita pelo ADR-0005, mantendo seus gates de licença/notices/distribuição; não aprova outra biblioteca, downloader ou conversor. Não instalar extensão, registrar host, mudar políticas do navegador ou baixar ferramentas neste incremento. Atualizações de código serão explícitas, sem mecanismo próprio de auto-update ou execução remota. O app permanece offline e sem nova dependência obrigatória.

Dados de página ficam efêmeros no popup/detector; apenas mídia selecionada vai ao host local. Sem logs de URLs, query strings visíveis, persistência de navegação ou cookies. A política de uso limitado exige uso necessário à função declarada; submissão futura exige divulgação/privacidade próprias e revisão, não aprovação implícita desta decisão. [Limited Use](https://developer.chrome.com/docs/webstore/program-policies/limited-use).

Domínios identificados como YouTube são recusados tanto na página quanto na mídia candidata. Não oferecer encaminhamento à extensão separada. A Chrome Web Store lista facilitar download de YouTube como causa de rejeição; filtros não garantem aprovação nem identificam todo proxy arbitrário. O host deve revalidar destinos/redirects e não pode confiar no filtro do cliente. [Troubleshooting oficial](https://developer.chrome.com/docs/webstore/troubleshooting/).

## Alternativas e consequências

- Acesso permanente a todos os sites: desnecessário para começar a seleção autorizada, aumenta exposição; futura detecção de rede exige decisão própria.
- Servidor HTTP localhost: rejeitado; amplia a superfície e conflita com ADR-0006.
- Service worker roteando comandos genéricos: desnecessário para o primeiro fluxo; o popup pode chamar o canal nativo diretamente, reduzindo fronteiras de mensagens.
- Framework web/build npm: desnecessário para uma lista e ações; manter módulos pequenos e testes nativos de Node.

Fechar o popup encerra detecção e pode impedir observar uma resposta nativa; não cancela trabalho que o dono já admitiu. Serviço/painel têm contratos de fila/status/cancelamento independentes, ainda sem execução remota protegida no produto. A integração completa só estará pronta com host instalado, download/conversão, importação e instaladores validados nas duas edições.

## Critérios de aceitação e revisão

TDD cobre descoberta/deduplicação/limites, limpeza de listeners, remetentes falsos, navegação, múltipla seleção e envio exclusivo de selecionados, contratos estritos/ACK, timeout/desconexão sem retry e texto hostil sem HTML. Checks devem executar código de produção com DOM/Chrome/relógio falsos, sem rede ou perfis pessoais. Testes fake não provam APIs carregadas pelo Brave, CSP real ou usabilidade visual nativa; esses smokes permanecem separados.

A biblioteca nativa exige Qt Test sem UI/rede com bytes hostis, todas as divisões do prefixo/corpo, prazo/clock determinísticos, origem configurada, schema/UTF-8/escapes/chaves duplicadas e porta de admissão falsa. Testar que mensagens inválidas não despacham e que exceções/recibos incorretos não fabricam aceitação. Checks verdes dessa biblioteca, isoladamente, não provam executável, I/O binário, registro, identidade instalada, idempotência entre conexões, download ou release.

Os testes adicionais do host/transporte usam pipes reais, relógios controlados, peer local falso e processos próprios para observar silêncio/recusa antes de IPC, framing fragmentado, EOF/deadline, resposta estrita/única e incerteza sem retry. Helpers do watchdog provam finish, stall e stall em teardown. A composição adicional usa quatro hosts reais em dois boots, repetindo o mesmo UUID duas vezes em cada boot; leitores independentes verificam IDs/recibos após cada resposta e os dois documentos após NormalExit. Importação sem autoplay, reprodução explícita e Imported com restore=false não podem depender de executor de rede. Evidência focada e seus limites ficam em [process-tests](../../process-tests.md) e na [estratégia de testes](../../quality/testing-strategy.md); suíte global, instalação e smokes Brave permanecem separados.

Revisar por lacunas de detecção que exijam permissões maiores, risco de origem/SSRF, mudanças de API, distribuição, identidade final das extensões/host ou necessidade de fila persistente no navegador. Não aceitar ferramentas de download/conversão ou a engine da Completa por associação.
